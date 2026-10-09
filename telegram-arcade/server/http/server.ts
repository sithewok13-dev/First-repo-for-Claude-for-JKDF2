// HTTP: static Mini App, emulator cores, JSON API, private file delivery,
// Telegram webhook and health checks.

import { createServer, type IncomingMessage, type Server, type ServerResponse } from 'node:http';
import { createReadStream, existsSync, mkdirSync, readFileSync, statSync } from 'node:fs';
import { extname, join, normalize, sep } from 'node:path';
import type { AuthSessions, AuthSession } from '../auth/sessions.ts';
import { displayName, validateInitData } from '../auth/initdata.ts';
import type { Config } from '../config.ts';
import type { Db } from '../db/db.ts';
import type { Groups } from '../groups.ts';
import { log } from '../log.ts';
import type { Records } from '../records.ts';
import type { RoomManager } from '../room/manager.ts';
import type { ShelfService } from '../shelf/types.ts';
import type { BotService, MembershipService } from '../telegram/types.ts';
import type { Gateway } from '../ws/gateway.ts';

export interface HttpDeps {
  cfg: Config;
  db: Db;
  groups: Groups;
  sessions: AuthSessions;
  membership: MembershipService;
  shelf: ShelfService;
  records: Records;
  rooms: RoomManager;
  bot: BotService | null;
  gateway: Gateway;
  webUpload: (req: IncomingMessage, opts: { tempDir: string; maxBytes: number }) => Promise<{ tempPath: string; size: number; fileName: string }>;
}

const MIME: Record<string, string> = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.wasm': 'application/wasm',
  '.txt': 'text/plain; charset=utf-8',
  '.wav': 'audio/wav',
  '.json': 'application/json',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.webmanifest': 'application/manifest+json',
};

const SHELF_STATUS: Record<string, number> = {
  not_found: 404, upload_missing: 404, invalid: 400, empty_file: 400, bad_set: 422, validation_failed: 422,
  too_large: 413, storage_full: 507, not_ready: 409, not_playable: 409, missing_dependency: 409, removed: 409,
  blob_missing: 500, blob_corrupt: 500,
};

class HttpError extends Error {
  readonly status: number;
  constructor(status: number, message: string) {
    super(message);
    this.status = status;
  }
}

function securityHeaders(res: ServerResponse): void {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('Referrer-Policy', 'no-referrer');
  res.setHeader('Permissions-Policy', 'microphone=(), camera=(), geolocation=(), gamepad=(self)');
  res.setHeader(
    'Content-Security-Policy',
    [
      "default-src 'self'",
      "script-src 'self' https://telegram.org 'wasm-unsafe-eval'",
      "style-src 'self' 'unsafe-inline'",
      "img-src 'self' data: blob:",
      "connect-src 'self' wss: ws:",
      "worker-src 'self' blob:",
      "media-src 'self' blob: data:",
      // Telegram Web shows Mini Apps in an iframe; mobile/desktop clients use a webview.
      "frame-ancestors 'self' https://web.telegram.org https://*.telegram.org",
      "base-uri 'none'",
      "form-action 'none'",
    ].join('; '),
  );
}

function json(res: ServerResponse, status: number, body: unknown): void {
  const s = JSON.stringify(body);
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', 'Content-Length': Buffer.byteLength(s) });
  res.end(s);
}

async function readJson(req: IncomingMessage, limit = 16 * 1024): Promise<any> {
  const chunks: Buffer[] = [];
  let n = 0;
  for await (const c of req) {
    n += (c as Buffer).length;
    if (n > limit) throw new HttpError(413, 'Request too large.');
    chunks.push(c as Buffer);
  }
  if (n === 0) return {};
  try {
    return JSON.parse(Buffer.concat(chunks).toString('utf8'));
  } catch {
    throw new HttpError(400, 'Invalid JSON.');
  }
}

// Simple fixed-window limiter for unauthenticated endpoints.
class Limiter {
  private hits = new Map<string, { n: number; reset: number }>();
  private readonly max: number;
  private readonly windowMs: number;
  constructor(max: number, windowMs: number) {
    this.max = max;
    this.windowMs = windowMs;
  }
  allow(key: string): boolean {
    const now = Date.now();
    const h = this.hits.get(key);
    if (!h || h.reset < now) {
      this.hits.set(key, { n: 1, reset: now + this.windowMs });
      if (this.hits.size > 10_000) this.hits.clear();
      return true;
    }
    h.n++;
    return h.n <= this.max;
  }
}

export function createHttpServer(d: HttpDeps): Server {
  const authLimiter = new Limiter(30, 60_000);
  // Uploads: one in flight per user (each holds a temp file and a validation
  // slot), and at most 30 per group per hour.
  const uploadsInFlight = new Set<number>();
  const groupUploads = new Limiter(30, 3600_000);
  const tempDir = join(d.cfg.dataDir, 'tmp');
  mkdirSync(tempDir, { recursive: true, mode: 0o700 });

  const clientIp = (req: IncomingMessage): string => {
    if (d.cfg.trustProxy) {
      const f = req.headers['x-forwarded-for'];
      // the right-most entry is the one our reverse proxy added; anything to
      // its left was supplied by the client and cannot be trusted
      if (typeof f === 'string' && f) return f.split(',').at(-1)!.trim();
    }
    return req.socket.remoteAddress ?? 'unknown';
  };

  const auth = (req: IncomingMessage): AuthSession => {
    const h = req.headers.authorization ?? '';
    const token = h.startsWith('Bearer ') ? h.slice(7) : '';
    const s = d.sessions.verify(token);
    if (!s) throw new HttpError(401, 'Your session expired. Reopen the arcade from the group.');
    const g = d.groups.byId(s.groupId);
    if (!g || g.status !== 'active') throw new HttpError(403, 'This group\'s arcade is not active.');
    return s;
  };

  const member = async (s: AuthSession) => {
    const m = await d.membership.check(s.groupId, s.userId);
    if (!m.isMember) {
      d.sessions.revokeUser(s.groupId, s.userId);
      throw new HttpError(403, 'Only members of this group can do that.');
    }
    return m;
  };

  // Moderation checks for HTTP-side actions (shelf removal etc.) use the
  // same rules as the room: host, acting host, deputies, Telegram admins.
  const canModerate = (groupId: number, userId: number, tgRole: string): boolean => {
    if (tgRole === 'creator' || tgRole === 'administrator') return true;
    return !!d.db.get("SELECT 1 FROM roles WHERE group_id = ? AND user_id = ? AND role IN ('host', 'deputy')", groupId, userId);
  };

  const serveFile = (req: IncomingMessage, res: ServerResponse, path: string, opts: { cache: string; type?: string }) => {
    let st;
    try {
      st = statSync(path);
    } catch {
      throw new HttpError(404, 'Not found.');
    }
    if (!st.isFile()) throw new HttpError(404, 'Not found.');
    const etag = `"${st.size.toString(16)}-${Math.floor(st.mtimeMs).toString(16)}"`;
    if (req.headers['if-none-match'] === etag) {
      res.writeHead(304, { ETag: etag, 'Cache-Control': opts.cache });
      res.end();
      return;
    }
    res.writeHead(200, {
      'Content-Type': opts.type ?? MIME[extname(path)] ?? 'application/octet-stream',
      'Content-Length': st.size,
      'Cache-Control': opts.cache,
      ETag: etag,
    });
    if (req.method === 'HEAD') {
      res.end();
      return;
    }
    createReadStream(path).pipe(res);
  };

  const staticFile = (req: IncomingMessage, res: ServerResponse, base: string, rel: string, cache: string) => {
    const clean = normalize(decodeURIComponent(rel)).replace(/^([/\\])+/, '');
    if (clean.includes('..') || clean.includes('\0')) throw new HttpError(404, 'Not found.');
    const full = join(base, clean);
    if (!full.startsWith(base + sep) && full !== base) throw new HttpError(404, 'Not found.');
    serveFile(req, res, full, { cache });
  };

  const route = async (req: IncomingMessage, res: ServerResponse): Promise<void> => {
    const url = new URL(req.url ?? '/', 'http://local');
    const p = url.pathname;
    const method = req.method ?? 'GET';

    // ---------------------------------------------------------------- health
    if (p === '/healthz') {
      let dbOk = true;
      try {
        d.db.get('SELECT 1');
      } catch {
        dbOk = false;
      }
      const cores = existsSync(join(d.cfg.coreDir, 'fceumm.wasm'));
      return json(res, dbOk && cores ? 200 : 503, { ok: dbOk && cores, db: dbOk, cores, rooms: d.rooms.health().rooms, sessions: d.rooms.health().sessions });
    }

    // ---------------------------------------------------------------- telegram webhook
    if (p === '/telegram/webhook' && method === 'POST') {
      if (!d.bot) return json(res, 404, {});
      const body = await readJson(req, 1024 * 1024);
      const h = req.headers['x-telegram-bot-api-secret-token'];
      const status = await d.bot.handleWebhook(body, typeof h === 'string' ? h : undefined);
      return json(res, status, {});
    }

    // ---------------------------------------------------------------- cores (open-source emulator builds)
    if (p.startsWith('/cores/') && (method === 'GET' || method === 'HEAD')) {
      const name = p.slice('/cores/'.length);
      // the binaries, plus their licenses and source notice (shipped verbatim, as FBNeo's license requires)
      if (!/^([a-z0-9_]+\.(mjs|wasm)|LICENSE-[A-Za-z]+\.txt|SOURCES\.txt)$/.test(name)) throw new HttpError(404, 'Not found.');
      return staticFile(req, res, d.cfg.coreDir, name, 'public, max-age=3600');
    }

    // ---------------------------------------------------------------- API
    if (p.startsWith('/api/')) {
      // auth: Telegram Mini App launch
      if (p === '/api/auth/telegram' && method === 'POST') {
        if (!authLimiter.allow(clientIp(req))) throw new HttpError(429, 'Too many attempts; wait a minute.');
        const body = await readJson(req);
        const v = validateInitData(String(body.initData ?? ''), d.cfg.botToken, d.cfg.initDataMaxAgeSec);
        if (!v.ok) {
          log.info('auth', `initData rejected: ${v.reason}`);
          throw new HttpError(401, v.reason === 'expired' ? 'This launch link is too old. Reopen the arcade from the group.' : 'Could not verify your Telegram identity.');
        }
        const token = v.data.startParam ?? String(body.room ?? '');
        const group = d.groups.byToken(token);
        if (!group) throw new HttpError(404, 'Unknown arcade room. Use the button in your group.');
        if (group.status !== 'active') throw new HttpError(403, 'This group\'s arcade is not active (was the bot removed?).');
        const u = v.data.user;
        d.db.run(
          'INSERT INTO users (id, first_name, last_name, username, updated_at) VALUES (?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET first_name = excluded.first_name, last_name = excluded.last_name, username = excluded.username, updated_at = excluded.updated_at',
          u.id, (u.first_name ?? '').slice(0, 64), (u.last_name ?? '').slice(0, 64), u.username ?? null, Date.now(),
        );
        // A forwarded link only works for members of THIS group: verified now
        // with getChatMember, not from anything in the launch data.
        const m = await d.membership.check(group.id, u.id, { force: true });
        if (!m.isMember) throw new HttpError(403, `This arcade belongs to "${group.title}". Only its members can enter.`);
        const session = d.sessions.issue(u.id, group.id, 'miniapp');
        log.info('auth', 'mini app session issued', { group: group.id, chatType: v.data.chatType });
        return json(res, 200, { token: session.token, expiresAt: session.expiresAt, user: { id: u.id, name: displayName(u) }, group: { id: group.id, title: group.title }, tgRole: m.tgRole });
      }

      // auth: external-browser continuation (one-time link created from inside Telegram)
      if (p === '/api/auth/handoff' && method === 'POST') {
        const s = auth(req);
        await member(s);
        const t = d.sessions.createHandoff(s.userId, s.groupId);
        return json(res, 200, { url: `${d.cfg.publicUrl}/#handoff=${t}`, expiresInSec: 60 });
      }
      if (p === '/api/auth/redeem' && method === 'POST') {
        if (!authLimiter.allow(clientIp(req))) throw new HttpError(429, 'Too many attempts; wait a minute.');
        const body = await readJson(req);
        const r = d.sessions.redeemHandoff(String(body.token ?? ''));
        if (!r) throw new HttpError(401, 'This link was already used or has expired. Create a new one from the arcade in Telegram.');
        const m = await d.membership.check(r.groupId, r.userId, { force: true });
        if (!m.isMember) throw new HttpError(403, 'Only members of this group can enter.');
        const s = d.sessions.issue(r.userId, r.groupId, 'browser');
        const g = d.groups.byId(r.groupId)!;
        const u = d.db.get<any>('SELECT first_name, last_name, username FROM users WHERE id = ?', r.userId);
        return json(res, 200, { token: s.token, expiresAt: s.expiresAt, user: { id: r.userId, name: displayName(u ?? {}) }, group: { id: g.id, title: g.title }, tgRole: m.tgRole });
      }

      // dev login (DEV_MODE only, loopback only): used by local runs and e2e tests
      if (p === '/api/auth/dev' && method === 'POST') {
        const ip = req.socket.remoteAddress ?? '';
        const proxied = req.headers['x-forwarded-for'] !== undefined || req.headers['forwarded'] !== undefined;
        if (!d.cfg.devMode || proxied || !(ip === '127.0.0.1' || ip === '::1' || ip === '::ffff:127.0.0.1')) throw new HttpError(404, 'Not found.');
        const body = await readJson(req);
        const userId = Number(body.userId);
        const group = d.groups.byToken(String(body.room ?? ''));
        if (!Number.isSafeInteger(userId) || userId <= 0 || !group) throw new HttpError(400, 'bad dev login');
        const name = String(body.name ?? `Dev ${userId}`).slice(0, 48);
        d.db.run('INSERT INTO users (id, first_name, updated_at) VALUES (?, ?, ?) ON CONFLICT(id) DO UPDATE SET first_name = excluded.first_name, updated_at = excluded.updated_at', userId, name, Date.now());
        const m = await d.membership.check(group.id, userId, { force: true });
        if (!m.isMember) throw new HttpError(403, 'not a member (dev)');
        const s = d.sessions.issue(userId, group.id, 'browser');
        return json(res, 200, { token: s.token, expiresAt: s.expiresAt, user: { id: userId, name }, group: { id: group.id, title: group.title }, tgRole: m.tgRole });
      }

      const s = auth(req);
      const m = await member(s);

      if (p === '/api/me' && method === 'GET') {
        const g = d.groups.byId(s.groupId)!;
        const settings = d.db.get<any>('SELECT settings FROM user_settings WHERE user_id = ?', s.userId);
        return json(res, 200, { userId: s.userId, group: { id: g.id, title: g.title }, tgRole: m.tgRole, settings: settings ? JSON.parse(settings.settings) : null, publicUrl: d.cfg.publicUrl });
      }

      if (p === '/api/settings' && method === 'PUT') {
        const body = await readJson(req, 64 * 1024);
        const str = JSON.stringify(body ?? {});
        if (str.length > 60_000) throw new HttpError(413, 'Settings too large.');
        d.db.run('INSERT INTO user_settings (user_id, settings, updated_at) VALUES (?, ?, ?) ON CONFLICT(user_id) DO UPDATE SET settings = excluded.settings, updated_at = excluded.updated_at', s.userId, str, Date.now());
        return json(res, 200, { ok: true });
      }

      // shelf
      if (p === '/api/shelf' && method === 'GET') {
        return json(res, 200, { games: d.shelf.list(s.groupId, s.userId), usage: d.shelf.usage(s.groupId) });
      }
      if (p === '/api/shelf/upload' && method === 'POST') {
        if (uploadsInFlight.has(s.userId)) {
          res.setHeader('Connection', 'close');
          throw new HttpError(429, 'Your previous upload is still being checked. Wait for it to finish.');
        }
        if (!groupUploads.allow(String(s.groupId))) {
          res.setHeader('Connection', 'close');
          throw new HttpError(429, 'This group has uploaded a lot in the last hour. Try again later.');
        }
        uploadsInFlight.add(s.userId);
        try {
          const up = await d.webUpload(req, { tempDir, maxBytes: d.cfg.maxUploadBytes });
          const r = await d.shelf.ingest({ groupId: s.groupId, userId: s.userId, fileName: up.fileName, source: 'web', tempPath: up.tempPath, size: up.size });
          return json(res, r.status === 'done' ? 200 : 422, r);
        } finally {
          uploadsInFlight.delete(s.userId);
        }
      }
      const gm = p.match(/^\/api\/shelf\/(\d+)(\/favorite)?$/);
      if (gm) {
        const gameId = Number(gm[1]);
        if (gm[2] && method === 'POST') {
          const body = await readJson(req);
          d.shelf.setFavorite(s.groupId, s.userId, gameId, !!body.favorite);
          return json(res, 200, { ok: true });
        }
        if (method === 'GET') {
          const g = d.shelf.get(s.groupId, gameId, s.userId);
          if (!g) throw new HttpError(404, 'Not on this shelf.');
          return json(res, 200, g);
        }
        if (method === 'PATCH') {
          const g = d.shelf.get(s.groupId, gameId, s.userId);
          if (!g) throw new HttpError(404, 'Not on this shelf.');
          // uploader or moderators may edit metadata
          if (g.uploaderId !== s.userId && !canModerate(s.groupId, s.userId, m.tgRole)) throw new HttpError(403, 'Only the uploader, the host or a deputy can edit this entry.');
          const body = await readJson(req);
          return json(res, 200, d.shelf.update(s.groupId, gameId, s.userId, body));
        }
        if (method === 'DELETE') {
          if (!canModerate(s.groupId, s.userId, m.tgRole)) throw new HttpError(403, 'Only the host or a deputy can remove games.');
          d.shelf.remove(s.groupId, gameId, s.userId);
          d.records.audit(s.groupId, s.userId, 'shelf.remove', gameId, null);
          return json(res, 200, { ok: true });
        }
      }

      // private file delivery (by content hash, group-bound)
      const fm = p.match(/^\/api\/files\/([0-9a-f]{64})$/);
      if (fm && (method === 'GET' || method === 'HEAD')) {
        const blob = d.shelf.blobForGroup(s.groupId, fm[1]);
        if (!blob) throw new HttpError(404, 'Not found.');
        return serveFile(req, res, blob.path, { cache: 'private, no-store', type: 'application/octet-stream' });
      }

      // records
      if (p === '/api/records' && method === 'GET') {
        const gameId = url.searchParams.get('game');
        if (gameId) {
          const id = Number(gameId);
          if (!d.shelf.get(s.groupId, id)) throw new HttpError(404, 'Not on this shelf.');
          return json(res, 200, { scores: d.records.highScores(s.groupId, id), fighters: d.records.fighterStats(s.groupId, id), matches: d.records.recentMatches(s.groupId, id) });
        }
        return json(res, 200, { matches: d.records.recentMatches(s.groupId, null) });
      }
      const rv = p.match(/^\/api\/records\/(scores|matches)\/(\d+)$/);
      if (rv && method === 'POST') {
        if (!canModerate(s.groupId, s.userId, m.tgRole)) throw new HttpError(403, 'Only the host or a deputy can correct records.');
        const body = await readJson(req);
        const reason = String(body.reason ?? '').slice(0, 200);
        if (reason.length < 3) throw new HttpError(400, 'Give a reason for the correction.');
        const ok = rv[1] === 'scores'
          ? d.records.voidScore(s.groupId, Number(rv[2]), s.userId, reason)
          : d.records.correctMatch(s.groupId, Number(rv[2]), s.userId, body.winner === null || body.winner === undefined ? null : Number(body.winner), body.result === 'draw' ? 'draw' : body.result === 'void' ? 'void' : 'win', reason);
        return json(res, ok ? 200 : 404, { ok });
      }

      if (p === '/api/audit' && method === 'GET') {
        if (!canModerate(s.groupId, s.userId, m.tgRole)) throw new HttpError(403, 'Only the host or a deputy can view the log.');
        return json(res, 200, { entries: d.db.all('SELECT action, actor_id, target_id, reason, data, created_at FROM audit_log WHERE group_id = ? ORDER BY id DESC LIMIT 200', s.groupId) });
      }

      throw new HttpError(404, 'Not found.');
    }

    // ---------------------------------------------------------------- Mini App (static)
    if (method === 'GET' || method === 'HEAD') {
      const rel = p === '/' ? 'index.html' : p;
      const isAsset = /\.(js|css|svg|png|ico|webmanifest)$/.test(rel);
      try {
        return staticFile(req, res, d.cfg.clientDir, rel, isAsset ? 'public, max-age=300' : 'no-cache');
      } catch (e) {
        if (e instanceof HttpError && e.status === 404 && !isAsset) return staticFile(req, res, d.cfg.clientDir, 'index.html', 'no-cache');
        throw e;
      }
    }
    throw new HttpError(405, 'Method not allowed.');
  };

  const server = createServer((req, res) => {
    securityHeaders(res);
    route(req, res).catch((e) => {
      if (res.headersSent) {
        res.destroy();
        return;
      }
      if (e instanceof HttpError) return json(res, e.status, { error: e.message });
      if ((e as any)?.name === 'ShelfError') {
        const code = String((e as any).code);
        const st = SHELF_STATUS[code] ?? 409;
        if (st >= 500) log.error('http', `shelf: ${code}`);
        return json(res, st, { error: (e as Error).message, code, findings: (e as any).findings ?? [] });
      }
      const status = (e as any)?.status;
      // an oversized upload body is not read to the end: drop the connection after answering
      if (status === 413) res.setHeader('Connection', 'close');
      if (typeof status === 'number' && status >= 400 && status < 500) return json(res, status, { error: (e as Error).message });
      log.error('http', `unhandled: ${(e as Error).message}`);
      json(res, 500, { error: 'Something went wrong.' });
    });
  });
  server.on('upgrade', (req, socket, head) => {
    if ((req.url ?? '').split('?')[0] !== '/ws') {
      socket.destroy();
      return;
    }
    d.gateway.handleUpgrade(req, socket, head);
  });
  server.headersTimeout = 20_000;
  server.requestTimeout = 120_000;
  return server;
}

export function readVersion(root: string): string {
  try {
    return JSON.parse(readFileSync(join(root, 'package.json'), 'utf8')).version;
  } catch {
    return 'unknown';
  }
}
