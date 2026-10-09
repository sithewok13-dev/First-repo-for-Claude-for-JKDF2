// Catalog provenance: a catalog generated from other FBNeo sources than the
// cores next to it describes another driver list, so the validator rebuilds
// it from the core instead of judging uploads against it.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { isStaleCatalog, RomCatalog, type CatalogFile } from '../../server/shelf/catalog.ts';

const CORE_DIR = join(new URL('../../', import.meta.url).pathname, 'native/build');

function emptyCatalog(build: Record<string, unknown>): CatalogFile {
  return { format: 1, core: 'fbneo_cps12', coreVersion: 'test', build, generatedAt: '', sets: [] };
}

test('staleness is decided by the FBNeo source commit; unknown provenance is not stale', () => {
  assert.equal(isStaleCatalog(emptyCatalog({ fbneo: 'aaa' }), { fbneo: 'aaa', frontend: 'x' }), false);
  assert.equal(isStaleCatalog(emptyCatalog({ fbneo: 'aaa', frontend: 'y' }), { fbneo: 'aaa', frontend: 'x' }), false, 'frontend changes do not alter the driver list');
  assert.equal(isStaleCatalog(emptyCatalog({ fbneo: 'aaa' }), { fbneo: 'bbb' }), true);
  assert.equal(isStaleCatalog(emptyCatalog({}), { fbneo: 'bbb' }), false);
  assert.equal(isStaleCatalog(emptyCatalog({ fbneo: 'aaa' }), {}), false);
});

test('a stale catalog file is rebuilt from the core; a current one is used as is', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'arcade-catalog-'));
  try {
    for (const f of ['fbneo_cps12.mjs', 'fbneo_cps12.wasm', 'build-info.json']) symlinkSync(join(CORE_DIR, f), join(dir, f));
    const build = JSON.parse(readFileSync(join(CORE_DIR, 'build-info.json'), 'utf8'));
    assert.equal(typeof build.fbneo, 'string', 'the core build records its FBNeo commit');
    const file = join(dir, 'catalog-fbneo_cps12.json');

    // An (empty) catalog from the same sources is trusted: nothing is found.
    writeFileSync(file, JSON.stringify(emptyCatalog(build)));
    assert.equal((await RomCatalog.loadOrBuild(dir)).get('fbneo_cps12', 'sf2'), null);

    // The same file claiming other sources is ignored and rebuilt in memory.
    writeFileSync(file, JSON.stringify(emptyCatalog({ ...build, fbneo: '0'.repeat(40) })));
    const rebuilt = await RomCatalog.loadOrBuild(dir);
    assert.ok(rebuilt.get('fbneo_cps12', 'sf2'), 'the driver list of the core in the directory');
    assert.deepEqual(rebuilt.systems, ['fbneo_cps12']);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
