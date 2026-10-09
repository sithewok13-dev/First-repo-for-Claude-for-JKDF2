// Online backup: node scripts/backup.ts <backup-dir> [--keep N]
// Reads DATA_DIR (default ./data). Safe while the server is running.
// Schedule it (cron, systemd timer, or the compose "backup" service) and copy
// <backup-dir> off the machine; see docs/OPERATIONS.md.

import { join, resolve } from 'node:path';
import { backup } from '../server/ops/backup.ts';

const args = process.argv.slice(2);
const out = args.find((a) => !a.startsWith('--'));
const keepIdx = args.indexOf('--keep');
const keep = keepIdx >= 0 ? Number(args[keepIdx + 1]) : Number(process.env.BACKUP_KEEP ?? 7);
if (!out || !Number.isInteger(keep) || keep < 0) {
  console.error('usage: node scripts/backup.ts <backup-dir> [--keep N]');
  process.exit(2);
}
const dataDir = resolve(process.env.DATA_DIR ?? join(new URL('../', import.meta.url).pathname, 'data'));
const r = backup({ dataDir, outDir: out, keep });
console.log(`backup written: ${r.dir}`);
console.log(`  schema v${r.manifest.schemaVersion}, ${r.manifest.blobs.length} ROM files (${r.linkedBlobs} shared with the previous backup), ${r.manifest.checkpoints.length} save points, ${(r.copiedBytes / 1e6).toFixed(1)} MB copied`);
if (r.missingBlobs.length) console.warn(`  WARNING: ${r.missingBlobs.length} ROM file(s) referenced by the database are missing from ${dataDir}/blobs`);
if (r.pruned.length) console.log(`  removed old backups: ${r.pruned.join(', ')}`);
