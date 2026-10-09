// The room engine: server-authoritative seats, waiting queue, seat offers,
// per-mode rotation rules, manual result reporting, host/deputy authority and
// succession, next-game voting and text chat.
//
// All commands for a room run on one event loop and each method runs to
// completion, so concurrent requests are applied atomically in arrival order.
// Time and side effects go through RoomIO, which keeps this file testable.

import type { AdapterEvent, PlayerStatus, RoomMode } from '../adapters/types.ts';
import { Ballot, DEFAULT_RULES, KEEP, RoomError, type BallotOutcome } from './votes.ts';
import { DEFAULT_SETTINGS, type PublicSeat, type RoomGame, type RoomIO, type RoomSettings, type TgRole } from './types.ts';

export { RoomError };

export interface Member {
  userId: number;
  name: string;
  conns: Set<string>;
  lastConn: string | null;
  joinedAt: number;          // first connection in this room session
  presentSince: number | null;
  tgRole: TgRole;
}

interface Seat {
  port: number;
  userId: number | null;
  controlConn: string | null;
  epoch: number;
  since: number | null;
  graceUntil: number | null;
  graceCancel: (() => void) | null;
  segStartFrame: number | null;
  segStartScore: number | null;
  decision: 'rotate' | 'continue' | null; // set at an individual game over
  rotateCancel: (() => void) | null;
}

interface QueueEntry {
  userId: number;
  since: number;
  misses: number;
}

interface Offer {
  port: number;
  userId: number;
  kind: 'seat' | 'handoff' | 'turn';
  from: number | null;
  expiresAt: number;
  stuck: boolean;
  cancel: () => void;
}

export interface ChatMsg {
  id: number;
  userId: number;
  name: string;
  text: string;
  at: number;
  frame: number | null;
}

interface PendingSwitch {
  gameId: number;
  title: string;
  by: 'vote' | 'override';
  actor: number | null;
  reason: string;
  approvedAt: number;
  deadline: number;
  optInCloses: number;
  optIn: Map<number, boolean>;
  waitingFor: string;           // human readable switch point
  cancelTimer: () => void;
}

interface ManualReport {
  userId: number;
  port: number;
  outcome: 'won' | 'lost' | 'draw';
  at: number;
}

export const REMOVAL_REASONS = ['afk', 'disruptive', 'abusive', 'stuck', 'left-device', 'other'] as const;
export type RemovalReason = (typeof REMOVAL_REASONS)[number];

const MAX_CHAT = 500;
// A coin pulse lasts 4 frames; the adapter reports status every 6 frames.
const COIN_SETTLE_FRAMES = 4 + 12;

export class Room {
  readonly groupId: number;
  settings: RoomSettings;
  private readonly io: RoomIO;
  game: RoomGame | null = null;

  readonly members = new Map<number, Member>();
  private connUser = new Map<string, number>();
  seats: Seat[] = [];
  queue: QueueEntry[] = [];
  private offers = new Map<number, Offer>();

  // roles
  hostId: number | null = null;
  deputies = new Map<number, { order: number; scope: 'session' | 'persistent' }>();
  actingHost: { userId: number; kind: 'deputy' | 'admin' | 'volunteer'; since: number } | null = null;
  private hostAwayCancel: (() => void) | null = null;
  private volunteerOffer: { userId: number; cancel: () => void } | null = null;

  // versus
  private matchOwners: (number | null)[] | null = null;
  private matchMixed = false;
  // A match was interrupted mid-way: the in-game match that is still running
  // continues with a different player and must not count for anyone.
  private matchDirty = false;
  private consecutiveDraws = 0;
  private reports: ManualReport[] = [];
  private reportCancel: (() => void) | null = null;
  disputed = false;

  // single / collab / turns
  participants: number[] = [];
  turnIndex = 0;
  private turnsCompleted = 0;
  private runStart: (number | null)[] = [];
  private runContinues: number[] = [];

  // voting
  ballot: Ballot | null = null;
  private ballotCancel: (() => void) | null = null;
  private lastBallotClosedAt = 0;
  private lastNomination = new Map<number, number>();
  lastOutcome: BallotOutcome | null = null;
  pendingSwitch: PendingSwitch | null = null;

  // chat
  chatLog: ChatMsg[] = [];
  loadChat(msgs: ChatMsg[]): void {
    this.chatLog = msgs.slice(-200);
  }
  private chatRate = new Map<number, number[]>();
  private mutes = new Map<number, number>();
  private nextChatId = 1;
  private lastCoin = new Map<number, number>();
  private coinPendingUntil: number | null = null;

  constructor(groupId: number, io: RoomIO, settings: Partial<RoomSettings> = {}) {
    this.groupId = groupId;
    this.io = io;
    this.settings = { ...DEFAULT_SETTINGS, ...settings };
  }

  // ================================================================ helpers

  private now(): number {
    return this.io.now();
  }

  private member(userId: number): Member | undefined {
    return this.members.get(userId);
  }

  private nameOf(userId: number | null): string {
    if (userId === null) return 'nobody';
    return this.members.get(userId)?.name ?? `user ${userId}`;
  }

  private connected(userId: number): boolean {
    return (this.members.get(userId)?.conns.size ?? 0) > 0;
  }

  presentUsers(): number[] {
    return [...this.members.values()].filter((m) => m.conns.size > 0).map((m) => m.userId);
  }

  seatOf(userId: number): Seat | null {
    return this.seats.find((s) => s.userId === userId) ?? null;
  }

  queueIndex(userId: number): number {
    return this.queue.findIndex((q) => q.userId === userId);
  }

  private offerFor(userId: number): Offer | null {
    for (const o of this.offers.values()) if (o.userId === userId) return o;
    return null;
  }

  private get mode(): RoomMode | null {
    return this.game?.mode ?? null;
  }

  private status(port: number): PlayerStatus | null {
    return this.game?.status?.players[port] ?? null;
  }

  private changed(): void {
    this.io.stateChanged();
  }

  private event(text: string, opts: { frame?: number | null; kind?: string } = {}): void {
    this.io.broadcast({ t: 'event', kind: opts.kind ?? 'info', text, frame: opts.frame ?? null, at: this.now() });
  }

  // ================================================================ roles

  isHost(userId: number): boolean {
    return userId === this.hostId;
  }

  private isOwner(userId: number): boolean {
    return this.member(userId)?.tgRole === 'creator';
  }

  // Telegram group admins act with deputy powers (documented mapping).
  isDeputy(userId: number): boolean {
    return this.deputies.has(userId) || this.member(userId)?.tgRole === 'administrator' || this.isOwner(userId);
  }

  isActingHost(userId: number): boolean {
    return this.actingHost?.userId === userId;
  }

  canModerate(userId: number): boolean {
    return this.isHost(userId) || this.isActingHost(userId) || this.isDeputy(userId);
  }

  // Host-only powers: deputies, succession, room settings. The Telegram group
  // owner always holds them too. An acting host never does.
  canAdminister(userId: number): boolean {
    return this.isHost(userId) || this.isOwner(userId);
  }

  private requireModerator(userId: number): void {
    if (!this.canModerate(userId)) throw new RoomError('forbidden', 'Only the host or a deputy can do that.');
  }

  private requireAdmin(userId: number): void {
    if (!this.canAdminister(userId)) throw new RoomError('forbidden', 'Only the host can do that.');
  }

  setHost(userId: number | null, persist = true): void {
    this.hostId = userId;
    if (userId !== null && this.connected(userId)) this.endActingHost('host present');
    if (persist) this.persistRoles();
    this.changed();
  }

  private persistRoles(): void {
    const dep = [...this.deputies.entries()].filter(([, d]) => d.scope === 'persistent').map(([userId, d]) => ({ userId, order: d.order }));
    this.io.persistRoles(this.hostId, dep);
  }

  appointDeputy(actor: number, userId: number, scope: 'session' | 'persistent', order?: number): void {
    this.requireAdmin(actor);
    if (userId === this.hostId) throw new RoomError('bad_target', 'The host is already in charge.');
    const o = order ?? this.deputies.size + 1;
    this.deputies.set(userId, { order: o, scope });
    this.io.audit(actor, 'deputy.appoint', userId, null, { scope, order: o });
    this.event(`${this.nameOf(userId)} is now a deputy${scope === 'session' ? ' for this session' : ''}.`, { kind: 'role' });
    if (scope === 'persistent') this.persistRoles();
    this.changed();
  }

  removeDeputy(actor: number, userId: number): void {
    this.requireAdmin(actor);
    const d = this.deputies.get(userId);
    if (!d) return;
    this.deputies.delete(userId);
    this.io.audit(actor, 'deputy.remove', userId, null);
    this.event(`${this.nameOf(userId)} is no longer a deputy.`, { kind: 'role' });
    if (this.isActingHost(userId)) this.endActingHost('deputy removed');
    if (d.scope === 'persistent') this.persistRoles();
    this.changed();
  }

  setSuccession(actor: number, order: number[]): void {
    this.requireAdmin(actor);
    order.forEach((u, i) => {
      const d = this.deputies.get(u);
      if (d) d.order = i + 1;
    });
    this.io.audit(actor, 'deputy.succession', null, null, { order });
    this.persistRoles();
    this.changed();
  }

  transferHost(actor: number, userId: number): void {
    this.requireAdmin(actor);
    const old = this.hostId;
    this.deputies.delete(userId);
    this.hostId = userId;
    this.io.audit(actor, 'host.transfer', userId, null, { from: old });
    this.event(`${this.nameOf(userId)} is now the host.`, { kind: 'role' });
    this.endActingHost('new host');
    if (!this.connected(userId)) this.onHostAway();
    this.persistRoles();
    this.changed();
  }

  private onHostAway(): void {
    if (this.hostAwayCancel || this.hostId === null) return;
    this.hostAwayCancel = this.io.timer(this.settings.hostGraceSeconds * 1000, () => {
      this.hostAwayCancel = null;
      if (this.hostId !== null && !this.connected(this.hostId)) this.chooseActingHost();
    });
  }

  // Succession: a present designated deputy (by order), then a present
  // Telegram admin, then a volunteer offer to an established participant.
  // Acting host powers are temporary and limited (see canAdminister).
  private chooseActingHost(skip: Set<number> = new Set()): void {
    if (this.actingHost && this.connected(this.actingHost.userId)) return;
    this.actingHost = null;
    const present = this.presentUsers().filter((u) => !skip.has(u));
    const deputies = present.filter((u) => this.deputies.has(u)).sort((a, b) => this.deputies.get(a)!.order - this.deputies.get(b)!.order);
    if (deputies.length) return this.startActingHost(deputies[0], 'deputy');
    const admins = present
      .filter((u) => { const r = this.member(u)?.tgRole; return r === 'administrator' || r === 'creator'; })
      .sort((a, b) => (this.member(a)!.presentSince ?? 0) - (this.member(b)!.presentSince ?? 0));
    if (admins.length) return this.startActingHost(admins[0], 'admin');
    const established = present
      .filter((u) => (this.member(u)!.presentSince ?? Infinity) <= this.now() - 5 * 60_000)
      .sort((a, b) => (this.member(a)!.presentSince ?? 0) - (this.member(b)!.presentSince ?? 0));
    if (established.length) {
      const u = established[0];
      this.io.send(u, { t: 'acting_host_offer', expiresAt: this.now() + 30_000 });
      const cancel = this.io.timer(30_000, () => {
        if (this.volunteerOffer?.userId === u) {
          this.volunteerOffer = null;
          this.chooseActingHost(new Set([...skip, u]));
        }
      });
      this.volunteerOffer = { userId: u, cancel };
      return;
    }
    this.event('The host is away. Normal room rules keep running automatically.', { kind: 'role' });
    this.changed();
  }

  private startActingHost(userId: number, kind: 'deputy' | 'admin' | 'volunteer'): void {
    this.actingHost = { userId, kind, since: this.now() };
    this.io.audit(null, 'host.acting', userId, kind);
    this.event(`The host is away; ${this.nameOf(userId)} is acting host.`, { kind: 'role' });
    this.changed();
  }

  answerActingHostOffer(userId: number, accept: boolean): void {
    if (this.volunteerOffer?.userId !== userId) throw new RoomError('no_offer', 'There is no acting-host offer for you.');
    this.volunteerOffer.cancel();
    this.volunteerOffer = null;
    if (accept) this.startActingHost(userId, 'volunteer');
    else this.chooseActingHost(new Set([userId]));
  }

  private endActingHost(reason: string): void {
    if (this.hostAwayCancel) {
      this.hostAwayCancel();
      this.hostAwayCancel = null;
    }
    if (this.volunteerOffer) {
      this.volunteerOffer.cancel();
      this.volunteerOffer = null;
    }
    if (this.actingHost) {
      this.io.audit(null, 'host.acting_end', this.actingHost.userId, reason);
      this.actingHost = null;
      this.changed();
    }
  }

  // ================================================================ presence

  // A member's Telegram status changed while connected (promoted/demoted).
  setTgRole(userId: number, role: TgRole): void {
    const m = this.member(userId);
    if (!m || m.tgRole === role) return;
    m.tgRole = role;
    this.changed();
  }

  connect(userId: number, connId: string, profile: { name: string; tgRole: TgRole }): void {
    let m = this.members.get(userId);
    const now = this.now();
    if (!m) {
      m = { userId, name: profile.name, conns: new Set(), lastConn: null, joinedAt: now, presentSince: null, tgRole: profile.tgRole };
      this.members.set(userId, m);
    }
    m.name = profile.name;
    m.tgRole = profile.tgRole;
    const wasAway = m.conns.size === 0;
    m.conns.add(connId);
    m.lastConn = connId;
    if (wasAway) m.presentSince = now;
    this.connUser.set(connId, userId);
    this.ballot?.arrive(userId);

    const seat = this.seatOf(userId);
    if (seat && (seat.graceUntil !== null || seat.controlConn === null)) {
      // Returning player: the seat survived the grace period. Control moves
      // to the new connection with a fresh epoch.
      seat.graceCancel?.();
      seat.graceCancel = null;
      seat.graceUntil = null;
      seat.controlConn = connId;
      seat.epoch = this.game ? this.game.resetPort(seat.port) : seat.epoch + 1;
      this.event(`${m.name} reconnected.`);
    }
    if (userId === this.hostId) this.endActingHost('host returned');
    this.sendYou(userId);
    this.changed();
  }

  disconnect(connId: string): void {
    const userId = this.connUser.get(connId);
    if (userId === undefined) return;
    this.connUser.delete(connId);
    const m = this.members.get(userId);
    if (!m) return;
    m.conns.delete(connId);
    if (m.lastConn === connId) m.lastConn = [...m.conns].pop() ?? null;
    const seat = this.seatOf(userId);
    if (seat && seat.controlConn === connId) {
      this.game?.releasePort(seat.port);
      if (m.conns.size > 0) {
        // another device of the same person takes over input authority
        seat.controlConn = m.lastConn;
        seat.epoch = this.game ? this.game.resetPort(seat.port) : seat.epoch + 1;
        this.sendYou(userId);
      } else {
        seat.controlConn = null;
        seat.graceUntil = this.now() + this.settings.graceSeconds * 1000;
        const port = seat.port;
        seat.graceCancel = this.io.timer(this.settings.graceSeconds * 1000, () => this.onGraceExpired(port, userId));
        this.event(`${m.name} lost connection; keeping the seat for ${this.settings.graceSeconds}s.`);
      }
    }
    if (m.conns.size === 0) {
      m.presentSince = null;
      if (userId === this.hostId) this.onHostAway();
      if (this.actingHost?.userId === userId) {
        this.actingHost = null;
        if (this.hostId !== null && !this.connected(this.hostId)) this.chooseActingHost();
      }
      if (this.volunteerOffer?.userId === userId) this.answerActingHostOffer(userId, false);
    }
    this.changed();
  }

  private onGraceExpired(port: number, userId: number): void {
    const seat = this.seats[port];
    if (!seat || seat.userId !== userId || this.connected(userId)) return;
    seat.graceCancel = null;
    this.event(`${this.nameOf(userId)} did not come back; the seat is open.`);
    this.vacate(port, 'disconnect');
  }

  // A focus loss, app switch or chat focus on the client: release held buttons.
  blur(connId: string): void {
    const userId = this.connUser.get(connId);
    if (userId === undefined) return;
    const seat = this.seatOf(userId);
    if (seat && seat.controlConn === connId) this.game?.releasePort(seat.port);
  }

  // Moves input authority to this device (multiple tabs/devices).
  takeControlHere(userId: number, connId: string): void {
    const seat = this.seatOf(userId);
    if (!seat) throw new RoomError('not_seated', 'You are not in a player seat.');
    if (this.connUser.get(connId) !== userId) throw new RoomError('forbidden', 'Unknown connection.');
    seat.controlConn = connId;
    seat.epoch = this.game ? this.game.resetPort(seat.port) : seat.epoch + 1;
    this.sendYou(userId);
    this.changed();
  }

  // Input gate used by the gateway: may this connection drive this port?
  inputAllowed(connId: string, port: number): boolean {
    const seat = this.seats[port];
    if (!seat || seat.userId === null) return false;
    return seat.controlConn === connId && this.connUser.get(connId) === seat.userId;
  }

  // ================================================================ game lifecycle

  // Binds a (new) running game. Seats are resized to the game's ports.
  setGame(game: RoomGame | null, seatOrder: number[] = []): void {
    // close segments of the previous game
    for (const s of this.seats) if (s.userId !== null) this.closeSegment(s, 'game ended');
    for (const o of this.offers.values()) o.cancel();
    this.offers.clear();
    for (const s of this.seats) {
      s.graceCancel?.();
      s.rotateCancel?.();
    }
    this.game = game;
    this.coinPendingUntil = null;
    this.matchOwners = null;
    this.matchMixed = false;
    this.consecutiveDraws = 0;
    this.reports = [];
    this.reportCancel?.();
    this.disputed = false;
    this.turnIndex = 0;
    const ports = game ? (game.mode === 'turns-shared' || game.mode === 'collab' || game.mode === 'single' ? 1 : game.ports) : 0;
    this.runStart = new Array(ports).fill(null);
    this.runContinues = new Array(ports).fill(0);
    const previous = this.seats.map((s) => s.userId).filter((u): u is number => u !== null);
    this.seats = Array.from({ length: ports }, (_, port) => ({
      port, userId: null, controlConn: null, epoch: 0, since: null, graceUntil: null, graceCancel: null,
      segStartFrame: null, segStartScore: null, decision: null, rotateCancel: null,
    }));
    if (!game) {
      this.changed();
      return;
    }
    const order = seatOrder.length ? seatOrder : previous;
    this.turnsCompleted = 0;
    this.matchDirty = false;
    if (game.mode === 'turns-shared') {
      this.participants = order.filter((u) => this.members.has(u)).slice(0, Math.max(1, game.ports));
    } else {
      this.participants = [];
    }
    const seatList = game.mode === 'turns-shared' ? this.participants.slice(0, 1) : order;
    for (const u of seatList) {
      const free = this.seats.find((s) => s.userId === null);
      if (!free) break;
      if (this.members.has(u) && !this.seatOf(u)) this.assign(free.port, u, null);
    }
    this.fillSeats();
    this.changed();
  }

  // Called when the game reports a status update (adapter).
  onStatus(): void {
    if (this.mode === 'versus') this.trackMatchOwners();
    this.changed();
  }

  // ================================================================ seats

  private assign(port: number, userId: number, connId: string | null): void {
    const seat = this.seats[port];
    const m = this.members.get(userId);
    seat.userId = userId;
    seat.controlConn = connId ?? m?.lastConn ?? null;
    seat.since = this.now();
    seat.graceUntil = null;
    seat.decision = null;
    seat.epoch = this.game ? this.game.resetPort(port) : seat.epoch + 1;
    seat.segStartFrame = this.game?.frame() ?? null;
    seat.segStartScore = this.status(port)?.score ?? null;
    this.removeFromQueue(userId);
    if (seat.controlConn === null && !this.connected(userId)) {
      // assigned while away (e.g. after a switch): normal grace applies
      seat.graceUntil = this.now() + this.settings.graceSeconds * 1000;
      seat.graceCancel = this.io.timer(this.settings.graceSeconds * 1000, () => this.onGraceExpired(port, userId));
    }
    if (this.mode === 'versus') this.trackMatchOwners();
    this.scheduleTimedRotation(port, userId);
    this.sendYou(userId);
    this.changed();
  }

  // Optional, host-approved timed rotation for single-player games that have
  // no detectable game over: after N minutes with someone waiting, the
  // player gets a 30 s warning and then hands over.
  private scheduleTimedRotation(port: number, userId: number): void {
    const seat = this.seats[port];
    seat.rotateCancel?.();
    seat.rotateCancel = null;
    const minutes = this.settings.timedRotationMinutes;
    if (!minutes || this.mode !== 'single' || this.game?.adapter?.capabilities.playerGameOver) return;
    const check = () => {
      if (seat.userId !== userId) return;
      if (this.queue.length === 0) {
        seat.rotateCancel = this.io.timer(60_000, check);
        return;
      }
      this.io.send(userId, { t: 'notice', text: 'Timed rotation: please hand over in 30 seconds.' });
      this.event(`Timed rotation: ${this.nameOf(userId)} hands over in 30 s.`);
      seat.rotateCancel = this.io.timer(30_000, () => {
        if (seat.userId !== userId || this.queue.length === 0) return;
        this.event(`Time's up for ${this.nameOf(userId)} (room rule: ${minutes} min).`);
        this.vacate(port, 'rotate');
        this.queue.push({ userId, since: this.now(), misses: 0 });
        this.sendYou(userId);
        this.changed();
      });
    };
    seat.rotateCancel = this.io.timer(minutes * 60_000, check);
  }

  private closeSegment(seat: Seat, reason: string): void {
    if (seat.userId === null || !this.game) return;
    this.io.recordSegment({
      sessionId: this.game.sessionId,
      port: seat.port,
      userId: seat.userId,
      startFrame: seat.segStartFrame ?? 0,
      endFrame: this.game.frame(),
      startScore: seat.segStartScore,
      endScore: this.status(seat.port)?.score ?? null,
      endReason: reason,
    });
  }

  // Empties a seat and offers it to the next person in the queue.
  private vacate(port: number, reason: 'leave' | 'disconnect' | 'rotate' | 'moderation' | 'handoff' | 'turn'): number | null {
    const seat = this.seats[port];
    const userId = seat.userId;
    if (userId === null) return null;
    if (this.mode === 'versus' && this.matchOwners && (reason === 'leave' || reason === 'disconnect' || reason === 'moderation')) {
      this.endMatchEarly(reason === 'leave' ? 'abandoned' : 'interrupted');
    }
    this.closeSegment(seat, reason);
    seat.graceCancel?.();
    seat.rotateCancel?.();
    seat.userId = null;
    seat.controlConn = null;
    seat.since = null;
    seat.graceUntil = null;
    seat.graceCancel = null;
    seat.rotateCancel = null;
    seat.decision = null;
    seat.epoch = this.game ? this.game.resetPort(port) : seat.epoch + 1;
    this.sendYou(userId);
    if (reason !== 'turn' && reason !== 'handoff') this.fillSeat(port);
    this.changed();
    return userId;
  }

  private fillSeats(): void {
    for (const s of this.seats) this.fillSeat(s.port);
  }

  private fillSeat(port: number): void {
    if (!this.game || this.mode === 'turns-shared') return;
    const seat = this.seats[port];
    if (!seat || seat.userId !== null || this.offers.has(port)) return;
    const offered = new Set([...this.offers.values()].map((o) => o.userId));
    const next = this.queue.find((q) => !offered.has(q.userId));
    if (!next) return;
    this.makeOffer(port, next.userId, 'seat', null);
  }

  private makeOffer(port: number, userId: number, kind: Offer['kind'], from: number | null): void {
    const expiresAt = this.now() + this.settings.offerSeconds * 1000;
    const cancel = this.io.timer(this.settings.offerSeconds * 1000, () => this.onOfferTimeout(port, userId));
    this.offers.set(port, { port, userId, kind, from, expiresAt, stuck: false, cancel });
    this.io.send(userId, { t: 'offer', port, kind, expiresAt, from });
    this.sendYou(userId);
    this.changed();
  }

  private onOfferTimeout(port: number, userId: number): void {
    const o = this.offers.get(port);
    if (!o || o.userId !== userId) return;
    if (o.kind === 'turn') {
      // Turn-based: never hand a turn to someone else automatically. The
      // offer stays open and moderators are told the turn is stuck.
      o.stuck = true;
      this.event(`Waiting for ${this.nameOf(userId)} to take their turn. The host or a deputy can resolve it.`, { kind: 'warn' });
      this.changed();
      return;
    }
    this.offers.delete(port);
    if (o.kind === 'handoff') {
      this.event(`${this.nameOf(userId)} did not accept the controller in time.`);
      this.changed();
      return;
    }
    // Skipped candidate: first miss moves them to the back of the queue,
    // a second consecutive miss removes them.
    const idx = this.queueIndex(userId);
    if (idx >= 0) {
      const entry = this.queue[idx];
      this.queue.splice(idx, 1);
      entry.misses++;
      if (entry.misses >= 2) {
        this.io.send(userId, { t: 'notice', text: 'You missed two seat offers and left the queue. Join again any time.' });
      } else {
        this.queue.push(entry);
        this.io.send(userId, { t: 'notice', text: 'You missed your seat offer and moved to the back of the queue.' });
      }
    }
    this.sendYou(userId);
    this.fillSeat(port);
    this.changed();
  }

  takeSeat(userId: number, connId: string, port?: number): void {
    if (!this.game) throw new RoomError('no_game', 'No game is running.');
    if (this.seatOf(userId)) throw new RoomError('already_seated', 'You already have a seat.');
    const offer = this.offerFor(userId);
    if (offer) return this.acceptOffer(userId, connId);
    if (this.mode === 'turns-shared') throw new RoomError('turns', 'Join the turn order instead.');
    if (this.queue.length > 0) throw new RoomError('queue_waiting', 'People are waiting; join the queue.');
    const seat = port !== undefined ? this.seats[port] : this.seats.find((s) => s.userId === null && !this.offers.has(s.port));
    if (!seat) throw new RoomError('no_seat', 'All seats are taken; join the queue.');
    if (seat.userId !== null) throw new RoomError('seat_taken', 'That seat was just taken.');
    if (this.offers.has(seat.port)) throw new RoomError('seat_offered', 'That seat is being offered to someone else.');
    this.assign(seat.port, userId, connId);
    this.event(`${this.nameOf(userId)} takes player ${seat.port + 1}.`);
  }

  leaveSeat(userId: number): void {
    const seat = this.seatOf(userId);
    if (!seat) throw new RoomError('not_seated', 'You are not seated.');
    this.event(`${this.nameOf(userId)} left player ${seat.port + 1}.`);
    this.vacate(seat.port, 'leave');
  }

  joinQueue(userId: number): void {
    if (!this.members.has(userId)) throw new RoomError('not_present', 'Open the room first.');
    if (this.seatOf(userId)) throw new RoomError('seated_cannot_queue', 'You are playing; you cannot also hold a place in the queue.');
    if (this.queueIndex(userId) >= 0) return;
    this.queue.push({ userId, since: this.now(), misses: 0 });
    this.sendYou(userId);
    this.fillSeats();
    this.changed();
  }

  leaveQueue(userId: number): void {
    const o = this.offerFor(userId);
    if (o && o.kind === 'seat') {
      o.cancel();
      this.offers.delete(o.port);
      this.removeFromQueue(userId);
      this.fillSeat(o.port);
    } else {
      this.removeFromQueue(userId);
    }
    this.sendYou(userId);
    this.changed();
  }

  private removeFromQueue(userId: number): void {
    const idx = this.queueIndex(userId);
    if (idx >= 0) this.queue.splice(idx, 1);
  }

  acceptOffer(userId: number, connId: string): void {
    const o = this.offerFor(userId);
    if (!o) throw new RoomError('no_offer', 'That offer is no longer available.');
    o.cancel();
    this.offers.delete(o.port);
    if (o.kind === 'seat') {
      if (this.seats[o.port].userId !== null) throw new RoomError('seat_taken', 'That seat was taken.');
      this.assign(o.port, userId, connId);
      this.event(`${this.nameOf(userId)} takes player ${o.port + 1}.`);
    } else {
      // handoff / turn: the previous controller (if still seated) gives way
      const seat = this.seats[o.port];
      if (seat.userId !== null && seat.userId !== userId) this.vacate(o.port, o.kind === 'turn' ? 'turn' : 'handoff');
      this.assign(o.port, userId, connId);
      if (o.kind === 'turn') {
        const idx = this.participants.indexOf(userId);
        if (idx >= 0) this.turnIndex = idx;
        this.turnsCompleted++;
      }
      this.event(`${this.nameOf(userId)} has the controller.`);
    }
  }

  declineOffer(userId: number): void {
    const o = this.offerFor(userId);
    if (!o) return;
    o.cancel();
    this.offers.delete(o.port);
    if (o.kind === 'seat') {
      this.removeFromQueue(userId);
      this.io.send(userId, { t: 'notice', text: 'You declined the seat and left the queue.' });
      this.fillSeat(o.port);
    } else if (o.kind === 'turn') {
      this.event(`${this.nameOf(userId)} declined the controller. The host or a deputy can resolve the turn.`, { kind: 'warn' });
    } else {
      this.event(`${this.nameOf(userId)} declined the controller.`);
    }
    this.sendYou(userId);
    this.changed();
  }

  // ================================================================ coins

  // Coins are server-mediated: a seated player asks, the server checks the
  // credit policy and pulses the coin input itself. Credits are inserted just
  // in time (never banked), and a player whose game-over was decided as a
  // rotation cannot buy a continue.
  insertCoin(userId: number): void {
    const seat = this.seatOf(userId);
    if (!seat || !this.game) throw new RoomError('not_seated', 'Only seated players can insert a coin.');
    if (this.game.coinBit === null) throw new RoomError('no_coin_slot', 'This game has no coin slot.');
    const now = this.now();
    const last = this.lastCoin.get(userId) ?? 0;
    const caps = this.game.adapter?.capabilities;
    const minGap = caps?.credits ? 1000 : 5000;
    if (now - last < minGap) throw new RoomError('rate_limited', 'Wait a moment before inserting another coin.');
    if (seat.decision === 'rotate') throw new RoomError('rotation_pending', 'Someone is waiting, so your turn ends here.');
    if (caps?.credits) {
      const st = this.game.status;
      // A coin that was just pulsed counts as a credit until the adapter has
      // reported a frame after it (status arrives every few frames), so a
      // second request in that window cannot bank a credit.
      const pending = this.coinPendingUntil !== null && (st?.frame ?? -1) < this.coinPendingUntil;
      if ((st?.credits ?? 0) > 0 || pending) throw new RoomError('credit_available', 'There is already a credit in the machine; press START.');
    }
    this.lastCoin.set(userId, now);
    this.game.pulse(seat.port, this.game.coinBit);
    this.coinPendingUntil = this.game.frame() + COIN_SETTLE_FRAMES;
  }

  // ================================================================ adapter events

  onAdapterEvents(events: AdapterEvent[]): void {
    if (!this.game) return;
    const sid = this.game.sessionId;
    for (const e of events) {
      const key = `${sid}:${e.type}:${e.frame}:${e.port ?? ''}`;
      if (!this.io.recordEvent(key, e.type, e.frame, 'adapter', { port: e.port ?? null, winnerPort: e.winnerPort ?? null, scores: e.scores ?? null, data: e.data ?? null })) continue;
      switch (e.type) {
        case 'player_join':
          if (e.port !== undefined && e.port < this.runStart.length) {
            this.runStart[e.port] = e.frame;
            this.runContinues[e.port] = 0;
          }
          break;
        case 'player_continue':
          if (e.port !== undefined && e.port < this.runContinues.length) this.runContinues[e.port]++;
          if (e.port !== undefined && this.seats[e.port]) this.seats[e.port].decision = null;
          break;
        case 'player_game_over':
          if (e.port !== undefined) this.handleGameOver(e.port, 'verified', e.frame);
          break;
        case 'player_out':
          if (e.port !== undefined) this.recordRun(e.port, e.frame, e.scores?.[e.port] ?? null, 'verified');
          break;
        case 'match_end':
          if (this.mode === 'versus') this.handleMatchEnd(e.winnerPort ?? null, 'verified', e.frame);
          else if (this.mode === 'turns-shared' || this.mode === 'turns-multi') this.handleTurnsGameEnd(e, 'verified');
          break;
        case 'stage_clear':
          this.event(`Stage clear!`, { frame: e.frame, kind: 'game' });
          if (this.pendingSwitch && this.mode === 'coop') this.reachedSwitchPoint('stage boundary');
          break;
        case 'game_over':
          for (let p = 0; p < this.runStart.length; p++) this.recordRun(p, e.frame, e.scores?.[p] ?? null, 'verified');
          this.event('Game over.', { frame: e.frame, kind: 'game' });
          if (this.pendingSwitch) this.reachedSwitchPoint('game over');
          break;
        case 'turn_change':
          if (this.mode === 'turns-shared' && e.port !== undefined) this.onTurnChange(e.port);
          break;
      }
    }
  }

  // ================================================================ co-op / single: individual game over

  private handleGameOver(port: number, source: 'verified' | 'manual', frame: number | null): void {
    const seat = this.seats[port];
    if (!seat || seat.userId === null) return;
    if (this.mode === 'versus') return; // versus rotates on complete matches only
    const someoneWaiting = this.queue.length > 0;
    if (!someoneWaiting) {
      seat.decision = 'continue';
      this.event(`${this.nameOf(seat.userId)} is out. Nobody is waiting, so continue as often as you like.`, { frame, kind: 'game' });
      this.changed();
      return;
    }
    // Someone is waiting: the decision is locked now. The player cannot buy
    // a continue; the seat goes to the next person, who can pick up the run
    // at the game's continue screen where the game supports it.
    seat.decision = 'rotate';
    const leaving = seat.userId;
    this.event(`${this.nameOf(leaving)} is out (${source === 'verified' ? 'detected' : 'reported'}); next player up.`, { frame, kind: 'game' });
    this.vacate(port, 'rotate');
    this.queue.push({ userId: leaving, since: this.now(), misses: 0 });
    this.sendYou(leaving);
    this.changed();
  }

  // Manual fallback when the game has no verified game-over detection.
  reportOut(userId: number, targetUser?: number): void {
    const target = targetUser ?? userId;
    if (target !== userId) this.requireModerator(userId);
    const seat = this.seatOf(target);
    if (!seat) throw new RoomError('not_seated', 'That person is not seated.');
    if (this.game?.adapter?.capabilities.playerGameOver) throw new RoomError('automatic', 'This game detects game over automatically.');
    this.handleGameOver(seat.port, 'manual', this.game?.frame() ?? null);
  }

  // Score of one in-game player's run, attributed by control segments.
  private recordRun(port: number, frame: number, score: number | null, verification: 'verified' | 'manual'): void {
    if (!this.game || score === null || score <= 0) return;
    const start = this.runStart[port] ?? 0;
    const seat = this.seats[port];
    const owner = seat?.userId ?? null;
    this.io.recordScore({
      key: `${this.game.sessionId}:run:${port}:${start}`,
      sessionId: this.game.sessionId,
      gameId: this.game.gameId,
      port,
      score,
      kind: 'seat',            // finalized by the records module from control segments
      userId: owner,
      participants: [],
      verification,
      continues: this.game.adapter?.capabilities.credits ? (this.runContinues[port] ?? 0) : null,
      openSegment: owner !== null ? { user: owner, from: seat!.segStartFrame ?? 0, startScore: seat!.segStartScore } : undefined,
    });
    this.runStart[port] = null;
    this.runContinues[port] = 0;
    void frame;
  }

  // ================================================================ versus

  private trackMatchOwners(): void {
    if (this.seats.length < 2) return;
    const owners = this.seats.map((s) => s.userId);
    const st = this.game?.status;
    if (!this.matchOwners) {
      if (owners.every((u) => u !== null) && (!st || st.phase === 'playing' || st.phase === 'round-over')) {
        this.matchOwners = owners;
        this.matchMixed = this.matchDirty;
      } else if (st && (st.phase === 'attract' || st.phase === 'match-over')) {
        this.matchDirty = false; // a clean boundary: the next match starts fresh
      }
      return;
    }
    if (owners.some((u, i) => u !== this.matchOwners![i])) this.matchMixed = true;
  }

  private handleMatchEnd(winnerPort: number | null, source: 'verified' | 'manual' | 'adjudicated', frame: number | null): void {
    if (!this.game) return;
    const owners = this.seats.map((s) => s.userId);
    const counted = this.matchOwners !== null && !this.matchMixed && owners.every((u, i) => u === this.matchOwners![i]);
    const winner = winnerPort === null ? null : owners[winnerPort] ?? null;
    const key = `${this.game.sessionId}:match:${frame ?? this.now()}`;
    this.io.recordMatch({
      key,
      sessionId: this.game.sessionId,
      gameId: this.game.gameId,
      p1: owners[0] ?? null,
      p2: owners[1] ?? null,
      winner,
      result: winnerPort === null ? 'draw' : 'win',
      verification: source,
      counts: counted,
    });
    this.reports = [];
    this.reportCancel?.();
    this.reportCancel = null;
    this.disputed = false;
    this.matchOwners = null;
    this.matchMixed = false;
    this.matchDirty = false;
    const text = winnerPort === null ? 'Draw!' : `${this.nameOf(winner)} wins the match!`;
    this.event(text + (counted ? '' : ' (not counted: the controller changed hands mid-match)'), { frame, kind: 'result' });
    if (this.pendingSwitch) {
      this.reachedSwitchPoint('match finished');
      return;
    }
    if (this.queue.length === 0) {
      // nobody waiting: same players rematch, indefinitely
      this.consecutiveDraws = winnerPort === null ? this.consecutiveDraws + 1 : 0;
      return;
    }
    let loserPort: number | null;
    if (winnerPort === null) {
      this.consecutiveDraws++;
      if (this.consecutiveDraws < this.settings.drawsBeforeRotation) {
        this.event('Draw: same players go again.', { kind: 'info' });
        return;
      }
      // repeated draws with people waiting: the longer-seated player rotates
      const a = this.seats[0].since ?? 0, b = this.seats[1].since ?? 0;
      loserPort = a <= b ? 0 : 1;
    } else {
      loserPort = winnerPort === 0 ? 1 : 0;
    }
    this.consecutiveDraws = 0;
    const loser = this.seats[loserPort].userId;
    if (loser === null) return;
    // Winner stays (no streak limit); loser goes to the back of the queue.
    this.vacate(loserPort, 'rotate');
    this.queue.push({ userId: loser, since: this.now(), misses: 0 });
    this.sendYou(loser);
    this.changed();
  }

  private endMatchEarly(result: 'abandoned' | 'interrupted'): void {
    if (!this.game || !this.matchOwners) return;
    const owners = this.matchOwners;
    this.io.recordMatch({
      key: `${this.game.sessionId}:match-early:${this.game.frame()}`,
      sessionId: this.game.sessionId,
      gameId: this.game.gameId,
      p1: owners[0] ?? null,
      p2: owners[1] ?? null,
      winner: null,
      result,
      verification: 'verified',
      counts: false,          // a disconnect is never recorded as a competitive loss
    });
    this.matchOwners = null;
    this.matchMixed = false;
    this.matchDirty = true;
    this.reports = [];
  }

  // Manual result reporting for fighters without verified detection.
  reportResult(userId: number, outcome: 'won' | 'lost' | 'draw'): void {
    if (this.mode !== 'versus') throw new RoomError('bad_mode', 'Results are reported only in versus games.');
    if (this.game?.adapter?.capabilities.matchResult) throw new RoomError('automatic', 'This game detects results automatically.');
    const seat = this.seatOf(userId);
    if (!seat) throw new RoomError('not_seated', 'Only the two players can report a result.');
    const other = this.seats[seat.port === 0 ? 1 : 0];
    if (!other || other.userId === null) throw new RoomError('no_opponent', 'There is no opponent.');
    this.reports = this.reports.filter((r) => r.userId !== userId);
    this.reports.push({ userId, port: seat.port, outcome, at: this.now() });
    const mine = outcome;
    const theirs = this.reports.find((r) => r.userId === other.userId)?.outcome;
    const frame = this.game?.frame() ?? null;
    if (mine === 'lost') return this.handleMatchEnd(other.port, 'manual', frame);       // concession
    if (theirs === 'lost' && mine === 'won') return this.handleMatchEnd(seat.port, 'manual', frame);
    if (mine === 'draw' && theirs === 'draw') return this.handleMatchEnd(null, 'manual', frame);
    if (theirs && theirs !== 'lost' && (theirs === mine || (mine === 'won' && theirs === 'draw') || (mine === 'draw' && theirs === 'won'))) {
      this.disputed = true;
      this.event('The players disagree about the result. The host or a deputy will decide.', { kind: 'warn' });
      this.changed();
      return;
    }
    // Waiting for the opponent: if they do not object in time, the report stands.
    this.event(`${this.nameOf(userId)} reported: ${outcome}. ${this.nameOf(other.userId)}, confirm or dispute.`, { kind: 'info' });
    this.reportCancel?.();
    this.reportCancel = this.io.timer(this.settings.resultDisputeSeconds * 1000, () => {
      this.reportCancel = null;
      const r = this.reports.find((x) => x.userId === userId);
      if (!r || this.disputed) return;
      if (r.outcome === 'won') this.handleMatchEnd(seat.port, 'manual', this.game?.frame() ?? null);
      else if (r.outcome === 'draw') this.handleMatchEnd(null, 'manual', this.game?.frame() ?? null);
    });
    this.changed();
  }

  adjudicate(actor: number, winnerPort: number | null | 'void', reason: string): void {
    this.requireModerator(actor);
    if (this.mode !== 'versus') throw new RoomError('bad_mode', 'Nothing to adjudicate.');
    this.io.audit(actor, 'result.adjudicate', null, reason, { winnerPort });
    if (winnerPort === 'void') {
      this.reports = [];
      this.disputed = false;
      this.matchOwners = null;
      this.event(`Result voided by ${this.nameOf(actor)}: ${reason}`, { kind: 'result' });
      this.changed();
      return;
    }
    this.handleMatchEnd(winnerPort, 'adjudicated', this.game?.frame() ?? null);
  }

  // ================================================================ single / collab: controller handoff

  // "Pass controller": offer control to the next person in the queue, or to
  // a chosen person in collaborative mode. The recipient must accept.
  passController(userId: number, toUser?: number): void {
    const seat = this.seatOf(userId);
    if (!seat) throw new RoomError('not_seated', 'You do not have the controller.');
    if (this.offers.has(seat.port)) throw new RoomError('pending', 'A handoff is already pending.');
    let target = toUser ?? null;
    if (target === null) target = this.queue[0]?.userId ?? null;
    if (target === null) {
      this.event(`${this.nameOf(userId)} put the controller down.`);
      this.vacate(seat.port, 'leave');
      return;
    }
    if (target === userId) throw new RoomError('bad_target', 'You already have it.');
    if (!this.members.has(target)) throw new RoomError('bad_target', 'That person is not in the room.');
    if (this.mode === 'single' && toUser !== undefined && this.queue.length > 0 && this.queue[0].userId !== toUser) {
      throw new RoomError('queue_order', 'People are waiting; the controller goes to the next person in the queue.');
    }
    this.makeOffer(seat.port, target, 'handoff', userId);
    this.event(`${this.nameOf(userId)} offers the controller to ${this.nameOf(target)}.`);
  }

  // ================================================================ turn-based (shared controller)

  joinTurns(userId: number): void {
    if (this.mode !== 'turns-shared') throw new RoomError('bad_mode', 'This game does not use a turn order.');
    if (this.participants.includes(userId)) return;
    const st = this.game?.status;
    // Admission point: with verified turn detection, only on the title /
    // attract screen (the game fixes its player count when it starts);
    // without it, until the first turn has been completed.
    const detects = !!this.game?.adapter?.capabilities.turnOwner;
    const atAdmission = this.participants.length === 0 || (detects ? !st || st.phase === 'attract' : this.turnsCompleted === 0);
    const cap = this.game?.adapter?.players ?? 4;
    if (!atAdmission || this.participants.length >= cap) {
      // not an admission point: wait in the spectator queue
      this.joinQueue(userId);
      this.io.send(userId, { t: 'notice', text: 'You can join the turn order at the next new game, or replace someone who leaves.' });
      return;
    }
    this.participants.push(userId);
    this.removeFromQueue(userId);
    if (this.seats[0] && this.seats[0].userId === null && this.participants.length === 1) this.assign(0, userId, null);
    this.event(`${this.nameOf(userId)} joined the turn order.`);
    this.changed();
  }

  leaveTurns(userId: number): void {
    const idx = this.participants.indexOf(userId);
    if (idx < 0) return;
    // Explicit replacement: the first person waiting takes the same place in
    // the order (same in-game player); otherwise the place stays empty and
    // moderators resolve it. Nobody's turn is played automatically.
    const replacement = this.queue[0]?.userId ?? null;
    if (replacement !== null) {
      this.participants[idx] = replacement;
      this.removeFromQueue(replacement);
      this.event(`${this.nameOf(replacement)} replaces ${this.nameOf(userId)} in the turn order.`);
    } else {
      this.participants.splice(idx, 1);
      if (this.turnIndex > idx) this.turnIndex--;
      this.event(`${this.nameOf(userId)} left the turn order.`);
    }
    const seat = this.seatOf(userId);
    if (seat) {
      this.vacate(seat.port, 'turn');
      const next = this.participants[this.turnIndex % Math.max(1, this.participants.length)];
      if (next !== undefined) this.makeOffer(0, next, 'turn', null);
    }
    this.changed();
  }

  // Manual "End turn" (no verified turn detection).
  endTurn(userId: number): void {
    if (this.mode !== 'turns-shared') throw new RoomError('bad_mode', 'This game does not use turns.');
    if (this.game?.adapter?.capabilities.turnOwner) throw new RoomError('automatic', 'This game detects turns automatically.');
    if (this.seats[0]?.userId !== userId) throw new RoomError('not_your_turn', 'It is not your turn.');
    if (this.offers.has(0)) throw new RoomError('pending', 'The controller is already on its way.');
    if (this.participants.length < 2) throw new RoomError('alone', 'Nobody else is in the turn order.');
    const next = (this.participants.indexOf(userId) + 1) % this.participants.length;
    this.makeOffer(0, this.participants[next], 'turn', userId);
    this.event(`${this.nameOf(userId)} ended their turn. ${this.nameOf(this.participants[next])}, take the controller.`);
  }

  private onTurnChange(inGamePlayer: number): void {
    const target = this.participants[inGamePlayer];
    if (target === undefined) return;
    if (this.seats[0]?.userId === target) return;
    this.offers.get(0)?.cancel();
    this.offers.delete(0);
    // The controller is released immediately so nobody plays someone else's
    // turn; the next participant accepts it.
    if (this.seats[0]?.userId !== null) this.vacate(0, 'turn');
    this.makeOffer(0, target, 'turn', null);
  }

  // Moderator resolution of a stuck turn or handoff.
  resolveController(actor: number, toUser: number | null, reason: string): void {
    this.requireModerator(actor);
    const port = 0;
    this.offers.get(port)?.cancel();
    this.offers.delete(port);
    if (this.seats[port]?.userId !== null) this.vacate(port, 'handoff');
    this.io.audit(actor, 'controller.resolve', toUser, reason);
    if (toUser !== null) {
      if (!this.members.has(toUser)) throw new RoomError('bad_target', 'That person is not in the room.');
      this.makeOffer(port, toUser, this.mode === 'turns-shared' ? 'turn' : 'handoff', actor);
    } else {
      this.fillSeat(port);
    }
    this.event(`${this.nameOf(actor)} resolved the controller: ${reason}`, { kind: 'role' });
  }

  private handleTurnsGameEnd(e: AdapterEvent, source: 'verified' | 'manual'): void {
    if (!this.game) return;
    const scores = e.scores ?? [];
    this.participants.forEach((u, i) => {
      const s = scores[i];
      if (s === null || s === undefined) return;
      this.io.recordScore({
        key: `${this.game!.sessionId}:turns:${e.frame}:${i}`,
        sessionId: this.game!.sessionId,
        gameId: this.game!.gameId,
        port: i,
        score: s,
        kind: 'individual',
        userId: u,
        participants: [{ user: u, from: 0, to: e.frame, points: s }],
        verification: source,
        continues: null,
      });
    });
    const w = e.winnerPort;
    this.event(w === null || w === undefined ? 'The game ended in a tie.' : `${this.nameOf(this.participants[w] ?? null)} wins!`, { frame: e.frame, kind: 'result' });
    if (this.pendingSwitch) this.reachedSwitchPoint('game finished');
  }

  // ================================================================ moderation

  removeFromSeat(actor: number, userId: number, reason: RemovalReason, note = ''): void {
    this.requireModerator(actor);
    if (!REMOVAL_REASONS.includes(reason)) throw new RoomError('bad_reason', 'Choose a reason for the removal.');
    if (reason === 'other' && note.trim().length < 3) throw new RoomError('bad_reason', 'Explain the removal.');
    if (userId === this.hostId && !this.canAdminister(actor)) throw new RoomError('forbidden', 'Deputies cannot remove the host.');
    const seat = this.seatOf(userId);
    if (!seat) throw new RoomError('not_seated', 'That person is not seated.');
    this.io.audit(actor, 'seat.remove', userId, `${reason}${note ? ': ' + note : ''}`);
    this.event(`${this.nameOf(actor)} removed ${this.nameOf(userId)} from player ${seat.port + 1} (${reason}${note ? ': ' + note : ''}).`, { kind: 'role' });
    this.vacate(seat.port, 'moderation');
    // Winning is never a removal reason, and a removal for being stuck or
    // away keeps the person's priority: they go to the FRONT of the queue.
    if (reason === 'afk' || reason === 'stuck' || reason === 'left-device') {
      this.queue.unshift({ userId, since: this.now(), misses: 0 });
      this.sendYou(userId);
    }
    this.changed();
  }

  removeFromQueueBy(actor: number, userId: number, reason: string): void {
    this.requireModerator(actor);
    if (userId === this.hostId && !this.canAdminister(actor)) throw new RoomError('forbidden', 'Deputies cannot remove the host.');
    this.io.audit(actor, 'queue.remove', userId, reason);
    this.leaveQueue(userId);
    this.event(`${this.nameOf(actor)} removed ${this.nameOf(userId)} from the queue (${reason}).`, { kind: 'role' });
  }

  async resetGame(actor: number, reason: string): Promise<void> {
    this.requireModerator(actor);
    this.io.audit(actor, 'game.reset', null, reason);
    this.event(`${this.nameOf(actor)} is resetting the game: ${reason}`, { kind: 'role' });
    const r = await this.io.resetGame(reason);
    if (!r.ok) this.event(`Reset failed: ${r.error}`, { kind: 'warn' });
  }

  updateSettings(actor: number, patch: Partial<RoomSettings>): void {
    this.requireAdmin(actor);
    const allowed: (keyof RoomSettings)[] = ['offerSeconds', 'graceSeconds', 'hostGraceSeconds', 'drawsBeforeRotation', 'timedRotationMinutes', 'switchTimeoutSeconds', 'optInSeconds', 'resultDisputeSeconds', 'maxViewers', 'voteSeconds'];
    const next = { ...this.settings };
    for (const k of allowed) {
      const v = patch[k];
      if (v === undefined) continue;
      if (typeof v !== 'number' || !Number.isFinite(v) || v < 0 || v > 86400) throw new RoomError('bad_setting', `Invalid value for ${k}.`);
      (next as any)[k] = Math.floor(v);
    }
    this.settings = next;
    this.io.audit(actor, 'settings.update', null, null, patch as Record<string, unknown>);
    this.changed();
  }

  // ================================================================ voting

  startBallot(userId: number): void {
    if (!this.connected(userId)) throw new RoomError('not_present', 'Open the room to start a vote.');
    if (this.ballot) throw new RoomError('ballot_open', 'A vote is already open.');
    if (this.pendingSwitch) throw new RoomError('switch_pending', 'A game switch is already pending.');
    const since = this.now() - this.lastBallotClosedAt;
    if (since < this.settings.ballotCooldownSeconds * 1000) {
      throw new RoomError('cooldown', `Please wait ${Math.ceil((this.settings.ballotCooldownSeconds * 1000 - since) / 1000)}s before another vote.`);
    }
    const id = `b${this.now().toString(36)}`;
    this.ballot = new Ballot(id, userId, this.now(), { ...DEFAULT_RULES, durationMs: this.settings.voteSeconds * 1000 }, this.game?.gameId ?? null, this.presentUsers());
    this.ballotCancel = this.io.timer(this.settings.voteSeconds * 1000, () => this.closeBallot('deadline'));
    this.event(`${this.nameOf(userId)} started a vote for the next game.`, { kind: 'vote' });
    this.io.announce(`🗳 Vote open: what should we play next? Closes in ${this.settings.voteSeconds}s.`, { key: 'vote' });
    this.changed();
  }

  nominate(userId: number, gameId: number): void {
    if (!this.ballot) this.startBallot(userId);
    const last = this.lastNomination.get(userId) ?? 0;
    if (this.now() - last < this.settings.nominationCooldownSeconds * 1000 && this.ballot!.nominations.some((n) => n.nominatedBy.includes(userId))) {
      throw new RoomError('rate_limited', 'You nominated a game recently.');
    }
    const title = this.io.gameTitle(gameId);
    if (!title) throw new RoomError('bad_game', 'That game is not on this group\'s shelf or is not ready.');
    this.ballot!.nominate(userId, gameId, title, this.now());
    this.lastNomination.set(userId, this.now());
    this.changed();
  }

  vote(userId: number, choice: string): void {
    if (!this.ballot) throw new RoomError('no_ballot', 'No vote is open.');
    this.ballot.vote(userId, choice);
    if (this.ballot.allPresentVoted(this.presentUsers()) && this.ballot.ballotCount >= this.ballot.quorum()) this.closeBallot('everyone voted');
    else this.changed();
  }

  private closeBallot(why: string): void {
    const b = this.ballot;
    if (!b) return;
    this.ballotCancel?.();
    this.ballotCancel = null;
    this.ballot = null;
    this.lastBallotClosedAt = this.now();
    const out = b.close();
    this.lastOutcome = out;
    this.io.audit(null, 'vote.close', null, why, { outcome: out });
    if (out.decided === 'switch' && out.gameId !== null) {
      this.beginSwitch(out.gameId, 'vote', null, out.reason);
    } else {
      this.event(`Vote closed: keep playing. ${out.reason}`, { kind: 'vote' });
    }
    this.changed();
  }

  // Host/deputy override: force a switch (with a visible reason) or cancel.
  override(actor: number, gameId: number | null, reason: string): void {
    this.requireModerator(actor);
    if (reason.trim().length < 3) throw new RoomError('bad_reason', 'Give a reason for the override.');
    this.io.audit(actor, gameId === null ? 'vote.cancel' : 'vote.override', gameId, reason);
    if (this.ballot) {
      this.ballotCancel?.();
      this.ballotCancel = null;
      this.ballot.close();
      this.ballot = null;
      this.lastBallotClosedAt = this.now();
    }
    if (gameId === null) {
      if (this.pendingSwitch) {
        this.pendingSwitch.cancelTimer();
        this.pendingSwitch = null;
      }
      this.event(`${this.nameOf(actor)} cancelled the game change: ${reason}`, { kind: 'vote' });
      this.changed();
      return;
    }
    if (!this.io.gameTitle(gameId)) throw new RoomError('bad_game', 'That game is not ready on this shelf.');
    if (this.pendingSwitch) {
      this.pendingSwitch.cancelTimer();
      this.pendingSwitch = null;
    }
    this.beginSwitch(gameId, 'override', actor, reason);
  }

  private beginSwitch(gameId: number, by: 'vote' | 'override', actor: number | null, reason: string): void {
    const title = this.io.gameTitle(gameId) ?? `game ${gameId}`;
    const now = this.now();
    const anyoneSeated = this.seats.some((s) => s.userId !== null);
    const caps = this.game?.adapter?.capabilities;
    let waitingFor = 'now';
    let deadline = now;
    if (anyoneSeated && this.game) {
      if (this.mode === 'versus') {
        waitingFor = caps?.matchResult ? 'the end of the current match' : 'the current match (report the result, or the host can switch now)';
        deadline = now + this.settings.switchTimeoutSeconds * 1000;
      } else if (this.mode === 'coop') {
        waitingFor = caps?.stageBoundary ? 'the next stage boundary' : 'an announced transition';
        deadline = now + (caps?.stageBoundary ? this.settings.switchTimeoutSeconds : 60) * 1000;
      } else {
        waitingFor = 'a handoff point (or the countdown)';
        deadline = now + 60_000;
      }
    }
    const optInCloses = now + this.settings.optInSeconds * 1000;
    const fireAt = Math.max(deadline, optInCloses);
    const cancelTimer = this.io.timer(fireAt - now, () => this.reachedSwitchPoint(deadline > now ? 'countdown finished' : 'ready'));
    this.pendingSwitch = { gameId, title, by, actor, reason, approvedAt: now, deadline, optInCloses, optIn: new Map(), waitingFor, cancelTimer };
    this.event(`Next game: ${title}. Switching at ${waitingFor}. Tap "Play next" to join.`, { kind: 'vote' });
    this.io.announce(`🎮 Next up: ${title}${by === 'override' ? ' (host decision)' : ' (group vote)'}.`, { key: 'switch' });
    this.io.broadcast({ t: 'opt_in', gameId, title, closesAt: optInCloses });
    this.changed();
  }

  optIn(userId: number, play: boolean): void {
    if (!this.pendingSwitch) throw new RoomError('no_switch', 'No game change is pending.');
    this.pendingSwitch.optIn.set(userId, play);
    this.changed();
  }

  // Manual "ready to switch" by a moderator (cannot-detect cases).
  switchNow(actor: number): void {
    this.requireModerator(actor);
    if (!this.pendingSwitch) throw new RoomError('no_switch', 'No game change is pending.');
    this.io.audit(actor, 'switch.now', this.pendingSwitch.gameId, null);
    this.reachedSwitchPoint('moderator');
  }

  private reachedSwitchPoint(why: string): void {
    const ps = this.pendingSwitch;
    if (!ps) return;
    // Opt-in stays open for its full window even if the game reaches a
    // switch point earlier.
    if (this.now() < ps.optInCloses) {
      ps.cancelTimer();
      ps.cancelTimer = this.io.timer(ps.optInCloses - this.now(), () => this.reachedSwitchPoint(why));
      return;
    }
    ps.cancelTimer();
    void this.executeSwitch(ps, why);
  }

  // New seat order: people already waiting keep priority, then current
  // players, then everyone else who opted in (in the order they did).
  seatOrderFor(ps: PendingSwitch): number[] {
    const yes = (u: number) => ps.optIn.get(u) === true && this.members.has(u);
    const order: number[] = [];
    for (const q of this.queue) if (yes(q.userId)) order.push(q.userId);
    for (const s of this.seats) if (s.userId !== null && yes(s.userId) && !order.includes(s.userId)) order.push(s.userId);
    for (const u of this.participants) if (yes(u) && !order.includes(u)) order.push(u);
    for (const [u, v] of ps.optIn) if (v && !order.includes(u) && this.members.has(u)) order.push(u);
    return order;
  }

  private async executeSwitch(ps: PendingSwitch, why: string): Promise<void> {
    if (this.pendingSwitch !== ps) return;
    this.pendingSwitch = null;
    const order = this.seatOrderFor(ps);
    this.event(`Switching to ${ps.title} (${why})…`, { kind: 'vote' });
    // The manager boots and validates the new game before stopping this one;
    // if that fails, the current game, seats and queue are untouched. On
    // success it calls setGame(newGame, order), which seats people in order.
    const r = await this.io.switchGame(ps.gameId, ps.reason, order);
    if (!r.ok) {
      this.event(`Could not start ${ps.title}: ${r.error}. The current game continues.`, { kind: 'warn' });
      this.changed();
      return;
    }
    // setGame() already seated (or enrolled in the turn order) the first
    // people of `order`; everyone else who opted in queues in order, followed
    // by earlier queue members who did not answer the opt-in.
    const placed = new Set<number>([...this.seats.map((s) => s.userId).filter((u): u is number => u !== null), ...this.participants]);
    const rest = order.filter((u) => !placed.has(u));
    const keep = this.queue.filter((q) => !order.includes(q.userId) && ps.optIn.get(q.userId) !== false && !placed.has(q.userId));
    this.queue = [...rest.map((u) => ({ userId: u, since: this.now(), misses: 0 })), ...keep];
    for (const u of this.members.keys()) this.sendYou(u);
    this.fillSeats();
    this.changed();
  }

  // ================================================================ chat

  chat(userId: number, raw: string): ChatMsg | null {
    const m = this.members.get(userId);
    if (!m || m.conns.size === 0) throw new RoomError('not_present', 'Open the room to chat.');
    const until = this.mutes.get(userId) ?? 0;
    if (until > this.now()) throw new RoomError('muted', 'You are muted for now.');
    // Plain text only: strip control characters, collapse whitespace runs.
    const text = raw.replace(/[\u0000-\u0008\u000b-\u001f\u007f​-‏‪-‮⁦-⁩]/g, '').replace(/\s{3,}/g, '  ').trim();
    if (!text) return null;
    if (text.length > MAX_CHAT) throw new RoomError('too_long', `Messages are limited to ${MAX_CHAT} characters.`);
    const now = this.now();
    const win = (this.chatRate.get(userId) ?? []).filter((t) => now - t < this.settings.chatRateWindowSeconds * 1000);
    if (win.length >= this.settings.chatRateCount) throw new RoomError('rate_limited', 'Slow down a little.');
    const last = [...this.chatLog].reverse().find((c) => c.userId === userId);
    if (last && last.text === text && now - last.at < 10_000) throw new RoomError('duplicate', 'You just sent that.');
    win.push(now);
    this.chatRate.set(userId, win);
    const id = this.io.persistChat(userId, text, now) || this.nextChatId++;
    const msg: ChatMsg = { id, userId, name: m.name, text, at: now, frame: this.game?.frame() ?? null };
    this.chatLog.push(msg);
    if (this.chatLog.length > 200) this.chatLog.splice(0, this.chatLog.length - 200);
    this.io.broadcast({ t: 'chat', msg });
    return msg;
  }

  deleteChat(actor: number, id: number, reason: string): void {
    this.requireModerator(actor);
    const idx = this.chatLog.findIndex((c) => c.id === id);
    if (idx < 0) return;
    const [msg] = this.chatLog.splice(idx, 1);
    this.io.deleteChatRow(id, actor);
    this.io.audit(actor, 'chat.delete', msg.userId, reason, { id });
    this.io.broadcast({ t: 'chat_delete', id });
  }

  mute(actor: number, userId: number, minutes: number, reason: string): void {
    this.requireModerator(actor);
    if (userId === this.hostId && !this.canAdminister(actor)) throw new RoomError('forbidden', 'Deputies cannot mute the host.');
    const m = Math.max(1, Math.min(24 * 60, Math.floor(minutes)));
    this.mutes.set(userId, this.now() + m * 60_000);
    this.io.audit(actor, 'chat.mute', userId, reason, { minutes: m });
    this.io.send(userId, { t: 'notice', text: `You are muted in chat for ${m} minutes (${reason}).` });
    this.event(`${this.nameOf(userId)} was muted for ${m} min by ${this.nameOf(actor)}.`, { kind: 'role' });
  }

  // ================================================================ views

  publicSeats(): PublicSeat[] {
    return this.seats.map((s) => {
      const o = this.offers.get(s.port);
      return {
        port: s.port,
        userId: s.userId,
        name: s.userId !== null ? this.nameOf(s.userId) : null,
        state: s.userId === null ? (o ? 'offered' : 'empty') : s.graceUntil !== null ? 'grace' : 'active',
        connected: s.userId !== null && this.connected(s.userId),
        offeredTo: o?.userId ?? null,
        offerExpiresAt: o?.expiresAt ?? null,
        since: s.since,
      };
    });
  }

  publicState(): Record<string, unknown> {
    const b = this.ballot;
    const ps = this.pendingSwitch;
    return {
      groupId: this.groupId,
      game: this.game ? {
        gameId: this.game.gameId, title: this.game.title, sessionId: this.game.sessionId, mode: this.game.mode, ports: this.game.ports,
        adapter: this.game.adapter ? { id: this.game.adapter.id, version: this.game.adapter.version, capabilities: this.game.adapter.capabilities } : null,
        coin: this.game.coinBit !== null,
        status: this.game.status,
      } : null,
      seats: this.publicSeats(),
      queue: this.queue.map((q, i) => ({ userId: q.userId, name: this.nameOf(q.userId), position: i + 1, connected: this.connected(q.userId), misses: q.misses })),
      participants: this.participants.map((u, i) => ({ userId: u, name: this.nameOf(u), turn: i === this.turnIndex })),
      members: [...this.members.values()].filter((m) => m.conns.size > 0).map((m) => ({
        userId: m.userId, name: m.name,
        role: this.seatOf(m.userId) ? 'player' : this.queueIndex(m.userId) >= 0 ? 'queued' : 'spectator',
        host: m.userId === this.hostId, acting: this.isActingHost(m.userId), deputy: this.deputies.has(m.userId), tgAdmin: m.tgRole !== 'member',
      })),
      hostId: this.hostId,
      hostName: this.hostId !== null ? this.nameOf(this.hostId) : null,
      hostPresent: this.hostId !== null && this.connected(this.hostId),
      actingHost: this.actingHost ? { userId: this.actingHost.userId, name: this.nameOf(this.actingHost.userId), kind: this.actingHost.kind } : null,
      deputies: [...this.deputies.entries()].sort((a, b) => a[1].order - b[1].order).map(([u, d]) => ({ userId: u, name: this.nameOf(u), order: d.order, scope: d.scope })),
      ballot: b ? {
        id: b.id, closesAt: b.closesAt, openedBy: this.nameOf(b.openedBy),
        nominations: b.nominations.map((n) => ({ gameId: n.gameId, title: n.title, by: n.nominatedBy.map((u) => this.nameOf(u)) })),
        tally: b.tally(), ballots: b.ballotCount, electorate: b.electorate.size, quorum: b.quorum(),
        rules: 'Quorum: half of the people present (min 2). A switch needs a majority over "Keep playing" and one clear leading game; otherwise the current game continues.',
      } : null,
      lastOutcome: this.lastOutcome,
      pendingSwitch: ps ? { gameId: ps.gameId, title: ps.title, by: ps.by, reason: ps.reason, waitingFor: ps.waitingFor, deadline: ps.deadline, optInCloses: ps.optInCloses, optedIn: [...ps.optIn.entries()].filter(([, v]) => v).map(([u]) => this.nameOf(u)) } : null,
      disputed: this.disputed,
      reports: this.reports.map((r) => ({ name: this.nameOf(r.userId), port: r.port, outcome: r.outcome })),
      settings: this.settings,
    };
  }

  // Personal view for one user.
  you(userId: number): Record<string, unknown> {
    const seat = this.seatOf(userId);
    const offer = this.offerFor(userId);
    const b = this.ballot;
    return {
      t: 'you',
      userId,
      seat: seat ? { port: seat.port, epoch: seat.epoch, controlConn: seat.controlConn, decision: seat.decision } : null,
      queuePosition: this.queueIndex(userId) >= 0 ? this.queueIndex(userId) + 1 : null,
      offer: offer ? { port: offer.port, kind: offer.kind, expiresAt: offer.expiresAt, stuck: offer.stuck } : null,
      participant: this.participants.includes(userId),
      vote: b ? b.choiceOf(userId) : null,
      optIn: this.pendingSwitch?.optIn.get(userId) ?? null,
      roles: {
        host: this.isHost(userId),
        acting: this.isActingHost(userId),
        deputy: this.deputies.has(userId),
        canModerate: this.canModerate(userId),
        canAdminister: this.canAdminister(userId),
        actingHostOffer: this.volunteerOffer?.userId === userId,
      },
      muted: (this.mutes.get(userId) ?? 0) > this.now(),
    };
  }

  private sendYou(userId: number): void {
    if (this.connected(userId)) this.io.send(userId, this.you(userId));
  }

  // ================================================================ persistence

  snapshot(): Record<string, unknown> {
    return {
      v: 1,
      hostId: this.hostId,
      queue: this.queue.map((q) => q.userId),
      seats: this.seats.map((s) => s.userId),
      participants: this.participants,
      turnIndex: this.turnIndex,
      sessionDeputies: [...this.deputies.entries()].filter(([, d]) => d.scope === 'session').map(([u, d]) => ({ u, o: d.order })),
      gameId: this.game?.gameId ?? null,
      settings: this.settings,
    };
  }

  // After a server restart: seats restored as disconnected (grace applies),
  // queue order preserved. Members re-appear as they reconnect.
  restoreAfterGame(snap: any, profiles: Map<number, { name: string; tgRole: TgRole }>): void {
    if (!snap || snap.v !== 1) return;
    const ensure = (u: number) => {
      if (!this.members.has(u)) {
        const p = profiles.get(u);
        this.members.set(u, { userId: u, name: p?.name ?? `user ${u}`, conns: new Set(), lastConn: null, joinedAt: this.now(), presentSince: null, tgRole: p?.tgRole ?? 'member' });
      }
    };
    for (const d of snap.sessionDeputies ?? []) this.deputies.set(d.u, { order: d.o, scope: 'session' });
    (snap.seats as (number | null)[] ?? []).forEach((u, port) => {
      if (u === null || !this.seats[port]) return;
      ensure(u);
      this.assign(port, u, null);
    });
    for (const u of snap.queue ?? []) {
      ensure(u);
      if (!this.seatOf(u) && this.queueIndex(u) < 0) this.queue.push({ userId: u, since: this.now(), misses: 0 });
    }
    if (Array.isArray(snap.participants)) this.participants = snap.participants.filter((u: unknown) => typeof u === 'number');
    if (typeof snap.turnIndex === 'number') this.turnIndex = snap.turnIndex;
    this.changed();
  }

  dispose(): void {
    for (const o of this.offers.values()) o.cancel();
    for (const s of this.seats) {
      s.graceCancel?.();
      s.rotateCancel?.();
    }
    this.ballotCancel?.();
    this.pendingSwitch?.cancelTimer();
    this.reportCancel?.();
    this.hostAwayCancel?.();
    this.volunteerOffer?.cancel();
  }
}

export { KEEP };
