// Identity and group isolation through the real HTTP/WebSocket server:
// Telegram initData validation, membership-gated sign-in (forwarded links),
// group-bound tokens (no cross-group access to shelf, files, records or the
// room), one-time browser handoff, and live revocation.

import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import WebSocket from 'ws';
import { request } from 'node:http';
import { signInitData } from '../../server/auth/initdata.ts';
import { startHarness, type Harness } from '../helpers/harness.ts';

const BOT_TOKEN = '123456789:TEST-ONLY-not-a-real-bot-token-e2e';
let h: Harness;
let groupB: { id: number; roomToken: string };

before(async () => {
  h = await startHarness({ BOT_TOKEN });
  const g = h.app.groups.ensure(-100777, 'Other Group');
  groupB = { id: g.id, roomToken: g.roomToken };
});
after(async () => { await h?.stop(); });

function initData(userId: number, startParam: string, authDate = Math.floor(Date.now() / 1000), extra: Record<string, string> = {}): string {
  return signInitData({
    auth_date: String(authDate),
    chat_instance: '-555',
    chat_type: 'supergroup',
    start_param: startParam,
    user: JSON.stringify({ id: userId, first_name: `User${userId}` }),
    ...extra,
  }, BOT_TOKEN);
}

async function post(path: string, body: unknown, token?: string): Promise<{ status: number; body: any }> {
  const r = await fetch(h.url + path, { method: 'POST', headers: { 'Content-Type': 'application/json', ...(token ? { Authorization: `Bearer ${token}` } : {}) }, body: JSON.stringify(body) });
  return { status: r.status, body: await r.json().catch(() => null) };
}

async function get(path: string, token: string): Promise<{ status: number; body: any }> {
  const r = await fetch(h.url + path, { headers: { Authorization: `Bearer ${token}` } });
  return { status: r.status, body: await r.json().catch(() => null) };
}

function connect(token: string): Promise<{ ws: WebSocket; msgs: any[]; closed: Promise<number> }> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(h.url.replace('http', 'ws') + '/ws');
    const msgs: any[] = [];
    const closed = new Promise<number>((r) => ws.on('close', (code) => r(code)));
    ws.on('open', () => ws.send(JSON.stringify({ t: 'auth', token })));
    ws.on('message', (d, bin) => {
      if (bin) return;
      const m = JSON.parse(d.toString());
      msgs.push(m);
      if (m.t === 'welcome') resolve({ ws, msgs, closed });
      if (m.t === 'auth_error') resolve({ ws, msgs, closed });
    });
    ws.on('error', reject);
  });
}

test('initData: valid signs in; tampered, expired, unknown room and non-members are refused', async () => {
  h.membership.set(h.groupId, 501, 'member');
  const ok = await post('/api/auth/telegram', { initData: initData(501, h.roomToken) });
  assert.equal(ok.status, 200);
  assert.equal(ok.body.group.id, h.groupId);
  assert.ok(ok.body.token.length > 30);

  const tampered = initData(501, h.roomToken).replace('User501', 'Admin');
  assert.equal((await post('/api/auth/telegram', { initData: tampered })).status, 401);
  const forgedRoom = initData(501, h.roomToken).replace(`start_param=${h.roomToken}`, `start_param=${groupB.roomToken}`);
  assert.equal((await post('/api/auth/telegram', { initData: forgedRoom })).status, 401, 'start_param is covered by the signature');
  const old = initData(501, h.roomToken, Math.floor(Date.now() / 1000) - 7200);
  assert.equal((await post('/api/auth/telegram', { initData: old })).status, 401);
  assert.equal((await post('/api/auth/telegram', { initData: initData(501, 'nosuchroom123') })).status, 404);
  // a forwarded link: valid Telegram identity, but not a member of the group
  const fwd = await post('/api/auth/telegram', { initData: initData(999, h.roomToken) });
  assert.equal(fwd.status, 403);
  assert.match(fwd.body.error, /Only its members/);
  // client-supplied group/user fields are ignored: identity comes from the signature only
  const spoof = await post('/api/auth/telegram', { initData: initData(999, h.roomToken), userId: 501, groupId: h.groupId, room: h.roomToken });
  assert.equal(spoof.status, 403);
});

test('tokens are bound to one group: no access to another group\'s shelf, files, records or room', async () => {
  h.membership.set(h.groupId, 601, 'member');
  h.membership.set(groupB.id, 601, 'member');
  const a = (await post('/api/auth/telegram', { initData: initData(601, h.roomToken) })).body.token;
  const b = (await post('/api/auth/telegram', { initData: initData(601, groupB.roomToken) })).body.token;
  const gameA = await h.addGame('native/testroms/build/atc-solo.nes', 601);
  const shelfA = await get('/api/shelf', a);
  const shelfB = await get('/api/shelf', b);
  assert.ok(shelfA.body.games.some((g: any) => g.id === gameA));
  assert.ok(!shelfB.body.games.some((g: any) => g.id === gameA), "group B cannot see group A's shelf");
  const sha = shelfA.body.games.find((g: any) => g.id === gameA).sha256;
  assert.equal((await fetch(`${h.url}/api/files/${sha}`, { headers: { Authorization: `Bearer ${a}` } })).status, 200);
  assert.equal((await fetch(`${h.url}/api/files/${sha}`, { headers: { Authorization: `Bearer ${b}` } })).status, 404, "group B cannot download group A's ROM");
  assert.equal((await get(`/api/shelf/${gameA}`, b)).status, 404);
  assert.equal((await get(`/api/records?game=${gameA}`, b)).status, 404);
  assert.equal((await fetch(`${h.url}/api/files/${sha}`)).status, 401, 'no anonymous downloads');
  // the WebSocket joins the token's own group, never another
  const c = await connect(b);
  const welcome = c.msgs.find((m) => m.t === 'welcome');
  assert.equal(welcome.groupId, groupB.id);
  c.ws.close();
});

test('membership revoked: live connection is closed and the token stops working', async () => {
  h.membership.set(h.groupId, 701, 'member');
  const t = (await post('/api/auth/telegram', { initData: initData(701, h.roomToken) })).body.token;
  const c = await connect(t);
  assert.ok(c.msgs.some((m) => m.t === 'welcome'));
  h.membership.set(h.groupId, 701, null); // fires the revocation listener
  const code = await c.closed;
  assert.equal(code, 4003);
  assert.equal((await get('/api/shelf', t)).status, 403);
  const again = await connect(t);
  assert.ok(again.msgs.some((m) => m.t === 'auth_error'));
});

test('external-browser handoff: one-time, user-bound, short-lived', async () => {
  h.membership.set(h.groupId, 801, 'member');
  const t = (await post('/api/auth/telegram', { initData: initData(801, h.roomToken) })).body.token;
  const r = await post('/api/auth/handoff', {}, t);
  assert.equal(r.status, 200);
  const handoff = new URL(r.body.url).hash.slice('#handoff='.length);
  const first = await post('/api/auth/redeem', { token: handoff });
  assert.equal(first.status, 200);
  assert.equal(first.body.user.id, 801);
  assert.equal((await post('/api/auth/redeem', { token: handoff })).status, 401, 'second use refused');
  assert.equal((await post('/api/auth/handoff', {})).status, 401, 'needs a signed-in session');
});

test('dev login exists only in DEV_MODE on loopback; production refuses it', async () => {
  h.membership.set(h.groupId, 901, 'member');
  assert.equal((await post('/api/auth/dev', { userId: 901, room: h.roomToken })).status, 200, 'harness runs DEV_MODE on loopback');
  // a second, production-mode server: same request is a plain 404
  const prod = await startHarness({ BOT_TOKEN, DEV_MODE: '0', PUBLIC_URL: 'https://arcade.test' });
  try {
    assert.equal(prod.app.cfg.devMode, false);
    prod.membership.set(prod.groupId, 901, 'member');
    const r = await fetch(prod.url + '/api/auth/dev', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ userId: 901, room: prod.roomToken }) });
    assert.equal(r.status, 404);
  } finally {
    await prod.stop();
  }
});

test('uploads: one in flight per user; a second one is refused until the first finishes', async () => {
  h.membership.set(h.groupId, 1001, 'member');
  const t = (await post('/api/auth/telegram', { initData: initData(1001, h.roomToken) })).body.token;
  const rom = (await import('node:fs')).readFileSync('native/testroms/build/atc-solo.nes');
  // first upload: headers and half the body, then hold
  const port = new URL(h.url).port;
  const first = request({ host: '127.0.0.1', port, path: '/api/shelf/upload', method: 'POST', headers: {
    Authorization: `Bearer ${t}`, 'Content-Type': 'application/octet-stream', 'X-File-Name': 'atc-solo.nes', 'Content-Length': rom.length,
  } });
  const firstDone = new Promise<number>((resolve, reject) => {
    first.on('response', (r) => { r.resume(); resolve(r.statusCode ?? 0); });
    first.on('error', reject);
  });
  first.write(rom.subarray(0, 1000));
  await new Promise((r) => setTimeout(r, 300));
  const second = await fetch(h.url + '/api/shelf/upload', { method: 'POST', headers: {
    Authorization: `Bearer ${t}`, 'Content-Type': 'application/octet-stream', 'X-File-Name': 'atc-solo.nes',
  }, body: rom });
  assert.equal(second.status, 429);
  assert.match((await second.json()).error, /still being checked/);
  first.end(rom.subarray(1000));
  assert.equal(await firstDone, 200);
  // once the first has finished, the next upload is accepted (a duplicate here)
  const third = await fetch(h.url + '/api/shelf/upload', { method: 'POST', headers: {
    Authorization: `Bearer ${t}`, 'Content-Type': 'application/octet-stream', 'X-File-Name': 'atc-solo.nes',
  }, body: rom });
  assert.equal(third.status, 200);
});
