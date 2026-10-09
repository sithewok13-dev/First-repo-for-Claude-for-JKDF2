// Personal settings: control mappings (keyboard, controller), a controller
// test screen, touch layout and display/sound preferences. Saved per user.

import { useEffect, useState } from 'preact/hooks';
import { PAD } from '../../../shared/emu/core.ts';
import { api, clearFileCache, fileCacheStats } from '../api.ts';
import { input, saveSettings, settings, net } from '../controller.ts';
import { DEFAULT_KEYS, DEFAULT_PAD } from '../input/input.ts';
import { store, useStore } from '../store.ts';
import { openExternal } from '../telegram.ts';
import { DEFAULT_LAYOUT } from './touch.tsx';

const BITS: [string, number][] = [
  ['Up', PAD.UP], ['Down', PAD.DOWN], ['Left', PAD.LEFT], ['Right', PAD.RIGHT],
  ['B', PAD.B], ['A', PAD.A], ['Y', PAD.Y], ['X', PAD.X], ['L', PAD.L], ['R', PAD.R],
  ['Start', PAD.START], ['Select', PAD.SELECT],
];

function bitName(bit: number): string {
  return BITS.find(([, b]) => b === bit)?.[0] ?? `#${bit}`;
}

export function Settings() {
  const st = useStore();
  const [capture, setCapture] = useState<{ kind: 'key' | 'pad'; bit: number } | null>(null);
  const [pads, setPads] = useState(input.pads);
  const [snap, setSnap] = useState<ReturnType<typeof input.padSnapshot>>(null);
  const [mask, setMask] = useState(0);
  useEffect(() => {
    input.onPads = setPads;
    const t = setInterval(() => {
      setSnap(input.padSnapshot());
      setMask(input.mask);
    }, 100);
    return () => clearInterval(t);
  }, []);
  useEffect(() => {
    if (!capture) {
      input.onRawKey = null;
      input.onRawPad = null;
      return;
    }
    if (capture.kind === 'key') {
      input.onRawKey = (code) => {
        if (code === 'Escape') { setCapture(null); return true; }
        const keys = { ...(settings.keys ?? DEFAULT_KEYS) };
        for (const k of Object.keys(keys)) if (keys[k] === capture.bit) delete keys[k];
        keys[code] = capture.bit;
        saveSettings({ keys });
        setCapture(null);
        return true;
      };
    } else {
      input.onRawPad = (button) => {
        const pad = { ...(settings.pad ?? DEFAULT_PAD) };
        for (const k of Object.keys(pad)) if (pad[Number(k)] === capture.bit) delete pad[Number(k)];
        pad[button] = capture.bit;
        saveSettings({ pad });
        setCapture(null);
        return true;
      };
    }
    return () => {
      input.onRawKey = null;
      input.onRawPad = null;
    };
  }, [capture]);
  const keys = settings.keys ?? DEFAULT_KEYS;
  const padMap = settings.pad ?? DEFAULT_PAD;
  const keyFor = (bit: number) => Object.entries(keys).filter(([, b]) => b === bit).map(([k]) => k.replace(/^Key|^Digit|^Arrow/, '')).join(', ') || '—';
  const btnFor = (bit: number) => Object.entries(padMap).filter(([, b]) => b === bit).map(([k]) => `#${k}`).join(', ') || '—';
  const handoff = async () => {
    try {
      const r = await api<any>('/api/auth/handoff', { method: 'POST' });
      openExternal(r.url);
    } catch (e) {
      store.set({ notice: (e as Error).message });
    }
  };
  return (
    <div class="settings">
      <section>
        <h3>Controller test</h3>
        <div class="pad-test">
          {BITS.map(([n, b]) => <span key={b} class={`pt${mask & (1 << b) ? ' on' : ''}`}>{n}</span>)}
        </div>
        <p class="small">{pads.length ? `Connected: ${pads.map((p) => `${p.id} (${p.mapping || 'non-standard'})`).join('; ')}` : 'No controller detected. Press a button on your controller to wake it up.'}</p>
        {snap && <p class="small mono">raw buttons: {snap.buttons.map((v, i) => (v > 0.5 ? i : null)).filter((x) => x !== null).join(' ') || 'none'} · axes: {snap.axes.map((a) => a.toFixed(1)).join(' ')}</p>}
        {pads.length > 1 && (
          <select value={settings.padIndex ?? ''} onChange={(e) => saveSettings({ padIndex: (e.target as HTMLSelectElement).value === '' ? null : Number((e.target as HTMLSelectElement).value) })}>
            <option value="">First connected controller</option>
            {pads.map((p) => <option key={p.index} value={p.index}>{p.id}</option>)}
          </select>
        )}
        <p class="small muted">The arcade sends exactly the buttons you press: no turbo, no macros. In Telegram on Android the in-app browser may not report controllers; if yours is not detected, use "Open in browser" below.</p>
      </section>
      <section>
        <h3>Mappings</h3>
        <table class="map">
          <thead><tr><th>Button</th><th>Keyboard</th><th>Controller</th></tr></thead>
          <tbody>
            {BITS.map(([n, b]) => (
              <tr key={b}>
                <td>{n}</td>
                <td><button class={`btn tiny${capture?.kind === 'key' && capture.bit === b ? ' on' : ''}`} onClick={() => setCapture({ kind: 'key', bit: b })}>{capture?.kind === 'key' && capture.bit === b ? 'press a key…' : keyFor(b)}</button></td>
                <td><button class={`btn tiny${capture?.kind === 'pad' && capture.bit === b ? ' on' : ''}`} onClick={() => setCapture({ kind: 'pad', bit: b })}>{capture?.kind === 'pad' && capture.bit === b ? 'press a button…' : btnFor(b)}</button></td>
              </tr>
            ))}
          </tbody>
        </table>
        <p class="small muted">Coin: key 5 or the "Insert coin" button. Game button names (e.g. Weak Punch) depend on the game: {bitName(PAD.Y)}/{bitName(PAD.X)}/{bitName(PAD.L)} are the top row, {bitName(PAD.B)}/{bitName(PAD.A)}/{bitName(PAD.R)} the bottom row on 6-button arcade games.</p>
        <button class="btn" onClick={() => saveSettings({ keys: { ...DEFAULT_KEYS }, pad: { ...DEFAULT_PAD } })}>Reset mappings</button>
      </section>
      <section>
        <h3>Touch controls</h3>
        <label>Show touch controls{' '}
          <select value={settings.touchEnabled ?? 'auto'} onChange={(e) => saveSettings({ touchEnabled: (e.target as HTMLSelectElement).value as any })}>
            <option value="auto">On touch screens</option><option value="on">Always</option><option value="off">Never</option>
          </select>
        </label>
        <label>Direction control{' '}
          <select value={settings.touch?.stick ?? 'dpad'} onChange={(e) => saveSettings({ touch: { ...(settings.touch ?? DEFAULT_LAYOUT), stick: (e.target as HTMLSelectElement).value as any } })}>
            <option value="dpad">D-pad</option><option value="stick">Joystick</option>
          </select>
        </label>
        <label>Opacity <input type="range" min="0.2" max="1" step="0.05" value={settings.touch?.opacity ?? DEFAULT_LAYOUT.opacity}
          onInput={(e) => saveSettings({ touch: { ...(settings.touch ?? DEFAULT_LAYOUT), opacity: Number((e.target as HTMLInputElement).value) } })} /></label>
        <p class="small muted">Move and resize the controls with the ✥ button on the game screen.</p>
        <button class="btn" onClick={() => saveSettings({ touch: { ...DEFAULT_LAYOUT } })}>Reset layout</button>
      </section>
      <section>
        <h3>Sound and display</h3>
        <label>Volume <input type="range" min="0" max="1" step="0.05" value={settings.volume ?? 0.8} onInput={(e) => saveSettings({ volume: Number((e.target as HTMLInputElement).value) })} /></label>
        <label><input type="checkbox" checked={!!settings.muted} onChange={(e) => saveSettings({ muted: (e.target as HTMLInputElement).checked })} /> Mute</label>
        <label><input type="checkbox" checked={!!settings.smoothing} onChange={(e) => saveSettings({ smoothing: (e.target as HTMLInputElement).checked })} /> Smooth pixels</label>
      </section>
      <FileCache />
      <section>
        <h3>Open in browser</h3>
        <p class="small">If something does not work inside Telegram (for example a controller), continue in your normal browser as the same Telegram user. The link works once, for 60 seconds.</p>
        <button class="btn" onClick={() => void handoff()}>Open in browser</button>
        <p class="small muted">Connection: {st.conn}{net.rttMs ? ` · ping ${Math.round(net.rttMs)} ms` : ''}</p>
      </section>
      <section>
        <h3>About</h3>
        <p class="small">Emulation by FBNeo (non-commercial licence) and FCEUmm (GPL-2.0). Licences and source: <a href="/cores/SOURCES.txt" target="_blank" rel="noopener">emulator cores</a> · <a href="/THIRD-PARTY-NOTICES.txt" target="_blank" rel="noopener">app</a>.</p>
      </section>
    </div>
  );
}

// Game files are downloaded to every device that plays or watches (the game
// runs on the device). They stay private to this app; this shows and clears them.
function FileCache() {
  const [stats, setStats] = useState<{ files: number; bytes: number } | null>(null);
  useEffect(() => { void fileCacheStats().then(setStats); }, []);
  return (
    <section>
      <h3>Game files on this device</h3>
      <p class="small">Games run on your device, so their files are downloaded here (only for this group's arcade) and kept to save loading time.</p>
      <p class="small muted">{stats ? `${stats.files} file${stats.files === 1 ? '' : 's'}, ${(stats.bytes / 1e6).toFixed(1)} MB` : 'Checking…'}</p>
      <button class="btn" disabled={!stats?.files} onClick={() => void clearFileCache().then(() => fileCacheStats()).then(setStats)}>Remove downloaded game files</button>
    </section>
  );
}
