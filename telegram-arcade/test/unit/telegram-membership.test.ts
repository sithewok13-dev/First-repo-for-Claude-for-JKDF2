// Membership checks: Telegram status mapping, the TTL cache and `force`,
// failing closed on API errors, revocation on loss of membership, and the
// dev-only allowlist provider.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Groups } from '../../server/groups.ts';
import { log } from '../../server/log.ts';
import { TelegramApi, TelegramError } from '../../server/telegram/api.ts';
import { DevMembership, TelegramMembership, chatMemberInfo, createMembership } from '../../server/telegram/membership.ts';
import { FAKE_API_BASE, FAKE_TOKEN, FakeClock, FakeTelegram, testConfig, testDb } from '../helpers/fake-telegram.ts';

log.setLevel('error');

const CHAT = -1001234567890;
const ALICE = 1001;

function setup(ttlSec = 300) {
  const tg = new FakeTelegram();
  const db = testDb();
  const groups = new Groups(db, 1 << 30);
  const g = groups.ensure(CHAT, 'Friends');
  const clock = new FakeClock();
  const api = new TelegramApi({ token: FAKE_TOKEN, baseUrl: FAKE_API_BASE, fetch: tg.fetch, maxRetries: 0 });
  const m = new TelegramMembership({ db, groups, ttlSec, now: () => clock.now(), getChatMember: (c, u) => api.call('getChatMember', { chat_id: c, user_id: u }) });
  const revoked: [number, number][] = [];
  m.onRevoked((gid, uid) => revoked.push([gid, uid]));
  return { tg, db, groups, g, clock, m, revoked, lookups: () => tg.callsOf('getChatMember').length };
}

test('ChatMember statuses map to membership and Telegram roles', () => {
  assert.deepEqual(chatMemberInfo({ status: 'creator' }), { status: 'creator', isMember: true, tgRole: 'creator' });
  assert.deepEqual(chatMemberInfo({ status: 'administrator' }), { status: 'administrator', isMember: true, tgRole: 'administrator' });
  assert.deepEqual(chatMemberInfo({ status: 'member' }), { status: 'member', isMember: true, tgRole: 'member' });
  assert.deepEqual(chatMemberInfo({ status: 'restricted', is_member: true }), { status: 'restricted', isMember: true, tgRole: 'member' });
  assert.deepEqual(chatMemberInfo({ status: 'restricted', is_member: false }), { status: 'restricted', isMember: false, tgRole: 'member' });
  assert.equal(chatMemberInfo({ status: 'restricted' }).isMember, false, 'is_member must be explicitly true');
  assert.equal(chatMemberInfo({ status: 'left' }).isMember, false);
  assert.equal(chatMemberInfo({ status: 'kicked' }).isMember, false);
  assert.deepEqual(chatMemberInfo({ status: 'owner?' }), { status: 'unknown', isMember: false, tgRole: 'member' });
  assert.equal(chatMemberInfo(null).isMember, false);
});

test('results are cached for the TTL; force bypasses the cache', async () => {
  const s = setup(300);
  s.tg.setMember(CHAT, ALICE, { status: 'administrator' });
  assert.deepEqual(await s.m.check(s.g.id, ALICE), { status: 'administrator', isMember: true, tgRole: 'administrator' });
  assert.equal(s.lookups(), 1);
  s.clock.advance(299_000);
  assert.equal((await s.m.check(s.g.id, ALICE)).isMember, true);
  assert.equal(s.lookups(), 1, 'served from the cache within the TTL');
  s.clock.advance(2000);
  await s.m.check(s.g.id, ALICE);
  assert.equal(s.lookups(), 2, 'expired entries are re-checked');
  await s.m.check(s.g.id, ALICE, { force: true });
  assert.equal(s.lookups(), 3, 'force always asks Telegram');
  s.m.invalidate(s.g.id, ALICE);
  await s.m.check(s.g.id, ALICE);
  assert.equal(s.lookups(), 4, 'invalidate drops the cached answer');
});

test('API errors fail closed unless a positive answer within the TTL is on record', async () => {
  const s = setup(300);
  // Nothing on record: denied.
  s.tg.failNext('getChatMember', 502, 'Bad Gateway');
  assert.deepEqual(await s.m.check(s.g.id, ALICE), { status: 'unknown', isMember: false, tgRole: 'member' });

  // Recent positive answer: a forced re-check that fails keeps it.
  s.tg.setMember(CHAT, ALICE, { status: 'member' });
  assert.equal((await s.m.check(s.g.id, ALICE)).isMember, true);
  s.clock.advance(60_000);
  s.tg.failNext('getChatMember', 500, 'Internal Server Error');
  assert.equal((await s.m.check(s.g.id, ALICE, { force: true })).isMember, true);

  // Positive answer older than the TTL: denied, and nothing is revoked (an
  // outage is not evidence that the user left).
  s.clock.advance(300_000);
  s.tg.failNext('getChatMember', 429, 'Too Many Requests: retry after 5', { retry_after: 5 });
  assert.equal((await s.m.check(s.g.id, ALICE)).isMember, false);
  assert.deepEqual(s.revoked, []);
  // The failure was not cached: the next check asks again and succeeds.
  assert.equal((await s.m.check(s.g.id, ALICE)).isMember, true);

  // A positive answer that was invalidated is not a fallback either.
  s.m.invalidate(s.g.id, ALICE);
  s.tg.failNext('getChatMember', 502, 'Bad Gateway');
  assert.equal((await s.m.check(s.g.id, ALICE)).isMember, false);
});

test('losing membership is detected by a check and reported once', async () => {
  const s = setup(300);
  s.tg.setMember(CHAT, ALICE, { status: 'member' });
  assert.equal((await s.m.check(s.g.id, ALICE)).isMember, true);
  s.tg.setMember(CHAT, ALICE, { status: 'kicked', until_date: 0 });
  const after = await s.m.check(s.g.id, ALICE, { force: true });
  assert.deepEqual(after, { status: 'kicked', isMember: false, tgRole: 'member' });
  assert.deepEqual(s.revoked, [[s.g.id, ALICE]]);
  await s.m.check(s.g.id, ALICE, { force: true });
  assert.equal(s.revoked.length, 1, 'only the transition is reported');

  // "user not found" is a definite answer: not a member.
  const BOB = 1002;
  s.tg.setMember(CHAT, BOB, { status: 'member' });
  await s.m.check(s.g.id, BOB);
  s.tg.members.delete(`${CHAT}:${BOB}`);
  assert.equal((await s.m.check(s.g.id, BOB, { force: true })).isMember, false);
  assert.deepEqual(s.revoked[1], [s.g.id, BOB]);
});

test('observe(): a chat_member loss applies at once; a gain only invalidates', async () => {
  const s = setup(300);
  s.tg.setMember(CHAT, ALICE, { status: 'member' });
  await s.m.check(s.g.id, ALICE);
  s.m.observe(s.g.id, ALICE, { status: 'left', isMember: false, tgRole: 'member' });
  assert.deepEqual(s.revoked, [[s.g.id, ALICE]]);
  assert.equal((await s.m.check(s.g.id, ALICE)).isMember, false, 'cached as not a member');
  assert.equal(s.lookups(), 1, 'no API call needed');

  s.m.observe(s.g.id, ALICE, { status: 'member', isMember: true, tgRole: 'member' });
  assert.equal((await s.m.check(s.g.id, ALICE)).isMember, true);
  assert.equal(s.lookups(), 2, 'a gain is re-verified with Telegram');
});

test('no access in groups the bot was removed from; no API call is made', async () => {
  const s = setup();
  s.tg.setMember(CHAT, ALICE, { status: 'creator' });
  s.groups.setStatus(s.g.id, 'bot_removed');
  assert.equal((await s.m.check(s.g.id, ALICE, { force: true })).isMember, false);
  assert.equal((await s.m.check(9999, ALICE)).isMember, false);
  assert.equal((await s.m.check(s.g.id, -5)).isMember, false);
  assert.equal(s.lookups(), 0);
});

test('concurrent checks share one request; an answer invalidated in flight is not trusted', async () => {
  const db = testDb();
  const groups = new Groups(db, 1 << 30);
  const g = groups.ensure(CHAT, 'Friends');
  const pending: { resolve: (v: unknown) => void }[] = [];
  const m = new TelegramMembership({ db, groups, ttlSec: 300, getChatMember: () => new Promise((resolve) => pending.push({ resolve })) });

  const a = m.check(g.id, ALICE);
  const b = m.check(g.id, ALICE, { force: true });
  await new Promise((r) => setImmediate(r));
  assert.equal(pending.length, 1, 'one request for both callers');

  // A chat_member update invalidates while the request is out.
  m.invalidate(g.id, ALICE);
  pending[0].resolve({ status: 'member' });
  await new Promise((r) => setImmediate(r));
  assert.equal(pending.length, 2, 'asked again after the invalidation');
  pending[1].resolve({ status: 'left' });
  assert.equal((await a).isMember, false);
  assert.equal((await b).isMember, false);

  // A new request started after the invalidation is trusted as usual, and
  // the bookkeeping is gone once nothing is in flight (it must not grow with
  // every chat_member update).
  const c = m.check(g.id, ALICE, { force: true });
  await new Promise((r) => setImmediate(r));
  m.invalidate(g.id, ALICE);
  const d = m.check(g.id, ALICE, { force: true });
  await new Promise((r) => setImmediate(r));
  assert.equal(pending.length, 4, 'the invalidation started a fresh request');
  pending[3].resolve({ status: 'member' });
  assert.equal((await d).isMember, true);
  pending[2].resolve({ status: 'left' });   // the older answer arrives late: asked again
  await new Promise((r) => setImmediate(r));
  assert.equal(pending.length, 5);
  pending[4].resolve({ status: 'member' });
  assert.equal((await c).isMember, true);
  for (let i = 0; i < 100; i++) m.invalidate(g.id, 5000 + i);
  const internals = m as unknown as { generation: Map<string, number>; running: Map<string, number> };
  assert.equal(internals.generation.size, 0);
  assert.equal(internals.running.size, 0);
});

test('a "group was upgraded" error moves the group to its new chat id and retries', async () => {
  const s = setup();
  const NEW = -1009876543210;
  s.tg.failNext('getChatMember', 400, 'Bad Request: group chat was upgraded to a supergroup chat', { migrate_to_chat_id: NEW });
  s.tg.setMember(NEW, ALICE, { status: 'member' });
  assert.equal((await s.m.check(s.g.id, ALICE)).isMember, true);
  const moved = s.groups.byId(s.g.id)!;
  assert.equal(moved.chatId, NEW);
  assert.equal(moved.roomToken, s.g.roomToken);
  assert.equal(s.tg.callsOf('getChatMember')[1].params.chat_id, NEW);
});

test('dev allowlist provider and factory selection', async () => {
  const dev = new DevMembership([{ userId: 1, role: 'creator' }, { userId: 2, groupId: 7 }]);
  const revoked: number[] = [];
  dev.onRevoked((_g, u) => revoked.push(u));
  assert.deepEqual(await dev.check(7, 1), { status: 'creator', isMember: true, tgRole: 'creator' });
  assert.equal((await dev.check(7, 2)).isMember, true);
  assert.equal((await dev.check(8, 2)).isMember, false, 'group-scoped entries only apply to that group');
  assert.equal((await dev.check(7, 3)).isMember, false);
  dev.observe(7, 2, { status: 'left', isMember: false, tgRole: 'member' });
  assert.equal((await dev.check(7, 2)).isMember, false);
  assert.deepEqual(revoked, [2]);

  const db = testDb();
  const groups = new Groups(db, 1 << 30);
  assert.ok(createMembership({ cfg: testConfig({ botToken: '', devMode: true }), db, groups, dev: [] }) instanceof DevMembership);
  assert.throws(() => createMembership({ cfg: testConfig({ botToken: '', devMode: false }), db, groups }), /BOT_TOKEN/);
  assert.ok(createMembership({ cfg: testConfig(), db, groups }) instanceof TelegramMembership);
  assert.ok(new TelegramError('x', 400, 'y') instanceof Error);
});
