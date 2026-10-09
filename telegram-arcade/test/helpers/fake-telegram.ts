// An in-process fake of the Telegram Bot API, used as the `fetch` of the bot
// and membership code under test. It implements the methods this project
// calls, records every call, serves files, and can be told to fail. Also a
// manual clock for timer-driven code.

import { loadConfig, type Config } from '../../server/config.ts';
import { Db } from '../../server/db/db.ts';
import type { Clock } from '../../server/telegram/outbox.ts';

export const FAKE_TOKEN = '123456789:TEST-ONLY-not-a-real-bot-token-unit';
export const FAKE_API_BASE = 'https://bot-api.invalid';

export interface FakeCall {
  method: string;
  params: any;
}

interface FakeFailure {
  error_code: number;
  description: string;
  parameters?: Record<string, unknown>;
}

interface FakeFile {
  fileId: string;
  fileUniqueId: string;
  path: string;          // relative (cloud) or absolute (local mode)
  data: Uint8Array;
  size?: number;         // reported size, defaults to data length
  // How /file/ serves it: 'whole' (default) with a Content-Length header,
  // 'chunked' streamed in 1 KiB chunks without one, or 'hang' (first chunk,
  // then nothing until the request is aborted, like a stalled transfer).
  serve?: 'whole' | 'chunked' | 'hang';
}

// A configuration for unit tests: dev defaults, the fake bot token and API
// base, then the given overrides.
export function testConfig(over: Partial<Config> = {}): Config {
  const keys = ['DEV_MODE', 'BOT_TOKEN', 'BOT_USERNAME', 'UPDATES_MODE', 'WEBHOOK_SECRET', 'ALLOWED_CHAT_IDS', 'BOT_API_LOCAL', 'BOT_FILE_DIR', 'PUBLIC_URL'];
  const saved = keys.map((k) => [k, process.env[k]] as const);
  try {
    for (const k of keys) delete process.env[k];
    process.env.DEV_MODE = '1';
    const base = loadConfig();
    return { ...base, botToken: FAKE_TOKEN, botApiBase: FAKE_API_BASE, publicUrl: 'https://arcade.example', updatesMode: 'off', ...over };
  } finally {
    for (const [k, v] of saved) {
      if (v === undefined) delete process.env[k];
      else process.env[k] = v;
    }
  }
}

// A fresh in-memory database with every migration applied.
export function testDb(): Db {
  const db = new Db(':memory:');
  db.migrate();
  return db;
}

function json(status: number, body: unknown): Response {
  return new Response(JSON.stringify(body), { status, headers: { 'content-type': 'application/json' } });
}

export class FakeTelegram {
  readonly token = FAKE_TOKEN;
  readonly base = FAKE_API_BASE;
  me = { id: 7_000_000_001, is_bot: true, first_name: 'Arcade', username: 'arcade_test_bot', can_join_groups: true, can_read_all_group_messages: false };
  readonly calls: FakeCall[] = [];
  readonly members = new Map<string, any>();      // `${chatId}:${userId}` -> ChatMember
  readonly files = new Map<string, FakeFile>();   // file_id -> file
  fileBodiesCancelled = 0;                        // /file/ responses the client cancelled or aborted
  readonly fetch: typeof fetch;
  private readonly failures = new Map<string, FakeFailure[]>();
  private readonly holds = new Map<string, Promise<void>>();
  private updates: any[] = [];
  private readonly messageIds = new Map<number, number>();
  private wake: (() => void) | null = null;

  constructor() {
    this.fetch = ((input: string | URL | Request, init?: RequestInit) => this.handle(input, init)) as typeof fetch;
  }

  setMember(chatId: number, userId: number, member: Record<string, unknown>): void {
    this.members.set(`${chatId}:${userId}`, { user: { id: userId, is_bot: false, first_name: `U${userId}` }, ...member });
  }

  addFile(f: FakeFile): void {
    this.files.set(f.fileId, f);
  }

  // The next `times` calls of `method` fail with this error.
  failNext(method: string, error_code: number, description: string, parameters?: Record<string, unknown>, times = 1): void {
    const list = this.failures.get(method) ?? [];
    for (let i = 0; i < times; i++) list.push({ error_code, description, parameters });
    this.failures.set(method, list);
  }

  // Calls of `method` wait (after being recorded) until the returned
  // function is called.
  hold(method: string): () => void {
    let release!: () => void;
    this.holds.set(method, new Promise<void>((r) => { release = r; }));
    return () => {
      this.holds.delete(method);
      release();
    };
  }

  pushUpdate(...u: any[]): void {
    this.updates.push(...u);
    this.wake?.();
  }

  callsOf(method: string): FakeCall[] {
    return this.calls.filter((c) => c.method === method);
  }

  // sendMessage / editMessageText calls to a chat.
  messagesTo(chatId: number): FakeCall[] {
    return this.calls.filter((c) => (c.method === 'sendMessage' || c.method === 'editMessageText') && c.params.chat_id === chatId);
  }

  // Calls that put a web_app button into a group or supergroup (negative chat id).
  groupWebAppViolations(): FakeCall[] {
    return this.calls.filter((c) => typeof c.params?.chat_id === 'number' && c.params.chat_id < 0 && JSON.stringify(c.params.reply_markup ?? {}).includes('"web_app"'));
  }

  private async handle(input: string | URL | Request, init?: RequestInit): Promise<Response> {
    const url = new URL(typeof input === 'string' ? input : input instanceof URL ? input.href : input.url);
    const file = url.pathname.match(/^\/file\/bot([^/]+)\/(.+)$/);
    if (file) {
      if (file[1] !== this.token) return new Response('unauthorized', { status: 401 });
      const path = decodeURIComponent(file[2]);
      const f = [...this.files.values()].find((x) => x.path === path);
      if (!f) return new Response('not found', { status: 404 });
      if (!f.serve || f.serve === 'whole') return new Response(f.data.slice(), { status: 200, headers: { 'content-length': String(f.data.byteLength) } });
      return new Response(this.fileStream(f, init?.signal ?? undefined), { status: 200 });
    }
    const m = url.pathname.match(/^\/bot([^/]+)\/([A-Za-z]+)$/);
    if (!m || m[1] !== this.token) return json(401, { ok: false, error_code: 401, description: 'Unauthorized' });
    const method = m[2];
    const params = init?.body ? JSON.parse(String(init.body)) : {};
    this.calls.push({ method, params });
    const fail = this.failures.get(method)?.shift();
    if (fail) return json(fail.error_code, { ok: false, ...fail });
    await this.holds.get(method);
    const r = await this.dispatch(method, params, init?.signal ?? undefined);
    return 'error' in r ? json(r.error.error_code, { ok: false, ...r.error }) : json(200, { ok: true, result: r.result });
  }

  // A streamed file body that, like a real fetch, errors when the request's
  // signal aborts.
  private fileStream(f: FakeFile, signal?: AbortSignal): ReadableStream<Uint8Array> {
    let offset = 0;
    let ctl: ReadableStreamDefaultController<Uint8Array>;
    const onAbort = () => {
      this.fileBodiesCancelled++;
      try {
        ctl.error(new DOMException('aborted', 'AbortError'));
      } catch { /* already closed */ }
    };
    return new ReadableStream<Uint8Array>({
      start: (c) => {
        ctl = c;
        signal?.addEventListener('abort', onAbort, { once: true });
      },
      pull: (c) => {
        if (f.serve === 'hang' && offset > 0) return new Promise<void>(() => {});   // stalls until aborted
        if (offset >= f.data.byteLength) {
          signal?.removeEventListener('abort', onAbort);
          c.close();
          return;
        }
        c.enqueue(f.data.slice(offset, offset + 1024));
        offset += 1024;
      },
      cancel: () => {
        signal?.removeEventListener('abort', onAbort);
        this.fileBodiesCancelled++;
      },
    });
  }

  private nextMessageId(chatId: number): number {
    const n = (this.messageIds.get(chatId) ?? 100) + 1;
    this.messageIds.set(chatId, n);
    return n;
  }

  private async dispatch(method: string, p: any, signal?: AbortSignal): Promise<{ result: unknown } | { error: FakeFailure }> {
    switch (method) {
      case 'getMe':
        return { result: this.me };
      case 'getUpdates':
        return { result: await this.getUpdates(p, signal) };
      case 'sendMessage':
        // Like Telegram (which counts after entity parsing; the raw text is an upper bound).
        if (String(p.text ?? '').length > 4096) return { error: { error_code: 400, description: 'Bad Request: message is too long' } };
        return { result: { message_id: this.nextMessageId(p.chat_id), chat: { id: p.chat_id }, date: 0, text: p.text } };
      case 'editMessageText':
        if (String(p.text ?? '').length > 4096) return { error: { error_code: 400, description: 'Bad Request: message is too long' } };
        return { result: { message_id: p.message_id, chat: { id: p.chat_id }, date: 0, text: p.text } };
      case 'getChatMember': {
        const member = this.members.get(`${p.chat_id}:${p.user_id}`);
        return member ? { result: member } : { error: { error_code: 400, description: 'Bad Request: user not found' } };
      }
      case 'getFile': {
        const f = this.files.get(p.file_id);
        if (!f) return { error: { error_code: 400, description: 'Bad Request: invalid file_id' } };
        const size = f.size ?? f.data.byteLength;
        return { result: { file_id: f.fileId, file_unique_id: f.fileUniqueId, file_size: size, file_path: f.path } };
      }
      case 'deleteWebhook':
      case 'setWebhook':
      case 'setMyCommands':
      case 'setMyDefaultAdministratorRights':
      case 'leaveChat':
      case 'answerCallbackQuery':
      case 'deleteMessage':
        return { result: true };
      default:
        return { error: { error_code: 404, description: 'Not Found: method not found' } };
    }
  }

  // Long poll: returns pending updates at or after `offset` (earlier ones are
  // confirmed and forgotten), or waits up to `timeout` seconds (capped at 2 s).
  private async getUpdates(p: any, signal?: AbortSignal): Promise<any[]> {
    if (typeof p.offset === 'number') this.updates = this.updates.filter((u) => u.update_id >= p.offset);
    if (!this.updates.length && p.timeout > 0) {
      await new Promise<void>((resolve, reject) => {
        const t = setTimeout(done, Math.min(p.timeout, 2) * 1000);
        function done() {
          clearTimeout(t);
          resolve();
        }
        this.wake = done;
        signal?.addEventListener('abort', () => {
          clearTimeout(t);
          reject(signal.reason ?? new DOMException('aborted', 'AbortError'));
        }, { once: true });
      });
      this.wake = null;
    }
    return this.updates.slice(0, p.limit ?? 100);
  }
}

// A clock that only moves when told to. Timers fire in time order.
export class FakeClock implements Clock {
  private t: number;
  private seq = 0;
  private timers: { id: number; at: number; fn: () => void }[] = [];

  constructor(start = 1_800_000_000_000) {
    this.t = start;
  }

  now(): number {
    return this.t;
  }

  setTimeout(fn: () => void, ms: number): () => void {
    const id = ++this.seq;
    this.timers.push({ id, at: this.t + Math.max(0, ms), fn });
    return () => {
      this.timers = this.timers.filter((x) => x.id !== id);
    };
  }

  advance(ms: number): void {
    const end = this.t + ms;
    for (;;) {
      const due = this.timers.filter((x) => x.at <= end).sort((a, b) => a.at - b.at || a.id - b.id)[0];
      if (!due) break;
      this.timers = this.timers.filter((x) => x.id !== due.id);
      this.t = Math.max(this.t, due.at);
      due.fn();
    }
    this.t = end;
  }

  pending(): number {
    return this.timers.length;
  }
}

// Lets promise chains driven by the fake fetch run to completion.
export async function settle(turns = 20): Promise<void> {
  for (let i = 0; i < turns; i++) await new Promise((r) => setImmediate(r));
}
