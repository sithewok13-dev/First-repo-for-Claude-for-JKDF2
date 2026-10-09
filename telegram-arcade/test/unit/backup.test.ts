// Backup/restore against a live server's data directory: online snapshot,
// hash-verified restore into a fresh directory that a new server instance
// can use, hard-linked blobs across backups, retention, damage detection,
// and refusal to overwrite a populated data directory without --force.

import { test, after } from 'node:test';
import assert from 'node:assert/strict';
import { appendFileSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { backup, restore, verifyBackup } from '../../server/ops/backup.ts';
import { Db } from '../../server/db/db.ts';
import { startHarness } from '../helpers/harness.ts';

const tmp = mkdtempSync(join(tmpdir(), 'arcade-backup-'));
after(() => rmSync(tmp, { recursive: true, force: true }));

test('online backup of a running server restores into a working data directory', async () => {
  const h = await startHarness();
  try {
    const gameId = await h.addGame('native/testroms/build/atc-versus.nes', 42);
    const game = h.app.db.get<any>('SELECT * FROM games WHERE id = ?', gameId);
    // a save point, as the room manager writes them
    mkdirSync(join(h.dataDir, 'checkpoints', String(h.groupId)), { recursive: true });
    writeFileSync(join(h.dataDir, 'checkpoints', String(h.groupId), '7-600.state'), Buffer.alloc(5000, 7));
    h.app.db.run('INSERT INTO checkpoints (group_id, game_id, session_id, frame, file, compat_key, size, created_at) VALUES (?, ?, 7, 600, ?, ?, 5000, ?)',
      h.groupId, gameId, `${h.groupId}/7-600.state`, 'k', Date.now());

    const out = join(tmp, 'backups');
    const b1 = backup({ dataDir: h.dataDir, outDir: out, now: new Date('2026-10-01T03:00:00Z') });
    assert.equal(b1.manifest.blobs.length, 1);
    assert.equal(b1.manifest.blobs[0].sha256, game.blob_sha256);
    assert.equal(b1.manifest.checkpoints.length, 1);
    assert.equal(b1.linkedBlobs, 0);
    assert.ok(b1.manifest.schemaVersion >= 1);
    // private permissions
    assert.equal(statSync(join(b1.dir, 'arcade.sqlite')).mode & 0o077, 0);
    assert.equal(statSync(b1.dir).mode & 0o077, 0);

    // the second backup shares the immutable blob with the first
    const b2 = backup({ dataDir: h.dataDir, outDir: out, now: new Date('2026-10-02T03:00:00Z') });
    assert.equal(b2.linkedBlobs, 1);
    const rel = join('blobs', game.blob_sha256.slice(0, 2), game.blob_sha256);
    assert.equal(statSync(join(b2.dir, rel)).ino, statSync(join(b1.dir, rel)).ino);

    // restore into an empty directory and boot a second server on it
    const restored = join(tmp, 'restored');
    restore({ backupDir: b2.dir, dataDir: restored });
    const h2 = await startHarness({ DATA_DIR: restored });
    try {
      const g2 = h2.app.db.get<any>('SELECT * FROM games WHERE id = ?', gameId);
      assert.equal(g2.blob_sha256, game.blob_sha256);
      assert.equal(g2.display_name, game.display_name);
      const rom = readFileSync(join(restored, rel));
      assert.deepEqual(rom, readFileSync('native/testroms/build/atc-versus.nes'));
      assert.ok(existsSync(join(restored, 'checkpoints', String(h.groupId), '7-600.state')));
    } finally {
      await h2.app.close();
    }
  } finally {
    await h.stop();
  }
});

test('restore refuses damaged backups and populated data directories', () => {
  const data = join(tmp, 'd2');
  mkdirSync(join(data, 'blobs'), { recursive: true });
  const db = new Db(join(data, 'arcade.sqlite'));
  db.migrate();
  const sha = 'ab'.repeat(32);
  db.run('INSERT INTO blobs (sha256, size, created_at) VALUES (?, 4, 0)', sha);
  db.close();
  mkdirSync(join(data, 'blobs', 'ab'), { recursive: true });
  writeFileSync(join(data, 'blobs', 'ab', sha), 'oops'); // content does not match its name
  assert.throws(() => backup({ dataDir: data, outDir: join(tmp, 'b2') }), /corrupt/);

  // a good backup, then damage it
  rmSync(join(data, 'blobs', 'ab', sha));
  const b = backup({ dataDir: data, outDir: join(tmp, 'b3'), keep: 2 });
  assert.deepEqual(b.missingBlobs, [sha]);
  verifyBackup(b.dir);
  appendFileSync(join(b.dir, 'arcade.sqlite'), 'x');
  assert.throws(() => restore({ backupDir: b.dir, dataDir: join(tmp, 'never') }), /damaged/);
  assert.ok(!existsSync(join(tmp, 'never', 'arcade.sqlite')), 'nothing written for a damaged backup');

  // populated target: refused without force; with force, old contents are kept aside
  const b4 = backup({ dataDir: data, outDir: join(tmp, 'b4') });
  assert.throws(() => restore({ backupDir: b4.dir, dataDir: data }), /not empty/);
  const r = restore({ backupDir: b4.dir, dataDir: data, force: true, now: new Date('2026-10-03T00:00:00Z') });
  assert.ok(r.movedAside && existsSync(join(r.movedAside, 'arcade.sqlite')));
  assert.ok(existsSync(join(data, 'arcade.sqlite')));
});

test('retention keeps the newest N backups', () => {
  const data = join(tmp, 'd3');
  const db = new Db(join(data, 'arcade.sqlite'));
  db.migrate();
  db.close();
  const out = join(tmp, 'b5');
  for (let d = 1; d <= 5; d++) backup({ dataDir: data, outDir: out, keep: 3, now: new Date(`2026-09-0${d}T00:00:00Z`) });
  assert.deepEqual(readdirSync(out).sort(), ['arcade-20260903T000000Z', 'arcade-20260904T000000Z', 'arcade-20260905T000000Z']);
});
