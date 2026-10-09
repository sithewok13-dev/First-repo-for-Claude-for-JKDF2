// End-to-end harness: boots the real server (HTTP, WebSocket gateway, rooms,
// emulation workers) on a random port with a temporary data directory, a
// fixed-allowlist membership provider instead of Telegram, and the real
// shelf when available. Also provides a latency-injecting TCP proxy.

import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { createServer, connect, type Server, type Socket } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createApp, createParts, type App, type Services } from '../../server/app.ts';
import { loadConfig } from '../../server/config.ts';
import type { ChatMemberInfo, MembershipService } from '../../server/telegram/types.ts';
import { ROOT } from './replica.ts';

export class AllowlistMembership implements MembershipService {
  members = new Map<string, ChatMemberInfo>();
  private listeners: ((g: number, u: number) => void)[] = [];
  set(groupId: number, userId: number, role: 'creator' | 'administrator' | 'member' | null): void {
    const k = `${groupId}:${userId}`;
    if (role === null) {
      this.members.set(k, { status: 'left', isMember: false, tgRole: 'member' });
      for (const l of this.listeners) l(groupId, userId);
    } else {
      this.members.set(k, { status: role, isMember: true, tgRole: role });
    }
  }
  async check(groupId: number, userId: number): Promise<ChatMemberInfo> {
    return this.members.get(`${groupId}:${userId}`) ?? { status: 'unknown', isMember: false, tgRole: 'member' };
  }
  invalidate(): void {}
  onRevoked(fn: (g: number, u: number) => void): void {
    this.listeners.push(fn);
  }
}

export interface Harness {
  app: App;
  port: number;
  url: string;
  dataDir: string;
  membership: AllowlistMembership;
  groupId: number;
  roomToken: string;
  addGame(file: string, uploader: number): Promise<number>;
  stop(): Promise<void>;
}

export async function startHarness(env: Record<string, string> = {}): Promise<Harness> {
  const dataDir = mkdtempSync(join(tmpdir(), 'arcade-e2e-'));
  const saved = { ...process.env };
  Object.assign(process.env, {
    DEV_MODE: '1',
    DATA_DIR: dataDir,
    PORT: '0',
    HOST: '127.0.0.1',
    PUBLIC_URL: 'http://127.0.0.1',
    UPDATES_MODE: 'off',
    LOG_LEVEL: 'warn',
    CHECKPOINT_INTERVAL_SEC: '10',
    ...env,
  });
  const cfg = loadConfig();
  process.env = saved;
  const parts = createParts(cfg);
  const membership = new AllowlistMembership();
  const shelfMod = await import('../../server/shelf/shelf.ts');
  const shelf = shelfMod.createShelf
    ? shelfMod.createShelf({ cfg, db: parts.db, groups: parts.groups })
    : new shelfMod.Shelf({ cfg, db: parts.db, groups: parts.groups });
  const uploadMod = await import('../../server/shelf/upload-http.ts');
  const services: Services = { shelf, membership, bot: null, webUpload: uploadMod.handleWebUpload };
  const app = createApp(cfg, parts, services);
  const port = await app.listen();
  const group = parts.groups.ensure(-1001234567890, 'Pilot Friends');
  return {
    app, port, url: `http://127.0.0.1:${port}`, dataDir, membership, groupId: group.id, roomToken: group.roomToken,
    async addGame(file: string, uploader: number): Promise<number> {
      const tmp = join(dataDir, `upload-${Date.now()}-${Math.random()}.bin`);
      const { writeFileSync } = await import('node:fs');
      const data = readFileSync(join(ROOT, file));
      writeFileSync(tmp, data);
      parts.db.run('INSERT OR IGNORE INTO users (id, first_name, updated_at) VALUES (?, ?, ?)', uploader, `User ${uploader}`, Date.now());
      const r = await shelf.ingest({ groupId: group.id, userId: uploader, fileName: file.split('/').pop()!, source: 'web', tempPath: tmp, size: data.length });
      if (r.status !== 'done' || !r.game) throw new Error(`ingest failed: ${r.error}`);
      return r.game.id;
    },
    async stop() {
      await app.close();
      rmSync(dataDir, { recursive: true, force: true });
    },
  };
}

// TCP proxy that delays every chunk in each direction by `oneWayMs` (+
// optional uniform jitter), preserving order. Used to emulate network RTT.
export function delayProxy(targetPort: number, oneWayMs: number, jitterMs = 0): Promise<{ port: number; close(): void }> {
  return new Promise((resolve) => {
    const socks = new Set<Socket>();
    const server: Server = createServer((client) => {
      const upstream = connect(targetPort, '127.0.0.1');
      // like real WebSocket stacks: no Nagle (it would add ~40 ms on its own)
      client.setNoDelay(true);
      upstream.setNoDelay(true);
      socks.add(client);
      socks.add(upstream);
      const pipe = (from: Socket, to: Socket) => {
        let last = 0;
        from.on('data', (chunk) => {
          const due = Math.max(last, Date.now() + oneWayMs + (jitterMs ? Math.random() * jitterMs : 0));
          last = due;
          setTimeout(() => { if (!to.destroyed) to.write(chunk); }, due - Date.now());
        });
        from.on('close', () => setTimeout(() => to.destroy(), oneWayMs + jitterMs + 5));
        from.on('error', () => to.destroy());
      };
      pipe(client, upstream);
      pipe(upstream, client);
    });
    server.listen(0, '127.0.0.1', () => {
      const addr = server.address();
      resolve({
        port: typeof addr === 'object' && addr ? addr.port : 0,
        close() {
          for (const s of socks) s.destroy();
          server.close();
        },
      });
    });
  });
}
