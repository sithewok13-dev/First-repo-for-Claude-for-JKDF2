// Verifies the ATC adapters against the real ROMs running in the real
// FCEUmm WebAssembly core. This is the evidence behind the adapters'
// declared capabilities (match results, draws, individual game over,
// continues, stage boundaries, turn ownership, scores, credits).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { Core, PAD } from '../../shared/emu/core.ts';
import { SYSTEMS, portDevicesFor } from '../../shared/systems.ts';
import { ATC_ADAPTERS, ATC_HASHES } from '../../server/adapters/atc.ts';
import type { AdapterEvent, AdapterInstance } from '../../server/adapters/types.ts';
import { findAdapter } from '../../server/adapters/registry.ts';
import { CORE_DIR, ROOT } from '../helpers/replica.ts';

class Rig {
  core!: Core;
  adapter!: AdapterInstance;
  events: AdapterEvent[] = [];
  pads = [0, 0, 0, 0];
  static async create(kind: string): Promise<Rig> {
    const r = new Rig();
    const mod = await import(join(CORE_DIR, 'fceumm.mjs'));
    r.core = await Core.create(mod.default, readFileSync(join(CORE_DIR, 'fceumm.wasm')), { quiet: true });
    for (const [k, v] of Object.entries(SYSTEMS.fceumm.options)) r.core.setOption(k, v);
    const ports = ATC_ADAPTERS.find((a) => a.meta.id === `atc-${kind}`)!.meta.players;
    for (const [p, d] of Object.entries(portDevicesFor(SYSTEMS.fceumm, ports))) r.core.setPortDevice(Number(p), d);
    r.core.writeFile('/roms/game.nes', readFileSync(join(ROOT, `native/testroms/build/atc-${kind}.nes`)));
    assert.ok(r.core.loadGame('/roms/game.nes'));
    r.adapter = ATC_ADAPTERS.find((a) => a.meta.id === `atc-${kind}`)!.create();
    r.run(20);
    return r;
  }
  run(n: number): void {
    for (let i = 0; i < n; i++) {
      for (let p = 0; p < 4; p++) this.core.setInput(p, this.pads[p]);
      this.core.runFrame(false, false);
      this.events.push(...this.adapter.step(this.core.frame, this.core.memory()!));
    }
  }
  press(port: number, bit: number): void {
    this.pads[port] |= 1 << bit;
    this.run(2);
    this.pads[port] &= ~(1 << bit);
    this.run(2);
  }
  status() {
    return this.adapter.status(this.core.frame, this.core.memory()!);
  }
  take(type: string): AdapterEvent[] {
    const out = this.events.filter((e) => e.type === type);
    this.events = this.events.filter((e) => e.type !== type);
    return out;
  }
}

test('pinned ROM hashes match the built test ROMs; registry finds adapters only for exact content', () => {
  for (const k of ['versus', 'coop', 'turns', 'solo']) {
    const h = createHash('sha256').update(readFileSync(join(ROOT, `native/testroms/build/atc-${k}.nes`))).digest('hex');
    assert.equal(h, ATC_HASHES[k], `atc-${k} hash`);
    assert.equal(findAdapter('fceumm', h)?.meta.id, `atc-${k}`);
  }
  assert.equal(findAdapter('fceumm', '0'.repeat(64)), null);
  assert.equal(findAdapter('fbneo_cps12', ATC_HASHES.versus), null, 'wrong system never matches');
});

test('versus: joins, round results, match winner, then the loser enters the continue countdown', async () => {
  const r = await Rig.create('versus');
  r.press(0, PAD.SELECT); r.press(0, PAD.START);
  r.press(1, PAD.SELECT); r.press(1, PAD.START);
  assert.equal(r.take('player_join').length, 2);
  assert.equal(r.status().phase, 'playing');
  for (let round = 0; round < 2; round++) {
    for (let i = 0; i < 5; i++) r.press(1, PAD.A);
    assert.equal(r.take('round_end').length, 1);
    r.run(100);
  }
  const m = r.take('match_end');
  assert.equal(m.length, 1);
  assert.equal(m[0].winnerPort, 1);
  r.run(160);
  const go = r.take('player_game_over');
  assert.deepEqual(go.map((e) => e.port), [0], 'only the loser');
  assert.equal(r.status().players[0].phase, 'continue');
  assert.equal(r.status().players[1].phase, 'playing', 'winner stays in');
});

test('versus: simultaneous final hits are a draw round; five draws are a drawn match', async () => {
  const r = await Rig.create('versus');
  r.press(0, PAD.SELECT); r.press(0, PAD.START);
  r.press(1, PAD.SELECT); r.press(1, PAD.START);
  for (let round = 0; round < 5; round++) {
    for (let i = 0; i < 5; i++) {
      r.pads[0] |= 1 << PAD.A; r.pads[1] |= 1 << PAD.A;
      r.run(2);
      r.pads[0] = 0; r.pads[1] = 0;
      r.run(2);
    }
    r.run(100);
  }
  const m = r.take('match_end');
  assert.equal(m.length, 1);
  assert.equal(m[0].winnerPort, null);
  assert.equal(m[0].data?.draw, true);
});

test('co-op (4 players via Four Score): individual game over, continue, out, stage clear, credits', async () => {
  const r = await Rig.create('coop');
  for (let p = 0; p < 4; p++) { r.press(p, PAD.SELECT); r.press(p, PAD.START); }
  assert.equal(r.take('player_join').length, 4, 'all four ports work');
  assert.equal(r.status().credits, 0);
  for (let i = 0; i < 3; i++) r.press(2, PAD.A);
  assert.equal(r.status().players[2].score, 30);
  for (let i = 0; i < 3; i++) r.press(2, PAD.B); // three lives lost
  const go = r.take('player_game_over');
  assert.deepEqual(go.map((e) => e.port), [2], 'one individual game over, not one per life');
  assert.equal(r.status().players[0].phase, 'playing', 'teammates keep playing');
  r.press(2, PAD.SELECT); r.press(2, PAD.START);
  assert.deepEqual(r.take('player_continue').map((e) => e.port), [2]);
  assert.equal(r.status().players[2].score, 30, 'a continue keeps the run score');
  for (let i = 0; i < 3; i++) r.press(2, PAD.B);
  r.take('player_game_over');
  r.run(11 * 60);
  assert.deepEqual(r.take('player_out').map((e) => e.port), [2]);
  r.run(1200);
  assert.ok(r.take('stage_clear').length >= 1, 'stage boundary detected');
});

test('turns (shared controller): turn ownership follows the game; winner by score', async () => {
  const r = await Rig.create('turns');
  r.press(0, PAD.SELECT); // 3 participants
  r.press(0, PAD.START);
  assert.equal(r.status().turnOwner, 0);
  const turns: number[] = [];
  for (let round = 0; round < 3; round++) {
    for (let p = 0; p < 3; p++) {
      for (let i = 0; i <= p; i++) r.press(0, PAD.A);  // player 3 scores most
      r.press(0, PAD.B);                                // end turn
      turns.push(...r.take('turn_change').map((e) => e.port!));
    }
  }
  assert.deepEqual(turns.slice(0, 5), [0, 1, 2, 0, 1], 'first turn at game start, then the turn order');
  const m = r.take('match_end');
  assert.equal(m.length, 1);
  assert.equal(m[0].winnerPort, 2);
  assert.deepEqual(m[0].scores?.slice(0, 3), [30, 60, 90]);
});

test('solo: game over then game end', async () => {
  const r = await Rig.create('solo');
  r.press(0, PAD.SELECT); r.press(0, PAD.START);
  for (let i = 0; i < 3; i++) r.press(0, PAD.B);
  assert.equal(r.take('player_game_over').length, 1);
  r.run(11 * 60);
  assert.equal(r.take('player_out').length, 1);
  r.run(10);
  assert.equal(r.take('game_over').length, 1);
});
