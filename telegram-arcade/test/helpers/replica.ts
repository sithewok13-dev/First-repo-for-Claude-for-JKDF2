// A headless lockstep client used by tests: it consumes the same binary
// messages a browser does and runs its own replica of the game.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { inflateRawSync } from 'node:zlib';
import { Core } from '../../shared/emu/core.ts';
import { Bin, decodeFrames, decodeHash, decodeSnapshot } from '../../shared/protocol.ts';
import { portDevicesFor, type SystemDef } from '../../shared/systems.ts';
import type { WorkerFile } from '../../server/emu/messages.ts';

export const ROOT = new URL('../../', import.meta.url).pathname;
export const CORE_DIR = process.env.CORE_DIR ?? join(ROOT, 'native/build');

export class Replica {
  core!: Core;
  session = 0;
  synced = false;
  pending = new Map<number, Uint16Array>(); // frame -> masks
  hashes = new Map<number, string>();       // authoritative hashes received
  mismatches: { frame: number; mine: string; theirs: string }[] = [];
  checked = 0;
  ports = 1;
  hashEvery = 120;
  localHashes = new Map<number, string>();

  static async create(sys: SystemDef, files: WorkerFile[], gamePath: string, options: Record<string, string>, epoch: number, ports: number): Promise<Replica> {
    const r = new Replica();
    const mod = await import(join(CORE_DIR, `${sys.core}.mjs`));
    r.core = await Core.create(mod.default, readFileSync(join(CORE_DIR, `${sys.core}.wasm`)), { quiet: true });
    r.core.setEpoch(epoch);
    for (const f of files) r.core.writeFile(f.path, f.data);
    const opts = { ...sys.options, ...options };
    for (const [k, v] of Object.entries(opts)) r.core.setOption(k, v);
    for (const [p, d] of Object.entries(portDevicesFor(sys, ports))) r.core.setPortDevice(Number(p), d);
    if (!r.core.loadGame(gamePath)) throw new Error('replica load failed');
    for (const [k, v] of Object.entries(opts)) r.core.setOption(k, v);
    r.core.setSavestateContext(3);
    r.ports = ports;
    return r;
  }

  receive(data: Uint8Array): void {
    switch (data[0]) {
      case Bin.Snapshot: {
        const s = decodeSnapshot(data)!;
        this.session = s.header.session;
        this.core.unserialize(new Uint8Array(inflateRawSync(s.data)));
        this.core.frame = s.header.frame;
        this.synced = true;
        for (const f of [...this.pending.keys()]) if (f < s.header.frame) this.pending.delete(f);
        break;
      }
      case Bin.Frames: {
        const m = decodeFrames(data)!;
        const count = m.masks.length / m.ports;
        for (let i = 0; i < count; i++) this.pending.set(m.firstFrame + i, m.masks.slice(i * m.ports, (i + 1) * m.ports));
        break;
      }
      case Bin.Hash: {
        const h = decodeHash(data)!;
        this.hashes.set(h.frame, h.hash);
        this.compare(h.frame);
        break;
      }
    }
  }

  private compare(frame: number): void {
    const mine = this.localHashes.get(frame);
    const theirs = this.hashes.get(frame);
    if (!mine || !theirs) return;
    this.checked++;
    if (mine !== theirs) this.mismatches.push({ frame, mine, theirs });
  }

  // Runs every frame we have records for.
  advance(): number {
    if (!this.synced) return 0;
    let n = 0;
    for (;;) {
      const masks = this.pending.get(this.core.frame);
      if (!masks) break;
      this.pending.delete(this.core.frame);
      for (let p = 0; p < this.ports; p++) this.core.setInput(p, masks[p]);
      this.core.runFrame(false, false);
      n++;
      const f = this.core.frame;
      if (f % this.hashEvery === 0) {
        this.localHashes.set(f, this.core.stateHash());
        this.compare(f);
      }
    }
    return n;
  }

  ram(): Uint8Array {
    return this.core.memory()!;
  }
}
