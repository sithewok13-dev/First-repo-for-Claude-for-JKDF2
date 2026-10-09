// The Telegram bot: update intake (long polling or webhook), group lifecycle,
// game uploads into the shelf, commands, the lobby card and rate-limited
// announcements.
//
// Facts this file relies on (sources in docs/notes-telegram.md):
// - Inline `web_app` buttons only work in private chats. In groups the launch
//   button is an inline `url` button holding the direct Mini App link
//   https://t.me/<bot>/<app>?startapp=<room token>, which clients open over
//   the group. That launch data carries no chat id, so access is verified
//   with getChatMember (./membership.ts), never from the link.
// - Updates can arrive more than once: each update_id is recorded in
//   telegram_updates BEFORE it is acted on, and a repeat is skipped.
// - Only an admin bot sees every group message (needed for document uploads)
//   and gets chat_member updates; no specific admin right is needed for that.
// - The cloud Bot API only downloads files up to 20 MB; a local Bot API
//   server (--local) lifts the limit.
// - Telegram allows bots about 20 messages per minute in a group; the outbox
//   keeps us at 10 and follows 429 retry_after.
//
// Update handling is synchronous bookkeeping (database writes) plus
// background tasks for anything slow (sends, downloads, validation), so one
// slow or failing update never holds up the others.

import { createHash, randomBytes, timingSafeEqual } from 'node:crypto';
import { mkdir, readdir, rm, stat } from 'node:fs/promises';
import { join } from 'node:path';
import type { AuthSessions } from '../auth/sessions.ts';
import type { Config } from '../config.ts';
import type { Db } from '../db/db.ts';
import type { Group, Groups } from '../groups.ts';
import { log } from '../log.ts';
import type { ShelfGame, ShelfService } from '../shelf/types.ts';
import { SYSTEMS } from '../../shared/systems.ts';
import {
  CLOUD_DOWNLOAD_LIMIT, DownloadError, TelegramApi, TelegramError, errText, isFileTooBig,
  type TgCallbackQuery, type TgChat, type TgChatMember, type TgChatMemberUpdated, type TgMessage, type TgUpdate, type TgUser,
} from './api.ts';
import { chatMemberInfo } from './membership.ts';
import { migrateChat, type MigrationResult } from './migration.ts';
import { Outbox, systemClock, type Clock, type SendOutcome } from './outbox.ts';
import * as T from './texts.ts';
import type { BotHooks, BotService, ChatMemberInfo, MembershipService } from './types.ts';

export type { Clock } from './outbox.ts';

// The HTTP server routes POSTs on this path to handleWebhook.
export const WEBHOOK_PATH = '/telegram/webhook';
// chat_member is not delivered unless asked for (and only to admin bots).
export const ALLOWED_UPDATES = ['message', 'my_chat_member', 'chat_member', 'callback_query'];

// Suggested when someone adds the bot as an admin (setMyDefaultAdministratorRights).
// can_manage_chat is implied by admin status itself; everything else is off.
const MINIMAL_ADMIN_RIGHTS = {
  is_anonymous: false,
  can_manage_chat: true,
  can_delete_messages: false,
  can_manage_video_chats: false,
  can_restrict_members: false,
  can_promote_members: false,
  can_change_info: false,
  can_invite_users: false,
  can_post_stories: false,
  can_edit_stories: false,
  can_delete_stories: false,
  can_send_welcome_messages: false,
  can_pin_messages: false,
  can_manage_topics: false,
};
const NEEDED_RIGHTS = new Set(['can_manage_chat']);

const GROUP_COMMANDS = [
  { command: 'arcade', description: 'Post the arcade launch button' },
  { command: 'shelf', description: 'List the games on this group\'s shelf' },
  { command: 'add', description: 'Reply to a game file to put it on the shelf' },
  { command: 'hostme', description: 'Group admins: become the room host' },
  { command: 'help', description: 'How the arcade works' },
];
const GROUP_COMMAND_NAMES = new Set(['arcade', 'shelf', 'add', 'hostme', 'help', 'start']);

const ACCEPTED_EXTENSIONS = new Set(Object.values(SYSTEMS).flatMap((s) => s.romExtensions));
const UNSUPPORTED_ARCHIVES = new Set(['7z', 'rar']);

const UPLOAD_CONCURRENCY = 2;
const MAX_QUEUED_UPLOADS = 40;
const MAX_QUEUED_UPLOADS_PER_GROUP = 10;
const CARD_DEBOUNCE_MS = 2000;              // upload cards that finish together share one message
const CARD_MAX_WAIT_MS = 60_000;            // a card waits at most this long, however busy the group
const MAX_CARD_ITEMS = 20;                  // kept per message; further outcomes are only counted
const COMMAND_COOLDOWN_MS = 10_000;
const PRIVATE_REPLY_COOLDOWN_MS = 30_000;
const MAX_PENDING_ANNOUNCEMENTS = 5;
const ANNOUNCEMENTS_PER_MESSAGE = 4;
const ANNOUNCEMENT_STALE_MS = 15 * 60_000;
const MAX_ANNOUNCE_DELAY_MS = 10 * 60_000;
const LOBBY_EDIT_WINDOW = 15;               // edit the lobby card in place if it is this close to the bottom
const ALLOWLIST_GRACE_MS = 10_000;
const UPDATE_RETENTION_MS = 3 * 86_400_000; // Telegram keeps undelivered updates for 24 h
const TEMP_PREFIX = 'tg-';
const STALE_TEMP_MS = 3600_000;             // download temp files older than this are crash leftovers

export interface BotDeps {
  cfg: Config;
  db: Db;
  groups: Groups;
  sessions: AuthSessions;
  shelf: ShelfService;
  membership: MembershipService;
  hooks: BotHooks;
  fetch?: typeof fetch;
  clock?: Clock;                // tests inject a fake clock
  pollTimeoutSec?: number;      // getUpdates long-poll timeout (default 50, Telegram's maximum)
}

interface Command {
  name: string;
  args: string;
}

interface UploadJob {
  groupId: number;
  userId: number;
  fileName: string;
  fileId: string;
  fileUniqueId: string;
  messageId: number;
}

interface PendingCards {
  items: T.CardItem[];          // at most MAX_CARD_ITEMS
  total: number;                // every outcome, including those not kept
  since: number;                // when the first one was added
}

interface PendingAnnouncement {
  key: string;
  text: string;
  notBefore: number;
  queuedAt: number;
}

// ------------------------------------------------------------------ helpers

function isUpdate(u: unknown): u is TgUpdate {
  const id = (u as { update_id?: unknown } | null)?.update_id;
  return typeof u === 'object' && u !== null && typeof id === 'number' && Number.isSafeInteger(id) && id >= 0;
}

function isChat(c: unknown): c is TgChat {
  const chat = c as TgChat | null;
  return !!chat && typeof chat === 'object' && Number.isSafeInteger(chat.id) && typeof chat.type === 'string';
}

function updateKind(u: TgUpdate): string {
  return Object.keys(u).find((k) => k !== 'update_id') ?? 'empty';
}

function extension(name: string): string {
  const i = name.lastIndexOf('.');
  return i > 0 ? name.slice(i + 1).toLowerCase() : '';
}

function secretMatches(given: string | undefined, expected: string): boolean {
  if (typeof given !== 'string' || given.length === 0) return false;
  const a = createHash('sha256').update(given).digest();
  const b = createHash('sha256').update(expected).digest();
  return timingSafeEqual(a, b);
}

// The bot's admin rights as reported in ChatMemberAdministrator.
function adminRights(m: TgChatMember): Record<string, boolean> {
  const out: Record<string, boolean> = {};
  for (const [k, v] of Object.entries(m)) {
    if ((k.startsWith('can_') || k === 'is_anonymous') && k !== 'can_be_edited' && typeof v === 'boolean') out[k] = v;
  }
  return out;
}

// "/cmd", "/cmd args" or "/cmd@this_bot args". Commands addressed to another
// bot are ignored.
export function parseCommand(m: TgMessage, botUsername: string | null): Command | null {
  const text = m.text;
  if (typeof text !== 'string' || !text.startsWith('/')) return null;
  const ent = Array.isArray(m.entities) ? m.entities.find((e) => e?.type === 'bot_command' && e.offset === 0) : undefined;
  if (!ent || !(ent.length >= 2)) return null;
  const [name, target] = text.slice(1, ent.length).split('@', 2);
  if (target && (!botUsername || target.toLowerCase() !== botUsername.toLowerCase())) return null;
  return { name: name.toLowerCase(), args: text.slice(ent.length).trim() };
}

// ---------------------------------------------------------------------- bot

export class TelegramBot implements BotService {
  private readonly cfg: Config;
  private readonly db: Db;
  private readonly groups: Groups;
  private readonly sessions: AuthSessions;
  private readonly shelf: ShelfService;
  private readonly membership: MembershipService;
  private readonly hooks: BotHooks;
  private readonly clock: Clock;
  private readonly api: TelegramApi | null;
  private readonly outbox: Outbox | null;
  private readonly pollTimeoutSec: number;

  private me: TgUser | null = null;
  private started = false;
  private running = false;
  private readonly stopping = new AbortController();      // aborts the long poll and downloads
  private polling: Promise<void> | null = null;
  private readonly tasks = new Set<Promise<unknown>>();   // background work
  private readonly timers = new Set<() => void>();        // one-shot timers (cancel functions)
  private readonly wakers = new Set<() => void>();        // pending sleeps, released by stop()
  private lastPrune = 0;

  private readonly uploadQueue: UploadJob[] = [];
  private uploadsRunning = 0;
  private readonly uploadsPending = new Map<number, number>();   // per group: queued + running
  private readonly uploadsInFlight = new Set<string>();          // `${groupId}:${fileUniqueId}`
  private readonly cards = new Map<number, PendingCards>();
  private readonly cardTimers = new Map<number, () => void>();

  private readonly announcements = new Map<number, { pending: PendingAnnouncement[]; timer: (() => void) | null }>();
  private announceSeq = 0;
  private readonly cooldowns = new Map<string, number>();
  private readonly deferredRejects = new Set<number>();

  constructor(d: BotDeps) {
    this.cfg = d.cfg;
    this.db = d.db;
    this.groups = d.groups;
    this.sessions = d.sessions;
    this.shelf = d.shelf;
    this.membership = d.membership;
    this.hooks = d.hooks;
    this.clock = d.clock ?? systemClock;
    this.pollTimeoutSec = d.pollTimeoutSec ?? 50;
    this.api = d.cfg.botToken
      ? new TelegramApi({ token: d.cfg.botToken, baseUrl: d.cfg.botApiBase, local: d.cfg.botApiLocal, fileDir: d.cfg.botFileDir, fetch: d.fetch })
      : null;
    this.outbox = this.api
      ? new Outbox({ api: this.api, groups: this.groups, clock: this.clock, onMigrated: (from, to) => this.onMigrate(from, to) })
      : null;
    // Whoever notices a lost membership (a check, or a chat_member update via
    // observe), the consequences are the same and happen here.
    this.membership.onRevoked((groupId, userId) => this.revokeMember(groupId, userId));
    // A membership check can be the first to learn of a supergroup upgrade.
    this.membership.onMigrated?.((r) => this.afterMigration(r));
  }

  // ------------------------------------------------------------ lifecycle

  async start(): Promise<void> {
    if (this.started) return;
    this.started = true;
    this.running = true;
    try {
      await this.startup();
    } catch (e) {
      // Nothing was started; a later start() may try again.
      this.started = false;
      this.running = false;
      throw e;
    }
  }

  private async startup(): Promise<void> {
    if (!this.api) {
      if (!this.cfg.devMode) throw new Error('BOT_TOKEN is required');
      log.warn('bot', 'no BOT_TOKEN: Telegram bot disabled (DEV_MODE)');
      return;
    }
    if (this.cfg.updatesMode === 'webhook' && !/^[A-Za-z0-9_-]{16,256}$/.test(this.cfg.webhookSecret)) {
      throw new Error('WEBHOOK_SECRET must be 16-256 characters of A-Z, a-z, 0-9, _ and -');
    }
    const me = await this.getMe();
    const want = this.cfg.botUsername.replace(/^@/, '');
    if (want && want.toLowerCase() !== (me.username ?? '').toLowerCase()) {
      throw new Error(`BOT_USERNAME (${want}) does not match the bot behind BOT_TOKEN (@${me.username})`);
    }
    this.me = me;
    log.info('bot', 'connected to Telegram', {
      bot: me.username, updates: this.cfg.updatesMode, localApi: this.cfg.botApiLocal,
      canJoinGroups: me.can_join_groups ?? null, privacyOff: me.can_read_all_group_messages ?? null,
    });
    this.pruneUpdates(true);
    this.track(this.sweepTemp());
    this.track(this.configureProfile());
    if (this.cfg.updatesMode === 'polling') this.polling = this.pollLoop();
    else if (this.cfg.updatesMode === 'webhook') this.track(this.registerWebhook());
    else log.info('bot', 'UPDATES_MODE=off: not receiving updates; announcements only');
  }

  stop(): void {
    if (!this.running) return;
    this.running = false;
    this.stopping.abort();
    for (const wake of [...this.wakers]) wake();
    for (const cancel of this.timers) cancel();
    this.timers.clear();
    for (const st of this.announcements.values()) st.timer?.();
    this.announcements.clear();
    for (const cancel of this.cardTimers.values()) cancel();
    this.cardTimers.clear();
    this.uploadQueue.length = 0;
    this.outbox?.stop();
  }

  // stop() plus waiting for the poll loop and background work to wind down.
  async shutdown(): Promise<void> {
    this.stop();
    await this.polling?.catch(() => {});
    await this.whenIdle();
  }

  // Resolves when no background task (send, download, ingest) is running.
  async whenIdle(): Promise<void> {
    while (this.tasks.size) await Promise.allSettled([...this.tasks]);
    await this.outbox?.whenIdle();
    if (this.tasks.size) await this.whenIdle();
  }

  private async getMe(): Promise<TgUser> {
    for (let attempt = 1; ; attempt++) {
      try {
        const me = await this.api!.call<TgUser>('getMe');
        if (!me || !Number.isSafeInteger(me.id) || typeof me.username !== 'string' || !me.username) throw new Error('getMe returned no username');
        return me;
      } catch (e) {
        if (e instanceof TelegramError && e.code === 401) throw new Error('Telegram rejected BOT_TOKEN (401 Unauthorized)');
        if (attempt >= 3 || !this.running) throw e;
        log.warn('bot', 'getMe failed; retrying', { err: errText(e), attempt });
        await this.sleep(2000 * attempt);
      }
    }
  }

  // Best effort: suggest minimal admin rights and publish the command list.
  private async configureProfile(): Promise<void> {
    const calls: [string, Record<string, unknown>][] = [
      ['setMyDefaultAdministratorRights', { rights: MINIMAL_ADMIN_RIGHTS }],
      ['setMyCommands', { commands: GROUP_COMMANDS, scope: { type: 'all_group_chats' } }],
      ['setMyCommands', { commands: [{ command: 'start', description: 'What this bot does' }], scope: { type: 'all_private_chats' } }],
    ];
    for (const [method, params] of calls) {
      if (!this.running) return;
      try {
        await this.api!.call(method, params);
      } catch (e) {
        log.warn('bot', `${method} failed (continuing)`, { err: errText(e) });
      }
    }
  }

  private async registerWebhook(): Promise<void> {
    const url = `${this.cfg.publicUrl}${WEBHOOK_PATH}`;
    const u = new URL(url);
    if (!this.cfg.botApiLocal && (u.protocol !== 'https:' || ![443, 80, 88, 8443].includes(u.port ? Number(u.port) : 443))) {
      log.warn('bot', 'the cloud Bot API only delivers webhooks over https to ports 443, 80, 88 or 8443; check PUBLIC_URL');
    }
    for (let delay = 5000; this.running; delay = Math.min(delay * 2, 300_000)) {
      try {
        // One connection: Telegram then delivers updates one at a time, in
        // order, as polling does. Several connections deliver concurrently,
        // so "bot removed" could be applied before an earlier "bot added", or
        // a member's "left" after their "rejoined". Each update is handled in
        // milliseconds (anything slow runs in the background), so one is plenty.
        await this.api!.call('setWebhook', { url, secret_token: this.cfg.webhookSecret, allowed_updates: ALLOWED_UPDATES, max_connections: 1 });
        log.info('bot', 'webhook registered', { path: WEBHOOK_PATH });
        return;
      } catch (e) {
        log.error('bot', 'setWebhook failed; retrying', { err: errText(e), retryInMs: delay });
        await this.sleep(delay);
      }
    }
  }

  private async pollLoop(): Promise<void> {
    let offset = this.loadOffset();
    let backoff = 1000;
    let webhookCleared = false;
    while (this.running) {
      try {
        // getUpdates fails with 409 while a webhook is set.
        if (!webhookCleared) {
          await this.api!.call('deleteWebhook', { drop_pending_updates: false }, { signal: this.stopping.signal });
          webhookCleared = true;
        }
        const params: Record<string, unknown> = { timeout: this.pollTimeoutSec, limit: 100, allowed_updates: ALLOWED_UPDATES };
        if (offset > 0) params.offset = offset;
        const updates = await this.api!.call<unknown[]>('getUpdates', params, {
          timeoutMs: (this.pollTimeoutSec + 15) * 1000, signal: this.stopping.signal, retries: 0,
        });
        backoff = 1000;
        for (const u of Array.isArray(updates) ? updates : []) {
          if (!this.running) break;
          if (!isUpdate(u)) continue;
          await this.processUpdate(u);
          // Confirmed only after it is recorded; a crash in between means a
          // redelivery, which telegram_updates turns into a no-op.
          offset = Math.max(offset, u.update_id + 1);
          this.saveOffset(offset);
        }
      } catch (e) {
        if (!this.running) break;
        const conflict = e instanceof TelegramError && e.code === 409;
        if (conflict) webhookCleared = false;
        const wait = conflict ? 30_000 : Math.max(backoff, ((e instanceof TelegramError && e.retryAfter) || 0) * 1000);
        log.warn('bot', conflict ? 'getUpdates conflict: another bot instance or a webhook is active' : 'getUpdates failed; retrying', { err: errText(e), retryInMs: wait });
        await this.sleep(wait);
        backoff = Math.min(backoff * 2, 30_000);
      }
    }
  }

  private loadOffset(): number {
    const n = Number(this.db.get<{ value: string }>("SELECT value FROM kv WHERE key = 'tg_offset'")?.value);
    return Number.isSafeInteger(n) && n > 0 ? n : 0;
  }

  private saveOffset(offset: number): void {
    this.db.run("INSERT INTO kv (key, value) VALUES ('tg_offset', ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value", String(offset));
  }

  // Download temp files are removed after every upload; a crash or kill in
  // the middle of one leaves its file behind. Run once at start (when this
  // process has no download of its own in flight); the age check spares a
  // download another instance may be running.
  private async sweepTemp(): Promise<void> {
    const dir = this.tempDir();
    let names: string[];
    try {
      names = await readdir(dir);
    } catch {
      return;   // no temp dir yet
    }
    const cutoff = Date.now() - STALE_TEMP_MS;   // file times are wall-clock: not the injected clock
    let removed = 0;
    for (const name of names) {
      if (!name.startsWith(TEMP_PREFIX) || !name.endsWith('.part')) continue;
      try {
        const st = await stat(join(dir, name));
        if (st.isFile() && st.mtimeMs < cutoff) {
          await rm(join(dir, name), { force: true });
          removed++;
        }
      } catch {
        /* gone already */
      }
    }
    if (removed) log.info('bot', 'removed download temp files left by an earlier run', { files: removed });
  }

  private tempDir(): string {
    return join(this.cfg.dataDir, 'tmp');
  }

  // ------------------------------------------------------------- updates

  async handleWebhook(body: unknown, secretHeader: string | undefined): Promise<number> {
    if (this.cfg.updatesMode !== 'webhook' || !this.cfg.webhookSecret) return 404;
    if (!secretMatches(secretHeader, this.cfg.webhookSecret)) {
      log.warn('bot', 'webhook request with a missing or wrong secret rejected');
      return 401;
    }
    if (!this.running || !this.me) return 503;   // Telegram retries later
    if (!isUpdate(body)) return 400;
    try {
      await this.processUpdate(body);
    } catch (e) {
      log.error('bot', 'could not record a webhook update', { err: errText(e) });
      return 500;
    }
    return 200;
  }

  // Shared by polling and the webhook. Throws only if the update could not be
  // recorded (it will then be redelivered).
  async processUpdate(u: TgUpdate): Promise<'processed' | 'duplicate' | 'invalid'> {
    if (!isUpdate(u)) return 'invalid';
    const now = this.clock.now();
    if (this.db.run('INSERT OR IGNORE INTO telegram_updates (update_id, received_at) VALUES (?, ?)', u.update_id, now).changes === 0) {
      log.debug('bot', 'duplicate update skipped', { update: u.update_id });
      return 'duplicate';
    }
    try {
      if (u.my_chat_member) this.onMyChatMember(u.my_chat_member);
      else if (u.chat_member) this.onChatMember(u.chat_member);
      else if (u.message) this.onMessage(u.message);
      else if (u.callback_query) this.onCallback(u.callback_query);
    } catch (e) {
      log.error('bot', 'update handler failed', { update: u.update_id, kind: updateKind(u), err: errText(e) });
    }
    this.pruneUpdates(false);
    return 'processed';
  }

  // Old update ids are forgotten after a few days: duplicates only happen
  // within Telegram's 24 h retention, and ids restart at a random value after
  // a week without updates.
  private pruneUpdates(force: boolean): void {
    const now = this.clock.now();
    if (!force && now - this.lastPrune < 6 * 3600_000) return;
    this.lastPrune = now;
    this.db.run('DELETE FROM telegram_updates WHERE received_at < ?', now - UPDATE_RETENTION_MS);
  }

  // ------------------------------------------------------ group lifecycle

  private onMyChatMember(cmu: TgChatMemberUpdated): void {
    const chat = cmu.chat;
    if (!isChat(chat) || (chat.type !== 'group' && chat.type !== 'supergroup')) return;
    if (this.superseded(chat.id)) return;
    if (!chatMemberInfo(cmu.new_chat_member).isMember) return this.onBotRemoved(chat);
    this.onBotPresent(chat, cmu.new_chat_member!, chatMemberInfo(cmu.old_chat_member).isMember);
  }

  private onBotPresent(chat: TgChat, member: TgChatMember, wasPresent: boolean): void {
    if (!this.isAllowed(chat.id)) return this.rejectChat(chat, member, wasPresent);
    const existing = this.groups.byChatId(chat.id);
    if (existing?.status === 'disabled') {
      log.info('bot', 'bot is in a group the operator disabled; staying idle', { group: existing.id });
      return;
    }
    const isAdmin = member.status === 'administrator';
    const rights = isAdmin ? adminRights(member) : {};
    const g = this.groups.ensure(chat.id, typeof chat.title === 'string' ? chat.title.slice(0, 128) : '');
    if (g.status !== 'active') this.groups.setStatus(g.id, 'active');
    this.groups.setBotAdmin(g.id, isAdmin, rights);
    const extra = Object.keys(rights).filter((r) => rights[r] && r !== 'is_anonymous' && !NEEDED_RIGHTS.has(r));
    if (extra.length || rights.is_anonymous) log.info('bot', 'bot has admin rights it does not need', { group: g.id, rights: extra, anonymous: !!rights.is_anonymous });

    if (!existing || existing.status === 'bot_removed' || !wasPresent) {
      log.info('bot', existing ? 'bot is back in a group' : 'bot added to a new group', { group: g.id, admin: isAdmin });
      this.track(this.postWelcome(g.id));
    } else if (isAdmin && !existing.botIsAdmin) {
      log.info('bot', 'bot promoted to admin', { group: g.id });
      this.track(this.send(g.id, T.promotedText(), {}, 'admin-status'));
    } else if (!isAdmin && existing.botIsAdmin) {
      log.info('bot', 'bot is no longer an admin', { group: g.id });
      this.track(this.send(g.id, T.demotedText(), {}, 'admin-status'));
    }
  }

  // ALLOWED_CHAT_IDS, when set, limits the bot to those groups (any chat id a
  // registered group ever had counts, so a supergroup upgrade stays allowed).
  private isAllowed(chatId: number): boolean {
    const list = this.cfg.allowedChatIds;
    if (!list.length || list.includes(chatId)) return true;
    const g = this.groups.byChatId(chatId);
    if (!g) return false;
    return this.db.all<{ chat_id: number }>('SELECT chat_id FROM group_chat_ids WHERE group_id = ?', g.id).some((r) => list.includes(Number(r.chat_id)));
  }

  private rejectChat(chat: TgChat, member: TgChatMember, wasPresent: boolean): void {
    if (this.deferredRejects.has(chat.id)) return;
    if (chat.type === 'supergroup') {
      // An allowed basic group that upgrades to a supergroup shows up under a
      // new chat id, and the migration notice may arrive just after this
      // update: look again shortly before leaving.
      this.deferredRejects.add(chat.id);
      this.timer(() => {
        this.deferredRejects.delete(chat.id);
        if (this.isAllowed(chat.id)) this.onBotPresent(chat, member, wasPresent);
        else this.leaveChat(chat);
      }, ALLOWLIST_GRACE_MS);
      return;
    }
    this.leaveChat(chat);
  }

  private leaveChat(chat: TgChat): void {
    const api = this.api;
    if (!api) return;
    log.info('bot', 'leaving a chat that is not in ALLOWED_CHAT_IDS', { chat: chat.id });
    this.track((async () => {
      await api.call('sendMessage', { chat_id: chat.id, text: T.notAllowedText() }).catch((e) => log.warn('bot', 'could not post the goodbye note', { err: errText(e) }));
      await api.call('leaveChat', { chat_id: chat.id });
    })());
  }

  private onBotRemoved(chat: TgChat): void {
    const g = this.groups.byChatId(chat.id);
    if (!g) return;
    if (g.status === 'active') this.groups.setStatus(g.id, 'bot_removed');
    this.groups.setBotAdmin(g.id, false, {});
    const revoked = this.sessions.revokeGroup(g.id);
    this.outbox?.drop(g.id);
    this.clearAnnouncements(g.id);
    log.info('bot', 'bot removed from a group; its arcade is disabled', { group: g.id, sessionsRevoked: revoked });
    this.safeHook('onGroupDisabled', () => this.hooks.onGroupDisabled(g.id));
  }

  // True for a chat id the group has already moved away from (the basic group
  // left behind by a supergroup upgrade). Membership news about that dead
  // chat, including the bot's own, says nothing about the group's current
  // chat and must not disable it, change its admin status or revoke anyone.
  private superseded(chatId: number): boolean {
    const g = this.groups.byChatId(chatId);
    if (!g || g.chatId === chatId) return false;
    log.debug('bot', 'membership update for a chat the group moved away from; ignored', { group: g.id });
    return true;
  }

  private onMigrate(oldChatId: number, newChatId: number): void {
    const r = migrateChat(this.db, this.groups, oldChatId, newChatId);
    if (r) this.afterMigration(r);
  }

  // A duplicate registration of the supergroup was merged into the group:
  // stop whatever belonged to it and fix the lobby card's launch link.
  private afterMigration(r: MigrationResult): void {
    if (r.absorbedGroupId === null) return;
    const dup = r.absorbedGroupId;
    this.outbox?.drop(dup);
    this.clearAnnouncements(dup);
    this.safeHook('onGroupDisabled', () => this.hooks.onGroupDisabled(dup));
    // The card posted for the merged registration carries its room token.
    this.track(this.refreshLobby(r.group.id));
  }

  private onChatMember(cmu: TgChatMemberUpdated): void {
    if (!isChat(cmu.chat) || this.superseded(cmu.chat.id)) return;
    const g = this.groups.byChatId(cmu.chat.id);
    const user = cmu.new_chat_member?.user;
    if (!g || !user || !Number.isSafeInteger(user.id) || user.is_bot) return;
    const info = chatMemberInfo(cmu.new_chat_member);
    if (info.isMember) {
      // Joined, promoted, demoted, restricted: re-verify on next access, and
      // let the room refresh a connected member's role (a demoted admin must
      // not keep moderator powers until they reconnect).
      this.membership.invalidate(g.id, user.id);
      const old = chatMemberInfo(cmu.old_chat_member);
      if (old.isMember && old.status !== info.status && this.hooks.onMemberChanged) {
        this.safeHook('onMemberChanged', () => this.hooks.onMemberChanged!(g.id, user.id));
      }
      return;
    }
    if (this.membership.observe) this.membership.observe(g.id, user.id, info);  // fires onRevoked -> revokeMember
    else {
      this.membership.invalidate(g.id, user.id);
      this.revokeMember(g.id, user.id);
    }
  }

  private revokeMember(groupId: number, userId: number): void {
    const n = this.sessions.revokeUser(groupId, userId);
    log.info('bot', 'membership ended; access revoked', { group: groupId, user: userId, sessions: n });
    this.safeHook('onMembershipRevoked', () => this.hooks.onMembershipRevoked(groupId, userId));
  }

  private activeGroup(chatId: number): Group | null {
    const g = this.groups.byChatId(chatId);
    return g && g.status === 'active' && this.isAllowed(chatId) ? g : null;
  }

  // ------------------------------------------------------------- messages

  private onMessage(m: TgMessage): void {
    if (!m || !isChat(m.chat) || !Number.isSafeInteger(m.message_id)) return;
    const chat = m.chat;
    if (chat.type === 'private') return this.onPrivateMessage(m);
    if (chat.type !== 'group' && chat.type !== 'supergroup') return;
    if (Number.isSafeInteger(m.migrate_to_chat_id)) return this.onMigrate(chat.id, m.migrate_to_chat_id!);
    if (Number.isSafeInteger(m.migrate_from_chat_id)) return this.onMigrate(m.migrate_from_chat_id!, chat.id);
    if (typeof m.new_chat_title === 'string') {
      if (this.groups.byChatId(chat.id)) this.groups.ensure(chat.id, m.new_chat_title.slice(0, 128));
      return;
    }
    const cmd = parseCommand(m, this.username());
    if (cmd) return this.onGroupCommand(m, cmd);
    if (m.document) this.onDocument(m);
  }

  private onPrivateMessage(m: TgMessage): void {
    const api = this.api;
    if (!api || !m.from || m.from.is_bot) return;
    const cmd = parseCommand(m, this.username());
    const wanted = cmd ? cmd.name === 'start' || cmd.name === 'help' : typeof m.text === 'string' || !!m.document;
    if (!wanted || !this.cooldown(`private:${m.chat.id}:${cmd ? 'cmd' : 'msg'}`, cmd ? 3000 : PRIVATE_REPLY_COOLDOWN_MS)) return;
    const user = this.username();
    const params: Record<string, unknown> = { chat_id: m.chat.id, text: T.privateText(), parse_mode: 'HTML', link_preview_options: { is_disabled: true } };
    // Adding with admin=manage_chat asks for admin status and nothing else.
    if (user) params.reply_markup = { inline_keyboard: [[{ text: '➕ Add to a group', url: `https://t.me/${user}?startgroup=arcade&admin=manage_chat` }]] };
    this.track(api.call('sendMessage', params).catch((e) => log.warn('bot', 'private reply failed', { err: errText(e) })));
  }

  private onCallback(q: TgCallbackQuery): void {
    // No callback buttons are posted today; answer so clients stop spinning.
    if (this.api && typeof q?.id === 'string') this.track(this.api.call('answerCallbackQuery', { callback_query_id: q.id }).catch(() => {}));
  }

  private onGroupCommand(m: TgMessage, cmd: Command): void {
    if (!GROUP_COMMAND_NAMES.has(cmd.name)) return;
    const g = this.groups.byChatId(m.chat.id);
    if (!g || g.status === 'bot_removed') {
      // Telegram only delivers messages from chats the bot is in.
      this.track(this.adoptChat(m.chat));
      return;
    }
    if (g.status !== 'active' || !this.isAllowed(m.chat.id)) return;
    const per = cmd.name === 'hostme' || cmd.name === 'add' ? `:${m.from?.id ?? 0}` : '';
    if (!this.cooldown(`cmd:${g.id}:${cmd.name}${per}`, COMMAND_COOLDOWN_MS)) return;
    switch (cmd.name) {
      case 'arcade':
        this.track(this.postLobby(g.id, m.message_id));
        break;
      case 'shelf':
        this.track(this.postShelf(g.id, m));
        break;
      case 'add':
        this.addReplied(g, m);
        break;
      case 'hostme':
        this.track(this.hostMe(g.id, m));
        break;
      default:
        this.track(this.send(g.id, T.helpText(this.limits()), { reply_markup: this.keyboard(g) }, 'help'));
    }
  }

  // A command from a group with no record, or one marked as having removed
  // the bot: the bot was added (back) while we were offline for longer than
  // Telegram keeps updates. Ask Telegram about our own membership and
  // register or reactivate the group if we belong there.
  private async adoptChat(chat: TgChat): Promise<void> {
    if (!this.api || !this.me || !this.cooldown(`adopt:${chat.id}`, 60_000)) return;
    const member = await this.api.call<TgChatMember>('getChatMember', { chat_id: chat.id, user_id: this.me.id });
    const now = this.groups.byChatId(chat.id);
    if (chatMemberInfo(member).isMember && (!now || now.status === 'bot_removed')) this.onBotPresent(chat, member, false);
  }

  private async postWelcome(groupId: number): Promise<void> {
    const g = this.groups.byId(groupId);
    if (!g) return;
    const r = await this.send(groupId, T.welcomeText({ title: g.title, isAdmin: g.botIsAdmin, ...this.limits() }), { reply_markup: this.keyboard(g) }, 'welcome');
    // The welcome card doubles as the lobby card (it has the launch button).
    if (r.ok && Number.isSafeInteger(r.result?.message_id)) this.groups.setLobbyMessage(groupId, r.result.message_id);
  }

  // /arcade: edit the lobby card in place when it is still near the bottom of
  // the chat, otherwise post a fresh one.
  private async postLobby(groupId: number, nearMessageId: number | null): Promise<void> {
    const g = this.groups.byId(groupId);
    if (!g || g.status !== 'active') return;
    const text = this.lobbyText(g);
    const lobby = g.lobbyMessageId;
    if (lobby !== null && nearMessageId !== null && nearMessageId > lobby && nearMessageId - lobby <= LOBBY_EDIT_WINDOW) {
      const r = await this.send(groupId, text, { message_id: lobby, reply_markup: this.keyboard(g) }, 'lobby-edit', 'editMessageText');
      if (r.ok) return;
    }
    const r = await this.send(groupId, text, { reply_markup: this.keyboard(g) }, 'lobby');
    if (r.ok && Number.isSafeInteger(r.result?.message_id)) this.groups.setLobbyMessage(groupId, r.result.message_id);
  }

  // Updates the existing lobby card (e.g. after a game change); never posts.
  async refreshLobby(groupId: number): Promise<void> {
    const g = this.groups.byId(groupId);
    if (!g || g.status !== 'active' || g.lobbyMessageId === null) return;
    await this.send(groupId, this.lobbyText(g), { message_id: g.lobbyMessageId, reply_markup: this.keyboard(g) }, 'lobby-edit', 'editMessageText');
  }

  private lobbyText(g: Group): string {
    let games = 0;
    let summary: string | null = null;
    try {
      games = this.shelf.usage(g.id).games;
    } catch (e) {
      log.warn('bot', 'shelf usage unavailable for the lobby card', { group: g.id, err: errText(e) });
    }
    try {
      summary = this.hooks.roomSummary?.(g.id) ?? null;
    } catch (e) {
      log.warn('bot', 'room summary unavailable for the lobby card', { group: g.id, err: errText(e) });
    }
    return T.lobbyText({ title: g.title, games, summary });
  }

  private async postShelf(groupId: number, m: TgMessage): Promise<void> {
    const g = this.groups.byId(groupId);
    if (!g) return;
    const text = T.shelfText(this.shelf.list(groupId, m.from?.id ?? 0), this.shelf.usage(groupId));
    await this.send(groupId, text, { reply_markup: this.keyboard(g) }, 'shelf');
  }

  // /hostme: a Telegram admin or the owner (verified with getChatMember now)
  // becomes the room host when none is set.
  private async hostMe(groupId: number, m: TgMessage): Promise<void> {
    const replyTo = { reply_parameters: { message_id: m.message_id, allow_sending_without_reply: true } };
    const say = (text: string) => this.send(groupId, T.esc(text), replyTo);
    if (m.sender_chat || !m.from || m.from.is_bot) {
      await say('Anonymous admins can\'t claim the host role: send /hostme from your own account.');
      return;
    }
    const userId = m.from.id;
    const info = await this.membership.check(groupId, userId, { force: true });
    if (!info.isMember || (info.tgRole !== 'creator' && info.tgRole !== 'administrator')) {
      await say('Only group admins can use /hostme. Ask the host or a deputy in the arcade instead.');
      return;
    }
    this.rememberUser(m.from);
    let res: { ok: boolean; message: string };
    try {
      res = await this.hooks.claimHost(groupId, userId);
    } catch (e) {
      log.error('bot', 'claimHost failed', { group: groupId, err: errText(e) });
      res = { ok: false, message: 'Could not change the host right now. Please try again.' };
    }
    log.info('bot', 'host claim via /hostme', { group: groupId, user: userId, ok: !!res?.ok });
    await say(`${res?.ok ? '👑' : '⚠️'} ${String(res?.message ?? '').slice(0, 300)}`);
  }

  // -------------------------------------------------------------- uploads

  // /add as a reply to an earlier game file. Telegram attaches the replied-to
  // message, document included, to the command, which picks up files posted
  // while the bot could not see them (not yet an admin, or offline for longer
  // than Telegram keeps updates).
  private addReplied(g: Group, m: TgMessage): void {
    const orig = m.reply_to_message;
    const ext = extension(typeof orig?.document?.file_name === 'string' ? orig.document.file_name : '');
    if (!orig || !orig.document || !Number.isSafeInteger(orig.message_id) || !(ACCEPTED_EXTENSIONS.has(ext) || UNSUPPORTED_ARCHIVES.has(ext))) {
      const hint = `Reply to a game file (${[...ACCEPTED_EXTENSIONS].map((e) => `.${e}`).join(' or ')}) with /add to put it on the shelf.`;
      this.track(this.send(g.id, T.esc(hint), { reply_parameters: { message_id: m.message_id, allow_sending_without_reply: true } }));
      return;
    }
    // Credit whoever posted the file when Telegram says who that was.
    const poster = orig.from && !orig.from.is_bot && !orig.sender_chat ? orig.from : null;
    this.onDocument({ ...orig, chat: m.chat, from: poster ?? m.from, sender_chat: poster ? undefined : m.sender_chat });
  }

  private onDocument(m: TgMessage): void {
    const g = this.activeGroup(m.chat.id);
    const doc = m.document;
    if (!g || !doc || typeof doc.file_id !== 'string' || typeof doc.file_unique_id !== 'string') return;
    const fileName = typeof doc.file_name === 'string' ? doc.file_name : '';
    const ext = extension(fileName);
    const archive = UNSUPPORTED_ARCHIVES.has(ext);
    if (!archive && !ACCEPTED_EXTENSIONS.has(ext)) return;   // not a game file: stay quiet
    const notice = (html: string) => this.addCard(g.id, { fileName, messageId: m.message_id, notice: html });
    if (m.sender_chat || !m.from) return notice(T.anonymousUploadText(fileName));
    if (m.from.is_bot) return;
    if (archive) return notice(T.unsupportedArchiveText(fileName));
    const size = typeof doc.file_size === 'number' && doc.file_size >= 0 ? doc.file_size : null;
    if (size !== null && size > this.cfg.maxUploadBytes) return notice(T.tooBigForArcadeText(fileName, size, this.cfg.maxUploadBytes));
    if (!this.cfg.botApiLocal && size !== null && size > CLOUD_DOWNLOAD_LIMIT) return notice(T.cloudLimitText(fileName, size));
    const key = `${g.id}:${doc.file_unique_id}`;
    if (this.uploadsInFlight.has(key)) return;
    const existing = this.alreadyOnShelf(g.id, doc.file_unique_id, m.from.id);
    if (existing) return this.addCard(g.id, { fileName, messageId: m.message_id, existing });
    if (this.uploadQueue.length >= MAX_QUEUED_UPLOADS || (this.uploadsPending.get(g.id) ?? 0) >= MAX_QUEUED_UPLOADS_PER_GROUP) return notice(T.busyText(fileName));
    this.rememberUser(m.from);
    this.uploadsInFlight.add(key);
    this.uploadQueue.push({ groupId: g.id, userId: m.from.id, fileName, fileId: doc.file_id, fileUniqueId: doc.file_unique_id, messageId: m.message_id });
    this.uploadsPending.set(g.id, (this.uploadsPending.get(g.id) ?? 0) + 1);
    this.pumpUploads();
  }

  // The same Telegram file (forwarded or re-posted) already made it to this
  // group's shelf: no need to download it again.
  private alreadyOnShelf(groupId: number, fileUniqueId: string, viewerId: number): ShelfGame | null {
    try {
      const r = this.db.get<{ game_id: number }>(
        "SELECT game_id FROM uploads WHERE group_id = ? AND tg_file_unique_id = ? AND status = 'done' AND game_id IS NOT NULL ORDER BY id DESC LIMIT 1",
        groupId, fileUniqueId,
      );
      if (!r) return null;
      const game = this.shelf.get(groupId, r.game_id, viewerId);
      return game && game.status !== 'removed' ? game : null;
    } catch (e) {
      log.warn('bot', 'upload history lookup failed', { group: groupId, err: errText(e) });
      return null;
    }
  }

  private pumpUploads(): void {
    while (this.running && this.uploadsRunning < UPLOAD_CONCURRENCY && this.uploadQueue.length) {
      const job = this.uploadQueue.shift()!;
      this.uploadsRunning++;
      this.track(this.runUpload(job).finally(() => {
        this.uploadsRunning--;
        this.uploadsInFlight.delete(`${job.groupId}:${job.fileUniqueId}`);
        this.pumpUploads();
      }));
    }
  }

  private async runUpload(job: UploadJob): Promise<void> {
    const item: T.CardItem = { fileName: job.fileName, messageId: job.messageId };
    const tmpDir = this.tempDir();
    const tempPath = join(tmpDir, `${TEMP_PREFIX}${randomBytes(12).toString('hex')}.part`);
    try {
      // The bot may have been removed (or the group disabled) while this job
      // waited: nothing is fetched or stored for a group that is not active.
      const g = this.groups.byId(job.groupId);
      if (!g || g.status !== 'active' || !this.isAllowed(g.chatId)) {
        log.info('bot', 'queued upload dropped: group no longer active', { group: job.groupId });
        return;
      }
      await mkdir(tmpDir, { recursive: true, mode: 0o700 });
      const { size } = await this.api!.download(job.fileId, tempPath, this.cfg.maxUploadBytes, { signal: this.stopping.signal });
      // Shutting down (the file can be added again with /add), or the bot was
      // removed during the download: nothing is stored.
      if (!this.running || this.groups.byId(job.groupId)?.status !== 'active') return;
      item.result = await this.shelf.ingest({
        groupId: job.groupId, userId: job.userId, fileName: job.fileName, source: 'telegram', tempPath, size,
        tg: { fileId: job.fileId, fileUniqueId: job.fileUniqueId, messageId: job.messageId },
      });
      log.info('bot', 'telegram upload processed', { group: job.groupId, upload: item.result.uploadId, status: item.result.status, duplicate: !!item.result.duplicate });
    } catch (e) {
      item.failure = isFileTooBig(e) ? 'too_big' : e instanceof TelegramError || e instanceof DownloadError ? 'download' : 'internal';
      log.warn('bot', 'telegram upload failed', { group: job.groupId, reason: item.failure, err: errText(e) });
    } finally {
      await rm(tempPath, { force: true }).catch(() => {});   // ingest moves or deletes it; this covers failures
      const left = (this.uploadsPending.get(job.groupId) ?? 1) - 1;
      if (left > 0) this.uploadsPending.set(job.groupId, left);
      else this.uploadsPending.delete(job.groupId);
    }
    if (this.running) this.addCard(job.groupId, item);
  }

  // Upload outcomes are posted as one card, or one summary for several that
  // finish together, after the group's queue drains (or a minute at most).
  // The debounce restarts with every new outcome but never pushes the card
  // past CARD_MAX_WAIT_MS, and only MAX_CARD_ITEMS outcomes are kept, so a
  // stream of posted files can neither postpone the card forever nor grow
  // the pending list without bound.
  private addCard(groupId: number, item: T.CardItem): void {
    const now = this.clock.now();
    let pending = this.cards.get(groupId);
    if (!pending) {
      pending = { items: [], total: 0, since: now };
      this.cards.set(groupId, pending);
    }
    pending.total++;
    if (pending.items.length < MAX_CARD_ITEMS) pending.items.push(item);
    this.cardTimers.get(groupId)?.();
    const busy = (this.uploadsPending.get(groupId) ?? 0) > 0;
    const left = Math.max(0, CARD_MAX_WAIT_MS - (now - pending.since));
    const delay = busy ? left : Math.min(CARD_DEBOUNCE_MS, left);
    this.cardTimers.set(groupId, this.clock.setTimeout(() => {
      this.cardTimers.delete(groupId);
      this.guarded('upload card', () => this.flushCards(groupId));
    }, delay));
  }

  private flushCards(groupId: number): void {
    const pending = this.cards.get(groupId);
    this.cards.delete(groupId);
    const g = this.groups.byId(groupId);
    if (!pending || !pending.items.length || !g || g.status !== 'active') return;
    const { items, total } = pending;
    const single = total === 1;
    const extra: Record<string, unknown> = { reply_markup: this.keyboard(g) };
    if (single) extra.reply_parameters = { message_id: items[0].messageId, allow_sending_without_reply: true };
    this.track(this.send(groupId, single ? T.gameCard(items[0]) : T.cardSummary(items, 10, total), extra));
  }

  private rememberUser(u: TgUser): void {
    this.db.run(
      'INSERT INTO users (id, first_name, last_name, username, updated_at) VALUES (?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET first_name = excluded.first_name, last_name = excluded.last_name, username = excluded.username, updated_at = excluded.updated_at',
      u.id, String(u.first_name ?? '').slice(0, 64), String(u.last_name ?? '').slice(0, 64), typeof u.username === 'string' ? u.username : null, this.clock.now(),
    );
  }

  // -------------------------------------------------------- announcements

  announce(groupId: number, text: string, opts: { key?: string; delayMs?: number } = {}): void {
    if (!this.outbox || !this.running) return;
    const g = this.groups.byId(groupId);
    if (!g || g.status !== 'active') {
      log.debug('bot', 'announcement dropped: group not active', { group: groupId });
      return;
    }
    const body = String(text ?? '').trim().slice(0, 600);
    if (!body) return;
    const now = this.clock.now();
    const key = opts.key ?? `#${++this.announceSeq}`;
    const notBefore = now + Math.min(Math.max(0, Number(opts.delayMs) || 0), MAX_ANNOUNCE_DELAY_MS);
    let st = this.announcements.get(groupId);
    if (!st) {
      st = { pending: [], timer: null };
      this.announcements.set(groupId, st);
    }
    const same = st.pending.find((p) => p.key === key);
    if (same) Object.assign(same, { text: body, notBefore, queuedAt: now });   // latest wins
    else {
      st.pending.push({ key, text: body, notBefore, queuedAt: now });
      if (st.pending.length > MAX_PENDING_ANNOUNCEMENTS) {
        st.pending.shift();
        log.debug('bot', 'announcement backlog full; oldest dropped', { group: groupId });
      }
    }
    this.scheduleAnnouncements(groupId);
  }

  // Sends when the earliest announcement is due and ANNOUNCE_MIN_INTERVAL_SEC
  // has passed since the last one (persisted, so restarts do not burst).
  private scheduleAnnouncements(groupId: number): void {
    const st = this.announcements.get(groupId);
    if (!st) return;
    st.timer?.();
    st.timer = null;
    const now = this.clock.now();
    st.pending = st.pending.filter((p) => now - p.queuedAt < ANNOUNCEMENT_STALE_MS);
    const g = this.groups.byId(groupId);
    if (!st.pending.length || !g || g.status !== 'active') {
      this.announcements.delete(groupId);
      return;
    }
    const at = Math.max(Math.min(...st.pending.map((p) => p.notBefore)), g.lastAnnounceAt + this.cfg.announceMinIntervalSec * 1000);
    if (at > now) {
      st.timer = this.clock.setTimeout(() => {
        st.timer = null;
        this.guarded('announcement', () => this.scheduleAnnouncements(groupId));
      }, at - now);
      return;
    }
    // Everything due goes out together as one message.
    const due = st.pending.filter((p) => p.notBefore <= now).slice(0, ANNOUNCEMENTS_PER_MESSAGE);
    st.pending = st.pending.filter((p) => !due.includes(p));
    this.groups.touchAnnounce(groupId, now);
    // Plain text: announcement texts contain user-provided names and titles.
    this.track(this.outbox!.send(groupId, 'sendMessage', {
      text: due.map((p) => p.text).join('\n'), link_preview_options: { is_disabled: true }, reply_markup: this.keyboard(g),
    }));
    this.scheduleAnnouncements(groupId);
  }

  private clearAnnouncements(groupId: number): void {
    this.announcements.get(groupId)?.timer?.();
    this.announcements.delete(groupId);
  }

  // ---------------------------------------------------------------- misc

  async getChatMember(chatId: number, userId: number): Promise<ChatMemberInfo> {
    if (!this.api) throw new Error('Telegram bot is not configured');
    return chatMemberInfo(await this.api.call('getChatMember', { chat_id: chatId, user_id: userId }));
  }

  launchUrl(roomToken: string): string {
    // startapp: base64url characters, at most 64 (the documented deep-link limit).
    if (!/^[A-Za-z0-9_-]{1,64}$/.test(roomToken)) throw new Error('room token must be 1-64 base64url characters');
    const user = this.username();
    if (!user) return `${this.cfg.publicUrl}/?startapp=${roomToken}`;   // dev without a bot
    return `https://t.me/${user}/${encodeURIComponent(this.cfg.miniAppShortName)}?startapp=${roomToken}`;
  }

  private username(): string | null {
    return this.me?.username ?? (this.cfg.botUsername.replace(/^@/, '') || null);
  }

  private keyboard(g: Group): ReturnType<typeof T.launchKeyboard> {
    return T.launchKeyboard(this.launchUrl(g.roomToken));
  }

  private limits(): T.UploadLimits {
    return { maxUploadBytes: this.cfg.maxUploadBytes, cloudApi: !this.cfg.botApiLocal };
  }

  // HTML message to a group through the outbox.
  private send(groupId: number, html: string, extra: Record<string, unknown> = {}, key?: string, method: 'sendMessage' | 'editMessageText' = 'sendMessage'): Promise<SendOutcome> {
    if (!this.outbox) return Promise.resolve({ ok: false, error: new Error('Telegram bot is not configured') });
    return this.outbox.send(groupId, method, { text: html, parse_mode: 'HTML', link_preview_options: { is_disabled: true }, ...extra }, { key });
  }

  private cooldown(key: string, ms: number): boolean {
    const now = this.clock.now();
    if ((this.cooldowns.get(key) ?? 0) > now) return false;
    if (this.cooldowns.size > 10_000) for (const [k, until] of this.cooldowns) if (until <= now) this.cooldowns.delete(k);
    this.cooldowns.set(key, now + ms);
    return true;
  }

  private safeHook(name: string, fn: () => unknown): void {
    try {
      const r = fn() as Promise<unknown> | undefined;
      if (r && typeof r.then === 'function') r.then(undefined, (e) => log.error('bot', `${name} hook failed`, { err: errText(e) }));
    } catch (e) {
      log.error('bot', `${name} hook failed`, { err: errText(e) });
    }
  }

  private track(p: Promise<unknown>): void {
    const t: Promise<unknown> = p
      .catch((e) => log.error('bot', 'background task failed', { err: errText(e) }))
      .finally(() => this.tasks.delete(t));
    this.tasks.add(t);
  }

  private timer(fn: () => void, ms: number): void {
    const cancel = this.clock.setTimeout(() => {
      this.timers.delete(cancel);
      this.guarded('timer', fn);
    }, ms);
    this.timers.add(cancel);
  }

  // Timer callbacks run outside processUpdate's error handling: an exception
  // there (a database error, say) would be uncaught and end the process,
  // taking every room with it. Log it instead.
  private guarded(what: string, fn: () => void): void {
    try {
      fn();
    } catch (e) {
      log.error('bot', `${what} failed`, { err: errText(e) });
    }
  }

  // Sleep that stop() cuts short.
  private sleep(ms: number): Promise<void> {
    return new Promise((resolve) => {
      const wake = () => {
        this.wakers.delete(wake);
        cancel();
        resolve();
      };
      const cancel = this.clock.setTimeout(wake, ms);
      this.wakers.add(wake);
    });
  }
}

export function createBot(deps: BotDeps): TelegramBot {
  return new TelegramBot(deps);
}
