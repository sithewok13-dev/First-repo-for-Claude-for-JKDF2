// Wire protocol between the arcade server and clients (WebSocket).
//
// Hot-path messages are binary; everything else is JSON text frames.
//
// Lockstep model: the server is the clock. Every emulated frame it emits one
// authoritative input record (a 16-bit RetroPad mask per port). Clients and the
// server's own emulation worker run the same WebAssembly core over the same
// records, so every replica computes the same game. Clients never send game
// state, only their own controller mask.

export const PROTOCOL_VERSION = 1;

export const Bin = {
  Input: 1,      // client -> server: controller mask for my seat
  Frames: 2,     // server -> client: authoritative input records
  Snapshot: 3,   // server -> client: savestate to (re)join the lockstep
  Hash: 4,       // server -> client: authoritative state hash at a frame
} as const;

// Frame numbering: S(f) is the emulated state after f frames have run. The
// record for frame f holds the inputs used to advance S(f) -> S(f+1). A
// snapshot or hash "at frame f" describes S(f).

// ---------------------------------------------------------------- input
// [u8 type][u8 port][u16 epoch][u32 seq][u16 mask][u32 clientMs]  = 14 bytes
export interface InputMsg {
  port: number;
  epoch: number;
  seq: number;
  mask: number;
  clientMs: number;
}

export function encodeInput(m: InputMsg): ArrayBuffer {
  const b = new ArrayBuffer(14);
  const v = new DataView(b);
  v.setUint8(0, Bin.Input);
  v.setUint8(1, m.port);
  v.setUint16(2, m.epoch & 0xffff, true);
  v.setUint32(4, m.seq >>> 0, true);
  v.setUint16(8, m.mask & 0xffff, true);
  v.setUint32(10, m.clientMs >>> 0, true);
  return b;
}

export function decodeInput(buf: Uint8Array): InputMsg | null {
  if (buf.byteLength !== 14 || buf[0] !== Bin.Input) return null;
  const v = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  return {
    port: v.getUint8(1),
    epoch: v.getUint16(2, true),
    seq: v.getUint32(4, true),
    mask: v.getUint16(8, true),
    clientMs: v.getUint32(10, true),
  };
}

// ---------------------------------------------------------------- frames
// [u8 type][u32 session][u32 firstFrame][u8 count][u8 ports]
//   count x ports x u16 masks
//   [u8 nacks] nacks x ([u8 port][u32 seq][u32 frame][u32 clientMs])
export interface InputAck {
  port: number;
  seq: number;      // client's input sequence number now in effect
  frame: number;    // first frame that carries it
  clientMs: number; // echoed client timestamp (for round-trip measurement)
}

export interface FramesMsg {
  session: number;
  firstFrame: number;
  ports: number;
  masks: Uint16Array; // count * ports
  acks: InputAck[];
}

export function encodeFrames(m: FramesMsg): Uint8Array {
  const count = m.masks.length / m.ports;
  const size = 11 + m.masks.length * 2 + 1 + m.acks.length * 13;
  const b = new Uint8Array(size);
  const v = new DataView(b.buffer);
  v.setUint8(0, Bin.Frames);
  v.setUint32(1, m.session >>> 0, true);
  v.setUint32(5, m.firstFrame >>> 0, true);
  v.setUint8(9, count);
  v.setUint8(10, m.ports);
  let o = 11;
  for (let i = 0; i < m.masks.length; i++, o += 2) v.setUint16(o, m.masks[i], true);
  v.setUint8(o++, m.acks.length);
  for (const a of m.acks) {
    v.setUint8(o, a.port);
    v.setUint32(o + 1, a.seq >>> 0, true);
    v.setUint32(o + 5, a.frame >>> 0, true);
    v.setUint32(o + 9, a.clientMs >>> 0, true);
    o += 13;
  }
  return b;
}

export function decodeFrames(buf: Uint8Array): FramesMsg | null {
  if (buf.byteLength < 12 || buf[0] !== Bin.Frames) return null;
  const v = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const count = v.getUint8(9);
  const ports = v.getUint8(10);
  let o = 11;
  if (buf.byteLength < o + count * ports * 2 + 1) return null;
  const masks = new Uint16Array(count * ports);
  for (let i = 0; i < masks.length; i++, o += 2) masks[i] = v.getUint16(o, true);
  const nacks = v.getUint8(o++);
  if (buf.byteLength < o + nacks * 13) return null;
  const acks: InputAck[] = [];
  for (let i = 0; i < nacks; i++, o += 13) {
    acks.push({ port: v.getUint8(o), seq: v.getUint32(o + 1, true), frame: v.getUint32(o + 5, true), clientMs: v.getUint32(o + 9, true) });
  }
  return { session: v.getUint32(1, true), firstFrame: v.getUint32(5, true), ports, masks, acks };
}

// ---------------------------------------------------------------- snapshot
// [u8 type][u32 session][u32 frame][u32 rawLength][deflate-raw state...]
// The state is the core's netplay-context savestate taken AFTER running
// `frame` frames; the client then applies records starting at `frame`.
export interface SnapshotHeader {
  session: number;
  frame: number;
  rawLength: number;
}

export function encodeSnapshot(h: SnapshotHeader, compressed: Uint8Array): Uint8Array {
  const b = new Uint8Array(13 + compressed.length);
  const v = new DataView(b.buffer);
  v.setUint8(0, Bin.Snapshot);
  v.setUint32(1, h.session >>> 0, true);
  v.setUint32(5, h.frame >>> 0, true);
  v.setUint32(9, h.rawLength >>> 0, true);
  b.set(compressed, 13);
  return b;
}

export function decodeSnapshot(buf: Uint8Array): { header: SnapshotHeader; data: Uint8Array } | null {
  if (buf.byteLength < 13 || buf[0] !== Bin.Snapshot) return null;
  const v = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  return {
    header: { session: v.getUint32(1, true), frame: v.getUint32(5, true), rawLength: v.getUint32(9, true) },
    data: buf.subarray(13),
  };
}

// ---------------------------------------------------------------- hash
// [u8 type][u32 session][u32 frame][8 bytes hash]
// `frame` = number of frames executed when the hash was taken.
export function encodeHash(session: number, frame: number, hashHex: string): Uint8Array {
  const b = new Uint8Array(17);
  const v = new DataView(b.buffer);
  v.setUint8(0, Bin.Hash);
  v.setUint32(1, session >>> 0, true);
  v.setUint32(5, frame >>> 0, true);
  for (let i = 0; i < 8; i++) b[9 + i] = parseInt(hashHex.slice(i * 2, i * 2 + 2), 16);
  return b;
}

export function decodeHash(buf: Uint8Array): { session: number; frame: number; hash: string } | null {
  if (buf.byteLength !== 17 || buf[0] !== Bin.Hash) return null;
  const v = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  let hash = '';
  for (let i = 0; i < 8; i++) hash += buf[9 + i].toString(16).padStart(2, '0');
  return { session: v.getUint32(1, true), frame: v.getUint32(5, true), hash };
}
