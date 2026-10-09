# Testing

What is tested, how to reproduce it, the measured results, and what is
**not** tested. "Passed" below always means a real run in this repository.
The environment and date are given in §3.

## 1. How to run

```sh
npm ci
npm run typecheck
npm test            # unit suites (Node test runner, one file at a time)
npm run test:e2e    # real server + headless Chromium (Playwright); writes test-results/
```

Requirements:
- **Cores in `native/build/`**, from `npm run build:cores` or copied from the
  Docker image. Needed by the core, lockstep, shelf and e2e tests.
- **Chromium** for the e2e slice. Set `PLAYWRIGHT_BROWSERS_PATH` or use
  `/opt/pw-browsers/chromium`.
- **Homebrew ROMs** (optional): `native/testroms/fetch-homebrew.sh`, which
  needs cc65 and Python with Pillow. The homebrew tests are skipped without
  them.
- `CORE_DIR=/path` runs the core tests against another core build. This is
  how the Docker-built cores were verified.

## 2. Coverage of the required tests

| Required test (spec) | Where | What it proves |
|---|---|---|
| Concurrent seat claims and queue fairness | `unit/room-seats` | exactly one winner per seat; no queue jumping; offers go to the queue head; unresponsive candidates are skipped without stalling; one seat or queue entry per person |
| Former-player input rejection | `unit/lockstep`, `unit/room-seats`, `e2e/slice` | stale epochs are rejected by the session; spectators never drive a port; the previous owner's input is dropped after a handoff |
| Group isolation and role authorization | `e2e/identity`, `unit/room-governance`, `unit/telegram-membership` | signed launch data only; non-members refused (forwarded links); tokens bound to one group (shelf, files, records, room); revocation; deputies cannot administer; live role changes |
| Vote outcomes and failed transitions | `unit/room-governance` | quorum and majority over Keep; ties keep; switch waits for a safe point; a failed launch keeps the room intact; override needs a reason |
| Host/deputy succession | `unit/room-governance`, `e2e/slice` | 60 s grace, then designated deputy, then group admin, then volunteer offer; host return; real disconnect in the browser |
| Match/turn/game-over deduplication | `unit/room-modes`, `unit/telegram-bot` | the same adapter event twice never rotates or records twice; Telegram redeliveries are processed once |
| Shared-score attribution | `unit/room-modes` | the run score is recorded once, with the open control segment, so replacements do not get the whole score |
| Malicious uploads | `unit/zip-safety`, `unit/shelf-ingest`, `unit/shelf-upload-http` | zip bombs, entry count, declared-size lies, traversal and control-character names, encryption, unknown methods, truncation, CRC errors, nesting, overlap, duplicates, hidden second archive (minizip differential), oversized and aborted bodies, validator timeout, forged validator replies |
| Disconnect and crash recovery | `unit/room-seats`, `unit/lockstep`, `e2e/restart`, `e2e/slice` | buttons released and seat held in grace; emulator worker killed, restored from a checkpoint and replayed; server restart (graceful and crash) resumes once with no phantom seats or duplicate sessions |
| *also:* determinism of every core | `unit/fbneo-determinism`, `unit/homebrew-compat`, `unit/lockstep` | separate boots agree; snapshot restore converges; FBNeo macro, diagnostic and service inputs masked |
| *also:* adapters against the real ROM | `unit/atc-adapter` | versus, co-op 4P, turns and solo events from the real ATC ROM in the real core |
| *also:* backups | `unit/backup` | online backup of a live server restores into a working server; damage detected; retention |
| *also:* Telegram bot behaviour (fake Bot API) | `unit/telegram-*` | webhook secret, idempotency, group lifecycle and migration, uploads, rate limits, shutdown |

## 3. Results

Run on 2026-10-09 in a Linux container: Intel(R) Xeon(R) Processor @ 2.80GHz (4 cores), Node v22.22.0, Chromium 141.0.7390.37 (headless).

| Suite | Tests | Passed | Failed | Skipped |
|---|---|---|---|---|
| Type check (`tsc --noEmit`, strict) | — | clean | — | — |
| Unit (`npm test`, 16 files) | 155 | 155 | 0 | 0 |
| End-to-end, run 1 (`npm run test:e2e`) | 9 | 9 | 0 | 0 |
| End-to-end, run 2 | 9 | 9 | 0 | 0 |

The core tests (FBNeo determinism, lockstep, ATC adapters, homebrew) were also run against the cores built inside the Docker image (`CORE_DIR=...`): 18/18 passed.

### Gameplay slice (`test/e2e/slice.test.ts`, run 2; `test-results/slice-report.json`)

Two players (Ana, Ben) and a spectator (Cy) in three separate browser contexts against the real server, playing the ATC versus ROM:
- all three run the same session; P1 and P2 move at the same time and every page agrees on the positions (RAM) — **0 desyncs**, periodic hash checks matched;
- coins are server-controlled: pressing the local SELECT key inserts nothing, a credit cannot be banked, a verified match result is recorded (`win`, verification `verified`);
- the spectator takes over P2's seat **without restarting** (frames continue, P1's score kept) and plays with **multi-touch** (CDP touch events);
- the creator/host disconnects: the game keeps running (92 frames advanced while checked), the seat is held then freed, the interrupted match is recorded as not counted, and the group admin becomes acting host.

**Latency** — input pressed → first rendered frame containing it, with a TCP proxy adding one-way delay (both directions):

| Added delay | Median ms | p95 ms | Input round trip median ms | Samples | Desyncs |
|---|---|---|---|---|---|
| 0 ms (0 ms RTT) | 28.7 | 47.9 | 13.8 | 80 | 0 |
| 25 ms (50 ms RTT) | 77.2 | 97.2 | 61.3 | 80 | 0 |
| 50 ms (100 ms RTT) | 125.9 | 140 | 110.4 | 80 | 0 |

Run 1 for comparison:

| Added delay | Median ms | p95 ms | Input round trip median ms | Samples | Desyncs |
|---|---|---|---|---|---|
| 0 ms (0 ms RTT) | 31.5 | 47.8 | 15.2 | 80 | 0 |
| 25 ms (50 ms RTT) | 75.4 | 92.1 | 60 | 80 | 0 |
| 50 ms (100 ms RTT) | 126.6 | 147.1 | 112.9 | 80 | 0 |

**Spectator delay** vs a player's view of the same frame: median 0.2 ms, p95 50.4 ms (n = 586; run 1: median 0.2 ms, p95 50.5 ms). Same machine, no added network delay; spectators buffer 4 frames vs 1 for players, so on real networks expect spectators ~50–70 ms behind players.

**Server timing:** 9137 ticks, 11 late (max 15 ms), 0 worker restarts, 285 inputs accepted, 0 rejected.

### Emulation cost (`scripts/bench-cores.ts`; `test-results/bench.json`)

| Content | Core | Server ms/frame | Browser-equivalent ms/frame | State size |
|---|---|---|---|---|
| ATC co-op (NES, lawful) | fceumm | 0.473 | 0.571 | 13 KB |
| sf2 (synthetic romset) | fbneo_cps12 | 1.636 | 1.856 | 263 KB |
| ssf2t (synthetic romset) | fbneo_cps12 | 2.054 | 2.187 | 349 KB |
| kof98 (synthetic romset) | fbneo_neogeo | 4.317 | 2.499 | 407 KB |

Measured on Intel(R) Xeon(R) Processor @ 2.80GHz, Node v22.22.0. FBNeo rows use synthetic romsets whose CPUs execute random data: indicative only.

### Docker image

`docker build` (cores compiled from pinned sources in `emscripten/emsdk:6.0.12`) succeeded; the container started with `--read-only --cap-drop ALL --security-opt no-new-privileges`, reported `healthy`, served the Mini App, cores and licence files, and refused path traversal attempts. `docker compose config` (all profiles) and `caddy validate` passed.

### Flakiness record

- `test/e2e/slice.test.ts` failed 2 of 5 runs under parallel load during review, at "credit banking refused": a real race in the coin policy (a second coin accepted before the adapter reported the first). Fixed in `server/room/room.ts` (a coin counts until the adapter reports a later frame) with a deterministic unit test; 5 consecutive full e2e runs passed afterwards.
- `test/e2e/restart.test.ts` hung once (1 of 11 runs) before per-test timeouts were added; not reproduced in 10 further runs.


## 4. Not tested (and why)

| Not tested | Why | How to close it |
|---|---|---|
| Any real Telegram client (iOS, Android, Desktop, Web) | no devices or account in the build environment | `PILOT_GUIDE.md` §4 |
| Gamepads | no hardware; the Gamepad API cannot be simulated meaningfully in headless Chromium | pilot test 6 |
| Audio output | headless Chromium runs muted | pilot test 7 |
| JavaScriptCore (iOS) determinism | no iOS runtime | pilot tests 3–4; the hash check and resync is the safety net |
| Commercial romsets | none available lawfully; FBNeo was tested with synthetic CRC-matched sets (random data) | pilot test 15 |
| Real Telegram Bot API | no bot token | pilot §1–§3; the bot was tested against a fake Bot API that enforces documented limits |
| Local Bot API server | needs api_id and api_hash | optional |
| Public HTTPS deployment, Caddy certificates, Cloudflare Tunnel | no domain or account | `OPERATIONS.md` §2 |
| Weak phones (CPU) | no devices | pilot; the app measures and warns |
| Long soak (hours) and many rooms at once | time | run a pilot evening and watch `docker stats` |
