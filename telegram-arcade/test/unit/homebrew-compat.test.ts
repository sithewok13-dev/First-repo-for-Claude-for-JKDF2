// Generic lockstep checks for lawful third-party NES homebrew (built from
// source by native/testroms/fetch-homebrew.sh into HOMEBREW_DIR):
//   boots and draws, two separately booted replicas agree under scripted
//   two-player input, and a replica restored from a snapshot converges.
// Skipped when the ROMs are not present. Results feed docs/COMPATIBILITY.md.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { Core } from '../../shared/emu/core.ts';
import { SYSTEMS, portDevicesFor } from '../../shared/systems.ts';
import { CORE_DIR, ROOT } from '../helpers/replica.ts';

const DIR = process.env.HOMEBREW_DIR ?? join(ROOT, 'native/testroms/homebrew');
const ROMS: { file: string; players: number; note: string }[] = [
  { file: 'rhde.nes', players: 2, note: 'RHDE: Furniture Fight — 2P simultaneous (GNU All-Permissive)' },
  { file: 'thwaite.nes', players: 2, note: 'Thwaite — 1P/2P co-op, game over (GPL-3.0+)' },
  { file: 'croom.nes', players: 2, note: 'Concentration Room — hot-seat turns (GPL-3.0+)' },
  { file: 'squirrel_domino.nes', players: 2, note: 'Squirrel Domino — 2P versus (zlib)' },
  { file: 'allpads.nes', players: 4, note: 'allpads — Four Score input diagnostic (zlib)' },
  { file: 'efp.nes', players: 1, note: 'Escape from Pong — 1P (BSD-3)' },
];

const sys = SYSTEMS.fceumm;

async function boot(data: Uint8Array, players: number): Promise<Core> {
  const mod = await import(join(CORE_DIR, 'fceumm.mjs'));
  const c = await Core.create(mod.default, readFileSync(join(CORE_DIR, 'fceumm.wasm')), { quiet: true });
  c.writeFile('/roms/game.nes', data);
  for (const [k, v] of Object.entries(sys.options)) c.setOption(k, v);
  for (const [p, d] of Object.entries(portDevicesFor(sys, players))) c.setPortDevice(Number(p), d);
  assert.ok(c.loadGame('/roms/game.nes'));
  c.setSavestateContext(3);
  return c;
}

function input(f: number, p: number): number {
  // press START early to get past title screens, then pseudo-random play
  if (f % 240 < 4) return 1 << 3;
  let x = (f * 2246822519 + p * 3266489917) >>> 0;
  x ^= x >>> 15;
  return x & 0x01f3;
}

function run(c: Core, until: number, players: number, render = false): void {
  while (c.frame < until) {
    for (let p = 0; p < players; p++) c.setInput(p, input(c.frame, p));
    c.runFrame(render, false);
  }
}

for (const rom of ROMS) {
  const path = join(DIR, rom.file);
  test(`homebrew ${rom.file}: boots, replicas agree, snapshot restore converges`, { skip: !existsSync(path) && `not built (${path})` }, async () => {
    const data = new Uint8Array(readFileSync(path));
    const a = await boot(data, rom.players);
    const b = await boot(data, rom.players);
    run(a, 600, rom.players);
    run(b, 600, rom.players);
    assert.equal(a.stateHash(), b.stateHash(), 'separately booted replicas agree');
    a.runFrame(true, false);
    b.runFrame(true, false);
    const img = a.image()!;
    let lit = 0;
    for (let i = 0; i < img.rgba.length; i += 4) if (img.rgba[i] | img.rgba[i + 1] | img.rgba[i + 2]) lit++;
    assert.ok(lit > 100, 'draws something');
    const c = await boot(data, rom.players);
    c.unserialize(a.serialize());
    c.frame = a.frame;
    for (let f = 1200; f <= 3600; f += 600) {
      run(a, f, rom.players);
      run(c, f, rom.players);
      assert.equal(c.stateHash(), a.stateHash(), `restored replica matches at frame ${f}`);
    }
  });
}
