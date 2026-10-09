// Types shared by the room engine and its adapters to the outside world.

import type { AdapterMeta, GameStatus, RoomMode } from '../adapters/types.ts';

export type TgRole = 'creator' | 'administrator' | 'member';

export interface RoomSettings {
  offerSeconds: number;            // seat offer acceptance countdown
  graceSeconds: number;            // disconnected player keeps the seat this long
  hostGraceSeconds: number;        // before acting-host succession
  drawsBeforeRotation: number;     // fighters: consecutive draws before the incumbent rotates
  timedRotationMinutes: number;    // single player without game-over detection (0 = off, room-approved)
  switchTimeoutSeconds: number;    // max wait for a switch point the system cannot detect
  optInSeconds: number;            // "who wants to play the next game" window
  resultDisputeSeconds: number;    // manual result reports: time to agree
  maxViewers: number;
  chatRateCount: number;           // messages per window
  chatRateWindowSeconds: number;
  nominationCooldownSeconds: number;
  ballotCooldownSeconds: number;
  voteSeconds: number;
}

export const DEFAULT_SETTINGS: RoomSettings = {
  offerSeconds: 15,
  graceSeconds: 30,
  hostGraceSeconds: 60,
  drawsBeforeRotation: 2,
  timedRotationMinutes: 0,
  switchTimeoutSeconds: 300,
  optInSeconds: 20,
  resultDisputeSeconds: 120,
  maxViewers: 16,
  chatRateCount: 5,
  chatRateWindowSeconds: 10,
  nominationCooldownSeconds: 60,
  ballotCooldownSeconds: 120,
  voteSeconds: 60,
};

// The running game, as the room sees it. Implemented by the room manager on
// top of a GameSession; implemented by a fake in unit tests.
export interface RoomGame {
  gameId: number;
  title: string;
  sessionId: number;
  mode: RoomMode;
  ports: number;
  adapter: AdapterMeta | null;
  coinBit: number | null;
  status: GameStatus | null;        // latest adapter status (null without adapter)
  frame(): number;
  resetPort(port: number): number;  // new authority epoch, held buttons released
  releasePort(port: number): void;  // release held buttons, same owner
  pulse(port: number, bit: number): void;
}

export interface MatchRecord {
  key: string;
  sessionId: number;
  gameId: number;
  p1: number | null;
  p2: number | null;
  winner: number | null;
  result: 'win' | 'draw' | 'abandoned' | 'interrupted' | 'void';
  verification: 'verified' | 'manual' | 'adjudicated';
  counts: boolean;
}

export interface SegmentRecord {
  sessionId: number;
  port: number;
  userId: number;
  startFrame: number;
  endFrame: number | null;
  startScore: number | null;
  endScore: number | null;
  endReason: string | null;
}

export interface ScoreRecord {
  key: string;
  sessionId: number;
  gameId: number;
  port: number | null;
  score: number;
  kind: 'individual' | 'seat' | 'team' | 'collaborative';
  userId: number | null;
  participants: { user: number; from: number; to: number; points: number | null }[];
  verification: 'verified' | 'manual';
  // continues used during this run (null when the game cannot report them);
  // runs with and without continues are never ranked together
  continues: number | null;
  // the current owner's still-open control segment (not yet in the database)
  openSegment?: { user: number; from: number; startScore: number | null };
}

// Everything the room does to the outside world goes through this interface.
export interface RoomIO {
  now(): number;
  timer(ms: number, fn: () => void): () => void;
  send(userId: number, msg: unknown): void;
  broadcast(msg: unknown): void;
  stateChanged(): void;
  announce(text: string, opts?: { delayMs?: number; key?: string }): void;
  audit(actor: number | null, action: string, target: number | null, reason: string | null, data?: Record<string, unknown>): void;
  recordMatch(m: MatchRecord): boolean;          // false if the key already exists (dedup)
  recordScore(s: ScoreRecord): boolean;
  recordSegment(s: SegmentRecord): void;
  recordEvent(key: string, type: string, frame: number | null, source: string, data: Record<string, unknown>): boolean;
  // Boots and validates the new game, then calls room.setGame(game, seatOrder).
  switchGame(gameId: number, reason: string, seatOrder: number[]): Promise<{ ok: boolean; error?: string }>;
  resetGame(reason: string): Promise<{ ok: boolean; error?: string }>;
  gameTitle(gameId: number): string | null;
  persistRoles(hostId: number | null, deputies: { userId: number; order: number }[]): void;
  // Stores a chat message (modest retention, see CHAT_RETENTION_DAYS); returns its id.
  persistChat(userId: number, text: string, at: number): number;
  deleteChatRow(id: number, by: number): void;
}

export interface PublicSeat {
  port: number;
  userId: number | null;
  name: string | null;
  state: 'empty' | 'active' | 'grace' | 'offered';
  connected: boolean;
  offeredTo: number | null;
  offerExpiresAt: number | null;
  since: number | null;
}
