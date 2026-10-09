// Composition root: wires the database, services, rooms, HTTP and WebSocket
// gateway together. main.ts builds the production services; tests pass fakes.

import type { IncomingMessage, Server } from 'node:http';
import { AuthSessions } from './auth/sessions.ts';
import type { Config } from './config.ts';
import { Db } from './db/db.ts';
import { Groups } from './groups.ts';
import { createHttpServer } from './http/server.ts';
import { log } from './log.ts';
import { Records } from './records.ts';
import { RoomManager } from './room/manager.ts';
import type { ShelfService } from './shelf/types.ts';
import type { BotService, MembershipService } from './telegram/types.ts';
import { Gateway } from './ws/gateway.ts';

export interface Services {
  shelf: ShelfService;
  membership: MembershipService;
  bot: BotService | null;
  webUpload: (req: IncomingMessage, opts: { tempDir: string; maxBytes: number }) => Promise<{ tempPath: string; size: number; fileName: string }>;
}

export interface App {
  cfg: Config;
  db: Db;
  groups: Groups;
  sessions: AuthSessions;
  records: Records;
  rooms: RoomManager;
  gateway: Gateway;
  server: Server;
  services: Services;
  listen(): Promise<number>;
  close(): Promise<void>;
}

export interface AppParts {
  db: Db;
  groups: Groups;
  sessions: AuthSessions;
  records: Records;
}

export function createParts(cfg: Config): AppParts {
  const db = new Db(cfg.dbPath);
  db.migrate();
  return { db, groups: new Groups(db, cfg.groupQuotaBytes), sessions: new AuthSessions(db, cfg.sessionTtlSec), records: new Records(db) };
}

export function createApp(cfg: Config, parts: AppParts, services: Services): App {
  const { db, groups, sessions, records } = parts;
  const rooms = new RoomManager({ cfg, db, groups, shelf: services.shelf, records, bot: services.bot });
  const allowedOrigins = cfg.devMode ? [] : [new URL(cfg.publicUrl).origin];
  const gateway = new Gateway({ sessions, membership: services.membership, groups, rooms, db, allowedOrigins });
  const server = createHttpServer({ cfg, db, groups, sessions, membership: services.membership, shelf: services.shelf, records, rooms, bot: services.bot, gateway, webUpload: services.webUpload });
  const maintenance = setInterval(() => {
    try {
      sessions.purgeExpired();
      services.shelf.cleanup();
      db.run('DELETE FROM telegram_updates WHERE received_at < ?', Date.now() - 7 * 86400_000);
      db.run('DELETE FROM chat_messages WHERE created_at < ?', Date.now() - cfg.chatRetentionDays * 86400_000);
    } catch (e) {
      log.warn('maintenance', (e as Error).message);
    }
  }, 3600_000);
  maintenance.unref();
  return {
    cfg, db, groups, sessions, records, rooms, gateway, server, services,
    listen: () => new Promise((resolve) => {
      server.listen(cfg.port, cfg.host, () => {
        const addr = server.address();
        resolve(typeof addr === 'object' && addr ? addr.port : cfg.port);
      });
    }),
    close: async () => {
      clearInterval(maintenance);
      // the bot first, so no upload or send touches the database after it closes
      if (services.bot?.shutdown) await services.bot.shutdown();
      else services.bot?.stop();
      await rooms.shutdown();
      gateway.close();
      await new Promise<void>((r) => server.close(() => r()));
      db.close();
    },
  };
}
