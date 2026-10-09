// Private, content-addressed file store for uploaded ROMs.
//
// A blob's path is derived only from the SHA-256 of its bytes
// (<dir>/<first two hex digits>/<sha256>), never from anything a user
// supplied. Directories are 0700 and files 0600. Writes go to a temporary
// file in <dir>/tmp, are flushed, and are renamed into place, so a blob path
// either holds the complete content or does not exist.

import { createHash, randomBytes } from 'node:crypto';
import { createReadStream, chmodSync, closeSync, existsSync, fsyncSync, mkdirSync, openSync, readdirSync, renameSync, statSync, unlinkSync, writeSync } from 'node:fs';
import { copyFile, readFile, rename, chmod, unlink } from 'node:fs/promises';
import { join } from 'node:path';
import { log } from '../log.ts';

const SHA256_HEX = /^[0-9a-f]{64}$/;

export function isSha256(s: unknown): s is string {
  return typeof s === 'string' && SHA256_HEX.test(s);
}

export async function sha256File(path: string): Promise<{ sha256: string; size: number }> {
  const h = createHash('sha256');
  let size = 0;
  for await (const chunk of createReadStream(path, { highWaterMark: 1 << 20 })) {
    h.update(chunk as Buffer);
    size += (chunk as Buffer).length;
  }
  return { sha256: h.digest('hex'), size };
}

export function sha256Bytes(data: Uint8Array): string {
  return createHash('sha256').update(data).digest('hex');
}

export class BlobStore {
  readonly dir: string;
  readonly tmpDir: string;

  constructor(dir: string) {
    this.dir = dir;
    this.tmpDir = join(dir, 'tmp');
    mkdirSync(this.tmpDir, { recursive: true, mode: 0o700 });
    chmodSync(dir, 0o700);
    chmodSync(this.tmpDir, 0o700);
  }

  pathOf(sha256: string): string {
    if (!isSha256(sha256)) throw new Error('invalid blob id');
    return join(this.dir, sha256.slice(0, 2), sha256);
  }

  has(sha256: string): boolean {
    return isSha256(sha256) && existsSync(this.pathOf(sha256));
  }

  size(sha256: string): number | null {
    if (!isSha256(sha256)) return null;
    try {
      return statSync(this.pathOf(sha256)).size;
    } catch {
      return null;
    }
  }

  // A fresh, unique path in the store's private temp directory.
  tempPath(suffix = ''): string {
    return join(this.tmpDir, `${randomBytes(12).toString('hex')}${suffix}`);
  }

  private shardFor(sha256: string): string {
    const shard = join(this.dir, sha256.slice(0, 2));
    mkdirSync(shard, { recursive: true, mode: 0o700 });
    return shard;
  }

  // Stores the file at `src` (hashing it first). With `move`, the source is
  // renamed into the store when possible and is gone afterwards either way.
  //
  // A blob that already exists is verified rather than trusted: a damaged
  // copy (disk error, truncated restore) is replaced by this good one, so
  // re-uploading a file repairs it. Verifying costs a full read, like the
  // check after storing a new file, which also narrows the timing difference
  // that could tell an uploader the content was already on the server (for
  // another group).
  async putFile(src: string, opts: { move?: boolean } = {}): Promise<{ sha256: string; size: number; created: boolean }> {
    const { sha256, size } = await sha256File(src);
    const dest = this.pathOf(sha256);
    const existed = existsSync(dest);
    if (existed) {
      const intact = await sha256File(dest).then((h) => h.sha256 === sha256, () => false);
      if (intact) {
        if (opts.move) await unlink(src).catch(() => {});
        return { sha256, size, created: false };
      }
      log.warn('blobs', 'stored blob was damaged; replacing it with the uploaded copy', { sha256 });
    }
    this.shardFor(sha256);
    const tmp = this.tempPath();
    try {
      if (opts.move) {
        try {
          await rename(src, tmp);
        } catch (e) {
          if ((e as NodeJS.ErrnoException).code !== 'EXDEV') throw e;
          await copyFile(src, tmp);
          await unlink(src).catch(() => {});
        }
      } else {
        await copyFile(src, tmp);
      }
      await chmod(tmp, 0o600);
      const fd = openSync(tmp, 'r');
      try {
        fsyncSync(fd);
      } finally {
        closeSync(fd);
      }
      // The content may have changed between hashing and copying only if
      // something else wrote to our private temp file; verify anyway.
      const check = await sha256File(tmp);
      if (check.sha256 !== sha256) throw new Error('blob content changed while storing');
      renameSync(tmp, dest);
    } catch (e) {
      await unlink(tmp).catch(() => {});
      if (opts.move) await unlink(src).catch(() => {});
      throw e;
    }
    return { sha256, size, created: !existed };
  }

  putBytes(data: Uint8Array): { sha256: string; size: number; created: boolean } {
    const sha256 = sha256Bytes(data);
    const dest = this.pathOf(sha256);
    if (existsSync(dest)) return { sha256, size: data.length, created: false };
    this.shardFor(sha256);
    const tmp = this.tempPath();
    const fd = openSync(tmp, 'wx', 0o600);
    try {
      let off = 0;
      while (off < data.length) off += writeSync(fd, data, off, data.length - off);
      fsyncSync(fd);
    } catch (e) {
      closeSync(fd);
      try {
        unlinkSync(tmp);
      } catch {
        /* already gone */
      }
      throw e;
    }
    closeSync(fd);
    renameSync(tmp, dest);
    return { sha256, size: data.length, created: true };
  }

  read(sha256: string): Promise<Buffer> {
    return readFile(this.pathOf(sha256));
  }

  delete(sha256: string): boolean {
    try {
      unlinkSync(this.pathOf(sha256));
      return true;
    } catch {
      return false;
    }
  }

  // Removes temp files left behind by a crash (older than maxAgeMs).
  sweepTemp(maxAgeMs: number, now = Date.now()): number {
    let n = 0;
    for (const f of readdirSync(this.tmpDir)) {
      const p = join(this.tmpDir, f);
      try {
        if (now - statSync(p).mtimeMs > maxAgeMs) {
          unlinkSync(p);
          n++;
        }
      } catch {
        /* raced with its owner */
      }
    }
    return n;
  }
}
