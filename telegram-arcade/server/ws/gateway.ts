// Authenticated real-time connections.
//
// A socket is useless until its first message authenticates it with one of
// our session tokens (never in the URL, so it does not land in proxy logs).
// The token is bound to one user and one group; membership is re-checked
// before the socket joins the room. Binary frames are controller input;
// text frames are JSON commands. Abusive clients are rate-limited and slow
// consumers are disconnected (they resync on reconnect) instead of buffering
// without bound.

import type { IncomingMessage } from 'node:http';
import type { Duplex } from 'node:stream';
import { randomUUID } from 'node:crypto';
import { WebSocketServer, type WebSocket, type RawData } from 'ws';
import type { AuthSessions } from '../auth/sessions.ts';
import type { Db } from '../db/db.ts';
import type { Groups } from '../groups.ts';
import { log } from '../log.ts';
import type { RoomManager, Conn } from '../room/manager.ts';
import type { MembershipService } from '../telegram/types.ts';

const AUTH_TIMEOUT_MS = 5000;
const MAX_JSON_BYTES = 8 * 1024;
const MAX_BUFFERED = 4 * 1024 * 1024;
const JSON_RATE = { tokens: 30, perSec: 15 };
const BIN_RATE = { tokens: 360, perSec: 240 };

class Bucket {
  private tokens: number;
  private last = Date.now();
  private readonly cap: number;
  private readonly rate: number;
  constructor(cap: number, rate: number) {
    this.cap = cap;
    this.rate = rate;
    this.tokens = cap;
  }
  take(): boolean {
    const now = Date.now();
    this.tokens = Math.min(this.cap, this.tokens + ((now - this.last) / 1000) * this.rate);
    this.last = now;
    if (this.tokens < 1) return false;
    this.tokens -= 1;
    return true;
  }
}

export interface GatewayDeps {
  sessions: AuthSessions;
  membership: MembershipService;
  groups: Groups;
  rooms: RoomManager;
  db: Db;
  allowedOrigins: string[];      // empty = any (dev)
}

export class Gateway {
  private readonly wss: WebSocketServer;
  private readonly d: GatewayDeps;
  private readonly conns = new Map<string, { ws: WebSocket; conn: Conn | null }>();
  private heartbeat: NodeJS.Timeout;

  constructor(deps: GatewayDeps) {
    this.d = deps;
    this.wss = new WebSocketServer({ noServer: true, maxPayload: 64 * 1024, perMessageDeflate: false });
    this.heartbeat = setInterval(() => {
      for (const { ws } of this.conns.values()) {
        if ((ws as any).isAlive === false) {
          ws.terminate();
          continue;
        }
        (ws as any).isAlive = false;
        try {
          ws.ping();
        } catch { /* closed */ }
      }
    }, 20_000);
    this.heartbeat.unref();
    deps.membership.onRevoked((groupId, userId) => deps.rooms.revokeUser(groupId, userId));
  }

  handleUpgrade(req: IncomingMessage, socket: Duplex, head: Buffer): void {
    const origin = req.headers.origin ?? '';
    if (this.d.allowedOrigins.length && !this.d.allowedOrigins.includes(origin)) {
      socket.write('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');
      socket.destroy();
      return;
    }
    this.wss.handleUpgrade(req, socket, head, (ws) => this.onSocket(ws));
  }

  private onSocket(ws: WebSocket): void {
    const id = randomUUID();
    const entry: { ws: WebSocket; conn: Conn | null } = { ws, conn: null };
    this.conns.set(id, entry);
    (ws as any).isAlive = true;
    ws.on('pong', () => { (ws as any).isAlive = true; });
    const jsonRate = new Bucket(JSON_RATE.tokens, JSON_RATE.perSec);
    const binRate = new Bucket(BIN_RATE.tokens, BIN_RATE.perSec);
    const authTimer = setTimeout(() => ws.close(4000, 'authentication timeout'), AUTH_TIMEOUT_MS);
    let authing = false;

    ws.on('message', async (data: RawData, isBinary: boolean) => {
      const buf = Array.isArray(data) ? Buffer.concat(data) : Buffer.from(data as ArrayBuffer);
      if (!entry.conn) {
        if (isBinary || authing || buf.length > MAX_JSON_BYTES) return ws.close(4000, 'authenticate first');
        authing = true;
        let msg: any;
        try {
          msg = JSON.parse(buf.toString('utf8'));
        } catch {
          return ws.close(4000, 'bad message');
        }
        if (msg?.t !== 'auth') return ws.close(4000, 'authenticate first');
        const result = await this.authenticate(id, ws, String(msg.token ?? ''));
        clearTimeout(authTimer);
        if (!result.ok) {
          ws.send(JSON.stringify({ t: 'auth_error', error: result.error }));
          return ws.close(4003, result.error.slice(0, 100));
        }
        entry.conn = result.conn;
        ws.send(JSON.stringify({ t: 'welcome', connId: id, userId: result.conn.userId, groupId: result.conn.groupId, serverTime: Date.now() }));
        const att = this.d.rooms.attach(result.conn);
        if (!att.ok) {
          ws.send(JSON.stringify({ t: 'auth_error', error: att.error }));
          entry.conn = null;
          return ws.close(4005, 'room full');
        }
        return;
      }
      const conn = entry.conn;
      if (isBinary) {
        if (!binRate.take()) return;
        this.d.rooms.handleBinary(conn, new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength));
        return;
      }
      if (buf.length > MAX_JSON_BYTES || !jsonRate.take()) {
        conn.sendJson({ t: 'error', code: 'rate_limited', error: 'Too many messages.' });
        return;
      }
      let msg: unknown;
      try {
        msg = JSON.parse(buf.toString('utf8'));
      } catch {
        return;
      }
      await this.d.rooms.handleJson(conn, msg);
    });

    ws.on('close', () => {
      clearTimeout(authTimer);
      this.conns.delete(id);
      if (entry.conn) this.d.rooms.detach(entry.conn);
    });
    ws.on('error', () => { /* close follows */ });
  }

  private async authenticate(id: string, ws: WebSocket, token: string): Promise<{ ok: true; conn: Conn } | { ok: false; error: string }> {
    const s = this.d.sessions.verify(token);
    if (!s) return { ok: false, error: 'Your session expired. Reopen the arcade from the group.' };
    const group = this.d.groups.byId(s.groupId);
    if (!group || group.status !== 'active') return { ok: false, error: 'This group\'s arcade is not active.' };
    const m = await this.d.membership.check(s.groupId, s.userId);
    if (!m.isMember) {
      this.d.sessions.revokeUser(s.groupId, s.userId);
      return { ok: false, error: 'Only members of this group can enter its arcade.' };
    }
    const u = this.d.db.get<any>('SELECT first_name, last_name, username FROM users WHERE id = ?', s.userId);
    const name = u ? ([u.first_name, u.last_name].filter(Boolean).join(' ').trim() || (u.username ? '@' + u.username : 'Player')) : 'Player';
    const send = (payload: string | Uint8Array, binary: boolean) => {
      if (ws.readyState !== ws.OPEN) return;
      if (ws.bufferedAmount > MAX_BUFFERED) {
        log.warn('ws', 'closing a slow connection');
        ws.close(4008, 'connection too slow');
        return;
      }
      ws.send(payload, { binary });
    };
    const conn: Conn = {
      id,
      userId: s.userId,
      groupId: s.groupId,
      name: name.slice(0, 48),
      tgRole: m.tgRole,
      sendJson: (obj) => send(JSON.stringify(obj), false),
      sendBin: (d) => send(d, true),
      close: (code, reason) => { try { ws.close(code, reason); } catch { /* closed */ } },
      bufferedAmount: () => ws.bufferedAmount,
    };
    return { ok: true, conn };
  }

  close(): void {
    clearInterval(this.heartbeat);
    for (const { ws } of this.conns.values()) ws.terminate();
    this.wss.close();
  }
}
