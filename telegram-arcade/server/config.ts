// Configuration from environment variables (see .env.example). Every limit
// the pilot depends on is configurable here with a conservative default.

import { join, resolve } from 'node:path';

function str(name: string, def?: string): string {
  const v = process.env[name];
  if (v === undefined || v === '') {
    if (def === undefined) throw new Error(`missing required environment variable ${name}`);
    return def;
  }
  return v;
}

function int(name: string, def: number, min = -Infinity, max = Infinity): number {
  const raw = process.env[name];
  if (raw === undefined || raw === '') return def;
  const n = Number(raw);
  if (!Number.isFinite(n) || n < min || n > max) throw new Error(`${name} must be a number in [${min}, ${max}]`);
  return Math.floor(n);
}

function bool(name: string, def: boolean): boolean {
  const raw = process.env[name];
  if (raw === undefined || raw === '') return def;
  return raw === '1' || raw.toLowerCase() === 'true';
}

export type Config = ReturnType<typeof loadConfig>;

export function loadConfig() {
  const root = resolve(new URL('../', import.meta.url).pathname);
  const dataDir = resolve(str('DATA_DIR', join(root, 'data')));
  const devMode = bool('DEV_MODE', false);
  const cfg = {
    root,
    devMode,
    // HTTP
    host: str('HOST', '0.0.0.0'),
    port: int('PORT', 8080, 0, 65535),           // 0 = any free port (tests)
    publicUrl: str('PUBLIC_URL', devMode ? 'http://localhost:8080' : undefined).replace(/\/$/, ''),
    trustProxy: bool('TRUST_PROXY', false),           // set when behind a reverse proxy (deploy/ sets it)
    // Telegram
    botToken: process.env.BOT_TOKEN ?? '',
    botUsername: process.env.BOT_USERNAME ?? '',
    miniAppShortName: str('MINIAPP_SHORT_NAME', 'arcade'),
    botApiBase: str('BOT_API_BASE', 'https://api.telegram.org'),
    botApiLocal: bool('BOT_API_LOCAL', false),          // self-hosted telegram-bot-api in --local mode
    botFileDir: process.env.BOT_FILE_DIR ?? '',          // shared volume with a local Bot API server
    updatesMode: str('UPDATES_MODE', 'polling') as 'polling' | 'webhook' | 'off',
    webhookSecret: process.env.WEBHOOK_SECRET ?? '',
    allowedChatIds: (process.env.ALLOWED_CHAT_IDS ?? '').split(',').map((s) => s.trim()).filter(Boolean).map(Number),
    initDataMaxAgeSec: int('INITDATA_MAX_AGE_SEC', 3600, 60, 86400),
    sessionTtlSec: int('SESSION_TTL_SEC', 12 * 3600, 300, 7 * 86400),
    membershipTtlSec: int('MEMBERSHIP_TTL_SEC', 300, 10, 86400),
    announceMinIntervalSec: int('ANNOUNCE_MIN_INTERVAL_SEC', 60, 5, 3600),
    // Storage
    dataDir,
    dbPath: join(dataDir, 'arcade.sqlite'),
    blobDir: join(dataDir, 'blobs'),
    checkpointDir: join(dataDir, 'checkpoints'),
    coreDir: resolve(str('CORE_DIR', join(root, 'native/build'))),
    clientDir: resolve(str('CLIENT_DIR', join(root, 'client/dist'))),
    maxUploadBytes: int('MAX_UPLOAD_BYTES', 64 * 1024 * 1024, 1024, 2 * 1024 * 1024 * 1024),
    maxExtractedBytes: int('MAX_EXTRACTED_BYTES', 256 * 1024 * 1024, 1024, 4 * 1024 * 1024 * 1024),
    maxArchiveEntries: int('MAX_ARCHIVE_ENTRIES', 512, 1, 100000),
    maxCompressionRatio: int('MAX_COMPRESSION_RATIO', 200, 2, 100000),
    groupQuotaBytes: int('GROUP_QUOTA_BYTES', 2 * 1024 * 1024 * 1024, 1024),
    globalStorageLimitBytes: int('GLOBAL_STORAGE_LIMIT_BYTES', 40 * 1024 * 1024 * 1024, 1024),
    validationTimeoutMs: int('VALIDATION_TIMEOUT_MS', 60_000, 1000, 600_000),
    maxConcurrentValidations: int('MAX_CONCURRENT_VALIDATIONS', 1, 1, 16),
    removedGameRetentionDays: int('REMOVED_GAME_RETENTION_DAYS', 7, 0, 3650),
    // Rooms and emulation
    maxActiveRooms: int('MAX_ACTIVE_ROOMS', 3, 1, 100),
    maxViewersPerRoom: int('MAX_VIEWERS_PER_ROOM', 16, 2, 500),
    roomIdleReleaseSec: int('ROOM_IDLE_RELEASE_SEC', 300, 10, 86400),
    checkpointIntervalSec: int('CHECKPOINT_INTERVAL_SEC', 30, 2, 3600),
    workerHeapMb: int('WORKER_HEAP_MB', 192, 64, 4096),
    replayLogSeconds: int('REPLAY_LOG_SECONDS', 120, 30, 600),
    hashEveryFrames: int('HASH_EVERY_FRAMES', 120, 10, 3600),
    // Room rules (defaults; each group can override in its settings)
    offerSeconds: int('SEAT_OFFER_SECONDS', 15, 3, 120),
    disconnectGraceSeconds: int('DISCONNECT_GRACE_SECONDS', 30, 1, 600),
    hostGraceSeconds: int('HOST_GRACE_SECONDS', 60, 1, 3600),
    voteSeconds: int('VOTE_SECONDS', 60, 15, 600),
    chatHistory: int('CHAT_HISTORY', 200, 10, 5000),
    chatRetentionDays: int('CHAT_RETENTION_DAYS', 14, 1, 365),
    // Logging
    logLevel: str('LOG_LEVEL', 'info'),
  };
  if (!devMode && !cfg.botToken) throw new Error('BOT_TOKEN is required unless DEV_MODE=1');
  if (cfg.updatesMode === 'webhook' && !/^[A-Za-z0-9_-]{16,256}$/.test(cfg.webhookSecret)) throw new Error('WEBHOOK_SECRET (16-256 characters from A-Z a-z 0-9 _ -) is required for webhook mode');
  if (!['polling', 'webhook', 'off'].includes(cfg.updatesMode)) throw new Error('UPDATES_MODE must be polling, webhook or off');
  if (!devMode && !cfg.publicUrl.startsWith('https://')) throw new Error('PUBLIC_URL must be an https:// origin (Telegram Mini Apps require HTTPS)');
  return cfg;
}
