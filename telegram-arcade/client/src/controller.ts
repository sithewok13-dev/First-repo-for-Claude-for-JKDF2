// Glue between the server connection, the lockstep runner, local input and
// the UI store.

import { api, signIn, getToken } from './api.ts';
import { Runner, type SessionInfo } from './emu/runner.ts';
import { InputHub } from './input/input.ts';
import { Net } from './net.ts';
import { store, type RoomEvent } from './store.ts';
import { haptic, initTelegram, onActiveChange, setClosingConfirmation } from './telegram.ts';
import { DEFAULT_LAYOUT, type TouchLayout } from './ui/touch.tsx';

const wsUrl = `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`;
export const net = new Net(wsUrl, getToken);
export const runner = new Runner(net);
export const input = new InputHub();

export interface UserSettings {
  keys?: Record<string, number>;
  pad?: Record<number, number>;
  padIndex?: number | null;
  touch?: TouchLayout;
  touchEnabled?: 'auto' | 'on' | 'off';
  volume?: number;
  muted?: boolean;
  scale?: 'fit' | 'integer';
  smoothing?: boolean;
  chatSide?: 'auto' | 'right' | 'bottom';
}

export let settings: UserSettings = {};
let settingsTimer = 0;

export function saveSettings(patch: Partial<UserSettings>): void {
  settings = { ...settings, ...patch };
  applySettings();
  clearTimeout(settingsTimer);
  settingsTimer = window.setTimeout(() => {
    void api('/api/settings', { method: 'PUT', body: JSON.stringify(settings) }).catch(() => {});
  }, 600);
  store.set({});
}

function applySettings(): void {
  if (settings.keys) input.keys = { ...settings.keys };
  if (settings.pad) input.padMap = { ...settings.pad };
  input.padIndex = settings.padIndex ?? null;
  runner.audio.setVolume(settings.volume ?? 0.8);
  runner.audio.setMuted(settings.muted ?? false);
}

export function touchLayout(): TouchLayout {
  return settings.touch ?? DEFAULT_LAYOUT;
}

// Events that describe game outcomes carry a frame number; they are shown
// only once this device has played that frame, so a spectator's view is
// never spoiled by text that arrives ahead of the picture.
let pendingEvents: RoomEvent[] = [];

function pushEvent(ev: RoomEvent): void {
  const frame = runner.core?.frame ?? null;
  if (ev.frame !== null && frame !== null && store.state.stats?.phase === 'running' && frame < ev.frame) {
    pendingEvents.push(ev);
    return;
  }
  store.set({ events: [...store.state.events.slice(-30), ev] });
}

runner.onFrameEvent = (f) => {
  if (!pendingEvents.length) return;
  const due = pendingEvents.filter((e) => (e.frame ?? 0) <= f);
  if (!due.length) return;
  pendingEvents = pendingEvents.filter((e) => (e.frame ?? 0) > f);
  store.set({ events: [...store.state.events.slice(-30), ...due] });
};

function handleJson(msg: any): void {
  const s = store.state;
  switch (msg.t) {
    case 'room':
      store.set({ room: msg.state });
      break;
    case 'you': {
      const seat = msg.seat;
      // Only the controlling device of a seated player sends input; another
      // tab of the same person watches until "Play here" is pressed.
      const controlling = seat && seat.controlConn === net.connId;
      const prevPort = runner.myPort;
      runner.setSeat(controlling ? seat.port : null, seat?.epoch ?? 0);
      if (prevPort !== runner.myPort) input.releaseAll();
      setClosingConfirmation(!!seat);
      if (msg.offer && !s.you?.offer) haptic('warning');
      store.set({ you: msg });
      break;
    }
    case 'session':
      if (s.session?.sessionId !== msg.sessionId) {
        pendingEvents = [];
        store.set({ session: msg as SessionInfo, events: [] });
        void runner.load(msg as SessionInfo);
      }
      break;
    case 'chat_history':
      store.set({ chat: msg.msgs ?? [] });
      break;
    case 'chat':
      store.set({ chat: [...s.chat.slice(-199), msg.msg], unread: s.chatOpen || s.tab !== 'cabinet' ? s.unread : s.unread + 1 });
      break;
    case 'chat_delete':
      store.set({ chat: s.chat.filter((c) => c.id !== msg.id) });
      break;
    case 'event':
      pushEvent({ kind: msg.kind, text: msg.text, frame: msg.frame ?? null, at: msg.at });
      break;
    case 'notice':
      store.set({ notice: msg.text });
      break;
    case 'opt_in':
      store.set({ optIn: { gameId: msg.gameId, title: msg.title, closesAt: msg.closesAt } });
      break;
    case 'offer':
      haptic('warning');
      break;
    case 'error':
      store.set({ notice: msg.error });
      break;
  }
}

export async function cmd(op: string, args: Record<string, unknown> = {}): Promise<boolean> {
  const r = await net.cmd(op, args);
  if (!r.ok) store.set({ notice: r.error ?? 'That did not work.' });
  return r.ok;
}

export function coin(): void {
  void cmd('coin');
}

export async function boot(): Promise<void> {
  initTelegram();
  let me;
  try {
    me = await signIn();
  } catch (e) {
    store.set({ error: (e as Error).message });
    return;
  }
  store.set({ me: me.user, group: me.group, tgRole: me.tgRole });
  try {
    const m = await api<any>('/api/me');
    settings = m.settings ?? {};
    applySettings();
  } catch { /* defaults */ }
  net.onStatus = (st, d) => {
    store.set({ conn: st, connDetail: d ?? '' });
    if (st !== 'open') input.releaseAll();
    if (st === 'open' && runner.core) runner.resync();
  };
  net.onJson = handleJson;
  net.onBin = (d) => void runner.onBinary(d);
  runner.onStats = (st) => store.set({ stats: st });
  input.onMask = (m) => runner.sendMask(m);
  input.onCoin = coin;
  input.attach();
  net.connect();
  // Background, phone call, screen lock, app switch: release everything and
  // tell the server (it zeroes the port too).
  onActiveChange((active) => {
    if (!active) {
      input.releaseAll();
      net.send({ t: 'blur' });
    } else {
      runner.audio.unlock();
    }
  });
  const unlock = () => runner.audio.unlock();
  window.addEventListener('pointerdown', unlock, { capture: true });
  window.addEventListener('keydown', unlock, { capture: true });
  setInterval(() => net.ping(), 5000);
  (window as any).__arcade = { runner, net, input, store };
}
