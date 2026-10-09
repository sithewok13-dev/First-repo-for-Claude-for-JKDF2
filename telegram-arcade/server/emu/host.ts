// Spawns and supervises one emulation worker process.

import { fork, type ChildProcess } from 'node:child_process';
import { EventEmitter } from 'node:events';
import { fileURLToPath } from 'node:url';
import type { AdapterEvent, GameStatus } from '../adapters/types.ts';
import type { CoreInfo, WorkerIn, WorkerOut } from './messages.ts';
import { log } from '../log.ts';

const WORKER = fileURLToPath(new URL('./worker.ts', import.meta.url));

export interface EmuHostOptions {
  maxOldSpaceMb: number;     // V8 heap cap for the worker (wasm memory is capped by the core build)
  bootTimeoutMs: number;
  requestTimeoutMs: number;
  heartbeatMs: number;
}

export interface EmuHostEvents {
  hash: [frame: number, hash: string];
  status: [status: GameStatus];
  events: [events: AdapterEvent[]];
  exit: [reason: string];
}

type Pending = { resolve: (m: any) => void; reject: (e: Error) => void; timer: NodeJS.Timeout };

export class EmuHost extends EventEmitter<EmuHostEvents> {
  private child: ChildProcess | null = null;
  private pending = new Map<number, Pending>();
  private nextId = 1;
  private heartbeat: NodeJS.Timeout | null = null;
  private exited = false;
  private readonly opts: EmuHostOptions;
  lastFrame = 0;

  constructor(opts: EmuHostOptions) {
    super();
    this.opts = opts;
  }

  get alive(): boolean {
    return this.child !== null && !this.exited;
  }

  async start(init: Extract<WorkerIn, { t: 'init' }>): Promise<{ frame: number; info: CoreInfo }> {
    const child = fork(WORKER, [], {
      serialization: 'advanced',
      execArgv: [`--max-old-space-size=${this.opts.maxOldSpaceMb}`, '--disable-warning=ExperimentalWarning'],
      // The worker gets no secrets: a minimal environment only.
      env: { NODE_ENV: process.env.NODE_ENV ?? 'production', PATH: process.env.PATH ?? '' },
      stdio: ['ignore', 'pipe', 'pipe', 'ipc'],
    });
    this.child = child;
    let errBytes = 0;
    const capture = (buf: Buffer) => {
      // bounded: emulator chatter must not flood the server log
      if (errBytes > 16384) return;
      errBytes += buf.length;
      log.debug('emu-worker', buf.toString('utf8').slice(0, 2000).trim());
    };
    child.stdout?.on('data', capture);
    child.stderr?.on('data', capture);

    const ready = new Promise<{ frame: number; info: CoreInfo }>((resolve, reject) => {
      const timer = setTimeout(() => {
        reject(new Error('emulation worker did not start in time'));
        this.kill('boot timeout');
      }, this.opts.bootTimeoutMs);
      const onMsg = (raw: unknown) => {
        const m = raw as WorkerOut;
        if (m.t === 'boot') {
          child.send(init);
        } else if (m.t === 'ready') {
          clearTimeout(timer);
          child.off('message', onMsg);
          resolve({ frame: m.frame, info: m.info });
        } else if (m.t === 'error') {
          clearTimeout(timer);
          child.off('message', onMsg);
          reject(new Error(m.message));
        }
      };
      child.on('message', onMsg);
      child.once('exit', (code, signal) => {
        clearTimeout(timer);
        reject(new Error(`emulation worker exited during start (${code ?? signal})`));
      });
    });

    child.on('message', (raw: unknown) => this.onMessage(raw as WorkerOut));
    child.on('exit', (code, signal) => {
      this.exited = true;
      if (this.heartbeat) clearInterval(this.heartbeat);
      for (const p of this.pending.values()) {
        clearTimeout(p.timer);
        p.reject(new Error('emulation worker exited'));
      }
      this.pending.clear();
      this.emit('exit', `exit ${code ?? signal}`);
    });

    const result = await ready;
    this.lastFrame = result.frame;
    this.heartbeat = setInterval(() => {
      this.request<{ frame: number }>({ t: 'ping', id: 0 }).then(
        (m) => { this.lastFrame = m.frame; },
        () => this.kill('unresponsive'),
      );
    }, this.opts.heartbeatMs);
    this.heartbeat.unref();
    return result;
  }

  private onMessage(m: WorkerOut): void {
    switch (m.t) {
      case 'hash':
        this.lastFrame = m.frame;
        this.emit('hash', m.frame, m.hash);
        break;
      case 'status':
        this.emit('status', m.status);
        break;
      case 'events':
        this.emit('events', m.events);
        break;
      case 'snapshot':
      case 'checkpoint':
      case 'pong': {
        const p = this.pending.get(m.id);
        if (p) {
          clearTimeout(p.timer);
          this.pending.delete(m.id);
          p.resolve(m);
        }
        break;
      }
      case 'error':
        log.warn('emu-worker', `error: ${m.message}`);
        break;
    }
  }

  private request<T>(msg: Extract<WorkerIn, { id: number }>): Promise<T> {
    if (!this.alive) return Promise.reject(new Error('emulation worker is not running'));
    const id = this.nextId++;
    return new Promise<T>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`emulation worker request ${msg.t} timed out`));
      }, this.opts.requestTimeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this.child!.send({ ...msg, id });
    });
  }

  sendFrames(first: number, masks: Uint16Array): void {
    if (this.alive) this.child!.send({ t: 'frames', first, masks } satisfies WorkerIn);
  }

  snapshot(): Promise<{ frame: number; rawLength: number; data: Uint8Array }> {
    return this.request({ t: 'snapshot', id: 0 });
  }

  checkpoint(): Promise<{ frame: number; data: Uint8Array }> {
    return this.request({ t: 'checkpoint', id: 0 });
  }

  kill(reason: string): void {
    if (!this.child || this.exited) return;
    log.warn('emu-worker', `stopping worker: ${reason}`);
    this.child.kill('SIGKILL');
  }

  stop(): void {
    if (!this.child || this.exited) return;
    try {
      this.child.send({ t: 'stop' } satisfies WorkerIn);
    } catch {
      /* already gone */
    }
    const c = this.child;
    setTimeout(() => { if (!this.exited) c.kill('SIGKILL'); }, 2000).unref();
  }
}
