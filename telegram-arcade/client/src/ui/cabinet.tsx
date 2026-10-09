// The cabinet: game view, players and queue, contextual actions, touch
// controls and chat.

import { useEffect, useRef, useState } from 'preact/hooks';
import { PAD } from '../../../shared/emu/core.ts';
import { cmd, coin, input, net, runner, saveSettings, settings, touchLayout } from '../controller.ts';
import { store, useStore } from '../store.ts';
import { canFullscreen, canLockOrientation, lockLandscape, toggleFullscreen } from '../telegram.ts';
import { Chat } from './chat.tsx';
import { TouchControls, type TouchButton } from './touch.tsx';

const MODE_LABEL: Record<string, string> = {
  versus: 'Versus · winner stays',
  coop: 'Co-op · rotate at game over',
  single: 'Single player · pass at game over',
  collab: 'Collaborative · pass the controller',
  'turns-shared': 'Turn-based · shared controller',
  'turns-multi': 'Turn-based · own controllers',
};

const isTouchDevice = () => matchMedia('(pointer: coarse)').matches || 'ontouchstart' in window;

function countdown(until: number | null | undefined): string {
  if (!until) return '';
  return `${Math.max(0, Math.ceil((until - Date.now()) / 1000))}s`;
}

function useTicker(ms = 1000): void {
  const [, set] = useState(0);
  useEffect(() => {
    const t = setInterval(() => set((n) => n + 1), ms);
    return () => clearInterval(t);
  }, [ms]);
}

// Buttons shown on touch controls: from the core's per-game descriptors
// (e.g. FBNeo "Weak Punch") limited to bits the server allows.
function touchButtons(): { buttons: TouchButton[]; select: boolean } {
  const s = store.state.session;
  const port = runner.myPort ?? 0;
  if (!s) return { buttons: [], select: false };
  const allowed = s.allowed?.[port] ?? 0;
  const names = new Map<number, string>();
  for (const d of s.descriptors ?? []) if (d.port === port && (d.device & 0xff) === 1) names.set(d.id, d.text);
  const order = s.system === 'fceumm' ? [PAD.B, PAD.A] : [PAD.Y, PAD.X, PAD.L, PAD.B, PAD.A, PAD.R];
  const short = (t: string, fallback: string) => {
    const m = t.match(/(weak|medium|strong|light|heavy|fierce|roundhouse)\s+(punch|kick)/i);
    if (m) return (m[1][0] + m[2][0]).toUpperCase();
    const b = t.match(/button\s*([a-z0-9]+)/i) || t.match(/fire\s*([0-9]+)/i);
    if (b) return b[1].toUpperCase();
    return (t.split(' ').slice(-1)[0] || fallback).slice(0, 5);
  };
  const fallbackName: Record<number, string> = { [PAD.B]: 'B', [PAD.A]: 'A', [PAD.Y]: 'Y', [PAD.X]: 'X', [PAD.L]: 'L', [PAD.R]: 'R' };
  const buttons = order.filter((b) => allowed & (1 << b)).map((b) => ({ bit: b, label: short(names.get(b) ?? '', fallbackName[b]) }));
  return { buttons, select: s.system === 'fceumm' && !!(allowed & (1 << PAD.SELECT)) };
}

function Seats() {
  const st = useStore();
  const room = st.room;
  if (!room) return null;
  useTicker();
  const meId = st.me?.id;
  return (
    <div class="seats">
      {room.seats.map((seat: any) => (
        <div key={seat.port} class={`seat seat-${seat.state}${seat.userId === meId ? ' seat-me' : ''}`}>
          <span class="seat-port">P{seat.port + 1}</span>
          <span class="seat-name">
            {seat.state === 'offered' ? `offered to ${room.members.find((m: any) => m.userId === seat.offeredTo)?.name ?? '…'} (${countdown(seat.offerExpiresAt)})`
              : seat.name ?? 'open'}
          </span>
          {seat.state === 'grace' && <span class="badge warn">reconnecting</span>}
          {room.game?.status?.players?.[seat.port] && seat.userId !== null && (
            <span class="seat-status">{statusText(room.game.status.players[seat.port])}</span>
          )}
        </div>
      ))}
      {room.participants?.length > 0 && (
        <div class="turn-order">Turn order: {room.participants.map((p: any, i: number) => <span key={p.userId} class={p.turn ? 'turn-now' : ''}>{i + 1}. {p.name}</span>)}</div>
      )}
    </div>
  );
}

function statusText(p: any): string {
  const bits: string[] = [];
  if (p.phase === 'continue') bits.push(`continue? ${p.continueSeconds ?? ''}`);
  if (p.phase === 'out') bits.push('waiting');
  if (p.lives !== null && p.phase === 'playing') bits.push(`♥${p.lives}`);
  if (p.score !== null) bits.push(String(p.score));
  return bits.join(' · ');
}

function Queue() {
  const st = useStore();
  const room = st.room;
  if (!room) return null;
  const q = room.queue as any[];
  return (
    <div class="queue">
      <span class="label">Queue</span>
      {q.length === 0 ? <span class="muted">nobody waiting</span> : q.map((e) => (
        <span key={e.userId} class={`q-entry${e.userId === st.me?.id ? ' me' : ''}`}>{e.position}. {e.name}{e.position === 1 ? ' (next)' : ''}</span>
      ))}
    </div>
  );
}

function Actions() {
  const st = useStore();
  const room = st.room, you = st.you;
  if (!room || !you || !room.game) return null;
  const mode = room.game.mode;
  const caps = room.game.adapter?.capabilities ?? {};
  const seated = !!you.seat;
  const controlling = seated && you.seat.controlConn === net.connId;
  const queued = you.queuePosition !== null;
  const anyFree = room.seats.some((s: any) => s.state === 'empty');
  const b = (label: string, op: string, args: Record<string, unknown> = {}, cls = '') => (
    <button class={`btn ${cls}`} onClick={() => void cmd(op, args)}>{label}</button>
  );
  return (
    <div class="actions">
      {!seated && mode !== 'turns-shared' && anyFree && room.queue.length === 0 && b('Take a seat', 'seat.take', {}, 'primary')}
      {!seated && !queued && mode !== 'turns-shared' && (room.queue.length > 0 || !anyFree) && b('Join queue', 'queue.join', {}, 'primary')}
      {queued && b(`Leave queue (#${you.queuePosition})`, 'queue.leave')}
      {mode === 'turns-shared' && !you.participant && b('Join turn order', 'turns.join', {}, 'primary')}
      {mode === 'turns-shared' && you.participant && b('Leave turn order', 'turns.leave')}
      {seated && !controlling && b('Play on this device', 'seat.control_here', {}, 'primary')}
      {seated && room.game.coin && <button class="btn coin" onClick={coin} title="Key: 5">Insert coin</button>}
      {seated && (mode === 'single' || mode === 'collab') && b('Pass controller', 'controller.pass')}
      {seated && mode === 'turns-shared' && !caps.turnOwner && b('End turn', 'turns.end', {}, 'primary')}
      {seated && mode === 'versus' && !caps.matchResult && (
        <span class="report">
          <span class="label">Report match:</span>
          {b('I won', 'result.report', { outcome: 'won' })}
          {b('I lost', 'result.report', { outcome: 'lost' })}
          {b('Draw', 'result.report', { outcome: 'draw' })}
        </span>
      )}
      {seated && (mode === 'coop' || mode === 'single') && !caps.playerGameOver && b("I'm out (game over)", 'player.out')}
      {seated && b('Leave seat', 'seat.leave', {}, 'subtle')}
    </div>
  );
}

function Capabilities() {
  const st = useStore();
  const g = st.room?.game;
  if (!g) return null;
  const caps = g.adapter?.capabilities;
  const line = (ok: boolean | undefined, yes: string, no: string) => <li class={ok ? 'ok' : 'manual'}>{ok ? '✓ ' + yes : '✎ ' + no}</li>;
  return (
    <details class="caps">
      <summary>{MODE_LABEL[g.mode] ?? g.mode} · {caps ? `adapter ${g.adapter.id} v${g.adapter.version}` : 'no verified adapter'}</summary>
      <ul>
        {g.mode === 'versus' && line(caps?.matchResult, 'Match results detected automatically', 'Players report results; host/deputy decides disputes')}
        {(g.mode === 'coop' || g.mode === 'single') && line(caps?.playerGameOver, 'Game over detected automatically', 'Seated player taps "I\'m out" at game over')}
        {(g.mode === 'coop') && line(caps?.stageBoundary, 'Stage boundaries detected (safe switch points)', 'Game switches use an announced countdown')}
        {g.mode === 'turns-shared' && line(caps?.turnOwner, 'Turns detected automatically', 'Tap "End turn"; the next player accepts the controller')}
        {line(caps?.score, 'Scores recorded automatically', 'Scores are not recorded for this game')}
        {g.coin && line(caps?.credits, 'Coins are inserted only when needed (no banking credits)', 'Coins rate-limited; queue rotation is manual')}
      </ul>
    </details>
  );
}

function OfferDialog() {
  const st = useStore();
  const offer = st.you?.offer;
  useTicker(250);
  if (!offer) return null;
  const what = offer.kind === 'seat' ? `Player ${offer.port + 1} is yours` : offer.kind === 'turn' ? 'Your turn: take the controller' : 'You are offered the controller';
  return (
    <div class="modal">
      <div class="modal-card">
        <h3>{what}</h3>
        <p>{offer.stuck ? 'Everyone is waiting for you.' : `Accept within ${countdown(offer.expiresAt)}.`}</p>
        <div class="row">
          <button class="btn primary" onClick={() => void cmd('offer.accept')}>Play</button>
          <button class="btn" onClick={() => void cmd('offer.decline')}>{offer.kind === 'seat' ? 'Not now (leave queue)' : 'Decline'}</button>
        </div>
      </div>
    </div>
  );
}

function OptInDialog() {
  const st = useStore();
  useTicker(500);
  const o = st.optIn;
  if (!o || Date.now() > o.closesAt || st.you?.optIn !== null && st.you?.optIn !== undefined) return null;
  return (
    <div class="modal">
      <div class="modal-card">
        <h3>Next game: {o.title}</h3>
        <p>Do you want to play it? ({countdown(o.closesAt)}) People already waiting keep their priority.</p>
        <div class="row">
          <button class="btn primary" onClick={() => void cmd('vote.optin', { play: true })}>Play next</button>
          <button class="btn" onClick={() => void cmd('vote.optin', { play: false })}>Just watch</button>
        </div>
      </div>
    </div>
  );
}

function ActingHostDialog() {
  const st = useStore();
  if (!st.you?.roles?.actingHostOffer) return null;
  return (
    <div class="modal">
      <div class="modal-card">
        <h3>The host is away</h3>
        <p>Would you act as temporary host? You can resolve stuck seats and disputes and override votes until the host returns. You cannot change deputies or remove games.</p>
        <div class="row">
          <button class="btn primary" onClick={() => void cmd('host.acting_answer', { accept: true })}>OK, I'll help</button>
          <button class="btn" onClick={() => void cmd('host.acting_answer', { accept: false })}>No thanks</button>
        </div>
      </div>
    </div>
  );
}

// One tap before play: unlocks audio inside a real user gesture, and on iOS
// makes the web view first responder so controllers and keyboards deliver
// input (WebKit only routes gamepad events to the first responder). This
// handler must not call preventDefault.
let tapped = false;

function GameView() {
  const st = useStore();
  const canvas = useRef<HTMLCanvasElement>(null);
  const [editing, setEditing] = useState(false);
  const [ready, setReady] = useState(tapped);
  const [locked, setLocked] = useState(false);
  const [, setLayoutTick] = useState(0);
  useEffect(() => {
    if (canvas.current) runner.attach(canvas.current);
  }, []);
  const stats = st.stats;
  const phase = stats?.phase ?? (st.session ? 'loading' : 'idle');
  const seated = !!st.you?.seat && st.you.seat.controlConn === net.connId;
  const touchOn = seated && (settings.touchEnabled === 'on' || ((settings.touchEnabled ?? 'auto') === 'auto' && isTouchDevice()));
  const tb = touchButtons();
  const rot = st.session?.rotation ?? 0;
  return (
    <div class={`game-wrap${touchOn ? ' with-touch' : ''}`}>
      <div class="screen">
        <canvas ref={canvas} class={`game-canvas${settings.smoothing ? ' smooth' : ''}`} style={rot ? { transform: `rotate(${-90 * rot}deg)` } : undefined} />
        {phase !== 'running' && (
          <div class="overlay">
            {!st.session && !st.room?.game && <p>No game is running. Pick one from the shelf{st.room?.resumable ? ' or resume the last session' : ''}.</p>}
            {st.session && phase === 'loading' && <p>{stats?.detail || 'Loading…'}</p>}
            {phase === 'syncing' && <p>{stats?.detail}</p>}
            {phase === 'error' && <p class="err">{stats?.detail} <button class="btn" onClick={() => st.session && void runner.load(st.session)}>Retry</button></p>}
          </div>
        )}
        {phase === 'running' && !ready && (
          <button class="tap-start" onClick={() => { tapped = true; runner.audio.unlock(); setReady(true); }}>
            {st.you?.seat ? 'Tap to play' : 'Tap to watch'}
            <span>sound on · controllers and keyboard ready</span>
          </button>
        )}
        {phase === 'running' && ready && runner.audio.state === 'suspended' && <div class="sound-hint">Tap to enable sound</div>}
      </div>
      {touchOn && (
        <TouchControls buttons={tb.buttons} showSelect={tb.select} layout={touchLayout()} editing={editing}
          onMask={(m) => input.setTouch(m)}
          onLayout={(l) => { saveSettings({ touch: l }); setLayoutTick((n) => n + 1); }} />
      )}
      <div class="screen-tools">
        {canFullscreen() && <button class="icon" title="Full screen" onClick={() => toggleFullscreen()}>⛶</button>}
        {canLockOrientation() && <button class={`icon${locked ? ' on' : ''}`} title="Lock screen rotation" onClick={() => { lockLandscape(!locked); setLocked(!locked); }}>{locked ? '🔒' : '🔓'}</button>}
        {touchOn && <button class="icon" title="Move controls" onClick={() => setEditing(!editing)}>{editing ? '✓' : '✥'}</button>}
        <button class="icon" title={settings.muted ? 'Unmute' : 'Mute'} onClick={() => saveSettings({ muted: !settings.muted })}>{settings.muted ? '🔇' : '🔊'}</button>
      </div>
    </div>
  );
}

function Stats() {
  const st = useStore();
  const s = st.stats;
  if (!s || s.phase !== 'running') return null;
  const ms = (v: number | null) => (v === null ? '–' : `${Math.round(v)} ms`);
  return (
    <div class="stats" title="Measured on this device">
      {s.fps} fps · buffer {s.buffered}/{s.target}
      {runner.myPort !== null ? ` · input→screen ${ms(s.inputToFrameMs)} (p95 ${ms(s.inputToFrameP95)}) · net ${ms(s.inputRttMs)}` : ` · behind live ${ms(s.behindLiveMs)}`}
      {s.desyncs > 0 ? ` · resyncs ${s.desyncs}` : ''}
      {s.emuMsPerFrame !== null ? ` · emulation ${s.emuMsPerFrame} ms/frame` : ''}
      {s.slowDevice && <div class="banner warn">This device is struggling to run this game at full speed, so play may lag behind the others. Watching still works; a faster device plays better.</div>}
    </div>
  );
}

function Events() {
  const st = useStore();
  const ev = st.events.slice(-4);
  if (!ev.length) return null;
  return <div class="events">{ev.map((e, i) => <div key={i} class={`ev ev-${e.kind}`}>{e.text}</div>)}</div>;
}

export function Cabinet() {
  const st = useStore();
  const room = st.room;
  return (
    <div class="cabinet">
      <div class="cab-main">
        <GameView />
        <div class="cab-info">
          {room?.game && <div class="now-playing">▶ {room.game.title}</div>}
          <Seats />
          <Queue />
          <Actions />
          <Events />
          <Capabilities />
          <Stats />
          {room?.pendingSwitch && <div class="banner">Switching to <b>{room.pendingSwitch.title}</b> at {room.pendingSwitch.waitingFor}. ({room.pendingSwitch.by === 'vote' ? 'group vote' : 'host decision'}: {room.pendingSwitch.reason})</div>}
          {room?.disputed && <div class="banner warn">Result disputed — waiting for the host or a deputy.</div>}
        </div>
      </div>
      <Chat />
      <OfferDialog />
      <OptInDialog />
      <ActingHostDialog />
    </div>
  );
}
