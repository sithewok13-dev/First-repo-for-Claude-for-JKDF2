// Per-game adapters read verified facts (match results, individual game over,
// stage boundaries, turn ownership, scores, credits) out of emulated RAM.
//
// An adapter is only applied to the exact ROM hashes, system/core build and
// core options it was verified against. Anything else runs without an adapter
// and the room falls back to clearly labelled manual controls.

import type { SystemId } from '../../shared/systems.ts';

export type RoomMode = 'versus' | 'coop' | 'single' | 'turns-shared' | 'turns-multi' | 'collab';

export interface AdapterCapabilities {
  matchResult: boolean;      // complete match winner / draw
  playerGameOver: boolean;   // an individual player is out (continue countdown)
  stageBoundary: boolean;    // a safe point between stages
  turnOwner: boolean;        // whose turn it is in a shared-controller game
  score: boolean;            // per-player score readout
  credits: boolean;          // credit counter readout (needed for coin gating)
  admission: boolean;        // knows when a replacement can enter a vacated slot
}

export interface AdapterMeta {
  id: string;                // stable id, e.g. "atc-versus"
  version: number;           // bump when the logic changes; part of record compatibility
  title: string;
  system: SystemId;
  romSha256: string[];       // exact content the adapter was verified with
  options?: Record<string, string>; // core options the verification assumed
  players: number;
  mode: RoomMode;
  coinBit: number | null;    // RetroPad bit that inserts a coin in this game
  startBit: number | null;
  creditModel: 'shared-pool' | 'per-player' | 'free';
  capabilities: AdapterCapabilities;
  verification: string;      // where the evidence lives (test name / doc)
}

export type PlayerPhase = 'out' | 'playing' | 'continue';

export interface PlayerStatus {
  phase: PlayerPhase;
  lives: number | null;
  score: number | null;
  continueSeconds: number | null;
}

export interface GameStatus {
  frame: number;
  phase: 'attract' | 'playing' | 'round-over' | 'match-over' | 'stage-clear' | 'game-over' | 'unknown';
  credits: number | null;
  round: number | null;
  turnOwner: number | null;   // port index whose turn it is
  players: PlayerStatus[];
}

export type AdapterEventType =
  | 'player_join'
  | 'player_game_over'
  | 'player_continue'
  | 'player_out'
  | 'round_end'
  | 'match_end'
  | 'stage_clear'
  | 'game_over'
  | 'turn_change';

export interface AdapterEvent {
  type: AdapterEventType;
  frame: number;             // emulated frame where the adapter observed it
  port?: number;
  winnerPort?: number | null; // match_end / round_end: null = draw
  scores?: (number | null)[];
  data?: Record<string, unknown>;
}

export interface AdapterInstance {
  // Called after every emulated frame with a live view of system RAM.
  // `frame` is the number of frames executed so far.
  step(frame: number, ram: Uint8Array): AdapterEvent[];
  status(frame: number, ram: Uint8Array): GameStatus;
}

export interface Adapter {
  meta: AdapterMeta;
  create(): AdapterInstance;
}
