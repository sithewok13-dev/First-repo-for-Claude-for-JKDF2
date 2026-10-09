# Architecture decisions

Status labels used below: **VERIFIED** (primary source read, or measured in
this repository by a test that is checked in), **REPORTED** (secondary
source), **ASSUMED** (inference that still needs a device or production
test). Full research reports with line-level citations are summarized in
`RESEARCH.md`.

## ADR-1 — Server-authoritative deterministic lockstep, not video streaming

**Decision.** The server runs the one authoritative instance of each room's
game: a WebAssembly libretro core inside a separate worker process. Every
emulated frame the server freezes one *authoritative input record* (a 16-bit
controller mask per port, taken from whoever currently owns each seat),
appends it to a replay log, feeds it to its own worker and broadcasts it.
Each player's and spectator's browser runs the **same .wasm build** as a
replica and advances it only with those records. Replicas verify themselves
against server state hashes every 2 s (120 frames) and reload a snapshot from
the server if they ever differ. Clients only ever send their own controller
mask; they cannot change game state.

So "players share the same running game" holds in the strict sense: there is
one authoritative game on the server; every screen is a verified replica of
it.

| Criterion | (a) Server emulation + video stream | (b) Authoritative lockstep with replicas (chosen) |
|---|---|---|
| Added input latency | Cloud gaming: about +40–67 ms over local in most reports, +27 ms to +216 ms across setups (REPORTED). Model: RTT + 15–40 ms (ESTIMATE) | **Measured here: RTT + 16–31 ms** median (0/50/100 ms RTT → 31/77/126 ms input→rendered frame; VERIFIED `test/e2e/slice.test.ts`) |
| Works in every Telegram client | No WebRTC in Telegram Desktop on Linux (WebKitGTK; VERIFIED in client source). Needs TURN for NATs | WebSocket + WebAssembly only — available in all clients (VERIFIED engines; device tests pending) |
| Bandwidth per viewer | 0.36–3.1 Mbps for 320×224@60 H.264 (MEASURED by research agent) | ~12–46 kbps of input records; snapshots only on join/resync (NES state 13.7 KB raw, CPS-2 ~270–360 KB). **Plus a one-time download of the game files per device**: CPS-2 25–46 MB, Neo Geo up to ~93 MB, plus ~3 MB core. Cached in IndexedDB and clearable by the user. |
| Spectators | Each one is another encoded stream | Cheap: same input stream, larger buffer; measured view lag vs players median ≈ 0 ms, p95 ≈ 50 ms on one machine |
| Picture | Compression artifacts on hit flashes | Pixel-perfect native output |
| Seat handoff | Remap input on server | Remap input on server (same) |
| Persistence | Server savestate | Server savestate (same) |
| Main risk | Latency, egress cost, encoder CPU, TURN | Client CPU on weak Android phones (single-threaded WASM; the app measures and warns); determinism (tested in V8; JavaScriptCore pending); every viewer holds a copy of the game files; Apple Lockdown Mode disables WebAssembly entirely |
| Pilot cost | 2–10× higher (egress, TURN) | ≈ $0–12/month on small hosts (see `COSTS.md`) |

Honest caveat (REPORTED by the netcode research): delay-based lockstep is not
dramatically lower latency than a well-tuned stream — both pay one round trip
— but it avoids encode/decode/jitter buffers, works without WebRTC and keeps
bandwidth tiny. It also allows adding client-side prediction (rollback) for
seated players later, which streaming cannot.

**Determinism work (VERIFIED by tests).**
* `native/frontend/fe.cpp` pins every host input to the emulation: wall-clock
  time, `rand()`, time zones and `mktime` are functions of the session frame
  counter; the wasm imports contain no clock or random sources.
* FBNeo's netgame mode is forced before load (fixed RTC date, fixed RNG seed,
  hiscore persistence off).
* **FBNeo savestates were not complete across instances.** Sound-chip glue
  (YM2151, YM2610, MSM6295, QSound) kept resampler state outside the
  savestate and the YM2610/Delta-T postload overwrote saved values, so a
  freshly restored instance drifted. Found by
  `test/unit/fbneo-determinism.test.ts`; fixed by
  `native/patches/fbneo-cross-instance-savestates.patch`. After the patch,
  CPS-1, CPS-2 and Neo Geo instances restored mid-run stay bit-identical for
  40 s of emulated time (synthetic romsets, no copyrighted content).
* Lockstep with the real NES core: two replicas (one joining mid-game) stay
  identical with the authoritative worker; a killed worker is restored from a
  checkpoint and replays the log without clients noticing
  (`test/unit/lockstep.test.ts`).

## ADR-2 — Hosting: one small cloud VM by default; Mac mini supported

**Decision.** Default pilot deployment is Docker Compose (app + Caddy for
automatic HTTPS) on one small VM near the friends (e.g. Hetzner CX22/CX23/CX33
class, 2–4 vCPU, 4–8 GB RAM). The same compose file runs on a Mac mini at
home behind Cloudflare Tunnel (free) when the owner prefers zero hosting cost.

Why: lockstep traffic is tiny, so bandwidth never decides; what matters is an
always-on public HTTPS endpoint (required for Mini Apps) with stable latency.
A VM gives that without home-network/power risk; a Mac mini is cheaper but
adds tunnel hops and depends on home uptime. A hybrid only pays off for video
streaming, which we do not use. Costs in `COSTS.md` (no purchases made).

Capacity (pilot): each active room costs one emulation worker. Measured
single-core cost on the test machine, a 2.8 GHz Xeon (`scripts/bench-cores.ts`):

| System | Server ms/frame | Client ms/frame |
|---|---|---|
| NES | 0.47 | 0.54 |
| CPS-1 (`sf2`) | 1.6 | 1.8 |
| CPS-2 (`ssf2t`) | 1.9 | 2.1 |
| Neo Geo (`kof98`) | 4.7 | 2.4 |

That is 3–28% of one core per room at 60 fps. The FBNeo figures use
synthetic romsets, whose CPUs run garbage code, so real games may differ.
Default limits are `MAX_ACTIVE_ROOMS=3` and `MAX_VIEWERS_PER_ROOM=16`.

## ADR-3 — Telegram presentation: direct-link Mini App launched from a URL button

VERIFIED from the Bot API spec and all four client codebases:
* Inline `web_app` and reply-keyboard `web_app` buttons are **private-chat
  only**; the menu button is private-chat only; the attachment menu is not
  available to normal bots.
* In groups, a bot posts an inline keyboard **URL button** pointing to the
  direct link `https://t.me/<bot>/<app>?startapp=<room_token>`. All clients
  open it as a Mini App *in the context of that group*.
* Such a launch's `initData` contains `user`, `start_param`, `chat_type`,
  `chat_instance`, but **not the group's chat id**. Membership is therefore
  verified server-side with `getChatMember(group, user)` on every sign-in and
  re-verified on WebSocket connect and on `chat_member` updates. A forwarded
  link opens the Mini App but the server refuses non-members.
* Telegram Desktop shows an "Open app" confirmation for unverified bots each
  time (VERIFIED in tdesktop source; behaviour accepted).
* `Telegram.WebApp.requestFullscreen()` (Bot API 8.0) is the only fullscreen
  path on mobile (the HTML Fullscreen API is disabled in Telegram's mobile
  webviews). `lockOrientation()` locks the *current* orientation.
  `disableVerticalSwipes()` (7.7) stops swipe-to-close during play.
* Fallback: "Open in browser" creates a one-time, 60 s, single-use link that
  signs the same verified Telegram user into a normal browser session (for
  example if a controller is not detected inside a Telegram webview).

## ADR-4 — Emulator cores and licensing

* Arcade: **FBNeo** (CPS-1/CPS-2 subset and Neo Geo subset, built separately
  to keep downloads ~6 MB each). Non-commercial license: no monetization, no
  donation requests, license text shipped, source changes published (our patch
  is in this repository). See `LICENSES.md`.
* Console: **FCEUmm** (NES, GPL-2.0) — also runs our lawful ATC test ROMs.
* Not chosen now: Genesis Plus GX / PicoDrive / snes9x (non-commercial),
  MAME (heavy), EmulatorJS (its netplay streams host video, VERIFIED).
* Cores are built from pinned upstream commits with Emscripten 6.0.12,
  single-threaded (Android WebView cannot use SharedArrayBuffer; VERIFIED),
  no SIMD, no fast-math.

## ADR-5 — Server stack

Node.js 22 (TypeScript via type stripping, no build step), `ws` for
WebSockets, `node:sqlite` (WAL) for state, `esbuild` + Preact for the Mini
App. One dependency at runtime (`ws`). Emulation and ROM validation run in
separate child processes with a minimal environment (no secrets), V8 heap
caps, WASM memory caps, timeouts and kill-and-restore supervision.

## ADR-6 — Adapters and honest capability labels

Game-specific facts (match results, individual game over, stage boundaries,
turn ownership, scores, credits) come only from **versioned adapters pinned
to exact ROM hashes** and verified by tests against the real ROM in the real
core. Everything else uses labelled manual controls (report result, "I'm
out", end turn) with host/deputy adjudication. The only verified adapters
today are for our ATC test ROMs; commercial games run in manual mode until an
adapter is written and verified against the owner's ROM.

## ADR-7: Packaging and operations

**Decision.**
- One Docker image builds the cores from pinned sources in an
  `emscripten/emsdk:6.0.12` stage. It then type-checks, bundles the client,
  generates the romset catalogs and ships a slim `node:22.22.0-bookworm-slim`
  runtime. That runtime runs as an unprivileged user with a read-only root
  filesystem.
- Compose adds either Caddy (automatic HTTPS) or Cloudflare Tunnel (home
  hosting), plus a daily backup job. An optional local Bot API server is
  available, untested.

**Evidence (VERIFIED here).**
- The image builds and boots healthy with `--read-only --cap-drop ALL`.
- The cores it builds pass every determinism and lockstep test.
- The Neo Geo core is byte-identical to the separately built local one.
  The CPS and NES cores differ by a few bytes from older local builds that
  used earlier link flags; the image's cores are the reference.
- Caddy and Compose configs validate.

**Backups.**
- A backup is an online `VACUUM INTO` snapshot, plus referenced blobs and
  checkpoints, plus a SHA-256 manifest. Blobs shared with the previous
  backup are hard-linked.
- Restore verifies everything first, and moves existing data aside instead
  of deleting it (`server/ops/backup.ts`, tests in `test/unit/backup.test.ts`).

## ADR-8: Recovery semantics

- **Worker crash.** The session restores the last in-memory checkpoint and
  replays the input log. Clients do not notice (`test/unit/lockstep.test.ts`).
- **Graceful stop (SIGTERM).**
  - Every room is checkpointed and its sessions are ended with reason
    `shutdown`.
  - After the restart, rooms offer **Resume** or **Start fresh**.
  - Seats are not restored: players reclaim them, so there are no phantom
    seats.
  - Roles and chat persist.
- **Crash or power loss.** On start, any session row still open is closed
  as `crash`, so no duplicate live session exists. The room offers the last
  committed periodic checkpoint (`CHECKPOINT_INTERVAL_SEC`).
- **Checkpoint use.**
  - A resumed checkpoint is consumed: it cannot start two copies of a game.
  - It is refused if its compat key differs. The key covers ROM, core build,
    options, adapter and controller hardware.
  - Resumed sessions are marked `fresh = 0`, so records can tell them apart.
  - All of this is tested end to end in `test/e2e/restart.test.ts`.
- **Coins.** Coins are server-only.
  - With a credits adapter, a coin counts as "in the machine" from its pulse
    until the adapter reports a later frame, so two quick requests cannot
    bank credits.
  - A legitimate coin may need a second tap while the server catches up.
