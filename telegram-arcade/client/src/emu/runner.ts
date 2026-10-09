// Client-side lockstep replica.
//
// The browser runs the same WebAssembly core as the server's authoritative
// worker and advances it ONLY with the server's authoritative input records.
// It never decides game state on its own: if its periodic state hash ever
// differs from the server's, it discards its state and reloads a snapshot.
//
// Pacing: frames are played at the core's frame rate from a small jitter
// buffer (players: ~1 frame, spectators: more), with a proportional speed
// correction that keeps the buffer near its target. A large backlog is
// fast-forwarded without rendering.

import { Core } from '../../../shared/emu/core.ts';
import { Bin, decodeFrames, decodeHash, decodeSnapshot, encodeInput } from '../../../shared/protocol.ts';
import { fetchFile } from '../api.ts';
import type { Net } from '../net.ts';
import { AudioOut } from './audio.ts';

export interface SessionInfo {
  sessionId: number;
  gameId: number;
  title: string;
  system: string;
  core: string;
  files: { path: string; sha256: string; size: number }[];
  gamePath: string;
  options: Record<string, string>;
  portDevices: Record<string, number>;
  ports: number;
  fps: number;
  epoch: number;
  mode: string;
  hashEvery: number;
  allowed: number[];
  coinBit: number | null;
  width: number;
  height: number;
  aspect: number;
  rotation: number;
  descriptors: { port: number; device: number; index: number; id: number; text: string }[];
  fresh: boolean;
}

export interface RunnerStats {
  phase: 'idle' | 'loading' | 'syncing' | 'running' | 'error';
  detail: string;
  frame: number;
  buffered: number;
  target: number;
  stalls: number;
  desyncs: number;
  hashChecks: number;
  fps: number;
  inputRttMs: number | null;         // input -> server -> back (median of recent)
  inputToFrameMs: number | null;     // input -> first rendered frame containing it (median)
  inputToFrameP95: number | null;
  behindLiveMs: number | null;       // how far this view trails the server's newest frame
  samples: number;
  emuMsPerFrame: number | null;      // this device's emulation cost (median of recent frames)
  slowDevice: boolean;               // cannot sustain the game's frame rate here
}

function median(a: number[]): number | null {
  if (!a.length) return null;
  const s = [...a].sort((x, y) => x - y);
  return s[Math.floor(s.length / 2)];
}

function p95(a: number[]): number | null {
  if (!a.length) return null;
  const s = [...a].sort((x, y) => x - y);
  return s[Math.min(s.length - 1, Math.floor(s.length * 0.95))];
}

async function inflateRaw(data: Uint8Array): Promise<Uint8Array> {
  const ds = new (window as any).DecompressionStream('deflate-raw');
  const stream = new Blob([data as BlobPart]).stream().pipeThrough(ds);
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

export const canInflate = typeof (window as any).DecompressionStream === 'function';

export class Runner {
  info: SessionInfo | null = null;
  core: Core | null = null;
  readonly audio = new AudioOut();
  private readonly net: Net;
  private canvas: HTMLCanvasElement | null = null;
  private ctx2d: CanvasRenderingContext2D | null = null;
  private records = new Map<number, Uint16Array>();
  private newest = -1;                       // newest record frame received
  private synced = false;
  private syncing = false;
  private raf = 0;
  private lastTs = 0;
  private acc = 0;
  private loadToken = 0;
  spectator = true;
  target = 1;
  private stallsRecent: number[] = [];
  private localHashes = new Map<number, string>();
  private serverHashes = new Map<number, string>();
  // input
  myPort: number | null = null;
  myEpoch = 0;
  private seq = 0;
  private mask = 0;
  private sentAt = new Map<number, number>();      // seq -> performance.now()
  private awaiting = new Map<number, number[]>();  // frame -> input times
  private rtts: number[] = [];
  private i2f: number[] = [];
  private fpsFrames = 0;
  private fpsT0 = 0;
  // rendered frame log for test harnesses (frame -> wall-clock ms)
  readonly renderLog: [number, number][] = [];
  stats: RunnerStats = { phase: 'idle', detail: '', frame: 0, buffered: 0, target: 1, stalls: 0, desyncs: 0, hashChecks: 0, fps: 0, inputRttMs: null, inputToFrameMs: null, inputToFrameP95: null, behindLiveMs: null, samples: 0, emuMsPerFrame: null, slowDevice: false };
  private emuCost: number[] = [];
  onStats: (s: RunnerStats) => void = () => {};
  onFrameEvent: (frame: number) => void = () => {};
  paused = false;
  audioEnabled = true;

  constructor(net: Net) {
    this.net = net;
  }

  attach(canvas: HTMLCanvasElement): void {
    this.canvas = canvas;
    this.ctx2d = canvas.getContext('2d', { alpha: false });
  }

  private setPhase(phase: RunnerStats['phase'], detail = ''): void {
    this.stats.phase = phase;
    this.stats.detail = detail;
    this.onStats({ ...this.stats });
  }

  async load(info: SessionInfo): Promise<void> {
    const token = ++this.loadToken;
    this.stop();
    this.unload();
    this.info = info;
    this.setPhase('loading', 'Downloading the emulator…');
    try {
      const mod = await import(/* @vite-ignore */ `/cores/${info.core}.mjs`);
      const wasm = await (await fetch(`/cores/${info.core}.wasm`)).arrayBuffer();
      if (token !== this.loadToken) return;
      const files: { path: string; data: Uint8Array }[] = [];
      let i = 0;
      for (const f of info.files) {
        i++;
        this.setPhase('loading', `Downloading game files (${i}/${info.files.length})…`);
        files.push({ path: f.path, data: await fetchFile(f.sha256) });
        if (token !== this.loadToken) return;
      }
      this.setPhase('loading', 'Starting the game…');
      const core = await Core.create(mod.default, wasm, { quiet: true });
      core.setEpoch(info.epoch);
      for (const f of files) core.writeFile(f.path, f.data);
      for (const [k, v] of Object.entries(info.options)) core.setOption(k, v);
      for (const [p, d] of Object.entries(info.portDevices)) core.setPortDevice(Number(p), d);
      if (!core.loadGame(info.gamePath)) throw new Error('The emulator could not load this game on your device.');
      for (const [k, v] of Object.entries(info.options)) core.setOption(k, v);
      core.setSavestateContext(3);
      if (token !== this.loadToken) return;
      this.core = core;
      await this.audio.init(core.sampleRate);
      this.resync();
      this.start();
    } catch (e) {
      if (token === this.loadToken) this.setPhase('error', (e as Error).message);
    }
  }

  unload(): void {
    this.core?.unload();
    this.core = null;
    this.records.clear();
    this.newest = -1;
    this.synced = false;
    this.localHashes.clear();
    this.serverHashes.clear();
    this.awaiting.clear();
  }

  resync(): void {
    if (!this.core || this.syncing) return;
    this.syncing = true;
    this.synced = false;
    this.setPhase('syncing', 'Joining the game…');
    void this.net.request('sync', { raw: !canInflate }).then((r) => {
      this.syncing = false;
      if (!r.ok) this.setPhase('error', r.error ?? 'Could not join the running game.');
    });
  }

  async onBinary(data: Uint8Array): Promise<void> {
    if (!this.core || !this.info) return;
    switch (data[0]) {
      case Bin.Snapshot: {
        const s = decodeSnapshot(data);
        if (!s || s.header.session !== this.info.sessionId) return;
        const raw = s.data.length === s.header.rawLength ? s.data : await inflateRaw(s.data);
        try {
          this.core.unserialize(raw);
        } catch {
          this.setPhase('error', 'Could not load the shared game state on this device.');
          return;
        }
        this.core.frame = s.header.frame;
        for (const f of [...this.records.keys()]) if (f < s.header.frame) this.records.delete(f);
        this.localHashes.clear();
        this.synced = true;
        this.acc = 0;
        this.audio.flush();
        this.setPhase('running');
        break;
      }
      case Bin.Frames: {
        const m = decodeFrames(data);
        if (!m || m.session !== this.info.sessionId) return;
        const count = m.masks.length / m.ports;
        for (let i = 0; i < count; i++) this.records.set(m.firstFrame + i, m.masks.subarray(i * m.ports, (i + 1) * m.ports));
        this.newest = Math.max(this.newest, m.firstFrame + count - 1);
        const now = performance.now();
        for (const a of m.acks) {
          if (a.port !== this.myPort) continue;
          const t0 = this.sentAt.get(a.seq);
          if (t0 === undefined) continue;
          this.sentAt.delete(a.seq);
          this.rtts.push(now - t0);
          if (this.rtts.length > 120) this.rtts.shift();
          const list = this.awaiting.get(a.frame) ?? [];
          list.push(t0);
          this.awaiting.set(a.frame, list);
        }
        break;
      }
      case Bin.Hash: {
        const h = decodeHash(data);
        if (!h || h.session !== this.info.sessionId) return;
        this.serverHashes.set(h.frame, h.hash);
        this.checkHash(h.frame);
        break;
      }
    }
  }

  private checkHash(frame: number): void {
    const mine = this.localHashes.get(frame);
    const theirs = this.serverHashes.get(frame);
    if (!mine || !theirs) return;
    this.localHashes.delete(frame);
    this.serverHashes.delete(frame);
    this.stats.hashChecks++;
    if (mine !== theirs) {
      this.stats.desyncs++;
      this.resync();
    }
  }

  // ------------------------------------------------------------ input

  setSeat(port: number | null, epoch: number): void {
    const changed = port !== this.myPort || epoch !== this.myEpoch;
    this.myPort = port;
    this.myEpoch = epoch;
    this.spectator = port === null;
    this.target = this.spectator ? 4 : 1;
    if (changed && port !== null) {
      this.seq = 0;
      this.sendMask(this.mask, true);
    }
  }

  // Called by the input layer whenever the local controller state changes.
  sendMask(mask: number, force = false): void {
    if (mask === this.mask && !force) return;
    this.mask = mask;
    if (this.myPort === null) return;
    const allowed = this.info?.allowed?.[this.myPort] ?? 0xffff;
    const seq = ++this.seq;
    const now = performance.now();
    this.sentAt.set(seq, now);
    if (this.sentAt.size > 200) this.sentAt.delete(this.sentAt.keys().next().value!);
    this.net.sendBin(encodeInput({ port: this.myPort, epoch: this.myEpoch, seq, mask: mask & allowed, clientMs: Math.floor(now) >>> 0 }));
  }

  // ------------------------------------------------------------ loop

  start(): void {
    if (this.raf) return;
    this.lastTs = performance.now();
    this.fpsT0 = this.lastTs;
    const loop = (ts: number) => {
      this.raf = requestAnimationFrame(loop);
      this.tick(ts);
    };
    this.raf = requestAnimationFrame(loop);
  }

  stop(): void {
    if (this.raf) cancelAnimationFrame(this.raf);
    this.raf = 0;
  }

  private tick(ts: number): void {
    const core = this.core;
    const info = this.info;
    if (!core || !info || !this.synced || this.paused) {
      this.lastTs = ts;
      return;
    }
    const dt = Math.min(250, ts - this.lastTs);
    this.lastTs = ts;
    let avail = 0;
    while (this.records.has(core.frame + avail)) avail++;
    const err = avail - this.target;
    const speed = Math.max(0.7, Math.min(1.5, 1 + err * 0.08));
    this.acc += (dt / 1000) * info.fps * speed;
    let n = Math.floor(this.acc);
    this.acc -= n;
    // Seated players keep at most one frame beyond the target in the buffer:
    // extra frames are run now (latency over smoothness). Spectators let the
    // speed controller absorb jitter smoothly.
    if (!this.spectator && avail - n > this.target + 1) n = avail - this.target;
    if (avail > this.target + 45) n = avail - this.target;   // far behind: fast-forward
    if (n > avail) {
      if (avail === 0) {
        this.stats.stalls++;
        this.stallsRecent.push(ts);
        this.acc = 0;
      }
      n = avail;
    }
    // Adapt the jitter buffer: frequent stalls raise the target a little.
    this.stallsRecent = this.stallsRecent.filter((t) => ts - t < 5000);
    const base = this.spectator ? 4 : 1;
    if (this.stallsRecent.length > 6 && this.target < base + 6) {
      this.target++;
      this.stallsRecent = [];
    } else if (this.stallsRecent.length === 0 && this.target > base && Math.random() < 0.002) {
      this.target--;
    }
    if (n <= 0) return;
    const ports = info.ports;
    for (let i = 0; i < n; i++) {
      const f = core.frame;
      const masks = this.records.get(f)!;
      this.records.delete(f);
      for (let p = 0; p < ports; p++) core.setInput(p, masks[p]);
      const last = i === n - 1;
      // only the last few frames of a burst produce audio, so catching up
      // never builds a long audio delay
      const t0 = performance.now();
      core.runFrame(last, this.audioEnabled && i >= n - 2);
      this.emuCost.push(performance.now() - t0);
      if (this.emuCost.length > 240) this.emuCost.shift();
      if (this.audioEnabled && i >= n - 2) this.audio.push(core.audio());
      const done = core.frame;
      if (done % info.hashEvery === 0) {
        this.localHashes.set(done, core.stateHash());
        this.checkHash(done);
        if (this.localHashes.size > 20) this.localHashes.delete(this.localHashes.keys().next().value!);
      }
      this.onFrameEvent(f);
      const waiting = this.awaiting.get(f);
      if (waiting) {
        this.awaiting.delete(f);
        if (last) {
          const now = performance.now();
          for (const t0 of waiting) this.i2f.push(now - t0);
          if (this.i2f.length > 300) this.i2f.splice(0, this.i2f.length - 300);
        } else {
          // input landed in a fast-forwarded frame: count it at the next draw
          this.awaiting.set(f + 1, (this.awaiting.get(f + 1) ?? []).concat(waiting));
        }
      }
    }
    this.draw();
    this.renderLog.push([core.frame, performance.timeOrigin + performance.now()]);
    if (this.renderLog.length > 4000) this.renderLog.splice(0, 1000);
    this.fpsFrames += n;
    if (ts - this.fpsT0 >= 1000) {
      this.stats.fps = Math.round((this.fpsFrames * 1000) / (ts - this.fpsT0));
      this.fpsFrames = 0;
      this.fpsT0 = ts;
      this.stats.frame = core.frame;
      this.stats.buffered = avail - n;
      this.stats.target = this.target;
      this.stats.inputRttMs = median(this.rtts);
      this.stats.inputToFrameMs = median(this.i2f);
      this.stats.inputToFrameP95 = p95(this.i2f);
      this.stats.samples = this.i2f.length;
      this.stats.behindLiveMs = Math.round(((this.newest + 1 - core.frame) * 1000) / info.fps);
      // A device that needs most of the frame budget just to emulate cannot
      // keep up (drawing, input and the browser need time too).
      const cost = median(this.emuCost);
      this.stats.emuMsPerFrame = cost === null ? null : Math.round(cost * 10) / 10;
      this.stats.slowDevice = cost !== null && this.emuCost.length >= 120 && cost > (1000 / info.fps) * 0.75;
      this.onStats({ ...this.stats });
    }
  }

  private draw(): void {
    const img = this.core?.image();
    if (!img || !this.canvas || !this.ctx2d) return;
    if (this.canvas.width !== img.width || this.canvas.height !== img.height) {
      this.canvas.width = img.width;
      this.canvas.height = img.height;
    }
    // copy: the view points into emulator memory that changes next frame
    const data = new ImageData(new Uint8ClampedArray(img.rgba), img.width, img.height);
    this.ctx2d.putImageData(data, 0, 0);
  }

  latencySamples(): { rtt: number[]; inputToFrame: number[] } {
    return { rtt: [...this.rtts], inputToFrame: [...this.i2f] };
  }

  clearLatencySamples(): void {
    this.rtts = [];
    this.i2f = [];
  }
}
