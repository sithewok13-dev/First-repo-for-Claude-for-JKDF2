// A tiny ZIP writer for tests. It can produce well-formed archives (stored or
// deflated, optionally zip64) and, through per-entry overrides, the kinds of
// lies a hostile archive tells: wrong CRCs or sizes, encryption flags,
// mismatched local headers, overlapping entries, unknown methods.
//
// Also provides CRC-32 forcing: synthetic test ROMs whose CRC and size match a
// catalog entry without containing any real ROM data.

import { crc32, deflateRawSync } from 'node:zlib';

export interface GenEntry {
  name: string;
  data: Uint8Array;
  method?: number;               // 0 stored (default), 8 deflate, or anything else (raw data copied)
  // Overrides written into BOTH headers unless the local* variants are set.
  crc?: number;
  compressedSize?: number;
  uncompressedSize?: number;
  flags?: number;
  localName?: string;            // a different name in the local header
  localCrc?: number;
  localOffsetOf?: number;        // central record points at another entry's local header
  zip64?: boolean;               // force zip64 extra fields for this entry
}

export interface GenOptions {
  zip64?: boolean;               // write zip64 end records (and zip64 extras on all entries)
  comment?: string | Uint8Array; // archive comment (raw bytes allowed: e.g. a second, hidden archive)
  prefix?: Uint8Array;           // junk before the first local header
  totalDisks?: number;           // zip64 locator's "total number of disks" (default 1)
}

function u16(n: number): Buffer {
  const b = Buffer.alloc(2);
  b.writeUInt16LE(n & 0xffff);
  return b;
}
function u32(n: number): Buffer {
  const b = Buffer.alloc(4);
  b.writeUInt32LE(n >>> 0);
  return b;
}
function u64(n: number): Buffer {
  const b = Buffer.alloc(8);
  b.writeBigUInt64LE(BigInt(n));
  return b;
}

export function makeZip(entries: GenEntry[], opts: GenOptions = {}): Buffer {
  const parts: Buffer[] = [];
  let offset = 0;
  const push = (b: Buffer) => {
    parts.push(b);
    offset += b.length;
  };
  if (opts.prefix) push(Buffer.from(opts.prefix));

  const central: Buffer[] = [];
  const localOffsets: number[] = [];
  for (const e of entries) {
    const method = e.method ?? 0;
    const payload = method === 8 ? deflateRawSync(e.data) : Buffer.from(e.data);
    const crc = e.crc ?? crc32(e.data);
    const csize = e.compressedSize ?? payload.length;
    const usize = e.uncompressedSize ?? e.data.length;
    const flags = e.flags ?? 0x0800; // UTF-8 names
    const nameBuf = Buffer.from(e.name, 'utf8');
    const localNameBuf = Buffer.from(e.localName ?? e.name, 'utf8');
    const z64 = !!(opts.zip64 || e.zip64);
    const lho = offset;
    localOffsets.push(lho);

    const localExtra = z64 ? Buffer.concat([u16(1), u16(16), u64(usize), u64(csize)]) : Buffer.alloc(0);
    push(
      Buffer.concat([
        u32(0x04034b50), u16(z64 ? 45 : 20), u16(flags), u16(method), u16(0), u16(0x21),
        u32(e.localCrc ?? crc), u32(z64 ? 0xffffffff : csize), u32(z64 ? 0xffffffff : usize),
        u16(localNameBuf.length), u16(localExtra.length), localNameBuf, localExtra,
      ]),
    );
    push(payload);

    const pointAt = e.localOffsetOf !== undefined ? localOffsets[e.localOffsetOf] : lho;
    const centralExtra = z64 ? Buffer.concat([u16(1), u16(24), u64(usize), u64(csize), u64(pointAt)]) : Buffer.alloc(0);
    central.push(
      Buffer.concat([
        u32(0x02014b50), u16(0x031e), u16(z64 ? 45 : 20), u16(flags), u16(method), u16(0), u16(0x21),
        u32(crc), u32(z64 ? 0xffffffff : csize), u32(z64 ? 0xffffffff : usize),
        u16(nameBuf.length), u16(centralExtra.length), u16(0), u16(0), u16(0), u32(0),
        u32(z64 ? 0xffffffff : pointAt), nameBuf, centralExtra,
      ]),
    );
  }

  const cdOffset = offset;
  for (const c of central) push(c);
  const cdSize = offset - cdOffset;
  const comment = typeof opts.comment === 'string' || opts.comment === undefined ? Buffer.from(opts.comment ?? '', 'utf8') : Buffer.from(opts.comment);
  if (opts.zip64) {
    const z64Offset = offset;
    push(
      Buffer.concat([
        u32(0x06064b50), u64(44), u16(45), u16(45), u32(0), u32(0),
        u64(entries.length), u64(entries.length), u64(cdSize), u64(cdOffset),
      ]),
    );
    push(Buffer.concat([u32(0x07064b50), u32(0), u64(z64Offset), u32(opts.totalDisks ?? 1)]));
    push(
      Buffer.concat([
        u32(0x06054b50), u16(0), u16(0), u16(0xffff), u16(0xffff), u32(0xffffffff), u32(0xffffffff), u16(comment.length), comment,
      ]),
    );
  } else {
    push(
      Buffer.concat([
        u32(0x06054b50), u16(0), u16(0), u16(entries.length), u16(entries.length), u32(cdSize), u32(cdOffset), u16(comment.length), comment,
      ]),
    );
  }
  return Buffer.concat(parts);
}

// ---------------------------------------------------------------- CRC forcing

const TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

// Index of the table entry whose top byte is b (unique for CRC-32).
const BY_TOP = (() => {
  const r = new Uint8Array(256);
  for (let i = 0; i < 256; i++) r[TABLE[i] >>> 24] = i;
  return r;
})();

// Returns `size` bytes (zeros except the last four) whose CRC-32 is `target`.
// The final register value after four bytes depends only on the four table
// indexes used, so they are found backwards from the target and the bytes
// that select them are then computed forwards from the prefix's register.
export function forcedCrcData(size: number, target: number, fill = 0): Uint8Array {
  if (size < 4) throw new Error('need at least 4 bytes to force a CRC');
  const out = new Uint8Array(size).fill(fill);
  const prefixCrc = crc32(out.subarray(0, size - 4));
  let x = (~target) >>> 0;
  const idx = [0, 0, 0, 0];
  for (let k = 3; k >= 0; k--) {
    const j = BY_TOP[x >>> 24];
    idx[k] = j;
    x = ((x ^ TABLE[j]) << 8) >>> 0;
  }
  let reg = (~prefixCrc) >>> 0;
  for (let k = 0; k < 4; k++) {
    out[size - 4 + k] = (reg ^ idx[k]) & 0xff;
    reg = (TABLE[idx[k]] ^ (reg >>> 8)) >>> 0;
  }
  if ((crc32(out) >>> 0) !== (target >>> 0)) throw new Error('CRC forcing failed');
  return out;
}
