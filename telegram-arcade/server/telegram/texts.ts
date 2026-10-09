// Texts the bot posts. Pure functions; every value that comes from users or
// files (titles, file names, game names, validation messages) is escaped for
// Telegram's HTML parse mode. Group messages are short on purpose: gameplay
// chat and details live inside the Mini App.

import type { RoomMode } from '../adapters/types.ts';
import type { IngestResult, ShelfGame } from '../shelf/types.ts';
import { SYSTEMS } from '../../shared/systems.ts';

export function esc(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

// Telegram rejects messages over 4096 characters (counted after HTML
// parsing). Untrusted pieces are clipped, and multi-item messages stop
// adding lines well before the limit, measured on the HTML itself (an upper
// bound of the parsed length).
export const MESSAGE_BUDGET = 3500;

// At most `max` characters (code points), with an ellipsis when cut. Clip
// before escaping, so an entity is never cut in half.
export function clip(s: string, max: number): string {
  const chars = Array.from(String(s ?? ''));
  return chars.length <= max ? chars.join('') : chars.slice(0, max - 1).join('') + '…';
}

// File names are untrusted: show at most one line of printable text.
export function displayFileName(name: string): string {
  const base = name.split(/[\\/]/).pop() ?? '';
  const clean = base.replace(/[\u0000-\u001f\u007f-\u009f​-‏‪-‮⁦-⁩]/g, '').trim();
  return (clean || 'file').slice(0, 80);
}

export function mb(bytes: number): string {
  const v = bytes / (1024 * 1024);
  if (v >= 1024) return `${(v / 1024).toFixed(1).replace(/\.0$/, '')} GB`;
  return `${v < 10 ? v.toFixed(1).replace(/\.0$/, '') : Math.round(v)} MB`;
}

// Inline url button with the direct Mini App link (works in groups, unlike
// web_app buttons).
export function launchKeyboard(url: string, label = '🎮 Open arcade'): { inline_keyboard: { text: string; url: string }[][] } {
  return { inline_keyboard: [[{ text: label, url }]] };
}

const MODE_LABEL: Record<RoomMode, string> = {
  versus: 'versus',
  coop: 'co-op',
  single: 'single player',
  'turns-shared': 'turns, shared controller',
  'turns-multi': 'turns',
  collab: 'pass-around',
};

export interface UploadLimits {
  maxUploadBytes: number;
  cloudApi: boolean;               // files over 20 MB cannot be fetched
}

function uploadHint(l: UploadLimits): string {
  const max = l.cloudApi ? Math.min(l.maxUploadBytes, 20 * 1024 * 1024) : l.maxUploadBytes;
  return `Post a game file here: an arcade <code>.zip</code> set or a NES <code>.nes</code> file, up to ${mb(max)}.` +
    (l.cloudApi && l.maxUploadBytes > 20 * 1024 * 1024 ? ` Bigger files: use <b>Upload from device</b> on the shelf (up to ${mb(l.maxUploadBytes)}).` : '');
}

// Basic groups have no per-right choice (and only the owner can promote);
// supergroups let every right be switched off.
const ADMIN_ASK =
  'Make me an <b>admin</b> of this group. Where Telegram lets you pick permissions, switch them all off: I never delete, pin, ban, invite or change anything. ' +
  'Admin status alone lets me see the game files you post and check who is a member when someone opens the arcade.';

export function welcomeText(o: { title: string; isAdmin: boolean } & UploadLimits): string {
  return [
    `🕹 <b>Arcade ready${o.title ? ` for ${esc(o.title)}` : ''}</b>`,
    '',
    `${uploadHint(o)} I'll put it on this group's private shelf. Then tap <b>Open arcade</b> to play together, watch, chat and queue.`,
    '',
    o.isAdmin
      ? '✅ I\'m an admin, so uploads and member checks work. None of my admin permissions are needed: they can all be switched off.'
      : `⚠️ <b>One setup step:</b> ${ADMIN_ASK}`,
    '',
    '/arcade launch button · /shelf games · /hostme admins: become room host · /help',
  ].join('\n');
}

export function promotedText(): string {
  return '✅ Thanks, I\'m an admin now: I can see game files posted here and check membership when someone opens the arcade. None of my admin permissions are needed: they can all be switched off.';
}

export function demotedText(): string {
  return `⚠️ I'm no longer an admin, so I can't see game files posted here and membership checks may fail. ${ADMIN_ASK}`;
}

export function notAllowedText(): string {
  return 'Sorry, this arcade bot is private and is not set up for this group, so I\'m leaving. Ask the arcade\'s operator if you think this is a mistake.';
}

export function lobbyText(o: { title: string; games: number; summary: string | null }): string {
  const lines = [
    `🕹 <b>${o.title ? `${esc(o.title)} arcade` : 'Arcade'}</b>`,
    o.games === 0 ? 'The shelf is empty: post a game file here to add one.' : `${o.games} game${o.games === 1 ? '' : 's'} on the shelf.`,
  ];
  if (o.summary) lines.push(esc(o.summary.slice(0, 200)));
  lines.push('Tap <b>Open arcade</b> to play, watch, chat and queue.');
  return lines.join('\n');
}

export function helpText(l: UploadLimits): string {
  return [
    '🕹 <b>Arcade help</b>',
    `• <b>Add games:</b> ${uploadHint(l)} 7z and RAR archives aren't supported: re-pack them as .zip.`,
    '• <b>Play:</b> tap <b>Open arcade</b> (/arcade). Only members of this group can enter.',
    '• /arcade posts the launch button · /shelf lists the games · /add (as a reply to an earlier game file) puts it on the shelf · /hostme lets a group admin become the room host.',
    'Gameplay chat lives inside the arcade; here I only post short updates.',
  ].join('\n');
}

export function privateText(): string {
  return [
    '👋 I run a private retro arcade for Telegram groups.',
    '',
    'Add me to your group and make me an admin there (where Telegram lets you pick permissions, switch them all off). Then post a game file in the group and tap <b>Open arcade</b>. Everything happens inside the group; there is nothing to do here.',
  ].join('\n');
}

// ---------------------------------------------------------------- uploads

export function unsupportedArchiveText(fileName: string): string {
  return `📦 <b>${esc(displayFileName(fileName))}</b>: 7z and RAR archives aren't supported. Re-pack it as a .zip (for arcade sets keep the original file names inside) and post it again.`;
}

export function tooBigForArcadeText(fileName: string, size: number, maxBytes: number): string {
  return `📦 <b>${esc(displayFileName(fileName))}</b> is ${mb(size)}; this arcade accepts files up to ${mb(maxBytes)}.`;
}

export function cloudLimitText(fileName: string, size: number | null): string {
  return [
    `📦 <b>${esc(displayFileName(fileName))}</b>${size ? ` is ${mb(size)}. Telegram` : ': Telegram'} only lets bots fetch files up to 20 MB, so I can't pick this one up from the chat.`,
    'Open the arcade and use <b>Upload from device</b> on the shelf instead.',
    '<i>Operators can lift this limit by running a local Bot API server.</i>',
  ].join('\n');
}

export function anonymousUploadText(fileName: string): string {
  return `I can't tell who posted <b>${esc(displayFileName(fileName))}</b> (it was sent anonymously). Please post it from your own account.`;
}

export function busyText(fileName: string): string {
  return `⏳ I'm still working through earlier uploads. Please post <b>${esc(displayFileName(fileName))}</b> again in a few minutes.`;
}

export interface CardItem {
  fileName: string;
  messageId: number;
  result?: IngestResult;
  existing?: ShelfGame;            // the same Telegram file is already on the shelf
  failure?: 'too_big' | 'download' | 'internal';
  notice?: string;                 // a ready-made (HTML) explanation, e.g. unsupported or too big
}

function statusLine(g: ShelfGame): string {
  if (g.kind !== 'game') return `📦 Added as ${g.kind === 'bios' ? 'a BIOS' : 'a parent set'} for other games.`;
  const firstFinding = (level: 'warn' | 'error') => {
    const msg = g.validation?.findings?.find((f) => f.level === level)?.message;
    return typeof msg === 'string' && msg ? clip(msg, 200) : undefined;
  };
  switch (g.status) {
    case 'ready':
      return '✅ Ready to play';
    case 'needs_dependency': {
      const missing = (g.validation?.missing ?? []).map((m) => `${m.kind === 'bios' ? 'BIOS' : m.kind} ${m.set}`);
      return `⏳ Missing ${missing.length ? esc(clip(missing.join(', '), 200)) : 'files'}: post ${missing.length === 1 ? 'it' : 'them'} here to finish`;
    }
    case 'needs_attention':
      return `⚠️ Needs attention${firstFinding('warn') ? `: ${esc(firstFinding('warn')!)}` : ''}`;
    case 'rejected':
      return `❌ Not playable${firstFinding('error') ? `: ${esc(firstFinding('error')!)}` : ''}`;
    default:
      return '❌ Removed from the shelf';
  }
}

function details(g: ShelfGame): string {
  const parts: string[] = [];
  if (g.system) parts.push(SYSTEMS[g.system]?.label ?? g.system);
  if (g.players) parts.push(`${g.players} player${g.players === 1 ? '' : 's'}`);
  if (g.mode) parts.push(MODE_LABEL[g.mode] ?? g.mode);
  return esc(parts.join(' · '));
}

function failureText(item: CardItem): string {
  if (item.failure === 'too_big') return 'too big to fetch from Telegram; use Upload from device on the shelf';
  if (item.failure === 'download') return 'could not fetch the file from Telegram; please post it again';
  if (item.failure === 'internal') return 'something went wrong on our side; please try again later';
  return item.result?.error ? clip(item.result.error, 200) : 'not added';
}

function gameName(g: ShelfGame): string {
  return esc(clip(g.displayName, 100));
}

// A full card for one upload.
export function gameCard(item: CardItem): string {
  if (item.notice) return item.notice;
  const g = item.existing ?? item.result?.game;
  if (!g) return `❌ <b>${esc(displayFileName(item.fileName))}</b>: ${esc(failureText(item))}`;
  const dup = item.existing || item.result?.duplicate;
  const lines = [`🕹 <b>${gameName(g)}</b>${dup ? ' (already on the shelf)' : ''}`];
  const d = details(g);
  if (d) lines.push(d);
  lines.push(statusLine(g));
  return lines.join('\n');
}

// One message for several uploads that finished together. `total` counts
// uploads beyond `items` too (the bot keeps a bounded list per message).
export function cardSummary(items: CardItem[], maxLines = 10, total = items.length): string {
  const lines = [`📥 <b>${total} uploads processed</b>`];
  let length = lines[0].length;
  let shown = 0;
  for (const item of items.slice(0, maxLines)) {
    const g = item.existing ?? item.result?.game;
    let line: string;
    if (item.notice) line = `• ${item.notice}`;
    else if (!g) line = `• <b>${esc(displayFileName(item.fileName))}</b>: ❌ ${esc(failureText(item))}`;
    else line = `• <b>${gameName(g)}</b>: ${statusLine(g)}${item.existing || item.result?.duplicate ? ' (already on the shelf)' : ''}`;
    if (length + line.length + 1 > MESSAGE_BUDGET) break;
    lines.push(line);
    length += line.length + 1;
    shown++;
  }
  if (total > shown) lines.push(`…and ${total - shown} more. Open the arcade for details.`);
  return lines.join('\n');
}

export function shelfText(games: ShelfGame[], usage: { bytes: number; quota: number; games: number }, maxLines = 12): string {
  const playable = games.filter((g) => g.kind === 'game' && g.status !== 'removed' && g.status !== 'rejected');
  const lines = [`🗂 <b>Shelf</b> · ${playable.length} game${playable.length === 1 ? '' : 's'} · ${mb(usage.bytes)} of ${mb(usage.quota)}`];
  if (!playable.length) lines.push('Nothing here yet: post a game file in this group to add one.');
  for (const g of playable.slice(0, maxLines)) {
    const mark = g.status === 'ready' ? '✅' : g.status === 'needs_dependency' ? '⏳' : '⚠️';
    const d = details(g);
    lines.push(`${mark} ${gameName(g)}${d ? ` (${d})` : ''}`);
  }
  if (playable.length > maxLines) lines.push(`…and ${playable.length - maxLines} more.`);
  if (playable.length) lines.push('Open the arcade to browse, favorite and vote.');
  return lines.join('\n');
}
