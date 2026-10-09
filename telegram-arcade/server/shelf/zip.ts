// A small, defensive ZIP reader written for untrusted uploads.
//
// Only what arcade romsets need is supported: a single-disk archive (with
// zip64 records), entries stored (method 0) or deflated (method 8). Every
// offset and length read from the file is bounds-checked before use, the
// local header of each entry must agree with its central directory record,
// entries must not overlap, and decompression is bounded by the declared size
// and verified against the declared CRC-32.
//
// Nothing here ever writes to disk or uses an entry name as a path: names are
// decoded for display and matching only, and suspicious names are reported as
// findings. Nested archives are recorded, never opened.
//
// The validated bytes are later opened by the emulator itself (FBNeo's
// minizip, src/burner/unzip.c, on the server and in every browser), so the
// end records are located the way minizip locates them too, and an archive
// the two readers would see differently is rejected (see findDirectory).

import { crc32, inflateRawSync } from 'node:zlib';
import type { ValidationFinding } from './types.ts';

const SIG_LOCAL = 0x04034b50;
const SIG_CENTRAL = 0x02014b50;
const SIG_EOCD = 0x06054b50;
const SIG_ZIP64_EOCD = 0x06064b50;
const SIG_ZIP64_LOCATOR = 0x07064b50;

const EOCD_SIZE = 22;
const ZIP64_LOCATOR_SIZE = 20;
const ZIP64_EOCD_MIN = 56;
const CENTRAL_SIZE = 46;
const LOCAL_SIZE = 30;
const MAX_COMMENT = 0xffff;
// minizip searches only the last 0xffff bytes for its signatures
// (unz64local_SearchCentralDir and ...SearchCentralDir64, uMaxBack).
const MINIZIP_WINDOW = 0xffff;

const FLAG_ENCRYPTED = 1 << 0;
const FLAG_DATA_DESCRIPTOR = 1 << 3;
const FLAG_STRONG_ENCRYPTION = 1 << 6;
const FLAG_UTF8 = 1 << 11;

export const METHOD_STORED = 0;
export const METHOD_DEFLATE = 8;
const METHOD_AES = 99;

// Entries smaller than this are not subject to the compression ratio limit:
// tiny blank ROMs and PLD dumps legitimately compress far beyond any sane
// ratio, while an archive bomb needs volume to do harm.
export const DEFAULT_RATIO_FLOOR_BYTES = 1024 * 1024;

export interface ZipLimits {
  maxEntries: number;
  maxExtractedBytes: number;     // declared (and therefore actual) total uncompressed size
  maxCompressionRatio: number;   // per entry and for the whole archive
  ratioFloorBytes?: number;      // see DEFAULT_RATIO_FLOOR_BYTES
}

export interface ZipEntry {
  index: number;                 // position in the central directory
  name: string;                  // decoded for display/matching only; never used as a path
  rawName: Uint8Array;
  method: number;
  flags: number;
  crc32: number;                 // unsigned
  compressedSize: number;
  uncompressedSize: number;
  localHeaderOffset: number;
  dataOffset: number;            // first byte of the entry's (compressed) data
  isDirectory: boolean;
  encrypted: boolean;
  zip64: boolean;
}

export class ZipError extends Error {
  readonly code: string;
  constructor(code: string, message: string) {
    super(message);
    this.name = 'ZipError';
    this.code = code;
  }
}

// ---------------------------------------------------------------- helpers

function view(buf: Uint8Array): DataView {
  return new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
}

function need(buf: Uint8Array, offset: number, length: number, what: string): void {
  if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(length) || offset < 0 || length < 0 || offset + length > buf.length) {
    throw new ZipError('zip_truncated', `the archive is truncated or corrupt (${what} lies outside the file)`);
  }
}

function u64(dv: DataView, offset: number, what: string): number {
  const v = dv.getBigUint64(offset, true);
  if (v > BigInt(Number.MAX_SAFE_INTEGER)) throw new ZipError('zip_malformed', `the archive declares an impossible ${what}`);
  return Number(v);
}

function sameBytes(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

const utf8 = new TextDecoder('utf-8', { fatal: false });

// Names without the UTF-8 flag are nominally CP437. Plain ASCII is identical;
// anything else is decoded as Latin-1, which is good enough for display and
// for the extension checks done here (matching is by CRC, never by name).
function decodeName(raw: Uint8Array, flags: number): string {
  if (flags & FLAG_UTF8) return utf8.decode(raw);
  let s = '';
  for (let i = 0; i < raw.length; i++) s += String.fromCharCode(raw[i]);
  return s;
}

// Reads the zip64 "extended information" extra field (id 0x0001). Only the
// fields whose 32-bit counterparts are saturated are present, in this order.
function readZip64Extra(
  buf: Uint8Array,
  extraStart: number,
  extraLen: number,
  want: { uncompressed: boolean; compressed: boolean; offset: boolean },
): { uncompressed?: number; compressed?: number; offset?: number } | null {
  const dv = view(buf);
  let p = extraStart;
  const end = extraStart + extraLen;
  while (p + 4 <= end) {
    const id = dv.getUint16(p, true);
    const len = dv.getUint16(p + 2, true);
    const body = p + 4;
    if (body + len > end) throw new ZipError('zip_malformed', 'an entry has a malformed extra field');
    if (id === 0x0001) {
      const out: { uncompressed?: number; compressed?: number; offset?: number } = {};
      let q = body;
      const take = (what: string): number => {
        if (q + 8 > body + len) throw new ZipError('zip_malformed', 'an entry has a truncated zip64 extra field');
        const v = u64(dv, q, what);
        q += 8;
        return v;
      };
      if (want.uncompressed) out.uncompressed = take('uncompressed size');
      if (want.compressed) out.compressed = take('compressed size');
      if (want.offset) out.offset = take('header offset');
      return out;
    }
    p = body + len;
  }
  return null;
}

// ---------------------------------------------------------------- EOCD

interface Directory {
  entries: number;
  cdOffset: number;
  cdSize: number;
  cdEnd: number;          // where the central directory must end (the next record)
  zip64: boolean;
}

// Offset of the last `sig` that starts at or after `from`, or -1.
function lastSignature(buf: Uint8Array, sig: number, from: number): number {
  const dv = view(buf);
  for (let p = buf.length - 4; p >= Math.max(0, from); p--) if (dv.getUint32(p, true) === sig) return p;
  return -1;
}

// Whether minizip would follow a zip64 locator at `p`: it reads the locator,
// requires disk 0 and exactly one disk, and then requires the zip64 end
// record signature at the (absolute) offset the locator names.
function minizipTakesLocator(buf: Uint8Array, p: number): boolean {
  if (p < 0 || p + ZIP64_LOCATOR_SIZE > buf.length) return false;
  const dv = view(buf);
  if (dv.getUint32(p + 4, true) !== 0 || dv.getUint32(p + 16, true) !== 1) return false;
  const target = dv.getBigUint64(p + 8, true);
  if (target > BigInt(buf.length - 4)) return false;
  return dv.getUint32(Number(target), true) === SIG_ZIP64_EOCD;
}

function findDirectory(buf: Uint8Array): Directory {
  if (buf.length < EOCD_SIZE) throw new ZipError('zip_truncated', 'the file is too small to be a zip archive');
  const dv = view(buf);
  // The EOCD record sits at the very end, followed only by its comment. Scan
  // backwards and accept the first signature whose comment ends exactly at
  // the end of the file (no trailing data is tolerated).
  const lowest = Math.max(0, buf.length - EOCD_SIZE - MAX_COMMENT);
  let eocd = -1;
  for (let p = buf.length - EOCD_SIZE; p >= lowest; p--) {
    if (dv.getUint32(p, true) !== SIG_EOCD) continue;
    const commentLen = dv.getUint16(p + 20, true);
    if (p + EOCD_SIZE + commentLen === buf.length) {
      eocd = p;
      break;
    }
  }
  if (eocd < 0) throw new ZipError('zip_truncated', 'no end-of-central-directory record: the archive is truncated or not a zip file');

  // minizip (the emulator's reader) does not check comment lengths: it
  // follows the LAST zip64 locator signature in the final 64 KiB if that
  // locator is usable, else the LAST end-record signature there, and it
  // tolerates data before the archive. A second archive hidden in the
  // comment would therefore be what the emulator loads while this module
  // validated the first. Only archives both readers see alike are accepted.
  const ambiguous = (why: string) => new ZipError('zip_ambiguous', `the archive's end records are ambiguous (${why}); re-pack it with a standard zip tool`);
  const windowStart = Math.max(0, buf.length - MINIZIP_WINDOW);
  if (eocd < windowStart) throw ambiguous('the archive comment is too long');
  if (lastSignature(buf, SIG_EOCD, eocd + 1) >= 0) throw ambiguous('a second end record follows the first');
  const locator = eocd - ZIP64_LOCATOR_SIZE;
  const ownLocator = locator >= 0 && dv.getUint32(locator, true) === SIG_ZIP64_LOCATOR ? locator : -1;
  const lastLocator = lastSignature(buf, SIG_ZIP64_LOCATOR, windowStart);
  if (lastLocator !== ownLocator && (ownLocator >= 0 || minizipTakesLocator(buf, lastLocator))) {
    throw ambiguous('a zip64 locator appears where it does not belong');
  }

  const disk = dv.getUint16(eocd + 4, true);
  const cdDisk = dv.getUint16(eocd + 6, true);
  let diskEntries = dv.getUint16(eocd + 8, true);
  let entries = dv.getUint16(eocd + 10, true);
  let cdSize = dv.getUint32(eocd + 12, true);
  let cdOffset = dv.getUint32(eocd + 16, true);
  let cdEnd = eocd;
  let zip64 = false;

  const saturated = diskEntries === 0xffff || entries === 0xffff || cdSize === 0xffffffff || cdOffset === 0xffffffff;
  if (ownLocator >= 0) {
    zip64 = true;
    const z64Disk = dv.getUint32(locator + 4, true);
    const z64Offset = u64(dv, locator + 8, 'zip64 record offset');
    const totalDisks = dv.getUint32(locator + 16, true);
    // Exactly one disk, as minizip requires (with any other count it would
    // ignore the zip64 records and read the 32-bit fields instead).
    if (z64Disk !== 0 || totalDisks !== 1) throw new ZipError('multi_disk', 'multi-part (spanned) archives are not supported');
    need(buf, z64Offset, ZIP64_EOCD_MIN, 'zip64 end record');
    if (dv.getUint32(z64Offset, true) !== SIG_ZIP64_EOCD) throw new ZipError('zip_malformed', 'the zip64 end record is missing or corrupt');
    const recordSize = u64(dv, z64Offset + 4, 'zip64 record size');
    if (recordSize < ZIP64_EOCD_MIN - 12 || z64Offset + 12 + recordSize !== locator) {
      throw new ZipError('zip_malformed', 'the zip64 end record has an inconsistent size');
    }
    const d = dv.getUint32(z64Offset + 16, true);
    const cd = dv.getUint32(z64Offset + 20, true);
    if (d !== 0 || cd !== 0) throw new ZipError('multi_disk', 'multi-part (spanned) archives are not supported');
    diskEntries = u64(dv, z64Offset + 24, 'entry count');
    entries = u64(dv, z64Offset + 32, 'entry count');
    cdSize = u64(dv, z64Offset + 40, 'directory size');
    cdOffset = u64(dv, z64Offset + 48, 'directory offset');
    cdEnd = z64Offset;
    // minizip treats an entry count of 0xffff as "unknown" and keeps reading
    // records past the directory (unzGoToNextFile).
    if (entries === 0xffff) throw ambiguous('an entry count of 65535 is read differently by other tools');
  } else if (saturated) {
    throw new ZipError('zip_malformed', 'the archive needs zip64 records but they are missing');
  } else if (disk !== 0 || cdDisk !== 0) {
    throw new ZipError('multi_disk', 'multi-part (spanned) archives are not supported');
  }
  if (diskEntries !== entries) throw new ZipError('multi_disk', 'multi-part (spanned) archives are not supported');
  // The central directory must end exactly where the end records begin.
  // This rejects prepended data (self-extractors), gaps and overlaps.
  if (cdOffset + cdSize !== cdEnd) {
    throw new ZipError('zip_malformed', 'the central directory is not where the archive says it is (corrupt, truncated or has prepended data)');
  }
  need(buf, cdOffset, cdSize, 'central directory');
  return { entries, cdOffset, cdSize, cdEnd, zip64 };
}

// ---------------------------------------------------------------- listing

// Parses and structurally verifies the archive. Throws ZipError when the
// archive is malformed, too large in entry count, or internally inconsistent.
// Encryption, methods, sizes, ratios and names are judged by validateZip().
export function listEntries(buf: Uint8Array, limits: Partial<ZipLimits> = {}): ZipEntry[] {
  const dir = findDirectory(buf);
  const maxEntries = limits.maxEntries ?? 65535;
  if (dir.entries > maxEntries) {
    throw new ZipError('too_many_entries', `the archive has ${dir.entries} entries; at most ${maxEntries} are allowed`);
  }
  // Each central record is at least 46 bytes: a declared count that cannot
  // fit is a lie, caught before any per-entry work.
  if (dir.entries * CENTRAL_SIZE > dir.cdSize) throw new ZipError('zip_malformed', 'the central directory is smaller than its entry count requires');

  const dv = view(buf);
  const entries: ZipEntry[] = [];
  let p = dir.cdOffset;
  const cdLimit = dir.cdOffset + dir.cdSize;
  for (let i = 0; i < dir.entries; i++) {
    if (p + CENTRAL_SIZE > cdLimit) throw new ZipError('zip_malformed', 'the central directory ends early');
    if (dv.getUint32(p, true) !== SIG_CENTRAL) throw new ZipError('zip_malformed', 'the central directory is corrupt');
    const flags = dv.getUint16(p + 8, true);
    const method = dv.getUint16(p + 10, true);
    const crc = dv.getUint32(p + 16, true);
    let compressed = dv.getUint32(p + 20, true);
    let uncompressed = dv.getUint32(p + 24, true);
    const nameLen = dv.getUint16(p + 28, true);
    const extraLen = dv.getUint16(p + 30, true);
    const commentLen = dv.getUint16(p + 32, true);
    const diskStart = dv.getUint16(p + 34, true);
    let lho = dv.getUint32(p + 42, true);
    const varEnd = p + CENTRAL_SIZE + nameLen + extraLen + commentLen;
    if (varEnd > cdLimit) throw new ZipError('zip_malformed', 'a central directory record runs past the directory');
    const rawName = buf.subarray(p + CENTRAL_SIZE, p + CENTRAL_SIZE + nameLen);

    const want = { uncompressed: uncompressed === 0xffffffff, compressed: compressed === 0xffffffff, offset: lho === 0xffffffff };
    let zip64 = false;
    if (want.uncompressed || want.compressed || want.offset) {
      const z = readZip64Extra(buf, p + CENTRAL_SIZE + nameLen, extraLen, want);
      if (!z) throw new ZipError('zip_malformed', 'an entry needs a zip64 extra field but has none');
      if (z.uncompressed !== undefined) uncompressed = z.uncompressed;
      if (z.compressed !== undefined) compressed = z.compressed;
      if (z.offset !== undefined) lho = z.offset;
      zip64 = true;
    }
    if (diskStart !== 0 && diskStart !== 0xffff) throw new ZipError('multi_disk', 'multi-part (spanned) archives are not supported');

    const name = decodeName(rawName, flags);
    entries.push({
      index: i,
      name,
      rawName,
      method,
      flags,
      crc32: crc >>> 0,
      compressedSize: compressed,
      uncompressedSize: uncompressed,
      localHeaderOffset: lho,
      dataOffset: -1,
      isDirectory: name.endsWith('/') && uncompressed === 0 && compressed <= 2,
      encrypted: (flags & (FLAG_ENCRYPTED | FLAG_STRONG_ENCRYPTION)) !== 0 || method === METHOD_AES,
      zip64,
    });
    p = varEnd;
  }
  if (p !== cdLimit) throw new ZipError('zip_malformed', 'the central directory has trailing data');

  for (const e of entries) verifyLocalHeader(buf, e, dir.cdOffset);
  checkOverlaps(entries);
  return entries;
}

// The local header must exist, lie before the central directory, and agree
// with the central record on name, method, encryption and (unless a data
// descriptor is used) CRC and sizes. Sets e.dataOffset.
function verifyLocalHeader(buf: Uint8Array, e: ZipEntry, cdOffset: number): void {
  const dv = view(buf);
  const p = e.localHeaderOffset;
  if (p + LOCAL_SIZE > cdOffset) throw new ZipError('zip_malformed', `entry #${e.index + 1} points outside the archive data`);
  if (dv.getUint32(p, true) !== SIG_LOCAL) throw new ZipError('zip_malformed', `entry #${e.index + 1} has no valid local header`);
  const flags = dv.getUint16(p + 6, true);
  const method = dv.getUint16(p + 8, true);
  const crc = dv.getUint32(p + 14, true) >>> 0;
  let compressed = dv.getUint32(p + 18, true);
  let uncompressed = dv.getUint32(p + 22, true);
  const nameLen = dv.getUint16(p + 26, true);
  const extraLen = dv.getUint16(p + 28, true);
  const dataOffset = p + LOCAL_SIZE + nameLen + extraLen;
  if (dataOffset > cdOffset) throw new ZipError('zip_malformed', `entry #${e.index + 1} has a local header that runs into the central directory`);

  const localName = buf.subarray(p + LOCAL_SIZE, p + LOCAL_SIZE + nameLen);
  const mismatch = (what: string) => new ZipError('header_mismatch', `entry #${e.index + 1}: local header and central directory disagree (${what})`);
  if (!sameBytes(localName, e.rawName)) throw mismatch('name');
  if (method !== e.method) throw mismatch('method');
  const encBits = FLAG_ENCRYPTED | FLAG_STRONG_ENCRYPTION | FLAG_DATA_DESCRIPTOR;
  if ((flags & encBits) !== (e.flags & encBits)) throw mismatch('flags');

  if (!(flags & FLAG_DATA_DESCRIPTOR)) {
    if (compressed === 0xffffffff || uncompressed === 0xffffffff) {
      const z = readZip64Extra(buf, p + LOCAL_SIZE + nameLen, extraLen, {
        uncompressed: uncompressed === 0xffffffff,
        compressed: compressed === 0xffffffff,
        offset: false,
      });
      if (!z) throw mismatch('zip64 sizes');
      if (z.uncompressed !== undefined) uncompressed = z.uncompressed;
      if (z.compressed !== undefined) compressed = z.compressed;
    }
    if (crc !== e.crc32) throw mismatch('CRC');
    if (compressed !== e.compressedSize || uncompressed !== e.uncompressedSize) throw mismatch('sizes');
  }
  if (dataOffset + e.compressedSize > cdOffset) throw new ZipError('zip_malformed', `entry #${e.index + 1} data runs past the end of the archive data`);
  e.dataOffset = dataOffset;
}

// Each entry owns [local header, end of data). Overlapping ranges are how
// "non-recursive" zip bombs reuse one compressed stream many times.
function checkOverlaps(entries: ZipEntry[]): void {
  const ranges = entries.map((e) => ({ start: e.localHeaderOffset, end: e.dataOffset + e.compressedSize, index: e.index }));
  ranges.sort((a, b) => a.start - b.start);
  for (let i = 1; i < ranges.length; i++) {
    if (ranges[i].start < ranges[i - 1].end) {
      throw new ZipError('overlap', `entries #${ranges[i - 1].index + 1} and #${ranges[i].index + 1} overlap (a crafted archive)`);
    }
  }
}

// ---------------------------------------------------------------- reading

// Returns the entry's bytes. Decompression never produces more than the
// declared size, and the result must match the declared size and CRC-32.
// For stored entries the result is a view into `buf`.
export function readEntry(buf: Uint8Array, entry: ZipEntry): Uint8Array {
  if (entry.encrypted) throw new ZipError('encrypted', `"${safeLabel(entry.name)}" is encrypted`);
  if (entry.dataOffset < 0) throw new ZipError('zip_malformed', 'entry was not verified');
  need(buf, entry.dataOffset, entry.compressedSize, 'entry data');
  const src = buf.subarray(entry.dataOffset, entry.dataOffset + entry.compressedSize);
  let out: Uint8Array;
  if (entry.method === METHOD_STORED) {
    if (entry.compressedSize !== entry.uncompressedSize) {
      throw new ZipError('size_mismatch', `"${safeLabel(entry.name)}" is stored but its sizes disagree`);
    }
    out = src;
  } else if (entry.method === METHOD_DEFLATE) {
    try {
      out = inflateRawSync(src, { maxOutputLength: Math.max(1, entry.uncompressedSize) });
    } catch (e) {
      const code = (e as { code?: string }).code;
      if (code === 'ERR_BUFFER_TOO_LARGE' || e instanceof RangeError) {
        throw new ZipError('size_mismatch', `"${safeLabel(entry.name)}" expands beyond its declared size`);
      }
      throw new ZipError('zip_corrupt_data', `"${safeLabel(entry.name)}" has corrupt compressed data`);
    }
  } else {
    throw new ZipError('unsupported_method', `"${safeLabel(entry.name)}" uses compression method ${entry.method}; only stored and deflate are supported`);
  }
  if (out.length !== entry.uncompressedSize) {
    throw new ZipError('size_mismatch', `"${safeLabel(entry.name)}" is ${out.length} bytes but declares ${entry.uncompressedSize}`);
  }
  if ((crc32(out) >>> 0) !== entry.crc32) {
    throw new ZipError('crc_mismatch', `"${safeLabel(entry.name)}" fails its CRC-32 check (corrupt archive)`);
  }
  return out;
}

// ---------------------------------------------------------------- policy

// Names are shown to people and matched by extension, never used as paths.
// Still, a romset never needs any of these, so they are reported.
export function nameIssues(name: string, raw?: Uint8Array): string[] {
  const issues: string[] = [];
  if (name.length === 0) issues.push('empty name');
  if (raw ? raw.includes(0) : name.includes('\0')) issues.push('NUL byte');
  if (/[\u0001-\u001f\u007f-\u009f]/.test(name)) issues.push('control character');
  if (/[\u200e\u200f\u202a-\u202e\u2066-\u2069]/.test(name)) issues.push('bidirectional control character');
  if (name.includes('\\')) issues.push('backslash');
  if (name.startsWith('/')) issues.push('absolute path');
  if (/^[A-Za-z]:/.test(name)) issues.push('drive letter');
  if (name.split(/[\\/]/).some((seg) => seg === '..')) issues.push('parent directory reference');
  if (name.includes('\ufffd')) issues.push('invalid UTF-8');
  if (name.length > 255) issues.push('very long name');
  return issues;
}

// Shortened, printable label for messages shown to people. Bidi controls
// are replaced too: an entry name must not reorder the text around it.
export function safeLabel(name: string): string {
  const s = name.replace(/[\u0000-\u001f\u007f-\u009f\u200e\u200f\u202a-\u202e\u2066-\u2069\u2028\u2029]/g, '?');
  return s.length > 80 ? s.slice(0, 77) + '...' : s;
}

const ARCHIVE_EXT = /\.(zip|7z|rar|gz|tgz|tar|bz2|xz|zst|lzh|lha|cab|arj)$/i;

function looksLikeArchive(data: Uint8Array): boolean {
  const b = data;
  if (b.length >= 4 && b[0] === 0x50 && b[1] === 0x4b && (b[2] === 3 || b[2] === 5 || b[2] === 7)) return true; // PK..
  if (b.length >= 6 && b[0] === 0x37 && b[1] === 0x7a && b[2] === 0xbc && b[3] === 0xaf && b[4] === 0x27 && b[5] === 0x1c) return true; // 7z
  if (b.length >= 4 && b[0] === 0x52 && b[1] === 0x61 && b[2] === 0x72 && b[3] === 0x21) return true; // Rar!
  if (b.length >= 2 && b[0] === 0x1f && b[1] === 0x8b) return true; // gzip
  if (b.length >= 3 && b[0] === 0x42 && b[1] === 0x5a && b[2] === 0x68) return true; // bzip2
  if (b.length >= 6 && b[0] === 0xfd && b[1] === 0x37 && b[2] === 0x7a && b[3] === 0x58 && b[4] === 0x5a && b[5] === 0x00) return true; // xz
  return false;
}

export interface ZipReport {
  ok: boolean;                    // false if any finding has level 'error'
  entries: ZipEntry[];            // files only (directories dropped); empty when not ok structurally
  findings: ValidationFinding[];
  nested: string[];               // names of entries that are archives themselves (never opened)
  totalUncompressed: number;
}

// Full safety validation. Structural problems, encryption, unsupported
// methods, duplicate names, size/ratio limits, CRC and size mismatches are
// errors; suspicious names and nested archives are reported as warnings.
// Every entry is decompressed (one at a time, bounded) to prove that the
// declared sizes and CRCs are true.
export function validateZip(buf: Uint8Array, limits: ZipLimits): ZipReport {
  const findings: ValidationFinding[] = [];
  const err = (code: string, message: string) => findings.push({ level: 'error', code, message });
  const fail = (): ZipReport => ({ ok: false, entries: [], findings, nested: [], totalUncompressed: 0 });

  let all: ZipEntry[];
  try {
    all = listEntries(buf, limits);
  } catch (e) {
    if (e instanceof ZipError) {
      err(e.code, e.message);
      return fail();
    }
    throw e;
  }
  if (all.some((e) => e.zip64)) findings.push({ level: 'info', code: 'zip64', message: 'the archive uses zip64 records' });
  const files = all.filter((e) => !e.isDirectory);
  if (files.length === 0) {
    err('empty_archive', 'the archive contains no files');
    return fail();
  }

  // Names: duplicates are errors (ambiguous content), odd names are warnings.
  const seen = new Set<string>();
  for (const e of all) {
    const key = e.name.normalize('NFC').toLowerCase();
    if (seen.has(key)) err('duplicate_name', `the archive contains "${safeLabel(e.name)}" more than once`);
    seen.add(key);
    const issues = nameIssues(e.name, e.rawName);
    if (issues.length) {
      findings.push({ level: 'warn', code: 'unsafe_name', message: `suspicious entry name "${safeLabel(e.name)}" (${issues.join(', ')}); it is never used as a path` });
    }
  }

  // Encryption and methods.
  for (const e of files) {
    if (e.encrypted) err('encrypted', `"${safeLabel(e.name)}" is encrypted; password-protected archives are not supported`);
    else if (e.method !== METHOD_STORED && e.method !== METHOD_DEFLATE) {
      err('unsupported_method', `"${safeLabel(e.name)}" uses compression method ${e.method}; re-pack the zip with standard (deflate) compression`);
    }
  }

  // Declared sizes and ratios, before anything is decompressed.
  const floor = limits.ratioFloorBytes ?? DEFAULT_RATIO_FLOOR_BYTES;
  let totalU = 0;
  let totalC = 0;
  for (const e of files) {
    totalU += e.uncompressedSize;
    totalC += e.compressedSize;
    if (e.uncompressedSize > floor && e.uncompressedSize / Math.max(1, e.compressedSize) > limits.maxCompressionRatio) {
      err('zip_bomb', `"${safeLabel(e.name)}" claims a compression ratio above ${limits.maxCompressionRatio}:1 (archive bomb protection)`);
    }
  }
  if (totalU > limits.maxExtractedBytes) {
    err('too_large', `the archive would extract to ${totalU} bytes; the limit is ${limits.maxExtractedBytes}`);
  }
  if (totalU > floor && totalU / Math.max(1, totalC) > limits.maxCompressionRatio) {
    err('zip_bomb', `the archive's overall compression ratio exceeds ${limits.maxCompressionRatio}:1 (archive bomb protection)`);
  }
  if (findings.some((f) => f.level === 'error')) return fail();

  // Prove declared sizes and CRCs, one bounded entry at a time.
  const nested: string[] = [];
  for (const e of files) {
    let data: Uint8Array;
    try {
      data = readEntry(buf, e);
    } catch (x) {
      if (x instanceof ZipError) {
        err(x.code, x.message);
        continue;
      }
      throw x;
    }
    if (ARCHIVE_EXT.test(e.name) || looksLikeArchive(data)) nested.push(e.name);
  }
  if (nested.length) {
    findings.push({
      level: 'warn',
      code: 'nested_archive',
      message: `contains ${nested.length} archive(s) inside the zip (${nested.slice(0, 3).map(safeLabel).join(', ')}${nested.length > 3 ? ', ...' : ''}); nested archives are never opened`,
    });
  }
  const ok = !findings.some((f) => f.level === 'error');
  return { ok, entries: ok ? files : [], findings, nested, totalUncompressed: ok ? totalU : 0 };
}
