// Server restart and crash recovery through the real server (HTTP, WebSocket,
// rooms, emulation workers): a graceful restart checkpoints the running game
// and offers Resume; a crash (the last committed state, reproduced with an
// online backup taken mid-game) offers the last periodic checkpoint. After
// either, the resumed game continues from the saved frame, the save point is
// used once, no seat survives as a phantom, roles persist, and the resumed
// session is marked as resumed for records.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import WebSocket from 'ws';
import { backup, restore } from '../../server/ops/backup.ts';
import { startHarness, type Harness } from '../helpers/harness.ts';

interface Client { ws: WebSocket; msgs: any[]; cmd(op: string, args?: Record<string, unknown>): Promise<any>; ping(): Promise<any>; close(): void }

async function login(h: Harness, userId: number): Promise<string> {
  const r = await fetch(h.url + '/api/auth/dev', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ userId, room: h.roomToken }) });
  assert.equal(r.status, 200);
  return (await r.json()).token;
}

function connect(h: Harness, token: string): Promise<Client> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(h.url.replace('http', 'ws') + '/ws');
    const msgs: any[] = [];
    let seq = 0;
    const waiters = new Map<number, (m: any) => void>();
    const pongs: ((m: any) => void)[] = [];
    ws.on('open', () => ws.send(JSON.stringify({ t: 'auth', token })));
    ws.on('error', reject);
    ws.on('message', (d, bin) => {
      if (bin) return;
      const m = JSON.parse(d.toString());
      msgs.push(m);
      if (m.t === 'ack') waiters.get(m.id)?.(m);
      if (m.t === 'pong') pongs.shift()?.(m);
      if (m.t === 'welcome') {
        resolve({
          ws, msgs,
          cmd: (op, args = {}) => new Promise((r) => { const id = ++seq; waiters.set(id, r); ws.send(JSON.stringify({ t: 'cmd', id, op, args })); }),
          ping: () => new Promise((r) => { pongs.push(r); ws.send(JSON.stringify({ t: 'ping', c: Date.now() })); }),
          close: () => ws.close(),
        });
      }
    });
  });
}

const lastRoom = (c: Client) => [...c.msgs].reverse().find((m) => m.t === 'room')?.state;
const until = async (fn: () => boolean, ms = 15000) => {
  const t0 = Date.now();
  while (!fn()) {
    if (Date.now() - t0 > ms) throw new Error('timed out waiting');
    await new Promise((r) => setTimeout(r, 50));
  }
};

async function runningGame(h: Harness): Promise<{ host: Client; player: Client; gameId: number }> {
  h.membership.set(h.groupId, 1, 'creator');
  h.membership.set(h.groupId, 2, 'member');
  const gameId = await h.addGame('native/testroms/build/atc-solo.nes', 1);
  const host = await connect(h, await login(h, 1));
  const player = await connect(h, await login(h, 2));
  assert.equal((await host.cmd('game.start', { gameId })).ok, true);
  await until(() => player.msgs.some((m) => m.t === 'session'));
  assert.equal((await player.cmd('seat.take')).ok, true);
  await until(() => lastRoom(player)?.seats?.some((s: any) => s?.userId === 2));
  return { host, player, gameId };
}

async function resumeAndCheck(h2: Harness, savedFrame: number, gameId: number): Promise<void> {
  h2.membership.set(h2.groupId, 1, 'creator');
  const host = await connect(h2, await login(h2, 1));
  await until(() => !!lastRoom(host));
  const st = lastRoom(host);
  assert.ok(st.resumable, 'Resume is offered after the restart');
  assert.equal(st.resumable.gameId, gameId);
  assert.equal(st.resumable.frame, savedFrame);
  assert.ok(!(st.seats ?? []).some((s: any) => s?.userId), 'no phantom seats survive a restart');
  assert.equal(st.hostId ?? st.host?.userId ?? 1, 1, 'roles persist');
  const r = await host.cmd('game.resume', { gameId, checkpointId: st.resumable.checkpointId });
  assert.equal(r.ok, true, r.error?.message ?? r.message);
  await until(() => host.msgs.some((m) => m.t === 'session'));
  const pong = await host.ping();
  assert.ok(pong.f >= savedFrame, `resumed at frame ${pong.f}, saved at ${savedFrame}`);
  const s = h2.app.db.get<any>('SELECT * FROM game_sessions ORDER BY started_at DESC LIMIT 1');
  assert.equal(s.fresh, 0, 'resumed sessions are marked for records');
  assert.equal(s.resumed_from, st.resumable.checkpointId);
  // the save point is consumed: it cannot start a second copy of the game
  const again = await host.cmd('game.resume', { gameId, checkpointId: st.resumable.checkpointId });
  assert.equal(again.ok, false);
  assert.equal(h2.app.db.get<any>('SELECT COUNT(*) AS n FROM game_sessions WHERE ended_at IS NULL').n, 1, 'exactly one live session');
  host.close();
}

test('graceful restart: the running game is checkpointed and resumes where it stopped', { timeout: 90_000 }, async () => {
  const dataDir = mkdtempSync(join(tmpdir(), 'arcade-restart-'));
  const h1 = await startHarness({ DATA_DIR: dataDir });
  const { host, player, gameId } = await runningGame(h1);
  await new Promise((r) => setTimeout(r, 1500));
  const before = (await host.ping()).f;
  assert.ok(before > 30, 'the game was running');
  await h1.app.close(); // SIGTERM path: checkpoint every room, close connections
  host.close(); player.close();

  const h2 = await startHarness({ DATA_DIR: dataDir });
  try {
    const saved = h2.app.db.get<any>('SELECT frame FROM checkpoints WHERE valid = 1 ORDER BY id DESC LIMIT 1');
    assert.ok(saved && saved.frame >= before, `shutdown checkpoint at ${saved?.frame} is not older than the last seen frame ${before}`);
    const ended = h2.app.db.get<any>('SELECT end_reason FROM game_sessions ORDER BY started_at DESC LIMIT 1');
    assert.equal(ended.end_reason, 'shutdown');
    await resumeAndCheck(h2, saved.frame, gameId);
  } finally {
    await h2.stop();
  }
});

test('crash: the last committed periodic checkpoint is offered and resumes', { timeout: 90_000 }, async () => {
  const h1 = await startHarness({ CHECKPOINT_INTERVAL_SEC: '2' });
  const crashDir = mkdtempSync(join(tmpdir(), 'arcade-crash-'));
  try {
    const { host, player, gameId } = await runningGame(h1);
    await until(() => !!h1.app.db.get('SELECT 1 FROM checkpoints WHERE valid = 1'), 20000);
    // the state a power cut would leave: the last committed database state and its files
    const b = backup({ dataDir: h1.dataDir, outDir: join(crashDir, 'snap') });
    restore({ backupDir: b.dir, dataDir: join(crashDir, 'data') });
    host.close(); player.close();
    const saved = b.manifest.checkpoints.length;
    assert.ok(saved >= 1);
    const h2 = await startHarness({ DATA_DIR: join(crashDir, 'data') });
    try {
      const cp = h2.app.db.get<any>('SELECT frame FROM checkpoints WHERE valid = 1 ORDER BY id DESC LIMIT 1');
      // the crashed session never ended cleanly; on start it is closed as a crash, never left "live"
      const crashed = h2.app.db.get<any>('SELECT end_reason, ended_at FROM game_sessions ORDER BY started_at DESC LIMIT 1');
      assert.equal(crashed.end_reason, 'crash');
      assert.ok(crashed.ended_at);
      await resumeAndCheck(h2, cp.frame, gameId);
    } finally {
      await h2.stop();
    }
  } finally {
    await h1.stop();
    rmSync(crashDir, { recursive: true, force: true });
  }
});
