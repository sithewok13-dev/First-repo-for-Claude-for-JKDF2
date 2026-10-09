// Our own bearer tokens, issued after Telegram initData + membership checks.
// Only a SHA-256 of each token is stored. Tokens are bound to one user AND
// one group: a token for group A can never open group B's room, shelf,
// files, chat or records.

import { createHash, randomBytes } from 'node:crypto';
import type { Db } from '../db/db.ts';

export interface AuthSession {
  userId: number;
  groupId: number;
  kind: 'miniapp' | 'browser';
  expiresAt: number;
}

const hash = (t: string) => createHash('sha256').update(t).digest('hex');

export class AuthSessions {
  private readonly db: Db;
  private readonly ttlMs: number;

  constructor(db: Db, ttlSec: number) {
    this.db = db;
    this.ttlMs = ttlSec * 1000;
  }

  issue(userId: number, groupId: number, kind: AuthSession['kind']): { token: string; expiresAt: number } {
    const token = randomBytes(32).toString('base64url');
    const now = Date.now();
    const expiresAt = now + this.ttlMs;
    this.db.run('INSERT INTO auth_sessions (token_hash, user_id, group_id, kind, created_at, expires_at) VALUES (?, ?, ?, ?, ?, ?)', hash(token), userId, groupId, kind, now, expiresAt);
    return { token, expiresAt };
  }

  verify(token: string | null | undefined): AuthSession | null {
    if (!token || token.length < 20 || token.length > 100) return null;
    const r = this.db.get('SELECT user_id, group_id, kind, expires_at, revoked_at FROM auth_sessions WHERE token_hash = ?', hash(token));
    if (!r || r.revoked_at || r.expires_at < Date.now()) return null;
    return { userId: Number(r.user_id), groupId: r.group_id, kind: r.kind, expiresAt: r.expires_at };
  }

  revokeUser(groupId: number, userId: number): number {
    return this.db.run('UPDATE auth_sessions SET revoked_at = ? WHERE group_id = ? AND user_id = ? AND revoked_at IS NULL', Date.now(), groupId, userId).changes;
  }

  revokeGroup(groupId: number): number {
    return this.db.run('UPDATE auth_sessions SET revoked_at = ? WHERE group_id = ? AND revoked_at IS NULL', Date.now(), groupId).changes;
  }

  // One-time, short-lived link to continue in an external browser as the
  // same verified user (fallback when an in-Telegram webview lacks a feature).
  createHandoff(userId: number, groupId: number, ttlMs = 60_000): string {
    const token = randomBytes(24).toString('base64url');
    this.db.run('INSERT INTO handoffs (token_hash, user_id, group_id, expires_at) VALUES (?, ?, ?, ?)', hash(token), userId, groupId, Date.now() + ttlMs);
    return token;
  }

  redeemHandoff(token: string): { userId: number; groupId: number } | null {
    if (!token || token.length > 100) return null;
    const h = hash(token);
    const r = this.db.get('SELECT user_id, group_id, expires_at, used_at FROM handoffs WHERE token_hash = ?', h);
    if (!r || r.used_at || r.expires_at < Date.now()) return null;
    const res = this.db.run('UPDATE handoffs SET used_at = ? WHERE token_hash = ? AND used_at IS NULL', Date.now(), h);
    if (res.changes !== 1) return null; // raced with another redemption
    return { userId: Number(r.user_id), groupId: r.group_id };
  }

  purgeExpired(): void {
    const now = Date.now();
    this.db.run('DELETE FROM auth_sessions WHERE expires_at < ?', now - 86400_000);
    this.db.run('DELETE FROM handoffs WHERE expires_at < ?', now - 3600_000);
  }
}
