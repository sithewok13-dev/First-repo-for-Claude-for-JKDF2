// Local controller state: keyboard, gamepads and touch are merged into one
// 16-bit RetroPad mask (one physical button -> one emulated button; no turbo,
// no macros). Every source is released on blur, app switch, chat focus,
// disconnect and seat change, so nothing stays held.

import { PAD } from '../../../shared/emu/core.ts';

export type Source = 'keyboard' | 'gamepad' | 'touch';

export interface KeyMap { [code: string]: number }   // KeyboardEvent.code -> PAD bit
export interface PadMap { [button: number]: number } // standard gamepad button -> PAD bit

export const DEFAULT_KEYS: KeyMap = {
  ArrowUp: PAD.UP, ArrowDown: PAD.DOWN, ArrowLeft: PAD.LEFT, ArrowRight: PAD.RIGHT,
  KeyA: PAD.Y, KeyS: PAD.X, KeyD: PAD.L,
  KeyZ: PAD.B, KeyX: PAD.A, KeyC: PAD.R,
  Enter: PAD.START, ShiftRight: PAD.SELECT, Backspace: PAD.SELECT,
};

// W3C "standard" gamepad layout -> RetroPad (position-based, like libretro).
export const DEFAULT_PAD: PadMap = {
  0: PAD.B, 1: PAD.A, 2: PAD.Y, 3: PAD.X,
  4: PAD.L, 5: PAD.R, 6: PAD.L2, 7: PAD.R2,
  8: PAD.SELECT, 9: PAD.START, 10: PAD.L3, 11: PAD.R3,
  12: PAD.UP, 13: PAD.DOWN, 14: PAD.LEFT, 15: PAD.RIGHT,
};

export const COIN_KEY = 'Digit5';

function isTyping(t: EventTarget | null): boolean {
  const el = t as HTMLElement | null;
  if (!el || !el.tagName) return false;
  return el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT' || el.isContentEditable;
}

export interface PadInfo {
  index: number;
  id: string;
  mapping: string;
}

export class InputHub {
  private kb = 0;
  private pad = 0;
  private touch = 0;
  private held = new Set<string>();
  private last = -1;
  keys: KeyMap = { ...DEFAULT_KEYS };
  padMap: PadMap = { ...DEFAULT_PAD };
  deadzone = 0.5;
  enabled = true;                 // false while a dialog or chat has focus
  padIndex: number | null = null; // chosen controller (null = first connected)
  pads: PadInfo[] = [];
  onMask: (mask: number) => void = () => {};
  onCoin: () => void = () => {};
  onPads: (pads: PadInfo[]) => void = () => {};
  onRawKey: ((code: string) => boolean) | null = null;   // remapping capture
  onRawPad: ((button: number) => boolean) | null = null;
  private raf = 0;
  private prevButtons: boolean[] = [];

  attach(): void {
    window.addEventListener('keydown', this.keydown, { capture: true });
    window.addEventListener('keyup', this.keyup, { capture: true });
    window.addEventListener('gamepadconnected', this.refreshPads);
    window.addEventListener('gamepaddisconnected', this.refreshPads);
    const loop = () => {
      this.raf = requestAnimationFrame(loop);
      this.pollPads();
    };
    this.raf = requestAnimationFrame(loop);
    this.refreshPads();
  }

  detach(): void {
    window.removeEventListener('keydown', this.keydown, { capture: true });
    window.removeEventListener('keyup', this.keyup, { capture: true });
    window.removeEventListener('gamepadconnected', this.refreshPads);
    window.removeEventListener('gamepaddisconnected', this.refreshPads);
    cancelAnimationFrame(this.raf);
  }

  private emit(): void {
    const m = this.enabled ? (this.kb | this.pad | this.touch) & 0xffff : 0;
    if (m !== this.last) {
      this.last = m;
      this.onMask(m);
    }
  }

  get mask(): number {
    return this.last < 0 ? 0 : this.last;
  }

  // Release everything (focus loss, chat, app switch, seat change).
  releaseAll(): void {
    this.held.clear();
    this.kb = 0;
    this.pad = 0;
    this.touch = 0;
    this.emit();
  }

  setEnabled(on: boolean): void {
    this.enabled = on;
    if (!on) this.releaseAll();
    else this.emit();
  }

  setTouch(mask: number): void {
    this.touch = mask;
    this.emit();
  }

  private recomputeKb(): void {
    let m = 0;
    for (const code of this.held) {
      const b = this.keys[code];
      if (b !== undefined) m |= 1 << b;
    }
    this.kb = m;
  }

  private keydown = (e: KeyboardEvent) => {
    if (isTyping(e.target)) return;      // chat typing never reaches the game
    if (e.ctrlKey || e.metaKey || e.altKey) return; // leave browser shortcuts alone
    if (this.onRawKey && this.onRawKey(e.code)) {
      e.preventDefault();
      return;
    }
    if (e.code === COIN_KEY && !e.repeat) {
      this.onCoin();
      e.preventDefault();
      return;
    }
    if (this.keys[e.code] === undefined) return;
    e.preventDefault();                  // no page scrolling with arrows/space
    if (e.repeat) return;
    this.held.add(e.code);
    this.recomputeKb();
    this.emit();
  };

  private keyup = (e: KeyboardEvent) => {
    if (!this.held.has(e.code)) return;
    this.held.delete(e.code);
    this.recomputeKb();
    this.emit();
  };

  private refreshPads = () => {
    const list: PadInfo[] = [];
    const gp = navigator.getGamepads ? navigator.getGamepads() : [];
    for (const g of gp) if (g && g.connected) list.push({ index: g.index, id: g.id, mapping: g.mapping });
    this.pads = list;
    if (this.padIndex !== null && !list.some((p) => p.index === this.padIndex)) {
      this.pad = 0;
      this.emit();
    }
    this.onPads(list);
  };

  private pollPads(): void {
    if (!navigator.getGamepads) return;
    const all = navigator.getGamepads();
    let g: Gamepad | null = null;
    if (this.padIndex !== null) g = all[this.padIndex] ?? null;
    else for (const x of all) if (x && x.connected) { g = x; break; }
    if (!g) {
      if (this.pad) {
        this.pad = 0;
        this.emit();
      }
      return;
    }
    let m = 0;
    g.buttons.forEach((b, i) => {
      const pressed = b.pressed || b.value > 0.5;
      if (pressed && !this.prevButtons[i] && this.onRawPad && this.onRawPad(i)) return;
      this.prevButtons[i] = pressed;
      const bit = this.padMap[i];
      if (pressed && bit !== undefined) m |= 1 << bit;
    });
    const [x = 0, y = 0] = g.axes;
    if (x < -this.deadzone) m |= 1 << PAD.LEFT;
    if (x > this.deadzone) m |= 1 << PAD.RIGHT;
    if (y < -this.deadzone) m |= 1 << PAD.UP;
    if (y > this.deadzone) m |= 1 << PAD.DOWN;
    if (m !== this.pad) {
      this.pad = m;
      this.emit();
    }
  }

  // Raw state for the controller test screen.
  padSnapshot(): { id: string; buttons: number[]; axes: number[] } | null {
    if (!navigator.getGamepads) return null;
    const all = navigator.getGamepads();
    const g = this.padIndex !== null ? all[this.padIndex] : [...all].find((x) => x && x.connected);
    if (!g) return null;
    return { id: g.id, buttons: g.buttons.map((b) => b.value), axes: [...g.axes] };
  }
}
