// REST helpers and authentication bootstrap.

import { tg } from './telegram.ts';

const TOKEN_KEY = 'arcade.token';

export interface Me {
  token: string;
  user: { id: number; name: string };
  group: { id: number; title: string };
  tgRole: string;
}

let token: string | null = null;

export function getToken(): string | null {
  return token;
}

function remember(t: string): void {
  token = t;
  try {
    sessionStorage.setItem(TOKEN_KEY, t);
  } catch { /* storage unavailable */ }
}

export async function api<T = any>(path: string, init: RequestInit = {}): Promise<T> {
  const headers = new Headers(init.headers);
  if (token) headers.set('Authorization', `Bearer ${token}`);
  if (init.body && typeof init.body === 'string') headers.set('Content-Type', 'application/json');
  const res = await fetch(path, { ...init, headers });
  const text = await res.text();
  let body: any = null;
  try {
    body = text ? JSON.parse(text) : null;
  } catch { /* not json */ }
  if (!res.ok) {
    const err = new Error(body?.error ?? `Request failed (${res.status})`);
    (err as any).status = res.status;
    (err as any).body = body;
    throw err;
  }
  return body as T;
}

// Signs in with, in order: Telegram launch data, a one-time handoff link
// (external browser), a stored session, or a local dev login.
export async function signIn(): Promise<Me> {
  const hash = new URLSearchParams(location.hash.slice(1));
  const initData = tg()?.initData;
  if (initData) {
    const me = await api<Me>('/api/auth/telegram', { method: 'POST', body: JSON.stringify({ initData }) });
    remember(me.token);
    return me;
  }
  const handoff = hash.get('handoff');
  if (handoff) {
    history.replaceState(null, '', location.pathname + location.search);
    const me = await api<Me>('/api/auth/redeem', { method: 'POST', body: JSON.stringify({ token: handoff }) });
    remember(me.token);
    return me;
  }
  const q = new URLSearchParams(location.search);
  if (q.get('dev') === '1') {
    const me = await api<Me>('/api/auth/dev', {
      method: 'POST',
      body: JSON.stringify({ userId: Number(q.get('user')), name: q.get('name') ?? undefined, room: q.get('room') }),
    });
    remember(me.token);
    return me;
  }
  let stored: string | null = null;
  try {
    stored = sessionStorage.getItem(TOKEN_KEY);
  } catch { /* storage unavailable */ }
  if (stored) {
    token = stored;
    try {
      const me = await api<any>('/api/me');
      return { token: stored, user: { id: me.userId, name: '' }, group: me.group, tgRole: me.tgRole };
    } catch {
      token = null;
    }
  }
  throw new Error('Open the arcade from the button in your Telegram group.');
}

// Private ROM/BIOS files, cached locally by content hash so a returning
// player does not download them again. Never stored anywhere public.
const DB_NAME = 'arcade-files';

function idb(): Promise<IDBDatabase | null> {
  return new Promise((resolve) => {
    try {
      const req = indexedDB.open(DB_NAME, 1);
      req.onupgradeneeded = () => req.result.createObjectStore('files');
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => resolve(null);
    } catch {
      resolve(null);
    }
  });
}

async function cacheGet(sha: string): Promise<Uint8Array | null> {
  const db = await idb();
  if (!db) return null;
  return new Promise((resolve) => {
    const tx = db.transaction('files', 'readonly').objectStore('files').get(sha);
    tx.onsuccess = () => resolve(tx.result ? new Uint8Array(tx.result) : null);
    tx.onerror = () => resolve(null);
  });
}

async function cachePut(sha: string, data: Uint8Array): Promise<void> {
  const db = await idb();
  if (!db) return;
  await new Promise<void>((resolve) => {
    const tx = db.transaction('files', 'readwrite').objectStore('files').put(data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength), sha);
    tx.onsuccess = () => resolve();
    tx.onerror = () => resolve();
  });
}

// What this device keeps (game files downloaded to play or watch), and a way
// to remove it all; files are fetched again, privately, when next needed.
export async function fileCacheStats(): Promise<{ files: number; bytes: number }> {
  const db = await idb();
  if (!db) return { files: 0, bytes: 0 };
  return new Promise((resolve) => {
    let files = 0, bytes = 0;
    const req = db.transaction('files', 'readonly').objectStore('files').openCursor();
    req.onsuccess = () => {
      const c = req.result;
      if (!c) return resolve({ files, bytes });
      files++;
      bytes += (c.value as ArrayBuffer).byteLength ?? 0;
      c.continue();
    };
    req.onerror = () => resolve({ files, bytes });
  });
}

export async function clearFileCache(): Promise<void> {
  const db = await idb();
  if (!db) return;
  await new Promise<void>((resolve) => {
    const tx = db.transaction('files', 'readwrite').objectStore('files').clear();
    tx.onsuccess = () => resolve();
    tx.onerror = () => resolve();
  });
}

async function sha256(data: Uint8Array): Promise<string> {
  const d = await crypto.subtle.digest('SHA-256', data as BufferSource);
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

export async function fetchFile(sha: string, onProgress?: (loaded: number) => void): Promise<Uint8Array> {
  const cached = await cacheGet(sha);
  if (cached && (await sha256(cached)) === sha) return cached;
  const res = await fetch(`/api/files/${sha}`, { headers: { Authorization: `Bearer ${token}` } });
  if (!res.ok) throw new Error(`Could not download a game file (${res.status}).`);
  const reader = res.body?.getReader();
  let data: Uint8Array;
  if (reader) {
    const parts: Uint8Array[] = [];
    let n = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      parts.push(value);
      n += value.length;
      onProgress?.(n);
    }
    data = new Uint8Array(n);
    let o = 0;
    for (const p of parts) {
      data.set(p, o);
      o += p.length;
    }
  } else {
    data = new Uint8Array(await res.arrayBuffer());
  }
  if ((await sha256(data)) !== sha) throw new Error('A game file was corrupted in transit.');
  await cachePut(sha, data);
  return data;
}
