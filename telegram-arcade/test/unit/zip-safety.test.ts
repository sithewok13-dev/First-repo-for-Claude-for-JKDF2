// The upload zip reader against hostile and malformed archives: archive
// bombs (ratio, declared size lies, overlapping entries), entry count limits,
// path traversal names, encryption, truncation, CRC mismatches, nested
// archives, header inconsistencies and zip64 parsing.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { crc32 } from 'node:zlib';
import { listEntries, readEntry, safeLabel, validateZip, ZipError, type ZipLimits } from '../../server/shelf/zip.ts';
import { forcedCrcData, makeZip } from '../helpers/zipgen.ts';

const LIMITS: ZipLimits = { maxEntries: 16, maxExtractedBytes: 32 * 1024 * 1024, maxCompressionRatio: 200 };

const text = (s: string) => new TextEncoder().encode(s);
const codes = (r: { findings: { code: string; level: string }[] }, level?: string) =>
  r.findings.filter((f) => !level || f.level === level).map((f) => f.code);

function rejects(buf: Buffer, code: string, limits = LIMITS): void {
  const r = validateZip(buf, limits);
  assert.equal(r.ok, false, `expected rejection with ${code}`);
  assert.ok(codes(r, 'error').includes(code), `expected ${code}, got ${codes(r).join(', ')}`);
  assert.deepEqual(r.entries, [], 'a rejected archive exposes no entries');
}

test('a well-formed archive (stored + deflated) validates and reads back', () => {
  const a = forcedCrcData(4096, 0x12345678);
  const zip = makeZip([
    { name: 'prog.bin', data: a },
    { name: 'readme.txt', data: text('hello hello hello hello'), method: 8 },
    { name: 'dir/', data: new Uint8Array(0) },
  ]);
  const r = validateZip(zip, LIMITS);
  assert.equal(r.ok, true, JSON.stringify(r.findings));
  assert.deepEqual(r.entries.map((e) => e.name), ['prog.bin', 'readme.txt'], 'directories are dropped');
  assert.equal(r.entries[0].crc32, 0x12345678);
  assert.deepEqual(Buffer.from(readEntry(zip, r.entries[1])).toString(), 'hello hello hello hello');
  assert.equal(r.totalUncompressed, 4096 + 23);
});

test('archive bomb: a highly compressed entry is rejected before decompression', () => {
  const zeros = new Uint8Array(8 * 1024 * 1024); // deflates ~1000:1
  rejects(makeZip([{ name: 'bomb.bin', data: zeros, method: 8 }]), 'zip_bomb');
  // Small blank files are fine (real romsets contain them)...
  const ok = validateZip(makeZip([{ name: 'blank.bin', data: new Uint8Array(64 * 1024), method: 8 }]), LIMITS);
  assert.equal(ok.ok, true);
  // ...unless the floor is lowered.
  rejects(makeZip([{ name: 'blank.bin', data: new Uint8Array(64 * 1024), method: 8 }]), 'zip_bomb', { ...LIMITS, ratioFloorBytes: 1024 });
});

test('declared total size above the extraction limit is rejected', () => {
  const zip = makeZip([
    { name: 'a.bin', data: new Uint8Array(10) },
    { name: 'b.bin', data: new Uint8Array(10), crc: 0, compressedSize: 10, uncompressedSize: 0x7ff00000 },
  ]);
  rejects(zip, 'too_large');
});

test('declared-size lies: data that inflates beyond or below its declared size', () => {
  const data = forcedCrcData(100_000, 0xcafebabe, 7);
  // Declares 1000 bytes; inflating stops at the declared size and fails.
  const under = makeZip([{ name: 'x.bin', data, method: 8, uncompressedSize: 1000 }]);
  rejects(under, 'size_mismatch');
  assert.throws(() => readEntry(under, listEntries(under)[0]), (e: unknown) => e instanceof ZipError && e.code === 'size_mismatch');
  // Declares more than it holds.
  rejects(makeZip([{ name: 'x.bin', data, method: 8, uncompressedSize: 200_000 }]), 'size_mismatch');
  // A stored entry whose sizes disagree.
  rejects(makeZip([{ name: 'x.bin', data: text('abcdef'), compressedSize: 6, uncompressedSize: 5 }]), 'size_mismatch');
});

test('too many entries are rejected before parsing them', () => {
  const many = Array.from({ length: 17 }, (_, i) => ({ name: `f${i}.bin`, data: text(String(i)) }));
  rejects(makeZip(many), 'too_many_entries');
  assert.throws(() => listEntries(makeZip(many), { maxEntries: 16 }), (e: unknown) => e instanceof ZipError && e.code === 'too_many_entries');
});

test('traversal, absolute, backslash, NUL and control-character names are reported', () => {
  const names = ['../../etc/passwd', '/abs/path.bin', 'dir\\win.bin', 'nul\u0000byte.bin', 'bell\u0007.bin', 'C:evil.bin'];
  const zip = makeZip(names.map((name, i) => ({ name, data: text(`file ${i}`) })));
  const r = validateZip(zip, LIMITS);
  assert.equal(r.ok, true, 'names alone do not reject: they are never used as paths');
  const unsafe = r.findings.filter((f) => f.code === 'unsafe_name');
  assert.equal(unsafe.length, names.length);
  assert.ok(unsafe.every((f) => f.level === 'warn'));
  assert.ok(unsafe.some((f) => f.message.includes('parent directory reference')));
  assert.ok(unsafe.some((f) => f.message.includes('absolute path')));
  assert.ok(unsafe.some((f) => f.message.includes('backslash')));
  assert.ok(unsafe.some((f) => f.message.includes('NUL byte')));
  assert.ok(unsafe.some((f) => f.message.includes('control character')));
  assert.ok(unsafe.some((f) => f.message.includes('drive letter')));
  assert.ok(unsafe.every((f) => !/[\u0000-\u001f]/.test(f.message)), 'messages are printable');
});

test('entry names cannot reorder the text of the messages they appear in', () => {
  const zip = makeZip([{ name: 'evil\u202egnp.exe', data: text('1') }, { name: 'rom.bin', data: text('2'), crc: 1 }, { name: 'x\u2066y\u2069.bin', data: text('3') }]);
  const r = validateZip(zip, LIMITS);
  assert.equal(r.ok, false);
  const unsafe = r.findings.filter((f) => f.code === 'unsafe_name');
  assert.equal(unsafe.length, 2, 'both bidi names are reported');
  assert.ok(unsafe.every((f) => f.message.includes('bidirectional control character')));
  for (const f of r.findings) assert.ok(!/[\u202a-\u202e\u2066-\u2069]/.test(f.message), f.message);
  assert.equal(safeLabel('a\u202eb'), 'a?b');
});

test('encrypted entries are rejected (traditional flag)', () => {
  const zip = makeZip([{ name: 'secret.bin', data: text('not really encrypted'), flags: 0x0801 }]);
  rejects(zip, 'encrypted');
  const e = listEntries(zip)[0];
  assert.equal(e.encrypted, true);
  assert.throws(() => readEntry(zip, e), (x: unknown) => x instanceof ZipError && x.code === 'encrypted');
});

test('unknown compression methods are rejected', () => {
  rejects(makeZip([{ name: 'a.bin', data: text('xyz'), method: 12 }]), 'unsupported_method'); // bzip2
  rejects(makeZip([{ name: 'a.bin', data: text('xyz'), method: 14 }]), 'unsupported_method'); // LZMA
});

test('truncated and malformed archives fail cleanly', () => {
  const good = makeZip([{ name: 'a.bin', data: forcedCrcData(2048, 1) }, { name: 'b.bin', data: forcedCrcData(2048, 2) }]);
  for (const cut of [1, 10, 22, 60, good.length - 100, good.length - 3000]) {
    const r = validateZip(good.subarray(0, good.length - cut), LIMITS);
    assert.equal(r.ok, false, `cut ${cut}`);
  }
  rejects(Buffer.alloc(10), 'zip_truncated');
  rejects(Buffer.from('PK\u0003\u0004 this is not really a zip file at all, just text'), 'zip_truncated');
  // Random garbage after a valid EOCD signature
  const junk = Buffer.alloc(4096);
  for (let i = 0; i < junk.length; i++) junk[i] = (i * 2654435761) >>> 24;
  junk.writeUInt32LE(0x06054b50, junk.length - 22);
  junk.writeUInt16LE(0, junk.length - 2);
  assert.equal(validateZip(junk, LIMITS).ok, false);
  // Prepended data (self-extractor style) shifts every offset.
  rejects(Buffer.concat([Buffer.alloc(100, 0x90), makeZip([{ name: 'a.bin', data: text('abc') }])]), 'zip_malformed');
  // (An archive whose offsets already account for a prefix is consistent and readable.)
  assert.equal(validateZip(makeZip([{ name: 'a.bin', data: text('abc') }], { prefix: Buffer.alloc(100, 0x90) }), LIMITS).ok, true);
  // Trailing data after the end record.
  rejects(Buffer.concat([good, Buffer.from('trailing')]), 'zip_truncated');
});

test('CRC mismatch is detected on read', () => {
  const zip = makeZip([{ name: 'bad.bin', data: text('some rom data'), crc: 0xdeadbeef }]);
  rejects(zip, 'crc_mismatch');
  const deflated = makeZip([{ name: 'bad.bin', data: forcedCrcData(5000, 9), method: 8, crc: 0x12345678 }]);
  rejects(deflated, 'crc_mismatch');
  assert.throws(() => readEntry(deflated, listEntries(deflated)[0]), (e: unknown) => e instanceof ZipError && e.code === 'crc_mismatch');
});

test('corrupt deflate data is reported, not thrown', () => {
  const data = forcedCrcData(5000, 3, 1);
  const zip = makeZip([{ name: 'x.bin', data, method: 8 }]);
  const lho = listEntries(zip)[0].dataOffset;
  zip.fill(0xff, lho, lho + 20);
  const r = validateZip(zip, LIMITS);
  assert.equal(r.ok, false);
  assert.ok(codes(r, 'error').some((c) => c === 'zip_corrupt_data' || c === 'size_mismatch' || c === 'crc_mismatch'));
});

test('nested archives are recorded and never opened', () => {
  const inner = makeZip([{ name: 'inner-secret.bin', data: text('inside') }]);
  const zip = makeZip([
    { name: 'game.bin', data: text('outer') },
    { name: 'inner.zip', data: inner },
    { name: 'disguised.dat', data: inner }, // archive by magic, not by name
  ]);
  const r = validateZip(zip, LIMITS);
  assert.equal(r.ok, true);
  assert.deepEqual(r.nested.sort(), ['disguised.dat', 'inner.zip']);
  assert.ok(codes(r, 'warn').includes('nested_archive'));
  assert.ok(!r.entries.some((e) => e.name === 'inner-secret.bin'), 'no recursion');
});

test('local header must agree with the central directory', () => {
  rejects(makeZip([{ name: 'a.bin', data: text('abc'), localName: 'b.bin' }]), 'header_mismatch');
  rejects(makeZip([{ name: 'a.bin', data: text('abc'), localCrc: 1234 }]), 'header_mismatch');
});

test('overlapping entries (non-recursive bomb) are rejected', () => {
  const big = forcedCrcData(10_000, 5);
  rejects(makeZip([{ name: 'a.bin', data: big }, { name: 'b.bin', data: big, localOffsetOf: 0, localName: 'a.bin' }]), 'header_mismatch');
  // Two central records naming the same local header and name.
  const zip = makeZip([{ name: 'a.bin', data: big }, { name: 'a.bin', data: big, localOffsetOf: 0 }]);
  rejects(zip, 'overlap');
});

test('duplicate names are rejected (also case-insensitively)', () => {
  rejects(makeZip([{ name: 'a.bin', data: text('1') }, { name: 'a.bin', data: text('2') }]), 'duplicate_name');
  rejects(makeZip([{ name: 'ROM.BIN', data: text('1') }, { name: 'rom.bin', data: text('2') }]), 'duplicate_name');
});

test('empty archives are rejected', () => {
  rejects(makeZip([]), 'empty_archive');
});

test('zip64 records and extra fields are parsed', () => {
  const a = forcedCrcData(70_000, 0x0badf00d);
  const zip = makeZip(
    [
      { name: 'a.bin', data: a },
      { name: 'b.txt', data: text('zip64 '.repeat(100)), method: 8 },
    ],
    { zip64: true },
  );
  const r = validateZip(zip, LIMITS);
  assert.equal(r.ok, true, JSON.stringify(r.findings));
  assert.ok(codes(r, 'info').includes('zip64'));
  assert.equal(r.entries.length, 2);
  assert.ok(r.entries.every((e) => e.zip64));
  assert.equal(r.entries[0].uncompressedSize, 70_000);
  assert.equal(r.entries[0].crc32, 0x0badf00d);
  assert.equal(crc32(readEntry(zip, r.entries[1])) >>> 0, r.entries[1].crc32);
  // A zip64 end record pointing outside the file is caught.
  const broken = Buffer.from(zip);
  const loc = broken.length - 22 - 20;
  broken.writeBigUInt64LE(BigInt(broken.length + 1000), loc + 8);
  assert.equal(validateZip(broken, LIMITS).ok, false);
});

// What FBNeo's minizip (src/burner/unzip.c) opens: it follows the LAST zip64
// locator signature in the final 64 KiB when that locator is usable, else the
// LAST end-record signature there; comment lengths are never checked.
function minizipEndRecord(buf: Buffer): { kind: 'zip64' | 'eocd'; at: number } | null {
  const from = Math.max(0, buf.length - 0xffff);
  const last = (sig: number) => {
    for (let p = buf.length - 4; p >= from; p--) if (buf.readUInt32LE(p) === sig) return p;
    return -1;
  };
  const loc = last(0x07064b50);
  if (loc >= 0 && loc + 20 <= buf.length && buf.readUInt32LE(loc + 4) === 0 && buf.readUInt32LE(loc + 16) === 1) {
    const target = Number(buf.readBigUInt64LE(loc + 8));
    if (target + 4 <= buf.length && buf.readUInt32LE(target) === 0x06064b50) return { kind: 'zip64', at: target };
  }
  const eocd = last(0x06054b50);
  return eocd >= 0 ? { kind: 'eocd', at: eocd } : null;
}

test('a second archive hidden in the comment (what the emulator would open) is rejected', () => {
  const outerEntries = [{ name: 'a.bin', data: forcedCrcData(300, 1) }];
  const outerEnd = makeZip(outerEntries).length - 22; // where the outer end record will sit

  // Variant 1: a complete archive in the comment, its own comment length
  // pointing past the end so a strict reader skips it; minizip does not.
  const hidden = makeZip([{ name: 'hidden.bin', data: new Uint8Array(1 << 20), method: 8 }]);
  hidden.writeUInt16LE(1, hidden.length - 2);
  const v1 = makeZip(outerEntries, { comment: hidden });
  assert.deepEqual(minizipEndRecord(v1), { kind: 'eocd', at: v1.length - 22 }, 'minizip would read the hidden archive');
  rejects(v1, 'zip_ambiguous');

  // Variant 2: only a zip64 locator + end record in the comment (minizip
  // looks for those first, with an absolute offset).
  const h64 = makeZip([{ name: 'hidden.bin', data: forcedCrcData(5000, 7) }], { zip64: true });
  const start = outerEnd + 22;
  const tail = Buffer.from(h64.subarray(0, h64.length - 22)); // drop its EOCD
  tail.writeBigUInt64LE(tail.readBigUInt64LE(tail.length - 12) + BigInt(start), tail.length - 12);
  const v2 = makeZip(outerEntries, { comment: tail });
  assert.equal(minizipEndRecord(v2)?.kind, 'zip64', 'minizip would follow the hidden zip64 records');
  rejects(v2, 'zip_ambiguous');

  // Ordinary text comments (e.g. TorrentZip's) are fine, and so is a stray
  // locator-like byte sequence minizip would ignore.
  assert.equal(validateZip(makeZip(outerEntries, { comment: 'TORRENTZIPPED-1A2B3C4D' }), LIMITS).ok, true);
  const stray = Buffer.from('PK\u0006\u0007 not a real locator', 'latin1');
  assert.equal(minizipEndRecord(makeZip(outerEntries, { comment: stray }))?.kind, 'eocd');
  assert.equal(validateZip(makeZip(outerEntries, { comment: stray }), LIMITS).ok, true);
});

test('zip64 locators must declare exactly one disk (as minizip requires)', () => {
  const zip = makeZip([{ name: 'a.bin', data: forcedCrcData(1000, 3) }], { zip64: true, totalDisks: 0 });
  // minizip would ignore these zip64 records and use the 32-bit fields.
  assert.equal(minizipEndRecord(zip)?.kind, 'eocd');
  rejects(zip, 'multi_disk');
});

test('data descriptor entries (flag bit 3) are accepted', () => {
  const data = forcedCrcData(3000, 0x42);
  // With a data descriptor the local header may carry zero CRC/sizes.
  const zip = makeZip([{ name: 'dd.bin', data, flags: 0x0808, localCrc: 0 }]);
  const r = validateZip(zip, LIMITS);
  assert.equal(r.ok, true, JSON.stringify(r.findings));
  assert.equal(r.entries[0].crc32, 0x42);
});
