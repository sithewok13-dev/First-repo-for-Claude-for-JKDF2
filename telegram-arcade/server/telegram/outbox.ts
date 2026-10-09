// Per-group send queue. Every bot message to a group goes through here, so:
// - at most `perMinute` messages per group per rolling minute (Telegram allows
//   about 20 in groups; we stay well under) and one per `minGapMs`;
// - a 429 pauses the whole group for retry_after, then the message is retried;
// - queued messages that share a key are coalesced (the latest wins);
// - a send that reports the group moved to a supergroup follows the move;
// - nothing is sent to a group that is no longer active;
// - inline `web_app` buttons can never reach a group (Telegram rejects them
//   outside private chats; groups launch the Mini App through url buttons).

import type { Groups } from '../groups.ts';
import { log } from '../log.ts';
import { TelegramError, errText, type TelegramApi } from './api.ts';

export interface Clock {
  now(): number;
  setTimeout(fn: () => void, ms: number): () => void;   // returns a cancel function
}

export const systemClock: Clock = {
  now: () => Date.now(),
  setTimeout(fn, ms) {
    const t = setTimeout(fn, ms);
    t.unref?.();
    return () => clearTimeout(t);
  },
};

export type SendOutcome = { ok: true; result: any } | { ok: false; error: unknown };

export interface OutboxOptions {
  api: TelegramApi;
  groups: Groups;
  clock: Clock;
  onMigrated(oldChatId: number, newChatId: number): void;
  perMinute?: number;
  minGapMs?: number;
}

interface Job {
  key: string | null;
  method: string;
  params: Record<string, unknown>;
  attempts: number;
  waiters: ((o: SendOutcome) => void)[];
}

interface Lane {
  jobs: Job[];
  sent: number[];             // send times within the last minute
  pausedUntil: number;
  timer: (() => void) | null;
  busy: boolean;
}

const MAX_ATTEMPTS = 3;
const MAX_QUEUED_PER_GROUP = 30;
const PUMP_RETRY_MS = 5000;

// Throws if a reply markup carries a web_app button (a programming error).
export function assertNoWebAppButtons(markup: unknown): void {
  const m = markup as { inline_keyboard?: unknown; keyboard?: unknown } | null | undefined;
  for (const rows of [m?.inline_keyboard, m?.keyboard]) {
    if (!Array.isArray(rows)) continue;
    for (const row of rows) {
      if (Array.isArray(row) && row.some((b) => b && typeof b === 'object' && 'web_app' in b)) {
        throw new Error('web_app buttons only work in private chats; use a url button with a t.me direct link in groups');
      }
    }
  }
}

function finish(job: Job, outcome: SendOutcome): void {
  for (const w of job.waiters) w(outcome);
}

export class Outbox {
  private readonly o: OutboxOptions;
  private readonly perMinute: number;
  private readonly minGapMs: number;
  private readonly lanes = new Map<number, Lane>();
  private readonly inflight = new Set<Promise<unknown>>();
  private stopped = false;

  constructor(o: OutboxOptions) {
    this.o = o;
    this.perMinute = o.perMinute ?? 10;
    this.minGapMs = o.minGapMs ?? 1100;
  }

  // Resolves when the message was sent (or finally failed); never rejects.
  send(groupId: number, method: 'sendMessage' | 'editMessageText', params: Record<string, unknown>, opts: { key?: string } = {}): Promise<SendOutcome> {
    assertNoWebAppButtons(params.reply_markup);
    if (this.stopped) return Promise.resolve({ ok: false, error: new Error('bot stopped') });
    const lane = this.lane(groupId);
    return new Promise<SendOutcome>((resolve) => {
      const key = opts.key ?? null;
      const at = key === null ? -1 : lane.jobs.findIndex((j) => j.key === key);
      if (at >= 0) {
        // Latest wins; whoever waited for the replaced message gets this one's outcome.
        const old = lane.jobs[at];
        lane.jobs[at] = { key, method, params, attempts: 0, waiters: [...old.waiters, resolve] };
      } else if (lane.jobs.length >= MAX_QUEUED_PER_GROUP) {
        log.warn('bot', 'group send queue full; message dropped', { group: groupId });
        resolve({ ok: false, error: new Error('send queue full') });
        return;
      } else {
        lane.jobs.push({ key, method, params, attempts: 0, waiters: [resolve] });
      }
      this.safePump(groupId);
    });
  }

  // Drops everything queued for a group (bot removed, group disabled).
  drop(groupId: number): void {
    const lane = this.lanes.get(groupId);
    if (!lane) return;
    lane.timer?.();
    lane.timer = null;
    for (const job of lane.jobs.splice(0)) finish(job, { ok: false, error: new Error('group is not active') });
    if (!lane.busy) this.lanes.delete(groupId);
  }

  stop(): void {
    this.stopped = true;
    for (const id of [...this.lanes.keys()]) this.drop(id);
  }

  // Waits for sends already handed to Telegram (not for paced, queued ones).
  async whenIdle(): Promise<void> {
    while (this.inflight.size) await Promise.allSettled([...this.inflight]);
  }

  queued(groupId: number): number {
    return this.lanes.get(groupId)?.jobs.length ?? 0;
  }

  private lane(groupId: number): Lane {
    let lane = this.lanes.get(groupId);
    if (!lane) {
      lane = { jobs: [], sent: [], pausedUntil: 0, timer: null, busy: false };
      this.lanes.set(groupId, lane);
    }
    return lane;
  }

  private pump(groupId: number): void {
    const lane = this.lanes.get(groupId);
    if (!lane || lane.busy || this.stopped) return;
    lane.timer?.();
    lane.timer = null;
    const now = this.o.clock.now();
    lane.sent = lane.sent.filter((t) => now - t < 60_000);
    if (!lane.jobs.length) return;
    let wait = Math.max(0, lane.pausedUntil - now);
    if (lane.sent.length >= this.perMinute) wait = Math.max(wait, lane.sent[0] + 60_000 - now);
    const last = lane.sent[lane.sent.length - 1];
    if (last !== undefined) wait = Math.max(wait, last + this.minGapMs - now);
    if (wait > 0) {
      lane.timer = this.o.clock.setTimeout(() => {
        lane.timer = null;
        this.safePump(groupId);
      }, wait);
      return;
    }
    // Looked up before the job leaves the queue: if this throws, the job is
    // still queued and safePump() tries again.
    const group = this.o.groups.byId(groupId);
    const job = lane.jobs.shift()!;
    if (!group || group.status !== 'active') {
      finish(job, { ok: false, error: new Error('group is not active') });
      this.pump(groupId);
      return;
    }
    lane.busy = true;
    lane.sent.push(now);
    job.attempts++;
    const chatId = group.chatId;
    const p: Promise<void> = this.o.api
      .call(job.method, { ...job.params, chat_id: chatId }, { retries: 0 })
      .then(
        (result) => finish(job, { ok: true, result }),
        (e) => this.failed(groupId, lane, job, chatId, e),
      )
      .catch((e) => {
        // failed() itself threw (e.g. the migration it follows hit a
        // database error): the job is over, and nothing may go unhandled.
        log.error('bot', 'group send bookkeeping failed', { group: groupId, err: errText(e) });
        finish(job, { ok: false, error: e });
      })
      .finally(() => {
        lane.busy = false;
        this.inflight.delete(p);
        if (this.lanes.get(groupId) === lane) this.safePump(groupId);
      });
    this.inflight.add(p);
  }

  // pump() from a timer or a settled send, where an exception would be
  // uncaught (or an unhandled rejection) and end the process.
  private safePump(groupId: number): void {
    try {
      this.pump(groupId);
    } catch (e) {
      log.error('bot', 'group send queue failed; retrying shortly', { group: groupId, err: errText(e) });
      const lane = this.lanes.get(groupId);
      if (lane && !lane.timer && !this.stopped) {
        lane.timer = this.o.clock.setTimeout(() => {
          lane.timer = null;
          this.safePump(groupId);
        }, PUMP_RETRY_MS);
      }
    }
  }

  private failed(groupId: number, lane: Lane, job: Job, chatId: number, e: unknown): void {
    if (e instanceof TelegramError && !this.stopped) {
      if (e.code === 429 && job.attempts < MAX_ATTEMPTS) {
        const retryAfter = Math.max(1, e.retryAfter ?? 5);
        lane.pausedUntil = this.o.clock.now() + retryAfter * 1000;
        log.warn('bot', 'Telegram rate limit for a group; pausing its messages', { group: groupId, retryAfterSec: retryAfter });
        this.requeue(lane, job);
        return;
      }
      if (e.migrateToChatId && job.attempts < MAX_ATTEMPTS) {
        this.o.onMigrated(chatId, e.migrateToChatId);
        this.requeue(lane, job);
        return;
      }
      if (job.method === 'editMessageText' && /message is not modified/i.test(e.description)) {
        finish(job, { ok: true, result: true });
        return;
      }
    }
    log.warn('bot', `${job.method} to a group failed`, { group: groupId, err: errText(e) });
    finish(job, { ok: false, error: e });
  }

  private requeue(lane: Lane, job: Job): void {
    const newer = job.key === null ? undefined : lane.jobs.find((j) => j.key === job.key);
    if (newer) newer.waiters.unshift(...job.waiters);
    else lane.jobs.unshift(job);
  }
}
