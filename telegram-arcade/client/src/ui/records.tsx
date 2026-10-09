// Group-private records.

import { Fragment } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { api } from '../api.ts';
import { useStore } from '../store.ts';

export function Records() {
  const st = useStore();
  const [games, setGames] = useState<any[]>([]);
  const [gameId, setGameId] = useState<number | null>(st.room?.game?.gameId ?? null);
  const [data, setData] = useState<any>(null);
  useEffect(() => {
    void api<any>('/api/shelf').then((r) => setGames(r.games.filter((g: any) => g.kind === 'game')));
  }, []);
  useEffect(() => {
    if (gameId === null) return;
    void api<any>(`/api/records?game=${gameId}`).then(setData).catch(() => setData(null));
  }, [gameId]);
  const nameOf = (id: number | null) => (id === null ? '—' : st.room?.members?.find((m: any) => m.userId === id)?.name ?? `#${id}`);
  return (
    <div class="records">
      <select value={gameId ?? ''} onChange={(e) => setGameId(Number((e.target as HTMLSelectElement).value))}>
        <option value="" disabled>Choose a game</option>
        {games.map((g) => <option key={g.id} value={g.id}>{g.displayName}</option>)}
      </select>
      <p class="small muted">Records are private to this group. ✓ = detected by a verified adapter; ✎ = reported by players. Different ROM revisions, emulator builds or settings are kept apart (column "build").</p>
      {data && (
        <>
          <h3>High scores</h3>
          {data.scores.length === 0 ? <p class="muted">No scores yet.</p> : (
            <table><thead><tr><th>Score</th><th>Who</th><th>Kind</th><th></th><th>Date</th><th>build</th></tr></thead><tbody>
              {data.scores.map((s: any, i: number) => {
                const parts = JSON.parse(s.participants || '[]');
                const who = s.user_id ? [s.first_name, s.last_name].filter(Boolean).join(' ') || s.username || `#${s.user_id}` : parts.map((p: any) => `${nameOf(p.user)}${p.points !== null ? ` (${p.points})` : ''}`).join(' + ');
                const flags = JSON.parse(s.run_flags || '{}');
                const cat = scoreCategory(s);
                const heading = i === 0 || scoreCategory(data.scores[i - 1]) !== cat;
                return (
                  <Fragment key={s.id}>
                  {heading && <tr class="cat"><td colSpan={6}>{categoryLabel(s)}</td></tr>}
                  <tr><td>{s.score}</td><td>{who}</td><td>{s.kind === 'individual' ? 'solo' : s.kind === 'seat' ? 'shared seat' : s.kind}{flags.fresh === false ? ' · resumed' : ''}{typeof flags.continues === 'number' && flags.continues > 0 ? ` · ${flags.continues} continue${flags.continues === 1 ? '' : 's'}` : ''}</td><td>{s.verification === 'verified' ? '✓' : '✎'}</td><td>{new Date(s.created_at).toLocaleDateString()}</td><td class="mono">{String(s.compat_key).slice(0, 6)}</td></tr>
                  </Fragment>
                );
              })}
            </tbody></table>
          )}
          <h3>Fighters</h3>
          {data.fighters.length === 0 ? <p class="muted">No counted matches yet.</p> : (
            <table><thead><tr><th>Player</th><th>W</th><th>L</th><th>D</th><th>Best streak</th><th>build</th></tr></thead><tbody>
              {data.fighters.sort((a: any, b: any) => b.wins - a.wins).map((f: any) => <tr key={`${f.compatKey}-${f.userId}`}><td>{nameOf(f.userId)}</td><td>{f.wins}</td><td>{f.losses}</td><td>{f.draws}</td><td>{f.longestStreak}</td><td class="mono">{f.compatKey.slice(0, 6)}</td></tr>)}
            </tbody></table>
          )}
          <h3>Recent matches</h3>
          <ul class="small">{data.matches.map((m: any) => <li key={m.id}>{nameOf(m.p1_user)} vs {nameOf(m.p2_user)} — {m.result === 'win' ? `${nameOf(m.winner_user)} won` : m.result}{m.counts ? '' : ' (not counted)'} {m.verification === 'verified' ? '✓' : '✎'}</li>)}</ul>
        </>
      )}
    </div>
  );
}

// Scores are only ranked against runs of the same kind: same build/ROM/settings,
// fresh vs resumed, and without vs with continues.
function scoreCategory(s: any): string {
  const f = JSON.parse(s.run_flags || '{}');
  return `${s.compat_key}|${f.fresh === false ? 'r' : 'f'}|${typeof f.continues === 'number' && f.continues > 0 ? 'c' : 'n'}`;
}

function categoryLabel(s: any): string {
  const f = JSON.parse(s.run_flags || '{}');
  const parts = [f.fresh === false ? 'Resumed runs' : 'Fresh runs'];
  if (typeof f.continues === 'number') parts.push(f.continues > 0 ? 'with continues' : 'no continues');
  return `${parts.join(', ')} · build ${String(s.compat_key).slice(0, 6)}`;
}
