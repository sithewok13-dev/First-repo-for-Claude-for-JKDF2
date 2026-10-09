// Backup and restore of everything a deployment needs to come back:
// the SQLite database (consistent online snapshot via VACUUM INTO), the
// private ROM blobs it references, and the room checkpoints it references.
//
// A backup is a directory <out>/arcade-<UTC timestamp>/ with a manifest of
// SHA-256 hashes; restore verifies every hash before it writes anything.
// Blobs are immutable and content-addressed, so a backup hard-links blobs it
// shares with the previous backup instead of copying them again.
//
// Backups contain the groups' private ROM files: keep them as private as the
// data directory itself (they are written 0700/0600).

import { createHash } from 'node:crypto';
import { chmodSync, copyFileSync, existsSync, linkSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { DatabaseSync } from 'node:sqlite';
import { dirname, join, resolve } from 'node:path';

export const BACKUP_FORMAT = 1;

export interface Manifest {
  format: number;
  createdAt: string;
  schemaVersion: number;
  db: { file: string; sha256: string; size: number };
  blobs: { sha256: string; size: number }[];
  checkpoints: { file: string; sha256: string; size: number }[];
}

export interface BackupResult {
  dir: string;
  manifest: Manifest;
  copiedBytes: number;
  linkedBlobs: number;
  missingBlobs: string[];
  missingCheckpoints: string[];
  pruned: string[];
}

function sha256(path: string): string {
  return createHash('sha256').update(readFileSync(path)).digest('hex');
}

function stamp(d = new Date()): string {
  return d.toISOString().replace(/[-:]/g, '').replace(/\.\d+Z$/, 'Z');
}

function blobRel(sha: string): string {
  if (!/^[0-9a-f]{64}$/.test(sha)) throw new Error(`invalid blob id in database: ${sha}`);
  return join(sha.slice(0, 2), sha);
}

function safeRel(rel: string): string {
  // checkpoint paths come from our own database, but never follow one out of its directory
  if (!/^[0-9]+\/[0-9]+-[0-9]+\.state$/.test(rel)) throw new Error(`unexpected checkpoint path: ${rel}`);
  return rel;
}

function listBackups(outDir: string): string[] {
  if (!existsSync(outDir)) return [];
  return readdirSync(outDir).filter((n) => /^arcade-\d{8}T\d{6}Z$/.test(n) && existsSync(join(outDir, n, 'manifest.json'))).sort();
}

export function backup(opts: { dataDir: string; outDir: string; keep?: number; now?: Date }): BackupResult {
  const dataDir = resolve(opts.dataDir);
  const dbPath = join(dataDir, 'arcade.sqlite');
  if (!existsSync(dbPath)) throw new Error(`no database at ${dbPath}`);
  const outDir = resolve(opts.outDir);
  mkdirSync(outDir, { recursive: true, mode: 0o700 });
  const previous = listBackups(outDir).at(-1);
  const name = `arcade-${stamp(opts.now)}`;
  const dir = join(outDir, name);
  if (existsSync(dir)) throw new Error(`backup ${dir} already exists`);
  const work = `${dir}.partial`;
  rmSync(work, { recursive: true, force: true });
  mkdirSync(work, { mode: 0o700 });

  // 1. consistent database snapshot (readers and the running server are not blocked)
  const live = new DatabaseSync(dbPath, { readOnly: true });
  try {
    live.exec('PRAGMA busy_timeout = 10000');
    live.exec(`VACUUM INTO '${join(work, 'arcade.sqlite').replace(/'/g, "''")}'`);
  } finally {
    live.close();
  }
  chmodSync(join(work, 'arcade.sqlite'), 0o600);

  // 2. everything the snapshot references, read from the snapshot itself
  const snap = new DatabaseSync(join(work, 'arcade.sqlite'), { readOnly: true });
  let schemaVersion = 0, blobRows: { sha256: string; size: number }[] = [], cpRows: { file: string }[] = [];
  try {
    schemaVersion = Number((snap.prepare('SELECT MAX(version) AS v FROM schema_migrations').get() as any)?.v ?? 0);
    blobRows = snap.prepare('SELECT sha256, size FROM blobs ORDER BY sha256').all() as any;
    cpRows = snap.prepare('SELECT file FROM checkpoints WHERE valid = 1 ORDER BY id').all() as any;
  } finally {
    snap.close();
  }

  let copiedBytes = 0, linkedBlobs = 0;
  const missingBlobs: string[] = [], missingCheckpoints: string[] = [];
  const blobs: Manifest['blobs'] = [];
  for (const b of blobRows) {
    const rel = blobRel(b.sha256);
    const src = join(dataDir, 'blobs', rel);
    if (!existsSync(src)) {
      missingBlobs.push(b.sha256);
      continue;
    }
    const dst = join(work, 'blobs', rel);
    mkdirSync(dirname(dst), { recursive: true, mode: 0o700 });
    const prev = previous ? join(outDir, previous, 'blobs', rel) : '';
    let linked = false;
    if (prev && existsSync(prev)) {
      try {
        linkSync(prev, dst);
        linked = true;
        linkedBlobs++;
      } catch { /* different filesystem: copy */ }
    }
    if (!linked) {
      copyFileSync(src, dst);
      chmodSync(dst, 0o600);
      copiedBytes += b.size;
    }
    const h = sha256(dst);
    if (h !== b.sha256) throw new Error(`blob ${b.sha256} is corrupt in the data directory (hash ${h}); backup aborted`);
    blobs.push({ sha256: b.sha256, size: statSync(dst).size });
  }

  const checkpoints: Manifest['checkpoints'] = [];
  for (const c of cpRows) {
    const rel = safeRel(c.file);
    const src = join(dataDir, 'checkpoints', rel);
    if (!existsSync(src)) {
      missingCheckpoints.push(rel); // pruned between the snapshot and the copy: harmless
      continue;
    }
    const dst = join(work, 'checkpoints', rel);
    mkdirSync(dirname(dst), { recursive: true, mode: 0o700 });
    copyFileSync(src, dst);
    chmodSync(dst, 0o600);
    copiedBytes += statSync(dst).size;
    checkpoints.push({ file: rel, sha256: sha256(dst), size: statSync(dst).size });
  }

  const dbFile = join(work, 'arcade.sqlite');
  const manifest: Manifest = {
    format: BACKUP_FORMAT,
    createdAt: (opts.now ?? new Date()).toISOString(),
    schemaVersion,
    db: { file: 'arcade.sqlite', sha256: sha256(dbFile), size: statSync(dbFile).size },
    blobs,
    checkpoints,
  };
  copiedBytes += manifest.db.size;
  writeFileSync(join(work, 'manifest.json'), JSON.stringify(manifest, null, 1), { mode: 0o600 });
  renameSync(work, dir);

  // 3. retention
  const pruned: string[] = [];
  const keep = opts.keep ?? 7;
  if (keep > 0) {
    const all = listBackups(outDir);
    for (const old of all.slice(0, Math.max(0, all.length - keep))) {
      rmSync(join(outDir, old), { recursive: true, force: true });
      pruned.push(old);
    }
  }
  return { dir, manifest, copiedBytes, linkedBlobs, missingBlobs, missingCheckpoints, pruned };
}

export function verifyBackup(dir: string): Manifest {
  const mf = join(dir, 'manifest.json');
  if (!existsSync(mf)) throw new Error(`${dir} has no manifest.json`);
  const m = JSON.parse(readFileSync(mf, 'utf8')) as Manifest;
  if (m.format !== BACKUP_FORMAT) throw new Error(`unsupported backup format ${m.format}`);
  const check = (rel: string, want: string) => {
    const p = join(dir, rel);
    if (!existsSync(p)) throw new Error(`backup is missing ${rel}`);
    const got = sha256(p);
    if (got !== want) throw new Error(`backup file ${rel} is damaged (hash mismatch)`);
  };
  check(m.db.file, m.db.sha256);
  for (const b of m.blobs) check(join('blobs', blobRel(b.sha256)), b.sha256);
  for (const c of m.checkpoints) check(join('checkpoints', safeRel(c.file)), c.sha256);
  const db = new DatabaseSync(join(dir, m.db.file), { readOnly: true });
  try {
    const ok = (db.prepare('PRAGMA integrity_check').get() as any)?.integrity_check;
    if (ok !== 'ok') throw new Error(`database integrity check failed: ${ok}`);
  } finally {
    db.close();
  }
  return m;
}

// Restores into dataDir. The server must be stopped. Existing contents are
// moved aside into dataDir/.before-restore-<timestamp>/ (never deleted) when
// force is set; everything happens inside dataDir, so it also works when
// dataDir is a volume mount point.
export function restore(opts: { backupDir: string; dataDir: string; force?: boolean; now?: Date }): { manifest: Manifest; movedAside: string | null } {
  const src = resolve(opts.backupDir);
  const dataDir = resolve(opts.dataDir);
  const manifest = verifyBackup(src);
  mkdirSync(dataDir, { recursive: true, mode: 0o700 });
  const existing = readdirSync(dataDir).filter((n) => !n.startsWith('.before-restore-'));
  let movedAside: string | null = null;
  if (existing.length > 0) {
    if (!opts.force) throw new Error(`${dataDir} is not empty; stop the server and pass --force to move its contents aside`);
    movedAside = join(dataDir, `.before-restore-${stamp(opts.now)}`);
    mkdirSync(movedAside, { mode: 0o700 });
    for (const n of existing) renameSync(join(dataDir, n), join(movedAside, n));
  }
  const work = join(dataDir, '.restoring');
  rmSync(work, { recursive: true, force: true });
  mkdirSync(work, { mode: 0o700 });
  const put = (rel: string) => {
    const dst = join(work, rel);
    mkdirSync(dirname(dst), { recursive: true, mode: 0o700 });
    copyFileSync(join(src, rel), dst);
    chmodSync(dst, 0o600);
  };
  put(manifest.db.file);
  for (const b of manifest.blobs) put(join('blobs', blobRel(b.sha256)));
  for (const c of manifest.checkpoints) put(join('checkpoints', safeRel(c.file)));
  mkdirSync(join(work, 'blobs', 'tmp'), { recursive: true, mode: 0o700 });
  mkdirSync(join(work, 'checkpoints'), { recursive: true, mode: 0o700 });
  for (const n of ['blobs', 'checkpoints', manifest.db.file]) renameSync(join(work, n), join(dataDir, n));
  rmSync(work, { recursive: true, force: true });
  return { manifest, movedAside };
}
