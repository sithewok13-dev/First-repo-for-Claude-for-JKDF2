// Contract of the private per-group game shelf (uploads, validation,
// metadata, private storage). Implemented in ./shelf.ts.

import type { RoomMode } from '../adapters/types.ts';
import type { WorkerFile } from '../emu/messages.ts';
import type { SystemId } from '../../shared/systems.ts';

export type GameStatus = 'ready' | 'needs_dependency' | 'needs_attention' | 'rejected' | 'removed';
export type Compat = 'untested' | 'working' | 'needs_attention';

export interface ValidationFinding {
  level: 'info' | 'warn' | 'error';
  code: string;          // machine readable, e.g. "missing_bios", "crc_mismatch", "zip_bomb"
  message: string;       // human readable, safe to show
}

export interface Validation {
  checkedAt: number;
  identifiedAs: string | null;       // e.g. "FBNeo set sf2 (Street Fighter II: The World Warrior (World 910522))"
  findings: ValidationFinding[];
  missing: { kind: 'bios' | 'parent' | 'samples'; set: string; files: string[] }[];
  boot: { tried: boolean; ok: boolean; frames: number; detail: string } | null;
  matchedRoms?: number;
  totalRoms?: number;
}

export interface ShelfGame {
  id: number;
  groupId: number;
  displayName: string;
  fileName: string;
  kind: 'game' | 'bios' | 'parent';
  system: SystemId | null;
  setName: string | null;
  parentSet: string | null;
  biosSet: string | null;
  players: number | null;
  mode: RoomMode | null;
  genre: string | null;
  year: string | null;
  manufacturer: string | null;
  status: GameStatus;
  compat: Compat;
  validation: Validation;
  metadata: { notes?: string; tags?: string[]; artwork?: string | null; handoffRule?: string };
  uploaderId: number | null;
  uploaderName: string | null;
  uploadedAt: number;
  lastPlayedAt: number | null;
  playCount: number;
  favorite: boolean;
  sizeBytes: number;
  sha256: string;
  adapter: { id: string; version: number; capabilities: Record<string, boolean> } | null;
}

export interface IngestRequest {
  groupId: number;
  userId: number;
  fileName: string;                  // as given by the uploader (untrusted)
  source: 'telegram' | 'web';
  tempPath: string;                  // file on disk, already within size limits; ingest moves or deletes it
  size: number;
  tg?: { fileId: string; fileUniqueId: string; messageId: number };
}

export interface IngestResult {
  uploadId: number;
  status: 'done' | 'failed';
  game?: ShelfGame;
  error?: string;
  duplicate?: boolean;               // the same content is already on THIS group's shelf
  findings?: ValidationFinding[];    // failed uploads: why (safe to show); nothing was stored
}

export interface ClientFile {
  path: string;                      // path in the emulator filesystem
  sha256: string;
  size: number;
}

export interface SessionGameSpec {
  gameId: number;
  title: string;
  system: SystemId;
  files: WorkerFile[];               // for the server's emulation worker
  clientFiles: ClientFile[];         // what browsers must download (by hash)
  gamePath: string;
  options: Record<string, string>;
  ports: number;
  mode: RoomMode;
  adapterId: string | null;
  compatKey: string;                 // rom hashes + core build + options + adapter version
}

export interface ShelfService {
  ingest(req: IngestRequest): Promise<IngestResult>;
  list(groupId: number, viewerId: number): ShelfGame[];
  get(groupId: number, gameId: number, viewerId?: number): ShelfGame | null;
  update(groupId: number, gameId: number, actor: number, patch: { displayName?: string; mode?: RoomMode; players?: number; notes?: string; handoffRule?: string }): ShelfGame;
  setFavorite(groupId: number, userId: number, gameId: number, favorite: boolean): void;
  remove(groupId: number, gameId: number, actor: number): void;
  sessionSpec(groupId: number, gameId: number): Promise<SessionGameSpec>;
  // A blob may be read by a group only if one of its non-removed shelf
  // entries references it, or it is pinned for that group's active session.
  blobForGroup(groupId: number, sha256: string): { path: string; size: number } | null;
  pin(groupId: number, sha256s: string[]): void;
  unpin(groupId: number): void;
  usage(groupId: number): { bytes: number; quota: number; games: number };
  markPlayed(groupId: number, gameId: number, compat?: 'working' | 'needs_attention'): void;
  cleanup(now?: number): { deletedBlobs: number; freedBytes: number };
}
