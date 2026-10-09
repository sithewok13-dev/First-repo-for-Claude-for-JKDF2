// Validation of Telegram Mini App launch data (initData).
//
// Algorithm (core.telegram.org/bots/webapps, "Validating data received via
// the Mini App"): data-check-string = every received field except `hash`,
// sorted by key, as "key=value" joined by "\n"; secret = HMAC-SHA256 with key
// "WebAppData" over the bot token; valid iff hex(HMAC-SHA256(secret, dcs))
// equals `hash`. Nothing in initData is trusted before this check passes, and
// even then it only proves who the user is: group membership is checked
// separately with getChatMember.

import { createHmac, timingSafeEqual } from 'node:crypto';

export interface InitUser {
  id: number;
  first_name: string;
  last_name?: string;
  username?: string;
  language_code?: string;
  is_premium?: boolean;
}

export interface InitData {
  user: InitUser;
  authDate: number;
  startParam: string | null;
  chatType: string | null;
  chatInstance: string | null;
}

export type InitResult = { ok: true; data: InitData } | { ok: false; reason: string };

export function secretKey(botToken: string): Buffer {
  return createHmac('sha256', 'WebAppData').update(botToken).digest();
}

export function signInitData(fields: Record<string, string>, botToken: string): string {
  const dcs = Object.keys(fields).sort().map((k) => `${k}=${fields[k]}`).join('\n');
  const hash = createHmac('sha256', secretKey(botToken)).update(dcs).digest('hex');
  const p = new URLSearchParams(fields);
  p.set('hash', hash);
  return p.toString();
}

export function validateInitData(raw: string, botToken: string, maxAgeSec: number, nowSec = Math.floor(Date.now() / 1000)): InitResult {
  if (typeof raw !== 'string' || raw.length === 0 || raw.length > 8192) return { ok: false, reason: 'missing' };
  if (!botToken) return { ok: false, reason: 'server not configured' };
  const params = new URLSearchParams(raw);
  const seen = new Set<string>();
  const fields: [string, string][] = [];
  let hash: string | null = null;
  for (const [k, v] of params) {
    if (seen.has(k)) return { ok: false, reason: 'duplicate field' };
    seen.add(k);
    if (k === 'hash') hash = v;
    else fields.push([k, v]);
  }
  if (!hash || !/^[0-9a-f]{64}$/.test(hash)) return { ok: false, reason: 'bad hash' };
  fields.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
  const dcs = fields.map(([k, v]) => `${k}=${v}`).join('\n');
  const expected = createHmac('sha256', secretKey(botToken)).update(dcs).digest();
  const given = Buffer.from(hash, 'hex');
  if (given.length !== expected.length || !timingSafeEqual(given, expected)) return { ok: false, reason: 'signature mismatch' };

  const map = new Map(fields);
  const authDate = Number(map.get('auth_date'));
  if (!Number.isInteger(authDate) || authDate <= 0) return { ok: false, reason: 'bad auth_date' };
  if (authDate > nowSec + 60) return { ok: false, reason: 'auth_date in the future' };
  if (nowSec - authDate > maxAgeSec) return { ok: false, reason: 'expired' };

  let user: InitUser;
  try {
    user = JSON.parse(map.get('user') ?? '');
  } catch {
    return { ok: false, reason: 'bad user' };
  }
  if (!user || typeof user.id !== 'number' || !Number.isSafeInteger(user.id) || user.id <= 0) return { ok: false, reason: 'bad user' };
  if (typeof user.first_name !== 'string') user.first_name = '';
  return {
    ok: true,
    data: {
      user,
      authDate,
      startParam: map.get('start_param') ?? null,
      chatType: map.get('chat_type') ?? null,
      chatInstance: map.get('chat_instance') ?? null,
    },
  };
}

export function displayName(u: { first_name?: string; last_name?: string; username?: string }): string {
  const n = [u.first_name ?? '', u.last_name ?? ''].join(' ').replace(/\s+/g, ' ').trim();
  const name = n || (u.username ? `@${u.username}` : 'Player');
  return name.slice(0, 48);
}
