# Progress log

This is the durable record of what is done, what is staged, and what remains.
The order follows the spec: research, slice, identity and shelf, room rules,
modes and recovery, packaging. Agreed features are never dropped silently.
Anything not yet built is listed under **Staged**, with its reason.

## Stage status (2026-10-09)

| Stage | Status | Evidence |
|---|---|---|
| A. Research and architecture | **Done** | `RESEARCH.md`; 7 reports plus a verification ledger in `research/`; `ARCHITECTURE.md` ADR-1 to ADR-8 |
| B. Playable multiplayer slice | **Done, in headless Chromium** | `test/e2e/slice.test.ts`: 2 players and a spectator, simultaneous input, seat transfer without restart, creator disconnect, touch, latency and spectator delay measured. Real devices: **not yet**. |
| C. Telegram identity, group access, shelf and uploads | **Done (automated)** | e2e `identity`; unit `telegram-*`, `shelf-*`, `zip-safety`. Real Telegram: **not yet**. |
| D. Queue, adapters, voting, roles, chat | **Done (automated)** | unit `room-seats`, `room-governance`, `atc-adapter`; e2e slice |
| E. Modes, records, recovery, limits | **Done (automated)** | unit `room-modes`, `lockstep`, `backup`; e2e `restart` |
| F. Packaging and docs | **Done** | `Dockerfile` (built and booted), `deploy/`, `.env.example`, backup scripts, all docs |
| Pilot on real devices and in a real group | **Not started**. Needs you. | `PILOT_GUIDE.md` §4 |

## Log

### 2026-10-08: research and slice
- Seven research areas were investigated and adversarially verified. Of 90
  claims checked: 64 confirmed, 23 downgraded, 1 refuted, 4 uncheckable.
- Chosen: server-authoritative deterministic lockstep with WebAssembly
  replicas (ADR-1); direct-link Mini App from a URL button (ADR-3).
- Cores built from pinned sources with Emscripten 6.0.12: FBNeo CPS-1/2,
  FBNeo Neo Geo and FCEUmm. They use our deterministic frontend: no clock,
  RNG or time-zone leaks, and the netplay savestate context.
- **Found and fixed an FBNeo determinism bug.** Savestates missed sound
  resampler state, and the YM2610/Delta-T postload overwrote saved values,
  so restored instances drifted.
  - The fix is `native/patches/fbneo-cross-instance-savestates.patch`.
  - Restored instances now stay bit-identical.
- Own MIT test cabinet ROM (ATC) with four modes and a fixed RAM map, plus
  six lawful homebrew titles built from source.
- Vertical slice passes in Chromium.
  - Latency, median input-to-screen: 31 / 77 / 126 ms at 0 / 50 / 100 ms RTT.
  - A spectator trails players by about 0 ms median (p95 about 50 ms) on one
    machine.

### 2026-10-09: product completion and hardening
- Identity and group isolation end to end:
  - signed launch data;
  - membership checked with Telegram;
  - group-bound tokens;
  - revocation closes live connections;
  - one-time browser handoff;
  - dev login refused in production.
- Shelf and uploads:
  - from-scratch zip parser that matches minizip;
  - romset identification against the core's own driver data;
  - BIOS and parent dependencies;
  - isolated boot test;
  - quotas, dedup, pinning, retention.
  - **Adversarial review fixed 11 defects.** The worst: the validated
    archive could differ from the one the emulator opens, and an early
    disconnect hung uploads.
- Telegram bot:
  - polling or webhook;
  - group lifecycle and migration;
  - uploads from the chat;
  - announcements with rate limits;
  - commands.
  - **Adversarial review fixed 14 defects.** The worst: a timer error could
    crash the server, and upload cards could be lost.
- Live role changes: a demoted Telegram admin loses moderator powers at once.
- Recovery:
  - after a crash, open sessions are closed as `crash` at startup, so they
    never count as running;
  - graceful and crash restarts offer Resume from the right checkpoint, used
    once;
  - no phantom seats.
- **Coin banking race fixed.** Two quick coin requests could both be
  accepted before the adapter saw the first. Found by the review's parallel
  test runs.
- Records:
  - Continues used in each run are tracked.
  - High scores are ranked only within their category (build, fresh vs
    resumed, without vs with continues), with headings in the Records tab.
- Web uploads: one in flight per user, and 30 per group per hour.
- Test bot tokens are now obviously fake strings, so secret scanners do not
  flag them.
- Backups: online snapshot plus blobs plus checkpoints with a SHA-256
  manifest, hard-linked increments, verified restore that never deletes, and
  retention.
- Docker image builds the cores from source. Those cores pass every
  determinism test. The image boots healthy with a read-only root
  filesystem, and Compose and Caddy configs validate.
- Client:
  - WebAssembly-unavailable banner (Lockdown Mode);
  - slow-device warning with measured emulation cost;
  - cached game files viewable and clearable;
  - licence and source links.
- Pinned two more FBNeo options (force-60hz, FM interpolation). Caddy is set
  to keep WebSockets open across config reloads.

## Staged (agreed, not yet built)

| Item | Spec | Why staged | Plan |
|---|---|---|---|
| Real-device verification (iOS, Android, Desktop, Web), controllers, audio | §1, §15 | needs your devices and account | `PILOT_GUIDE.md` §4 |
| Gameplay with real commercial romsets | §6, §8 | needs your lawful ROMs | pilot test 15 |
| Verified adapters for commercial games (automatic results, game over, scores) | §8–§12 | need the exact ROMs to verify against; until then manual results and labelled controls apply | write per ROM hash after the pilot |
| Per-game touch layouts | §15 | one saved layout per user exists (movable, resizable); per-game variants not yet | small UI addition |
| Client-side prediction (rollback) for seated players | ADR-1 | delay-based lockstep first, as planned; rollback depth must adapt to device CPU | after device measurements |
| Unordered datagram transport (WebTransport/DataChannel) for player input | research | WebSocket works everywhere; this is a tail-latency optimization | after the pilot |
| Validator sandboxing (separate uid/container, no network, memory cgroup) | §6 | processes are already separate, with no secrets and with time and heap limits; OS-level isolation is a deployment hardening step | Compose sidecar |
| Rate limit for uploads arriving through the Telegram chat | §6 | web uploads are capped (1 in flight per user, 30 per group per hour); chat uploads are paced by Telegram and the 20 MB limit, but have no cap of their own | share the HTTP cap with the bot |
| Operator command to purge a group's data | §6, §21 | removal is soft, with retention; a full purge is manual SQL plus cleanup | admin script |
| Orphan blob sweep (crash between file rename and DB insert) | §6 | tiny window; harmless except disk use | filesystem sweep in `cleanup()` |
| Re-identification of `needs_attention` uploads after a core update | §6 | rare | include them in `revalidate()` |
| Local Bot API server profile | §6 | configured but **untested**; the web upload covers large files | test in the pilot if wanted |
| JavaScriptCore (iOS) determinism | ADR-1 | cannot be tested without a device; hash checks and resync are the safety net | pilot test 3 |

## Known issues

- An end-to-end restart test once hung until the outer 300 s timeout. That
  was 1 of 11 runs, seen before per-test timeouts were added. It has not
  recurred in 10 runs since. If it reappears, the 90 s per-test timeout will
  show where it stopped.
- The FBNeo performance numbers were measured with synthetic romsets, whose
  CPUs run garbage code. Real games may cost more. Re-measure with
  `node scripts/bench-cores.ts` on your ROMs.
- The CPS and NES cores built in Docker differ by a few bytes from older
  local builds, which used earlier link flags; the Neo Geo core is
  identical. The Docker build is the reference. Its cores pass the full
  determinism suite.

## Decisions waiting on you

1. **Licensing.** Publish `native/` (FBNeo patch plus frontend) publicly
   and choose a licence for the frontend (GPL-2.0-compatible). See
   `LICENSES.md`.
2. **Hosting.** Choose a Mac mini with Cloudflare Tunnel (≈ $3–5/month) or
   a VPS (≈ $12–26/month). Nothing has been bought. See `COSTS.md`.
3. **Pilot.** Create the bot, add it to a test group, and run the device
   session in `PILOT_GUIDE.md`.
