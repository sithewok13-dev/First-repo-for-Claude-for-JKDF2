// Bot API client: errors, bounded 429 retries, timeouts, and file downloads
// with hard size limits (cloud HTTP download and local Bot API server copy,
// which must refuse paths outside BOT_FILE_DIR).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { DownloadError, TelegramApi, TelegramError, isFileTooBig } from '../../server/telegram/api.ts';
import { log } from '../../server/log.ts';
import { FAKE_API_BASE, FAKE_TOKEN, FakeTelegram } from '../helpers/fake-telegram.ts';

log.setLevel('error');

function api(tg: FakeTelegram, over: Partial<ConstructorParameters<typeof TelegramApi>[0]> = {}) {
  const sleeps: number[] = [];
  const a = new TelegramApi({ token: FAKE_TOKEN, baseUrl: FAKE_API_BASE, fetch: tg.fetch, sleep: async (ms) => { sleeps.push(ms); }, ...over });
  return { a, sleeps };
}

function tmp(): string {
  return mkdtempSync(join(tmpdir(), 'tg-api-'));
}

test('successful call returns the result; Bot API errors carry code, description and parameters', async () => {
  const tg = new FakeTelegram();
  const { a } = api(tg);
  const me = await a.call('getMe');
  assert.equal(me.username, 'arcade_test_bot');

  tg.failNext('sendMessage', 400, 'Bad Request: group chat was upgraded to a supergroup chat', { migrate_to_chat_id: -1001234 });
  const err = await a.call('sendMessage', { chat_id: -1234, text: 'x' }).catch((e) => e);
  assert.ok(err instanceof TelegramError);
  assert.equal(err.code, 400);
  assert.equal(err.migrateToChatId, -1001234);
  assert.ok(!String(err.message).includes(FAKE_TOKEN), 'the token never appears in errors');
});

test('429 is retried after retry_after, a bounded number of times', async () => {
  const tg = new FakeTelegram();
  const { a, sleeps } = api(tg, { maxRetries: 2 });
  tg.failNext('sendMessage', 429, 'Too Many Requests: retry after 3', { retry_after: 3 });
  const r = await a.call('sendMessage', { chat_id: 1, text: 'hi' });
  assert.equal(r.message_id, 101);
  assert.deepEqual(sleeps, [3000]);

  tg.failNext('sendMessage', 429, 'Too Many Requests: retry after 1', { retry_after: 1 }, 3);
  const err = await a.call('sendMessage', { chat_id: 1, text: 'hi' }).catch((e) => e);
  assert.ok(err instanceof TelegramError && err.code === 429, 'gives up after maxRetries');
  assert.equal(tg.callsOf('sendMessage').length, 2 + 3);

  // A retry_after longer than the cap fails fast instead of sleeping.
  const before = sleeps.length;
  tg.failNext('sendMessage', 429, 'Too Many Requests: retry after 600', { retry_after: 600 });
  const long = await a.call('sendMessage', { chat_id: 1, text: 'hi' }).catch((e) => e);
  assert.equal(long.retryAfter, 600);
  assert.equal(sleeps.length, before);
});

test('timeouts and network failures become TelegramError code 0 without leaking the URL', async () => {
  const hang = ((_: unknown, init?: RequestInit) => new Promise((_r, reject) => {
    init?.signal?.addEventListener('abort', () => reject(new DOMException('aborted', 'AbortError')));
  })) as typeof fetch;
  const a = new TelegramApi({ token: FAKE_TOKEN, baseUrl: FAKE_API_BASE, fetch: hang, timeoutMs: 50 });
  // AbortSignal.timeout does not keep the event loop alive by itself.
  const keepAlive = setInterval(() => {}, 1000);
  const err = await a.call('getMe').catch((e) => e).finally(() => clearInterval(keepAlive));
  assert.ok(err instanceof TelegramError);
  assert.equal(err.code, 0);
  assert.match(err.description, /timeout/);

  const broken = (async () => { throw new TypeError(`fetch failed for ${FAKE_API_BASE}/bot${FAKE_TOKEN}/getMe`); }) as typeof fetch;
  const b = new TelegramApi({ token: FAKE_TOKEN, baseUrl: FAKE_API_BASE, fetch: broken });
  const e2 = await b.call('getMe').catch((e) => e);
  assert.equal(e2.code, 0);
  assert.ok(!String(e2.message).includes(FAKE_TOKEN));
});

test('cloud download: streams into a new file and enforces the size limit', async () => {
  const tg = new FakeTelegram();
  const { a } = api(tg);
  const dir = tmp();
  try {
    const data = new Uint8Array(5000).map((_, i) => i & 0xff);
    tg.addFile({ fileId: 'F1', fileUniqueId: 'U1', path: 'documents/file_1.nes', data });
    const dest = join(dir, 'a.part');
    const r = await a.download('F1', dest, 10_000);
    assert.equal(r.size, 5000);
    assert.deepEqual(new Uint8Array(readFileSync(dest)), data);

    // Reported size over the limit: refused before downloading.
    const small = join(dir, 'b.part');
    const e1 = await a.download('F1', small, 1000).catch((e) => e);
    assert.ok(isFileTooBig(e1));
    assert.ok(!existsSync(small));

    // The Bot API under-reports the size: the stream is cut off and the file removed.
    tg.addFile({ fileId: 'F2', fileUniqueId: 'U2', path: 'documents/file_2.zip', data, size: 10 });
    const lying = join(dir, 'c.part');
    const e2 = await a.download('F2', lying, 1000).catch((e) => e);
    assert.ok(e2 instanceof DownloadError && e2.reason === 'too_big');
    assert.ok(!existsSync(lying));

    // Cloud getFile refuses big files ("file is too big"): reported as too big.
    tg.failNext('getFile', 400, 'Bad Request: file is too big');
    const e3 = await a.download('F1', join(dir, 'd.part'), 10_000).catch((e) => e);
    assert.ok(isFileTooBig(e3));
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('local Bot API server: copies from BOT_FILE_DIR and refuses anything outside it', async () => {
  const tg = new FakeTelegram();
  const root = tmp();
  const shared = join(root, 'botapi');
  const outside = join(root, 'secret.txt');
  const work = join(root, 'work');
  mkdirSync(join(shared, '123', 'documents'), { recursive: true });
  mkdirSync(work);
  const source = join(shared, '123', 'documents', 'file_0.zip');
  writeFileSync(source, 'zipdata');
  writeFileSync(outside, 'do not read');
  symlinkSync(outside, join(shared, '123', 'documents', 'link.zip'));
  try {
    // Mounted at the same path on both sides.
    const { a } = api(tg, { local: true, fileDir: shared });
    tg.addFile({ fileId: 'OK', fileUniqueId: 'u', path: join(shared, '123', 'documents', 'file_0.zip'), data: new Uint8Array(7) });
    const r = await a.download('OK', join(work, 'ok.part'), 1000);
    assert.equal(r.size, 7);
    assert.equal(readFileSync(join(work, 'ok.part'), 'utf8'), 'zipdata');
    assert.ok(!existsSync(source), 'the Bot API server copy is removed once we have ours');
    writeFileSync(source, 'zipdata');

    const refuse = async (fileId: string, path: string) => {
      tg.addFile({ fileId, fileUniqueId: fileId, path, data: new Uint8Array(1) });
      const dest = join(work, `${fileId}.part`);
      const e = await a.download(fileId, dest, 1000).catch((x) => x);
      assert.ok(e instanceof DownloadError && e.reason === 'outside_dir', `${fileId} must be refused`);
      assert.ok(!existsSync(dest));
    };
    await refuse('abs', outside);
    await refuse('dotdot', `${shared}/123/../../secret.txt`);
    await refuse('symlink', join(shared, '123', 'documents', 'link.zip'));
    await refuse('relative', 'documents/file_0.zip');

    // Size limit applies to local copies too (and the source is kept).
    const big = await a.download('OK', join(work, 'big.part'), 3).catch((e) => e);
    assert.ok(isFileTooBig(big));
    assert.ok(existsSync(source));
    assert.ok(existsSync(outside), 'files outside BOT_FILE_DIR are never touched');

    // Mounted elsewhere: "<server path>=<local path>".
    const { a: mapped } = api(tg, { local: true, fileDir: `/var/lib/telegram-bot-api=${shared}` });
    tg.addFile({ fileId: 'MAP', fileUniqueId: 'm', path: '/var/lib/telegram-bot-api/123/documents/file_0.zip', data: new Uint8Array(7) });
    assert.equal((await mapped.download('MAP', join(work, 'map.part'), 1000)).size, 7);
    tg.addFile({ fileId: 'MAPX', fileUniqueId: 'mx', path: `${shared}/123/documents/file_0.zip`, data: new Uint8Array(7) });
    const e = await mapped.download('MAPX', join(work, 'mapx.part'), 1000).catch((x) => x);
    assert.equal(e.reason, 'outside_dir', 'only the server-side prefix is accepted');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('cloud download without Content-Length: the stream itself is cut off at the limit', async () => {
  const tg = new FakeTelegram();
  const { a } = api(tg);
  const dir = tmp();
  try {
    // getFile under-reports the size and the body carries no Content-Length,
    // so only the byte count of the stream can stop it.
    const data = new Uint8Array(8 * 1024).fill(7);
    tg.addFile({ fileId: 'C', fileUniqueId: 'uc', path: 'documents/c.zip', data, size: 10, serve: 'chunked' });
    const dest = join(dir, 'c.part');
    const e = await a.download('C', dest, 3000).catch((x) => x);
    assert.ok(e instanceof DownloadError && e.reason === 'too_big');
    assert.ok(!existsSync(dest), 'the partial file is removed');
    assert.ok(tg.fileBodiesCancelled >= 1, 'the transfer is cancelled, not drained');

    // Within the limit, the same streamed body is written in full.
    const ok = join(dir, 'ok.part');
    assert.equal((await a.download('C', ok, data.length)).size, data.length);
    assert.deepEqual(new Uint8Array(readFileSync(ok)), data);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('cloud download: an abort signal stops a stalled transfer and removes the file', async () => {
  const tg = new FakeTelegram();
  const { a } = api(tg);
  const dir = tmp();
  try {
    tg.addFile({ fileId: 'H', fileUniqueId: 'uh', path: 'documents/h.zip', data: new Uint8Array(4096), serve: 'hang' });
    const stop = new AbortController();
    const dest = join(dir, 'h.part');
    const p = a.download('H', dest, 1 << 20, { signal: stop.signal }).catch((x) => x);
    // Wait until the first chunk is on disk, then abort.
    const deadline = Date.now() + 2000;
    while (!existsSync(dest) && Date.now() < deadline) await new Promise((r) => setTimeout(r, 5));
    stop.abort();
    const e = await p;
    assert.ok(e instanceof TelegramError && e.code === 0, 'reported as an aborted transfer');
    assert.ok(!existsSync(dest));

    // Already aborted: getFile is not even asked.
    const before = tg.callsOf('getFile').length;
    const e2 = await a.download('H', join(dir, 'h2.part'), 1 << 20, { signal: stop.signal }).catch((x) => x);
    assert.ok(e2 instanceof TelegramError);
    assert.equal(tg.callsOf('getFile').length, before);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('cloud download: an existing destination is refused without touching it, and the body is released', async () => {
  const tg = new FakeTelegram();
  const { a } = api(tg);
  const dir = tmp();
  try {
    tg.addFile({ fileId: 'E', fileUniqueId: 'ue', path: 'documents/e.zip', data: new Uint8Array(4096).fill(1), serve: 'chunked' });
    const dest = join(dir, 'taken.part');
    writeFileSync(dest, 'someone else');
    const e = await a.download('E', dest, 1 << 20).catch((x) => x);
    assert.ok(e instanceof DownloadError && e.reason === 'unavailable');
    assert.match(e.message, /EEXIST/);
    assert.ok(!e.message.includes(dir), 'no paths in the error');
    assert.equal(readFileSync(dest, 'utf8'), 'someone else');
    assert.ok(tg.fileBodiesCancelled >= 1, 'the response body is cancelled, not left open');
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('local Bot API server: copy failures never expose paths (they contain the bot token)', async () => {
  const tg = new FakeTelegram();
  const root = tmp();
  // The local server keeps each bot's files under <dir>/<token>/.
  const shared = join(root, 'botapi');
  const tokenDir = join(shared, FAKE_TOKEN, 'documents');
  mkdirSync(tokenDir, { recursive: true });
  writeFileSync(join(tokenDir, 'file_1.zip'), 'zipdata');
  try {
    const { a } = api(tg, { local: true, fileDir: shared });
    tg.addFile({ fileId: 'L', fileUniqueId: 'ul', path: join(tokenDir, 'file_1.zip'), data: new Uint8Array(7) });
    // The destination directory does not exist: copyFile fails with ENOENT,
    // whose raw message would name the source path.
    const e = await a.download('L', join(root, 'missing-dir', 'x.part'), 1000).catch((x) => x);
    assert.ok(e instanceof DownloadError && e.reason === 'unavailable');
    assert.match(e.message, /ENOENT/);
    assert.ok(!e.message.includes(FAKE_TOKEN) && !e.message.includes(root), 'neither the token nor any path leaks');
    assert.ok(existsSync(join(tokenDir, 'file_1.zip')), 'the source is kept when the copy failed');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
