// Writes native/build/catalog-<core>.json for each FBNeo core: the romsets
// the compiled driver list supports, with every ROM's name, size, CRC-32 and
// type bits. The upload validator identifies arcade zips against these files.
//
// Usage: node scripts/build-catalog.ts [coreDir]
// Run after native/build-cores.sh (the catalog must match the core build).

import { existsSync, renameSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { CATALOG_SYSTEMS, catalogFromCore, catalogPath } from '../server/shelf/catalog.ts';

const root = resolve(new URL('../', import.meta.url).pathname);
const coreDir = resolve(process.argv[2] ?? join(root, 'native/build'));

let built = 0;
for (const core of CATALOG_SYSTEMS) {
  if (!existsSync(join(coreDir, `${core}.wasm`))) {
    console.warn(`skip ${core}: ${core}.wasm not found in ${coreDir}`);
    continue;
  }
  const t0 = Date.now();
  const { file, notes } = await catalogFromCore(coreDir, core);
  const out = catalogPath(coreDir, core);
  const tmp = `${out}.${process.pid}.tmp`;
  writeFileSync(tmp, JSON.stringify(file));
  renameSync(tmp, out);
  const roms = file.sets.reduce((n, s) => n + s.roms.length, 0);
  const boards = file.sets.filter((s) => s.flags & (1 << 3)).map((s) => s.name);
  console.log(`${core}: ${file.sets.length} sets, ${roms} ROM entries, boards [${boards.join(', ')}], core ${file.coreVersion} -> ${out} (${Date.now() - t0} ms)`);
  for (const n of notes) console.log(`  note: ${n}`);
  built++;
}
if (built === 0) {
  console.error('no FBNeo cores found; build them first (npm run build:cores)');
  process.exit(1);
}
