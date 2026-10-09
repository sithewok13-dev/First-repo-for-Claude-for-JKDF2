// Vertical slice, end to end, in real headless Chromium against the real
// server: one shared ATC Versus game (our own lawful NES test ROM in the
// FCEUmm WebAssembly core), two independent players, a read-only spectator,
// simultaneous input, consistent state (server-verified hashes), seat
// transfer without restart, stale-input rejection, the room creator
// disconnecting, touch input, and measured latency / spectator delay with
// injected network delay. Writes evidence to test-results/.

import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { chromium, type Browser, type BrowserContext, type Page } from 'playwright-core';
import { startHarness, delayProxy, type Harness } from '../helpers/harness.ts';
import { ROOT } from '../helpers/replica.ts';

const OUT = join(ROOT, 'test-results');
const CHROME = process.env.CHROME_PATH ?? '/opt/pw-browsers/chromium';
let h: Harness;
let browser: Browser;
const report: Record<string, unknown> = { startedAt: new Date().toISOString(), environment: {} };
const contexts: BrowserContext[] = [];

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

async function waitFor<T>(page: Page, fn: string, timeout = 30000): Promise<T> {
  const h = await page.waitForFunction(fn, undefined, { timeout, polling: 50 });
  return (await h.jsonValue()) as T;
}

async function open(userId: number, name: string, opts: { port?: number; touch?: boolean } = {}): Promise<Page> {
  // bypassCSP: our CSP (correctly) forbids eval, which Playwright's string predicates use
  const ctx = await browser.newContext({ viewport: { width: 1100, height: 760 }, hasTouch: !!opts.touch, isMobile: false, bypassCSP: true });
  contexts.push(ctx);
  // Outside Telegram there is no telegram-web-app.js; stub it.
  await ctx.route('https://telegram.org/**', (r) => r.fulfill({ status: 200, contentType: 'text/javascript', body: '' }));
  const page = await ctx.newPage();
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  (page as any).errors = errors;
  const base = `http://127.0.0.1:${opts.port ?? h.port}`;
  await page.goto(`${base}/?dev=1&user=${userId}&name=${encodeURIComponent(name)}&room=${h.roomToken}`);
  await waitFor(page, 'window.__arcade && window.__arcade.net.open');
  return page;
}

const cmd = (page: Page, op: string, args: Record<string, unknown> = {}) =>
  page.evaluate(([o, a]) => (window as any).__arcade.net.cmd(o, a), [op, args] as const) as Promise<{ ok: boolean; error?: string }>;

// A legitimate coin can be refused for a moment while the server's adapter
// catches up with what the browsers already show (a player would tap again).
async function coin(page: Page): Promise<{ ok: boolean; error?: string }> {
  for (let i = 0; ; i++) {
    const r = await cmd(page, 'coin');
    if (r.ok || i >= 30 || !/credit in the machine|Wait a moment/.test(String(r.error ?? ''))) return r;
    await new Promise((res) => setTimeout(res, 100));
  }
}

const ram = (page: Page, addr: number) => page.evaluate((a) => (window as any).__arcade.runner.core?.memory()?.[a] ?? null, addr) as Promise<number | null>;
const frame = (page: Page) => page.evaluate(() => (window as any).__arcade.runner.core?.frame ?? -1) as Promise<number>;
const stats = (page: Page) => page.evaluate(() => (window as any).__arcade.runner.stats) as Promise<any>;
const you = (page: Page) => page.evaluate(() => (window as any).__arcade.store.state.you) as Promise<any>;

async function tap(page: Page, key: string, holdMs = 70): Promise<void> {
  await page.keyboard.down(key);
  await sleep(holdMs);
  await page.keyboard.up(key);
  await sleep(40);
}

async function running(page: Page): Promise<void> {
  await waitFor(page, "window.__arcade.runner.stats.phase === 'running'", 60000);
  // the one-time "tap to play/watch" overlay (audio unlock, iOS first responder)
  const tap = page.locator('.tap-start');
  if (await tap.count()) await tap.click();
}

// all pages agree on a RAM value once they have caught up
async function agree(pages: Page[], addr: number, expected?: number, timeout = 8000): Promise<number> {
  const t0 = Date.now();
  for (;;) {
    const vals = await Promise.all(pages.map((p) => ram(p, addr)));
    if (vals.every((v) => v === vals[0]) && (expected === undefined || vals[0] === expected)) return vals[0]!;
    if (Date.now() - t0 > timeout) throw new Error(`pages disagree at $${addr.toString(16)}: ${vals.join(',')} (expected ${expected})`);
    await sleep(50);
  }
}

function summarize(xs: number[]): { n: number; median: number | null; p95: number | null; min: number | null; max: number | null } {
  if (!xs.length) return { n: 0, median: null, p95: null, min: null, max: null };
  const s = [...xs].sort((a, b) => a - b);
  return { n: s.length, median: +s[Math.floor(s.length / 2)].toFixed(1), p95: +s[Math.min(s.length - 1, Math.floor(s.length * 0.95))].toFixed(1), min: +s[0].toFixed(1), max: +s[s.length - 1].toFixed(1) };
}

before(async () => {
  mkdirSync(OUT, { recursive: true });
  h = await startHarness({ HOST_GRACE_SECONDS: '3', DISCONNECT_GRACE_SECONDS: '5', SEAT_OFFER_SECONDS: '15' });
  browser = await chromium.launch({ executablePath: CHROME, args: ['--autoplay-policy=no-user-gesture-required'] });
  report.environment = { chromium: browser.version(), node: process.version, cpu: (await import('node:os')).cpus()[0]?.model, cores: (await import('node:os')).cpus().length };
});

after(async () => {
  report.finishedAt = new Date().toISOString();
  writeFileSync(join(OUT, 'slice-report.json'), JSON.stringify(report, null, 2));
  for (const c of contexts) await c.close().catch(() => {});
  await browser?.close();
  await h?.stop();
});

test('vertical slice: shared game, players, spectator, handoff, disconnect, latency', { timeout: 300_000 }, async () => {
  const P1 = 101, P2 = 102, S = 103;
  h.membership.set(h.groupId, P1, 'creator');
  h.membership.set(h.groupId, P2, 'administrator');
  h.membership.set(h.groupId, S, 'member');
  const gameId = await h.addGame('native/testroms/build/atc-versus.nes', P1);

  const p1 = await open(P1, 'Ana');
  const p2 = await open(P2, 'Ben');
  const sp = await open(S, 'Cy', { touch: true });
  const steps: Record<string, unknown> = {};
  report.steps = steps;

  // the room creator claims host; then starts the game
  assert.ok(h.app.rooms.claimHost(h.groupId, P1, true).ok);
  const started = await cmd(p1, 'game.start', { gameId });
  assert.ok(started.ok, started.error);
  await Promise.all([running(p1), running(p2), running(sp)]);
  steps.allRunning = true;

  // 1+2: two independent players take seats in the SAME running game
  assert.ok((await cmd(p1, 'seat.take', { port: 0 })).ok);
  assert.ok((await cmd(p2, 'seat.take', { port: 1 })).ok);
  // a spectator cannot take a seat that is occupied, and cannot send input
  assert.equal((await cmd(sp, 'seat.take', { port: 0 })).ok, false);
  await waitFor(p1, 'window.__arcade.runner.myPort === 0');
  await waitFor(p2, 'window.__arcade.runner.myPort === 1');

  // coins are server-controlled (the player cannot press SELECT directly)
  await tap(p1, 'Backspace'); // mapped to SELECT locally; the server strips it
  await sleep(300);
  assert.equal(await agree([p1, p2, sp], 0x308), 0, 'direct SELECT did not insert a coin');
  assert.ok((await cmd(p1, 'coin')).ok);
  await agree([p1, p2, sp], 0x308, 1);
  const banked = await cmd(p2, 'coin');
  assert.equal(banked.ok, false, 'credit banking refused while a credit is in the machine');
  await tap(p1, 'Enter');
  await agree([p1, p2, sp], 0x310, 1);  // P1 joined
  assert.ok((await coin(p2)).ok);
  await agree([p1, p2, sp], 0x308, 1);
  await tap(p2, 'Enter');
  await agree([p1, p2, sp], 0x305, 1);  // match started
  steps.matchStarted = true;
  await p1.screenshot({ path: join(OUT, 'slice-p1.png') });

  // 4: simultaneous input from both players
  const x1 = (await ram(p1, 0x316))!, x2 = (await ram(p1, 0x31e))!;
  await Promise.all([p1.keyboard.down('ArrowRight'), p2.keyboard.down('ArrowLeft')]);
  await sleep(400);
  await Promise.all([p1.keyboard.up('ArrowRight'), p2.keyboard.up('ArrowLeft')]);
  await sleep(300);
  const nx1 = await agree([p1, p2, sp], 0x316);
  const nx2 = await agree([p1, p2, sp], 0x31e);
  assert.ok(nx1 > x1 && nx2 < x2, `both moved at once (${x1}->${nx1}, ${x2}->${nx2})`);
  steps.simultaneousInput = { p1x: [x1, nx1], p2x: [x2, nx2] };

  // P1 wins two rounds (5 hits each) -> verified match result
  for (let round = 0; round < 2; round++) {
    await agree([p1, p2, sp], 0x305, 1, 15000);
    for (let i = 0; i < 5; i++) await tap(p1, 'KeyX');
    await agree([p1, p2, sp], 0x315, round + 1, 8000);
  }
  await agree([p1, p2, sp], 0x30a, 1, 10000); // WINNER = P1
  await sleep(500);
  const matches = h.app.db.all('SELECT p1_user, p2_user, winner_user, result, verification, counts FROM matches');
  assert.equal(matches.length, 1);
  assert.equal(Number(matches[0].winner_user), P1);
  assert.equal(matches[0].verification, 'verified');
  steps.verifiedMatch = matches[0];
  // nobody waiting: same players stay (rematch indefinitely)
  assert.equal((await you(p2)).seat?.port, 1);

  // consistent shared state: every page passed server hash checks, no resyncs
  await sleep(2500);
  for (const p of [p1, p2, sp]) {
    const s = await stats(p);
    assert.ok(s.hashChecks >= 2, `hash checks ran (${s.hashChecks})`);
    assert.equal(s.desyncs, 0, 'no desync');
  }
  steps.hashChecks = await Promise.all([p1, p2, sp].map(async (p) => (await stats(p)).hashChecks));

  // 5: seat transfer without restart. Spectator queues, P2 leaves, spectator accepts.
  const frameBefore = await frame(p1);
  const scoreBefore = await agree([p1, p2, sp], 0x312);
  const sessionBefore = await p1.evaluate(() => (window as any).__arcade.store.state.session.sessionId);
  assert.ok((await cmd(sp, 'queue.join')).ok);
  const oldEpoch = (await you(p2)).seat.epoch;
  assert.ok((await cmd(p2, 'seat.leave')).ok);
  await waitFor(sp, 'window.__arcade.store.state.you && window.__arcade.store.state.you.offer');
  assert.ok((await cmd(sp, 'offer.accept')).ok);
  await waitFor(sp, 'window.__arcade.runner.myPort === 1');
  const sessionAfter = await sp.evaluate(() => (window as any).__arcade.store.state.session.sessionId);
  assert.equal(sessionAfter, sessionBefore, 'same session: no restart');
  assert.ok((await frame(p1)) > frameBefore, 'frames kept advancing');
  assert.equal(await agree([p1, p2, sp], 0x312), scoreBefore, "P1's progress untouched");

  // the former owner's delayed input is rejected (stale epoch)
  const rejectedBefore = (h.app.rooms.health() as any).details[0].stats.inputsRejected;
  await p2.evaluate((epoch) => {
    const buf = new ArrayBuffer(14);
    const v = new DataView(buf);
    v.setUint8(0, 1); v.setUint8(1, 1); v.setUint16(2, epoch, true); v.setUint32(4, 999, true); v.setUint16(8, 1 << 8, true); v.setUint32(10, 0, true);
    (window as any).__arcade.net.sendBin(buf);
  }, oldEpoch);
  await sleep(300);
  const hits = await agree([p1, p2, sp], 0x341);
  assert.equal(hits, 0, 'stale input from the former player had no effect');
  const rejectedAfter = (h.app.rooms.health() as any).details[0].stats.inputsRejected;
  steps.staleInput = { rejectedBefore, rejectedAfter, note: 'rejected at the room gate (not the seat owner) before reaching the session' };

  // the new player joins the running game in slot 2 (coin + START), then
  // drives the block with TOUCH (two fingers at once)
  await agree([p1, p2, sp], 0x308, 0);
  assert.ok((await coin(sp)).ok);
  await agree([p1, p2, sp], 0x308, 1);
  await tap(sp, 'Enter');
  await agree([p1, p2, sp], 0x318, 1);
  const cdp = await sp.context().newCDPSession(sp);
  const rightArrow = await sp.locator('.tc-right').boundingBox();
  const btn = await sp.locator('.tc-btn').first().boundingBox();
  assert.ok(rightArrow && btn, 'touch controls are shown for the seated player on a touch device');
  const xBefore = await agree([p1, p2, sp], 0x31e);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [
    { x: rightArrow!.x + rightArrow!.width / 2, y: rightArrow!.y + rightArrow!.height / 2, id: 1 },
    { x: btn!.x + btn!.width / 2, y: btn!.y + btn!.height / 2, id: 2 },
  ] });
  await sleep(120);
  const touchMask = await sp.evaluate(() => (window as any).__arcade.input.mask);
  await sleep(300);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  await sleep(300);
  const xAfter = await agree([p1, p2, sp], 0x31e);
  assert.ok(touchMask & (1 << 7), 'multi-touch: right held');
  assert.ok(touchMask & ~(1 << 7), 'multi-touch: a button held at the same time');
  assert.ok(xAfter > xBefore, 'touch moved the block');
  assert.equal(await sp.evaluate(() => (window as any).__arcade.input.mask), 0, 'released after touch end');
  steps.touch = { mask: touchMask, x: [xBefore, xAfter] };
  await sp.screenshot({ path: join(OUT, 'slice-spectator-now-player-touch.png') });

  // 6: the room creator (P1, also the host) disconnects; the session continues
  const f0 = await frame(sp);
  await p1.context().close();
  await sleep(1500);
  const f1 = await frame(sp);
  assert.ok(f1 > f0 + 30, `game kept running without the creator (${f0} -> ${f1})`);
  const seat0 = (await sp.evaluate(() => (window as any).__arcade.store.state.room.seats[0])) as any;
  assert.equal(seat0.state, 'grace', "creator's seat is held during the grace period");
  await sleep(4000); // host grace (3s in this test) -> acting host
  const acting = await sp.evaluate(() => (window as any).__arcade.store.state.room.actingHost);
  assert.equal(acting?.userId, P2, 'present Telegram admin became acting host');
  await sleep(2500); // seat grace (5s) -> seat released
  const seat0b = (await sp.evaluate(() => (window as any).__arcade.store.state.room.seats[0])) as any;
  assert.equal(seat0b.state, 'empty');
  const interrupted = h.app.db.all("SELECT result, counts FROM matches WHERE result = 'interrupted'");
  steps.creatorDisconnect = { framesAdvanced: f1 - f0, actingHost: acting, interruptedMatchRecorded: interrupted.length, counted: interrupted.map((m: any) => m.counts) };

  // 8: measured latency at several injected network delays (player 2 = sp now)
  const latency: any[] = [];
  for (const oneWay of [0, 25, 50]) {
    const proxy = await delayProxy(h.port, oneWay);
    const pl = await open(S, 'Cy', { port: proxy.port });
    await running(pl);
    assert.ok((await cmd(pl, 'seat.control_here')).ok);
    await waitFor(pl, 'window.__arcade.runner.myPort === 1');
    await sleep(1500);
    await pl.evaluate(() => (window as any).__arcade.runner.clearLatencySamples());
    for (let i = 0; i < 40; i++) {
      await tap(pl, i % 2 ? 'ArrowUp' : 'ArrowDown', 40 + Math.random() * 40);
      await sleep(60 + Math.random() * 120);
    }
    await sleep(500);
    const samples = await pl.evaluate(() => (window as any).__arcade.runner.latencySamples());
    const st = await stats(pl);
    latency.push({ oneWayDelayMs: oneWay, inputToRenderedFrameMs: summarize(samples.inputToFrame), inputRoundTripMs: summarize(samples.rtt), bufferTarget: st.target, stalls: st.stalls, desyncs: st.desyncs });
    await pl.context().close();
    proxy.close();
  }
  report.latency = latency;
  report.serverTicks = (h.app.rooms.health() as any).details[0].stats;

  // spectator delay: same frames rendered by a player (sp, direct) and a
  // spectator (p2 is now a spectator) — wall-clock difference per frame.
  await sleep(3000);
  const logs = await Promise.all([sp, p2].map((p) => p.evaluate(() => (window as any).__arcade.runner.renderLog.slice(-600))));
  const playerTimes = new Map<number, number>(logs[0].map(([f, t]: [number, number]) => [f, t]));
  const diffs: number[] = [];
  for (const [f, t] of logs[1] as [number, number][]) {
    const pt = playerTimes.get(f);
    if (pt !== undefined) diffs.push(t - pt);
  }
  report.spectatorDelayVsPlayerMs = { ...summarize(diffs), note: 'Same machine, no injected delay; spectator buffer target 4 frames vs player 1.' };
  const spStats = await stats(p2);
  report.spectatorBehindLive = spStats.behindLiveMs;
  assert.ok(diffs.length > 100, 'enough common frames measured');

  // no client-side errors
  for (const p of [p2, sp]) assert.deepEqual((p as any).errors, []);
  report.pass = true;
});
