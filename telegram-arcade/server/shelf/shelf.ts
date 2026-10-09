// The private per-group game shelf (ShelfService, see ./types.ts).
//
// Ingest: an uploaded file is inspected in a separate validation process
// (archive safety, romset identification or NES header checks), stored once
// in the content-addressed blob store, and becomes a shelf entry of THIS
// group. FBNeo dependencies (parent romsets, the Neo Geo BIOS) are resolved
// against this group's shelf only; when one arrives later, the entries that
// waited for it are boot-tested again.
//
// Group isolation: every query is filtered by group id. Identical content
// on another group's shelf is stored once but is otherwise invisible: the
// upload is validated, quota-checked and recorded exactly like a new file.
//
// Deletion is deferred: remove() only marks an entry removed. The blob stays
// while any group's live entry references it, while a running session has
// it pinned, while a valid checkpoint depends on it, and for
// removedGameRetentionDays after the last removal; cleanup() deletes it then.
//
// Authorization (who may remove entries or edit metadata) is enforced by the
// caller (HTTP/room layer); this module trusts its `actor` arguments.
//
// Artwork: nothing is scraped or downloaded. metadata.artwork stays null and
// clients render a generated placeholder (e.g. from the display name).

import { createHash } from 'node:crypto';
import { readFileSync, statSync } from 'node:fs';
import { unlink } from 'node:fs/promises';
import { join } from 'node:path';
import { portDevicesFor, SYSTEMS, type SystemId } from '../../shared/systems.ts';
import { findAdapter } from '../adapters/registry.ts';
import type { Adapter, RoomMode } from '../adapters/types.ts';
import { displayName as userDisplayName } from '../auth/initdata.ts';
import type { Config } from '../config.ts';
import type { Db, Row } from '../db/db.ts';
import type { WorkerFile } from '../emu/messages.ts';
import type { Groups } from '../groups.ts';
import { log } from '../log.ts';
import { BlobStore, isSha256, sha256Bytes, sha256File } from './blobs.ts';
import { BDF, defaultModeFor, describeSet, genreLabel, type DependencyNeed } from './catalog.ts';
import type {
  ClientFile,
  Compat,
  GameStatus,
  IngestRequest,
  IngestResult,
  SessionGameSpec,
  ShelfGame,
  ShelfService,
  Validation,
  ValidationFinding,
} from './types.ts';
import { ValidationTimeout, Validator, type BootResult, type InspectResult, type ValidatorLike } from './validator.ts';

export type ShelfConfig = Pick<
  Config,
  | 'blobDir'
  | 'coreDir'
  | 'maxUploadBytes'
  | 'maxExtractedBytes'
  | 'maxArchiveEntries'
  | 'maxCompressionRatio'
  | 'globalStorageLimitBytes'
  | 'validationTimeoutMs'
  | 'maxConcurrentValidations'
  | 'removedGameRetentionDays'
  | 'workerHeapMb'
>;

export interface ShelfOptions {
  db: Db;
  cfg: ShelfConfig;
  groups?: Groups;                         // quota lookups (falls back to the groups table)
  validator?: ValidatorLike;               // injectable for tests
  onChange?: (groupId: number, gameId: number) => void;  // an entry was added/changed (status, metadata, removal)
}

export class ShelfError extends Error {
  readonly code: string;
  readonly findings: ValidationFinding[];
  constructor(code: string, message: string, findings: ValidationFinding[] = []) {
    super(message);
    this.name = 'ShelfError';
    this.code = code;
    this.findings = findings;
  }
}

const MODES: readonly RoomMode[] = ['versus', 'coop', 'single', 'turns-shared', 'turns-multi', 'collab'];
const MULTI_SEAT_MODES: readonly RoomMode[] = ['versus', 'coop', 'turns-multi'];
const SET_NAME = /^[a-z0-9_]{1,40}$/;
const BOOT_MIN_FRAMES = 300;
const BOOT_MAX_FRAMES = 600;
const DAY_MS = 86_400_000;
const NES_PATH = '/roms/game.nes';

// Validation details that are stored with the entry but not shown to clients.
interface Internal {
  identified: boolean;           // a valid NES ROM, or an identified romset
  complete: boolean;             // all of the set's own required files are present
  base: ValidationFinding[];     // inspection findings (dependency/boot findings are recomputed)
  needs: DependencyNeed[];       // files that come from the parent / board zip
  catalogPlayers?: number;       // FBNeo: inputs the driver has
  fourScore?: boolean;           // NES: four controllers supported (header, adapter or editor)
  notWorking?: boolean;          // FBNeo flags the set as not working
}

type StoredValidation = Validation & { _x?: Internal };

interface Draft {
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
  displayName: string;
  status: GameStatus;
  compat: Compat;
  validation: StoredValidation;
}

// ---------------------------------------------------------------- text

const BIDI = /[\u200e\u200f\u202a-\u202e\u2066-\u2069]/g;

// Plain text for names and notes: NFC, no control or bidi-override
// characters, trimmed. Clients still render it as text, never as markup.
function cleanText(s: unknown, multiline: boolean): string {
  if (typeof s !== 'string') return '';
  let t = s.normalize('NFC').replace(BIDI, '');
  if (multiline) {
    t = t.replace(/\r\n?/g, '\n').replace(/[\u0000-\u0009\u000b-\u001f\u007f-\u009f]/g, ' ').replace(/\n{3,}/g, '\n\n');
  } else {
    t = t.replace(/[\u0000-\u001f\u007f-\u009f]/g, ' ').replace(/\s+/g, ' ');
  }
  return t.trim();
}

const codePoints = (s: string) => [...s].length;

function truncate(s: string, max: number): string {
  const cp = [...s];
  return cp.length <= max ? s : cp.slice(0, max - 1).join('').trimEnd() + '\u2026';
}

function cleanFileName(name: string): string {
  const base = cleanText(String(name ?? '').replace(/^.*[\\/]/, ''), false);
  return truncate(base || 'upload', 255);
}

function nameFromFile(fileName: string): string {
  const stem = fileName.replace(/\.[A-Za-z0-9]{1,5}$/, '').replace(/_/g, ' ');
  return truncate(cleanText(stem, false) || 'Untitled', 80);
}

function fmtBytes(n: number): string {
  if (n >= 1024 * 1024 * 1024) return `${(n / (1024 * 1024 * 1024)).toFixed(1)} GiB`;
  if (n >= 1024 * 1024) return `${(n / (1024 * 1024)).toFixed(1)} MiB`;
  if (n >= 1024) return `${(n / 1024).toFixed(1)} KiB`;
  return `${n} bytes`;
}

function parseJson<T>(s: unknown, fallback: T): T {
  if (typeof s !== 'string' || !s) return fallback;
  try {
    return JSON.parse(s) as T;
  } catch {
    return fallback;
  }
}

// The validation child parses hostile files, so its reply is not trusted
// further than it has to be: above all, the file it names for storage must
// be one of the two paths this process gave it (the upload, or the .nes
// extraction target). Anything else would let a compromised validator move
// an arbitrary server file into a group's storage.
function checkInspectReply(r: InspectResult, allowedRomPaths: string[]): InspectResult {
  const bad = (what: string) => new ShelfError('validation_failed', `the file could not be validated (invalid validator reply: ${what})`);
  if (!r || typeof r !== 'object' || typeof r.ok !== 'boolean') throw bad('shape');
  if (!r.ok) {
    if (typeof r.code !== 'string' || typeof r.error !== 'string' || !Array.isArray(r.findings)) throw bad('rejection');
    return r;
  }
  if (!Array.isArray(r.findings)) throw bad('findings');
  if (r.format === 'nes') {
    if (typeof r.romPath !== 'string' || !allowedRomPaths.includes(r.romPath)) throw bad('ROM path');
    if (!r.nes || typeof r.nes !== 'object') throw bad('NES details');
    return r;
  }
  if (r.format === 'zip') {
    if (!r.ident || typeof r.ident !== 'object' || !Array.isArray(r.ident.needs)) throw bad('identification');
    return r;
  }
  throw bad('format');
}

const GAME_SELECT = `
  SELECT g.*, b.size AS blob_size,
         u.id AS u_id, u.first_name AS u_first, u.last_name AS u_last, u.username AS u_username,
         EXISTS (SELECT 1 FROM favorites f WHERE f.group_id = g.group_id AND f.game_id = g.id AND f.user_id = ?) AS fav
    FROM games g
    JOIN blobs b ON b.sha256 = g.blob_sha256
    LEFT JOIN users u ON u.id = g.uploader_id`;

// ---------------------------------------------------------------- service

export class Shelf implements ShelfService {
  readonly blobs: BlobStore;
  private readonly db: Db;
  private readonly cfg: ShelfConfig;
  private readonly groups: Groups | null;
  private readonly validator: ValidatorLike;
  private readonly onChange: ((groupId: number, gameId: number) => void) | null;
  private readonly pins = new Map<number, Set<string>>();
  private readonly inflight = new Map<string, number>();       // blobs being ingested (never cleaned up)
  private readonly revalidating = new Map<number, Promise<void>>();
  private buildInfoText: string | null = null;

  constructor(opts: ShelfOptions) {
    this.db = opts.db;
    this.cfg = opts.cfg;
    this.groups = opts.groups ?? null;
    this.blobs = new BlobStore(opts.cfg.blobDir);
    this.onChange = opts.onChange ?? null;
    this.validator =
      opts.validator ??
      new Validator({
        timeoutMs: opts.cfg.validationTimeoutMs,
        maxConcurrent: opts.cfg.maxConcurrentValidations,
        heapMb: Math.max(256, opts.cfg.workerHeapMb),
      });
  }

  // ============================================================== ingest

  async ingest(req: IngestRequest): Promise<IngestResult> {
    const group = this.db.get('SELECT id FROM groups WHERE id = ?', req.groupId);
    if (!group) {
      // A caller bug, but the contract still holds: the temp file is consumed.
      await unlink(req.tempPath).catch(() => {});
      throw new Error(`ingest for unknown group ${req.groupId}`);
    }
    const fileName = cleanFileName(req.fileName);
    const now = Date.now();
    const uploadId = this.db.run(
      `INSERT INTO uploads (group_id, user_id, source, file_name, size, tg_file_id, tg_file_unique_id, tg_message_id, status, created_at, updated_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'validating', ?, ?)`,
      req.groupId, req.userId, req.source, fileName, req.size,
      req.tg?.fileId ?? null, req.tg?.fileUniqueId ?? null, req.tg?.messageId ?? null, now, now,
    ).lastInsertRowid;

    const temps = new Set<string>([req.tempPath]);
    let held: string | null = null;
    try {
      let size: number;
      try {
        size = statSync(req.tempPath).size;
      } catch {
        throw new ShelfError('upload_missing', 'the uploaded file could not be read');
      }
      if (size > this.cfg.maxUploadBytes) {
        throw new ShelfError('too_large', `the file is ${fmtBytes(size)}; the upload limit is ${fmtBytes(this.cfg.maxUploadBytes)}`);
      }
      if (size === 0) throw new ShelfError('empty_file', 'the file is empty');

      // 1. Inspect in the validation process.
      const nesOutPath = this.blobs.tempPath('.nes');
      temps.add(nesOutPath);
      let inspected: InspectResult;
      try {
        inspected = await this.validator.inspect({
          path: req.tempPath,
          fileName,
          maxBytes: this.cfg.maxUploadBytes,
          limits: {
            maxEntries: this.cfg.maxArchiveEntries,
            maxExtractedBytes: this.cfg.maxExtractedBytes,
            maxCompressionRatio: this.cfg.maxCompressionRatio,
          },
          coreDir: this.cfg.coreDir,
          nesOutPath,
        });
      } catch (e) {
        // The detail (it may name server paths) is for the operator's log only.
        log.warn('shelf', 'validation process failed', { groupId: req.groupId, uploadId, error: e instanceof Error ? e.message : String(e) });
        const why = e instanceof ValidationTimeout ? 'it took too long' : 'internal error';
        throw new ShelfError('validation_failed', `the file could not be validated (${why}); try again later`);
      }
      inspected = checkInspectReply(inspected, [req.tempPath, nesOutPath]);
      if (!inspected.ok) throw new ShelfError(inspected.code, inspected.error, inspected.findings);

      // 2. What gets stored: the zip as uploaded (arcade archive structure is
      //    preserved), or the bare .nes (also when it came inside a zip).
      const contentPath = inspected.format === 'nes' ? inspected.romPath : req.tempPath;
      const { sha256, size: contentSize } = await sha256File(contentPath);

      // 3. Same content already on THIS group's shelf: report the existing entry.
      const existing = this.db.get('SELECT id, status FROM games WHERE group_id = ? AND blob_sha256 = ?', req.groupId, sha256);
      if (existing && existing.status !== 'removed') {
        // The file is stored anyway: this restores a blob that went missing
        // or was damaged (e.g. after a partial restore); an intact one is
        // only verified. The entry references it, so cleanup keeps it.
        await this.blobs.putFile(contentPath, { move: true });
        temps.delete(contentPath);
        this.finishUpload(uploadId, 'done', existing.id, null);
        log.info('shelf', 'duplicate upload', { groupId: req.groupId, uploadId, gameId: existing.id });
        return { uploadId, status: 'done', duplicate: true, game: this.get(req.groupId, existing.id, req.userId) ?? undefined };
      }

      // 4. Quotas, judged as if the content were new to the server (so the
      //    outcome never reveals whether another group has it).
      this.checkQuota(req.groupId, contentSize, true);

      // 5. Store (deduplicated across groups by content) and evaluate.
      held = sha256;
      this.inflight.set(sha256, (this.inflight.get(sha256) ?? 0) + 1);
      const stored = await this.blobs.putFile(contentPath, { move: true });
      temps.delete(contentPath);
      this.db.run('INSERT OR IGNORE INTO blobs (sha256, size, created_at) VALUES (?, ?, ?)', sha256, contentSize, Date.now());

      let draft: Draft;
      let gameId: number;
      let duplicate = false;
      try {
        draft = await this.draft(req.groupId, sha256, inspected, fileName);
        ({ gameId, duplicate } = this.db.tx(() => this.insert(req.groupId, req.userId, sha256, contentSize, fileName, draft)));
      } catch (e) {
        // Nothing references a blob this upload just created: drop it now.
        if (stored.created) this.dropIfUnreferenced(sha256);
        throw e;
      }
      this.finishUpload(uploadId, 'done', gameId, null);
      log.info('shelf', 'upload added', { groupId: req.groupId, uploadId, gameId, kind: draft.kind, system: draft.system, status: draft.status, duplicate });
      if (!duplicate) {
        this.changed(req.groupId, gameId);
        // A new parent/BIOS set may unblock entries that waited for it.
        if (draft.system && draft.setName) await this.reevaluateDependents(req.groupId, draft.system, draft.setName);
      }
      return { uploadId, status: 'done', game: this.get(req.groupId, gameId, req.userId) ?? undefined, ...(duplicate ? { duplicate } : {}) };
    } catch (e) {
      const se = e instanceof ShelfError ? e : null;
      const error = se ? se.message : 'the upload could not be processed';
      if (!se) log.error('shelf', 'ingest failed', { groupId: req.groupId, uploadId, error: e instanceof Error ? e.message : String(e) });
      else log.info('shelf', 'upload rejected', { groupId: req.groupId, uploadId, code: se.code });
      this.finishUpload(uploadId, 'failed', null, error);
      return { uploadId, status: 'failed', error, findings: se?.findings.length ? se.findings : undefined };
    } finally {
      if (held) {
        const n = (this.inflight.get(held) ?? 1) - 1;
        if (n > 0) this.inflight.set(held, n);
        else this.inflight.delete(held);
      }
      for (const p of temps) await unlink(p).catch(() => {});
    }
  }

  private finishUpload(uploadId: number, status: 'done' | 'failed', gameId: number | null, error: string | null): void {
    this.db.run('UPDATE uploads SET status = ?, game_id = ?, error = ?, updated_at = ? WHERE id = ?', status, gameId, error, Date.now(), uploadId);
  }

  // Inserts the entry (or revives this group's removed entry for the same
  // content). Runs inside a transaction; re-checks duplicates and the quota.
  private insert(groupId: number, userId: number, sha256: string, size: number, fileName: string, d: Draft): { gameId: number; duplicate: boolean } {
    const ex = this.db.get('SELECT id, status FROM games WHERE group_id = ? AND blob_sha256 = ?', groupId, sha256);
    if (ex && ex.status !== 'removed') return { gameId: ex.id, duplicate: true };
    this.checkQuota(groupId, size, false);
    const now = Date.now();
    const cols = [
      d.kind, d.system, d.setName, d.parentSet, d.biosSet, d.players, d.mode, d.genre, d.year, d.manufacturer,
      d.status, d.compat, JSON.stringify(d.validation), fileName, d.displayName,
    ];
    if (ex) {
      // Re-uploaded after removal: same id (records keep pointing at it),
      // fresh metadata and validation.
      this.db.run(
        `UPDATE games SET kind = ?, system = ?, set_name = ?, parent_set = ?, bios_set = ?, players = ?, mode = ?, genre = ?, year = ?,
                manufacturer = ?, status = ?, compat = ?, validation = ?, file_name = ?, display_name = ?, metadata = '{}',
                uploader_id = ?, uploaded_at = ?, removed_at = NULL, removed_by = NULL
          WHERE id = ? AND group_id = ?`,
        ...cols, userId, now, ex.id, groupId,
      );
      return { gameId: ex.id, duplicate: false };
    }
    const r = this.db.run(
      `INSERT INTO games (kind, system, set_name, parent_set, bios_set, players, mode, genre, year, manufacturer,
                          status, compat, validation, file_name, display_name, group_id, blob_sha256, uploader_id, uploaded_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
      ...cols, groupId, sha256, userId, now,
    );
    return { gameId: r.lastInsertRowid, duplicate: false };
  }

  // Builds the shelf entry for inspected content, boot-testing it when every
  // dependency is available on this group's shelf.
  private async draft(groupId: number, sha256: string, ins: Extract<InspectResult, { ok: true }>, fileName: string): Promise<Draft> {
    if (ins.format === 'nes') {
      const adapter = findAdapter('fceumm', sha256, SYSTEMS.fceumm.options);
      const base = [...ins.findings];
      const players = adapter ? adapter.meta.players : ins.nes.fourScore ? 4 : 2;
      const mode: RoomMode = adapter ? adapter.meta.mode : 'coop';
      if (adapter) {
        base.push({ level: 'info', code: 'adapter', message: `verified game adapter ${adapter.meta.id} v${adapter.meta.version}: ${adapter.meta.mode}, ${adapter.meta.players} players` });
      } else {
        base.push({ level: 'info', code: 'defaults', message: `no verified adapter for this ROM: set to ${players} players, co-op; edit the mode and player count if needed` });
      }
      const x: Internal = { identified: true, complete: true, base, needs: [], fourScore: ins.nes.fourScore || (adapter?.meta.players ?? 0) > 2 };
      const boot = await this.bootTest('fceumm', null, sha256, []);
      const c = this.compose('game', x, ins.description, [], boot, undefined, null);
      return {
        kind: 'game', system: 'fceumm', setName: null, parentSet: null, biosSet: null, players, mode,
        genre: null, year: null, manufacturer: null,
        displayName: adapter ? truncate(adapter.meta.title, 80) : nameFromFile(fileName),
        ...c,
      };
    }

    const id = ins.ident;
    const base = [...ins.findings];
    const set = id.set;
    if (id.result !== 'identified' || !set || !id.system) {
      const x: Internal = { identified: false, complete: false, base, needs: [] };
      const probable = id.result === 'incomplete' && set ? `probably ${describeSet(set)} (incomplete)` : null;
      const c = this.compose('game', x, probable, [], null, undefined, null);
      return {
        kind: 'game', system: id.result === 'incomplete' ? id.system : null, setName: null, parentSet: null, biosSet: null,
        players: null, mode: null, genre: null, year: null, manufacturer: null,
        displayName: nameFromFile(fileName), ...c,
      };
    }

    const system = id.system;
    const kind = id.kind;
    const sameSet = this.db.get(
      "SELECT id FROM games WHERE group_id = ? AND system = ? AND set_name = ? AND status != 'removed' LIMIT 1",
      groupId, system, set.name,
    );
    if (sameSet) base.push({ level: 'info', code: 'same_set', message: `this group's shelf already has another copy of ${set.name} (entry #${sameSet.id})` });
    const x: Internal = {
      identified: true,
      complete: id.complete,
      base,
      needs: id.needs,
      catalogPlayers: set.players,
      notWorking: kind === 'game' && !(set.flags & BDF.WORKING),
    };
    const { missing, boot } = kind === 'game'
      ? await this.bootWithDeps(groupId, system, set.name, sha256, id.needs)
      : { missing: this.resolveNeeds(groupId, system, id.needs).missing, boot: null };
    const identifiedAs = describeSet(set) + (kind === 'bios' ? ', BIOS/board set' : kind === 'parent' ? ', parent set (dependency only)' : '');
    const c = this.compose(kind, x, identifiedAs, missing, boot, undefined, { matched: id.matchedRoms, total: id.totalRoms });
    const def = kind === 'game' ? defaultModeFor({ system, players: set.players, genre: set.genre }) : null;
    const boardNeed = id.needs.find((n) => n.set === set.board);
    const parentNeed = id.needs.find((n) => n.set === set.parent);
    return {
      kind,
      system,
      setName: set.name,
      parentSet: parentNeed ? set.parent : null,
      biosSet: boardNeed ? set.board : null,
      players: def?.players ?? null,
      mode: def?.mode ?? null,
      genre: genreLabel(set.genre),
      year: set.year || null,
      manufacturer: set.manufacturer || null,
      displayName: truncate(cleanText(set.fullname, false) || set.name, 80),
      ...c,
    };
  }

  // Status, compatibility and the stored validation from the parts.
  private compose(
    kind: Draft['kind'],
    x: Internal,
    identifiedAs: string | null,
    missing: Validation['missing'],
    boot: BootResult | null,
    prevCompat: Compat | undefined,
    roms: { matched: number; total: number } | null,
  ): { status: GameStatus; compat: Compat; validation: StoredValidation } {
    const findings = [...x.base];
    for (const m of missing) {
      const what = m.kind === 'bios' ? 'BIOS/board set' : 'parent romset';
      const files = m.files.slice(0, 6).join(', ') + (m.files.length > 6 ? `, and ${m.files.length - 6} more` : '');
      findings.push({
        level: 'warn',
        code: m.kind === 'bios' ? 'missing_bios' : 'missing_parent',
        message: `needs the ${what} "${m.set}" (${files}); upload ${m.set}.zip to this group's shelf`,
      });
    }
    if (boot && !boot.ok) findings.push({ level: 'error', code: 'boot_failed', message: `boot test failed: ${boot.detail}` });

    let status: GameStatus;
    if (!x.identified || !x.complete) status = 'needs_attention';
    else if (kind !== 'game') status = 'ready';
    else if (missing.length) status = 'needs_dependency';
    else status = boot?.ok ? 'ready' : 'needs_attention';

    let compat: Compat = 'untested';
    if (status === 'needs_attention') compat = 'needs_attention';
    else if (status === 'ready' && kind === 'game') compat = x.notWorking ? 'needs_attention' : prevCompat === 'working' ? 'working' : 'untested';

    const validation: StoredValidation = {
      checkedAt: Date.now(),
      identifiedAs,
      findings,
      missing,
      boot,
      ...(roms ? { matchedRoms: roms.matched, totalRoms: roms.total } : {}),
      _x: x,
    };
    return { status, compat, validation };
  }

  // ============================================================== dependencies

  // The newest complete entry of `set` on this group's shelf, if any.
  private provider(groupId: number, system: SystemId, set: string): { gameId: number; sha256: string } | null {
    const rows = this.db.all(
      "SELECT id, blob_sha256, validation FROM games WHERE group_id = ? AND system = ? AND set_name = ? AND status != 'removed' ORDER BY uploaded_at DESC, id DESC",
      groupId, system, set,
    );
    for (const r of rows) {
      const x = parseJson<StoredValidation>(r.validation, {} as StoredValidation)._x;
      if (x?.complete) return { gameId: r.id, sha256: r.blob_sha256 };
    }
    return null;
  }

  private resolveNeeds(groupId: number, system: SystemId, needs: DependencyNeed[]): { deps: { set: string; sha256: string }[]; missing: Validation['missing'] } {
    const deps: { set: string; sha256: string }[] = [];
    const missing: Validation['missing'] = [];
    for (const n of needs) {
      const p = this.provider(groupId, system, n.set);
      if (p) deps.push({ set: n.set, sha256: p.sha256 });
      else missing.push({ kind: n.kind, set: n.set, files: n.files });
    }
    return { deps, missing };
  }

  private romPath(system: SystemId, set: string | null): string {
    if (system === 'fceumm') return NES_PATH;
    if (!set || !SET_NAME.test(set)) throw new ShelfError('bad_set', 'invalid romset name');
    return `/roms/${set}.zip`;
  }

  // Resolves an FBNeo game's dependencies on this group's shelf and, when all
  // are present, boot-tests with them. A dependency can be removed while the
  // (slow) boot test runs; the result is then "missing" again rather than a
  // stale "ready" that no dependencyGone() pass would correct.
  private async bootWithDeps(
    groupId: number,
    system: SystemId,
    setName: string | null,
    sha256: string,
    needs: DependencyNeed[],
  ): Promise<{ missing: Validation['missing']; boot: BootResult | null }> {
    const { deps, missing } = this.resolveNeeds(groupId, system, needs);
    if (missing.length) return { missing, boot: null };
    const boot = await this.bootTest(system, setName, sha256, deps);
    const now = this.resolveNeeds(groupId, system, needs);
    return now.missing.length ? { missing: now.missing, boot: null } : { missing: [], boot };
  }

  private bootTest(system: SystemId, setName: string | null, sha256: string, deps: { set: string; sha256: string }[]): Promise<BootResult> {
    const files = [
      { path: this.romPath(system, setName), source: this.blobs.pathOf(sha256) },
      ...deps.map((d) => ({ path: this.romPath(system, d.set), source: this.blobs.pathOf(d.sha256) })),
    ];
    return this.validator.boot({
      system,
      coreDir: this.cfg.coreDir,
      files,
      gamePath: files[0].path,
      minFrames: BOOT_MIN_FRAMES,
      maxFrames: BOOT_MAX_FRAMES,
    });
  }

  // Boot-tests entries that wait for (or failed with an older copy of) `set`.
  private async reevaluateDependents(groupId: number, system: SystemId, set: string): Promise<void> {
    const rows = this.db.all(
      `SELECT id FROM games WHERE group_id = ? AND system = ? AND kind = 'game' AND status IN ('needs_dependency', 'needs_attention')
          AND (parent_set = ? OR bios_set = ?) ORDER BY id`,
      groupId, system, set, set,
    );
    for (const r of rows) await this.revalidate(groupId, r.id);
  }

  // Re-checks an entry's dependencies and boot test (e.g. after a dependency
  // arrived, or on an operator's request after a core update). Calls for the
  // same entry are serialized.
  revalidate(groupId: number, gameId: number): Promise<void> {
    const prev = this.revalidating.get(gameId) ?? Promise.resolve();
    const next = prev.then(() => this.doRevalidate(groupId, gameId)).catch((e) => {
      log.error('shelf', 'revalidation failed', { groupId, gameId, error: e instanceof Error ? e.message : String(e) });
    });
    this.revalidating.set(gameId, next);
    return next.finally(() => {
      if (this.revalidating.get(gameId) === next) this.revalidating.delete(gameId);
    });
  }

  private async doRevalidate(groupId: number, gameId: number): Promise<void> {
    const r = this.db.get('SELECT * FROM games WHERE id = ? AND group_id = ?', gameId, groupId);
    if (!r || r.status === 'removed' || !r.system) return;
    const v = parseJson<StoredValidation>(r.validation, {} as StoredValidation);
    const x = v._x;
    if (!x || !x.identified || !x.complete) return;
    const system = r.system as SystemId;
    const { missing, boot } = r.kind === 'game'
      ? await this.bootWithDeps(groupId, system, r.set_name, r.blob_sha256, x.needs)
      : { missing: this.resolveNeeds(groupId, system, x.needs).missing, boot: null };
    // The boot test took a while: start from the row as it is now, so edits
    // made meanwhile (update(): NES Four Score; markPlayed(): compat) survive.
    const now = this.db.get('SELECT status, compat, validation FROM games WHERE id = ? AND group_id = ?', gameId, groupId);
    if (!now || now.status === 'removed') return;
    const v2 = parseJson<StoredValidation>(now.validation, {} as StoredValidation);
    const x2 = v2._x ?? x;
    const roms = v2.totalRoms !== undefined ? { matched: v2.matchedRoms ?? 0, total: v2.totalRoms } : null;
    const c = this.compose(r.kind, x2, v2.identifiedAs ?? null, missing, boot, now.compat, roms);
    const res = this.db.run(
      "UPDATE games SET status = ?, compat = ?, validation = ? WHERE id = ? AND group_id = ? AND status != 'removed'",
      c.status, c.compat, JSON.stringify(c.validation), gameId, groupId,
    );
    if (res.changes) {
      log.info('shelf', 'entry revalidated', { groupId, gameId, status: c.status });
      this.changed(groupId, gameId);
    }
  }

  // After a removal: entries that relied on the removed set and have no
  // other provider go back to needs_dependency (no boot test needed).
  private dependencyGone(groupId: number, system: SystemId, set: string): void {
    const rows = this.db.all(
      `SELECT * FROM games WHERE group_id = ? AND system = ? AND kind = 'game' AND status != 'removed'
          AND (parent_set = ? OR bios_set = ?)`,
      groupId, system, set, set,
    );
    for (const r of rows) {
      const v = parseJson<StoredValidation>(r.validation, {} as StoredValidation);
      const x = v._x;
      if (!x || !x.identified || !x.complete) continue;
      const { missing } = this.resolveNeeds(groupId, system, x.needs);
      if (missing.length === 0 || r.status === 'needs_dependency') continue;
      const roms = v.totalRoms !== undefined ? { matched: v.matchedRoms ?? 0, total: v.totalRoms } : null;
      const c = this.compose(r.kind, x, v.identifiedAs ?? null, missing, null, r.compat, roms);
      this.db.run('UPDATE games SET status = ?, compat = ?, validation = ? WHERE id = ? AND group_id = ?', c.status, c.compat, JSON.stringify(c.validation), r.id, groupId);
      this.changed(groupId, r.id);
    }
  }

  // ============================================================== quotas

  private groupUsage(groupId: number): number {
    const r = this.db.get(
      "SELECT COALESCE(SUM(size), 0) AS n FROM blobs WHERE sha256 IN (SELECT blob_sha256 FROM games WHERE group_id = ? AND status != 'removed')",
      groupId,
    );
    return Number(r?.n ?? 0);
  }

  // Bytes actually on disk (rows of deleted blobs are kept for removed entries).
  private storedBytes(): number {
    let n = 0;
    for (const r of this.db.all<{ sha256: string; size: number }>('SELECT sha256, size FROM blobs')) {
      if (this.blobs.has(r.sha256)) n += r.size;
    }
    return n;
  }

  private quotaOf(groupId: number): number {
    if (this.groups) return this.groups.byId(groupId)?.quotaBytes ?? 0;
    return Number(this.db.get('SELECT quota_bytes FROM groups WHERE id = ?', groupId)?.quota_bytes ?? 0);
  }

  private checkQuota(groupId: number, size: number, includeGlobal: boolean): void {
    const quota = this.quotaOf(groupId);
    const used = this.groupUsage(groupId);
    if (used + size > quota) {
      throw new ShelfError(
        'quota_exceeded',
        `this group's shelf is full: ${fmtBytes(used)} of ${fmtBytes(quota)} used, and this file needs ${fmtBytes(size)}; remove games you no longer play or ask the operator for more space`,
      );
    }
    if (includeGlobal && this.storedBytes() + size > this.cfg.globalStorageLimitBytes) {
      log.warn('shelf', 'global storage limit reached', { limit: this.cfg.globalStorageLimitBytes });
      throw new ShelfError('storage_full', 'the arcade server is out of storage space; try again later or ask the operator');
    }
  }

  usage(groupId: number): { bytes: number; quota: number; games: number } {
    const n = this.db.get("SELECT COUNT(*) AS n FROM games WHERE group_id = ? AND status != 'removed' AND kind = 'game'", groupId);
    return { bytes: this.groupUsage(groupId), quota: this.quotaOf(groupId), games: Number(n?.n ?? 0) };
  }

  // ============================================================== reading

  private toGame(r: Row): ShelfGame {
    const v = parseJson<StoredValidation>(r.validation, {} as StoredValidation);
    const meta = parseJson<Record<string, unknown>>(r.metadata, {});
    const system = (r.system ?? null) as SystemId | null;
    const adapter = system && SYSTEMS[system] ? findAdapter(system, r.blob_sha256, SYSTEMS[system].options) : null;
    const metadata: ShelfGame['metadata'] = { artwork: typeof meta.artwork === 'string' ? meta.artwork : null };
    if (typeof meta.notes === 'string') metadata.notes = meta.notes;
    if (Array.isArray(meta.tags)) metadata.tags = meta.tags.filter((t): t is string => typeof t === 'string');
    if (typeof meta.handoffRule === 'string') metadata.handoffRule = meta.handoffRule;
    const validation: Validation = {
      checkedAt: v.checkedAt ?? 0,
      identifiedAs: v.identifiedAs ?? null,
      findings: v.findings ?? [],
      missing: v.missing ?? [],
      boot: v.boot ?? null,
    };
    if (v.matchedRoms !== undefined) validation.matchedRoms = v.matchedRoms;
    if (v.totalRoms !== undefined) validation.totalRoms = v.totalRoms;
    return {
      id: r.id,
      groupId: r.group_id,
      displayName: r.display_name,
      fileName: r.file_name,
      kind: r.kind,
      system,
      setName: r.set_name ?? null,
      parentSet: r.parent_set ?? null,
      biosSet: r.bios_set ?? null,
      players: r.players ?? null,
      mode: r.mode ?? null,
      genre: r.genre ?? null,
      year: r.year ?? null,
      manufacturer: r.manufacturer ?? null,
      status: r.status,
      compat: r.compat,
      validation,
      metadata,
      uploaderId: r.uploader_id === null || r.uploader_id === undefined ? null : Number(r.uploader_id),
      uploaderName: r.u_id ? userDisplayName({ first_name: r.u_first, last_name: r.u_last, username: r.u_username ?? undefined }) : null,
      uploadedAt: r.uploaded_at,
      lastPlayedAt: r.last_played_at ?? null,
      playCount: r.play_count,
      favorite: !!r.fav,
      sizeBytes: Number(r.blob_size),
      sha256: r.blob_sha256,
      adapter: adapter ? { id: adapter.meta.id, version: adapter.meta.version, capabilities: { ...adapter.meta.capabilities } } : null,
    };
  }

  // Live entries of one group (removed entries are not listed).
  list(groupId: number, viewerId: number): ShelfGame[] {
    return this.db
      .all(
        `${GAME_SELECT} WHERE g.group_id = ? AND g.status != 'removed'
          ORDER BY CASE g.kind WHEN 'game' THEN 0 ELSE 1 END, g.display_name COLLATE NOCASE, g.id`,
        viewerId, groupId,
      )
      .map((r) => this.toGame(r));
  }

  // One entry of one group, including removed ones (status 'removed'), so
  // records and history can still show what was played.
  get(groupId: number, gameId: number, viewerId?: number): ShelfGame | null {
    const r = this.db.get(`${GAME_SELECT} WHERE g.group_id = ? AND g.id = ?`, viewerId ?? 0, groupId, gameId);
    return r ? this.toGame(r) : null;
  }

  // ============================================================== editing

  private liveRow(groupId: number, gameId: number): Row {
    const r = this.db.get("SELECT * FROM games WHERE id = ? AND group_id = ? AND status != 'removed'", gameId, groupId);
    if (!r) throw new ShelfError('not_found', 'this game is not on the shelf');
    return r;
  }

  private maxPlayers(r: Row, x: Internal | undefined): number {
    const system = r.system as SystemId;
    const sys = SYSTEMS[system];
    if (system === 'fceumm') {
      const adapter = findAdapter(system, r.blob_sha256, sys.options);
      return adapter ? adapter.meta.players : 4;
    }
    return Math.max(1, Math.min(sys.maxPorts, x?.catalogPlayers || sys.maxPorts));
  }

  // Metadata edits. The caller has already checked that `actor` may edit.
  update(
    groupId: number,
    gameId: number,
    actor: number,
    patch: { displayName?: string; mode?: RoomMode; players?: number; notes?: string; handoffRule?: string },
  ): ShelfGame {
    // `patch` is often a request body as parsed: check its types, so a
    // non-string never turns into an empty value (or a crash) silently.
    if (!patch || typeof patch !== 'object' || Array.isArray(patch)) throw new ShelfError('invalid', 'expected an object with the fields to change');
    for (const k of ['displayName', 'notes', 'handoffRule', 'mode'] as const) {
      if (patch[k] !== undefined && typeof patch[k] !== 'string') throw new ShelfError('invalid', `${k} must be text`);
    }
    if (patch.players !== undefined && typeof patch.players !== 'number') throw new ShelfError('invalid', 'players must be a number');
    const r = this.liveRow(groupId, gameId);
    const sets: string[] = [];
    const vals: unknown[] = [];
    const changed: string[] = [];

    if (patch.displayName !== undefined) {
      const name = cleanText(patch.displayName, false);
      if (codePoints(name) < 1 || codePoints(name) > 80) throw new ShelfError('invalid', 'the display name must be 1 to 80 characters of plain text');
      sets.push('display_name = ?');
      vals.push(name);
      changed.push('displayName');
    }

    const v = parseJson<StoredValidation>(r.validation, {} as StoredValidation);
    if (patch.mode !== undefined || patch.players !== undefined) {
      if (r.kind !== 'game' || !r.system) throw new ShelfError('invalid', 'mode and players can only be set for playable games');
      const mode = patch.mode ?? (r.mode as RoomMode | null) ?? 'coop';
      const players = patch.players ?? r.players ?? 2;
      if (!MODES.includes(mode)) throw new ShelfError('invalid', `unknown mode; use one of ${MODES.join(', ')}`);
      const max = this.maxPlayers(r, v._x);
      if (!Number.isInteger(players) || players < 1 || players > max) throw new ShelfError('invalid', `players must be a whole number from 1 to ${max} for this game`);
      if (MULTI_SEAT_MODES.includes(mode) && players < 2) throw new ShelfError('invalid', `${mode} needs at least 2 players`);
      sets.push('mode = ?', 'players = ?');
      vals.push(mode, players);
      if (patch.mode !== undefined) changed.push('mode');
      if (patch.players !== undefined) changed.push('players');
      // On the NES, more than two players means the Four Score; raising the
      // count is the editor's statement that this game supports it.
      if (r.system === 'fceumm' && players > 2 && v._x && !v._x.fourScore) {
        v._x.fourScore = true;
        sets.push('validation = ?');
        vals.push(JSON.stringify(v));
      }
    }

    if (patch.notes !== undefined || patch.handoffRule !== undefined) {
      const meta = parseJson<Record<string, unknown>>(r.metadata, {});
      if (patch.notes !== undefined) {
        const notes = cleanText(patch.notes, true);
        if (codePoints(notes) > 500) throw new ShelfError('invalid', 'notes can be at most 500 characters');
        if (notes) meta.notes = notes;
        else delete meta.notes;
        changed.push('notes');
      }
      if (patch.handoffRule !== undefined) {
        const rule = cleanText(patch.handoffRule, true);
        if (codePoints(rule) > 200) throw new ShelfError('invalid', 'the handoff rule can be at most 200 characters');
        if (rule) meta.handoffRule = rule;
        else delete meta.handoffRule;
        changed.push('handoffRule');
      }
      sets.push('metadata = ?');
      vals.push(JSON.stringify(meta));
    }

    if (sets.length) {
      this.db.tx(() => {
        this.db.run(`UPDATE games SET ${sets.join(', ')} WHERE id = ? AND group_id = ?`, ...vals, gameId, groupId);
        this.audit(groupId, actor, 'shelf_update', gameId, { fields: changed });
      });
      this.changed(groupId, gameId);
    }
    return this.get(groupId, gameId, actor)!;
  }

  setFavorite(groupId: number, userId: number, gameId: number, favorite: boolean): void {
    if (favorite) {
      this.liveRow(groupId, gameId);
      this.db.run('INSERT OR IGNORE INTO favorites (group_id, user_id, game_id, created_at) VALUES (?, ?, ?, ?)', groupId, userId, gameId, Date.now());
    } else {
      this.db.run('DELETE FROM favorites WHERE group_id = ? AND user_id = ? AND game_id = ?', groupId, userId, gameId);
    }
  }

  // Soft removal. The caller has checked that `actor` is the host or a
  // deputy. The blob is not touched: running sessions keep their pinned
  // files, and cleanup() decides later.
  remove(groupId: number, gameId: number, actor: number): void {
    const r = this.db.get('SELECT id, status, system, set_name FROM games WHERE id = ? AND group_id = ?', gameId, groupId);
    if (!r) throw new ShelfError('not_found', 'this game is not on the shelf');
    if (r.status === 'removed') return;
    const now = Date.now();
    this.db.tx(() => {
      this.db.run("UPDATE games SET status = 'removed', removed_at = ?, removed_by = ? WHERE id = ? AND group_id = ?", now, actor, gameId, groupId);
      this.audit(groupId, actor, 'shelf_remove', gameId, { previousStatus: r.status });
    });
    log.info('shelf', 'entry removed', { groupId, gameId });
    this.changed(groupId, gameId);
    if (r.system && r.set_name) this.dependencyGone(groupId, r.system as SystemId, r.set_name);
  }

  markPlayed(groupId: number, gameId: number, compat?: 'working' | 'needs_attention'): void {
    const c = compat === 'working' || compat === 'needs_attention' ? compat : null;
    this.db.run(
      'UPDATE games SET last_played_at = ?, play_count = play_count + 1, compat = COALESCE(?, compat) WHERE id = ? AND group_id = ?',
      Date.now(), c, gameId, groupId,
    );
  }

  private audit(groupId: number, actor: number | null, action: string, target: number | null, data: Record<string, unknown>): void {
    this.db.run(
      'INSERT INTO audit_log (group_id, actor_id, action, target_id, reason, data, created_at) VALUES (?, ?, ?, ?, NULL, ?, ?)',
      groupId, actor, action, target, JSON.stringify(data), Date.now(),
    );
  }

  private changed(groupId: number, gameId: number): void {
    if (!this.onChange) return;
    try {
      this.onChange(groupId, gameId);
    } catch (e) {
      log.warn('shelf', 'change listener failed', { error: e instanceof Error ? e.message : String(e) });
    }
  }

  // ============================================================== sessions

  private buildInfo(): string {
    if (this.buildInfoText === null) {
      try {
        this.buildInfoText = readFileSync(join(this.cfg.coreDir, 'build-info.json'), 'utf8');
      } catch {
        this.buildInfoText = '';
      }
    }
    return this.buildInfoText;
  }

  // Identity of the emulated machine a checkpoint or record belongs to: ROM
  // contents, core build, core options, the controller hardware plugged into
  // each port (the NES Four Score is switched on per game by the session's
  // port count, see portDevicesFor) and the adapter version.
  compatKey(system: SystemId, romSha256s: string[], options: Record<string, string>, adapter: Adapter | null, ports: number): string {
    const sys = SYSTEMS[system];
    const opts = Object.entries(options).sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0));
    const devices = Object.entries(portDevicesFor(sys, ports)).sort(([a], [b]) => Number(a) - Number(b));
    return createHash('sha256')
      .update(
        JSON.stringify({
          v: 2,
          core: sys.core,
          build: this.buildInfo(),
          roms: [...romSha256s].sort(),
          options: opts,
          devices,
          adapter: adapter ? `${adapter.meta.id}@${adapter.meta.version}` : null,
        }),
      )
      .digest('hex');
  }

  // Everything a session needs to run an entry: file bytes for the
  // authoritative worker, hashes for browsers (fetched via blobForGroup).
  async sessionSpec(groupId: number, gameId: number): Promise<SessionGameSpec> {
    const r = this.db.get('SELECT * FROM games WHERE id = ? AND group_id = ?', gameId, groupId);
    if (!r) throw new ShelfError('not_found', 'this game is not on the shelf');
    const pinned = this.pins.get(groupId);
    // A removed entry can only be restarted by the session still running it.
    if (r.status === 'removed' && !pinned?.has(r.blob_sha256)) throw new ShelfError('removed', 'this game was removed from the shelf');
    if (r.kind !== 'game') throw new ShelfError('not_playable', 'BIOS and parent sets cannot be played by themselves');
    if (r.status !== 'ready' && r.status !== 'removed') {
      const why = r.status === 'needs_dependency' ? 'it needs a BIOS or parent set first' : r.status === 'rejected' ? 'it was rejected' : 'it failed validation';
      throw new ShelfError('not_ready', `this game cannot be started: ${why}`);
    }
    const system = r.system as SystemId;
    const sys = SYSTEMS[system];
    if (!sys) throw new ShelfError('not_ready', 'unsupported system');
    const v = parseJson<StoredValidation>(r.validation, {} as StoredValidation);
    const x = v._x;

    const wanted: { path: string; sha256: string }[] = [{ path: this.romPath(system, r.set_name), sha256: r.blob_sha256 }];
    if (system !== 'fceumm') {
      for (const dep of [r.parent_set, r.bios_set]) {
        if (!dep) continue;
        const p = this.provider(groupId, system, dep);
        if (!p) throw new ShelfError('missing_dependency', `this game needs "${dep}" on the shelf first`);
        wanted.push({ path: this.romPath(system, dep), sha256: p.sha256 });
      }
    }

    const files: WorkerFile[] = [];
    const clientFiles: ClientFile[] = [];
    for (const w of wanted) {
      let data: Buffer;
      try {
        data = await this.blobs.read(w.sha256);
      } catch {
        throw new ShelfError('blob_missing', 'a file of this game is missing from storage');
      }
      if (sha256Bytes(data) !== w.sha256) {
        log.error('shelf', 'blob content does not match its hash', { groupId, gameId });
        throw new ShelfError('blob_corrupt', 'a file of this game is damaged in storage');
      }
      files.push({ path: w.path, data: new Uint8Array(data.buffer, data.byteOffset, data.byteLength) });
      clientFiles.push({ path: w.path, sha256: w.sha256, size: data.length });
    }

    const options = { ...sys.options };
    const adapter = findAdapter(system, r.blob_sha256, options);
    const players: number = r.players ?? x?.catalogPlayers ?? 2;
    let ports: number;
    if (system === 'fceumm') {
      if (adapter) ports = adapter.meta.players;
      else ports = players > 2 && x?.fourScore ? 4 : 2;
    } else {
      ports = Math.max(1, Math.min(sys.maxPorts, players));
    }
    return {
      gameId,
      title: r.display_name,
      system,
      files,
      clientFiles,
      gamePath: wanted[0].path,
      options,
      ports,
      mode: (r.mode as RoomMode | null) ?? adapter?.meta.mode ?? 'coop',
      adapterId: adapter?.meta.id ?? null,
      compatKey: this.compatKey(system, wanted.map((w) => w.sha256), options, adapter, ports),
    };
  }

  // The ONLY way ROM files are served: a blob is readable by a group if one
  // of its live entries references it, or its running session pinned it.
  blobForGroup(groupId: number, sha256: string): { path: string; size: number } | null {
    if (!isSha256(sha256)) return null;
    const live = this.db.get("SELECT 1 AS ok FROM games WHERE group_id = ? AND blob_sha256 = ? AND status != 'removed' LIMIT 1", groupId, sha256);
    if (!live && !this.pins.get(groupId)?.has(sha256)) return null;
    const size = this.blobs.size(sha256);
    if (size === null) return null;
    return { path: this.blobs.pathOf(sha256), size };
  }

  // Pins add to the group's set (a game switch may pin the next game before
  // the old one is released); unpin() releases everything of the group.
  pin(groupId: number, sha256s: string[]): void {
    let set = this.pins.get(groupId);
    if (!set) {
      set = new Set();
      this.pins.set(groupId, set);
    }
    for (const s of sha256s) if (isSha256(s)) set.add(s);
  }

  unpin(groupId: number): void {
    this.pins.delete(groupId);
  }

  // ============================================================== cleanup

  // Deletes blob files nobody needs any more: not referenced by any live
  // entry of any group, not pinned, not in use by an ingest, not needed by a
  // valid checkpoint, and removed (or orphaned) longer than the retention
  // period. Rows of blobs still referenced by removed entries are kept
  // (records reference those entries); their files are gone.
  cleanup(now = Date.now()): { deletedBlobs: number; freedBytes: number } {
    const keep = new Set<string>();
    for (const r of this.db.all("SELECT DISTINCT blob_sha256 FROM games WHERE status != 'removed'")) keep.add(r.blob_sha256);
    for (const set of this.pins.values()) for (const s of set) keep.add(s);
    for (const s of this.inflight.keys()) keep.add(s);
    // Valid checkpoints need their game's files and its dependencies.
    const cp = this.db.all(
      `SELECT DISTINCT g.group_id, g.blob_sha256, g.system, g.parent_set, g.bios_set
         FROM checkpoints c JOIN games g ON g.id = c.game_id WHERE c.valid = 1`,
    );
    for (const r of cp) {
      keep.add(r.blob_sha256);
      for (const dep of [r.parent_set, r.bios_set]) {
        if (!dep) continue;
        for (const d of this.db.all('SELECT blob_sha256 FROM games WHERE group_id = ? AND system = ? AND set_name = ?', r.group_id, r.system, dep)) {
          keep.add(d.blob_sha256);
        }
      }
    }

    const retentionMs = this.cfg.removedGameRetentionDays * DAY_MS;
    let deletedBlobs = 0;
    let freedBytes = 0;
    const rows = this.db.all(
      `SELECT b.sha256, b.size, b.created_at,
              (SELECT MAX(removed_at) FROM games g WHERE g.blob_sha256 = b.sha256) AS last_removed,
              (SELECT COUNT(*) FROM games g WHERE g.blob_sha256 = b.sha256) AS refs
         FROM blobs b`,
    );
    for (const r of rows) {
      if (keep.has(r.sha256)) continue;
      const since = Math.max(Number(r.created_at), Number(r.last_removed ?? 0));
      if (now - since < retentionMs) continue;
      if (this.blobs.has(r.sha256) && this.blobs.delete(r.sha256)) {
        deletedBlobs++;
        freedBytes += Number(r.size);
      }
      if (Number(r.refs) === 0) this.db.run('DELETE FROM blobs WHERE sha256 = ?', r.sha256);
    }
    this.blobs.sweepTemp(DAY_MS, now);
    if (deletedBlobs) log.info('shelf', 'cleanup deleted blobs', { deletedBlobs, freedBytes });
    return { deletedBlobs, freedBytes };
  }

  // A blob stored by a failed ingest that nothing references.
  private dropIfUnreferenced(sha256: string): void {
    if ((this.inflight.get(sha256) ?? 0) > 1) return;
    for (const set of this.pins.values()) if (set.has(sha256)) return;
    const ref = this.db.get('SELECT 1 AS x FROM games WHERE blob_sha256 = ? LIMIT 1', sha256);
    if (ref) return;
    this.blobs.delete(sha256);
    this.db.run('DELETE FROM blobs WHERE sha256 = ?', sha256);
  }
}

export function createShelf(opts: ShelfOptions): Shelf {
  return new Shelf(opts);
}
