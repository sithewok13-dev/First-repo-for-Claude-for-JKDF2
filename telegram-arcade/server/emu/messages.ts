// IPC messages between the server and an emulation worker process.

import type { InputDescriptor } from '../../shared/emu/core.ts';
import type { AdapterEvent, GameStatus } from '../adapters/types.ts';

export interface WorkerFile {
  path: string;       // path inside the emulator's in-memory filesystem
  data: Uint8Array;
}

export interface CoreInfo {
  coreName: string;
  coreVersion: string;
  fps: number;
  sampleRate: number;
  width: number;
  height: number;
  aspect: number;
  rotation: number;
  descriptors: InputDescriptor[];
  driver: any;
  stateSize: number;
  options: Record<string, string>;
}

export type WorkerIn =
  | {
      t: 'init';
      core: string;
      coreDir: string;
      files: WorkerFile[];
      gamePath: string;
      options: Record<string, string>;
      portDevices: Record<number, number>;
      ports: number;
      epoch: number;
      startFrame: number;
      state?: Uint8Array;
      stateContext?: number;
      adapterId?: string | null;
      hashEvery: number;
      statusEvery: number;
    }
  | { t: 'frames'; first: number; masks: Uint16Array }
  | { t: 'snapshot'; id: number }
  | { t: 'checkpoint'; id: number }
  | { t: 'ping'; id: number }
  | { t: 'stop' };

export type WorkerOut =
  | { t: 'boot' }
  | { t: 'ready'; frame: number; info: CoreInfo }
  | { t: 'hash'; frame: number; hash: string }
  | { t: 'status'; status: GameStatus }
  | { t: 'events'; events: AdapterEvent[] }
  | { t: 'snapshot'; id: number; frame: number; rawLength: number; data: Uint8Array }
  | { t: 'checkpoint'; id: number; frame: number; data: Uint8Array }
  | { t: 'pong'; id: number; frame: number }
  | { t: 'error'; message: string };
