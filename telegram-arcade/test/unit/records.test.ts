// Records against the real schema: scores are ranked only within their
// category (build/compat key, fresh vs resumed, with vs without continues),
// duplicates after recovery are ignored, shared runs are never credited to
// one person, and records never cross groups.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Db } from '../../server/db/db.ts';
import { Records } from '../../server/records.ts';
import type { ScoreRecord } from '../../server/room/types.ts';

function setup() {
  const db = new Db(':memory:');
  db.migrate();
  const now = Date.now();
  for (const [id, chat] of [[1, -100], [2, -200]]) {
    db.run('INSERT INTO groups (id, chat_id, title, room_token, quota_bytes, created_at, updated_at) VALUES (?, ?, ?, ?, 1, ?, ?)', id, chat, `G${id}`, `tok${id}`, now, now);
  }
  db.run("INSERT INTO blobs (sha256, size, created_at) VALUES ('aa', 1, 0)");
  db.run("INSERT INTO games (id, group_id, blob_sha256, file_name, display_name, status, uploaded_at) VALUES (1, 1, 'aa', 'g.nes', 'Game', 'ready', 0)");
  db.run("INSERT INTO games (id, group_id, blob_sha256, file_name, display_name, status, uploaded_at) VALUES (2, 2, 'aa', 'g.nes', 'Game', 'ready', 0)");
  for (const [sid, g, gid, ck] of [[11, 1, 1, 'buildA'], [12, 1, 1, 'buildA'], [13, 1, 1, 'buildB'], [21, 2, 2, 'buildA']] as const) {
    db.run('INSERT INTO game_sessions (id, group_id, game_id, compat_key, fresh, started_at) VALUES (?, ?, ?, ?, 1, 0)', sid, g, gid, ck);
  }
  for (const u of [1, 2, 3]) db.run('INSERT INTO users (id, first_name, updated_at) VALUES (?, ?, 0)', u, `U${u}`);
  return { db, rec: new Records(db) };
}

const run = (key: string, sessionId: number, score: number, userId: number, gameId = 1): ScoreRecord => ({
  key, sessionId, gameId, port: 0, score, kind: 'individual', userId, participants: [{ user: userId, from: 0, to: 100, points: score }], verification: 'verified', continues: null,
});

test('high scores are ranked within categories: build, fresh vs resumed, no continues vs continues', () => {
  const { rec } = setup();
  rec.score(1, run('a', 11, 900, 1), { fresh: true, continues: 0 });
  rec.score(1, run('b', 11, 5000, 2), { fresh: true, continues: 3 });   // higher, but used continues
  rec.score(1, run('c', 12, 7000, 3), { fresh: false, continues: 0 });  // resumed run
  rec.score(1, run('d', 11, 1200, 3), { fresh: true, continues: 0 });
  rec.score(1, run('e', 13, 9999, 1), { fresh: true, continues: 0 });   // different build
  const rows = rec.highScores(1, 1).map((r) => [r.compat_key, JSON.parse(r.run_flags).fresh, JSON.parse(r.run_flags).continues, r.score]);
  assert.deepEqual(rows, [
    ['buildA', true, 0, 1200],
    ['buildA', true, 0, 900],
    ['buildA', true, 3, 5000],
    ['buildA', false, 0, 7000],
    ['buildB', true, 0, 9999],
  ]);
});

test('a score recorded again after recovery is ignored; records stay in their group', () => {
  const { rec } = setup();
  assert.equal(rec.score(1, run('same', 11, 900, 1), { fresh: true }), true);
  assert.equal(rec.score(1, run('same', 11, 900, 1), { fresh: true }), false, 'same event key: no duplicate');
  rec.score(2, run('other', 21, 4000, 2, 2), { fresh: true });
  assert.deepEqual(rec.highScores(1, 1).map((r) => r.score), [900]);
  assert.deepEqual(rec.highScores(1, 2), [], 'group 1 cannot read group 2\'s game records');
});

test('a shared run is a seat score listing each participant, never one person\'s record', () => {
  const { db, rec } = setup();
  rec.segment(1, { sessionId: 11, port: 0, userId: 1, startFrame: 0, endFrame: 500, startScore: 0, endScore: 300, endReason: 'handoff' });
  rec.score(1, { ...run('shared', 11, 1000, 2), kind: 'seat', userId: 2, participants: [] }, { fresh: true }, { user: 2, from: 500, startScore: 300 });
  const r = db.get<any>("SELECT * FROM scores WHERE event_key = 'shared'");
  assert.equal(r.kind, 'seat');
  assert.equal(r.user_id, null);
  assert.deepEqual(JSON.parse(r.participants).map((p: any) => [p.user, p.points]), [[1, 300], [2, 700]]);
});
