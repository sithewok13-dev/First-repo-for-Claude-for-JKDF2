// Lockstep correctness against the real NES core and the ATC test ROM:
// two independent replicas joining at different times stay bit-identical with
// the authoritative worker, seat epochs reject stale input, and a crashed
// worker is restored from a checkpoint without disturbing clients.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { GameSession } from '../../server/session/session.ts';
import { SYSTEMS } from '../../shared/systems.ts';
import { PAD } from '../../shared/emu/core.ts';
import { Replica, ROOT, CORE_DIR } from '../helpers/replica.ts';

const sys = SYSTEMS.fceumm;
const rom = readFileSync(join(ROOT, 'native/testroms/build/atc-versus.nes'));
const files = [{ path: '/roms/atc-versus.nes', data: rom }];
const EPOCH = 1_700_000_000;

function session(): GameSession {
  return new GameSession(
    { system: sys, files, gamePath: '/roms/atc-versus.nes', options: {}, ports: 2, adapterId: 'atc-versus' },
    {
      coreDir: CORE_DIR,
      emu: { maxOldSpaceMb: 256, bootTimeoutMs: 20000, requestTimeoutMs: 10000, heartbeatMs: 2000 },
      logSeconds: 60,
      hashEvery: 60,
      statusEvery: 6,
      maxCatchUpFrames: 30,
    },
    EPOCH,
  );
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

async function waitFor(cond: () => boolean, ms = 10000): Promise<void> {
  const t0 = Date.now();
  while (!cond()) {
    if (Date.now() - t0 > ms) throw new Error('timeout');
    await sleep(5);
  }
}

test('two replicas and the authoritative worker stay identical; stale input is rejected', async () => {
  const s = session();
  await s.start();
  const a = await Replica.create(sys, files, '/roms/atc-versus.nes', {}, EPOCH, 2);
  const b = await Replica.create(sys, files, '/roms/atc-versus.nes', {}, EPOCH, 2);
  a.hashEvery = b.hashEvery = 60;
  s.subscribe({ id: 'a', live: false, send: (d) => a.receive(d) });
  assert.ok(await s.sync('a'));

  const press = (port: number, bit: number, seq: number) => {
    assert.ok(s.input(port, s.epochs[port], seq, 1 << bit, 0));
    s.produce(2);
    assert.ok(s.input(port, s.epochs[port], seq + 1, 0, 0));
    s.produce(2);
  };
  s.produce(30);
  press(0, PAD.SELECT, 1); // SELECT is a game button on the NES: the ATC ROM uses it as a coin
  press(1, PAD.SELECT, 1);
  press(0, PAD.START, 3);
  // b joins late, mid-game
  s.subscribe({ id: 'b', live: false, send: (d) => b.receive(d) });
  await waitFor(() => s.workerFrame >= 0);
  assert.ok(await s.sync('b'));
  press(1, PAD.START, 3);
  for (let i = 0; i < 4; i++) press(0, PAD.A, 5 + i * 2);

  // seat changes hands: old epoch input must be rejected
  const oldEpoch = s.epochs[1];
  s.resetPort(1);
  assert.equal(s.input(1, oldEpoch, 100, 1 << PAD.A, 0), false, 'former owner input rejected');
  assert.equal(s.input(1, s.epochs[1], 1, 1 << PAD.A, 0), true, 'new owner accepted');
  s.produce(2);
  assert.equal(s.input(1, s.epochs[1], 1, 0, 0), false, 'replayed sequence rejected');
  assert.ok(s.input(1, s.epochs[1], 2, 0, 0));
  s.produce(200);

  a.advance();
  b.advance();
  await waitFor(() => a.checked >= 4 && b.checked >= 2);
  assert.deepEqual(a.mismatches, []);
  assert.deepEqual(b.mismatches, []);
  assert.equal(a.core.frame, s.nextFrame);
  assert.equal(b.core.frame, s.nextFrame);
  assert.equal(a.core.stateHash(), b.core.stateHash());
  const ram = a.ram();
  assert.equal(ram[0x305] !== 0, true, 'match started');
  assert.equal(ram[0x340], 4, 'P1 hits counted');
  assert.equal(ram[0x341], 1, 'P2 (new owner) hit counted');
  s.stop();
});

test('crashed worker is restored from a checkpoint and replays the log', async () => {
  const s = session();
  await s.start();
  const a = await Replica.create(sys, files, '/roms/atc-versus.nes', {}, EPOCH, 2);
  a.hashEvery = 60;
  s.subscribe({ id: 'a', live: false, send: (d) => a.receive(d) });
  assert.ok(await s.sync('a'));
  s.produce(100);
  await waitFor(() => s.workerFrame >= 60);
  const cp = await s.takeCheckpoint();
  assert.ok(cp);
  s.produce(150);
  let recovered = -1;
  s.on('recovered', (f) => { recovered = f; });
  s.killWorkerForTest();
  await waitFor(() => recovered >= 0, 20000);
  s.produce(200);
  a.advance();
  await waitFor(() => a.hashes.has(420) && a.localHashes.has(420), 20000);
  assert.deepEqual(a.mismatches, []);
  assert.equal(s.stats.workerRestarts, 1);
  s.stop();
});
