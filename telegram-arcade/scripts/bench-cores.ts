// Measures emulation cost per frame for each core in Node (server worker
// conditions: no RGBA conversion) and with rendering + audio (client
// conditions), using lawful content: the ATC NES ROM and synthetic FBNeo
// romsets (random bytes with the right CRCs; CPUs run garbage, so treat
// arcade numbers as indicative only). Writes test-results/bench.json.

import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { cpus } from 'node:os';
import { join } from 'node:path';
import { Core } from '../shared/emu/core.ts';
import { SYSTEMS, portDevicesFor, type SystemDef } from '../shared/systems.ts';
import { forgeCrc, makeZip, syntheticSet } from '../test/helpers/synthetic-romset.ts';

const ROOT = new URL('../', import.meta.url).pathname;
const D = join(ROOT, 'native/build');

async function core(sys: SystemDef): Promise<Core> {
  const mod = await import(join(D, `${sys.core}.mjs`));
  return Core.create(mod.default, readFileSync(join(D, `${sys.core}.wasm`)), { quiet: true });
}

async function bench(label: string, sys: SystemDef, files: { path: string; data: Uint8Array }[], gamePath: string, frames = 1200) {
  const c = await core(sys);
  for (const f of files) c.writeFile(f.path, f.data);
  for (const [k, v] of Object.entries(sys.options)) c.setOption(k, v);
  for (const [p, d] of Object.entries(portDevicesFor(sys, 2))) c.setPortDevice(Number(p), d);
  if (!c.loadGame(gamePath)) throw new Error(`load failed: ${label}`);
  for (const [k, v] of Object.entries(sys.options)) c.setOption(k, v);
  for (let i = 0; i < 120; i++) c.runFrame(false, false);
  const time = (render: boolean) => {
    const t0 = performance.now();
    for (let i = 0; i < frames; i++) {
      c.setInput(0, (i * 7) & 0xf3);
      c.runFrame(render, render);
    }
    return (performance.now() - t0) / frames;
  };
  const server = time(false);
  const client = time(true);
  const t0 = performance.now();
  const state = c.serialize();
  const serMs = performance.now() - t0;
  const t1 = performance.now();
  c.stateHash();
  const hashMs = performance.now() - t1;
  return { label, core: sys.core, msPerFrameServer: +server.toFixed(3), msPerFrameClient: +client.toFixed(3), stateBytes: state.length, serializeMs: +serMs.toFixed(2), hashMs: +hashMs.toFixed(2) };
}

const results: any[] = [];
results.push(await bench('ATC co-op (NES, lawful)', SYSTEMS.fceumm, [{ path: '/roms/g.nes', data: readFileSync(join(ROOT, 'native/testroms/build/atc-coop.nes')) }], '/roms/g.nes'));
for (const [sysId, set, board] of [['fbneo_cps12', 'sf2', null], ['fbneo_cps12', 'ssf2t', null], ['fbneo_neogeo', 'kof98', 'neogeo']] as const) {
  const sys = SYSTEMS[sysId];
  const cat = (await core(sys)).catalog();
  const files = [{ path: `/roms/${set}.zip`, data: syntheticSet(cat.find((d: any) => d.name === set), 3) }];
  if (board) {
    const b = cat.find((d: any) => d.name === board);
    files.push({ path: `/roms/${board}.zip`, data: makeZip(b.roms.filter((r: any) => !(r.t & ((1 << 27) | (1 << 28))) && r.s >= 4).map((r: any, i: number) => ({ name: r.n, data: forgeCrc(r.s, r.c, 99 + i) }))) });
  }
  results.push(await bench(`${set} (synthetic romset)`, sys, files, `/roms/${set}.zip`));
}
const out = { machine: { cpu: cpus()[0]?.model, cores: cpus().length, node: process.version }, at: new Date().toISOString(), results };
mkdirSync(join(ROOT, 'test-results'), { recursive: true });
writeFileSync(join(ROOT, 'test-results/bench.json'), JSON.stringify(out, null, 2));
console.table(results);
