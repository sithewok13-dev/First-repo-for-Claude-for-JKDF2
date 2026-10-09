// Roles, succession, moderation limits, voting and game switching.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Ballot, KEEP, DEFAULT_RULES } from '../../server/room/votes.ts';
import { FakeGame, makeRoom } from '../helpers/fake-room.ts';
import type { Room } from '../../server/room/room.ts';

const acting = (r: Room) => r.actingHost; // a call, so assertions do not narrow the type

test('deputies cannot appoint deputies, remove the host or administer; host can', () => {
  const { room, join } = makeRoom();
  join(1); join(2); join(3);
  room.setHost(1);
  room.appointDeputy(1, 2, 'persistent');
  assert.throws(() => room.appointDeputy(2, 3, 'session'), /Only the host/);
  assert.throws(() => room.transferHost(2, 3), /Only the host/);
  assert.throws(() => room.updateSettings(2, { offerSeconds: 5 }), /Only the host/);
  room.setGame(new FakeGame('single', 1));
  room.takeSeat(1, 'c1');
  assert.throws(() => room.removeFromSeat(2, 1, 'afk'), /cannot remove the host/);
  assert.throws(() => room.mute(2, 1, 5, 'x'), /cannot mute the host/);
  assert.throws(() => room.removeFromSeat(3, 1, 'afk'), /host or a deputy/);
});

test('removal needs a listed reason; "winning" is not one, and away/stuck keeps priority', () => {
  const { room, join, rec } = makeRoom();
  for (const u of [1, 2, 3, 4]) join(u);
  room.setHost(1);
  room.setGame(new FakeGame('versus', 2));
  room.takeSeat(2, 'c2');
  room.takeSeat(3, 'c3');
  room.joinQueue(4);
  assert.throws(() => room.removeFromSeat(1, 2, 'winning' as any), /reason/);
  assert.throws(() => room.removeFromSeat(1, 2, 'other', ''), /Explain/);
  room.removeFromSeat(1, 2, 'afk');
  assert.equal(room.queue[0].userId, 2, 'away player keeps priority at the front of the queue');
  assert.ok(rec.audits.some((a) => a.action === 'seat.remove' && a.reason === 'afk'));
});

test('host leaves: room keeps running, 60 s grace, then a designated deputy acts; host return ends it without disruption', () => {
  const { room, join, clock } = makeRoom();
  join(1); join(2, 'administrator'); join(3); join(4);
  room.setHost(1);
  room.appointDeputy(1, 4, 'persistent');
  const g = new FakeGame('single', 1);
  room.setGame(g);
  room.takeSeat(3, 'c3');
  const epochBefore = g.epochs[0];
  room.disconnect('c1');
  clock.advance(59_000);
  assert.equal(room.actingHost, null, 'grace period');
  assert.ok(room.canModerate(4), 'deputy keeps powers meanwhile');
  clock.advance(2000);
  assert.equal(acting(room)?.userId, 4, 'designated deputy preferred over a group admin');
  assert.equal(room.canAdminister(4), false, 'acting host has no permanent/admin powers');
  join(1, 'member', 'c1-again');
  assert.equal(room.actingHost, null);
  assert.equal(room.seats[0].userId, 3);
  assert.equal(g.epochs[0], epochBefore, 'host return does not touch the game');
});

test('succession falls back to a present group admin, then offers a willing established participant', () => {
  const r = makeRoom();
  r.join(1); r.join(2, 'administrator');
  r.room.setHost(1);
  r.room.disconnect('c1');
  r.clock.advance(61_000);
  assert.equal(acting(r.room)?.userId, 2);

  const s = makeRoom();
  s.join(1); s.join(5);
  s.room.setHost(1);
  s.clock.advance(6 * 60_000); // 5 has been present long enough
  s.room.disconnect('c1');
  s.clock.advance(61_000);
  assert.equal(s.room.actingHost, null);
  assert.ok(s.rec.sent.some((m) => m.userId === 5 && m.msg.t === 'acting_host_offer'));
  s.room.answerActingHostOffer(5, true);
  assert.equal(acting(s.room)?.userId, 5);
  assert.equal(acting(s.room)?.kind, 'volunteer');
});

test('ballot: quorum, majority over Keep, clear leader; fragmented or tied -> keep', () => {
  const now = 0;
  const b = new Ballot('b', 1, now, DEFAULT_RULES, 1, [1, 2, 3, 4, 5, 6]);
  b.nominate(1, 2, 'Two', now);
  b.nominate(2, 3, 'Three', now);
  b.nominate(3, 2, 'Two', now);
  assert.equal(b.nominations.length, 2, 'duplicates merged');
  assert.deepEqual(b.nominations[0].nominatedBy, [1, 3]);
  b.vote(1, '2');
  b.vote(2, '3');
  assert.equal(b.outcome().decided, 'keep', 'no quorum (2 of 3)');
  b.vote(3, KEEP);
  assert.equal(b.outcome().decided, 'keep', '2 switch vs 1 keep but tie between games');
  b.vote(4, '2');
  assert.equal(b.outcome().decided, 'switch');
  assert.equal(b.outcome().gameId, 2);
  b.vote(4, KEEP);
  b.vote(5, KEEP);
  assert.equal(b.outcome().decided, 'keep', 'keep not outvoted');
  assert.throws(() => b.vote(99, KEEP), /Only people in the room/);
  b.arrive(99);
  b.vote(99, '3');
  assert.throws(() => b.nominate(1, 1, 'One', now), /already running/);
});

test('ballot: one vote per member, changeable, departures keep cast ballots', () => {
  const b = new Ballot('b', 1, 0, DEFAULT_RULES, null, [1, 2, 3]);
  b.nominate(1, 2, 'Two', 0);
  b.vote(1, '2');
  b.vote(1, '2');
  b.vote(1, KEEP);
  assert.equal(b.ballotCount, 1);
  assert.equal(b.choiceOf(1), KEEP);
});

test('vote -> pending switch waits for the match end (versus), opt-in order preserves waiting players', async () => {
  const { room, join, clock, rec } = makeRoom();
  for (const u of [1, 2, 3, 4, 5]) join(u);
  room.setGame(new FakeGame('versus', 2));
  room.takeSeat(1, 'c1');
  room.takeSeat(2, 'c2');
  room.joinQueue(3);
  room.joinQueue(4);
  room.nominate(5, 2);
  for (const u of [1, 2, 3, 4, 5]) room.vote(u, '2');
  assert.ok(room.pendingSwitch, 'approved');
  assert.equal(rec.switches.length, 0, 'not yet: waiting for the match to finish');
  for (const u of [5, 1, 4, 2, 3]) room.optIn(u, true);
  clock.advance(21_000);       // opt-in window closed; still waiting for the match end
  assert.equal(rec.switches.length, 0);
  room.onAdapterEvents([{ type: 'match_end', frame: 10, winnerPort: 0 }]);
  await new Promise((r) => setImmediate(r));
  assert.equal(rec.switches.length, 1);
  assert.deepEqual(room.seats.map((s) => s.userId), [3, 4], 'waiting players first');
  assert.deepEqual(room.queue.map((q) => q.userId), [1, 2, 5], 'then current players, then other opt-ins');
});

test('pending switch never waits forever: deadline, and failed launch keeps the room intact', async () => {
  const r = makeRoom({ switchOk: false });
  for (const u of [1, 2, 3]) r.join(u);
  r.room.setGame(new FakeGame('single', 1));
  r.room.takeSeat(1, 'c1');
  r.room.joinQueue(3);
  r.room.setHost(2);
  r.room.override(2, 2, 'everyone asked');
  assert.ok(r.room.pendingSwitch);
  r.clock.advance(61_000); // no detection: announced 60 s transition
  await new Promise((res) => setImmediate(res));
  assert.equal(r.rec.switches.length, 1);
  assert.equal(r.room.game?.gameId, 1, 'current game continues');
  assert.equal(r.room.seats[0].userId, 1, 'seats untouched');
  assert.deepEqual(r.room.queue.map((q) => q.userId), [3], 'queue untouched');
  assert.equal(r.room.pendingSwitch, null);
});

test('override needs a reason and moderator powers; cancellation is visible', () => {
  const r = makeRoom();
  for (const u of [1, 2, 3]) r.join(u);
  r.room.setGame(new FakeGame('single', 1));
  r.room.setHost(1);
  assert.throws(() => r.room.override(3, 2, 'because'), /host or a deputy/);
  assert.throws(() => r.room.override(1, 2, ''), /reason/);
  r.room.override(1, 2, 'tournament night');
  r.room.override(1, null, 'changed our minds');
  assert.equal(r.room.pendingSwitch, null);
  assert.ok(r.rec.audits.some((a) => a.action === 'vote.cancel'));
});

test('vote rate limits: cooldown after a ballot closes', () => {
  const r = makeRoom();
  r.join(1); r.join(2);
  r.room.setGame(new FakeGame('single', 1));
  r.room.startBallot(1);
  assert.throws(() => r.room.startBallot(2), /already open/);
  r.clock.advance(61_000);
  assert.throws(() => r.room.startBallot(2), /wait/);
  r.clock.advance(120_000);
  assert.doesNotThrow(() => r.room.startBallot(2));
});

test('chat: plain text, length limit, rate limit, duplicates, mute', () => {
  const r = makeRoom();
  r.join(1); r.join(2);
  r.room.setHost(2);
  assert.equal(r.room.chat(1, '  hi\u0000 there‮ ')?.text, 'hi there');
  assert.throws(() => r.room.chat(1, 'x'.repeat(501)), /limited/);
  assert.throws(() => r.room.chat(1, 'hi there'), /just sent/);
  for (let i = 0; i < 4; i++) r.room.chat(1, `m${i}`);
  assert.throws(() => r.room.chat(1, 'one more'), /Slow down/);
  r.room.mute(2, 1, 5, 'spam');
  r.clock.advance(11_000);
  assert.throws(() => r.room.chat(1, 'muted?'), /muted/);
});

test('a Telegram admin demoted while connected loses moderator powers at once', () => {
  const { room, join } = makeRoom();
  join(1); join(2, 'administrator'); join(3);
  room.setHost(1);
  room.setGame(new FakeGame('single', 1));
  room.takeSeat(3, 'c3');
  assert.ok(room.canModerate(2), 'group admins act as deputies');
  room.setTgRole(2, 'member');
  assert.equal(room.canModerate(2), false);
  assert.throws(() => room.removeFromSeat(2, 3, 'afk'), /host or a deputy/);
  room.setTgRole(2, 'administrator');
  assert.ok(room.canModerate(2), 'promotion applies just as quickly');
});
