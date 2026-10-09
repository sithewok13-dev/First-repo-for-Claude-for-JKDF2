// Group-private records: matches, scores (with control-ownership
// attribution), control segments and idempotent game events.
//
// Attribution rules:
// * A score earned while one person controlled the seat for the whole run is
//   an "individual" record for that person.
// * If control changed hands during the run, the record is a "seat" score
//   listing every participant with the frames they controlled and the points
//   gained while they did (when the adapter reports scores). Nobody is
//   credited with points earned by someone else.
// * Collaborative mode records are "collaborative", never a solo achievement.
// Records are only compared within the same compat_key (ROM hashes, core
// build, score-affecting options, adapter version) and run flags.

import type { Db } from './db/db.ts';
import type { MatchRecord, ScoreRecord, SegmentRecord } from './room/types.ts';

export class Records {
  private readonly db: Db;

  constructor(db: Db) {
    this.db = db;
  }

  event(groupId: number, sessionId: number, key: string, type: string, frame: number | null, source: string, data: Record<string, unknown>): boolean {
    const r = this.db.run(
      'INSERT OR IGNORE INTO game_events (group_id, session_id, event_key, type, frame, source, data, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
      groupId, sessionId, key, type, frame, source, JSON.stringify(data), Date.now(),
    );
    return r.changes === 1;
  }

  segment(groupId: number, s: SegmentRecord): void {
    this.db.run(
      'INSERT INTO control_segments (session_id, group_id, port, user_id, start_frame, end_frame, start_score, end_score, end_reason) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)',
      s.sessionId, groupId, s.port, s.userId, s.startFrame, s.endFrame, s.startScore, s.endScore, s.endReason,
    );
  }

  private compatKey(sessionId: number): string {
    return this.db.get<{ compat_key: string }>('SELECT compat_key FROM game_sessions WHERE id = ?', sessionId)?.compat_key ?? 'unknown';
  }

  match(groupId: number, m: MatchRecord): boolean {
    const r = this.db.run(
      `INSERT OR IGNORE INTO matches (group_id, game_id, session_id, event_key, p1_user, p2_user, winner_user, result, verification, compat_key, counts, created_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
      groupId, m.gameId, m.sessionId, m.key, m.p1, m.p2, m.winner, m.result, m.verification, this.compatKey(m.sessionId), m.counts ? 1 : 0, Date.now(),
    );
    return r.changes === 1;
  }

  // Segments that overlap [from, to] on a port, oldest first. Includes the
  // still-open segment of the current owner (passed in by the caller).
  private segmentsFor(sessionId: number, port: number, from: number, to: number): { user: number; from: number; to: number; startScore: number | null; endScore: number | null }[] {
    return this.db.all<any>(
      'SELECT user_id, start_frame, end_frame, start_score, end_score FROM control_segments WHERE session_id = ? AND port = ? AND start_frame <= ? AND (end_frame IS NULL OR end_frame >= ?) ORDER BY start_frame',
      sessionId, port, to, from,
    ).map((r) => ({ user: Number(r.user_id), from: Math.max(from, r.start_frame), to: Math.min(to, r.end_frame ?? to), startScore: r.start_score, endScore: r.end_score }));
  }

  score(groupId: number, s: ScoreRecord, run: { fresh: boolean; continues?: number }, currentOwner?: { user: number; from: number; startScore: number | null }): boolean {
    let participants = s.participants;
    let kind = s.kind;
    let userId = s.userId;
    if (kind === 'seat' && s.port !== null) {
      const segs = this.segmentsFor(s.sessionId, s.port, 0, Number.MAX_SAFE_INTEGER);
      if (currentOwner) segs.push({ user: currentOwner.user, from: currentOwner.from, to: Number.MAX_SAFE_INTEGER, startScore: currentOwner.startScore, endScore: s.score });
      participants = segs.map((g) => ({
        user: g.user,
        from: g.from,
        to: g.to,
        points: g.startScore !== null && g.endScore !== null ? Math.max(0, g.endScore - g.startScore) : null,
      }));
      const people = new Set(participants.map((p) => p.user));
      if (people.size === 1) {
        kind = 'individual';
        userId = participants[0].user;
      } else {
        kind = 'seat';
        userId = null;   // shared run: never credited to one person
      }
    }
    const r = this.db.run(
      `INSERT OR IGNORE INTO scores (group_id, game_id, session_id, event_key, kind, user_id, port, participants, score, verification, compat_key, run_flags, created_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
      groupId, s.gameId, s.sessionId, s.key, kind, userId, s.port, JSON.stringify(participants), s.score, s.verification,
      this.compatKey(s.sessionId), JSON.stringify({ fresh: run.fresh, continues: run.continues ?? null }), Date.now(),
    );
    return r.changes === 1;
  }

  // ------------------------------------------------------------ queries (group-scoped)

  highScores(groupId: number, gameId: number, limit = 20): any[] {
    return this.db.all(
      `SELECT s.id, s.score, s.kind, s.user_id, s.participants, s.verification, s.compat_key, s.run_flags, s.created_at,
              u.first_name, u.last_name, u.username
         FROM scores s LEFT JOIN users u ON u.id = s.user_id
        WHERE s.group_id = ? AND s.game_id = ? AND s.voided_at IS NULL
        ORDER BY s.compat_key,
                 COALESCE(json_extract(s.run_flags, '$.fresh'), 1) DESC,
                 COALESCE(json_extract(s.run_flags, '$.continues'), 0) > 0,
                 s.score DESC
        LIMIT ?`,
      groupId, gameId, limit,
    );
  }

  fighterStats(groupId: number, gameId: number): { userId: number; wins: number; losses: number; draws: number; longestStreak: number; compatKey: string }[] {
    const rows = this.db.all<any>(
      `SELECT p1_user, p2_user, winner_user, result, compat_key FROM matches
        WHERE group_id = ? AND game_id = ? AND counts = 1 AND result IN ('win', 'draw') ORDER BY id`,
      groupId, gameId,
    );
    const stats = new Map<string, { userId: number; wins: number; losses: number; draws: number; streak: number; longestStreak: number; compatKey: string }>();
    const get = (u: number, ck: string) => {
      const k = `${ck}:${u}`;
      let s = stats.get(k);
      if (!s) stats.set(k, (s = { userId: u, wins: 0, losses: 0, draws: 0, streak: 0, longestStreak: 0, compatKey: ck }));
      return s;
    };
    for (const r of rows) {
      const ps = [r.p1_user, r.p2_user].filter((u: unknown) => u !== null).map(Number);
      if (ps.length !== 2) continue;
      for (const u of ps) {
        const s = get(u, r.compat_key);
        if (r.result === 'draw') {
          s.draws++;
        } else if (Number(r.winner_user) === u) {
          s.wins++;
          s.streak++;
          s.longestStreak = Math.max(s.longestStreak, s.streak);
        } else {
          s.losses++;
          s.streak = 0;
        }
      }
    }
    return [...stats.values()].map(({ streak: _s, ...rest }) => rest);
  }

  recentMatches(groupId: number, gameId: number | null, limit = 30): any[] {
    return gameId === null
      ? this.db.all('SELECT * FROM matches WHERE group_id = ? ORDER BY id DESC LIMIT ?', groupId, limit)
      : this.db.all('SELECT * FROM matches WHERE group_id = ? AND game_id = ? ORDER BY id DESC LIMIT ?', groupId, gameId, limit);
  }

  // Logged correction of a mistaken or fraudulent record.
  voidScore(groupId: number, scoreId: number, actor: number, reason: string): boolean {
    const r = this.db.run('UPDATE scores SET voided_at = ?, voided_by = ?, void_reason = ? WHERE id = ? AND group_id = ? AND voided_at IS NULL', Date.now(), actor, reason, scoreId, groupId);
    if (r.changes) this.audit(groupId, actor, 'record.void_score', scoreId, reason);
    return r.changes === 1;
  }

  correctMatch(groupId: number, matchId: number, actor: number, winner: number | null, result: 'win' | 'draw' | 'void', reason: string): boolean {
    const r = this.db.run(
      'UPDATE matches SET winner_user = ?, result = ?, counts = ?, verification = ?, corrected_by = ?, correction_reason = ? WHERE id = ? AND group_id = ?',
      winner, result, result === 'void' ? 0 : 1, 'adjudicated', actor, reason, matchId, groupId,
    );
    if (r.changes) this.audit(groupId, actor, 'record.correct_match', matchId, reason, { winner, result });
    return r.changes === 1;
  }

  audit(groupId: number, actor: number | null, action: string, target: number | null, reason: string | null, data: Record<string, unknown> = {}): void {
    this.db.run('INSERT INTO audit_log (group_id, actor_id, action, target_id, reason, data, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)', groupId, actor, action, target, reason, JSON.stringify(data), Date.now());
  }
}
