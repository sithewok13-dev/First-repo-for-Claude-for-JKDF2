// The bot against a fake Bot API: webhook authentication, idempotent update
// processing, group registration and removal, the allowlist, supergroup
// migration, membership revocation, uploads (including Telegram's 20 MB
// cloud limit), commands, announcement and send rate limits, and polling
// offset persistence. No web_app button may ever reach a group.

import { after, test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, utimesSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { AuthSessions } from '../../server/auth/sessions.ts';
import type { Config } from '../../server/config.ts';
import type { Db } from '../../server/db/db.ts';
import { Groups } from '../../server/groups.ts';
import { log } from '../../server/log.ts';
import type { IngestRequest, IngestResult, ShelfGame, ShelfService } from '../../server/shelf/types.ts';
import { TelegramApi } from '../../server/telegram/api.ts';
import { ALLOWED_UPDATES, createBot } from '../../server/telegram/bot.ts';
import { TelegramMembership } from '../../server/telegram/membership.ts';
import { Outbox, assertNoWebAppButtons } from '../../server/telegram/outbox.ts';
import { FAKE_API_BASE, FAKE_TOKEN, FakeClock, FakeTelegram, settle, testConfig, testDb } from '../helpers/fake-telegram.ts';

log.setLevel('error');

const SECRET = 'webhook-secret-0123456789abcdef';
const ADMIN = 500;
const ALICE = 1001;
const BOB = 1002;
const SUPER = { id: -1001111111111, type: 'supergroup', title: 'Friends' };
const BASIC = { id: -222222, type: 'group', title: 'Old school' };

const dirs: string[] = [];
after(() => {
  for (const d of dirs) rmSync(d, { recursive: true, force: true });
});

// ------------------------------------------------------------------ fakes

function game(over: Partial<ShelfGame> = {}): ShelfGame {
  return {
    id: 1, groupId: 1, displayName: 'ATC Versus', fileName: 'atc-versus.nes', kind: 'game', system: 'fceumm',
    setName: null, parentSet: null, biosSet: null, players: 2, mode: 'versus', genre: null, year: null, manufacturer: null,
    status: 'ready', compat: 'untested',
    validation: { checkedAt: 0, identifiedAs: null, findings: [], missing: [], boot: null },
    metadata: {}, uploaderId: ALICE, uploaderName: 'Alice', uploadedAt: 0, lastPlayedAt: null, playCount: 0, favorite: false,
    sizeBytes: 100, sha256: 'x'.repeat(64), adapter: null, ...over,
  };
}

class FakeShelf {
  readonly ingests: { req: IngestRequest; content: Buffer }[] = [];
  readonly games = new Map<number, ShelfGame>();
  result: (req: IngestRequest, id: number) => IngestResult = (req, id) => ({ uploadId: id, status: 'done', game: game({ id, groupId: req.groupId, fileName: req.fileName }) });

  async ingest(req: IngestRequest): Promise<IngestResult> {
    const content = readFileSync(req.tempPath);
    rmSync(req.tempPath);
    this.ingests.push({ req, content });
    const r = this.result(req, this.ingests.length);
    if (r.game) this.games.set(r.game.id, r.game);
    return r;
  }
  list(): ShelfGame[] {
    return [...this.games.values()];
  }
  get(_groupId: number, gameId: number): ShelfGame | null {
    return this.games.get(gameId) ?? null;
  }
  usage(): { bytes: number; quota: number; games: number } {
    return { bytes: 3 * 1024 * 1024, quota: 2 * 1024 ** 3, games: this.games.size };
  }
}

function setup(over: Partial<Config> = {}, opts: { db?: Db; tg?: FakeTelegram; clock?: FakeClock } = {}) {
  const tg = opts.tg ?? new FakeTelegram();
  const db = opts.db ?? testDb();
  const dataDir = mkdtempSync(join(tmpdir(), 'tg-bot-'));
  dirs.push(dataDir);
  const cfg = testConfig({ dataDir, ...over });
  const groups = new Groups(db, cfg.groupQuotaBytes);
  const sessions = new AuthSessions(db, cfg.sessionTtlSec);
  const clock = opts.clock ?? new FakeClock();
  const api = new TelegramApi({ token: FAKE_TOKEN, baseUrl: FAKE_API_BASE, fetch: tg.fetch, maxRetries: 0 });
  const membership = new TelegramMembership({ db, groups, ttlSec: cfg.membershipTtlSec, now: () => clock.now(), getChatMember: (c, u) => api.call('getChatMember', { chat_id: c, user_id: u }) });
  const shelf = new FakeShelf();
  const calls = { disabled: [] as number[], revoked: [] as [number, number][], claims: [] as [number, number][], changed: [] as [number, number][] };
  const hooks = {
    onGroupDisabled: (id: number) => { calls.disabled.push(id); },
    onMembershipRevoked: (g: number, u: number) => { calls.revoked.push([g, u]); },
    onMemberChanged: (g: number, u: number) => { calls.changed.push([g, u]); },
    claimHost: (g: number, u: number) => {
      calls.claims.push([g, u]);
      return { ok: true, message: 'You are now the arcade host for this group.' };
    },
  };
  const bot = createBot({ cfg, db, groups, sessions, shelf: shelf as unknown as ShelfService, membership, hooks, fetch: tg.fetch, clock, pollTimeoutSec: 1 });
  return { tg, db, cfg, groups, sessions, clock, membership, shelf, calls, bot };
}

// Update builders.
let updateId = 10_000;
let messageId = 5000;

function botMember(status: string, extra: Record<string, unknown> = {}) {
  return { status, user: { id: 7_000_000_001, is_bot: true, first_name: 'Arcade', username: 'arcade_test_bot' }, ...extra };
}

function myChatMember(chat: object, oldStatus: string, newStatus: string, extra: Record<string, unknown> = {}) {
  return { update_id: ++updateId, my_chat_member: { chat, from: { id: ADMIN, first_name: 'Admin' }, date: 0, old_chat_member: botMember(oldStatus), new_chat_member: botMember(newStatus, extra) } };
}

function chatMember(chat: object, userId: number, oldStatus: string, newStatus: string) {
  const user = { id: userId, is_bot: false, first_name: `U${userId}` };
  return { update_id: ++updateId, chat_member: { chat, from: { id: ADMIN }, date: 0, old_chat_member: { status: oldStatus, user }, new_chat_member: { status: newStatus, user } } };
}

function message(chat: object, fields: Record<string, unknown>, from: number = ALICE) {
  return { update_id: ++updateId, message: { message_id: ++messageId, chat, from: { id: from, is_bot: false, first_name: from === ALICE ? 'Alice' : 'Bob' }, date: 0, ...fields } };
}

function command(chat: object, text: string, from: number = ALICE, id?: number) {
  const u = message(chat, { text, entities: [{ type: 'bot_command', offset: 0, length: text.split(' ')[0].length }] }, from);
  if (id !== undefined) u.message.message_id = id;
  return u;
}

function document(chat: object, doc: Record<string, unknown>, from: number = ALICE) {
  return message(chat, { document: doc }, from);
}

async function addBot(s: ReturnType<typeof setup>, chat: { id: number }, status = 'administrator') {
  await s.bot.processUpdate(myChatMember(chat, 'left', status, status === 'administrator' ? { can_manage_chat: true, can_be_edited: false, can_delete_messages: false } : {}) as any);
  await s.bot.whenIdle();
  s.clock.advance(5000);
  return s.groups.byChatId(chat.id)!;
}

function lastText(s: ReturnType<typeof setup>, chatId: number): string {
  const m = s.tg.messagesTo(chatId);
  return m[m.length - 1]?.params.text ?? '';
}

// ------------------------------------------------------------------ tests

test('webhook: a missing or wrong secret is rejected; redeliveries are processed once', async () => {
  const s = setup({ updatesMode: 'webhook', webhookSecret: SECRET });
  await s.bot.start();
  await s.bot.whenIdle();
  const set = s.tg.callsOf('setWebhook')[0].params;
  assert.equal(set.url, 'https://arcade.example/telegram/webhook');
  assert.equal(set.secret_token, SECRET);
  assert.deepEqual(set.allowed_updates, ALLOWED_UPDATES);
  assert.ok(set.allowed_updates.includes('chat_member'));
  assert.equal(set.max_connections, 1, 'updates are delivered one at a time, in order');

  const u = myChatMember(SUPER, 'left', 'member');
  assert.equal(await s.bot.handleWebhook(u, undefined), 401);
  assert.equal(await s.bot.handleWebhook(u, ''), 401);
  assert.equal(await s.bot.handleWebhook(u, SECRET.slice(0, -1) + 'x'), 401);
  assert.equal(s.groups.byChatId(SUPER.id), null, 'nothing was processed');

  assert.equal(await s.bot.handleWebhook(u, SECRET), 200);
  assert.equal(await s.bot.handleWebhook(u, SECRET), 200, 'a redelivery is acknowledged');
  await s.bot.whenIdle();
  assert.equal(s.tg.messagesTo(SUPER.id).length, 1, 'but processed only once');
  assert.equal(await s.bot.handleWebhook({ hello: 'world' }, SECRET), 400);

  const polling = setup({ updatesMode: 'off' });
  await polling.bot.start();
  assert.equal(await polling.bot.handleWebhook(u, SECRET), 404, 'no webhook endpoint unless in webhook mode');
  await s.bot.shutdown();
  await polling.bot.shutdown();
});

test('bot added: the group is registered and gets a welcome card with a url launch button', async () => {
  const s = setup();
  await s.bot.start();
  await s.bot.whenIdle();
  const rights = s.tg.callsOf('setMyDefaultAdministratorRights')[0].params.rights;
  assert.equal(rights.can_manage_chat, true);
  assert.deepEqual(Object.entries(rights).filter(([k, v]) => v && k !== 'can_manage_chat'), [], 'no other right is requested');

  assert.equal(await s.bot.processUpdate(myChatMember(SUPER, 'left', 'member') as any), 'processed');
  await s.bot.whenIdle();
  const g = s.groups.byChatId(SUPER.id)!;
  assert.equal(g.status, 'active');
  assert.equal(g.title, 'Friends');
  assert.equal(g.botIsAdmin, false);
  const [welcome] = s.tg.messagesTo(SUPER.id);
  assert.match(welcome.params.text, /admin/);
  assert.match(welcome.params.text, /switch them all off/);
  const button = welcome.params.reply_markup.inline_keyboard[0][0];
  assert.equal(button.url, `https://t.me/arcade_test_bot/arcade?startapp=${g.roomToken}`);
  assert.equal(button.web_app, undefined);
  assert.equal(s.bot.launchUrl(g.roomToken), button.url);
  assert.equal(s.groups.byId(g.id)!.lobbyMessageId, 101, 'the welcome card is the lobby card');

  // Promotion to admin: rights recorded, short confirmation.
  s.clock.advance(5000);
  await s.bot.processUpdate(myChatMember(SUPER, 'member', 'administrator', { can_manage_chat: true, can_delete_messages: false, can_be_edited: false }) as any);
  await s.bot.whenIdle();
  const promoted = s.groups.byId(g.id)!;
  assert.equal(promoted.botIsAdmin, true);
  const stored = JSON.parse(s.db.get<{ bot_rights: string }>('SELECT bot_rights FROM groups WHERE id = ?', g.id)!.bot_rights);
  assert.deepEqual(stored, { can_manage_chat: true, can_delete_messages: false });
  assert.match(lastText(s, SUPER.id), /admin now/);
  assert.equal(s.tg.messagesTo(SUPER.id).length, 2);

  assert.deepEqual(s.tg.groupWebAppViolations(), []);
  assert.throws(() => assertNoWebAppButtons({ inline_keyboard: [[{ text: 'Play', web_app: { url: 'https://x' } }]] }), /web_app/);
  await s.bot.shutdown();
});

test('the same update delivered twice is processed once', async () => {
  const s = setup();
  await s.bot.start();
  const g = await addBot(s, SUPER);
  const token = s.sessions.issue(ALICE, g.id, 'miniapp').token;
  const u = chatMember(SUPER, ALICE, 'member', 'kicked');
  assert.equal(await s.bot.processUpdate(u as any), 'processed');
  assert.equal(await s.bot.processUpdate(u as any), 'duplicate');
  assert.equal(await s.bot.processUpdate({ ...u } as any), 'duplicate', 'identified by update_id');
  assert.deepEqual(s.calls.revoked, [[g.id, ALICE]]);
  assert.equal(s.sessions.verify(token), null);
  assert.equal(await s.bot.processUpdate({ nope: true } as any), 'invalid');
  await s.bot.shutdown();
});

test('allowlist: other groups are politely left; an allowed group that upgrades stays', async () => {
  const s = setup({ allowedChatIds: [BASIC.id] });
  await s.bot.start();

  const STRANGERS = { id: -333333, type: 'group', title: 'Strangers' };
  await s.bot.processUpdate(myChatMember(STRANGERS, 'left', 'member') as any);
  await s.bot.whenIdle();
  assert.equal(s.groups.byChatId(STRANGERS.id), null);
  assert.match(lastText(s, STRANGERS.id), /private/);
  assert.equal(s.tg.callsOf('leaveChat').at(-1)!.params.chat_id, STRANGERS.id);

  // Supergroups get a short grace period (an allowed group may be upgrading).
  const STRANGE_SUPER = { id: -1003333333333, type: 'supergroup', title: 'Strangers 2' };
  await s.bot.processUpdate(myChatMember(STRANGE_SUPER, 'left', 'administrator') as any);
  await s.bot.whenIdle();
  assert.equal(s.tg.callsOf('leaveChat').length, 1);
  s.clock.advance(10_000);
  await s.bot.whenIdle();
  assert.equal(s.tg.callsOf('leaveChat').at(-1)!.params.chat_id, STRANGE_SUPER.id);
  assert.equal(s.groups.byChatId(STRANGE_SUPER.id), null);

  // The allowed basic group is promoted, which upgrades it: the supergroup's
  // my_chat_member arrives before the migration notice.
  const g = await addBot(s, BASIC, 'member');
  const UP = { id: -1002222220000, type: 'supergroup', title: 'Old school' };
  await s.bot.processUpdate(myChatMember(UP, 'left', 'administrator', { can_manage_chat: true }) as any);
  await s.bot.processUpdate(message(UP, { migrate_from_chat_id: BASIC.id }) as any);
  s.clock.advance(10_000);
  await s.bot.whenIdle();
  assert.equal(s.tg.callsOf('leaveChat').filter((c) => c.params.chat_id === UP.id).length, 0, 'did not leave the upgraded group');
  const moved = s.groups.byChatId(UP.id)!;
  assert.equal(moved.id, g.id);
  assert.equal(moved.botIsAdmin, true, 'the deferred promotion was applied');
  assert.deepEqual(s.tg.groupWebAppViolations(), []);
  await s.bot.shutdown();
});

test('bot removed: group disabled, every session revoked, hook called; re-adding reactivates', async () => {
  const s = setup();
  await s.bot.start();
  const g = await addBot(s, SUPER);
  const a = s.sessions.issue(ALICE, g.id, 'miniapp').token;
  const b = s.sessions.issue(BOB, g.id, 'browser').token;
  s.tg.setMember(SUPER.id, ALICE, { status: 'member' });

  await s.bot.processUpdate(myChatMember(SUPER, 'administrator', 'kicked') as any);
  await s.bot.whenIdle();
  const off = s.groups.byId(g.id)!;
  assert.equal(off.status, 'bot_removed');
  assert.equal(off.botIsAdmin, false);
  assert.equal(s.sessions.verify(a), null);
  assert.equal(s.sessions.verify(b), null);
  assert.deepEqual(s.calls.disabled, [g.id]);
  assert.equal((await s.membership.check(g.id, ALICE, { force: true })).isMember, false, 'no access while the bot is out');

  const sent = s.tg.calls.length;
  s.bot.announce(g.id, 'Next up: something');
  s.clock.advance(120_000);
  await s.bot.whenIdle();
  assert.equal(s.tg.calls.length, sent, 'nothing is posted to a disabled group');

  await s.bot.processUpdate(myChatMember(SUPER, 'kicked', 'member') as any);
  await s.bot.whenIdle();
  const back = s.groups.byId(g.id)!;
  assert.equal(back.status, 'active');
  assert.equal(back.roomToken, g.roomToken);
  assert.match(lastText(s, SUPER.id), /Arcade ready/);
  await s.bot.shutdown();
});

test('group -> supergroup migration keeps the internal group id and room token', async () => {
  const s = setup();
  await s.bot.start();
  const g = await addBot(s, BASIC);
  const NEW = { id: -1004444444444, type: 'supergroup', title: 'Old school' };

  await s.bot.processUpdate(message(BASIC, { migrate_to_chat_id: NEW.id }) as any);
  const moved = s.groups.byChatId(NEW.id)!;
  assert.equal(moved.id, g.id);
  assert.equal(moved.roomToken, g.roomToken);
  assert.equal(moved.chatId, NEW.id);
  assert.equal(s.groups.byChatId(BASIC.id)!.id, g.id, 'the old chat id is remembered');
  assert.equal(moved.lobbyMessageId, null, 'a card in the old chat cannot be edited');

  // The second notice (from the new supergroup) changes nothing.
  await s.bot.processUpdate(message(NEW, { migrate_from_chat_id: BASIC.id }) as any);
  assert.equal(s.groups.all().length, 1);
  assert.equal(s.groups.byChatId(NEW.id)!.id, g.id);

  // Messages now go to the new chat, with the same launch link.
  await s.bot.processUpdate(command(NEW, '/arcade') as any);
  await s.bot.whenIdle();
  const card = s.tg.messagesTo(NEW.id).at(-1)!;
  assert.equal(card.params.reply_markup.inline_keyboard[0][0].url, `https://t.me/arcade_test_bot/arcade?startapp=${g.roomToken}`);

  // Race: the new supergroup registers itself before the migration notice.
  const OLD2 = { id: -555555, type: 'group', title: 'Second' };
  const NEW2 = { id: -1005555555555, type: 'supergroup', title: 'Second' };
  const g1 = await addBot(s, OLD2, 'member');
  await s.bot.processUpdate(myChatMember(NEW2, 'left', 'administrator', { can_manage_chat: true }) as any);
  await s.bot.whenIdle();
  const dup = s.groups.byChatId(NEW2.id)!;
  assert.notEqual(dup.id, g1.id);
  s.clock.advance(5000);
  await s.bot.processUpdate(message(OLD2, { migrate_to_chat_id: NEW2.id }) as any);
  await s.bot.whenIdle();
  const merged = s.groups.byChatId(NEW2.id)!;
  assert.equal(merged.id, g1.id, 'folded into the original group');
  assert.equal(merged.roomToken, g1.roomToken);
  assert.equal(merged.botIsAdmin, true);
  assert.equal(s.groups.byId(dup.id), null);
  assert.ok(s.calls.disabled.includes(dup.id));
  const fixed = s.tg.callsOf('editMessageText').at(-1)!;
  assert.equal(fixed.params.chat_id, NEW2.id);
  assert.equal(fixed.params.reply_markup.inline_keyboard[0][0].url, `https://t.me/arcade_test_bot/arcade?startapp=${g1.roomToken}`);
  await s.bot.shutdown();
});

test('chat_member: a member who leaves loses access at once; a rejoin is re-verified', async () => {
  const s = setup();
  await s.bot.start();
  const g = await addBot(s, SUPER);
  s.tg.setMember(SUPER.id, ALICE, { status: 'member' });
  assert.equal((await s.membership.check(g.id, ALICE)).isMember, true);
  const token = s.sessions.issue(ALICE, g.id, 'miniapp').token;
  const lookups = () => s.tg.callsOf('getChatMember').length;

  await s.bot.processUpdate(chatMember(SUPER, ALICE, 'member', 'left') as any);
  assert.equal(s.sessions.verify(token), null);
  assert.deepEqual(s.calls.revoked, [[g.id, ALICE]], 'hook called exactly once');
  const before = lookups();
  assert.equal((await s.membership.check(g.id, ALICE)).isMember, false);
  assert.equal(lookups(), before, 'known without asking Telegram');

  await s.bot.processUpdate(chatMember(SUPER, ALICE, 'left', 'member') as any);
  assert.equal((await s.membership.check(g.id, ALICE)).isMember, true);
  assert.equal(lookups(), before + 1, 'a rejoin is verified with getChatMember');

  // Promotion is not a revocation, but the cached role is refreshed.
  s.tg.setMember(SUPER.id, ALICE, { status: 'administrator' });
  await s.bot.processUpdate(chatMember(SUPER, ALICE, 'member', 'administrator') as any);
  assert.equal((await s.membership.check(g.id, ALICE)).tgRole, 'administrator');
  assert.equal(s.calls.revoked.length, 1);

  // Restricted users stay only while is_member is true.
  await s.bot.processUpdate({ update_id: ++updateId, chat_member: { chat: SUPER, date: 0, old_chat_member: { status: 'member', user: { id: BOB } }, new_chat_member: { status: 'restricted', is_member: false, user: { id: BOB } } } } as any);
  assert.deepEqual(s.calls.revoked[1], [g.id, BOB]);
  await s.bot.shutdown();
});

test('uploads: files Telegram will not let us fetch are explained, never downloaded', async () => {
  const s = setup({ maxUploadBytes: 64 * 1024 * 1024 });
  await s.bot.start();
  await addBot(s, SUPER);
  const post = async (doc: Record<string, unknown>) => {
    const u = document(SUPER, doc);
    await s.bot.processUpdate(u as any);
    s.clock.advance(2000);
    await s.bot.whenIdle();
    s.clock.advance(2000);
    return u.message.message_id;
  };
  const count = () => s.tg.messagesTo(SUPER.id).length;

  let n = count();
  const big = await post({ file_id: 'BIG', file_unique_id: 'u-big', file_name: 'kof98.zip', file_size: 25 * 1024 * 1024 });
  assert.equal(count(), n + 1);
  const reply = s.tg.messagesTo(SUPER.id).at(-1)!.params;
  assert.match(reply.text, /20 MB/);
  assert.match(reply.text, /Upload from device/);
  assert.equal(reply.reply_parameters.message_id, big);
  assert.match(reply.reply_markup.inline_keyboard[0][0].url, /^https:\/\/t\.me\/arcade_test_bot\/arcade\?startapp=/);

  n = count();
  await post({ file_id: 'HUGE', file_unique_id: 'u-huge', file_name: 'set.zip', file_size: 65 * 1024 * 1024 });
  assert.equal(count(), n + 1);
  assert.match(lastText(s, SUPER.id), /accepts files up to 64 MB/);

  n = count();
  await post({ file_id: '7Z', file_unique_id: 'u-7z', file_name: 'game.7z', file_size: 1000 });
  assert.match(lastText(s, SUPER.id), /7z and RAR archives aren't supported/);

  n = count();
  await post({ file_id: 'PDF', file_unique_id: 'u-pdf', file_name: 'manual.pdf', file_size: 1000 });
  assert.equal(count(), n, 'documents that are not game files are ignored');

  assert.equal(s.tg.callsOf('getFile').length, 0, 'nothing was downloaded');
  assert.equal(s.shelf.ingests.length, 0);

  // With a local Bot API server the 20 MB limit does not apply.
  const local = setup({ botApiLocal: true, botFileDir: '/var/lib/telegram-bot-api' });
  await local.bot.start();
  await addBot(local, SUPER);
  await local.bot.processUpdate(document(SUPER, { file_id: 'BIG', file_unique_id: 'u-big', file_name: 'kof98.zip', file_size: 25 * 1024 * 1024 }) as any);
  await local.bot.whenIdle();
  assert.equal(local.tg.callsOf('getFile').length, 1, 'fetched through the local server');
  await s.bot.shutdown();
  await local.bot.shutdown();
});

test('uploads: game files are downloaded, ingested and answered with a game card', async () => {
  const s = setup();
  await s.bot.start();
  const g = await addBot(s, SUPER);
  const rom = new Uint8Array(40_000).map((_, i) => (i * 7) & 0xff);
  s.tg.addFile({ fileId: 'NES1', fileUniqueId: 'u-nes1', path: 'documents/file_1.nes', data: rom });
  const u = document(SUPER, { file_id: 'NES1', file_unique_id: 'u-nes1', file_name: 'atc-versus.nes', file_size: rom.length });
  await s.bot.processUpdate(u as any);
  await s.bot.whenIdle();

  assert.equal(s.shelf.ingests.length, 1);
  const { req, content } = s.shelf.ingests[0];
  assert.deepEqual(new Uint8Array(content), rom);
  assert.equal(req.groupId, g.id);
  assert.equal(req.userId, ALICE);
  assert.equal(req.source, 'telegram');
  assert.equal(req.fileName, 'atc-versus.nes');
  assert.equal(req.size, rom.length);
  assert.deepEqual(req.tg, { fileId: 'NES1', fileUniqueId: 'u-nes1', messageId: u.message.message_id });
  assert.ok(req.tempPath.startsWith(join(s.cfg.dataDir, 'tmp')));
  assert.ok(!existsSync(req.tempPath));
  assert.equal(s.db.get<{ first_name: string }>('SELECT first_name FROM users WHERE id = ?', ALICE)!.first_name, 'Alice');

  s.clock.advance(2000);
  await s.bot.whenIdle();
  const card = s.tg.messagesTo(SUPER.id).at(-1)!.params;
  assert.match(card.text, /ATC Versus/);
  assert.match(card.text, /NES \/ Famicom · 2 players · versus/);
  assert.match(card.text, /Ready to play/);
  assert.equal(card.reply_parameters.message_id, u.message.message_id);
  assert.equal(card.reply_markup.inline_keyboard[0][0].url, s.bot.launchUrl(g.roomToken));

  // Several uploads finishing together share one message; dependencies are named.
  s.shelf.result = (r, id) => ({
    uploadId: id, status: 'done',
    game: game({ id, groupId: r.groupId, displayName: `Game ${id}`, system: 'fbneo_neogeo', status: 'needs_dependency',
      validation: { checkedAt: 0, identifiedAs: null, findings: [], missing: [{ kind: 'bios', set: 'neogeo', files: [] }], boot: null } }),
  });
  for (const id of ['A', 'B']) s.tg.addFile({ fileId: id, fileUniqueId: `u-${id}`, path: `documents/${id}.zip`, data: rom.slice(0, 100) });
  const before = s.tg.messagesTo(SUPER.id).length;
  await s.bot.processUpdate(document(SUPER, { file_id: 'A', file_unique_id: 'u-A', file_name: 'a.zip', file_size: 100 }) as any);
  await s.bot.processUpdate(document(SUPER, { file_id: 'B', file_unique_id: 'u-B', file_name: 'b.zip', file_size: 100 }, BOB) as any);
  await s.bot.whenIdle();
  s.clock.advance(2000);
  await s.bot.whenIdle();
  assert.equal(s.tg.messagesTo(SUPER.id).length, before + 1);
  const summary = lastText(s, SUPER.id);
  assert.match(summary, /2 uploads processed/);
  assert.match(summary, /Missing BIOS neogeo/);

  // The same Telegram file again: already on the shelf, not downloaded again.
  s.db.run("INSERT INTO uploads (group_id, user_id, source, file_name, tg_file_unique_id, status, game_id, created_at, updated_at) VALUES (?, ?, 'telegram', 'x', 'u-nes1', 'done', 1, 0, 0)", g.id, ALICE);
  const fetches = s.tg.callsOf('getFile').length;
  await s.bot.processUpdate(document(SUPER, { file_id: 'NES1-again', file_unique_id: 'u-nes1', file_name: 'atc-versus.nes', file_size: rom.length }, BOB) as any);
  s.clock.advance(2000);
  await s.bot.whenIdle();
  assert.equal(s.tg.callsOf('getFile').length, fetches);
  assert.match(lastText(s, SUPER.id), /already on the shelf/);

  // HTML in file names is escaped.
  s.shelf.result = (_r, id) => ({ uploadId: id, status: 'failed', error: 'Not a recognized game.' });
  s.tg.addFile({ fileId: 'X', fileUniqueId: 'u-x', path: 'documents/x.zip', data: rom.slice(0, 10) });
  await s.bot.processUpdate(document(SUPER, { file_id: 'X', file_unique_id: 'u-x', file_name: '<script>evil.zip', file_size: 10 }) as any);
  await s.bot.whenIdle();
  s.clock.advance(2000);
  await s.bot.whenIdle();
  assert.match(lastText(s, SUPER.id), /<b>&lt;script&gt;evil\.zip<\/b>: Not a recognized game\./);
  assert.deepEqual(s.tg.groupWebAppViolations(), []);
  await s.bot.shutdown();
});

test('/add as a reply picks up a game file posted while the bot could not see it', async () => {
  const s = setup();
  await s.bot.start();
  const g = await addBot(s, SUPER);
  s.tg.addFile({ fileId: 'OLD', fileUniqueId: 'u-old', path: 'documents/old.nes', data: new Uint8Array(1000).fill(3) });
  const original = { message_id: 42, chat: SUPER, from: { id: BOB, is_bot: false, first_name: 'Bob' }, date: 0, document: { file_id: 'OLD', file_unique_id: 'u-old', file_name: 'old.nes', file_size: 1000 } };
  const u = command(SUPER, '/add@arcade_test_bot');
  (u.message as any).reply_to_message = original;
  await s.bot.processUpdate(u as any);
  await s.bot.whenIdle();
  assert.equal(s.shelf.ingests.length, 1);
  assert.equal(s.shelf.ingests[0].req.groupId, g.id);
  assert.equal(s.shelf.ingests[0].req.userId, BOB, 'credited to whoever posted the file');
  assert.equal(s.shelf.ingests[0].req.tg?.messageId, 42);
  s.clock.advance(2000);
  await s.bot.whenIdle();
  assert.equal(s.tg.messagesTo(SUPER.id).at(-1)!.params.reply_parameters.message_id, 42);

  s.clock.advance(11_000);
  await s.bot.processUpdate(command(SUPER, '/add') as any);
  await s.bot.whenIdle();
  assert.match(lastText(s, SUPER.id), /Reply to a game file \(\.zip or \.nes\) with \/add/);
  await s.bot.shutdown();
});

test('announcements: per-group interval, latest wins per key, optional delay, inactive groups dropped', async () => {
  const s = setup({ announceMinIntervalSec: 60 });
  await s.bot.start();
  const g = await addBot(s, SUPER);
  const announcements = () => s.tg.messagesTo(SUPER.id).filter((c) => c.params.parse_mode === undefined).map((c) => c.params.text);

  s.bot.announce(g.id, '🎮 Next up: ATC Versus');
  await s.bot.whenIdle();
  assert.deepEqual(announcements(), ['🎮 Next up: ATC Versus']);
  assert.equal(s.groups.byId(g.id)!.lastAnnounceAt, s.clock.now(), 'persisted');
  const card = s.tg.messagesTo(SUPER.id).at(-1)!.params;
  assert.ok(card.reply_markup.inline_keyboard[0][0].url.includes('startapp='));

  s.bot.announce(g.id, '🗳 Vote open: A', { key: 'vote' });
  s.bot.announce(g.id, '🗳 Vote open: B', { key: 'vote' });
  s.bot.announce(g.id, '🏆 Alice wins', { key: 'result' });
  s.clock.advance(59_000);
  await s.bot.whenIdle();
  assert.equal(announcements().length, 1, 'nothing before the interval has passed');
  s.clock.advance(1000);
  await s.bot.whenIdle();
  assert.deepEqual(announcements().slice(1), ['🗳 Vote open: B\n🏆 Alice wins'], 'latest per key, due ones in one message');

  s.bot.announce(g.id, '🏆 Bob wins', { key: 'result', delayMs: 120_000 });
  s.clock.advance(60_000);
  await s.bot.whenIdle();
  assert.equal(announcements().length, 2, 'held back by its delay');
  s.clock.advance(60_000);
  await s.bot.whenIdle();
  assert.equal(announcements().at(-1), '🏆 Bob wins');

  // A restart does not reset the interval (last_announce_at is persisted).
  await s.bot.shutdown();
  const again = setup({ announceMinIntervalSec: 60 }, { db: s.db, tg: s.tg, clock: s.clock });
  await again.bot.start();
  again.bot.announce(g.id, 'after restart');
  await again.bot.whenIdle();
  assert.equal(announcements().length, 3);
  again.clock.advance(60_000);
  await again.bot.whenIdle();
  assert.equal(announcements().at(-1), 'after restart');

  again.groups.setStatus(g.id, 'bot_removed');
  again.bot.announce(g.id, 'dropped');
  again.clock.advance(600_000);
  await again.bot.whenIdle();
  assert.equal(announcements().at(-1), 'after restart');
  await again.bot.shutdown();
});

test('outbox: at most 10 messages a minute per group; 429 retry_after pauses the group', async () => {
  const tg = new FakeTelegram();
  const db = testDb();
  const groups = new Groups(db, 1 << 30);
  const g = groups.ensure(SUPER.id, 'Friends');
  const other = groups.ensure(BASIC.id, 'Other');
  const clock = new FakeClock();
  const api = new TelegramApi({ token: FAKE_TOKEN, baseUrl: FAKE_API_BASE, fetch: tg.fetch, maxRetries: 0 });
  const ob = new Outbox({ api, groups, clock, onMigrated: () => {} });
  const sentTo = (id: number) => tg.messagesTo(id).length;
  // Paced sends are released one timer at a time: move time in small steps.
  const run = async (ms: number) => {
    for (let t = 0; t < ms; t += 500) {
      clock.advance(Math.min(500, ms - t));
      await settle();
    }
  };

  for (let i = 0; i < 15; i++) void ob.send(g.id, 'sendMessage', { text: `m${i}` });
  void ob.send(other.id, 'sendMessage', { text: 'other group' });
  await settle();
  assert.equal(sentTo(BASIC.id), 1, 'groups do not wait for each other');
  await run(59_000);
  assert.equal(sentTo(SUPER.id), 10);
  await run(2000);
  assert.ok(sentTo(SUPER.id) > 10, 'continues once the minute has passed');

  // Coalescing: queued messages with the same key, latest wins.
  await run(120_000);
  assert.equal(sentTo(SUPER.id), 15);
  const base = sentTo(SUPER.id);
  const first = ob.send(g.id, 'sendMessage', { text: 'now' });
  const a = ob.send(g.id, 'sendMessage', { text: 'status 1' }, { key: 'status' });
  const b = ob.send(g.id, 'sendMessage', { text: 'status 2' }, { key: 'status' });
  await run(5000);
  assert.deepEqual(tg.messagesTo(SUPER.id).slice(base).map((c) => c.params.text), ['now', 'status 2']);
  assert.equal((await a).ok, true);
  assert.deepEqual(await a, await b);
  assert.equal((await first).ok, true);

  // 429: the message is retried after retry_after, not before.
  await run(120_000);
  tg.failNext('sendMessage', 429, 'Too Many Requests: retry after 30', { retry_after: 30 });
  const r = ob.send(g.id, 'sendMessage', { text: 'patient' });
  await settle();
  const attempts = () => tg.callsOf('sendMessage').filter((c) => c.params.text === 'patient').length;
  assert.equal(attempts(), 1);
  await run(29_000);
  assert.equal(attempts(), 1);
  await run(1000);
  assert.equal(attempts(), 2);
  assert.equal((await r).ok, true);
  ob.stop();
});

test('commands: /arcade edits a recent lobby card or posts a new one; /hostme is for verified admins', async () => {
  const s = setup();
  await s.bot.start();
  const g = await addBot(s, SUPER);
  const lobby = s.groups.byId(g.id)!.lobbyMessageId!;

  await s.bot.processUpdate(command(SUPER, '/arcade', ALICE, lobby + 3) as any);
  await s.bot.whenIdle();
  const edit = s.tg.callsOf('editMessageText').at(-1)!.params;
  assert.equal(edit.message_id, lobby, 'edited in place');
  assert.match(edit.text, /Friends arcade/);

  // Within the cooldown a repeat is ignored; later and far below, a new card.
  await s.bot.processUpdate(command(SUPER, '/arcade', ALICE, lobby + 4) as any);
  await s.bot.whenIdle();
  assert.equal(s.tg.callsOf('editMessageText').length, 1);
  s.clock.advance(11_000);
  const sends = s.tg.callsOf('sendMessage').length;
  await s.bot.processUpdate(command(SUPER, '/arcade@arcade_test_bot', ALICE, lobby + 100) as any);
  await s.bot.whenIdle();
  assert.equal(s.tg.callsOf('sendMessage').length, sends + 1);
  assert.notEqual(s.groups.byId(g.id)!.lobbyMessageId, lobby);

  // Commands for other bots are not ours.
  s.clock.advance(11_000);
  const total = s.tg.calls.length;
  await s.bot.processUpdate(command(SUPER, '/arcade@some_other_bot') as any);
  await s.bot.whenIdle();
  assert.equal(s.tg.calls.length, total);

  // /hostme: a plain member is refused, an admin (verified now) is passed on.
  s.tg.setMember(SUPER.id, ALICE, { status: 'administrator' });
  s.tg.setMember(SUPER.id, BOB, { status: 'member' });
  await s.bot.processUpdate(command(SUPER, '/hostme', BOB) as any);
  await s.bot.whenIdle();
  assert.match(lastText(s, SUPER.id), /Only group admins/);
  assert.deepEqual(s.calls.claims, []);
  s.clock.advance(2000);
  await s.bot.processUpdate(command(SUPER, '/hostme', ALICE) as any);
  await s.bot.whenIdle();
  assert.deepEqual(s.calls.claims, [[g.id, ALICE]]);
  assert.match(lastText(s, SUPER.id), /You are now the arcade host/);

  // /shelf lists the group's games.
  s.shelf.games.set(1, game({ groupId: g.id }));
  s.clock.advance(2000);
  await s.bot.processUpdate(command(SUPER, '/shelf') as any);
  await s.bot.whenIdle();
  assert.match(lastText(s, SUPER.id), /Shelf<\/b> · 1 game/);
  assert.match(lastText(s, SUPER.id), /ATC Versus/);

  // Private chat: an explanation and an add-to-group link asking only for admin status.
  await s.bot.processUpdate(command({ id: ALICE, type: 'private' }, '/start') as any);
  await s.bot.whenIdle();
  const intro = s.tg.messagesTo(ALICE).at(-1)!.params;
  assert.match(intro.text, /inside the group/);
  assert.equal(intro.reply_markup.inline_keyboard[0][0].url, 'https://t.me/arcade_test_bot?startgroup=arcade&admin=manage_chat');
  assert.deepEqual(s.tg.groupWebAppViolations(), []);
  await s.bot.shutdown();
});

test('polling: offset is persisted and resumed; getUpdates asks for chat_member updates', async () => {
  const db = testDb();
  const s = setup({ updatesMode: 'polling' }, { db });
  const privateStart = (chat: number) => command({ id: chat, type: 'private' }, '/start', chat);
  const u1 = privateStart(2001);
  const u2 = privateStart(2002);
  const u3 = privateStart(2003);
  s.tg.pushUpdate(u1, u2, u3);
  await s.bot.start();
  const offset = () => db.get<{ value: string }>("SELECT value FROM kv WHERE key = 'tg_offset'")?.value;
  const deadline = Date.now() + 5000;
  while (offset() !== String(u3.update_id + 1) && Date.now() < deadline) await new Promise((r) => setTimeout(r, 10));
  assert.equal(offset(), String(u3.update_id + 1));
  await s.bot.shutdown();

  const order = s.tg.calls.map((c) => c.method).filter((m) => m === 'deleteWebhook' || m === 'getUpdates');
  assert.equal(order[0], 'deleteWebhook', 'a stale webhook is removed before polling');
  const first = s.tg.callsOf('getUpdates')[0].params;
  assert.equal(first.offset, undefined);
  assert.deepEqual(first.allowed_updates, ALLOWED_UPDATES);
  assert.equal(s.tg.messagesTo(2003).length, 1, 'updates were processed');

  // A new process resumes after the last processed update; redelivered
  // updates are skipped by update_id.
  const s2 = setup({ updatesMode: 'polling' }, { db });
  s2.tg.pushUpdate(u3);
  await s2.bot.start();
  const deadline2 = Date.now() + 5000;
  while (!s2.tg.callsOf('getUpdates').length && Date.now() < deadline2) await new Promise((r) => setTimeout(r, 10));
  await s2.bot.shutdown();
  assert.equal(s2.tg.callsOf('getUpdates')[0].params.offset, u3.update_id + 1);
  assert.equal(s2.tg.messagesTo(2003).length, 0);
});

// ------------------------------------------------- review regression tests

test('upload cards: a steady stream of files can neither postpone the card past a minute nor overflow it', async () => {
  const s = setup();
  await s.bot.start();
  await addBot(s, SUPER);
  const before = s.tg.messagesTo(SUPER.id).length;
  // A 7z file every 1.5 s. Each outcome restarts the 2 s debounce, which on
  // its own would hold the card back for as long as the stream lasts.
  for (let i = 0; i < 60; i++) {
    await s.bot.processUpdate(document(SUPER, { file_id: `Z${i}`, file_unique_id: `uz${i}`, file_name: `game-${i}.7z`, file_size: 10 }) as any);
    s.clock.advance(1500);
    await s.bot.whenIdle();
  }
  const during = s.tg.messagesTo(SUPER.id).length - before;
  assert.ok(during >= 1, 'a card went out within CARD_MAX_WAIT_MS while files kept coming');
  s.clock.advance(5000);
  await s.bot.whenIdle();
  const cards = s.tg.messagesTo(SUPER.id).slice(before).map((c) => c.params.text as string);
  const totals = cards.map((t) => Number(/^📥 <b>(\d+) uploads processed/.exec(t)?.[1]));
  assert.ok(totals.every((n) => n > 0), 'every card is a summary');
  assert.equal(totals.reduce((a, b) => a + b, 0), 60, 'every outcome is counted exactly once');
  assert.ok(totals[0] < 60);
  for (const t of cards) {
    assert.ok(t.length <= 4096);
    assert.match(t, /…and \d+ more/);
  }
  await s.bot.shutdown();
});

test('upload cards: long validation texts and names are clipped to fit a Telegram message', async () => {
  const s = setup();
  await s.bot.start();
  await addBot(s, SUPER);
  // '<' becomes '&lt;': the worst case for the escaped length.
  s.shelf.result = (r, id) => ({
    uploadId: id, status: 'done',
    game: game({ id, groupId: r.groupId, displayName: `${'N'.repeat(300)}${id}`, status: 'needs_attention',
      validation: { checkedAt: 0, identifiedAs: null, findings: [{ level: 'warn', code: 'x', message: '<'.repeat(3000) }], missing: [], boot: null } }),
  });
  const post = async (n: number) => {
    for (let i = 0; i < n; i++) {
      const id = `L${s.shelf.ingests.length}-${i}`;
      s.tg.addFile({ fileId: id, fileUniqueId: `u-${id}`, path: `documents/${id}.nes`, data: new Uint8Array(16) });
      await s.bot.processUpdate(document(SUPER, { file_id: id, file_unique_id: `u-${id}`, file_name: `${id}.nes`, file_size: 16 }) as any);
    }
    await s.bot.whenIdle();
    s.clock.advance(3000);
    await s.bot.whenIdle();
  };
  const sent = () => s.tg.messagesTo(SUPER.id).length;

  let n = sent();
  await post(1);
  assert.equal(sent(), n + 1, 'the single card was accepted by Telegram');
  const one = lastText(s, SUPER.id);
  assert.ok(one.length <= 4096);
  assert.match(one, /Needs attention: (&lt;)+…/);

  n = sent();
  await post(12);
  assert.equal(sent(), n + 1, 'the summary was accepted by Telegram');
  const summary = lastText(s, SUPER.id);
  assert.ok(summary.length <= 4096);
  assert.match(summary, /^📥 <b>12 uploads processed/);
  assert.match(summary, /…and \d+ more/);

  // A failed upload's error text is clipped too.
  s.shelf.result = (_r, id) => ({ uploadId: id, status: 'failed', error: '<'.repeat(5000) });
  n = sent();
  await post(1);
  assert.equal(sent(), n + 1);
  assert.ok(lastText(s, SUPER.id).length <= 4096);
  await s.bot.shutdown();
});

test('uploads: nothing is fetched or stored for a group once the bot is removed', async () => {
  const s = setup();
  await s.bot.start();
  const g = await addBot(s, SUPER);
  for (const id of ['Q1', 'Q2', 'Q3']) s.tg.addFile({ fileId: id, fileUniqueId: `u-${id}`, path: `documents/${id}.nes`, data: new Uint8Array(16) });
  const release = s.tg.hold('getFile');
  // Two downloads start (the concurrency limit); the third waits in the queue.
  for (const id of ['Q1', 'Q2', 'Q3']) await s.bot.processUpdate(document(SUPER, { file_id: id, file_unique_id: `u-${id}`, file_name: `${id}.nes`, file_size: 16 }) as any);
  const deadline = Date.now() + 3000;   // the jobs create the temp dir on the real file system first
  while (s.tg.callsOf('getFile').length < 2 && Date.now() < deadline) await new Promise((r) => setTimeout(r, 5));
  await settle();
  assert.equal(s.tg.callsOf('getFile').length, 2);

  await s.bot.processUpdate(myChatMember(SUPER, 'administrator', 'kicked') as any);
  release();
  await s.bot.whenIdle();
  assert.equal(s.groups.byId(g.id)!.status, 'bot_removed');
  assert.equal(s.tg.callsOf('getFile').length, 2, 'the queued upload was dropped, not downloaded');
  assert.equal(s.shelf.ingests.length, 0, 'downloads that finished after the removal are not stored');
  assert.deepEqual(readdirSync(join(s.cfg.dataDir, 'tmp')), [], 'temp files are cleaned up');
  await s.bot.shutdown();
});

test('migration: news about the superseded basic group never disables or revokes the moved group', async () => {
  const s = setup();
  await s.bot.start();
  const g = await addBot(s, BASIC);
  const NEW = { id: -1006666666666, type: 'supergroup', title: 'Old school' };
  await s.bot.processUpdate(message(BASIC, { migrate_to_chat_id: NEW.id }) as any);
  const token = s.sessions.issue(ALICE, g.id, 'miniapp').token;

  // Whatever Telegram reports about the dead basic group afterwards...
  await s.bot.processUpdate(myChatMember(BASIC, 'administrator', 'left') as any);
  await s.bot.processUpdate(chatMember(BASIC, ALICE, 'member', 'left') as any);
  await s.bot.processUpdate(myChatMember(BASIC, 'left', 'member') as any);
  await s.bot.whenIdle();
  // ...the group in its new supergroup is untouched.
  const now = s.groups.byId(g.id)!;
  assert.equal(now.status, 'active');
  assert.equal(now.chatId, NEW.id);
  assert.equal(now.botIsAdmin, true, 'admin status is not overwritten from the old chat');
  assert.deepEqual(s.calls.disabled, []);
  assert.deepEqual(s.calls.revoked, []);
  assert.ok(s.sessions.verify(token), 'sessions stay valid');

  // The same updates for the current chat still apply.
  await s.bot.processUpdate(chatMember(NEW, ALICE, 'member', 'left') as any);
  assert.deepEqual(s.calls.revoked, [[g.id, ALICE]]);
  assert.equal(s.sessions.verify(token), null);
  await s.bot.shutdown();
});

test('migration: the bot reported gone from the old chat, then present in the supergroup, ends up active', async () => {
  const s = setup();
  await s.bot.start();
  const OLD = { id: -777777, type: 'group', title: 'Third' };
  const NEW = { id: -1007777777777, type: 'supergroup', title: 'Third' };
  const g = await addBot(s, OLD, 'member');
  // Updates for different chats are not ordered: the old group's "bot left"
  // and the supergroup's registration both arrive before the notice.
  await s.bot.processUpdate(myChatMember(OLD, 'member', 'left') as any);
  assert.equal(s.groups.byId(g.id)!.status, 'bot_removed');
  await s.bot.processUpdate(myChatMember(NEW, 'left', 'administrator', { can_manage_chat: true }) as any);
  await s.bot.whenIdle();
  s.clock.advance(5000);
  await s.bot.processUpdate(message(OLD, { migrate_to_chat_id: NEW.id }) as any);
  await s.bot.whenIdle();
  const merged = s.groups.byChatId(NEW.id)!;
  assert.equal(merged.id, g.id);
  assert.equal(merged.roomToken, g.roomToken);
  assert.equal(merged.status, 'active', 'presence in the supergroup outranks the old chat');
  assert.equal(merged.botIsAdmin, true);
  // An operator's 'disabled' is never lifted this way.
  s.groups.setStatus(g.id, 'disabled');
  const OLD2 = { id: -888888, type: 'group', title: 'Fourth' };
  const NEW2 = { id: -1008888888888, type: 'supergroup', title: 'Fourth' };
  const g2 = await addBot(s, OLD2, 'member');
  s.groups.setStatus(g2.id, 'disabled');
  await s.bot.processUpdate(myChatMember(NEW2, 'left', 'member') as any);
  await s.bot.processUpdate(message(NEW2, { migrate_from_chat_id: OLD2.id }) as any);
  assert.equal(s.groups.byChatId(NEW2.id)!.status, 'disabled');
  await s.bot.shutdown();
});

test('migration found by a membership check: the merged duplicate is wound up like any other', async () => {
  const s = setup();
  await s.bot.start();
  const OLD = { id: -999991, type: 'group', title: 'Fifth' };
  const NEW = { id: -1009999999991, type: 'supergroup', title: 'Fifth' };
  const g = await addBot(s, OLD, 'member');
  await s.bot.processUpdate(myChatMember(NEW, 'left', 'administrator', { can_manage_chat: true }) as any);
  await s.bot.whenIdle();
  const dup = s.groups.byChatId(NEW.id)!;
  assert.notEqual(dup.id, g.id);
  s.clock.advance(5000);
  // Both migration notices were missed; a check runs into Telegram's error.
  s.tg.failNext('getChatMember', 400, 'Bad Request: group chat was upgraded to a supergroup chat', { migrate_to_chat_id: NEW.id });
  s.tg.setMember(NEW.id, ALICE, { status: 'member' });
  assert.equal((await s.membership.check(g.id, ALICE)).isMember, true);
  await s.bot.whenIdle();
  assert.equal(s.groups.byChatId(NEW.id)!.id, g.id);
  assert.equal(s.groups.byId(dup.id), null);
  assert.deepEqual(s.calls.disabled, [dup.id], 'the duplicate room is stopped');
  const fixed = s.tg.callsOf('editMessageText').at(-1)!;
  assert.equal(fixed.params.chat_id, NEW.id);
  assert.equal(fixed.params.reply_markup.inline_keyboard[0][0].url, s.bot.launchUrl(g.roomToken), 'the card now opens the original room');
  await s.bot.shutdown();
});

test('a command from a group marked as having removed the bot re-checks and reactivates it', async () => {
  const s = setup();
  await s.bot.start();
  const g = await addBot(s, SUPER);
  await s.bot.processUpdate(myChatMember(SUPER, 'administrator', 'kicked') as any);
  await s.bot.whenIdle();
  assert.equal(s.groups.byId(g.id)!.status, 'bot_removed');

  // A stale command (the bot really is gone): nothing changes.
  s.tg.setMember(SUPER.id, s.tg.me.id, botMember('kicked'));
  await s.bot.processUpdate(command(SUPER, '/arcade') as any);
  await s.bot.whenIdle();
  assert.equal(s.groups.byId(g.id)!.status, 'bot_removed');

  // Re-added while the server was offline (that update was never seen).
  s.clock.advance(61_000);
  s.tg.setMember(SUPER.id, s.tg.me.id, botMember('administrator', { can_manage_chat: true }));
  await s.bot.processUpdate(command(SUPER, '/arcade') as any);
  await s.bot.whenIdle();
  const back = s.groups.byId(g.id)!;
  assert.equal(back.status, 'active');
  assert.equal(back.roomToken, g.roomToken);
  assert.match(lastText(s, SUPER.id), /Arcade ready/);
  await s.bot.shutdown();
});

test('chat_member: a role change of a member who stays is passed on so the room can refresh it', async () => {
  const s = setup();
  await s.bot.start();
  const g = await addBot(s, SUPER);
  await s.bot.processUpdate(chatMember(SUPER, ALICE, 'administrator', 'member') as any);
  assert.deepEqual(s.calls.changed, [[g.id, ALICE]], 'a demotion');
  await s.bot.processUpdate(chatMember(SUPER, BOB, 'left', 'member') as any);
  assert.equal(s.calls.changed.length, 1, 'a join is not a role change (access is verified on entry)');
  assert.deepEqual(s.calls.revoked, []);
  await s.bot.shutdown();
});

test('start: download temp files left by a crash are swept; fresh ones and other files are kept', async () => {
  const s = setup();
  const tmp = join(s.cfg.dataDir, 'tmp');
  mkdirSync(tmp, { recursive: true });
  const old = Date.now() / 1000 - 2 * 3600;
  for (const name of ['tg-0123456789abcdef01234567.part', 'tg-new.part', 'upload-abc', 'notes.txt']) writeFileSync(join(tmp, name), 'x');
  utimesSync(join(tmp, 'tg-0123456789abcdef01234567.part'), old, old);
  utimesSync(join(tmp, 'upload-abc'), old, old);
  await s.bot.start();
  await s.bot.whenIdle();
  assert.deepEqual(readdirSync(tmp).sort(), ['notes.txt', 'tg-new.part', 'upload-abc']);
  await s.bot.shutdown();
});

test('shutdown aborts a download in progress instead of waiting for it', async () => {
  const s = setup();
  await s.bot.start();
  await addBot(s, SUPER);
  s.tg.addFile({ fileId: 'SLOW', fileUniqueId: 'u-slow', path: 'documents/slow.nes', data: new Uint8Array(8192), serve: 'hang' });
  await s.bot.processUpdate(document(SUPER, { file_id: 'SLOW', file_unique_id: 'u-slow', file_name: 'slow.nes', file_size: 8192 }) as any);
  const tmp = join(s.cfg.dataDir, 'tmp');
  const deadline = Date.now() + 3000;
  while (!(existsSync(tmp) && readdirSync(tmp).length) && Date.now() < deadline) await new Promise((r) => setTimeout(r, 5));
  assert.equal(readdirSync(tmp).length, 1, 'the transfer is under way');
  // Without the abort, shutdown would wait for the 10-minute download timeout.
  let timer: NodeJS.Timeout | undefined;
  const finished = await Promise.race([
    s.bot.shutdown().then(() => true),
    new Promise<boolean>((r) => { timer = setTimeout(() => r(false), 3000); }),
  ]);
  clearTimeout(timer);
  assert.ok(finished, 'shutdown did not wait for the transfer');
  assert.ok(s.tg.fileBodiesCancelled >= 1);
  assert.deepEqual(readdirSync(tmp), [], 'the partial file is removed');
  assert.equal(s.shelf.ingests.length, 0);
});

test('start: a failed start can be retried; the webhook answers 503 until the bot is up', async () => {
  const s = setup({ updatesMode: 'webhook', webhookSecret: SECRET });
  const u = myChatMember(SUPER, 'left', 'member');
  assert.equal(await s.bot.handleWebhook(u, SECRET), 503, 'Telegram retries later');
  s.tg.failNext('getMe', 401, 'Unauthorized');
  await assert.rejects(s.bot.start(), /401/);
  assert.equal(await s.bot.handleWebhook(u, SECRET), 503);
  await s.bot.start();
  assert.equal(await s.bot.handleWebhook(u, SECRET), 200);
  await s.bot.whenIdle();
  assert.ok(s.groups.byChatId(SUPER.id));
  await s.bot.shutdown();
});

test('/hostme: anonymous admins and members are refused; nobody is passed on without a verified admin status', async () => {
  const s = setup();
  await s.bot.start();
  await addBot(s, SUPER);
  const anon = command(SUPER, '/hostme');
  (anon.message as any).sender_chat = SUPER;
  await s.bot.processUpdate(anon as any);
  await s.bot.whenIdle();
  assert.match(lastText(s, SUPER.id), /Anonymous admins/);
  // Telegram cannot answer: fail closed (no cached positive on record).
  s.clock.advance(11_000);
  s.tg.failNext('getChatMember', 502, 'Bad Gateway');
  await s.bot.processUpdate(command(SUPER, '/hostme', ALICE) as any);
  await s.bot.whenIdle();
  assert.match(lastText(s, SUPER.id), /Only group admins/);
  assert.deepEqual(s.calls.claims, []);
  await s.bot.shutdown();
});

test('outbox: a failing lookup or migration handler neither crashes the process nor strands a message', async () => {
  const tg = new FakeTelegram();
  const db = testDb();
  const groups = new Groups(db, 1 << 30);
  const g = groups.ensure(SUPER.id, 'Friends');
  const clock = new FakeClock();
  const api = new TelegramApi({ token: FAKE_TOKEN, baseUrl: FAKE_API_BASE, fetch: tg.fetch, maxRetries: 0 });
  let failures = 1;
  const flaky = Object.create(groups) as Groups;
  flaky.byId = (id: number) => {
    if (failures-- > 0) throw new Error('database is locked');
    return groups.byId(id);
  };
  const ob = new Outbox({ api, groups: flaky, clock, onMigrated: () => { throw new Error('migration failed'); } });

  // The lookup fails once: the message stays queued and goes out on the retry.
  const first = ob.send(g.id, 'sendMessage', { text: 'hello' });
  await settle();
  assert.equal(tg.messagesTo(SUPER.id).length, 0);
  clock.advance(5000);
  await settle();
  assert.deepEqual(tg.messagesTo(SUPER.id).map((c) => c.params.text), ['hello']);
  assert.equal((await first).ok, true);

  // Following a migration throws: the send ends as failed, nothing unhandled.
  clock.advance(5000);
  tg.failNext('sendMessage', 400, 'Bad Request: group chat was upgraded to a supergroup chat', { migrate_to_chat_id: -1009090909090 });
  const second = await ob.send(g.id, 'sendMessage', { text: 'moved?' });
  assert.equal(second.ok, false);
  await ob.whenIdle();
  ob.stop();
});
