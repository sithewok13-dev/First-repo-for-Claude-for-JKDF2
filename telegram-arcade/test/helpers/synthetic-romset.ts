// Builds SYNTHETIC FBNeo romsets for tests: every file has the exact name,
// size and CRC32 the driver expects, but its content is seeded pseudo-random
// bytes with the last 4 bytes chosen to force the CRC. No copyrighted data
// is involved. The emulated CPUs execute garbage, which still exercises the
// cores' hardware emulation, savestates and determinism.

import { deflateRawSync } from 'node:zlib';

const TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

// index of the table entry whose top byte is b (unique for CRC-32)
const TOPBYTE = (() => {
  const m = new Uint8Array(256);
  for (let i = 0; i < 256; i++) m[TABLE[i] >>> 24] = i;
  return m;
})();

export function crc32(data: Uint8Array, start = 0xffffffff): number {
  let c = start >>> 0;
  for (let i = 0; i < data.length; i++) c = TABLE[(c ^ data[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function rawState(data: Uint8Array): number {
  let c = 0xffffffff;
  for (let i = 0; i < data.length; i++) c = TABLE[(c ^ data[i]) & 0xff] ^ (c >>> 8);
  return c >>> 0;
}

// Returns `len` bytes whose CRC32 equals `target` (len >= 4).
export function forgeCrc(len: number, target: number, seed: number): Uint8Array {
  const out = new Uint8Array(len);
  let s = seed >>> 0 || 1;
  for (let i = 0; i < len - 4; i++) {
    s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0;
    out[i] = s & 0xff;
  }
  // Work backwards from the desired final register value.
  const want = (target ^ 0xffffffff) >>> 0;
  const idx: number[] = new Array(4);
  let reg = want;
  for (let k = 3; k >= 0; k--) {
    const i = TOPBYTE[reg >>> 24];
    idx[k] = i;
    reg = ((reg ^ TABLE[i]) << 8) >>> 0;
  }
  let c = rawState(out.subarray(0, len - 4));
  for (let k = 0; k < 4; k++) {
    out[len - 4 + k] = (idx[k] ^ c) & 0xff;
    c = (TABLE[idx[k]] ^ (c >>> 8)) >>> 0;
  }
  if (crc32(out) !== target >>> 0) throw new Error('crc forge failed');
  return out;
}

// Minimal zip writer (deflate or store).
export function makeZip(files: { name: string; data: Uint8Array }[], deflate = true): Uint8Array {
  const parts: Uint8Array[] = [];
  const central: Uint8Array[] = [];
  let offset = 0;
  for (const f of files) {
    const name = new TextEncoder().encode(f.name);
    const comp = deflate ? new Uint8Array(deflateRawSync(f.data)) : f.data;
    const crc = crc32(f.data);
    const lh = new Uint8Array(30 + name.length);
    const v = new DataView(lh.buffer);
    v.setUint32(0, 0x04034b50, true); v.setUint16(4, 20, true); v.setUint16(6, 0, true); v.setUint16(8, deflate ? 8 : 0, true);
    v.setUint32(14, crc, true); v.setUint32(18, comp.length, true); v.setUint32(22, f.data.length, true);
    v.setUint16(26, name.length, true); v.setUint16(28, 0, true);
    lh.set(name, 30);
    const ch = new Uint8Array(46 + name.length);
    const w = new DataView(ch.buffer);
    w.setUint32(0, 0x02014b50, true); w.setUint16(4, 20, true); w.setUint16(6, 20, true); w.setUint16(10, deflate ? 8 : 0, true);
    w.setUint32(16, crc, true); w.setUint32(20, comp.length, true); w.setUint32(24, f.data.length, true);
    w.setUint16(28, name.length, true); w.setUint32(42, offset, true);
    ch.set(name, 46);
    parts.push(lh, comp);
    central.push(ch);
    offset += lh.length + comp.length;
  }
  const cdSize = central.reduce((n, c) => n + c.length, 0);
  const end = new Uint8Array(22);
  const e = new DataView(end.buffer);
  e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true);
  e.setUint32(12, cdSize, true); e.setUint32(16, offset, true);
  const all = [...parts, ...central, end];
  const out = new Uint8Array(all.reduce((n, p) => n + p.length, 0));
  let o = 0;
  for (const p of all) { out.set(p, o); o += p.length; }
  return out;
}

export interface CatalogRom { n: string; s: number; c: number; t: number }
export interface CatalogDriver { name: string; parent: string; board: string; roms: CatalogRom[] }

const BRF_BIOS = 1 << 25, BRF_OPT = 1 << 27, BRF_NODUMP = 1 << 28;

// Synthetic zip containing every required file of a set (non-merged: a
// clone's zip also carries the parent files it needs).
export function syntheticSet(driver: CatalogDriver, seed = 1): Uint8Array {
  const files = driver.roms
    .filter((r) => !(r.t & (BRF_BIOS | BRF_OPT | BRF_NODUMP)) && r.s >= 4)
    .map((r, i) => ({ name: r.n, data: forgeCrc(r.s, r.c, seed * 7919 + i + 1) }));
  return makeZip(files);
}
