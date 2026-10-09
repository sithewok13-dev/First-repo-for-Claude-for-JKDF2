// Seats, queue fairness, offers, grace periods, multi-device control and
// coin policy.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { FakeGame, adapterMeta, makeRoom } from '../helpers/fake-room.ts';

test('concurrent seat claims: exactly one wins, the other gets a clear error', () => {
  const { room, join } = makeRoom();
  join(1); join(2);
  room.setGame(new FakeGame('single', 1));
  room.takeSeat(1, 'c1');
  assert.throws(() => room.takeSeat(2, 'c2'), /taken|queue/);
  assert.equal(room.seats[0].userId, 1);
});

test('one seat per person; a seated player cannot also queue; one queue entry each', () => {
  const { room, join } = makeRoom();
  join(1); join(2); join(3);
  room.setGame(new FakeGame('versus', 2));
  room.takeSeat(1, 'c1');
  assert.throws(() => room.takeSeat(1, 'c1'), /already have a seat/);
  assert.throws(() => room.joinQueue(1), /cannot also hold/);
  room.takeSeat(2, 'c2');
  room.joinQueue(3);
  room.joinQueue(3);
  assert.deepEqual(room.queue.map((q) => q.userId), [3]);
});

test('no queue jumping: free seat goes to the queue head via an offer', () => {
  const { room, join, rec } = makeRoom();
  for (const u of [1, 2, 3, 4]) join(u);
  room.setGame(new FakeGame('versus', 2));
  room.takeSeat(1, 'c1');
  room.takeSeat(2, 'c2');
  room.joinQueue(3);
  room.joinQueue(4);
  room.leaveSeat(2);
  assert.throws(() => room.takeSeat(4, 'c4'), /waiting|offered/);
  const offer = rec.sent.find((s) => s.userId === 3 && s.msg.t === 'offer');
  assert.ok(offer, 'queue head got the offer');
  room.acceptOffer(3, 'c3');
  assert.equal(room.seats[1].userId, 3);
  assert.deepEqual(room.queue.map((q) => q.userId), [4]);
});

test('unresponsive candidate is skipped without stalling; fair re-entry then removal', () => {
  const { room, join, clock } = makeRoom();
  for (const u of [1, 2, 3, 4]) join(u);
  room.setGame(new FakeGame('single', 1));
  room.takeSeat(1, 'c1');
  room.joinQueue(3);
  room.joinQueue(4);
  room.leaveSeat(1);
  // 3 is offered first; ignores it
  clock.advance(15_001);
  assert.deepEqual(room.queue.map((q) => q.userId), [4, 3], 'missed once: moved to the back');
  assert.equal(room.publicSeats()[0].offeredTo, 4, 'next person offered immediately');
  room.declineOffer(4);
  assert.equal(room.publicSeats()[0].offeredTo, 3);
  clock.advance(15_001);
  assert.deepEqual(room.queue.map((q) => q.userId), [], 'second consecutive miss: removed from the queue');
  assert.equal(room.publicSeats()[0].state, 'empty');
});

test('joining the queue never interrupts players', () => {
  const { room, join } = makeRoom();
  for (const u of [1, 2, 3]) join(u);
  const g = new FakeGame('versus', 2);
  room.setGame(g);
  room.takeSeat(1, 'c1');
  room.takeSeat(2, 'c2');
  const epochs = [...g.epochs];
  room.joinQueue(3);
  assert.deepEqual(g.epochs, epochs, 'no seat authority changed');
  assert.equal(room.seats[0].userId, 1);
  assert.equal(room.seats[1].userId, 2);
});

test('disconnect: buttons released at once, grace keeps the seat, return restores control with a new epoch', () => {
  const { room, join, clock } = makeRoom();
  join(1);
  const g = new FakeGame('single', 1);
  room.setGame(g);
  room.takeSeat(1, 'c1');
  const e1 = g.epochs[0];
  room.disconnect('c1');
  assert.ok(g.released.includes(0), 'held buttons released immediately');
  assert.equal(room.publicSeats()[0].state, 'grace');
  assert.equal(room.inputAllowed('c1', 0), false);
  clock.advance(10_000);
  join(1, 'member', 'c1b');
  assert.equal(room.seats[0].userId, 1);
  assert.ok(g.epochs[0] > e1, 'new epoch for the new connection');
  assert.equal(room.inputAllowed('c1b', 0), true);
  assert.equal(room.inputAllowed('c1', 0), false, 'old connection can never send again');
});

test('grace expiry frees the seat as a connectivity failure, not a loss', () => {
  const { room, join, clock, rec } = makeRoom();
  join(1); join(2); join(3);
  room.setGame(new FakeGame('versus', 2));
  room.takeSeat(1, 'c1');
  room.takeSeat(2, 'c2');
  (room as any).matchOwners = [1, 2];
  room.disconnect('c2');
  clock.advance(30_001);
  assert.equal(room.seats[1].userId, null);
  assert.equal(rec.matches.length, 1);
  assert.equal(rec.matches[0].result, 'interrupted');
  assert.equal(rec.matches[0].counts, false, 'disconnects are never counted as a competitive loss');
});

test('multiple devices: only the controlling connection sends input; take control here', () => {
  const { room, join } = makeRoom();
  join(1, 'member', 'phone');
  join(1, 'member', 'laptop');
  const g = new FakeGame('single', 1);
  room.setGame(g);
  room.takeSeat(1, 'phone');
  assert.equal(room.inputAllowed('phone', 0), true);
  assert.equal(room.inputAllowed('laptop', 0), false);
  room.takeControlHere(1, 'laptop');
  assert.equal(room.inputAllowed('phone', 0), false);
  assert.equal(room.inputAllowed('laptop', 0), true);
  // closing the controlling device hands control to the other device, no grace
  room.disconnect('laptop');
  assert.equal(room.inputAllowed('phone', 0), true);
  assert.equal(room.publicSeats()[0].state, 'active');
});

test('spectators can never drive a port', () => {
  const { room, join } = makeRoom();
  join(1); join(2);
  room.setGame(new FakeGame('versus', 2));
  room.takeSeat(1, 'c1');
  assert.equal(room.inputAllowed('c2', 0), false);
  assert.equal(room.inputAllowed('c2', 1), false);
});

test('coins: just-in-time credits, no banking, rate limit, blocked when rotation decided', () => {
  const { room, join, clock } = makeRoom();
  join(1); join(2); join(3);
  const g = new FakeGame('coop', 2, adapterMeta('coop', { credits: true, playerGameOver: true }));
  room.setGame(g);
  room.takeSeat(1, 'c1');
  room.takeSeat(2, 'c2');
  g.status = { frame: 0, phase: 'playing', credits: 0, round: 1, turnOwner: null, players: [] };
  room.insertCoin(1);
  assert.equal(g.pulses.length, 1);
  // before the adapter has seen the coin, a second player's coin would bank a credit
  assert.throws(() => room.insertCoin(2), /already a credit/, 'a just-inserted coin counts until the adapter reports it');
  g.status = { ...g.status, frame: 30, credits: 1 };
  clock.advance(2000);
  assert.throws(() => room.insertCoin(1), /already a credit/);
  g.status = { ...g.status, frame: 60, credits: 0 };
  assert.doesNotThrow(() => room.insertCoin(1));
  assert.throws(() => room.insertCoin(1), /Wait a moment/);
  assert.throws(() => room.insertCoin(3), /Only seated/);
});
