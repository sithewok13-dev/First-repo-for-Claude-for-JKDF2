// Shelf ingest end to end: real validation child processes, real cores, a
// temporary data directory and an in-memory database with all migrations.
//
// Arcade romsets are SYNTHETIC: each file has the exact name, size and
// CRC-32 the FBNeo driver expects, but its content is zeros plus four bytes
// that force the CRC. No ROM data is involved; booting them shows a blank
// screen, which is exactly what the boot test must catch.

import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Db } from '../../server/db/db.ts';
import { Groups } from '../../server/groups.ts';
import { ATC_HASHES } from '../../server/adapters/atc.ts';
import { RomCatalog, isRequiredRom, BRF } from '../../server/shelf/catalog.ts';
import { parseNes } from '../../server/shelf/nes.ts';
import { Shelf, ShelfError, type ShelfConfig } from '../../server/shelf/shelf.ts';
import type { IngestResult } from '../../server/shelf/types.ts';
import { Validator, type BootJob, type BootResult, type InspectJob, type InspectResult, type ValidatorLike } from '../../server/shelf/validator.ts';
import { forcedCrcData, makeZip } from '../helpers/zipgen.ts';

const ROOT = new URL('../../', import.meta.url).pathname;
const CORE_DIR = join(ROOT, 'native/build');
const ROMS = join(ROOT, 'native/testroms/build');
const DAY = 86_400_000;

let dataDir: string;
let db: Db;
let groups: Groups;
let n = 0;

function config(over: Partial<ShelfConfig> = {}): ShelfConfig {
  return {
    blobDir: join(dataDir, 'blobs'),
    coreDir: CORE_DIR,
    maxUploadBytes: 64 * 1024 * 1024,
    maxExtractedBytes: 256 * 1024 * 1024,
    maxArchiveEntries: 512,
    maxCompressionRatio: 200,
    globalStorageLimitBytes: 1024 * 1024 * 1024,
    validationTimeoutMs: 60_000,
    maxConcurrentValidations: 2,
    removedGameRetentionDays: 7,
    workerHeapMb: 256,
    ...over,
  };
}

function newGroup(title: string, quota?: number): number {
  const g = groups.ensure(-100_000 - ++n, title);
  if (quota !== undefined) db.run('UPDATE groups SET quota_bytes = ? WHERE id = ?', quota, g.id);
  return g.id;
}

function addUser(id: number, first: string): number {
  db.run('INSERT OR REPLACE INTO users (id, first_name, last_name, updated_at) VALUES (?, ?, ?, ?)', id, first, '', Date.now());
  return id;
}

async function upload(shelf: Shelf, groupId: number, userId: number, fileName: string, data: Uint8Array): Promise<IngestResult> {
  const tempPath = join(dataDir, `incoming-${++n}.bin`);
  writeFileSync(tempPath, data);
  const r = await shelf.ingest({ groupId, userId, fileName, source: 'web', tempPath, size: data.length });
  assert.equal(existsSync(tempPath), false, 'ingest consumes the temp file');
  return r;
}

// A synthetic zip for `set` (its own required files; `filter` narrows them).
const catalog = () => RomCatalog.load(CORE_DIR);
function synthZip(system: 'fbneo_cps12' | 'fbneo_neogeo', set: string, filter: (r: { n: string; s: number; c: number; t: number }) => boolean = () => true): Buffer {
  const s = catalog().get(system, set);
  assert.ok(s, `catalog has ${set}`);
  const roms = s.full.filter(isRequiredRom).filter(filter);
  return makeZip(roms.map((r) => ({ name: r.n, data: forcedCrcData(r.s, r.c) })));
}

// Real inspection in the child process, fake boot test (to exercise the
// "everything present -> ready" path without real ROM data).
class FakeBoot implements ValidatorLike {
  readonly real: Validator;
  boots: BootJob[] = [];
  constructor() {
    this.real = new Validator({ timeoutMs: 60_000, maxConcurrent: 2, heapMb: 256 });
  }
  inspect(job: Parameters<Validator['inspect']>[0]) {
    return this.real.inspect(job);
  }
  async boot(job: BootJob): Promise<BootResult> {
    this.boots.push(job);
    return { tried: true, ok: true, frames: 300, detail: 'fake boot' };
  }
}

// Like FakeBoot, but a boot test can be held open, so the test can change
// the shelf while it runs.
class GatedBoot implements ValidatorLike {
  readonly real = new Validator({ timeoutMs: 60_000, maxConcurrent: 2, heapMb: 256 });
  boots: BootJob[] = [];
  started: Promise<void> = Promise.resolve();
  private gate: Promise<void> = Promise.resolve();
  private onStart: (() => void) | null = null;
  private release: (() => void) | null = null;
  hold(): void {
    this.gate = new Promise((r) => (this.release = r));
    this.started = new Promise((r) => (this.onStart = r));
  }
  open(): void {
    this.release?.();
    this.gate = Promise.resolve();
  }
  inspect(job: InspectJob) {
    return this.real.inspect(job);
  }
  async boot(job: BootJob): Promise<BootResult> {
    this.boots.push(job);
    this.onStart?.();
    await this.gate;
    return { tried: true, ok: true, frames: 300, detail: 'fake boot' };
  }
}

before(() => {
  dataDir = mkdtempSync(join(tmpdir(), 'arcade-shelf-'));
  db = new Db(':memory:');
  db.migrate();
  groups = new Groups(db, 2 * 1024 * 1024 * 1024);
  addUser(1001, 'Alice');
  addUser(1002, 'Bob');
});

after(() => {
  db.close();
  rmSync(dataDir, { recursive: true, force: true });
});

test('NES test ROM: ingest, adapter mode, session spec and group-bound file access', async () => {
  const shelf = new Shelf({ db, cfg: config(), groups });
  const ga = newGroup('A');
  const gb = newGroup('B');
  const rom = readFileSync(join(ROMS, 'atc-versus.nes'));
  const r = await upload(shelf, ga, 1001, 'atc-versus.nes', rom);
  assert.equal(r.status, 'done', r.error);
  const g = r.game!;
  assert.equal(g.status, 'ready');
  assert.equal(g.system, 'fceumm');
  assert.equal(g.kind, 'game');
  assert.equal(g.mode, 'versus');
  assert.equal(g.players, 2);
  assert.equal(g.adapter?.id, 'atc-versus');
  assert.equal(g.adapter?.capabilities.matchResult, true);
  assert.equal(g.sha256, ATC_HASHES.versus);
  assert.equal(g.sizeBytes, rom.length);
  assert.equal(g.uploaderName, 'Alice');
  assert.equal(g.displayName, 'ATC Versus (test ROM)');
  assert.equal(g.compat, 'untested');
  assert.equal(g.validation.boot?.ok, true);
  assert.match(g.validation.identifiedAs ?? '', /mapper 0/);
  assert.equal(g.metadata.artwork, null, 'no artwork: clients draw a placeholder');
  assert.ok(!('_x' in g.validation), 'internal details are not exposed');

  const spec = await shelf.sessionSpec(ga, g.id);
  assert.equal(spec.gamePath, '/roms/game.nes');
  assert.deepEqual(spec.files.map((f) => f.path), ['/roms/game.nes']);
  assert.equal(Buffer.compare(Buffer.from(spec.files[0].data), rom), 0);
  assert.deepEqual(spec.clientFiles, [{ path: '/roms/game.nes', sha256: ATC_HASHES.versus, size: rom.length }]);
  assert.equal(spec.ports, 2);
  assert.equal(spec.mode, 'versus');
  assert.equal(spec.adapterId, 'atc-versus');
  assert.match(spec.compatKey, /^[0-9a-f]{64}$/);
  assert.equal((await shelf.sessionSpec(ga, g.id)).compatKey, spec.compatKey, 'compat key is stable');
  assert.equal(spec.options.fceumm_turbo_enable, 'None');

  assert.ok(shelf.blobForGroup(ga, g.sha256), 'the owning group can fetch its file');
  assert.equal(shelf.blobForGroup(gb, g.sha256), null, 'another group cannot');
  assert.equal(shelf.blobForGroup(ga, '../../etc/passwd'), null);
  assert.equal(shelf.get(gb, g.id), null, 'entries are group-bound');
  await assert.rejects(shelf.sessionSpec(gb, g.id), (e: unknown) => e instanceof ShelfError && e.code === 'not_found');
});

test('NES ROM inside a zip: the .nes is stored; co-op adapter gives 4 ports', async () => {
  const shelf = new Shelf({ db, cfg: config(), groups });
  const gid = newGroup('zip');
  const rom = readFileSync(join(ROMS, 'atc-coop.nes'));
  const zip = makeZip([{ name: 'ATC Co-op.nes', data: rom, method: 8 }, { name: 'readme.txt', data: Buffer.from('hi') }]);
  const r = await upload(shelf, gid, 1001, 'atc-coop.zip', zip);
  assert.equal(r.status, 'done', r.error);
  assert.equal(r.game!.sha256, ATC_HASHES.coop, 'stored content is the ROM itself');
  assert.equal(r.game!.mode, 'coop');
  assert.equal(r.game!.players, 4);
  assert.equal(r.game!.status, 'ready');
  assert.ok(r.game!.validation.findings.some((f) => f.code === 'nes_in_zip'));
  const spec = await shelf.sessionSpec(gid, r.game!.id);
  assert.equal(spec.ports, 4);
  assert.equal(spec.adapterId, 'atc-coop');
});

test('duplicate in the same group; same content in another group is a normal new entry', async () => {
  const shelf = new Shelf({ db, cfg: config(), groups });
  const ga = newGroup('dupA');
  const gb = newGroup('dupB');
  const rom = readFileSync(join(ROMS, 'atc-turns.nes'));
  const first = await upload(shelf, ga, 1001, 'atc-turns.nes', rom);
  assert.equal(first.status, 'done');
  assert.equal(first.duplicate, undefined);

  const again = await upload(shelf, ga, 1002, 'renamed.nes', rom);
  assert.equal(again.status, 'done');
  assert.equal(again.duplicate, true);
  assert.equal(again.game!.id, first.game!.id);
  assert.equal(shelf.list(ga, 1001).length, 1, 'no second entry');

  const other = await upload(shelf, gb, 1002, 'atc-turns.nes', rom);
  assert.equal(other.status, 'done');
  assert.equal(other.duplicate, undefined, 'nothing reveals that another group has this file');
  assert.notEqual(other.game!.id, first.game!.id);
  assert.equal(other.game!.uploaderName, 'Bob');
  assert.equal(other.game!.validation.boot?.tried, true, 'full validation ran again');
  assert.ok(other.game!.uploadedAt >= first.game!.uploadedAt);
  assert.equal(other.game!.mode, 'turns-shared');
  assert.equal(db.get('SELECT COUNT(*) AS n FROM blobs WHERE sha256 = ?', ATC_HASHES.turns)!.n, 1, 'stored once');
  assert.equal(shelf.list(ga, 1001).length, 1);
  assert.equal(shelf.list(gb, 1001).length, 1);
});

test('group quota and global storage limit', async () => {
  const shelf = new Shelf({ db, cfg: config(), groups });
  const gid = newGroup('small', 30_000);
  const a = await upload(shelf, gid, 1001, 'atc-versus.nes', readFileSync(join(ROMS, 'atc-versus.nes')));
  assert.equal(a.status, 'done', a.error);
  const b = await upload(shelf, gid, 1001, 'atc-solo.nes', readFileSync(join(ROMS, 'atc-solo.nes')));
  assert.equal(b.status, 'failed');
  assert.match(b.error!, /shelf is full/);
  assert.equal(shelf.list(gid, 1001).length, 1);
  assert.deepEqual(shelf.usage(gid), { bytes: 24592, quota: 30_000, games: 1 });
  const upl = db.get('SELECT status, error FROM uploads WHERE id = ?', b.uploadId)!;
  assert.equal(upl.status, 'failed');

  // Global limit: judged as if the content were new, even when another
  // group already stored the same bytes (no side channel).
  const tight = new Shelf({ db, cfg: config({ globalStorageLimitBytes: 1 }), groups });
  const g2 = newGroup('global');
  const c = await upload(tight, g2, 1001, 'atc-versus.nes', readFileSync(join(ROMS, 'atc-versus.nes')));
  assert.equal(c.status, 'failed');
  assert.match(c.error!, /out of storage/);
});

test('rejected uploads store nothing: 7z, archive bomb, broken NES header', async () => {
  const shelf = new Shelf({ db, cfg: config(), groups });
  const gid = newGroup('reject');
  const blobsBefore = db.get('SELECT COUNT(*) AS n FROM blobs')!.n;

  const sevenZ = Buffer.concat([Buffer.from([0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c, 0, 4]), Buffer.alloc(100)]);
  const r1 = await upload(shelf, gid, 1001, 'sf2.7z', sevenZ);
  assert.equal(r1.status, 'failed');
  assert.match(r1.error!, /7z archives are not supported/);

  const bomb = makeZip([{ name: 'bomb.bin', data: new Uint8Array(16 * 1024 * 1024), method: 8 }]);
  const r2 = await upload(shelf, gid, 1001, 'ffight.zip', bomb);
  assert.equal(r2.status, 'failed');
  assert.ok(r2.findings?.some((f) => f.code === 'zip_bomb'), JSON.stringify(r2.findings));

  const nes = Buffer.from(readFileSync(join(ROMS, 'atc-solo.nes')).subarray(0, 5000));
  const r3 = await upload(shelf, gid, 1001, 'cut.nes', nes);
  assert.equal(r3.status, 'failed');
  assert.match(r3.error!, /truncated|declares/);

  const evil = makeZip([{ name: '../../evil.nes', data: readFileSync(join(ROMS, 'atc-solo.nes')) }, { name: 'b.nes', data: readFileSync(join(ROMS, 'atc-coop.nes')) }]);
  const r4 = await upload(shelf, gid, 1001, 'two.zip', evil);
  assert.equal(r4.status, 'failed');
  assert.match(r4.error!, /one game per file/);

  assert.equal(shelf.list(gid, 1001).length, 0);
  assert.equal(db.get('SELECT COUNT(*) AS n FROM blobs')!.n, blobsBefore, 'no blob stored');
});

test('unknown zip becomes a needs_attention entry with a clear message', async () => {
  const shelf = new Shelf({ db, cfg: config(), groups });
  const gid = newGroup('unknown');
  const zip = makeZip([{ name: 'mystery.bin', data: forcedCrcData(65536, 0x13572468, 0x55) }, { name: 'game.sfc', data: Buffer.alloc(1024, 1) }]);
  const r = await upload(shelf, gid, 1001, 'mystery.zip', zip);
  assert.equal(r.status, 'done', r.error);
  const g = r.game!;
  assert.equal(g.status, 'needs_attention');
  assert.equal(g.compat, 'needs_attention');
  assert.equal(g.system, null);
  assert.equal(g.validation.identifiedAs, null);
  const codes = g.validation.findings.map((f) => f.code);
  assert.ok(codes.includes('unknown_romset'));
  assert.ok(codes.includes('unsupported_system'), 'names the other system');
  assert.ok(g.validation.findings.some((f) => f.message.includes('Super Nintendo')));
  await assert.rejects(shelf.sessionSpec(gid, g.id), (e: unknown) => e instanceof ShelfError && e.code === 'not_ready');
});

test('FBNeo CPS-1: clone needs its parent; boot test fails cleanly on garbage; parent arrival re-evaluates', async () => {
  const shelf = new Shelf({ db, cfg: config(), groups });
  const gid = newGroup('cps');
  const cat = catalog();
  const parentKeys = new Set(cat.get('fbneo_cps12', 'ffight')!.full.map((r) => `${r.c}:${r.s}`));

  // Split clone zip: only the files that differ from the parent.
  const clone = await upload(shelf, gid, 1001, 'ffightu.zip', synthZip('fbneo_cps12', 'ffightu', (r) => !parentKeys.has(`${r.c}:${r.s}`)));
  assert.equal(clone.status, 'done', clone.error);
  const c = clone.game!;
  assert.equal(c.system, 'fbneo_cps12');
  assert.equal(c.setName, 'ffightu');
  assert.equal(c.status, 'needs_dependency');
  assert.equal(c.parentSet, 'ffight');
  assert.equal(c.biosSet, null);
  assert.equal(c.mode, 'coop', "beat 'em up -> co-op");
  assert.equal(c.players, 2);
  assert.equal(c.genre, "Beat 'em up");
  assert.equal(c.validation.boot, null, 'no boot test without the parent');
  assert.equal(c.validation.missing.length, 1);
  assert.equal(c.validation.missing[0].kind, 'parent');
  assert.equal(c.validation.missing[0].set, 'ffight');
  assert.ok(c.validation.missing[0].files.includes('ff-5m.7a'));
  assert.ok(!c.validation.missing[0].files.includes('ffu_43.12h'), 'its own file is present');
  assert.ok(c.validation.findings.some((f) => f.code === 'missing_parent' && f.message.includes('ffight.zip')));
  await assert.rejects(shelf.sessionSpec(gid, c.id), (e: unknown) => e instanceof ShelfError && e.code === 'not_ready');

  // The parent: identified, booted with the real core, blank screen -> needs attention.
  const parent = await upload(shelf, gid, 1002, 'ffight.zip', synthZip('fbneo_cps12', 'ffight'));
  assert.equal(parent.status, 'done', parent.error);
  const p = parent.game!;
  assert.equal(p.setName, 'ffight');
  assert.equal(p.kind, 'game');
  assert.match(p.validation.identifiedAs ?? '', /Final Fight \(World, set 1\)/);
  assert.equal(p.validation.matchedRoms, p.validation.totalRoms);
  assert.equal(p.validation.boot?.tried, true);
  assert.equal(p.validation.boot?.ok, false);
  assert.match(p.validation.boot!.detail, /blank screen/);
  assert.equal(p.status, 'needs_attention');

  // The clone was re-evaluated with the parent present: no longer waiting,
  // boot-tested with both zips (and, being garbage, fails cleanly).
  const c2 = shelf.get(gid, c.id)!;
  assert.deepEqual(c2.validation.missing, []);
  assert.equal(c2.validation.boot?.tried, true);
  assert.equal(c2.validation.boot?.ok, false);
  assert.match(c2.validation.boot!.detail, /blank screen/, 'the driver started: clone + parent files were complete');
  assert.equal(c2.status, 'needs_attention');
});

test('the computed Neo Geo BIOS requirement matches the real core', async () => {
  const v = new Validator({ timeoutMs: 60_000, maxConcurrent: 1, heapMb: 256 });
  const game = join(dataDir, 'ltorb-real.zip');
  const bios = join(dataDir, 'neogeo-real.zip');
  writeFileSync(game, synthZip('fbneo_neogeo', 'ltorb', (r) => !(r.t & BRF.BIOS)));
  writeFileSync(bios, synthZip('fbneo_neogeo', 'neogeo')); // only the 4 files the catalog says are required
  const job = (files: { path: string; source: string }[]) => ({ system: 'fbneo_neogeo' as const, coreDir: CORE_DIR, files, gamePath: '/roms/ltorb.zip', minFrames: 300, maxFrames: 600 });
  const without = await v.boot(job([{ path: '/roms/ltorb.zip', source: game }]));
  assert.equal(without.ok, false);
  assert.match(without.detail, /could not start/, 'FBNeo shows its error screen; the boot test is not fooled by it');
  assert.match(without.detail, /sm1\.sm1/);
  const withBios = await v.boot(job([{ path: '/roms/ltorb.zip', source: game }, { path: '/roms/neogeo.zip', source: bios }]));
  assert.equal(withBios.frames, 600, 'the driver started and ran');
  assert.equal(withBios.ok, false);
  assert.match(withBios.detail, /blank screen/);
});

test('FBNeo Neo Geo: game needs the neogeo BIOS; BIOS upload unblocks it; removal blocks it again', async () => {
  const fake = new FakeBoot();
  const shelf = new Shelf({ db, cfg: config(), groups, validator: fake });
  const gid = newGroup('neo');

  const game = await upload(shelf, gid, 1001, 'ltorb.zip', synthZip('fbneo_neogeo', 'ltorb', (r) => !(r.t & BRF.BIOS)));
  assert.equal(game.status, 'done', game.error);
  const g = game.game!;
  assert.equal(g.system, 'fbneo_neogeo');
  assert.equal(g.status, 'needs_dependency');
  assert.equal(g.biosSet, 'neogeo');
  assert.deepEqual(g.validation.missing, [{ kind: 'bios', set: 'neogeo', files: ['sp-s3.sp1', 'sm1.sm1', 'sfix.sfix', '000-lo.lo'] }]);
  assert.ok(g.validation.findings.some((f) => f.code === 'missing_bios'));
  assert.equal(fake.boots.length, 0);

  // Only the BIOS files the default (MVS Europe) mode requires.
  const bios = await upload(shelf, gid, 1002, 'neogeo.zip', synthZip('fbneo_neogeo', 'neogeo'));
  assert.equal(bios.status, 'done', bios.error);
  const b = bios.game!;
  assert.equal(b.kind, 'bios');
  assert.equal(b.status, 'ready');
  assert.equal(b.mode, null);
  assert.equal(b.setName, 'neogeo');
  await assert.rejects(shelf.sessionSpec(gid, b.id), (e: unknown) => e instanceof ShelfError && e.code === 'not_playable');

  // Re-evaluated: boot test ran with the game and the BIOS at session paths.
  assert.equal(fake.boots.length, 1);
  assert.deepEqual(fake.boots[0].files.map((f) => f.path), ['/roms/ltorb.zip', '/roms/neogeo.zip']);
  assert.equal(fake.boots[0].gamePath, '/roms/ltorb.zip');
  const ready = shelf.get(gid, g.id)!;
  assert.equal(ready.status, 'ready');
  assert.deepEqual(ready.validation.missing, []);

  const spec = await shelf.sessionSpec(gid, g.id);
  assert.deepEqual(spec.clientFiles.map((f) => f.path), ['/roms/ltorb.zip', '/roms/neogeo.zip']);
  assert.equal(spec.clientFiles[1].sha256, b.sha256);
  assert.equal(spec.options['fbneo-neogeo-mode'], 'MVS_EUR');
  assert.ok(spec.ports >= 1 && spec.ports <= 2);
  assert.ok(shelf.blobForGroup(gid, b.sha256), 'the BIOS is served as a session file');

  shelf.remove(gid, b.id, 1001);
  const blocked = shelf.get(gid, g.id)!;
  assert.equal(blocked.status, 'needs_dependency');
  assert.equal(blocked.validation.missing[0]?.set, 'neogeo');
});

test('fighters are versus; FBNeo mode/players are editable within the driver limits', async () => {
  const fake = new FakeBoot();
  const shelf = new Shelf({ db, cfg: config(), groups, validator: fake });
  const gid = newGroup('sf2');
  const r = await upload(shelf, gid, 1001, 'sf2.zip', synthZip('fbneo_cps12', 'sf2'));
  assert.equal(r.status, 'done', r.error);
  const g = r.game!;
  assert.equal(g.status, 'ready');
  assert.equal(g.mode, 'versus');
  assert.equal(g.players, 2);
  assert.equal(g.genre, 'Fighting');
  assert.equal(g.year, '1991');
  assert.equal(g.manufacturer, 'Capcom');

  const u = shelf.update(gid, g.id, 1001, { displayName: '  Street\u0007 Fighter II \u202e ', notes: 'Best of 3.\nNo cheese.', handoffRule: 'Winner stays' });
  assert.equal(u.displayName, 'Street Fighter II');
  assert.equal(u.metadata.notes, 'Best of 3.\nNo cheese.');
  assert.equal(u.metadata.handoffRule, 'Winner stays');
  assert.throws(() => shelf.update(gid, g.id, 1001, { displayName: '' }), ShelfError);
  assert.throws(() => shelf.update(gid, g.id, 1001, { displayName: 'x'.repeat(81) }), ShelfError);
  assert.throws(() => shelf.update(gid, g.id, 1001, { notes: 'n'.repeat(501) }), ShelfError);
  assert.throws(() => shelf.update(gid, g.id, 1001, { players: 3 }), /from 1 to 2/);
  assert.throws(() => shelf.update(gid, g.id, 1001, { mode: 'nonsense' as never }), /unknown mode/);
  assert.throws(() => shelf.update(gid, g.id, 1001, { mode: 'versus', players: 1 }), /at least 2/);
  // Request bodies are passed straight in: wrong types are errors, never a
  // silent "clear the field" or a crash.
  assert.throws(() => shelf.update(gid, g.id, 1001, null as never), (e: unknown) => e instanceof ShelfError && e.code === 'invalid');
  assert.throws(() => shelf.update(gid, g.id, 1001, { notes: 5 } as never), /notes must be text/);
  assert.throws(() => shelf.update(gid, g.id, 1001, { players: '1' } as never), /players must be a number/);
  assert.equal(shelf.get(gid, g.id)!.metadata.notes, 'Best of 3.\nNo cheese.', 'unchanged by rejected edits');
  // Seat count changes do not change the emulated arcade machine.
  const before = (await shelf.sessionSpec(gid, g.id)).compatKey;
  assert.equal(shelf.update(gid, g.id, 1001, { mode: 'single', players: 1 }).mode, 'single');
  assert.equal((await shelf.sessionSpec(gid, g.id)).ports, 1);
  assert.equal((await shelf.sessionSpec(gid, g.id)).compatKey, before);
  assert.equal(db.get("SELECT COUNT(*) AS n FROM audit_log WHERE group_id = ? AND action = 'shelf_update'", gid)!.n, 2);

  shelf.setFavorite(gid, 1002, g.id, true);
  assert.equal(shelf.list(gid, 1002)[0].favorite, true);
  assert.equal(shelf.list(gid, 1001)[0].favorite, false, 'favorites are per viewer');
  shelf.setFavorite(gid, 1002, g.id, false);
  assert.equal(shelf.get(gid, g.id, 1002)!.favorite, false);
  shelf.markPlayed(gid, g.id, 'working');
  const played = shelf.get(gid, g.id)!;
  assert.equal(played.playCount, 1);
  assert.equal(played.compat, 'working');
  assert.ok(played.lastPlayedAt);
});

test('removing a pinned game keeps its file until unpinned and the retention period has passed', async () => {
  const shelf = new Shelf({ db, cfg: config({ removedGameRetentionDays: 7 }), groups });
  const gid = newGroup('pins');
  const rom = readFileSync(join(ROMS, 'atc-solo.nes'));
  const r = await upload(shelf, gid, 1001, 'atc-solo.nes', rom);
  assert.equal(r.status, 'done', r.error);
  const g = r.game!;
  const blobPath = shelf.blobs.pathOf(g.sha256);
  const spec = await shelf.sessionSpec(gid, g.id);
  shelf.pin(gid, spec.clientFiles.map((f) => f.sha256));

  const t0 = Date.now();
  shelf.remove(gid, g.id, 1002);
  assert.equal(shelf.get(gid, g.id)!.status, 'removed');
  assert.equal(shelf.list(gid, 1001).length, 0);
  assert.ok(shelf.blobForGroup(gid, g.sha256), 'the running session can still fetch its file');
  assert.ok((await shelf.sessionSpec(gid, g.id)).files.length, 'and restart from it');
  assert.equal(db.get("SELECT COUNT(*) AS n FROM audit_log WHERE action = 'shelf_remove' AND target_id = ?", g.id)!.n, 1);

  // (cleanup is global: other tests' removed blobs may go, this one must stay)
  shelf.cleanup(t0 + 30 * DAY);
  assert.ok(existsSync(blobPath), 'pinned');

  shelf.unpin(gid);
  assert.equal(shelf.blobForGroup(gid, g.sha256), null, 'no longer served');
  await assert.rejects(shelf.sessionSpec(gid, g.id), (e: unknown) => e instanceof ShelfError && e.code === 'removed');
  shelf.cleanup(t0 + 1 * DAY);
  assert.ok(existsSync(blobPath), 'within retention');

  // A valid checkpoint also keeps it.
  db.run('INSERT INTO game_sessions (id, group_id, game_id, compat_key, fresh, started_at) VALUES (?, ?, ?, ?, 1, ?)', 777, gid, g.id, spec.compatKey, t0);
  const cp = db.run('INSERT INTO checkpoints (group_id, game_id, session_id, frame, file, compat_key, size, created_at) VALUES (?, ?, 777, 60, ?, ?, 1, ?)', gid, g.id, 'cp.bin', spec.compatKey, t0);
  shelf.cleanup(t0 + 8 * DAY);
  assert.ok(existsSync(blobPath), 'a valid checkpoint needs it');
  db.run('UPDATE checkpoints SET valid = 0 WHERE id = ?', cp.lastInsertRowid);

  const res = shelf.cleanup(t0 + 8 * DAY);
  assert.equal(res.deletedBlobs, 1);
  assert.equal(res.freedBytes, rom.length);
  assert.equal(existsSync(blobPath), false);
  assert.ok(db.get('SELECT 1 AS x FROM blobs WHERE sha256 = ?', g.sha256), 'row kept: the removed entry still references it');

  // Uploading it again revives the same entry (records keep their game id).
  const again = await upload(shelf, gid, 1001, 'atc-solo.nes', rom);
  assert.equal(again.status, 'done', again.error);
  assert.equal(again.game!.id, g.id);
  assert.equal(again.game!.status, 'ready');
  assert.ok(existsSync(blobPath));
});

test('validation runs in a separate process that is killed on timeout', async () => {
  const v = new Validator({ timeoutMs: 1000, maxConcurrent: 1, heapMb: 128 });
  const t0 = Date.now();
  // Booting a heavy arcade core cannot finish within one second.
  const zip = join(dataDir, 'ffight-timeout.zip');
  writeFileSync(zip, synthZip('fbneo_cps12', 'ffight'));
  const res = await v.boot({ system: 'fbneo_cps12', coreDir: CORE_DIR, files: [{ path: '/roms/ffight.zip', source: zip }], gamePath: '/roms/ffight.zip', minFrames: 100_000, maxFrames: 100_000 });
  assert.equal(res.ok, false);
  assert.match(res.detail, /did not finish/);
  assert.ok(Date.now() - t0 < 5000);
  assert.equal(v.stats.timeouts, 1);
});

test('a validator reply naming a file it was not given is refused; nothing is moved or stored', async () => {
  const victim = join(dataDir, 'victim.sqlite');
  writeFileSync(victim, 'server data that must never become a group file');
  const nes = parseNes(readFileSync(join(ROMS, 'atc-solo.nes'))).info!;
  // A compromised validation process trying to have the server move an
  // arbitrary file into the blob store (and serve it to the group).
  const evil: ValidatorLike = {
    async inspect(): Promise<InspectResult> {
      return { ok: true, format: 'nes', nes, romPath: victim, description: 'NES ROM', findings: [] };
    },
    async boot(): Promise<BootResult> {
      return { tried: true, ok: true, frames: 300, detail: 'fake boot' };
    },
  };
  const shelf = new Shelf({ db, cfg: config(), groups, validator: evil });
  const gid = newGroup('evil');
  const blobsBefore = db.get('SELECT COUNT(*) AS n FROM blobs')!.n;
  const r = await upload(shelf, gid, 1001, 'atc-solo.nes', readFileSync(join(ROMS, 'atc-solo.nes')));
  assert.equal(r.status, 'failed');
  assert.match(r.error!, /invalid validator reply/);
  assert.ok(existsSync(victim), 'the named file was not moved');
  assert.equal(readFileSync(victim, 'utf8'), 'server data that must never become a group file');
  assert.equal(shelf.list(gid, 1001).length, 0);
  assert.equal(db.get('SELECT COUNT(*) AS n FROM blobs')!.n, blobsBefore);
});

test('ingest for an unknown group still consumes the temp file', async () => {
  const shelf = new Shelf({ db, cfg: config(), groups, validator: new FakeBoot() });
  const tempPath = join(dataDir, 'orphan-upload.bin');
  writeFileSync(tempPath, 'x');
  await assert.rejects(shelf.ingest({ groupId: 987_654, userId: 1001, fileName: 'a.nes', source: 'web', tempPath, size: 1 }), /unknown group/);
  assert.equal(existsSync(tempPath), false);
});

test('the compat key follows the emulated controller hardware (NES Four Score)', async () => {
  const shelf = new Shelf({ db, cfg: config(), groups, validator: new FakeBoot() });
  const gid = newGroup('fourscore');
  const rom = Buffer.from(readFileSync(join(ROMS, 'atc-solo.nes')));
  rom[rom.length - 1] ^= 0xff; // different content (a CHR byte): no verified adapter applies
  const r = await upload(shelf, gid, 1001, 'homebrew.nes', rom);
  assert.equal(r.status, 'done', r.error);
  const g = r.game!;
  assert.equal(g.adapter, null);
  assert.equal(g.players, 2);
  const two = await shelf.sessionSpec(gid, g.id);
  assert.equal(two.ports, 2);
  shelf.update(gid, g.id, 1001, { players: 4 });
  const four = await shelf.sessionSpec(gid, g.id);
  assert.equal(four.ports, 4, 'Four Score session');
  assert.notEqual(four.compatKey, two.compatKey, 'a checkpoint made without the Four Score is not resumed with it (and records do not mix)');
  shelf.update(gid, g.id, 1001, { players: 2 });
  assert.equal((await shelf.sessionSpec(gid, g.id)).compatKey, two.compatKey);
});

test('changes made while a boot test runs are not overwritten by its result', async () => {
  const gated = new GatedBoot();
  const shelf = new Shelf({ db, cfg: config(), groups, validator: gated });
  const gid = newGroup('race');

  // 1. The BIOS a re-evaluation is booting with is removed meanwhile: the
  //    game must end up waiting for it again, not "ready" without it.
  const game = await upload(shelf, gid, 1001, 'ltorb.zip', synthZip('fbneo_neogeo', 'ltorb', (r) => !(r.t & BRF.BIOS)));
  assert.equal(game.game!.status, 'needs_dependency');
  gated.hold();
  const biosUpload = upload(shelf, gid, 1002, 'neogeo.zip', synthZip('fbneo_neogeo', 'neogeo'));
  await gated.started; // the game's boot test with the new BIOS is running
  const biosId = db.get("SELECT id FROM games WHERE group_id = ? AND kind = 'bios'", gid)!.id;
  shelf.remove(gid, biosId, 1001);
  gated.open();
  assert.equal((await biosUpload).status, 'done');
  const g = shelf.get(gid, game.game!.id)!;
  assert.equal(g.status, 'needs_dependency', 'the BIOS is gone again');
  assert.equal(g.validation.missing[0]?.set, 'neogeo');
  assert.equal(g.validation.boot, null);
  await assert.rejects(shelf.sessionSpec(gid, g.id), (e: unknown) => e instanceof ShelfError && e.code === 'not_ready');

  // 2. A session reports the game as working during a revalidation.
  const sf2 = await upload(shelf, gid, 1001, 'sf2.zip', synthZip('fbneo_cps12', 'sf2'));
  assert.equal(sf2.game!.status, 'ready');
  gated.hold();
  const again = shelf.revalidate(gid, sf2.game!.id);
  await gated.started;
  shelf.markPlayed(gid, sf2.game!.id, 'working');
  gated.open();
  await again;
  assert.equal(shelf.get(gid, sf2.game!.id)!.compat, 'working');
});

test('validation process failures are reported without server details', async () => {
  // A crashing boot test (here: the core cannot even be loaded).
  const v = new Validator({ timeoutMs: 30_000, maxConcurrent: 1, heapMb: 128 });
  const boot = await v.boot({ system: 'fceumm', coreDir: '/nonexistent-arcade-dir/cores', files: [], gamePath: '/roms/game.nes', minFrames: 1, maxFrames: 1 });
  assert.equal(boot.ok, false);
  assert.match(boot.detail, /crashed or failed/);
  assert.ok(!boot.detail.includes('/nonexistent-arcade-dir'), boot.detail);

  // A failing inspect job.
  const failing: ValidatorLike = {
    async inspect(): Promise<InspectResult> {
      throw new Error("ENOENT: no such file or directory, open '/srv/arcade/private/catalog.json'");
    },
    async boot(): Promise<BootResult> {
      return { tried: true, ok: true, frames: 300, detail: 'fake boot' };
    },
  };
  const shelf = new Shelf({ db, cfg: config(), groups, validator: failing });
  const r = await upload(shelf, newGroup('crash'), 1001, 'atc-solo.nes', readFileSync(join(ROMS, 'atc-solo.nes')));
  assert.equal(r.status, 'failed');
  assert.match(r.error!, /could not be validated \(internal error\)/);
  assert.ok(!r.error!.includes('/srv/arcade'), r.error);
});

test('re-uploading a file restores its missing or damaged blob (backup/restore recovery)', async () => {
  const shelf = new Shelf({ db, cfg: config(), groups, validator: new FakeBoot() });
  const gid = newGroup('restore');
  const rom = Buffer.from(readFileSync(join(ROMS, 'atc-solo.nes')));
  rom[rom.length - 2] ^= 0x5a; // content no other test stores
  const first = await upload(shelf, gid, 1001, 'restore.nes', rom);
  assert.equal(first.status, 'done', first.error);
  const g = first.game!;
  const blobPath = shelf.blobs.pathOf(g.sha256);

  // Missing (e.g. the database was restored without this file).
  rmSync(blobPath);
  await assert.rejects(shelf.sessionSpec(gid, g.id), (e: unknown) => e instanceof ShelfError && e.code === 'blob_missing');
  const again = await upload(shelf, gid, 1002, 'restore.nes', rom);
  assert.equal(again.duplicate, true);
  assert.equal(again.game!.id, g.id);
  assert.equal(Buffer.compare(readFileSync(blobPath), rom), 0, 'restored');
  assert.ok((await shelf.sessionSpec(gid, g.id)).files.length);

  // Damaged in place (same size, wrong bytes).
  writeFileSync(blobPath, Buffer.alloc(rom.length));
  await assert.rejects(shelf.sessionSpec(gid, g.id), (e: unknown) => e instanceof ShelfError && e.code === 'blob_corrupt');
  assert.equal((await upload(shelf, gid, 1001, 'restore.nes', rom)).duplicate, true);
  assert.equal(Buffer.compare(readFileSync(blobPath), rom), 0, 'repaired');
  assert.ok((await shelf.sessionSpec(gid, g.id)).files.length);
});
