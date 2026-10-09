// Deterministic test doubles for the room engine: a manual clock with
// timers, an in-memory RoomIO that records effects, and a fake running game.

import type { AdapterMeta, GameStatus, RoomMode } from '../../server/adapters/types.ts';
import { Room } from '../../server/room/room.ts';
import type { MatchRecord, RoomGame, RoomIO, ScoreRecord, SegmentRecord, RoomSettings } from '../../server/room/types.ts';

export class Clock {
  t = 1_000_000;
  private timers: { at: number; fn: () => void; id: number }[] = [];
  private nextId = 1;
  now = () => this.t;
  timer = (ms: number, fn: () => void) => {
    const id = this.nextId++;
    this.timers.push({ at: this.t + ms, fn, id });
    return () => { this.timers = this.timers.filter((x) => x.id !== id); };
  };
  advance(ms: number): void {
    const end = this.t + ms;
    for (;;) {
      this.timers.sort((a, b) => a.at - b.at || a.id - b.id);
      const next = this.timers[0];
      if (!next || next.at > end) break;
      this.timers.shift();
      this.t = next.at;
      next.fn();
    }
    this.t = end;
  }
}

export class FakeGame implements RoomGame {
  gameId: number;
  title: string;
  sessionId = 42;
  mode: RoomMode;
  ports: number;
  adapter: AdapterMeta | null;
  coinBit: number | null;
  status: GameStatus | null = null;
  f = 0;
  epochs: number[];
  released: number[] = [];
  pulses: { port: number; bit: number }[] = [];
  constructor(mode: RoomMode, ports: number, adapter: AdapterMeta | null = null, gameId = 1) {
    this.mode = mode;
    this.ports = ports;
    this.adapter = adapter;
    this.coinBit = adapter ? adapter.coinBit : 2;
    this.gameId = gameId;
    this.title = `Game ${gameId}`;
    this.epochs = new Array(ports).fill(1);
  }
  frame = () => this.f;
  resetPort = (p: number) => { this.epochs[p]++; return this.epochs[p]; };
  releasePort = (p: number) => { this.released.push(p); };
  pulse = (port: number, bit: number) => { this.pulses.push({ port, bit }); };
}

export function adapterMeta(mode: RoomMode, caps: Partial<AdapterMeta['capabilities']> = {}, players = 2): AdapterMeta {
  return {
    id: 'fake', version: 1, title: 'Fake', system: 'fceumm', romSha256: ['x'], players, mode, coinBit: 2, startBit: 3, creditModel: 'shared-pool',
    capabilities: { matchResult: false, playerGameOver: false, stageBoundary: false, turnOwner: false, score: false, credits: false, admission: false, ...caps },
    verification: 'test',
  };
}

export interface Recorded {
  sent: { userId: number; msg: any }[];
  broadcasts: any[];
  announcements: string[];
  audits: { actor: number | null; action: string; target: number | null; reason: string | null }[];
  matches: MatchRecord[];
  scores: ScoreRecord[];
  segments: SegmentRecord[];
  events: Set<string>;
  switches: { gameId: number; reason: string }[];
  resets: string[];
}

export function makeRoom(opts: { settings?: Partial<RoomSettings>; switchOk?: boolean; titles?: Record<number, string> } = {}) {
  const clock = new Clock();
  const rec: Recorded = { sent: [], broadcasts: [], announcements: [], audits: [], matches: [], scores: [], segments: [], events: new Set(), switches: [], resets: [] };
  let room!: Room;
  const io: RoomIO = {
    now: clock.now,
    timer: clock.timer,
    send: (userId, msg) => rec.sent.push({ userId, msg }),
    broadcast: (msg) => rec.broadcasts.push(msg),
    stateChanged: () => {},
    announce: (text) => rec.announcements.push(text),
    audit: (actor, action, target, reason) => rec.audits.push({ actor, action, target, reason }),
    recordMatch: (m) => {
      if (rec.matches.some((x) => x.key === m.key)) return false;
      rec.matches.push(m);
      return true;
    },
    recordScore: (s) => {
      if (rec.scores.some((x) => x.key === s.key)) return false;
      rec.scores.push(s);
      return true;
    },
    recordSegment: (s) => { rec.segments.push(s); },
    recordEvent: (key) => {
      if (rec.events.has(key)) return false;
      rec.events.add(key);
      return true;
    },
    switchGame: async (gameId, reason, seatOrder) => {
      rec.switches.push({ gameId, reason });
      if (opts.switchOk === false) return { ok: false, error: 'boot failed' };
      room.setGame(new FakeGame(room.game?.mode ?? 'versus', room.game?.ports ?? 2, null, gameId), seatOrder);
      return { ok: true };
    },
    resetGame: async (reason) => { rec.resets.push(reason); return { ok: true }; },
    gameTitle: (gameId) => (opts.titles ?? { 1: 'Game 1', 2: 'Game 2', 3: 'Game 3' })[gameId] ?? null,
    persistRoles: () => {},
    persistChat: () => 0,
    deleteChatRow: () => {},
  };
  room = new Room(1, io, { offerSeconds: 15, graceSeconds: 30, hostGraceSeconds: 60, ...(opts.settings ?? {}) });
  const join = (userId: number, role: 'creator' | 'administrator' | 'member' = 'member', conn = `c${userId}`) =>
    room.connect(userId, conn, { name: `U${userId}`, tgRole: role });
  return { room, clock, rec, io, join };
}
