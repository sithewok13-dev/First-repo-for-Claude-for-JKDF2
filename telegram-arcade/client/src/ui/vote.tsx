// Next-game voting.

import { useEffect, useState } from 'preact/hooks';
import { cmd } from '../controller.ts';
import { useStore } from '../store.ts';

export function Vote() {
  const st = useStore();
  const [, tick] = useState(0);
  useEffect(() => {
    const t = setInterval(() => tick((n) => n + 1), 1000);
    return () => clearInterval(t);
  }, []);
  const room = st.room;
  if (!room) return null;
  const b = room.ballot;
  const mine = st.you?.vote;
  const canMod = st.you?.roles?.canModerate;
  return (
    <div class="vote">
      {!b && !room.pendingSwitch && (
        <div>
          <p>No vote is open. Nominate a game from the shelf, or start an empty vote.</p>
          <button class="btn primary" onClick={() => void cmd('vote.start')}>Start a vote</button>
        </div>
      )}
      {b && (
        <div class="ballot">
          <h3>What next? <span class="muted">closes in {Math.max(0, Math.ceil((b.closesAt - Date.now()) / 1000))}s</span></h3>
          <p class="small muted">{b.rules}</p>
          <p class="small">Voters present: {b.electorate} · ballots: {b.ballots} · quorum: {b.quorum}</p>
          <div class="options">
            <button class={`opt${mine === 'keep' ? ' on' : ''}`} onClick={() => void cmd('vote.cast', { choice: 'keep' })}>
              <span>Keep playing {room.game ? `(${room.game.title})` : ''}</span><b>{b.tally.keep ?? 0}</b>
            </button>
            {b.nominations.map((n: any) => (
              <button key={n.gameId} class={`opt${mine === String(n.gameId) ? ' on' : ''}`} onClick={() => void cmd('vote.cast', { choice: String(n.gameId) })}>
                <span>{n.title} <span class="muted small">nominated by {n.by.join(', ')}</span></span><b>{b.tally[String(n.gameId)] ?? 0}</b>
              </button>
            ))}
          </div>
          <p class="small muted">Add more options from the shelf with "Nominate". You can change your vote until it closes.</p>
        </div>
      )}
      {room.pendingSwitch && (
        <div class="banner">
          Switching to <b>{room.pendingSwitch.title}</b> at {room.pendingSwitch.waitingFor}. Playing next: {room.pendingSwitch.optedIn.join(', ') || 'nobody yet'}.
          <div class="row">
            <button class="btn primary" onClick={() => void cmd('vote.optin', { play: true })}>I want to play</button>
            <button class="btn" onClick={() => void cmd('vote.optin', { play: false })}>Just watch</button>
          </div>
        </div>
      )}
      {room.lastOutcome && !b && <p class="small muted">Last vote: {room.lastOutcome.decided === 'switch' ? 'change approved' : 'keep playing'} — {room.lastOutcome.reason}</p>}
      {canMod && (b || room.pendingSwitch) && <ModOverride />}
    </div>
  );
}

function ModOverride() {
  const st = useStore();
  const [reason, setReason] = useState('');
  const room = st.room;
  const nominees = room.ballot?.nominations ?? [];
  return (
    <div class="mod-box">
      <h4>Host / deputy override</h4>
      <input placeholder="Reason (shown to everyone)" value={reason} onInput={(e) => setReason((e.target as HTMLInputElement).value)} />
      <div class="row">
        {nominees.map((n: any) => <button key={n.gameId} class="btn" disabled={reason.length < 3} onClick={() => void cmd('mod.override', { gameId: n.gameId, reason })}>Switch to {n.title}</button>)}
        <button class="btn" disabled={reason.length < 3} onClick={() => void cmd('mod.override', { gameId: null, reason })}>Cancel change</button>
        {room.pendingSwitch && <button class="btn" onClick={() => void cmd('mod.switch_now')}>Switch now</button>}
      </div>
    </div>
  );
}
