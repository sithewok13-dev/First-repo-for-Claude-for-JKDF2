// Next-game ballots.
//
// A ballot answers two separate questions:
//   1. Authorize a switch?  "Keep playing" vs. all game options combined.
//   2. Which game?          plurality among game options, no tie allowed.
// A switch happens only if a quorum voted, a strict majority of ballots chose
// some game over "Keep playing", AND one game strictly leads the others. A
// fragmented field or a tie is inconclusive, and inconclusive keeps the
// current game.

export const KEEP = 'keep';

export interface BallotRules {
  durationMs: number;
  quorumFraction: number;      // of the electorate
  minQuorum: number;           // absolute minimum ballots
}

export const DEFAULT_RULES: BallotRules = { durationMs: 60_000, quorumFraction: 0.5, minQuorum: 2 };

export interface Nomination {
  gameId: number;
  title: string;
  nominatedBy: number[];       // merged duplicate nominations
  at: number;
}

export interface BallotOutcome {
  decided: 'switch' | 'keep';
  reason: string;
  gameId: number | null;
  tally: Record<string, number>;
  ballots: number;
  electorate: number;
  quorum: number;
}

export class Ballot {
  readonly id: string;
  readonly openedAt: number;
  readonly closesAt: number;
  readonly openedBy: number;
  readonly rules: BallotRules;
  readonly currentGameId: number | null;
  nominations: Nomination[] = [];
  private ballots = new Map<number, string>();   // userId -> KEEP | gameId
  // Members who were present at any time while the ballot was open. A
  // member who leaves keeps their cast ballot; late arrivals may vote.
  readonly electorate = new Set<number>();
  closed = false;

  constructor(id: string, openedBy: number, now: number, rules: BallotRules, currentGameId: number | null, present: Iterable<number>) {
    this.id = id;
    this.openedBy = openedBy;
    this.openedAt = now;
    this.closesAt = now + rules.durationMs;
    this.rules = rules;
    this.currentGameId = currentGameId;
    for (const u of present) this.electorate.add(u);
  }

  arrive(userId: number): void {
    if (!this.closed) this.electorate.add(userId);
  }

  // Adds a nomination or merges a duplicate. Returns the nomination.
  nominate(userId: number, gameId: number, title: string, now: number): Nomination {
    if (this.closed) throw new RoomError('ballot_closed', 'This vote has closed.');
    if (gameId === this.currentGameId) throw new RoomError('already_playing', 'That game is already running; vote "Keep playing" instead.');
    let n = this.nominations.find((x) => x.gameId === gameId);
    if (!n) {
      if (this.nominations.length >= 6) throw new RoomError('too_many_nominations', 'This vote already has the maximum of 6 games.');
      n = { gameId, title, nominatedBy: [], at: now };
      this.nominations.push(n);
    }
    if (!n.nominatedBy.includes(userId)) n.nominatedBy.push(userId);
    return n;
  }

  // One ballot per eligible member; changeable until the ballot closes.
  vote(userId: number, choice: string): void {
    if (this.closed) throw new RoomError('ballot_closed', 'This vote has closed.');
    if (!this.electorate.has(userId)) throw new RoomError('not_eligible', 'Only people in the room can vote.');
    if (choice !== KEEP && !this.nominations.some((n) => String(n.gameId) === choice)) {
      throw new RoomError('bad_choice', 'That game is not on this ballot.');
    }
    this.ballots.set(userId, choice);
  }

  retract(userId: number): void {
    if (!this.closed) this.ballots.delete(userId);
  }

  get ballotCount(): number {
    return this.ballots.size;
  }

  choiceOf(userId: number): string | null {
    return this.ballots.get(userId) ?? null;
  }

  quorum(): number {
    return Math.max(this.rules.minQuorum, Math.ceil(this.electorate.size * this.rules.quorumFraction));
  }

  // Everyone currently present has voted: the ballot may close early.
  allPresentVoted(present: Iterable<number>): boolean {
    for (const u of present) if (!this.ballots.has(u)) return false;
    return true;
  }

  tally(): Record<string, number> {
    const t: Record<string, number> = { [KEEP]: 0 };
    for (const n of this.nominations) t[String(n.gameId)] = 0;
    for (const c of this.ballots.values()) t[c] = (t[c] ?? 0) + 1;
    return t;
  }

  outcome(): BallotOutcome {
    const tally = this.tally();
    const ballots = this.ballots.size;
    const quorum = this.quorum();
    const base = { tally, ballots, electorate: this.electorate.size, quorum };
    if (ballots < quorum) return { ...base, decided: 'keep', reason: `No quorum (${ballots} of ${quorum} needed votes).`, gameId: null };
    const switchVotes = ballots - tally[KEEP];
    if (switchVotes * 2 <= ballots) return { ...base, decided: 'keep', reason: `"Keep playing" was not outvoted (${switchVotes} of ${ballots} wanted a change).`, gameId: null };
    const games = this.nominations.map((n) => ({ id: n.gameId, votes: tally[String(n.gameId)] ?? 0 })).sort((a, b) => b.votes - a.votes);
    if (games.length === 0 || games[0].votes === 0) return { ...base, decided: 'keep', reason: 'No game received votes.', gameId: null };
    if (games.length > 1 && games[1].votes === games[0].votes) {
      return { ...base, decided: 'keep', reason: 'Tie between games: inconclusive, so the current game continues.', gameId: null };
    }
    return { ...base, decided: 'switch', reason: `Switch approved (${switchVotes} of ${ballots}); next game has ${games[0].votes} votes.`, gameId: games[0].id };
  }

  close(): BallotOutcome {
    this.closed = true;
    return this.outcome();
  }
}

export class RoomError extends Error {
  readonly code: string;
  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}
