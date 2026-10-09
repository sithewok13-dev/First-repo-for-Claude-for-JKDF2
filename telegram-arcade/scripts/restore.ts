// Restore: node scripts/restore.ts <backup-dir>/arcade-<timestamp> [--force]
// Stop the server first. Verifies every file's SHA-256 and the database
// integrity before writing anything; with --force, the current contents of
// DATA_DIR are moved to DATA_DIR/.before-restore-<timestamp>/ (not deleted).

import { join, resolve } from 'node:path';
import { restore, verifyBackup } from '../server/ops/backup.ts';

const args = process.argv.slice(2);
const dir = args.find((a) => !a.startsWith('--'));
if (!dir) {
  console.error('usage: node scripts/restore.ts <backup-dir>/arcade-<timestamp> [--force] [--verify-only]');
  process.exit(2);
}
if (args.includes('--verify-only')) {
  const m = verifyBackup(resolve(dir));
  console.log(`backup OK: schema v${m.schemaVersion}, ${m.blobs.length} ROM files, ${m.checkpoints.length} save points, created ${m.createdAt}`);
  process.exit(0);
}
const dataDir = resolve(process.env.DATA_DIR ?? join(new URL('../', import.meta.url).pathname, 'data'));
const r = restore({ backupDir: dir, dataDir, force: args.includes('--force') });
console.log(`restored ${dir} into ${dataDir}`);
if (r.movedAside) console.log(`previous contents kept in ${r.movedAside} (delete it once the restore is confirmed)`);
console.log('start the server; it applies any newer migrations automatically.');
