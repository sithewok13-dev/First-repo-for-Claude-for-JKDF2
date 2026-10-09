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

RESULTS_PLACEHOLDER

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
