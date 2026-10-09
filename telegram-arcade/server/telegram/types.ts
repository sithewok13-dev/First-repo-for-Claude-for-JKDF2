// Contract of the Telegram integration (bot updates, group lifecycle,
// membership checks, announcements). Implemented in ./bot.ts and
// ./membership.ts.

import type { TgRole } from '../room/types.ts';
import type { MigrationResult } from './migration.ts';

export interface ChatMemberInfo {
  status: 'creator' | 'administrator' | 'member' | 'restricted' | 'left' | 'kicked' | 'unknown';
  isMember: boolean;
  tgRole: TgRole;
}

export interface MembershipService {
  // Verified membership of a Telegram user in a registered group. Cached for
  // MEMBERSHIP_TTL_SEC; `force` bypasses the cache.
  check(groupId: number, userId: number, opts?: { force?: boolean }): Promise<ChatMemberInfo>;
  // Called when a chat_member update arrives or access must be re-checked.
  invalidate(groupId: number, userId: number): void;
  // Listeners are told when a user stops being a member (to revoke access).
  onRevoked(fn: (groupId: number, userId: number) => void): void;
  // Applies a status learned from a chat_member update without an API call.
  // A loss takes effect at once (cached as not a member, onRevoked fires); a
  // gain only invalidates, so access is re-verified with getChatMember.
  observe?(groupId: number, userId: number, info: ChatMemberInfo): void;
  // Listeners are told when a check is the first to learn that the group
  // moved to a supergroup (Telegram's "group chat was upgraded" error) and
  // the move was applied, so a merged duplicate registration can be wound up.
  onMigrated?(fn: (r: MigrationResult) => void): void;
}

// What the bot needs from the rest of the server (wired in main).
export interface BotHooks {
  // The bot was removed from the group: stop its room (sessions are already revoked).
  onGroupDisabled(groupId: number): void;
  // A user left or was removed (sessions are already revoked): evict them.
  onMembershipRevoked(groupId: number, userId: number): void;
  // Optional: a member's Telegram status changed without them leaving
  // (promoted, demoted, restricted). Their cached membership is already
  // invalidated; re-check it (MembershipService.check) and refresh the role
  // of any live room connection, so a demoted admin loses moderator powers
  // at once rather than at their next reconnect.
  onMemberChanged?(groupId: number, userId: number): void;
  // /hostme from a verified Telegram admin/creator of the group.
  claimHost(groupId: number, userId: number): { ok: boolean; message: string } | Promise<{ ok: boolean; message: string }>;
  // Optional one-line plain-text room status for the lobby card, e.g.
  // "Now playing: Street Fighter II (2 playing, 3 watching)".
  roomSummary?(groupId: number): string | null;
}

export interface BotService {
  start(): Promise<void>;
  stop(): void;
  // Stops and waits for in-flight background work (downloads, ingests, sends).
  shutdown?(): Promise<void>;
  // Webhook entry point: validates X-Telegram-Bot-Api-Secret-Token and
  // processes the update idempotently. Returns an HTTP status.
  handleWebhook(body: unknown, secretHeader: string | undefined): Promise<number>;
  // Rate-limited, useful-only announcements to the group thread.
  announce(groupId: number, text: string, opts?: { key?: string; delayMs?: number }): void;
  getChatMember(chatId: number, userId: number): Promise<ChatMemberInfo>;
  launchUrl(roomToken: string): string;
}
