// GameSession: one running game in one room.
//
// The server is the clock. Each emulated frame it freezes one authoritative
// input record (the latest mask from each seat's current owner, sanitized),
// appends it to a replay log, broadcasts it to every synchronized client and
// feeds it to the authoritative emulation worker. Clients join by loading a
// snapshot from the worker and replaying the log from that frame.

import { randomInt } from 'node:crypto';
import { inflateRawSync } from 'node:zlib';
import { EventEmitter } from 'node:events';
import { encodeFrames, encodeHash, encodeSnapshot, type InputAck } from '../../shared/protocol.ts';
import { cleanSocd, portDevicesFor, type SystemDef } from '../../shared/systems.ts';
import type { AdapterEvent, GameStatus } from '../adapters/types.ts';
import { EmuHost, type EmuHostOptions } from '../emu/host.ts';
import type { CoreInfo, WorkerFile } from '../emu/messages.ts';
import { log } from '../log.ts';

export interface SessionGame {
  system: SystemDef;
  files: WorkerFile[];
  gamePath: string;
  options: Record<string, string>;
  ports: number;
  adapterId: string | null;
  allowedOverride?: number[];   // per-port allowed bits (from descriptors/adapter)
}

export interface SessionOptions {
  coreDir: string;
  emu: EmuHostOptions;
  logSeconds: number;          // replay log length (must exceed checkpoint interval)
  hashEvery: number;           // frames between authoritative state hashes
  statusEvery: number;         // frames between adapter status reports
  maxCatchUpFrames: number;    // larger stalls reset the clock instead of bursting
}

export interface Subscriber {
  id: string;
  send(data: Uint8Array): void;
  live: boolean;               // has a snapshot and receives every record
}

export interface SessionEvents {
  ready: [info: CoreInfo];
  status: [status: GameStatus];
  adapter: [events: AdapterEvent[]];
  crashed: [reason: string];
  recovered: [frame: number];
  failed: [reason: string];
}

interface Pulse {
  bit: number;
  frames: number;
}

export class GameSession extends EventEmitter<SessionEvents> {
  readonly id: number;
  readonly game: SessionGame;
  private readonly opts: SessionOptions;
  private host: EmuHost | null = null;
  info: CoreInfo | null = null;

  private fps = 60;
  private frameMs = 1000 / 60;
  nextFrame = 0;                      // frame index of the next record to produce
  private clockBase = 0;              // performance.now() when nextFrame == frameBase
  private frameBase = 0;
  private timer: NodeJS.Timeout | null = null;
  paused = true;

  private masks: Uint16Array;         // latest sanitized mask per port
  private allowed: number[];
  private pulses: (Pulse | null)[];
  private pendingAcks: (InputAck | null)[];
  private lastSeq: number[];
  epochs: number[];                   // current input-authority epoch per port

  // replay log: ring of records
  private logFrames: number;
  private log: Uint16Array;
  private logStart = 0;               // oldest frame still in the log

  private subs = new Map<string, Subscriber>();
  readonly epochSeconds: number;

  // checkpoint used to restore a crashed worker
  private checkpoint: { frame: number; data: Uint8Array } | null = null;
  private restoring = false;
  private stopped = false;
  stats = { framesProduced: 0, catchUpResets: 0, workerRestarts: 0, inputsAccepted: 0, inputsRejected: 0, ticks: 0, lateTicks: 0, maxLateMs: 0 };

  constructor(game: SessionGame, opts: SessionOptions, epochSeconds = Math.floor(Date.now() / 1000)) {
    super();
    this.id = randomInt(1, 0x7fffffff);
    this.game = game;
    this.opts = opts;
    this.epochSeconds = epochSeconds;
    const ports = game.ports;
    this.masks = new Uint16Array(ports);
    this.allowed = new Array(ports).fill(game.system.defaultAllowed);
    this.pulses = new Array(ports).fill(null);
    this.pendingAcks = new Array(ports).fill(null);
    this.lastSeq = new Array(ports).fill(-1);
    this.epochs = new Array(ports).fill(1);
    this.logFrames = Math.ceil(opts.logSeconds * 61);
    this.log = new Uint16Array(this.logFrames * ports);
  }

  get ports(): number {
    return this.game.ports;
  }

  get workerFrame(): number {
    return this.host?.lastFrame ?? 0;
  }

  get frameRate(): number {
    return this.fps;
  }

  allowedMask(port: number): number {
    return this.allowed[port] ?? 0;
  }

  // Starts (or restores) the authoritative worker. Frames are produced once
  // resume() is called.
  async start(restore?: { frame: number; data: Uint8Array; context: number }): Promise<CoreInfo> {
    const info = await this.spawnWorker(restore ?? null);
    this.info = info;
    this.fps = info.fps > 1 ? info.fps : 60;
    this.frameMs = 1000 / this.fps;
    if (this.game.allowedOverride) this.allowed = this.game.allowedOverride.slice(0, this.ports);
    if (restore) {
      this.nextFrame = restore.frame;
      this.logStart = restore.frame;
    }
    this.emit('ready', info);
    return info;
  }

  setAllowed(masks: number[]): void {
    this.allowed = masks.slice(0, this.ports);
  }

  private async spawnWorker(restore: { frame: number; data: Uint8Array; context: number } | null): Promise<CoreInfo> {
    const host = new EmuHost(this.opts.emu);
    this.host = host;
    host.on('hash', (frame, hash) => this.broadcastLive(encodeHash(this.id, frame, hash)));
    host.on('status', (s) => this.emit('status', s));
    host.on('events', (e) => this.emit('adapter', e));
    host.on('exit', (reason) => this.onWorkerExit(host, reason));
    const { info } = await host.start({
      t: 'init',
      core: this.game.system.core,
      coreDir: this.opts.coreDir,
      files: this.game.files,
      gamePath: this.game.gamePath,
      options: { ...this.game.system.options, ...this.game.options },
      portDevices: portDevicesFor(this.game.system, this.ports),
      ports: this.ports,
      epoch: this.epochSeconds,
      startFrame: restore?.frame ?? 0,
      state: restore?.data,
      stateContext: restore?.context,
      adapterId: this.game.adapterId,
      hashEvery: this.opts.hashEvery,
      statusEvery: this.opts.statusEvery,
    });
    return info;
  }

  // ------------------------------------------------------------ clock

  resume(): void {
    if (this.stopped || !this.paused) return;
    this.paused = false;
    this.frameBase = this.nextFrame;
    this.clockBase = performance.now();
    this.schedule();
  }

  pause(): void {
    this.paused = true;
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
  }

  private schedule(): void {
    if (this.paused || this.stopped) return;
    const dueAt = this.clockBase + (this.nextFrame - this.frameBase + 1) * this.frameMs;
    const delay = Math.max(0, dueAt - performance.now());
    this.timer = setTimeout(() => this.tick(), delay > 2 ? delay - 1 : 0);
  }

  private tick(): void {
    this.timer = null;
    if (this.paused || this.stopped) return;
    const now = performance.now();
    // how late this tick fired relative to the frame it was scheduled for
    const lateMs = now - (this.clockBase + (this.nextFrame - this.frameBase + 1) * this.frameMs);
    this.stats.ticks++;
    if (lateMs > 4) this.stats.lateTicks++;
    if (lateMs > this.stats.maxLateMs) this.stats.maxLateMs = Math.round(lateMs);
    let target = this.frameBase + Math.floor((now - this.clockBase) / this.frameMs);
    if (target - this.nextFrame > this.opts.maxCatchUpFrames) {
      // The event loop stalled for a long time. Resetting the clock pauses
      // the game for the stall instead of fast-forwarding every viewer.
      this.stats.catchUpResets++;
      this.frameBase = this.nextFrame;
      this.clockBase = now;
      target = this.nextFrame + 1;
    }
    if (target > this.nextFrame) this.produce(target - this.nextFrame);
    this.schedule();
  }

  // Produces `n` records now (also used directly by tests).
  produce(n: number): void {
    const ports = this.ports;
    while (n > 0) {
      const count = Math.min(n, 255);
      const first = this.nextFrame;
      const out = new Uint16Array(count * ports);
      const acks: InputAck[] = [];
      for (let i = 0; i < count; i++) {
        const f = first + i;
        for (let p = 0; p < ports; p++) {
          let m = this.masks[p];
          const pulse = this.pulses[p];
          if (pulse) {
            m |= 1 << pulse.bit;
            if (--pulse.frames <= 0) this.pulses[p] = null;
          }
          out[i * ports + p] = m;
          this.log[(f % this.logFrames) * ports + p] = m;
          const ack = this.pendingAcks[p];
          if (ack) {
            ack.frame = f;
            acks.push(ack);
            this.pendingAcks[p] = null;
          }
        }
      }
      this.nextFrame += count;
      if (this.nextFrame - this.logStart > this.logFrames) this.logStart = this.nextFrame - this.logFrames;
      this.stats.framesProduced += count;
      if (this.host?.alive && !this.restoring) this.host.sendFrames(first, out);
      this.broadcastLive(encodeFrames({ session: this.id, firstFrame: first, ports, masks: out, acks }));
      n -= count;
    }
  }

  // ------------------------------------------------------------ input

  // Accepts a controller mask from the connection that currently holds the
  // port. Authority (who may call this) is checked by the room; the session
  // checks the epoch and sequence so delayed packets from a former owner or
  // a stale tab are dropped.
  input(port: number, epoch: number, seq: number, mask: number, clientMs: number): boolean {
    if (port < 0 || port >= this.ports || epoch !== this.epochs[port] || seq <= this.lastSeq[port]) {
      this.stats.inputsRejected++;
      return false;
    }
    this.lastSeq[port] = seq;
    this.masks[port] = cleanSocd(mask & this.allowed[port]);
    this.pendingAcks[port] = { port, seq, frame: 0, clientMs };
    this.stats.inputsAccepted++;
    return true;
  }

  // New owner (or no owner) for a port: bump the epoch, release every held
  // button and forget the old owner's sequence numbers.
  resetPort(port: number): number {
    this.epochs[port] = (this.epochs[port] % 0xffff) + 1;
    this.masks[port] = 0;
    this.lastSeq[port] = -1;
    this.pendingAcks[port] = null;
    return this.epochs[port];
  }

  // Releases held buttons without changing who owns the port.
  releasePort(port: number): void {
    this.masks[port] = 0;
  }

  // Server-generated button press (coins). Never comes from client input.
  pulse(port: number, bit: number, frames = 4): void {
    this.pulses[port] = { bit, frames };
  }

  // ------------------------------------------------------------ viewers

  subscribe(sub: Subscriber): void {
    this.subs.set(sub.id, sub);
  }

  unsubscribe(id: string): void {
    this.subs.delete(id);
  }

  private broadcastLive(data: Uint8Array): void {
    for (const s of this.subs.values()) if (s.live) s.send(data);
  }

  // Brings a subscriber into the lockstep: snapshot from the worker, then the
  // logged records since that snapshot, then live records.
  // `raw` asks for an uncompressed snapshot (clients without DecompressionStream).
  async sync(id: string, opts: { raw?: boolean } = {}): Promise<boolean> {
    const sub = this.subs.get(id);
    if (!sub || !this.host) return false;
    sub.live = false;
    let snap: { frame: number; rawLength: number; data: Uint8Array };
    try {
      snap = await this.host.snapshot();
    } catch (e) {
      log.warn('session', `snapshot failed: ${(e as Error).message}`);
      return false;
    }
    if (!this.subs.has(id)) return false;
    if (snap.frame < this.logStart) {
      log.warn('session', 'snapshot older than replay log; worker is too far behind');
      return false;
    }
    const payload = opts.raw ? new Uint8Array(inflateRawSync(snap.data)) : snap.data;
    sub.send(encodeSnapshot({ session: this.id, frame: snap.frame, rawLength: snap.rawLength }, payload));
    const ports = this.ports;
    for (let f = snap.frame; f < this.nextFrame; ) {
      const count = Math.min(255, this.nextFrame - f);
      const masks = new Uint16Array(count * ports);
      for (let i = 0; i < count; i++)
        for (let p = 0; p < ports; p++) masks[i * ports + p] = this.log[((f + i) % this.logFrames) * ports + p];
      sub.send(encodeFrames({ session: this.id, firstFrame: f, ports, masks, acks: [] }));
      f += count;
    }
    sub.live = true;
    return true;
  }

  // ------------------------------------------------------------ persistence & recovery

  async takeCheckpoint(): Promise<{ frame: number; data: Uint8Array } | null> {
    if (!this.host?.alive || this.restoring) return null;
    try {
      const cp = await this.host.checkpoint();
      this.checkpoint = cp;
      return cp;
    } catch (e) {
      log.warn('session', `checkpoint failed: ${(e as Error).message}`);
      return null;
    }
  }

  private async onWorkerExit(host: EmuHost, reason: string): Promise<void> {
    if (this.stopped || host !== this.host) return;
    log.warn('session', `emulation worker for session ${this.id} stopped (${reason})`);
    this.emit('crashed', reason);
    const cp = this.checkpoint;
    if (!cp || cp.frame < this.logStart || this.stats.workerRestarts >= 3) {
      this.pause();
      this.emit('failed', cp ? 'too many restarts' : 'no checkpoint to recover from');
      return;
    }
    // Restore the authoritative instance from the last checkpoint and replay
    // the logged records. Clients keep running; nothing visible changes.
    this.restoring = true;
    this.stats.workerRestarts++;
    try {
      await this.spawnWorker({ frame: cp.frame, data: cp.data, context: 0 });
      const ports = this.ports;
      for (let f = cp.frame; f < this.nextFrame; ) {
        const count = Math.min(255, this.nextFrame - f);
        const masks = new Uint16Array(count * ports);
        for (let i = 0; i < count; i++)
          for (let p = 0; p < ports; p++) masks[i * ports + p] = this.log[((f + i) % this.logFrames) * ports + p];
        this.host!.sendFrames(f, masks);
        f += count;
      }
      this.restoring = false;
      this.emit('recovered', cp.frame);
    } catch (e) {
      this.restoring = false;
      this.pause();
      this.emit('failed', (e as Error).message);
    }
  }

  stop(): void {
    this.stopped = true;
    this.pause();
    this.host?.stop();
    this.subs.clear();
  }

  // Test hook: simulate an emulator crash.
  killWorkerForTest(): void {
    this.host?.kill('test');
  }
}
