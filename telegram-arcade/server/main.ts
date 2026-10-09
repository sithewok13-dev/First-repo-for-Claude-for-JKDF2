// Production entry point: node server/main.ts (see .env.example).

import { createApp, createParts, type App, type Services } from './app.ts';
import { loadConfig } from './config.ts';
import { log } from './log.ts';
import { createShelf } from './shelf/shelf.ts';
import { handleWebUpload } from './shelf/upload-http.ts';
import { TelegramApi } from './telegram/api.ts';
import { createBot } from './telegram/bot.ts';
import { DevMembership, TelegramMembership, type DevMember } from './telegram/membership.ts';
import type { MembershipService } from './telegram/types.ts';

const cfg = loadConfig();
log.setLevel(cfg.logLevel as any);
const parts = createParts(cfg);
const shelf = createShelf({ cfg, db: parts.db, groups: parts.groups });

let membership: MembershipService;
if (cfg.botToken) {
  const api = new TelegramApi({ token: cfg.botToken, baseUrl: cfg.botApiBase, local: cfg.botApiLocal, fileDir: cfg.botFileDir });
  membership = new TelegramMembership({
    db: parts.db,
    groups: parts.groups,
    ttlSec: cfg.membershipTtlSec,
    getChatMember: (chatId, userId) => api.call('getChatMember', { chat_id: chatId, user_id: userId }),
  });
} else {
  // DEV_MODE without Telegram: DEV_MEMBERS="userId[:role],..." members of every group.
  const allow: DevMember[] = (process.env.DEV_MEMBERS ?? '').split(',').filter(Boolean).map((s) => {
    const [id, role] = s.split(':');
    return { userId: Number(id), role: (role as any) || 'member' };
  });
  membership = new DevMembership(allow);
}

let app: App | null = null;
const services: Services = { shelf, membership, bot: null, webUpload: handleWebUpload };
if (cfg.botToken && cfg.updatesMode !== 'off') {
  services.bot = createBot({
    cfg,
    db: parts.db,
    groups: parts.groups,
    sessions: parts.sessions,
    shelf,
    membership,
    hooks: {
      onGroupDisabled: (groupId) => void app?.rooms.disableGroup(groupId, 'The bot was removed from the group.'),
      onMembershipRevoked: (groupId, userId) => app?.rooms.revokeUser(groupId, userId),
      onMemberChanged: (groupId, userId) => {
        membership.check(groupId, userId, { force: true }).then((info) => {
          if (!info.isMember) app?.rooms.revokeUser(groupId, userId);
          else app?.rooms.updateMemberRole(groupId, userId, info.tgRole);
        }, (e) => log.warn('main', `role refresh failed: ${(e as Error).message}`));
      },
      claimHost: (groupId, userId) => app ? app.rooms.claimHost(groupId, userId, true) : { ok: false, message: 'Starting up, try again.' },
      roomSummary: (groupId) => app?.rooms.summary(groupId) ?? null,
    },
  });
}
app = createApp(cfg, parts, services);

// In dev mode, a group can be created for local testing: DEV_GROUP="title"
if (cfg.devMode && process.env.DEV_GROUP) {
  const g = parts.groups.ensure(-1000000000001, process.env.DEV_GROUP);
  log.info('main', `dev group "${g.title}" room token ${g.roomToken}`);
  for (const s of (process.env.DEV_MEMBERS ?? '').split(',').filter(Boolean)) {
    const id = s.split(':')[0];
    log.info('main', `  open as user ${id}: ${cfg.publicUrl}/?dev=1&user=${id}&name=Player${id}&room=${g.roomToken}`);
  }
}

const port = await app.listen();
log.info('main', `listening on ${cfg.host}:${port} (${cfg.devMode ? 'DEV MODE' : 'production'}), public URL ${cfg.publicUrl}`);
await services.bot?.start();

let stopping = false;
async function shutdown(signal: string): Promise<void> {
  if (stopping) return;
  stopping = true;
  log.info('main', `${signal}: checkpointing rooms and shutting down`);
  const force = setTimeout(() => process.exit(1), 20_000);
  try {
    await app!.close();
  } finally {
    clearTimeout(force);
    process.exit(0);
  }
}
process.on('SIGTERM', () => void shutdown('SIGTERM'));
process.on('SIGINT', () => void shutdown('SIGINT'));
process.on('unhandledRejection', (e) => log.error('main', `unhandled rejection: ${(e as Error)?.message ?? e}`));
