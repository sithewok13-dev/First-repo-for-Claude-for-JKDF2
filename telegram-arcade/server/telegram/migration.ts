// Group -> supergroup migration.
//
// Telegram gives the supergroup a NEW chat id and reports the move several
// ways: a service message with migrate_to_chat_id in the old group, one with
// migrate_from_chat_id in the new supergroup, and a 400 error carrying
// parameters.migrate_to_chat_id when the bot writes to the old id. Updates
// for different chats are not ordered relative to each other, so any of these
// can arrive first, more than once, or after other updates from the new
// supergroup. Promoting the bot with specific admin rights in a basic group is
// itself a common trigger for the upgrade.
//
// The group keeps its internal id, shelf, records and room token: only its
// current chat id changes (Groups.migrate, which also remembers the old id).
// If the new supergroup was already registered on its own (its my_chat_member
// update won the race), that fresh, empty registration is folded into the
// original group.

import type { Db } from '../db/db.ts';
import type { Group, Groups } from '../groups.ts';
import { log } from '../log.ts';

export interface MigrationResult {
  group: Group;
  changed: boolean;               // false when the move was already known
  absorbedGroupId: number | null; // duplicate registration that was merged away
}

// Light per-group state a seconds-old registration may already have. Anything
// heavier (games, uploads, sessions, records) means it is not a throwaway
// duplicate, and the merge is refused (foreign keys also enforce this).
const DUPLICATE_STATE_TABLES = ['memberships', 'auth_sessions', 'handoffs', 'roles', 'favorites', 'room_state', 'chat_messages'];

function absorbDuplicate(db: Db, dup: Group, into: Group): boolean {
  try {
    db.tx(() => {
      const used = db.get<{ n: number }>(
        'SELECT (SELECT COUNT(*) FROM games WHERE group_id = ?) + (SELECT COUNT(*) FROM uploads WHERE group_id = ?) + (SELECT COUNT(*) FROM game_sessions WHERE group_id = ?) AS n',
        dup.id, dup.id, dup.id,
      );
      if (used && used.n > 0) throw new Error('the other registration already has content');
      const row = db.get<{ status: string; bot_is_admin: number; bot_rights: string; lobby_message_id: number | null }>(
        'SELECT status, bot_is_admin, bot_rights, lobby_message_id FROM groups WHERE id = ?', dup.id,
      );
      for (const t of DUPLICATE_STATE_TABLES) db.run(`DELETE FROM ${t} WHERE group_id = ?`, dup.id);
      db.run('DELETE FROM group_chat_ids WHERE group_id = ?', dup.id);
      db.run('DELETE FROM groups WHERE id = ?', dup.id);
      // The duplicate saw the bot's newest status in the supergroup (often the
      // promotion that caused the upgrade) and owns the card posted there. Its
      // presence there also outranks a "bot left" reported for the old basic
      // group: the group is active again (an operator's 'disabled' stays).
      if (row) {
        db.run(
          `UPDATE groups SET bot_is_admin = ?, bot_rights = ?, lobby_message_id = ?, updated_at = ?,
             status = CASE WHEN status = 'bot_removed' AND ? = 'active' THEN 'active' ELSE status END
           WHERE id = ?`,
          row.bot_is_admin, row.bot_rights, row.lobby_message_id ?? null, Date.now(), row.status, into.id);
      }
    });
    log.warn('telegram', 'merged a duplicate registration of a migrated group', { group: into.id, duplicate: dup.id });
    return true;
  } catch (e) {
    log.error('telegram', 'cannot merge a duplicate registration of a migrated group', { group: into.id, duplicate: dup.id, err: (e as Error).message });
    return false;
  }
}

// Idempotent. Returns null when the old chat is not a registered group or the
// move cannot be applied.
export function migrateChat(db: Db, groups: Groups, oldChatId: number, newChatId: number): MigrationResult | null {
  if (!Number.isSafeInteger(oldChatId) || !Number.isSafeInteger(newChatId) || oldChatId === newChatId) return null;
  const from = groups.byChatId(oldChatId);
  if (!from) return null;
  const dup = groups.byChatId(newChatId);
  let absorbedGroupId: number | null = null;
  if (dup && dup.id !== from.id) {
    if (!absorbDuplicate(db, dup, from)) return null;
    absorbedGroupId = dup.id;
  }
  const changed = from.chatId !== newChatId;
  const g = groups.migrate(oldChatId, newChatId);
  if (!g) return null;
  // A card in the old chat can no longer be edited; one adopted from the
  // duplicate already lives in the new chat.
  if (changed && absorbedGroupId === null) groups.setLobbyMessage(g.id, null);
  if (changed) log.info('telegram', 'group moved to a new chat id (supergroup upgrade)', { group: g.id });
  return { group: groups.byId(g.id)!, changed, absorbedGroupId };
}
