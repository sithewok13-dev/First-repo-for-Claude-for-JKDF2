// Verified Telegram group membership.
//
// Launch data from a group's direct link has no chat id, so it never proves
// membership: the server asks Telegram with getChatMember(group chat, user).
// (Telegram only guarantees this for other users when the bot is an admin.)
//
// - Status mapping follows the Bot API server's own predicate: creator,
//   administrator and member are members; restricted is a member only while
//   is_member is true; left and kicked are not. ("creator" stays a member even
//   if the owner left; Telegram does not tell bots otherwise.)
// - Results are cached in the `memberships` table for MEMBERSHIP_TTL_SEC;
//   `force` bypasses the cache. Concurrent checks for the same user share one
//   request.
// - Fail closed: if Telegram cannot answer, the user is NOT a member, unless a
//   positive answer younger than the TTL is on record.
// - A user who stops being a member (seen by a check or a chat_member update)
//   is reported to onRevoked listeners, which revoke sessions and evict.

import type { Config } from '../config.ts';
import type { Db } from '../db/db.ts';
import type { Groups } from '../groups.ts';
import { log } from '../log.ts';
import type { TgRole } from '../room/types.ts';
import { TelegramApi, TelegramError, errText } from './api.ts';
import { migrateChat, type MigrationResult } from './migration.ts';
import type { ChatMemberInfo, MembershipService } from './types.ts';

type Status = ChatMemberInfo['status'];
const STATUSES = new Set<Status>(['creator', 'administrator', 'member', 'restricted', 'left', 'kicked']);

export function notMember(status: Status = 'unknown'): ChatMemberInfo {
  return { status, isMember: false, tgRole: 'member' };
}

function roleOf(status: Status): TgRole {
  return status === 'creator' ? 'creator' : status === 'administrator' ? 'administrator' : 'member';
}

// Maps a Bot API ChatMember object (untrusted shape) to ChatMemberInfo.
export function chatMemberInfo(raw: unknown): ChatMemberInfo {
  const m = raw as { status?: unknown; is_member?: unknown } | null;
  const status = typeof m?.status === 'string' && STATUSES.has(m.status as Status) ? (m.status as Status) : 'unknown';
  switch (status) {
    case 'creator':
    case 'administrator':
    case 'member':
      return { status, isMember: true, tgRole: roleOf(status) };
    case 'restricted':
      return { status, isMember: m?.is_member === true, tgRole: 'member' };
    default:
      return notMember(status);
  }
}

// Errors that mean "this user is not in the chat" rather than "Telegram could
// not answer".
function isAbsentUser(e: TelegramError): boolean {
  return e.code === 400 && /user not found|member not found|participant_id_invalid|user_not_participant/i.test(e.description);
}

type Listener = (groupId: number, userId: number) => void;

function fire(listeners: Listener[], groupId: number, userId: number): void {
  for (const fn of listeners) {
    try {
      fn(groupId, userId);
    } catch (e) {
      log.error('membership', 'revocation listener failed', { group: groupId, err: errText(e) });
    }
  }
}

export interface TelegramMembershipOptions {
  db: Db;
  groups: Groups;
  ttlSec: number;
  // Raw Bot API getChatMember; throws TelegramError on failure.
  getChatMember: (chatId: number, userId: number) => Promise<unknown>;
  now?: () => number;
}

export class TelegramMembership implements MembershipService {
  private readonly db: Db;
  private readonly groups: Groups;
  private readonly ttlMs: number;
  private readonly getChatMember: (chatId: number, userId: number) => Promise<unknown>;
  private readonly now: () => number;
  private readonly listeners: Listener[] = [];
  private readonly migrationListeners: ((r: MigrationResult) => void)[] = [];
  private readonly inflight = new Map<string, Promise<ChatMemberInfo>>();
  // Bumped by invalidate(): an answer requested before the bump may predate
  // the change that caused it and is not trusted. Only needed while lookups
  // are running, so entries exist only then (`running` counts them); every
  // chat_member update invalidates, and the map must not grow with them.
  private readonly generation = new Map<string, number>();
  private readonly running = new Map<string, number>();

  constructor(o: TelegramMembershipOptions) {
    this.db = o.db;
    this.groups = o.groups;
    this.ttlMs = o.ttlSec * 1000;
    this.getChatMember = o.getChatMember;
    this.now = o.now ?? Date.now;
  }

  async check(groupId: number, userId: number, opts: { force?: boolean } = {}): Promise<ChatMemberInfo> {
    if (!Number.isSafeInteger(userId) || userId <= 0) return notMember();
    const group = this.groups.byId(groupId);
    // No access at all while the bot is out of the group or the group is off.
    if (!group || group.status !== 'active') return notMember();
    if (!opts.force) {
      const c = this.cached(groupId, userId);
      if (c && this.now() - c.checkedAt < this.ttlMs) return c.info;
    }
    const k = `${groupId}:${userId}`;
    let p = this.inflight.get(k);
    if (!p) {
      this.running.set(k, (this.running.get(k) ?? 0) + 1);
      const req: Promise<ChatMemberInfo> = this.lookup(groupId, userId, group.chatId, 0).finally(() => {
        if (this.inflight.get(k) === req) this.inflight.delete(k);
        const left = (this.running.get(k) ?? 1) - 1;
        if (left > 0) this.running.set(k, left);
        else {
          this.running.delete(k);
          this.generation.delete(k);   // nothing older is still out
        }
      });
      this.inflight.set(k, req);
      p = req;
    }
    return p;
  }

  invalidate(groupId: number, userId: number): void {
    const k = `${groupId}:${userId}`;
    if (this.running.has(k)) this.generation.set(k, (this.generation.get(k) ?? 0) + 1);
    this.inflight.delete(k);
    this.db.run('UPDATE memberships SET checked_at = 0 WHERE group_id = ? AND user_id = ?', groupId, userId);
  }

  onRevoked(fn: Listener): void {
    this.listeners.push(fn);
  }

  onMigrated(fn: (r: MigrationResult) => void): void {
    this.migrationListeners.push(fn);
  }

  observe(groupId: number, userId: number, info: ChatMemberInfo): void {
    this.invalidate(groupId, userId);
    if (info.isMember) return;
    this.store(groupId, userId, info);
    fire(this.listeners, groupId, userId);
  }

  private cached(groupId: number, userId: number): { info: ChatMemberInfo; checkedAt: number } | null {
    const r = this.db.get<{ tg_status: string; is_member: number; checked_at: number }>(
      'SELECT tg_status, is_member, checked_at FROM memberships WHERE group_id = ? AND user_id = ?', groupId, userId,
    );
    if (!r) return null;
    const status = (STATUSES.has(r.tg_status as Status) ? r.tg_status : 'unknown') as Status;
    return { info: { status, isMember: !!r.is_member, tgRole: roleOf(status) }, checkedAt: Number(r.checked_at) };
  }

  // Records an answer; returns true if it turned a member into a non-member.
  private store(groupId: number, userId: number, info: ChatMemberInfo): boolean {
    const prev = this.db.get<{ is_member: number }>('SELECT is_member FROM memberships WHERE group_id = ? AND user_id = ?', groupId, userId);
    this.db.run(
      `INSERT INTO memberships (group_id, user_id, tg_status, is_member, is_admin, checked_at) VALUES (?, ?, ?, ?, ?, ?)
       ON CONFLICT(group_id, user_id) DO UPDATE SET tg_status = excluded.tg_status, is_member = excluded.is_member, is_admin = excluded.is_admin, checked_at = excluded.checked_at`,
      groupId, userId, info.status, info.isMember ? 1 : 0, info.tgRole === 'member' ? 0 : 1, this.now(),
    );
    return !!prev?.is_member && !info.isMember;
  }

  private async lookup(groupId: number, userId: number, chatId: number, attempt: number): Promise<ChatMemberInfo> {
    const k = `${groupId}:${userId}`;
    const gen = this.generation.get(k) ?? 0;
    let raw: unknown;
    try {
      raw = await this.getChatMember(chatId, userId);
    } catch (e) {
      if (e instanceof TelegramError && e.migrateToChatId && attempt === 0) {
        // The group became a supergroup and we missed the notice.
        const moved = migrateChat(this.db, this.groups, chatId, e.migrateToChatId);
        if (moved) {
          for (const fn of this.migrationListeners) {
            try {
              fn(moved);
            } catch (err) {
              log.error('membership', 'migration listener failed', { group: groupId, err: errText(err) });
            }
          }
          return this.lookup(groupId, userId, moved.group.chatId, attempt + 1);
        }
      }
      if (e instanceof TelegramError && isAbsentUser(e)) {
        raw = { status: 'left' };
      } else {
        const c = this.cached(groupId, userId);
        if (c && c.info.isMember && this.now() - c.checkedAt < this.ttlMs) {
          log.warn('membership', 'getChatMember failed; using a recent positive check', { group: groupId, err: errText(e) });
          return c.info;
        }
        log.warn('membership', 'getChatMember failed; denying access (fail closed)', { group: groupId, err: errText(e) });
        return notMember();
      }
    }
    if ((this.generation.get(k) ?? 0) !== gen) {
      // Invalidated while the request was in flight (e.g. a chat_member update
      // arrived): ask again rather than trust an answer that may be older.
      return attempt < 2 ? this.lookup(groupId, userId, chatId, attempt + 1) : notMember();
    }
    const info = chatMemberInfo(raw);
    if (this.store(groupId, userId, info)) {
      log.info('membership', 'user is no longer a member; revoking access', { group: groupId, user: userId });
      fire(this.listeners, groupId, userId);
    }
    return info;
  }
}

// ------------------------------------------------------------------ dev mode

export interface DevMember {
  userId: number;
  groupId?: number;              // omitted: a member of every group
  role?: TgRole;
}

// Membership from a fixed allowlist written in code. For tests and local dev
// scripts only (DEV_MODE without a bot token): nothing is verified with
// Telegram.
export class DevMembership implements MembershipService {
  private readonly allow: DevMember[];
  private readonly revoked = new Set<string>();
  private readonly listeners: Listener[] = [];

  constructor(allow: readonly DevMember[]) {
    this.allow = allow.map((m) => ({ ...m }));
    log.warn('membership', 'using the DEV membership allowlist; group membership is NOT verified with Telegram');
  }

  async check(groupId: number, userId: number): Promise<ChatMemberInfo> {
    if (this.revoked.has(`${groupId}:${userId}`)) return notMember('left');
    const m = this.allow.find((a) => a.userId === userId && (a.groupId === undefined || a.groupId === groupId));
    if (!m) return notMember();
    const role = m.role ?? 'member';
    return { status: role, isMember: true, tgRole: role };
  }

  invalidate(): void {}

  onRevoked(fn: Listener): void {
    this.listeners.push(fn);
  }

  observe(groupId: number, userId: number, info: ChatMemberInfo): void {
    if (!info.isMember) this.revoke(groupId, userId);
  }

  // Simulates a user leaving the group.
  revoke(groupId: number, userId: number): void {
    this.revoked.add(`${groupId}:${userId}`);
    fire(this.listeners, groupId, userId);
  }
}

// ------------------------------------------------------------------ factory

// Telegram-verified membership when a bot token is configured. Without one,
// only DEV_MODE may run, on the given dev allowlist.
export function createMembership(d: { cfg: Config; db: Db; groups: Groups; fetch?: typeof fetch; dev?: readonly DevMember[] }): MembershipService {
  const { cfg } = d;
  if (cfg.botToken) {
    const api = new TelegramApi({ token: cfg.botToken, baseUrl: cfg.botApiBase, local: cfg.botApiLocal, fileDir: cfg.botFileDir, fetch: d.fetch });
    return new TelegramMembership({
      db: d.db,
      groups: d.groups,
      ttlSec: cfg.membershipTtlSec,
      getChatMember: (chatId, userId) => api.call('getChatMember', { chat_id: chatId, user_id: userId }),
    });
  }
  if (!cfg.devMode) throw new Error('BOT_TOKEN is required to verify group membership');
  return new DevMembership(d.dev ?? []);
}
