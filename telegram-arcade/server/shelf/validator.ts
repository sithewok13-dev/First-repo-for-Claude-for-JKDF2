// Runs upload validation in short-lived child processes.
//
// Parsing hostile archives and booting an emulator on unknown ROM data are
// the riskiest things the server does, so neither happens in the app server
// process. Each job forks ./validate-worker.ts with a minimal environment (no
// secrets), a V8 heap cap, and a hard wall-clock timeout after which the
// process is killed. At most `maxConcurrent` jobs run at once; the rest wait
// in a FIFO queue. The running game is never affected: its emulation runs in
// its own worker process and this module only awaits IPC messages.

import { fork } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import type { SystemId } from '../../shared/systems.ts';
import { log } from '../log.ts';
import type { Identification } from './catalog.ts';
import type { NesInfo } from './nes.ts';
import type { ValidationFinding } from './types.ts';
import type { ZipLimits } from './zip.ts';

const WORKER = fileURLToPath(new URL('./validate-worker.ts', import.meta.url));

export interface InspectJob {
  path: string;            // the uploaded file (read-only for the worker)
  fileName: string;        // sanitized upload name; only a tie-breaker hint
  maxBytes: number;
  limits: ZipLimits;
  coreDir: string;         // holds catalog-<core>.json (and the cores)
  nesOutPath: string;      // where a .nes extracted from a zip is written
}

export type InspectResult =
  | { ok: false; code: string; error: string; findings: ValidationFinding[] }
  | {
      ok: true;
      format: 'nes';
      nes: NesInfo;
      romPath: string;       // the .nes to store: the upload itself or nesOutPath
      description: string;
      findings: ValidationFinding[];
    }
  | {
      ok: true;
      format: 'zip';
      ident: Identification;
      entries: number;
      totalUncompressed: number;
      findings: ValidationFinding[];
    };

export interface BootJob {
  system: SystemId;
  coreDir: string;
  files: { path: string; source: string }[];  // emulator path <- file on disk
  gamePath: string;
  minFrames: number;
  maxFrames: number;
}

export interface BootResult {
  tried: boolean;
  ok: boolean;
  frames: number;
  detail: string;
}

export type ValidateRequest = { t: 'inspect'; job: InspectJob } | { t: 'boot'; job: BootJob };
export type ValidateReply = { t: 'ready' } | { t: 'result'; result: unknown } | { t: 'error'; message: string };

export interface ValidatorLike {
  inspect(job: InspectJob): Promise<InspectResult>;
  boot(job: BootJob): Promise<BootResult>;
}

export interface ValidatorOptions {
  timeoutMs: number;
  maxConcurrent: number;
  heapMb: number;
}

export class ValidationTimeout extends Error {}

export class Validator implements ValidatorLike {
  private readonly opts: ValidatorOptions;
  private active = 0;
  private readonly waiting: (() => void)[] = [];
  readonly stats = { jobs: 0, timeouts: 0, failures: 0 };

  constructor(opts: ValidatorOptions) {
    this.opts = opts;
  }

  get queued(): number {
    return this.waiting.length;
  }

  get running(): number {
    return this.active;
  }

  inspect(job: InspectJob): Promise<InspectResult> {
    return this.run<InspectResult>({ t: 'inspect', job });
  }

  // A boot test never throws: a crash or timeout is a failed boot. The
  // detail is shown to users, so the raw error (which may name server
  // paths) goes to the operator's log instead.
  async boot(job: BootJob): Promise<BootResult> {
    try {
      return await this.run<BootResult>({ t: 'boot', job });
    } catch (e) {
      if (e instanceof ValidationTimeout) {
        return { tried: true, ok: false, frames: 0, detail: `the boot test did not finish within ${Math.round(this.opts.timeoutMs / 1000)} s` };
      }
      log.warn('validator', 'boot test process failed', { system: job.system, error: e instanceof Error ? e.message : String(e) });
      return { tried: true, ok: false, frames: 0, detail: 'the emulator crashed or failed during the boot test' };
    }
  }

  private async acquire(): Promise<() => void> {
    if (this.active >= this.opts.maxConcurrent) {
      await new Promise<void>((resolve) => this.waiting.push(resolve));
    } else {
      this.active++;
    }
    let released = false;
    return () => {
      if (released) return;
      released = true;
      const next = this.waiting.shift();
      if (next) next(); // hand the slot over directly
      else this.active--;
    };
  }

  private async run<T>(req: ValidateRequest): Promise<T> {
    const release = await this.acquire();
    try {
      return await this.spawn<T>(req);
    } finally {
      release();
    }
  }

  private spawn<T>(req: ValidateRequest): Promise<T> {
    this.stats.jobs++;
    const child = fork(WORKER, [], {
      serialization: 'advanced',
      execArgv: [`--max-old-space-size=${this.opts.heapMb}`, '--disable-warning=ExperimentalWarning'],
      // No secrets reach the validator: a minimal environment only.
      env: { NODE_ENV: process.env.NODE_ENV ?? 'production', PATH: process.env.PATH ?? '' },
      stdio: ['ignore', 'pipe', 'pipe', 'ipc'],
    });
    let outBytes = 0;
    const capture = (buf: Buffer) => {
      // bounded: a hostile file must not be able to flood the server log
      if (outBytes > 8192) return;
      outBytes += buf.length;
      log.debug('validator', buf.toString('utf8').slice(0, 1000).trim());
    };
    child.stdout?.on('data', capture);
    child.stderr?.on('data', capture);

    return new Promise<T>((resolve, reject) => {
      let settled = false;
      const finish = (fn: () => void) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        fn();
        if (child.exitCode === null && child.signalCode === null) child.kill('SIGKILL');
      };
      const timer = setTimeout(() => {
        this.stats.timeouts++;
        log.warn('validator', `job ${req.t} timed out; killing the validator process`);
        finish(() => reject(new ValidationTimeout('validation timed out')));
      }, this.opts.timeoutMs);
      child.on('message', (raw: unknown) => {
        const m = raw as ValidateReply;
        if (m.t === 'ready') {
          child.send(req);
        } else if (m.t === 'result') {
          finish(() => resolve(m.result as T));
        } else if (m.t === 'error') {
          this.stats.failures++;
          finish(() => reject(new Error(String(m.message).slice(0, 300))));
        }
      });
      child.on('error', (e) => {
        this.stats.failures++;
        finish(() => reject(new Error(`validator process error: ${e.message}`)));
      });
      // 'close', not 'exit': 'exit' can be emitted before the last IPC
      // messages (the worker exits right after sending its reply), while
      // 'close' comes after the IPC channel and stdio have been drained.
      child.on('close', (code, signal) => {
        if (settled) return;
        this.stats.failures++;
        finish(() => reject(new Error(`validator process exited unexpectedly (${code ?? signal})`)));
      });
    });
  }
}
