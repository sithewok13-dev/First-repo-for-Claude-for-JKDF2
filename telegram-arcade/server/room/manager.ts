// RoomManager: one Room per Telegram group, the running GameSession behind
// it, client connections, checkpoints, idle release and resource limits.

import { mkdirSync, readFileSync, writeFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
import { decodeInput } from '../../shared/protocol.ts';
import { allowedMasks, portDevicesFor, SYSTEMS } from '../../shared/systems.ts';
import { adapterById } from '../adapters/registry.ts';
import type { Config } from '../config.ts';
import type { Db } from '../db/db.ts';
import type { Groups } from '../groups.ts';
import { log } from '../log.ts';
import type { Records } from '../records.ts';
import { GameSession } from '../session/session.ts';
import type { SessionGameSpec, ShelfService } from '../shelf/types.ts';
import type { BotService } from '../telegram/types.ts';
import { Room, RoomError, REMOVAL_REASONS, type RemovalReason } from './room.ts';
import type { RoomGame, RoomIO, TgRole } from './types.ts';

export interface Conn {
  id: string;
  userId: number;
  groupId: number;
  name: string;
  tgRole: TgRole;
  sendJson(obj: unknown): void;
  sendBin(data: Uint8Array): void;
  close(code: number, reason: string): void;
  bufferedAmount(): number;
}

interface ActiveRoom {
  groupId: number;
  room: Room;
  conns: Map<string, Conn>;
  session: GameSession | null;
  spec: SessionGameSpec | null;
  game: RoomGame | null;
  fresh: boolean;
  starting: boolean;
  stateTimer: NodeJS.Timeout | null;
  persistTimer: NodeJS.Timeout | null;
  idleTimer: NodeJS.Timeout | null;
  checkpointTimer: NodeJS.Timeout | null;
  resumable: { gameId: number; checkpointId: number; frame: number; at: number; title: string } | null;
  lastHealth: string;
}

export interface ManagerDeps {
  cfg: Config;
  db: Db;
  groups: Groups;
  shelf: ShelfService;
  records: Records;
  bot: BotService | null;
}

export class RoomManager {
  private readonly d: ManagerDeps;
  private readonly active = new Map<number, ActiveRoom>();
  private coreBuild = '';
  private closed = false;

  constructor(deps: ManagerDeps) {
    this.d = deps;
    try {
      this.coreBuild = readFileSync(join(deps.cfg.coreDir, 'build-info.json'), 'utf8');
    } catch {
      this.coreBuild = 'unknown';
    }
    mkdirSync(deps.cfg.checkpointDir, { recursive: true, mode: 0o700 });
    // No emulator session can be live when the process starts: anything still
    // open was cut off by a crash or power loss. Close it so it never counts as
    // running (the rooms offer Resume from the last committed checkpoint).
    const now = Date.now();
    const cut = deps.db.run("UPDATE game_sessions SET ended_at = ?, end_reason = 'crash' WHERE ended_at IS NULL", now).changes;
    deps.db.run("UPDATE control_segments SET end_reason = 'crash' WHERE end_frame IS NULL AND end_reason IS NULL");
    if (cut) log.warn('rooms', `${cut} session(s) were interrupted by an unclean stop; their rooms offer Resume`);
  }

  // ================================================================ rooms

  private roomIO(groupId: number): RoomIO {
    const d = this.d;
    return {
      now: () => Date.now(),
      timer: (ms, fn) => {
        const t = setTimeout(() => {
          try {
            fn();
          } catch (e) {
            log.error('room', `timer failed: ${(e as Error).message}`);
          }
        }, ms);
        return () => clearTimeout(t);
      },
      send: (userId, msg) => {
        const a = this.active.get(groupId);
        if (!a) return;
        for (const c of a.conns.values()) if (c.userId === userId) c.sendJson(msg);
      },
      broadcast: (msg) => this.broadcast(groupId, msg),
      stateChanged: () => this.scheduleState(groupId),
      announce: (text, opts) => d.bot?.announce(groupId, text, { ...opts, delayMs: opts?.delayMs ?? 3000 }),
      audit: (actor, action, target, reason, data) => d.records.audit(groupId, actor, action, target, reason, data ?? {}),
      recordMatch: (m) => d.records.match(groupId, m),
      recordScore: (s) => {
        const a = this.active.get(groupId);
        return d.records.score(groupId, s, { fresh: a?.fresh ?? true, continues: s.continues ?? undefined }, s.openSegment);
      },
      recordSegment: (s) => d.records.segment(groupId, s),
      recordEvent: (key, type, frame, source, data) => {
        const sid = this.active.get(groupId)?.session?.id ?? 0;
        return d.records.event(groupId, sid, key, type, frame, source, data);
      },
      switchGame: (gameId, reason, seatOrder) => this.startGame(groupId, gameId, { reason, seatOrder }),
      resetGame: (reason) => this.resetGame(groupId, reason),
      gameTitle: (gameId) => {
        const g = d.shelf.get(groupId, gameId);
        return g && g.status === 'ready' && g.kind === 'game' ? g.displayName : null;
      },
      persistChat: (userId, text, at) => d.db.run('INSERT INTO chat_messages (group_id, user_id, text, created_at) VALUES (?, ?, ?, ?)', groupId, userId, text, at).lastInsertRowid,
      deleteChatRow: (id, by) => { d.db.run('UPDATE chat_messages SET deleted_by = ?, deleted_at = ? WHERE id = ? AND group_id = ?', by, Date.now(), id, groupId); },
      persistRoles: (hostId, deputies) => {
        d.db.tx(() => {
          d.db.run("DELETE FROM roles WHERE group_id = ? AND role IN ('host', 'deputy')", groupId);
          const now = Date.now();
          if (hostId !== null) d.db.run("INSERT INTO roles (group_id, user_id, role, granted_at) VALUES (?, ?, 'host', ?)", groupId, hostId, now);
          for (const dep of deputies) d.db.run("INSERT INTO roles (group_id, user_id, role, succession_order, granted_at) VALUES (?, ?, 'deputy', ?, ?)", groupId, dep.userId, dep.order, now);
        });
      },
    };
  }

  private ensure(groupId: number): ActiveRoom {
    let a = this.active.get(groupId);
    if (a) return a;
    const group = this.d.groups.byId(groupId);
    const settings = { maxViewers: this.d.cfg.maxViewersPerRoom, offerSeconds: this.d.cfg.offerSeconds, graceSeconds: this.d.cfg.disconnectGraceSeconds, hostGraceSeconds: this.d.cfg.hostGraceSeconds, voteSeconds: this.d.cfg.voteSeconds, ...((group?.settings as any)?.room ?? {}) };
    const room = new Room(groupId, this.roomIO(groupId), settings);
    // persistent roles
    for (const r of this.d.db.all<any>('SELECT user_id, role, succession_order FROM roles WHERE group_id = ?', groupId)) {
      if (r.role === 'host') room.hostId = Number(r.user_id);
      else if (r.role === 'deputy') room.deputies.set(Number(r.user_id), { order: r.succession_order ?? 99, scope: 'persistent' });
    }
    // recent chat survives restarts (retention: CHAT_RETENTION_DAYS)
    const chat = this.d.db.all<any>(
      `SELECT c.id, c.user_id, c.text, c.created_at, u.first_name, u.last_name, u.username FROM chat_messages c LEFT JOIN users u ON u.id = c.user_id
        WHERE c.group_id = ? AND c.deleted_at IS NULL ORDER BY c.id DESC LIMIT 100`, groupId).reverse();
    room.loadChat(chat.map((r) => ({ id: r.id, userId: Number(r.user_id), name: [r.first_name, r.last_name].filter(Boolean).join(' ') || (r.username ? '@' + r.username : 'Player'), text: r.text, at: r.created_at, frame: null })));
    a = {
      groupId, room, conns: new Map(), session: null, spec: null, game: null, fresh: true, starting: false,
      stateTimer: null, persistTimer: null, idleTimer: null, checkpointTimer: null, resumable: this.findResumable(groupId), lastHealth: 'idle',
    };
    this.active.set(groupId, a);
    return a;
  }

  private findResumable(groupId: number): ActiveRoom['resumable'] {
    const r = this.d.db.get<any>(
      `SELECT c.id, c.game_id, c.frame, c.created_at, g.display_name FROM checkpoints c JOIN games g ON g.id = c.game_id
        WHERE c.group_id = ? AND c.valid = 1 AND g.status = 'ready' ORDER BY c.id DESC LIMIT 1`, groupId);
    return r ? { gameId: r.game_id, checkpointId: r.id, frame: r.frame, at: r.created_at, title: r.display_name } : null;
  }

  private broadcast(groupId: number, msg: unknown): void {
    const a = this.active.get(groupId);
    if (!a) return;
    for (const c of a.conns.values()) c.sendJson(msg);
  }

  private scheduleState(groupId: number): void {
    const a = this.active.get(groupId);
    if (!a || a.stateTimer) return;
    a.stateTimer = setTimeout(() => {
      a.stateTimer = null;
      const state = { ...a.room.publicState(), resumable: a.session ? null : a.resumable, starting: a.starting, health: a.lastHealth };
      for (const c of a.conns.values()) {
        c.sendJson({ t: 'room', state });
        c.sendJson(a.room.you(c.userId));
      }
      if (!a.persistTimer) {
        a.persistTimer = setTimeout(() => {
          a.persistTimer = null;
          if (this.closed) return;
          this.d.db.run('INSERT INTO room_state (group_id, state, updated_at) VALUES (?, ?, ?) ON CONFLICT(group_id) DO UPDATE SET state = excluded.state, updated_at = excluded.updated_at',
            groupId, JSON.stringify(a.room.snapshot()), Date.now());
        }, 1000);
      }
    }, 60);
  }

  // ================================================================ connections

  viewerCount(groupId: number): number {
    return this.active.get(groupId)?.conns.size ?? 0;
  }

  attach(conn: Conn): { ok: true } | { ok: false; error: string } {
    const a = this.ensure(conn.groupId);
    const room = a.room;
    const privileged = room.seatOf(conn.userId) !== null || room.queueIndex(conn.userId) >= 0 || room.canModerate(conn.userId);
    const distinctViewers = new Set([...a.conns.values()].map((c) => c.userId));
    if (!privileged && !distinctViewers.has(conn.userId) && distinctViewers.size >= room.settings.maxViewers) {
      return { ok: false, error: `The room is full (${room.settings.maxViewers} people). Try again soon.` };
    }
    // per-user connection cap (tabs/devices)
    const mine = [...a.conns.values()].filter((c) => c.userId === conn.userId);
    if (mine.length >= 4) mine[0].close(4001, 'too many connections for this user');
    a.conns.set(conn.id, conn);
    if (a.idleTimer) {
      clearTimeout(a.idleTimer);
      a.idleTimer = null;
    }
    room.connect(conn.userId, conn.id, { name: conn.name, tgRole: conn.tgRole });
    conn.sendJson({ t: 'chat_history', msgs: room.chatLog.slice(-100) });
    if (a.session && a.spec) {
      conn.sendJson(this.sessionInfo(a));
      if (a.session.paused) a.session.resume();
    }
    this.scheduleState(conn.groupId);
    return { ok: true };
  }

  detach(conn: Conn): void {
    const a = this.active.get(conn.groupId);
    if (!a || !a.conns.has(conn.id)) return;
    a.conns.delete(conn.id);
    a.session?.unsubscribe(conn.id);
    a.room.disconnect(conn.id);
    if (a.conns.size === 0) this.onEmpty(a);
  }

  // Everyone left: pause (frames stop; nothing advances), checkpoint, and
  // release the emulator after the idle period.
  private onEmpty(a: ActiveRoom): void {
    if (a.session) {
      a.session.pause();
      void this.checkpoint(a);
    }
    if (a.idleTimer) clearTimeout(a.idleTimer);
    a.idleTimer = setTimeout(() => void this.release(a, 'idle'), this.d.cfg.roomIdleReleaseSec * 1000);
    a.idleTimer.unref();
  }

  private async release(a: ActiveRoom, why: string): Promise<void> {
    a.idleTimer = null;
    if (a.conns.size > 0) return;
    if (a.session) await this.checkpoint(a);
    this.stopSession(a, why);
    a.room.setGame(null);
    a.resumable = this.findResumable(a.groupId);
    log.info('rooms', `released room ${a.groupId} (${why})`);
  }

  revokeUser(groupId: number, userId: number): void {
    const a = this.active.get(groupId);
    if (!a) return;
    for (const c of [...a.conns.values()]) if (c.userId === userId) c.close(4003, 'membership revoked');
    if (a.room.seatOf(userId)) {
      try {
        a.room.leaveSeat(userId);
      } catch { /* already gone */ }
    }
    a.room.leaveQueue(userId);
  }

  // Telegram status changed for a member who is still in the group: live
  // connections and the room pick up the new role (moderation rights follow it).
  updateMemberRole(groupId: number, userId: number, role: TgRole): void {
    const a = this.active.get(groupId);
    if (!a) return;
    for (const c of a.conns.values()) if (c.userId === userId) c.tgRole = role;
    a.room.setTgRole(userId, role);
  }

  async disableGroup(groupId: number, reason: string): Promise<void> {
    const a = this.active.get(groupId);
    if (!a) return;
    for (const c of [...a.conns.values()]) c.close(4004, reason);
    if (a.session) await this.checkpoint(a);
    this.stopSession(a, reason);
    a.room.dispose();
    this.active.delete(groupId);
  }

  claimHost(groupId: number, userId: number, isTgAdmin: boolean): { ok: boolean; message: string } {
    const a = this.ensure(groupId);
    if (!isTgAdmin) return { ok: false, message: 'Only a group admin can claim the host role.' };
    if (a.room.hostId !== null && a.room.hostId !== userId) return { ok: false, message: 'This room already has a host. The host can transfer the role from the arcade.' };
    a.room.setHost(userId);
    this.d.records.audit(groupId, userId, 'host.claim', userId, null);
    return { ok: true, message: 'You are now the arcade host for this group.' };
  }

  // ================================================================ games

  private sessionInfo(a: ActiveRoom): Record<string, unknown> {
    const s = a.session!, spec = a.spec!;
    return {
      t: 'session',
      sessionId: s.id,
      gameId: spec.gameId,
      title: spec.title,
      system: spec.system,
      core: SYSTEMS[spec.system].core,
      files: spec.clientFiles,
      gamePath: spec.gamePath,
      options: { ...SYSTEMS[spec.system].options, ...spec.options },
      portDevices: portDevicesFor(SYSTEMS[spec.system], s.ports),
      ports: s.ports,
      fps: s.frameRate,
      epoch: s.epochSeconds,
      mode: spec.mode,
      hashEvery: this.d.cfg.hashEveryFrames,
      allowed: Array.from({ length: s.ports }, (_, p) => s.allowedMask(p)),
      coinBit: a.game?.coinBit ?? null,
      width: s.info?.width ?? 0,
      height: s.info?.height ?? 0,
      aspect: s.info?.aspect ?? 0,
      rotation: s.info?.rotation ?? 0,
      descriptors: s.info?.descriptors ?? [],
      fresh: a.fresh,
    };
  }

  activeSessions(): number {
    let n = 0;
    for (const a of this.active.values()) if (a.session) n++;
    return n;
  }

  // Boots a game for a room. When `replace` is set the new game is fully
  // started and validated before the old one is stopped; on failure the
  // old game, seats and queue are untouched.
  async startGame(groupId: number, gameId: number, opts: { resumeCheckpointId?: number; seatOrder?: number[]; reason?: string } = {}): Promise<{ ok: boolean; error?: string }> {
    const a = this.ensure(groupId);
    if (a.starting) return { ok: false, error: 'A game is already starting.' };
    if (!a.session && this.activeSessions() >= this.d.cfg.maxActiveRooms) {
      return { ok: false, error: `The arcade is at capacity (${this.d.cfg.maxActiveRooms} active rooms). Try again later.` };
    }
    a.starting = true;
    this.scheduleState(groupId);
    let session: GameSession | null = null;
    try {
      const spec = await this.d.shelf.sessionSpec(groupId, gameId);
      const sys = SYSTEMS[spec.system];
      let restore: { frame: number; data: Uint8Array; context: number } | undefined;
      let fresh = true;
      if (opts.resumeCheckpointId !== undefined) {
        const cp = this.d.db.get<any>('SELECT * FROM checkpoints WHERE id = ? AND group_id = ? AND game_id = ? AND valid = 1', opts.resumeCheckpointId, groupId, gameId);
        if (!cp) throw new RoomError('no_checkpoint', 'That save point is no longer available.');
        if (cp.compat_key !== spec.compatKey) {
          this.d.db.run('UPDATE checkpoints SET valid = 0 WHERE id = ?', cp.id);
          throw new RoomError('incompatible_checkpoint', 'The save point was made with a different ROM, core build or settings and cannot be resumed. Start fresh instead.');
        }
        const path = join(this.d.cfg.checkpointDir, cp.file);
        if (!existsSync(path)) throw new RoomError('no_checkpoint', 'The save point file is missing.');
        restore = { frame: cp.frame, data: new Uint8Array(readFileSync(path)), context: 0 };
        fresh = false;
      }
      session = new GameSession(
        { system: sys, files: spec.files, gamePath: spec.gamePath, options: spec.options, ports: spec.ports, adapterId: spec.adapterId },
        {
          coreDir: this.d.cfg.coreDir,
          emu: { maxOldSpaceMb: this.d.cfg.workerHeapMb, bootTimeoutMs: 60_000, requestTimeoutMs: 15_000, heartbeatMs: 5000 },
          logSeconds: this.d.cfg.replayLogSeconds,
          hashEvery: this.d.cfg.hashEveryFrames,
          statusEvery: 6,
          maxCatchUpFrames: 30,
        },
      );
      let info;
      try {
        info = await session.start(restore);
      } catch (e) {
        if (restore && opts.resumeCheckpointId !== undefined) {
          this.d.db.run('UPDATE checkpoints SET valid = 0 WHERE id = ?', opts.resumeCheckpointId);
          throw new RoomError('corrupt_checkpoint', `The save point could not be loaded (${(e as Error).message}). Start fresh instead.`);
        }
        throw e;
      }
      const adapter = spec.adapterId ? adapterById(spec.adapterId) : null;
      // Coins only ever come from the server's coin policy: the coin input
      // is removed from what players may send directly.
      const coinBit = adapter ? adapter.meta.coinBit : sys.coinBit;
      session.setAllowed(allowedMasks(sys, info.descriptors, spec.ports).map((m) => (coinBit === null ? m : m & ~(1 << coinBit))));
      // the new game works: now stop the old one
      const old = a.session;
      if (old) {
        await this.checkpoint(a);
        this.stopSession(a, opts.reason ?? 'switch');
      }
      this.d.db.run(
        'INSERT INTO game_sessions (id, group_id, game_id, compat_key, adapter_id, fresh, resumed_from, started_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
        session.id, groupId, gameId, spec.compatKey, spec.adapterId, fresh ? 1 : 0, opts.resumeCheckpointId ?? null, Date.now(),
      );
      a.session = session;
      a.spec = spec;
      a.fresh = fresh;
      a.resumable = null;
      this.d.shelf.pin(groupId, spec.clientFiles.map((f) => f.sha256));
      this.d.shelf.markPlayed(groupId, gameId);
      const s = session;
      const game: RoomGame = {
        gameId,
        title: spec.title,
        sessionId: s.id,
        mode: spec.mode,
        ports: s.ports,
        adapter: adapter?.meta ?? null,
        coinBit: adapter ? adapter.meta.coinBit : sys.coinBit,
        status: null,
        frame: () => s.nextFrame,
        resetPort: (p) => s.resetPort(p),
        releasePort: (p) => s.releasePort(p),
        pulse: (p, bit) => s.pulse(p, bit),
      };
      a.game = game;
      s.on('status', (st) => {
        game.status = st;
        a.room.onStatus();
      });
      s.on('adapter', (events) => a.room.onAdapterEvents(events));
      s.on('crashed', () => {
        a.lastHealth = 'recovering';
        this.broadcast(groupId, { t: 'event', kind: 'warn', text: 'The emulator hit a problem; restoring from the last save point…', frame: null, at: Date.now() });
      });
      s.on('recovered', () => {
        a.lastHealth = 'ok';
        this.broadcast(groupId, { t: 'event', kind: 'info', text: 'Recovered. The game continues.', frame: null, at: Date.now() });
      });
      s.on('failed', (why) => {
        a.lastHealth = 'failed';
        this.d.shelf.markPlayed(groupId, gameId, 'needs_attention');
        this.broadcast(groupId, { t: 'event', kind: 'warn', text: `The game stopped: ${why}. A host or deputy can reset it.`, frame: null, at: Date.now() });
        this.scheduleState(groupId);
      });
      a.lastHealth = 'ok';
      a.room.setGame(game, opts.seatOrder);
      if (a.checkpointTimer) clearInterval(a.checkpointTimer);
      a.checkpointTimer = setInterval(() => void this.checkpoint(a), this.d.cfg.checkpointIntervalSec * 1000);
      a.checkpointTimer.unref();
      // first checkpoint right away so a worker crash is always recoverable
      setTimeout(() => void this.checkpoint(a), 2000).unref();
      if (a.conns.size > 0) s.resume();
      this.broadcast(groupId, this.sessionInfo(a));
      this.scheduleState(groupId);
      log.info('rooms', `room ${groupId} started game ${gameId} session ${s.id} (${fresh ? 'fresh' : 'resumed'})`);
      return { ok: true };
    } catch (e) {
      session?.stop();
      const msg = e instanceof RoomError ? e.message : `could not start the game (${(e as Error).message})`;
      log.warn('rooms', `start failed for room ${groupId}: ${(e as Error).message}`);
      return { ok: false, error: msg };
    } finally {
      a.starting = false;
      this.scheduleState(groupId);
    }
  }

  private async resetGame(groupId: number, reason: string): Promise<{ ok: boolean; error?: string }> {
    const a = this.active.get(groupId);
    if (!a?.spec) return { ok: false, error: 'No game is running.' };
    const order = a.room.seats.map((s) => s.userId).filter((u): u is number => u !== null);
    return this.startGame(groupId, a.spec.gameId, { seatOrder: order, reason: `reset: ${reason}` });
  }

  private stopSession(a: ActiveRoom, why: string): void {
    if (!a.session) return;
    this.d.db.run('UPDATE game_sessions SET ended_at = ?, end_reason = ? WHERE id = ?', Date.now(), why, a.session.id);
    a.session.stop();
    a.session = null;
    a.spec = null;
    a.game = null;
    if (a.checkpointTimer) clearInterval(a.checkpointTimer);
    a.checkpointTimer = null;
    this.d.shelf.unpin(a.groupId);
  }

  private async checkpoint(a: ActiveRoom): Promise<void> {
    const s = a.session, spec = a.spec;
    if (!s || !spec || this.closed) return;
    const cp = await s.takeCheckpoint();
    if (!cp || a.session !== s || this.closed) return;
    const dir = join(this.d.cfg.checkpointDir, String(a.groupId));
    mkdirSync(dir, { recursive: true, mode: 0o700 });
    const file = `${s.id}-${cp.frame}.state`;
    try {
      writeFileSync(join(dir, file), cp.data, { mode: 0o600 });
    } catch (e) {
      log.warn('rooms', `could not write checkpoint: ${(e as Error).message}`);
      return;
    }
    this.d.db.run(
      'INSERT INTO checkpoints (group_id, game_id, session_id, frame, file, compat_key, size, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
      a.groupId, spec.gameId, s.id, cp.frame, `${a.groupId}/${file}`, spec.compatKey, cp.data.length, Date.now(),
    );
    // keep only the latest few checkpoints per room
    const old = this.d.db.all<any>('SELECT id, file FROM checkpoints WHERE group_id = ? ORDER BY id DESC LIMIT -1 OFFSET 3', a.groupId);
    for (const o of old) {
      this.d.db.run('DELETE FROM checkpoints WHERE id = ?', o.id);
      try {
        const { rmSync } = await import('node:fs');
        rmSync(join(this.d.cfg.checkpointDir, o.file), { force: true });
      } catch { /* best effort */ }
    }
  }

  async shutdown(): Promise<void> {
    for (const a of this.active.values()) {
      for (const t of [a.stateTimer, a.persistTimer, a.idleTimer]) if (t) clearTimeout(t);
      a.stateTimer = a.persistTimer = a.idleTimer = null;
      for (const c of a.conns.values()) c.close(1012, 'server restarting');
      if (a.session) await this.checkpoint(a);
      this.stopSession(a, 'shutdown');
      this.d.db.run('INSERT INTO room_state (group_id, state, updated_at) VALUES (?, ?, ?) ON CONFLICT(group_id) DO UPDATE SET state = excluded.state, updated_at = excluded.updated_at',
        a.groupId, JSON.stringify(a.room.snapshot()), Date.now());
      a.room.dispose();
    }
    this.active.clear();
    this.closed = true;
  }

  // One plain-text line for the group's lobby card.
  summary(groupId: number): string | null {
    const a = this.active.get(groupId);
    if (!a?.game) return null;
    const players = a.room.seats.filter((s) => s.userId !== null).length;
    const watching = Math.max(0, new Set([...a.conns.values()].map((c) => c.userId)).size - players);
    return `Now playing: ${a.game.title} (${players} playing, ${watching} watching)`;
  }

  health(): Record<string, unknown> {
    return {
      rooms: this.active.size,
      sessions: this.activeSessions(),
      viewers: [...this.active.values()].reduce((n, a) => n + a.conns.size, 0),
      details: [...this.active.values()].map((a) => ({
        groupId: a.groupId, viewers: a.conns.size, session: a.session?.id ?? null, frame: a.session?.nextFrame ?? null,
        workerFrame: a.session?.workerFrame ?? null, paused: a.session?.paused ?? null, health: a.lastHealth, stats: a.session?.stats ?? null,
      })),
    };
  }

  // ================================================================ client messages

  handleBinary(conn: Conn, data: Uint8Array): void {
    const a = this.active.get(conn.groupId);
    if (!a?.session) return;
    const m = decodeInput(data);
    if (!m) return;
    if (!a.room.inputAllowed(conn.id, m.port)) return; // spectators and former owners
    a.session.input(m.port, m.epoch, m.seq, m.mask, m.clientMs);
  }

  async handleJson(conn: Conn, msg: any): Promise<void> {
    const a = this.active.get(conn.groupId);
    if (!a || typeof msg !== 'object' || msg === null) return;
    const room = a.room;
    const u = conn.userId;
    const reply = (ok: boolean, extra: Record<string, unknown> = {}) => {
      if (msg.id !== undefined) conn.sendJson({ t: 'ack', id: msg.id, ok, ...extra });
    };
    try {
      switch (msg.t) {
        case 'sync': {
          if (!a.session) throw new RoomError('no_game', 'No game is running.');
          a.session.subscribe({ id: conn.id, live: false, send: (d) => conn.sendBin(d) });
          const ok = await a.session.sync(conn.id, { raw: !!msg.raw });
          reply(ok);
          return;
        }
        case 'blur':
          room.blur(conn.id);
          return;
        case 'ping':
          conn.sendJson({ t: 'pong', c: msg.c, s: Date.now(), f: a.session?.nextFrame ?? null });
          return;
        case 'chat':
          room.chat(u, String(msg.text ?? ''));
          reply(true);
          return;
        case 'cmd':
          await this.command(a, conn, String(msg.op ?? ''), msg.args ?? {});
          reply(true);
          return;
      }
    } catch (e) {
      if (e instanceof RoomError) reply(false, { code: e.code, error: e.message });
      else {
        log.warn('rooms', `command failed: ${(e as Error).message}`);
        reply(false, { code: 'internal', error: 'Something went wrong.' });
      }
    }
  }

  private async command(a: ActiveRoom, conn: Conn, op: string, args: any): Promise<void> {
    const room = a.room;
    const u = conn.userId;
    const num = (v: unknown, name: string): number => {
      const n = Number(v);
      if (!Number.isInteger(n)) throw new RoomError('bad_args', `Missing ${name}.`);
      return n;
    };
    const text = (v: unknown, max = 200): string => String(v ?? '').slice(0, max);
    switch (op) {
      case 'seat.take': return room.takeSeat(u, conn.id, args.port === undefined ? undefined : num(args.port, 'port'));
      case 'seat.leave': return room.leaveSeat(u);
      case 'seat.control_here': return room.takeControlHere(u, conn.id);
      case 'queue.join': return room.joinQueue(u);
      case 'queue.leave': return room.leaveQueue(u);
      case 'offer.accept': return room.acceptOffer(u, conn.id);
      case 'offer.decline': return room.declineOffer(u);
      case 'coin': return room.insertCoin(u);
      case 'controller.pass': return room.passController(u, args.to === undefined ? undefined : num(args.to, 'to'));
      case 'turns.join': return room.joinTurns(u);
      case 'turns.leave': return room.leaveTurns(u);
      case 'turns.end': return room.endTurn(u);
      case 'result.report': {
        const o = String(args.outcome);
        if (o !== 'won' && o !== 'lost' && o !== 'draw') throw new RoomError('bad_args', 'Unknown result.');
        return room.reportResult(u, o);
      }
      case 'player.out': return room.reportOut(u, args.user === undefined ? undefined : num(args.user, 'user'));
      case 'vote.start': return room.startBallot(u);
      case 'vote.nominate': return room.nominate(u, num(args.gameId, 'gameId'));
      case 'vote.cast': return room.vote(u, String(args.choice ?? ''));
      case 'vote.optin': return room.optIn(u, !!args.play);
      case 'host.acting_answer': return room.answerActingHostOffer(u, !!args.accept);
      // moderation
      case 'mod.override': return room.override(u, args.gameId === null || args.gameId === undefined ? null : num(args.gameId, 'gameId'), text(args.reason));
      case 'mod.switch_now': return room.switchNow(u);
      case 'mod.adjudicate': return room.adjudicate(u, args.winnerPort === 'void' ? 'void' : args.winnerPort === null ? null : num(args.winnerPort, 'winnerPort'), text(args.reason));
      case 'mod.remove_seat': {
        const reason = String(args.reason) as RemovalReason;
        if (!REMOVAL_REASONS.includes(reason)) throw new RoomError('bad_reason', 'Choose a reason.');
        return room.removeFromSeat(u, num(args.user, 'user'), reason, text(args.note));
      }
      case 'mod.remove_queue': return room.removeFromQueueBy(u, num(args.user, 'user'), text(args.reason));
      case 'mod.resolve_controller': return room.resolveController(u, args.to === null || args.to === undefined ? null : num(args.to, 'to'), text(args.reason));
      case 'mod.reset': return room.resetGame(u, text(args.reason));
      case 'mod.chat_delete': return room.deleteChat(u, num(args.id, 'id'), text(args.reason));
      case 'mod.mute': return room.mute(u, num(args.user, 'user'), num(args.minutes, 'minutes'), text(args.reason));
      case 'host.deputy_add': return room.appointDeputy(u, num(args.user, 'user'), args.scope === 'persistent' ? 'persistent' : 'session', args.order === undefined ? undefined : num(args.order, 'order'));
      case 'host.deputy_remove': return room.removeDeputy(u, num(args.user, 'user'));
      case 'host.succession': return room.setSuccession(u, Array.isArray(args.order) ? args.order.map((x: unknown) => num(x, 'order')) : []);
      case 'host.transfer': return room.transferHost(u, num(args.user, 'user'));
      case 'host.settings': {
        room.updateSettings(u, args.settings ?? {});
        const g = this.d.groups.byId(a.groupId);
        if (g) this.d.groups.updateSettings(a.groupId, { ...g.settings, room: room.settings });
        return;
      }
      // starting / resuming when nothing is running (no switch involved)
      case 'game.start':
      case 'game.resume': {
        if (a.session) throw new RoomError('game_running', 'A game is running; vote to change it.');
        const gameId = num(args.gameId, 'gameId');
        const r = await this.startGame(a.groupId, gameId, op === 'game.resume' ? { resumeCheckpointId: num(args.checkpointId, 'checkpointId') } : {});
        if (!r.ok) throw new RoomError('start_failed', r.error ?? 'Could not start the game.');
        return;
      }
      default:
        throw new RoomError('unknown_op', 'Unknown command.');
    }
  }
}

export function sha256Hex(data: Uint8Array | string): string {
  return createHash('sha256').update(data).digest('hex');
}
