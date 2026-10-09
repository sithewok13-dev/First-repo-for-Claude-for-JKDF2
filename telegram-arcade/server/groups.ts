// Registered Telegram groups. A group keeps its internal id across a
// group -> supergroup migration; every chat id it ever had is remembered.

import { randomBytes } from 'node:crypto';
import type { Db } from './db/db.ts';

export interface Group {
  id: number;
  chatId: number;
  title: string;
  roomToken: string;
  status: 'active' | 'bot_removed' | 'disabled';
  botIsAdmin: boolean;
  settings: Record<string, unknown>;
  quotaBytes: number;
  lobbyMessageId: number | null;
  lastAnnounceAt: number;
}

function toGroup(r: any): Group {
  return {
    id: r.id,
    chatId: Number(r.chat_id),
    title: r.title,
    roomToken: r.room_token,
    status: r.status,
    botIsAdmin: !!r.bot_is_admin,
    settings: JSON.parse(r.settings || '{}'),
    quotaBytes: r.quota_bytes,
    lobbyMessageId: r.lobby_message_id ?? null,
    lastAnnounceAt: r.last_announce_at,
  };
}

export class Groups {
  private readonly db: Db;
  private readonly defaultQuota: number;

  constructor(db: Db, defaultQuota: number) {
    this.db = db;
    this.defaultQuota = defaultQuota;
  }

  byId(id: number): Group | null {
    const r = this.db.get('SELECT * FROM groups WHERE id = ?', id);
    return r ? toGroup(r) : null;
  }

  byToken(token: string): Group | null {
    if (!/^[A-Za-z0-9_-]{8,64}$/.test(token)) return null;
    const r = this.db.get('SELECT * FROM groups WHERE room_token = ?', token);
    return r ? toGroup(r) : null;
  }

  byChatId(chatId: number): Group | null {
    const r = this.db.get('SELECT g.* FROM groups g JOIN group_chat_ids c ON c.group_id = g.id WHERE c.chat_id = ?', chatId);
    return r ? toGroup(r) : null;
  }

  // Registers a chat (idempotent). New groups get a random room token.
  ensure(chatId: number, title: string): Group {
    const existing = this.byChatId(chatId);
    const now = Date.now();
    if (existing) {
      if (title && title !== existing.title) this.db.run('UPDATE groups SET title = ?, updated_at = ? WHERE id = ?', title, now, existing.id);
      return this.byId(existing.id)!;
    }
    const token = randomBytes(12).toString('base64url');
    return this.db.tx(() => {
      const r = this.db.run(
        'INSERT INTO groups (chat_id, title, room_token, quota_bytes, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?)',
        chatId, title, token, this.defaultQuota, now, now,
      );
      this.db.run('INSERT INTO group_chat_ids (chat_id, group_id, created_at) VALUES (?, ?, ?)', chatId, r.lastInsertRowid, now);
      return this.byId(r.lastInsertRowid)!;
    });
  }

  // Group -> supergroup migration: same group, new chat id.
  migrate(oldChatId: number, newChatId: number): Group | null {
    const g = this.byChatId(oldChatId);
    if (!g) return null;
    const now = Date.now();
    this.db.tx(() => {
      this.db.run('UPDATE groups SET chat_id = ?, updated_at = ? WHERE id = ?', newChatId, now, g.id);
      this.db.run('INSERT OR IGNORE INTO group_chat_ids (chat_id, group_id, created_at) VALUES (?, ?, ?)', newChatId, g.id, now);
    });
    return this.byId(g.id);
  }

  setStatus(id: number, status: Group['status']): void {
    this.db.run('UPDATE groups SET status = ?, updated_at = ? WHERE id = ?', status, Date.now(), id);
  }

  setBotAdmin(id: number, isAdmin: boolean, rights: Record<string, unknown>): void {
    this.db.run('UPDATE groups SET bot_is_admin = ?, bot_rights = ?, updated_at = ? WHERE id = ?', isAdmin ? 1 : 0, JSON.stringify(rights), Date.now(), id);
  }

  setLobbyMessage(id: number, messageId: number | null): void {
    this.db.run('UPDATE groups SET lobby_message_id = ? WHERE id = ?', messageId, id);
  }

  touchAnnounce(id: number, at: number): void {
    this.db.run('UPDATE groups SET last_announce_at = ? WHERE id = ?', at, id);
  }

  updateSettings(id: number, settings: Record<string, unknown>): void {
    this.db.run('UPDATE groups SET settings = ?, updated_at = ? WHERE id = ?', JSON.stringify(settings), Date.now(), id);
  }

  all(): Group[] {
    return this.db.all('SELECT * FROM groups ORDER BY id').map(toGroup);
  }
}
