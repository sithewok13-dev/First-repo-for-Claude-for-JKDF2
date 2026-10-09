// The group's private game shelf.

import { useEffect, useState } from 'preact/hooks';
import { api, getToken } from '../api.ts';
import { cmd } from '../controller.ts';
import { store, useStore } from '../store.ts';

const SYS: Record<string, string> = { fbneo_cps12: 'Arcade · CPS-1/2', fbneo_neogeo: 'Arcade · Neo Geo', fceumm: 'NES' };
const MODE: Record<string, string> = { versus: 'Versus', coop: 'Co-op', single: 'Single', collab: 'Collaborative', 'turns-shared': 'Turns (shared pad)', 'turns-multi': 'Turns' };

// Placeholder "artwork": a deterministic colour band from the title (no
// scraping of third-party artwork).
function hue(s: string): number {
  let h = 0;
  for (const c of s) h = (h * 31 + c.charCodeAt(0)) % 360;
  return h;
}

export function Shelf() {
  const st = useStore();
  const [games, setGames] = useState<any[]>([]);
  const [usage, setUsage] = useState<any>(null);
  const [filter, setFilter] = useState<'all' | 'fav' | 'recent'>('all');
  const [busy, setBusy] = useState<string | null>(null);
  const [open, setOpen] = useState<number | null>(null);
  const load = async () => {
    try {
      const r = await api<any>('/api/shelf');
      setGames(r.games);
      setUsage(r.usage);
    } catch (e) {
      store.set({ notice: (e as Error).message });
    }
  };
  useEffect(() => { void load(); }, []);
  const canMod = !!st.you?.roles?.canModerate;
  const running = st.room?.game;
  const upload = async (file: File) => {
    setBusy(`Uploading ${file.name}… (validation runs in the background; the game keeps running)`);
    try {
      const res = await fetch('/api/shelf/upload', {
        method: 'POST',
        headers: { Authorization: `Bearer ${getToken()}`, 'Content-Type': 'application/octet-stream', 'X-File-Name': encodeURIComponent(file.name) },
        body: file,
      });
      const body = await res.json().catch(() => ({}));
      store.set({ notice: body.game ? `${body.game.displayName}: ${body.game.status === 'ready' ? 'ready to play' : body.game.status.replace('_', ' ')}` : body.error ?? 'Upload failed.' });
    } catch {
      store.set({ notice: 'Upload failed.' });
    }
    setBusy(null);
    void load();
  };
  let list = games.filter((g) => g.kind === 'game' && g.status !== 'removed');
  if (filter === 'fav') list = list.filter((g) => g.favorite);
  if (filter === 'recent') list = [...list].filter((g) => g.lastPlayedAt).sort((a, b) => b.lastPlayedAt - a.lastPlayedAt);
  const deps = games.filter((g) => g.kind !== 'game' && g.status !== 'removed');
  return (
    <div class="shelf">
      <div class="shelf-head">
        <div class="tabs-small">
          {(['all', 'fav', 'recent'] as const).map((f) => <button key={f} class={filter === f ? 'on' : ''} onClick={() => setFilter(f)}>{f === 'all' ? 'All' : f === 'fav' ? '★ Favorites' : 'Recently played'}</button>)}
        </div>
        <label class="btn">Upload from device<input type="file" accept=".zip,.nes" hidden onChange={(e) => { const f = (e.target as HTMLInputElement).files?.[0]; if (f) void upload(f); }} /></label>
      </div>
      {busy && <div class="banner">{busy}</div>}
      <p class="muted small">Upload ROMs you own by sending them to the Telegram group (up to 20 MB) or with the button above. Files stay private to this group.{usage ? ` Storage: ${(usage.bytes / 1048576).toFixed(1)} of ${(usage.quota / 1048576).toFixed(0)} MB.` : ''}</p>
      {list.length === 0 && <p class="muted">No games yet.</p>}
      <div class="shelf-grid">
        {list.map((g) => (
          <div key={g.id} class={`card status-${g.status}`}>
            <div class="art" style={{ background: `linear-gradient(135deg, hsl(${hue(g.displayName)},70%,35%), hsl(${(hue(g.displayName) + 60) % 360},70%,20%))` }}>
              <span>{g.displayName.slice(0, 2).toUpperCase()}</span>
            </div>
            <div class="card-body">
              <div class="card-title">{g.displayName}{running?.gameId === g.id && <span class="badge">playing</span>}</div>
              <div class="card-meta">{SYS[g.system] ?? g.system ?? 'unknown system'} · {g.players ?? '?'}P · {MODE[g.mode] ?? '?'}</div>
              <div class="card-meta">
                <span class={`compat compat-${g.compat}`}>{g.compat === 'working' ? 'works' : g.compat === 'needs_attention' ? 'needs attention' : 'untested'}</span>
                {g.adapter && <span class="badge ok" title="Verified adapter">auto-detect</span>}
                {g.status !== 'ready' && <span class="badge warn">{g.status.replace('_', ' ')}</span>}
              </div>
              <div class="card-actions">
                {g.status === 'ready' && !running && <button class="btn primary" onClick={() => void cmd('game.start', { gameId: g.id })}>Start</button>}
                {g.status === 'ready' && running && running.gameId !== g.id && <button class="btn" onClick={async () => { if (await cmd('vote.nominate', { gameId: g.id })) store.set({ tab: 'vote' }); }}>Nominate</button>}
                <button class="btn subtle" onClick={async () => { await api(`/api/shelf/${g.id}/favorite`, { method: 'POST', body: JSON.stringify({ favorite: !g.favorite }) }); void load(); }}>{g.favorite ? '★' : '☆'}</button>
                <button class="btn subtle" onClick={() => setOpen(open === g.id ? null : g.id)}>Details</button>
              </div>
              {open === g.id && <GameDetails g={g} canMod={canMod} reload={load} />}
            </div>
          </div>
        ))}
      </div>
      {deps.length > 0 && (
        <details class="deps"><summary>BIOS and dependency files ({deps.length})</summary>
          <ul>{deps.map((g) => <li key={g.id}>{g.displayName} · {g.kind} · {g.status}</li>)}</ul>
        </details>
      )}
      {st.room?.resumable && !running && (
        <div class="banner">
          Last session: <b>{st.room.resumable.title}</b>.{' '}
          <button class="btn primary" onClick={() => void cmd('game.resume', { gameId: st.room.resumable.gameId, checkpointId: st.room.resumable.checkpointId })}>Resume</button>{' '}
          <button class="btn" onClick={() => void cmd('game.start', { gameId: st.room.resumable.gameId })}>Start fresh</button>
        </div>
      )}
    </div>
  );
}

function GameDetails({ g, canMod, reload }: { g: any; canMod: boolean; reload: () => void }) {
  const st = useStore();
  const [name, setName] = useState(g.displayName);
  const [mode, setMode] = useState(g.mode ?? 'single');
  const [notes, setNotes] = useState(g.metadata?.notes ?? '');
  const canEdit = canMod || g.uploaderId === st.me?.id;
  const v = g.validation ?? {};
  return (
    <div class="details">
      <div class="small">Uploaded by {g.uploaderName ?? 'unknown'} on {new Date(g.uploadedAt).toLocaleDateString()} · {(g.sizeBytes / 1024).toFixed(0)} KB · {g.fileName}</div>
      {v.identifiedAs && <div class="small">Identified as: {v.identifiedAs}</div>}
      {(v.findings ?? []).map((f: any, i: number) => <div key={i} class={`small finding-${f.level}`}>{f.message}</div>)}
      {(v.missing ?? []).map((m: any, i: number) => <div key={i} class="small finding-warn">Missing {m.kind} "{m.set}" ({m.files.length} files). Upload it to this group to enable the game.</div>)}
      {v.boot && <div class="small">Boot test: {v.boot.ok ? 'passed' : 'failed'} — {v.boot.detail}</div>}
      <div class="small muted">A successful boot is not full support: multiplayer, handoff and detection are tracked separately.</div>
      {canEdit && (
        <div class="edit">
          <input value={name} maxLength={80} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
          <select value={mode} onChange={(e) => setMode((e.target as HTMLSelectElement).value)}>
            {Object.entries(MODE).map(([k, l]) => <option key={k} value={k}>{l}</option>)}
          </select>
          <input value={notes} maxLength={500} placeholder="Notes (e.g. agreed handoff points)" onInput={(e) => setNotes((e.target as HTMLInputElement).value)} />
          <button class="btn" onClick={async () => { await api(`/api/shelf/${g.id}`, { method: 'PATCH', body: JSON.stringify({ displayName: name, mode, notes }) }).catch((e) => store.set({ notice: e.message })); reload(); }}>Save</button>
        </div>
      )}
      {canMod && <button class="btn danger" onClick={async () => { if (confirm(`Remove ${g.displayName} from the shelf?`)) { await api(`/api/shelf/${g.id}`, { method: 'DELETE' }).catch((e) => store.set({ notice: e.message })); reload(); } }}>Remove from shelf</button>}
    </div>
  );
}
