// The streaming web upload path: body to a private temp file with a hard
// byte limit, URL-encoded file names, no partial files left behind.

import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { createServer, request, type Server } from 'node:http';
import { mkdtempSync, readdirSync, readFileSync, rmSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { handleWebUpload, UploadError } from '../../server/shelf/upload-http.ts';

let dir: string;
let server: Server;
let port = 0;
const MAX = 64 * 1024;
// Outcome of each handled request, for tests whose client never sees a reply.
let settled: ((outcome: string) => void) | null = null;

before(async () => {
  dir = mkdtempSync(join(tmpdir(), 'arcade-upload-'));
  server = createServer(async (req, res) => {
    // Like the real router, which authenticates asynchronously first.
    if (req.url === '/delayed') await new Promise((r) => setTimeout(r, 150));
    try {
      const r = await handleWebUpload(req, { tempDir: join(dir, 'tmp'), maxBytes: MAX });
      settled?.('ok');
      res.writeHead(200, { 'content-type': 'application/json' });
      res.end(JSON.stringify(r));
    } catch (e) {
      const status = e instanceof UploadError ? e.status : 500;
      settled?.(`${status}`);
      res.writeHead(status, { connection: 'close', 'content-type': 'text/plain' });
      res.end(e instanceof Error ? e.message : 'error');
    }
  });
  await new Promise<void>((r) => server.listen(0, '127.0.0.1', r));
  const a = server.address();
  port = typeof a === 'object' && a ? a.port : 0;
});

after(() => {
  server.close();
  rmSync(dir, { recursive: true, force: true });
});

function send(body: Buffer, headers: Record<string, string>, chunked = false): Promise<{ status: number; text: string }> {
  return new Promise((resolve, reject) => {
    const req = request({ host: '127.0.0.1', port, method: 'POST', path: '/upload', headers }, (res) => {
      let text = '';
      res.on('data', (c) => (text += c));
      res.on('end', () => resolve({ status: res.statusCode ?? 0, text }));
    });
    req.on('error', (e) => {
      // the server may close the connection after rejecting an oversized body
      if ((e as NodeJS.ErrnoException).code === 'ECONNRESET' || (e as NodeJS.ErrnoException).code === 'EPIPE') resolve({ status: -1, text: 'reset' });
      else reject(e);
    });
    if (chunked) {
      for (let i = 0; i < body.length; i += 8192) req.write(body.subarray(i, i + 8192));
      req.end();
    } else {
      req.end(body);
    }
  });
}

const leftovers = () => {
  try {
    return readdirSync(join(dir, 'tmp')).filter((f) => statSync(join(dir, 'tmp', f)).isFile());
  } catch {
    return [];
  }
};

test('streams the body to a private temp file and decodes the file name', async () => {
  const body = Buffer.alloc(10_000, 7);
  const r = await send(body, { 'content-type': 'application/octet-stream', 'x-file-name': encodeURIComponent('../../Street Fighter II (World).zip') });
  assert.equal(r.status, 200, r.text);
  const out = JSON.parse(r.text);
  assert.equal(out.size, 10_000);
  assert.equal(out.fileName, 'Street Fighter II (World).zip', 'path components are stripped');
  assert.equal(Buffer.compare(readFileSync(out.tempPath), body), 0);
  assert.equal(statSync(out.tempPath).mode & 0o777, 0o600);
  rmSync(out.tempPath);
});

test('rejects oversized bodies (declared or streamed) and leaves no partial file', async () => {
  const big = Buffer.alloc(MAX + 1, 1);
  const declared = await send(big, { 'content-type': 'application/octet-stream', 'x-file-name': 'big.zip' });
  assert.equal(declared.status, 413);
  // Without Content-Length (chunked): counted while streaming.
  const streamed = await send(Buffer.alloc(MAX * 3, 2), { 'content-type': 'application/octet-stream', 'x-file-name': 'big.zip', 'transfer-encoding': 'chunked' }, true);
  assert.equal(streamed.status, 413);
  assert.deepEqual(leftovers(), []);
});

test('rejects wrong content types, missing or malformed names and empty bodies', async () => {
  assert.equal((await send(Buffer.from('x'), { 'content-type': 'multipart/form-data', 'x-file-name': 'a.zip' })).status, 415);
  assert.equal((await send(Buffer.from('x'), { 'content-type': 'application/octet-stream' })).status, 400);
  assert.equal((await send(Buffer.from('x'), { 'content-type': 'application/octet-stream', 'x-file-name': '%E0%A4%A' })).status, 400);
  assert.equal((await send(Buffer.from('x'), { 'content-type': 'application/octet-stream', 'x-file-name': '..' })).status, 400);
  assert.equal((await send(Buffer.alloc(0), { 'content-type': 'application/octet-stream', 'x-file-name': 'a.zip' })).status, 400);
  assert.deepEqual(leftovers(), []);
});

test('a client that disconnects mid-body leaves no partial file', async () => {
  const seen = new Promise<void>((resolve) => server.once('request', (req) => req.once('close', () => setTimeout(resolve, 50))));
  const req = request({ host: '127.0.0.1', port, method: 'POST', path: '/upload', headers: { 'content-type': 'application/octet-stream', 'x-file-name': 'cut.zip', 'content-length': '50000' } });
  req.on('error', () => {});
  req.write(Buffer.alloc(10_000, 3));
  await new Promise((r) => setTimeout(r, 50));
  req.destroy();
  await seen;
  assert.deepEqual(leftovers(), []);
});

test('a client that disconnected before the handler ran is rejected at once (no hang, no temp file)', async () => {
  const outcome = new Promise<string>((resolve) => (settled = resolve));
  const req = request({ host: '127.0.0.1', port, method: 'POST', path: '/delayed', headers: { 'content-type': 'application/octet-stream', 'x-file-name': 'gone.zip', 'content-length': '50000' } });
  req.on('error', () => {});
  req.write(Buffer.alloc(1000, 4));
  setTimeout(() => req.destroy(), 30); // while the router is still "authenticating"
  const timeout = new Promise<string>((resolve) => setTimeout(() => resolve('hung'), 3000).unref());
  const result = await Promise.race([outcome, timeout]);
  settled = null;
  assert.equal(result, '400', 'the handler settles with "interrupted" instead of waiting forever');
  assert.deepEqual(leftovers(), []);
});
