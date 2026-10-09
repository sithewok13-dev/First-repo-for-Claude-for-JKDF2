// Emulation worker: a separate OS process that runs the authoritative
// instance of one room's game. It only ever receives files, options and
// authoritative input records, and only returns hashes, snapshots, status
// and adapter events. The emulator itself is WebAssembly (no syscalls beyond
// an in-memory filesystem), and this process is killed and restored from a
// checkpoint if it misbehaves.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { deflateRawSync } from 'node:zlib';
import { Core, RETRO_SAVESTATE_CONTEXT_NORMAL, RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY } from '../../shared/emu/core.ts';
import { adapterById } from '../adapters/registry.ts';
import type { AdapterInstance } from '../adapters/types.ts';
import type { WorkerIn, WorkerOut } from './messages.ts';

let core: Core | null = null;
let adapter: AdapterInstance | null = null;
let hashEvery = 120;
let statusEvery = 6;
let ports = 1;

function send(m: WorkerOut): void {
  process.send!(m);
}

function fail(message: string): never {
  send({ t: 'error', message });
  process.exit(2);
}

async function init(m: Extract<WorkerIn, { t: 'init' }>): Promise<void> {
  const mod = await import(pathToFileURL(join(m.coreDir, `${m.core}.mjs`)).href);
  const wasm = readFileSync(join(m.coreDir, `${m.core}.wasm`));
  core = await Core.create(mod.default, wasm, { quiet: true });
  core.setLogLevel(2);
  core.setEpoch(m.epoch);
  for (const f of m.files) core.writeFile(f.path, f.data);
  for (const [k, v] of Object.entries(m.options)) core.setOption(k, v);
  for (const [p, d] of Object.entries(m.portDevices)) core.setPortDevice(Number(p), d);
  if (!core.loadGame(m.gamePath)) fail(`the core could not load this game (${core.log.slice(-3).join(' | ') || 'no details'})`);
  // FBNeo's retro_load_game reports success even when a ROM is missing and
  // shows its own error screen instead; a started driver is the real signal.
  if (m.core.startsWith('fbneo') && !core.driverInfo()?.name) fail(`FBNeo could not start this romset (${core.log.slice(-3).join(' | ') || 'no details'})`);
  // Options are re-applied after load: FBNeo registers per-game options during load.
  for (const [k, v] of Object.entries(m.options)) core.setOption(k, v);
  core.setSavestateContext(RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY);
  ports = m.ports;
  hashEvery = m.hashEvery;
  statusEvery = m.statusEvery;
  if (m.state) {
    core.setSavestateContext(m.stateContext ?? RETRO_SAVESTATE_CONTEXT_NORMAL);
    try {
      core.unserialize(m.state);
    } catch {
      fail('checkpoint is not compatible with this game/core build');
    }
    core.setSavestateContext(RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY);
    core.frame = m.startFrame;
  }
  if (m.adapterId) {
    const a = adapterById(m.adapterId);
    adapter = a ? a.create() : null;
  }
  send({
    t: 'ready',
    frame: core.frame,
    info: {
      coreName: core.name,
      coreVersion: core.version,
      fps: core.fps,
      sampleRate: core.sampleRate,
      width: core.baseWidth,
      height: core.baseHeight,
      aspect: core.aspect,
      rotation: core.rotation,
      descriptors: core.descriptors(),
      driver: core.driverInfo(),
      stateSize: core.serialize().length,
      options: core.options(),
    },
  });
}

function runFrames(first: number, masks: Uint16Array, n: number): void {
  const c = core!;
  if (first !== c.frame) fail(`frame gap: worker at ${c.frame}, records start at ${first}`);
  for (let i = 0; i < n; i++) {
    for (let p = 0; p < ports; p++) c.setInput(p, masks[i * ports + p]);
    c.runFrame(false, false);
    const f = c.frame;
    if (adapter) {
      const ram = c.memory();
      if (ram) {
        const ev = adapter.step(f, ram);
        if (ev.length) send({ t: 'events', events: ev });
        if (f % statusEvery === 0) send({ t: 'status', status: adapter.status(f, ram) });
      }
    }
    if (f % hashEvery === 0) send({ t: 'hash', frame: f, hash: c.stateHash() });
  }
}

process.on('message', async (raw: unknown) => {
  const m = raw as WorkerIn;
  try {
    switch (m.t) {
      case 'init':
        await init(m);
        break;
      case 'frames':
        if (!core) fail('frames before init');
        runFrames(m.first, m.masks, m.masks.length / ports);
        break;
      case 'snapshot': {
        if (!core) fail('snapshot before init');
        const state = core.serialize();
        send({ t: 'snapshot', id: m.id, frame: core.frame, rawLength: state.length, data: deflateRawSync(state, { level: 6 }) });
        break;
      }
      case 'checkpoint': {
        if (!core) fail('checkpoint before init');
        core.setSavestateContext(RETRO_SAVESTATE_CONTEXT_NORMAL);
        const state = core.serialize();
        core.setSavestateContext(RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY);
        send({ t: 'checkpoint', id: m.id, frame: core.frame, data: state });
        break;
      }
      case 'ping':
        send({ t: 'pong', id: m.id, frame: core ? core.frame : -1 });
        break;
      case 'stop':
        process.exit(0);
    }
  } catch (e) {
    fail(e instanceof Error ? e.message : String(e));
  }
});

process.on('disconnect', () => process.exit(0));
send({ t: 'boot' });
