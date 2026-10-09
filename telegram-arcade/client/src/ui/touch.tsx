// Multi-touch controls: an 8-way stick/D-pad and one-to-one buttons.
// Each finger presses exactly one control (no two-button-per-touch tricks).
// Groups can be moved and resized in edit mode; layouts are saved per user.

import { useEffect, useRef, useState } from 'preact/hooks';
import { PAD } from '../../../shared/emu/core.ts';

export interface TouchButton {
  bit: number;
  label: string;
}

export interface GroupPos {
  x: number;      // 0..1 of the overlay width (group center)
  y: number;      // 0..1 of the overlay height
  scale: number;  // 0.6..1.8
}

export interface TouchLayout {
  dpad: GroupPos;
  buttons: GroupPos;
  start: GroupPos;
  stick: 'dpad' | 'stick';
  opacity: number;
}

export const DEFAULT_LAYOUT: TouchLayout = {
  dpad: { x: 0.14, y: 0.72, scale: 1 },
  buttons: { x: 0.85, y: 0.7, scale: 1 },
  start: { x: 0.5, y: 0.93, scale: 1 },
  stick: 'dpad',
  opacity: 0.55,
};

interface Props {
  buttons: TouchButton[];          // right-hand buttons in display order
  showSelect: boolean;
  layout: TouchLayout;
  editing: boolean;
  onMask: (mask: number) => void;
  onLayout: (l: TouchLayout) => void;
}

const DIR_BITS = [1 << PAD.RIGHT, (1 << PAD.RIGHT) | (1 << PAD.DOWN), 1 << PAD.DOWN, (1 << PAD.DOWN) | (1 << PAD.LEFT), 1 << PAD.LEFT, (1 << PAD.LEFT) | (1 << PAD.UP), 1 << PAD.UP, (1 << PAD.UP) | (1 << PAD.RIGHT)];

// Button grid: 2 -> one row, 4 -> diamond-ish 2x2, 6 -> 2 rows of 3 (fighters).
function gridFor(n: number): { cols: number } {
  if (n <= 2) return { cols: 2 };
  if (n <= 4) return { cols: 2 };
  return { cols: 3 };
}

export function TouchControls(p: Props) {
  const root = useRef<HTMLDivElement>(null);
  const pointers = useRef(new Map<number, { kind: 'dpad' | 'btn' | 'drag'; bit?: number; dir?: number; group?: keyof TouchLayout; ox?: number; oy?: number }>());
  const [pressed, setPressed] = useState(0);
  const [dir, setDir] = useState(-1);

  const recompute = () => {
    let m = 0;
    let d = -1;
    for (const v of pointers.current.values()) {
      if (v.kind === 'btn' && v.bit !== undefined) m |= 1 << v.bit;
      if (v.kind === 'dpad' && v.dir !== undefined && v.dir >= 0) {
        m |= DIR_BITS[v.dir];
        d = v.dir;
      }
    }
    setPressed(m);
    setDir(d);
    p.onMask(m);
  };

  useEffect(() => () => p.onMask(0), []);

  const dpadDir = (el: HTMLElement, x: number, y: number): number => {
    const r = el.getBoundingClientRect();
    const dx = x - (r.left + r.width / 2);
    const dy = y - (r.top + r.height / 2);
    if (Math.hypot(dx, dy) < r.width * 0.12) return -1; // dead zone
    const a = Math.atan2(dy, dx);
    return ((Math.round(a / (Math.PI / 4)) % 8) + 8) % 8;
  };

  const buttonAt = (x: number, y: number): number | undefined => {
    const el = document.elementFromPoint(x, y) as HTMLElement | null;
    const b = el?.closest('[data-bit]') as HTMLElement | null;
    return b ? Number(b.dataset.bit) : undefined;
  };

  const onDown = (e: PointerEvent) => {
    const t = e.target as HTMLElement;
    e.preventDefault();
    (e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId);
    if (p.editing) {
      const g = t.closest('[data-group]') as HTMLElement | null;
      if (!g) return;
      const r = root.current!.getBoundingClientRect();
      const key = g.dataset.group as keyof TouchLayout;
      const pos = p.layout[key] as GroupPos;
      pointers.current.set(e.pointerId, { kind: 'drag', group: key, ox: e.clientX - (r.left + pos.x * r.width), oy: e.clientY - (r.top + pos.y * r.height) });
      return;
    }
    const dpadEl = t.closest('[data-dpad]') as HTMLElement | null;
    if (dpadEl) {
      pointers.current.set(e.pointerId, { kind: 'dpad', dir: dpadDir(dpadEl, e.clientX, e.clientY) });
    } else {
      const bit = buttonAt(e.clientX, e.clientY);
      if (bit === undefined) return;
      pointers.current.set(e.pointerId, { kind: 'btn', bit });
    }
    navigator.vibrate?.(8);
    recompute();
  };

  const onMove = (e: PointerEvent) => {
    const v = pointers.current.get(e.pointerId);
    if (!v) return;
    e.preventDefault();
    if (v.kind === 'drag' && v.group) {
      const r = root.current!.getBoundingClientRect();
      const pos = { ...(p.layout[v.group] as GroupPos) };
      pos.x = Math.min(0.97, Math.max(0.03, (e.clientX - v.ox! - r.left) / r.width));
      pos.y = Math.min(0.97, Math.max(0.03, (e.clientY - v.oy! - r.top) / r.height));
      p.onLayout({ ...p.layout, [v.group]: pos });
      return;
    }
    if (v.kind === 'dpad') {
      const el = root.current!.querySelector('[data-dpad]') as HTMLElement;
      const d = dpadDir(el, e.clientX, e.clientY);
      if (d !== v.dir) {
        v.dir = d;
        recompute();
      }
    } else if (v.kind === 'btn') {
      // sliding a finger onto another button moves the press (one button per finger)
      const bit = buttonAt(e.clientX, e.clientY);
      if (bit !== undefined && bit !== v.bit) {
        v.bit = bit;
        recompute();
      }
    }
  };

  const onUp = (e: PointerEvent) => {
    if (!pointers.current.has(e.pointerId)) return;
    pointers.current.delete(e.pointerId);
    recompute();
  };

  // Any interruption releases every touch.
  useEffect(() => {
    const cancel = () => {
      pointers.current.clear();
      recompute();
    };
    window.addEventListener('blur', cancel);
    document.addEventListener('visibilitychange', cancel);
    return () => {
      window.removeEventListener('blur', cancel);
      document.removeEventListener('visibilitychange', cancel);
    };
  }, []);

  const group = (key: 'dpad' | 'buttons' | 'start', child: preact.ComponentChildren) => {
    const pos = p.layout[key];
    return (
      // Keep groups clear of the screen edges: on iOS the leftmost ~30 pt
      // start Telegram's own back/dismiss gesture.
      <div class={`tc-group tc-${key}${p.editing ? ' tc-edit' : ''}`} data-group={key}
        style={{ left: `clamp(${34 + 80 * pos.scale}px, ${pos.x * 100}%, calc(100% - ${16 + 80 * pos.scale}px))`, top: `${pos.y * 100}%`, transform: `translate(-50%, -50%) scale(${pos.scale})` }}>
        {child}
        {p.editing && (
          <div class="tc-scale">
            <button onPointerDown={(e) => { e.stopPropagation(); p.onLayout({ ...p.layout, [key]: { ...pos, scale: Math.max(0.6, pos.scale - 0.1) } }); }}>−</button>
            <button onPointerDown={(e) => { e.stopPropagation(); p.onLayout({ ...p.layout, [key]: { ...pos, scale: Math.min(1.8, pos.scale + 0.1) } }); }}>+</button>
          </div>
        )}
      </div>
    );
  };

  const { cols } = gridFor(p.buttons.length);
  return (
    <div ref={root} class="touch-overlay" style={{ opacity: p.editing ? 0.9 : p.layout.opacity }}
      onPointerDown={onDown as any} onPointerMove={onMove as any} onPointerUp={onUp as any} onPointerCancel={onUp as any}
      onContextMenu={(e) => e.preventDefault()}>
      {group('dpad', (
        <div class={`tc-dpad ${p.layout.stick === 'stick' ? 'tc-stick' : ''}`} data-dpad>
          {p.layout.stick === 'stick'
            ? <div class="tc-knob" style={dir >= 0 ? { transform: `translate(${Math.cos(dir * Math.PI / 4) * 28}px, ${Math.sin(dir * Math.PI / 4) * 28}px)` } : undefined} />
            : ['up', 'right', 'down', 'left'].map((d) => <div key={d} class={`tc-arrow tc-${d}${pressed & (1 << (PAD as any)[d.toUpperCase()]) ? ' on' : ''}`} />)}
        </div>
      ))}
      {group('buttons', (
        <div class="tc-buttons" style={{ gridTemplateColumns: `repeat(${cols}, 1fr)` }}>
          {p.buttons.map((b) => (
            <div key={b.bit} class={`tc-btn${pressed & (1 << b.bit) ? ' on' : ''}`} data-bit={b.bit}><span>{b.label}</span></div>
          ))}
        </div>
      ))}
      {group('start', (
        <div class="tc-start-row">
          {p.showSelect && <div class={`tc-small${pressed & (1 << PAD.SELECT) ? ' on' : ''}`} data-bit={PAD.SELECT}>SELECT</div>}
          <div class={`tc-small${pressed & (1 << PAD.START) ? ' on' : ''}`} data-bit={PAD.START}>START</div>
        </div>
      ))}
    </div>
  );
}
