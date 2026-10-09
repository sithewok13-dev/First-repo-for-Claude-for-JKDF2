// Mode rules: fighters (winner stays), co-op/single (rotate at individual
// game over), manual results and disputes, turn-based shared controller,
// event deduplication, score attribution inputs.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { FakeGame, adapterMeta, makeRoom } from '../helpers/fake-room.ts';

function versus(caps = { matchResult: true }) {
  const r = makeRoom();
  for (const u of [1, 2, 3, 4]) r.join(u);
  const g = new FakeGame('versus', 2, adapterMeta('versus', caps));
  r.room.setGame(g);
  r.room.takeSeat(1, 'c1');
  r.room.takeSeat(2, 'c2');
  g.status = { frame: 0, phase: 'playing', credits: 0, round: 1, turnOwner: null, players: [] };
  r.room.onStatus();
  return { ...r, g };
}

test('fighters: winner stays with no streak limit; loser to the back of the queue', () => {
  const { room, g, rec } = versus();
  room.joinQueue(3);
  room.joinQueue(4);
  for (let i = 0; i < 10; i++) {
    g.f += 1000;
    const challengerPort = 1;
    room.onAdapterEvents([{ type: 'match_end', frame: g.f, winnerPort: 0 }]);
    // challenger seat offered to the queue head; accept
    const head = room.publicSeats()[challengerPort].offeredTo!;
    room.acceptOffer(head, `c${head}`);
    room.onStatus();
  }
  assert.equal(room.seats[0].userId, 1, 'winner never removed, 10 straight wins');
  assert.equal(rec.matches.length, 10);
  assert.equal(room.queue.length, 2, 'everyone else keeps cycling');
});

test('fighters: with nobody waiting the same players rematch indefinitely', () => {
  const { room, g } = versus();
  for (let i = 0; i < 5; i++) {
    g.f += 1000;
    room.onAdapterEvents([{ type: 'match_end', frame: g.f, winnerPort: i % 2 }]);
    room.onStatus();
  }
  assert.equal(room.seats[0].userId, 1);
  assert.equal(room.seats[1].userId, 2);
});

test('fighters: rotation after a complete match, not a round', () => {
  const { room, g } = versus();
  room.joinQueue(3);
  g.f = 500;
  room.onAdapterEvents([{ type: 'round_end', frame: g.f }]);
  assert.equal(room.seats[1].userId, 2);
});

test('fighters: draws — first draw replays, repeated draws rotate the longer-seated player', () => {
  const { room, g, clock } = versus();
  room.joinQueue(3);
  clock.advance(1000);
  g.f = 1000;
  room.onAdapterEvents([{ type: 'match_end', frame: g.f, winnerPort: null }]);
  assert.equal(room.seats[0].userId, 1);
  assert.equal(room.seats[1].userId, 2);
  room.onStatus();
  g.f = 2000;
  room.onAdapterEvents([{ type: 'match_end', frame: g.f, winnerPort: null }]);
  assert.equal(room.seats[0].userId, null, 'player 1 sat down first, rotates after the second draw');
  assert.ok(room.queue.some((q) => q.userId === 1));
});

test('event deduplication: the same adapter event twice never rotates or records twice', () => {
  const { room, g, rec } = versus();
  room.joinQueue(3);
  g.f = 900;
  const ev = { type: 'match_end' as const, frame: 900, winnerPort: 0 };
  room.onAdapterEvents([ev]);
  room.onAdapterEvents([ev]);
  assert.equal(rec.matches.length, 1);
  assert.equal(room.queue.filter((q) => q.userId === 2).length, 1);
});

test('mixed control: a match where a seat changed hands mid-match is not counted', () => {
  const { room, g, rec } = versus();
  room.joinQueue(3);
  room.leaveSeat(2);       // abandons mid-match -> recorded as abandoned (not a loss)
  room.acceptOffer(3, 'c3');
  g.f = 1500;
  room.onAdapterEvents([{ type: 'match_end', frame: g.f, winnerPort: 0 }]);
  const abandoned = rec.matches.find((m) => m.result === 'abandoned');
  assert.ok(abandoned && !abandoned.counts);
  const last = rec.matches[rec.matches.length - 1];
  assert.equal(last.counts, false, 'result after a mid-match handoff is excluded from stats');
});

test('manual results: concession accepted, conflicting claims disputed and adjudicated', () => {
  const { room, rec, g } = versus({ matchResult: false });
  room.joinQueue(3);
  room.reportResult(2, 'lost');
  assert.equal(rec.matches.length, 1);
  assert.equal(rec.matches[0].winner, 1);
  assert.equal(rec.matches[0].verification, 'manual');
  room.acceptOffer(3, 'c3');
  room.onStatus();
  room.reportResult(1, 'won');
  room.reportResult(3, 'won');
  assert.equal(room.disputed, true);
  assert.throws(() => room.adjudicate(3, 0, 'x'), /host or a deputy/);
  room.setHost(4);
  g.f = 77;
  room.adjudicate(4, 0, 'watched it');
  assert.equal(rec.matches[rec.matches.length - 1].verification, 'adjudicated');
  assert.equal(room.disputed, false);
});

test('manual results: an unanswered report stands after the dispute window', () => {
  const { room, rec, clock } = versus({ matchResult: false });
  room.reportResult(1, 'won');
  assert.equal(rec.matches.length, 0);
  clock.advance(120_001);
  assert.equal(rec.matches.length, 1);
  assert.equal(rec.matches[0].winner, 1);
});

test('co-op: rotate at an individual game over only when someone waits; surviving teammates stay', () => {
  const r = makeRoom();
  for (const u of [1, 2, 3]) r.join(u);
  const g = new FakeGame('coop', 2, adapterMeta('coop', { playerGameOver: true, credits: true }));
  r.room.setGame(g);
  r.room.takeSeat(1, 'c1');
  r.room.takeSeat(2, 'c2');
  // nobody waiting: unlimited continues
  r.room.onAdapterEvents([{ type: 'player_game_over', frame: 100, port: 0 }]);
  assert.equal(r.room.seats[0].userId, 1);
  assert.equal(r.room.seats[0].decision, 'continue');
  r.room.onAdapterEvents([{ type: 'player_continue', frame: 150, port: 0 }]);
  // a lost life is not a game over: nothing happens on life loss events
  r.room.joinQueue(3);
  assert.equal(r.room.seats[0].userId, 1, 'queue entry does not eject anyone mid-life');
  r.room.onAdapterEvents([{ type: 'player_game_over', frame: 400, port: 0 }]);
  assert.equal(r.room.seats[0].userId, null, 'player 1 rotates out');
  assert.equal(r.room.seats[1].userId, 2, 'surviving teammate keeps playing');
  assert.equal(r.room.publicSeats()[0].offeredTo, 3, 'replacement offered the vacated slot');
  assert.ok(r.room.queue.some((q) => q.userId === 1), 'rotated player re-queued at the back');
  r.room.acceptOffer(3, 'c3');
  assert.equal(r.room.seats[0].userId, 3);
});

test('co-op: the continue decision is locked at the game-over moment', () => {
  const r = makeRoom();
  for (const u of [1, 2]) r.join(u);
  const g = new FakeGame('coop', 2, adapterMeta('coop', { playerGameOver: true, credits: true }));
  r.room.setGame(g);
  r.room.takeSeat(1, 'c1');
  r.room.onAdapterEvents([{ type: 'player_game_over', frame: 100, port: 0 }]);
  r.room.joinQueue(2); // arrives during the continue countdown
  g.status = { frame: 0, phase: 'playing', credits: 0, round: 1, turnOwner: null, players: [] };
  assert.doesNotThrow(() => r.room.insertCoin(1), 'continue allowed: nobody was waiting at game over');
});

test('co-op without detection: manual "I\'m out" does the same rotation', () => {
  const r = makeRoom();
  for (const u of [1, 2, 3]) r.join(u);
  r.room.setGame(new FakeGame('coop', 2));
  r.room.takeSeat(1, 'c1');
  r.room.joinQueue(3);
  r.room.reportOut(1);
  assert.equal(r.room.seats[0].userId, null);
  assert.throws(() => r.room.reportOut(3, 1), /host or a deputy|not seated/);
});

test('single player: pass controller goes to the queue head and needs acceptance', () => {
  const r = makeRoom();
  for (const u of [1, 2, 3]) r.join(u);
  r.room.setGame(new FakeGame('single', 1));
  r.room.takeSeat(1, 'c1');
  r.room.joinQueue(2);
  r.room.joinQueue(3);
  assert.throws(() => r.room.passController(1, 3), /next person/);
  r.room.passController(1);
  assert.equal(r.room.seats[0].userId, 1, 'still in control until accepted');
  r.room.acceptOffer(2, 'c2');
  assert.equal(r.room.seats[0].userId, 2);
});

test('turn-based shared controller: turn order separate from queue, acceptance, stuck turns resolved', () => {
  const r = makeRoom();
  for (const u of [1, 2, 3, 9]) r.join(u);
  const g = new FakeGame('turns-shared', 4);
  r.room.setGame(g);
  r.room.joinTurns(1);
  r.room.joinTurns(2);
  assert.deepEqual(r.room.participants, [1, 2]);
  assert.equal(r.room.seats[0].userId, 1);
  r.room.endTurn(1);
  assert.equal(r.room.seats[0].userId, 1, 'controller stays until the next player accepts');
  r.room.acceptOffer(2, 'c2');
  assert.equal(r.room.seats[0].userId, 2);
  assert.deepEqual(r.room.participants, [1, 2], 'completing a turn does not remove anyone');
  // nobody accepts: stuck, never played automatically
  r.room.endTurn(2);
  r.clock.advance(60_000);
  assert.equal(r.room.seats[0].userId, 2);
  assert.throws(() => r.room.resolveController(3, 1, 'x'), /host or a deputy/);
  r.room.setHost(9);
  r.room.resolveController(9, 1, 'player 1 is back');
  r.room.acceptOffer(1, 'c1');
  assert.equal(r.room.seats[0].userId, 1);
});

test('turn-based: spectators wait for an admission point; leaving player is replaced in place', () => {
  const r = makeRoom();
  for (const u of [1, 2, 3]) r.join(u);
  const g = new FakeGame('turns-shared', 4);
  r.room.setGame(g);
  r.room.joinTurns(1);
  r.room.joinTurns(2);
  r.room.endTurn(1);
  r.room.acceptOffer(2, 'c2');          // the first turn is complete: the game is under way
  g.status = { frame: 0, phase: 'playing', credits: null, round: 1, turnOwner: 0, players: [] };
  r.room.joinTurns(3);
  assert.deepEqual(r.room.participants, [1, 2], 'not an admission point');
  assert.deepEqual(r.room.queue.map((q) => q.userId), [3]);
  r.room.leaveTurns(2);
  assert.deepEqual(r.room.participants, [1, 3], 'replacement takes the same place in the order');
  assert.equal(r.room.publicSeats()[0].offeredTo, 3, 'the replacement is offered the turn in progress');
});

test('turn-based with a verified adapter: turn changes hand control to the right participant', () => {
  const r = makeRoom();
  for (const u of [1, 2]) r.join(u);
  const g = new FakeGame('turns-shared', 4, adapterMeta('turns-shared', { turnOwner: true }, 4));
  r.room.setGame(g);
  r.room.joinTurns(1);
  r.room.joinTurns(2);
  r.room.onAdapterEvents([{ type: 'turn_change', frame: 50, port: 1 }]);
  assert.equal(r.room.seats[0].userId, null, 'controller released at once');
  r.room.acceptOffer(2, 'c2');
  assert.equal(r.room.seats[0].userId, 2);
  assert.throws(() => r.room.endTurn(2), /automatically/);
});

test('scores: run score is recorded once with the open control segment for attribution', () => {
  const r = makeRoom();
  for (const u of [1, 2]) r.join(u);
  const g = new FakeGame('coop', 2, adapterMeta('coop', { playerGameOver: true, score: true }));
  r.room.setGame(g);
  r.room.takeSeat(1, 'c1');
  r.room.onAdapterEvents([{ type: 'player_join', frame: 10, port: 0 }]);
  r.room.onAdapterEvents([{ type: 'player_out', frame: 500, port: 0, scores: [1230, 0] }]);
  r.room.onAdapterEvents([{ type: 'player_out', frame: 500, port: 0, scores: [1230, 0] }]);
  assert.equal(r.rec.scores.length, 1);
  assert.equal(r.rec.scores[0].score, 1230);
  assert.equal(r.rec.scores[0].openSegment?.user, 1);
});

test('scores: continues used in a run are recorded so 1-credit and continued runs are ranked apart', () => {
  const r = makeRoom();
  for (const u of [1, 2]) r.join(u);
  const g = new FakeGame('coop', 2, adapterMeta('coop', { playerGameOver: true, score: true, credits: true }));
  r.room.setGame(g);
  r.room.takeSeat(1, 'c1');
  r.room.onAdapterEvents([{ type: 'player_join', frame: 10, port: 0 }]);
  r.room.onAdapterEvents([{ type: 'player_continue', frame: 200, port: 0 }]);
  r.room.onAdapterEvents([{ type: 'player_continue', frame: 400, port: 0 }]);
  r.room.onAdapterEvents([{ type: 'player_out', frame: 600, port: 0, scores: [5000, 0] }]);
  r.room.onAdapterEvents([{ type: 'player_join', frame: 700, port: 0 }]);
  r.room.onAdapterEvents([{ type: 'player_out', frame: 900, port: 0, scores: [800, 0] }]);
  assert.deepEqual(r.rec.scores.map((s) => [s.score, s.continues]), [[5000, 2], [800, 0]]);
});

test('single player: optional timed rotation (room rule) warns, then hands over only if someone waits', () => {
  const r = makeRoom({ settings: { timedRotationMinutes: 10 } });
  for (const u of [1, 2]) r.join(u);
  r.room.setGame(new FakeGame('single', 1));
  r.room.takeSeat(1, 'c1');
  r.clock.advance(11 * 60_000);
  assert.equal(r.room.seats[0].userId, 1, 'nobody waiting: keep playing');
  r.room.joinQueue(2);
  r.clock.advance(61_000);
  assert.ok(r.rec.sent.some((m) => m.userId === 1 && /30 seconds/.test(m.msg.text ?? '')), 'warning first');
  r.clock.advance(31_000);
  assert.equal(r.room.seats[0].userId, null);
  assert.equal(r.room.publicSeats()[0].offeredTo, 2);
  assert.deepEqual(r.room.queue.map((q) => q.userId), [2, 1]);
});
