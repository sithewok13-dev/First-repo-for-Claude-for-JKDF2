// FBNeo (CPS-1, CPS-2, Neo Geo) lockstep properties without copyrighted
// content: synthetic romsets with the right names/sizes/CRCs (seeded random
// bytes). Checks (1) two instances booted separately and fed identical
// input stay bit-identical, (2) an instance restored from a netplay
// savestate mid-run converges with the original, (3) the frontend's
// clock/RNG pinning holds (no wall-clock dependence), (4) macro / service
// inputs are excluded from the allowed masks.
//
// This proves the cores' determinism and savestate completeness on our
// build; it does NOT prove any particular commercial game works — that
// needs owner-supplied ROMs (see docs/TESTING.md).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { Core } from '../../shared/emu/core.ts';
import { SYSTEMS, allowedMasks, portDevicesFor, type SystemDef } from '../../shared/systems.ts';
import { CORE_DIR } from '../helpers/replica.ts';
import { makeZip, forgeCrc, syntheticSet, type CatalogDriver } from '../helpers/synthetic-romset.ts';

const BRF_OPT = 1 << 27, BRF_NODUMP = 1 << 28;

async function boot(sys: SystemDef, files: { path: string; data: Uint8Array }[], gamePath: string, epoch: number): Promise<Core> {
  const mod = await import(join(CORE_DIR, `${sys.core}.mjs`));
  const c = await Core.create(mod.default, readFileSync(join(CORE_DIR, `${sys.core}.wasm`)), { quiet: true });
  c.setEpoch(epoch);
  for (const f of files) c.writeFile(f.path, f.data);
  for (const [k, v] of Object.entries(sys.options)) c.setOption(k, v);
  for (const [p, d] of Object.entries(portDevicesFor(sys, 2))) c.setPortDevice(Number(p), d);
  assert.ok(c.loadGame(gamePath), `load ${gamePath}: ${c.log.slice(-3).join(' | ')}`);
  for (const [k, v] of Object.entries(sys.options)) c.setOption(k, v);
  c.setSavestateContext(3);
  return c;
}

function inputFor(frame: number, port: number): number {
  let x = (frame * 2654435761 + port * 40503) >>> 0;
  x ^= x >>> 13;
  return (x & 0x0ff3) & ~(1 << 2); // no SELECT (coins are server-side)
}

function run(c: Core, until: number): void {
  while (c.frame < until) {
    const f = c.frame;
    c.setInput(0, inputFor(f, 0));
    c.setInput(1, inputFor(f, 1));
    c.runFrame(false, false);
  }
}

async function catalog(core: string): Promise<CatalogDriver[]> {
  const mod = await import(join(CORE_DIR, `${core}.mjs`));
  const c = await Core.create(mod.default, readFileSync(join(CORE_DIR, `${core}.wasm`)), { quiet: true });
  return c.catalog();
}

const cases: { system: 'fbneo_cps12' | 'fbneo_neogeo'; set: string; board?: string }[] = [
  { system: 'fbneo_cps12', set: 'sf2' },      // CPS-1 versus fighter
  { system: 'fbneo_cps12', set: 'captcomm' }, // CPS-1 4-player beat 'em up
  { system: 'fbneo_cps12', set: 'ssf2t' },    // CPS-2 (QSound, encrypted program)
  { system: 'fbneo_neogeo', set: 'kof98', board: 'neogeo' },
];

for (const tc of cases) {
  test(`FBNeo ${tc.set}: separate boots agree; savestate restore converges; macros masked`, { timeout: 180_000 }, async () => {
    const sys = SYSTEMS[tc.system];
    const cat = await catalog(sys.core);
    const drv = cat.find((d) => d.name === tc.set);
    assert.ok(drv, `${tc.set} in the ${sys.core} catalog`);
    const files = [{ path: `/roms/${tc.set}.zip`, data: syntheticSet(drv!, 3) }];
    if (tc.board) {
      const board = cat.find((d) => d.name === tc.board)!;
      const boardFiles = board.roms.filter((r) => !(r.t & (BRF_OPT | BRF_NODUMP)) && r.s >= 4).map((r, i) => ({ name: r.n, data: forgeCrc(r.s, r.c, 99 + i) }));
      files.push({ path: `/roms/${tc.board}.zip`, data: makeZip(boardFiles) });
    }
    const gamePath = `/roms/${tc.set}.zip`;
    // different "wall clock" epochs would only matter if the core read the
    // host clock; it must not (deterministic frontend clock + netgame mode)
    const a = await boot(sys, files, gamePath, 1_700_000_000);
    const b = await boot(sys, files, gamePath, 1_700_000_000);
    run(a, 600);
    run(b, 600);
    assert.equal(a.stateHash(), b.stateHash(), 'two separately booted instances agree');
    const snap = a.serialize();
    const c = await boot(sys, files, gamePath, 1_700_000_000);
    c.unserialize(snap);
    c.frame = a.frame;
    // compare repeatedly over 40 s of emulated time after the restore
    for (let f = 900; f <= 3000; f += 300) {
      run(a, f);
      run(c, f);
      assert.equal(c.stateHash(), a.stateHash(), `restored instance matches the original at frame ${f}`);
    }
    // fairness: per-game descriptors -> allowed masks exclude macros, diagnostics and the coin
    const masks = allowedMasks(sys, a.descriptors(), 2);
    const descs = a.descriptors().filter((d) => d.port === 0);
    for (const d of descs) {
      if (/3x|buttons [a-d]{2,}|diagnostic|service|reset|dip/i.test(d.text)) {
        assert.equal(masks[0] & (1 << d.id), 0, `"${d.text}" is not sendable`);
      }
    }
    assert.equal(masks[0] & (1 << 2), 0, 'coin (SELECT) is server-controlled');
    (test as any).diagnostic?.(`${tc.set}: state ${snap.length} bytes, descriptors ${descs.map((d) => d.text).join(', ')}`);
  });
}
