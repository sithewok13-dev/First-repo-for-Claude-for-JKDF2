// Compact host / deputy controls.

import { useState } from 'preact/hooks';
import { api } from '../api.ts';
import { cmd } from '../controller.ts';
import { useStore } from '../store.ts';

const REASONS: [string, string][] = [['afk', 'Away / not playing'], ['stuck', 'Stuck / frozen'], ['left-device', 'Left the device'], ['disruptive', 'Disruptive'], ['abusive', 'Abusive'], ['other', 'Other']];

export function Host() {
  const st = useStore();
  const room = st.room;
  const roles = st.you?.roles;
  const [reason, setReason] = useState('afk');
  const [note, setNote] = useState('');
  const [log, setLog] = useState<any[] | null>(null);
  if (!room || !roles) return null;
  if (!roles.canModerate) {
    return (
      <div class="host">
        <p>Host: <b>{room.hostName ?? 'not set'}</b>{room.hostPresent ? '' : ' (away)'}{room.actingHost ? ` · acting host: ${room.actingHost.name}` : ''}</p>
        <p>Deputies: {room.deputies.map((d: any) => d.name).join(', ') || 'none'}</p>
        <p class="small muted">The host and deputies can resolve stuck seats and disputes, moderate chat and override votes. Nobody can be removed for winning.</p>
      </div>
    );
  }
  const others = room.members.filter((m: any) => m.userId !== st.me?.id);
  return (
    <div class="host">
      <p>You are {roles.host ? 'the host' : roles.acting ? 'the acting host' : 'a deputy'}.{!roles.canAdminister && ' (Deputies and acting hosts cannot change deputies or settings.)'}</p>
      <section>
        <h4>Players</h4>
        <div class="row">
          <select value={reason} onChange={(e) => setReason((e.target as HTMLSelectElement).value)}>
            {REASONS.map(([k, l]) => <option key={k} value={k}>{l}</option>)}
          </select>
          {reason === 'other' && <input placeholder="Explain" value={note} onInput={(e) => setNote((e.target as HTMLInputElement).value)} />}
        </div>
        {room.seats.filter((s: any) => s.userId !== null).map((s: any) => (
          <div key={s.port} class="row">P{s.port + 1} {s.name}
            <button class="btn tiny" onClick={() => void cmd('mod.remove_seat', { user: s.userId, reason, note })}>Remove from seat</button>
          </div>
        ))}
        {room.queue.map((q: any) => (
          <div key={q.userId} class="row">Queue {q.position}. {q.name}
            <button class="btn tiny" onClick={() => void cmd('mod.remove_queue', { user: q.userId, reason })}>Remove from queue</button>
          </div>
        ))}
        <p class="small muted">Removed for being away or stuck → they keep priority (front of the queue). Winning is never a reason.</p>
      </section>
      <section>
        <h4>Game</h4>
        <div class="row">
          {room.game?.mode === 'versus' && room.disputed && (
            <>
              <button class="btn" onClick={() => void cmd('mod.adjudicate', { winnerPort: 0, reason: 'host decision' })}>P1 won</button>
              <button class="btn" onClick={() => void cmd('mod.adjudicate', { winnerPort: 1, reason: 'host decision' })}>P2 won</button>
              <button class="btn" onClick={() => void cmd('mod.adjudicate', { winnerPort: null, reason: 'host decision' })}>Draw</button>
              <button class="btn" onClick={() => void cmd('mod.adjudicate', { winnerPort: 'void', reason: 'host decision' })}>Void</button>
            </>
          )}
          <button class="btn" onClick={() => { const r = prompt('Why reset? (shown to everyone)'); if (r) void cmd('mod.reset', { reason: r }); }}>Reset frozen game</button>
          <button class="btn" onClick={() => void cmd('mod.resolve_controller', { to: null, reason: 'resolved stuck handoff' })}>Resolve stuck controller</button>
        </div>
      </section>
      <section>
        <h4>Chat</h4>
        {others.map((m: any) => (
          <div key={m.userId} class="row">{m.name}
            <button class="btn tiny" onClick={() => void cmd('mod.mute', { user: m.userId, minutes: 10, reason: 'chat moderation' })}>Mute 10 min</button>
            {roles.canAdminister && !m.deputy && <button class="btn tiny" onClick={() => void cmd('host.deputy_add', { user: m.userId, scope: 'session' })}>Deputy (session)</button>}
            {roles.canAdminister && !m.deputy && <button class="btn tiny" onClick={() => void cmd('host.deputy_add', { user: m.userId, scope: 'persistent' })}>Deputy (always)</button>}
            {roles.canAdminister && m.deputy && <button class="btn tiny" onClick={() => void cmd('host.deputy_remove', { user: m.userId })}>Remove deputy</button>}
            {roles.canAdminister && <button class="btn tiny" onClick={() => { if (confirm(`Make ${m.name} the host?`)) void cmd('host.transfer', { user: m.userId }); }}>Make host</button>}
          </div>
        ))}
      </section>
      {roles.canAdminister && (
        <section>
          <h4>Deputies (succession order)</h4>
          <ol>{room.deputies.map((d: any) => <li key={d.userId}>{d.name} · {d.scope === 'persistent' ? 'always' : 'this session'}</li>)}</ol>
          <h4>Room rules</h4>
          <Rules />
        </section>
      )}
      <section>
        <button class="btn" onClick={async () => setLog((await api<any>('/api/audit')).entries)}>Show moderation log</button>
        {log && <ul class="small">{log.map((e, i) => <li key={i}>{new Date(e.created_at).toLocaleString()} · {e.action} · {e.reason ?? ''}</li>)}</ul>}
      </section>
    </div>
  );
}

function Rules() {
  const st = useStore();
  const s = st.room.settings;
  const [v, setV] = useState({ offerSeconds: s.offerSeconds, graceSeconds: s.graceSeconds, drawsBeforeRotation: s.drawsBeforeRotation, timedRotationMinutes: s.timedRotationMinutes, maxViewers: s.maxViewers, voteSeconds: s.voteSeconds });
  const field = (k: keyof typeof v, label: string) => (
    <label key={k}>{label} <input type="number" value={v[k]} onInput={(e) => setV({ ...v, [k]: Number((e.target as HTMLInputElement).value) })} /></label>
  );
  return (
    <div class="rules">
      {field('offerSeconds', 'Seat offer countdown (s)')}
      {field('graceSeconds', 'Disconnect grace (s)')}
      {field('drawsBeforeRotation', 'Draws before rotation')}
      {field('timedRotationMinutes', 'Timed rotation (min, 0 = off)')}
      {field('maxViewers', 'Max people in room')}
      {field('voteSeconds', 'Vote duration (s)')}
      <button class="btn" onClick={() => void cmd('host.settings', { settings: v })}>Save rules</button>
    </div>
  );
}
