// Minimal Telegram Bot API client over fetch.
//
// - Every method is a JSON POST to <base>/bot<token>/<method> with a timeout.
// - Failures throw TelegramError with Telegram's error_code, description and
//   ResponseParameters (retry_after, migrate_to_chat_id). Network failures and
//   timeouts use error code 0.
// - 429 Too Many Requests is retried after `retry_after`, a bounded number of
//   times (Telegram guarantees a 429'd request was not executed).
// - Files: getFile, then a download into a local file with a hard size limit.
//   Cloud Bot API: HTTP GET <base>/file/bot<token>/<file_path>; Telegram only
//   returns a file_path for files up to 20 MB. A self-hosted telegram-bot-api
//   in --local mode has no limit, returns an ABSOLUTE path on its own disk and
//   serves no /file/ downloads, so the file is copied from the shared volume
//   named by BOT_FILE_DIR, refusing anything outside it. There getFile only
//   answers once the whole file is on the server's disk, so it gets the long
//   download timeout, and our copy's source is removed afterwards (best
//   effort) so ROMs do not pile up twice.
//
// The token is part of every URL, and of local file paths (the local server
// keeps files under <dir>/<token>/), so neither ever goes into logs or errors.

import { constants } from 'node:fs';
import { copyFile, open, realpath, rm, stat, type FileHandle } from 'node:fs/promises';
import { isAbsolute, join, relative, resolve, sep } from 'node:path';
import { log } from '../log.ts';

// getFile on the cloud Bot API only works up to this size (20 MiB).
export const CLOUD_DOWNLOAD_LIMIT = 20 * 1024 * 1024;

// ------------------------------------------------------------ Bot API shapes
// Only the fields this project reads. Everything Telegram sends is checked
// before use; these types describe the expected shape, not a guarantee.

export interface TgUser {
  id: number;
  is_bot?: boolean;
  first_name?: string;
  last_name?: string;
  username?: string;
  can_join_groups?: boolean;
  can_read_all_group_messages?: boolean;
}

export interface TgChat {
  id: number;
  type: string;                  // private | group | supergroup | channel
  title?: string;
}

export interface TgDocument {
  file_id: string;
  file_unique_id: string;
  file_name?: string;
  mime_type?: string;
  file_size?: number;
}

export interface TgEntity {
  type: string;
  offset: number;
  length: number;
}

export interface TgMessage {
  message_id: number;
  chat: TgChat;
  from?: TgUser;
  sender_chat?: TgChat;          // set when posted anonymously as the group or a channel
  date?: number;
  text?: string;
  entities?: TgEntity[];
  document?: TgDocument;
  migrate_to_chat_id?: number;
  migrate_from_chat_id?: number;
  new_chat_title?: string;
  reply_to_message?: TgMessage;  // delivered in full with a command that replies to it
}

export interface TgChatMember {
  status: string;                // creator | administrator | member | restricted | left | kicked
  user?: TgUser;
  is_member?: boolean;           // restricted only
  [field: string]: unknown;      // administrator rights (can_*), until_date, ...
}

export interface TgChatMemberUpdated {
  chat: TgChat;
  from?: TgUser;
  date?: number;
  old_chat_member?: TgChatMember;
  new_chat_member?: TgChatMember;
}

export interface TgCallbackQuery {
  id: string;
  from?: TgUser;
}

export interface TgUpdate {
  update_id: number;
  message?: TgMessage;
  my_chat_member?: TgChatMemberUpdated;
  chat_member?: TgChatMemberUpdated;
  callback_query?: TgCallbackQuery;
}

export interface TgFile {
  file_id: string;
  file_unique_id: string;
  file_size?: number;
  file_path?: string;
}

// ------------------------------------------------------------------- errors

export class TelegramError extends Error {
  readonly method: string;
  readonly code: number;                 // Telegram error_code; 0 = network error or timeout
  readonly description: string;
  readonly retryAfter: number | null;    // seconds (429)
  readonly migrateToChatId: number | null;

  constructor(method: string, code: number, description: string, parameters?: { retry_after?: unknown; migrate_to_chat_id?: unknown }) {
    super(`${method} failed: ${code ? `${code} ` : ''}${description}`);
    this.name = 'TelegramError';
    this.method = method;
    this.code = code;
    this.description = description;
    const ra = Number(parameters?.retry_after);
    this.retryAfter = Number.isFinite(ra) && ra >= 0 ? ra : null;
    const mig = Number(parameters?.migrate_to_chat_id);
    this.migrateToChatId = Number.isSafeInteger(mig) && mig !== 0 ? mig : null;
  }
}

export type DownloadFailure = 'too_big' | 'unavailable' | 'outside_dir';

export class DownloadError extends Error {
  readonly reason: DownloadFailure;
  constructor(reason: DownloadFailure, message: string) {
    super(message);
    this.name = 'DownloadError';
    this.reason = reason;
  }
}

// True when the file cannot be fetched because of its size, whether we
// refused it or the Bot API did ("Bad Request: file is too big").
export function isFileTooBig(e: unknown): boolean {
  if (e instanceof DownloadError) return e.reason === 'too_big';
  return e instanceof TelegramError && /file is too big/i.test(e.description);
}

// Short, token-free description of any error for logs.
export function errText(e: unknown): string {
  if (e instanceof TelegramError) return `${e.method} ${e.code} ${e.description}`.slice(0, 200);
  if (e instanceof Error) return `${e.name}: ${e.message}`.slice(0, 200);
  return String(e).slice(0, 200);
}

// The errno code of a file system error (never its message, which holds paths).
function fsCode(e: unknown): string {
  const code = (e as NodeJS.ErrnoException | null)?.code;
  return typeof code === 'string' && /^[A-Z0-9_]{1,32}$/.test(code) ? code : 'error';
}

function networkError(e: any, timedOut: boolean): string {
  if (timedOut) return 'timeout';
  if (e?.name === 'AbortError' || e?.name === 'TimeoutError') return 'aborted';
  // Never e.message: a custom fetch could include the URL (and the token).
  const code = e?.cause?.code ?? e?.code;
  return `network error${typeof code === 'string' ? ` (${code})` : ''}`;
}

// ------------------------------------------------------------------- client

export interface TelegramApiOptions {
  token: string;
  baseUrl?: string;              // default https://api.telegram.org
  local?: boolean;               // self-hosted telegram-bot-api in --local mode
  // Where a local Bot API server's files are visible to us. Either the
  // server's working directory when it is mounted at the same path here, or
  // "<path on the Bot API server>=<path here>" when it is mounted elsewhere.
  fileDir?: string;
  fetch?: typeof fetch;
  timeoutMs?: number;            // per call (default 15 s)
  downloadTimeoutMs?: number;    // per file download, including a local getFile (default 10 min)
  maxRetries?: number;           // 429 retries per call (default 2)
  maxRetryAfterSec?: number;     // a longer retry_after fails fast instead (default 60)
  sleep?: (ms: number) => Promise<void>;
}

export interface CallOptions {
  timeoutMs?: number;
  retries?: number;              // 429 retries for this call
  signal?: AbortSignal;
}

function parseFileDir(spec: string): { server: string; local: string } | null {
  if (!spec) return null;
  const eq = spec.indexOf('=');
  const server = resolve(eq >= 0 ? spec.slice(0, eq) : spec);
  const local = resolve(eq >= 0 ? spec.slice(eq + 1) : spec);
  return { server, local };
}

// `child` is `root` itself or below it (both absolute, normalized).
function within(root: string, child: string): boolean {
  const rel = relative(root, child);
  return rel !== '' && rel !== '..' && !rel.startsWith('..' + sep) && !isAbsolute(rel);
}

export class TelegramApi {
  readonly local: boolean;
  private readonly token: string;
  private readonly base: string;
  private readonly fileDir: { server: string; local: string } | null;
  private readonly fetchFn: typeof fetch;
  private readonly timeoutMs: number;
  private readonly downloadTimeoutMs: number;
  private readonly maxRetries: number;
  private readonly maxRetryAfterSec: number;
  private readonly sleep: (ms: number) => Promise<void>;

  constructor(o: TelegramApiOptions) {
    if (!/^\d{1,20}:[A-Za-z0-9_-]{20,}$/.test(o.token)) throw new Error('BOT_TOKEN does not look like a bot token');
    this.token = o.token;
    this.base = (o.baseUrl ?? 'https://api.telegram.org').replace(/\/+$/, '');
    this.local = !!o.local;
    this.fileDir = parseFileDir(o.fileDir ?? '');
    this.fetchFn = o.fetch ?? fetch;
    this.timeoutMs = o.timeoutMs ?? 15_000;
    this.downloadTimeoutMs = o.downloadTimeoutMs ?? 10 * 60_000;
    this.maxRetries = o.maxRetries ?? 2;
    this.maxRetryAfterSec = o.maxRetryAfterSec ?? 60;
    this.sleep = o.sleep ?? ((ms) => new Promise((r) => setTimeout(r, ms)));
  }

  async call<T = any>(method: string, params: Record<string, unknown> = {}, opts: CallOptions = {}): Promise<T> {
    if (!/^[A-Za-z]{1,64}$/.test(method)) throw new Error('invalid Bot API method name');
    const retries = opts.retries ?? this.maxRetries;
    for (let attempt = 0; ; attempt++) {
      try {
        return await this.once<T>(method, params, opts);
      } catch (e) {
        if (!(e instanceof TelegramError) || e.code !== 429 || attempt >= retries || opts.signal?.aborted) throw e;
        const wait = Math.max(1, e.retryAfter ?? 1);
        if (wait > this.maxRetryAfterSec) throw e;
        log.warn('telegram', `${method} rate limited; retrying`, { retryAfterSec: wait, attempt: attempt + 1 });
        await this.sleep(wait * 1000);
      }
    }
  }

  private async once<T>(method: string, params: Record<string, unknown>, opts: CallOptions): Promise<T> {
    if (opts.signal?.aborted) throw new TelegramError(method, 0, 'aborted');   // never sent
    const timeout = AbortSignal.timeout(opts.timeoutMs ?? this.timeoutMs);
    const signal = opts.signal ? AbortSignal.any([opts.signal, timeout]) : timeout;
    let res: Response;
    let body: any;
    try {
      res = await this.fetchFn(`${this.base}/bot${this.token}/${method}`, {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify(params),
        signal,
      });
    } catch (e) {
      throw new TelegramError(method, 0, networkError(e, timeout.aborted));
    }
    try {
      body = await res.json();
    } catch (e) {
      if (signal.aborted) throw new TelegramError(method, 0, networkError(e, timeout.aborted));
      throw new TelegramError(method, res.status || 0, `unexpected response (HTTP ${res.status})`);
    }
    if (body && body.ok === true) return body.result as T;
    const code = Number(body?.error_code) || res.status || 0;
    const description = typeof body?.description === 'string' ? body.description.slice(0, 300) : 'request failed';
    throw new TelegramError(method, code, description, body?.parameters);
  }

  getFile(fileId: string, opts: { signal?: AbortSignal } = {}): Promise<TgFile> {
    // A local server downloads the whole file before answering.
    return this.call<TgFile>('getFile', { file_id: fileId }, { timeoutMs: this.local ? this.downloadTimeoutMs : undefined, signal: opts.signal });
  }

  // Downloads a file the bot received into `dest` (created exclusively, mode
  // 0600). Never writes more than `maxBytes`; on any failure `dest` is gone.
  // `signal` aborts the transfer (the bot stops all downloads on shutdown).
  async download(fileId: string, dest: string, maxBytes: number, opts: { signal?: AbortSignal } = {}): Promise<{ size: number }> {
    const f = await this.getFile(fileId, opts);
    if (typeof f?.file_size === 'number' && f.file_size > maxBytes) throw new DownloadError('too_big', 'file exceeds the upload limit');
    if (typeof f?.file_path !== 'string' || !f.file_path) {
      // The cloud Bot API omits file_path for files over 20 MB.
      throw new DownloadError(this.local ? 'unavailable' : 'too_big', 'Bot API returned no file path');
    }
    if (opts.signal?.aborted) throw new TelegramError('download', 0, 'aborted');
    return this.local ? this.copyLocal(f.file_path, dest, maxBytes) : this.fetchFile(f.file_path, dest, maxBytes, opts.signal);
  }

  private async fetchFile(filePath: string, dest: string, maxBytes: number, stop?: AbortSignal): Promise<{ size: number }> {
    // Cloud paths look like "documents/file_12.zip": relative, no traversal.
    const parts = filePath.split('/');
    if (filePath.startsWith('/') || parts.some((p) => p === '' || p === '.' || p === '..')) throw new DownloadError('unavailable', 'unexpected file path from the Bot API');
    const url = `${this.base}/file/bot${this.token}/${parts.map(encodeURIComponent).join('/')}`;
    const timeout = AbortSignal.timeout(this.downloadTimeoutMs);
    const signal = stop ? AbortSignal.any([stop, timeout]) : timeout;
    let res: Response;
    try {
      res = await this.fetchFn(url, { signal });
    } catch (e) {
      throw new TelegramError('download', 0, networkError(e, timeout.aborted));
    }
    if (!res.ok || !res.body) {
      await res.body?.cancel().catch(() => {});
      throw new DownloadError('unavailable', `file download failed (HTTP ${res.status})`);
    }
    const declared = Number(res.headers.get('content-length'));
    if (Number.isFinite(declared) && declared > maxBytes) {
      await res.body.cancel().catch(() => {});
      throw new DownloadError('too_big', 'file exceeds the upload limit');
    }
    const reader = res.body.getReader();
    let fh: FileHandle;
    try {
      fh = await open(dest, 'wx', 0o600);
    } catch (e) {
      // Release the connection; the fs error names only our own temp path.
      await reader.cancel().catch(() => {});
      throw new DownloadError('unavailable', `cannot create the download file (${fsCode(e)})`);
    }
    let size = 0;
    try {
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        size += value.byteLength;
        if (size > maxBytes) throw new DownloadError('too_big', 'file exceeds the upload limit');
        await fh.write(value);
      }
    } catch (e) {
      await reader.cancel().catch(() => {});
      await fh.close().catch(() => {});
      await rm(dest, { force: true });
      if (e instanceof DownloadError) throw e;
      throw new TelegramError('download', 0, networkError(e, timeout.aborted));
    }
    await fh.close();
    return { size };
  }

  private async copyLocal(filePath: string, dest: string, maxBytes: number): Promise<{ size: number }> {
    if (!this.fileDir) throw new DownloadError('unavailable', 'BOT_FILE_DIR is not configured for the local Bot API server');
    if (!isAbsolute(filePath)) throw new DownloadError('outside_dir', 'local Bot API returned a relative file path');
    const onServer = resolve(filePath);
    if (!within(this.fileDir.server, onServer)) throw new DownloadError('outside_dir', 'file path is outside BOT_FILE_DIR');
    const mapped = join(this.fileDir.local, relative(this.fileDir.server, onServer));
    // Symlinks must not lead out of the shared directory either.
    let src: string;
    try {
      const root = await realpath(this.fileDir.local);
      src = await realpath(mapped);
      if (!within(root, src)) throw new DownloadError('outside_dir', 'file path resolves outside BOT_FILE_DIR');
    } catch (e) {
      if (e instanceof DownloadError) throw e;
      throw new DownloadError('unavailable', 'file is not visible in BOT_FILE_DIR');
    }
    // Raw fs errors are never passed on: their messages carry the source
    // path, which on a local Bot API server contains the bot token.
    let copied: { size: number };
    try {
      const st = await stat(src);
      if (!st.isFile()) throw new DownloadError('unavailable', 'not a regular file');
      if (st.size > maxBytes) throw new DownloadError('too_big', 'file exceeds the upload limit');
      await copyFile(src, dest, constants.COPYFILE_EXCL);
      copied = await stat(dest);
    } catch (e) {
      // COPYFILE_EXCL: an existing dest is not ours to remove.
      if (fsCode(e) !== 'EEXIST') await rm(dest, { force: true }).catch(() => {});
      if (e instanceof DownloadError) throw e;
      throw new DownloadError('unavailable', `cannot copy the file from BOT_FILE_DIR (${fsCode(e)})`);
    }
    if (copied.size > maxBytes) {
      await rm(dest, { force: true });
      throw new DownloadError('too_big', 'file exceeds the upload limit');
    }
    // We keep our own copy; the Bot API server fetches the file again if it
    // is ever asked for it. A read-only mount simply keeps the file.
    await rm(src).catch((e) => log.debug('telegram', 'could not remove the local Bot API copy', { code: (e as NodeJS.ErrnoException).code }));
    return { size: copied.size };
  }
}
