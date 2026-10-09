// Fallback upload path for files Telegram's Bot API cannot hand us (larger
// than its download limit): the Mini App streams the raw file body to the
// server. The authenticated HTTP router calls handleWebUpload() and then
// shelf.ingest() with the result.
//
// Request: POST, Content-Type: application/octet-stream, the original file
// name URL-encoded in X-File-Name. The body is streamed to a private temp
// file and counted; past `maxBytes` the transfer is aborted and the partial
// file deleted. On an UploadError with status 413 the router should answer
// with "Connection: close" so the rest of the body is not read. Statuses:
// 400 bad request or interrupted transfer, 413 too large, 415 wrong type,
// 500 the temp file could not be written (logged).

import { randomBytes } from 'node:crypto';
import { createWriteStream, mkdirSync } from 'node:fs';
import { unlink } from 'node:fs/promises';
import type { IncomingMessage } from 'node:http';
import { join } from 'node:path';
import { Transform, type TransformCallback } from 'node:stream';
import { pipeline } from 'node:stream/promises';
import { log } from '../log.ts';

export class UploadError extends Error {
  readonly status: number;
  constructor(status: number, message: string) {
    super(message);
    this.name = 'UploadError';
    this.status = status;
  }
}

const MAX_NAME_BYTES = 1024;

// The name is only a label (and a romset-name hint): never a path.
function decodeFileName(raw: string | string[] | undefined): string {
  if (typeof raw !== 'string' || raw.length === 0) throw new UploadError(400, 'missing X-File-Name header');
  if (raw.length > MAX_NAME_BYTES) throw new UploadError(400, 'file name too long');
  let name: string;
  try {
    name = decodeURIComponent(raw);
  } catch {
    throw new UploadError(400, 'X-File-Name must be URL-encoded');
  }
  // Strip every path component and control character.
  name = name.replace(/^.*[\\/]/, '').replace(/[\u0000-\u001f\u007f-\u009f]/g, '').trim();
  if (!name || name === '.' || name === '..') throw new UploadError(400, 'invalid file name');
  return [...name].slice(0, 255).join('');
}

class ByteLimit extends Transform {
  count = 0;
  private readonly max: number;
  constructor(max: number) {
    super();
    this.max = max;
  }
  _transform(chunk: Buffer, _enc: BufferEncoding, cb: TransformCallback): void {
    this.count += chunk.length;
    if (this.count > this.max) {
      cb(new UploadError(413, `the file is larger than the ${this.max}-byte upload limit`));
      return;
    }
    cb(null, chunk);
  }
}

export async function handleWebUpload(
  req: IncomingMessage,
  opts: { tempDir: string; maxBytes: number },
): Promise<{ tempPath: string; size: number; fileName: string }> {
  const type = String(req.headers['content-type'] ?? '').split(';')[0].trim().toLowerCase();
  if (type !== 'application/octet-stream') throw new UploadError(415, 'send the file as application/octet-stream');
  const fileName = decodeFileName(req.headers['x-file-name']);
  const declared = req.headers['content-length'];
  if (declared !== undefined) {
    const n = Number(declared);
    if (!Number.isSafeInteger(n) || n < 0) throw new UploadError(400, 'invalid Content-Length');
    if (n > opts.maxBytes) throw new UploadError(413, `the file is larger than the ${opts.maxBytes}-byte upload limit`);
    if (n === 0) throw new UploadError(400, 'the file is empty');
  }
  // The router authenticates (asynchronously) before calling us: a client
  // that disconnected meanwhile has a destroyed request whose 'close' was
  // already emitted. Piping it would never end, so it is rejected here.
  if (req.destroyed || req.readableAborted) throw new UploadError(400, 'the upload was interrupted');

  mkdirSync(opts.tempDir, { recursive: true, mode: 0o700 });
  const tempPath = join(opts.tempDir, `upload-${randomBytes(12).toString('hex')}`);
  const limit = new ByteLimit(opts.maxBytes);
  // The request is piped in by hand rather than being part of the pipeline,
  // so a rejected body does not destroy the socket before the router has
  // answered 413. A client that disconnects mid-body fails the pipeline: a
  // request that closes before its body has ended will never end.
  const onClose = () => {
    if (!req.readableEnded) limit.destroy(new UploadError(400, 'the upload was interrupted'));
  };
  const onError = () => limit.destroy(new UploadError(400, 'the upload was interrupted'));
  req.on('close', onClose);
  req.on('error', onError);
  req.pipe(limit);
  try {
    await pipeline(limit, createWriteStream(tempPath, { flags: 'wx', mode: 0o600 }));
  } catch (e) {
    req.unpipe(limit);
    req.pause();
    await unlink(tempPath).catch(() => {});
    if (e instanceof UploadError) throw e;
    // Only the temp file can fail otherwise (disk full, permissions): a
    // server problem, not the client's.
    log.error('upload', 'could not write the upload to a temp file', { code: (e as NodeJS.ErrnoException).code ?? 'unknown' });
    throw new UploadError(500, 'the server could not store the upload; try again later');
  } finally {
    req.off('close', onClose);
    req.off('error', onError);
  }
  if (limit.count === 0) {
    await unlink(tempPath).catch(() => {});
    throw new UploadError(400, 'the file is empty');
  }
  return { tempPath, size: limit.count, fileName };
}
