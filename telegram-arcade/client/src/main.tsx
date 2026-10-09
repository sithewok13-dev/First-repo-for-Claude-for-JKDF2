// Mini App entry point.

import { render } from 'preact';
import { useEffect } from 'preact/hooks';
import { boot } from './controller.ts';
import { store, useStore } from './store.ts';
import { Cabinet } from './ui/cabinet.tsx';
import { Host } from './ui/host.tsx';
import { Records } from './ui/records.tsx';
import { Settings } from './ui/settings.tsx';
import { Shelf } from './ui/shelf.tsx';
import { Vote } from './ui/vote.tsx';

const TABS = [
  ['cabinet', 'Arcade'],
  ['shelf', 'Shelf'],
  ['vote', 'Vote'],
  ['records', 'Records'],
  ['settings', 'Controls'],
  ['host', 'Host'],
] as const;

// Apple Lockdown Mode (and some hardened webviews) disable WebAssembly entirely.
const HAS_WASM = typeof WebAssembly === 'object' && typeof WebAssembly.instantiate === 'function';

function Notice() {
  const st = useStore();
  useEffect(() => {
    if (!st.notice) return;
    const t = setTimeout(() => store.set({ notice: null }), 5000);
    return () => clearTimeout(t);
  }, [st.notice]);
  return st.notice ? <div class="toast" role="status" onClick={() => store.set({ notice: null })}>{st.notice}</div> : null;
}

function App() {
  const st = useStore();
  if (st.error) {
    return (
      <div class="fatal">
        <h1>ARCADE</h1>
        <p>{st.error}</p>
      </div>
    );
  }
  const r = st.room;
  const roleBadge = st.you?.roles?.host ? 'host' : st.you?.roles?.acting ? 'acting host' : st.you?.roles?.deputy ? 'deputy' : st.you?.seat ? `player ${st.you.seat.port + 1}` : st.you?.queuePosition ? `queue #${st.you.queuePosition}` : 'spectator';
  return (
    <div class={`app tab-${st.tab}`}>
      <header class="top">
        <span class="logo">ARCADE</span>
        <span class="group">{st.group?.title}</span>
        <span class={`conn conn-${st.conn}`} title={st.connDetail}>{st.conn === 'open' ? '●' : st.conn === 'connecting' ? '◌' : '○'}</span>
        <span class="role">{roleBadge}</span>
      </header>
      {!HAS_WASM && <div class="banner warn">Games can't run on this device because WebAssembly is turned off (on iPhone this usually means Lockdown Mode). Chat, votes and records still work. To play here, exclude Telegram under Settings › {"Privacy & Security"} › Lockdown Mode › Configure Web Browsing, or use another device.</div>}
      {st.conn === 'denied' && <div class="banner warn">{st.connDetail || 'Access denied.'}</div>}
      {st.conn === 'closed' && <div class="banner warn">Connection lost — reconnecting…</div>}
      <nav class="tabs">
        {TABS.map(([k, label]) => (
          <button key={k} class={st.tab === k ? 'on' : ''} onClick={() => store.set({ tab: k })}>
            {label}
            {k === 'vote' && r?.ballot ? <span class="dot" /> : null}
            {k === 'host' && r?.disputed && st.you?.roles?.canModerate ? <span class="dot" /> : null}
          </button>
        ))}
      </nav>
      <main>
        <div style={{ display: st.tab === 'cabinet' ? 'contents' : 'none' }}><Cabinet /></div>
        {st.tab === 'shelf' && <Shelf />}
        {st.tab === 'vote' && <Vote />}
        {st.tab === 'records' && <Records />}
        {st.tab === 'settings' && <Settings />}
        {st.tab === 'host' && <Host />}
      </main>
      <Notice />
    </div>
  );
}

render(<App />, document.getElementById('app')!);
void boot();
