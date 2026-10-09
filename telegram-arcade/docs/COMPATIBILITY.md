# Compatibility matrix

Last updated 2026-10-09. This page separates what has been **tested** from
what has only been **checked in source or docs** and what is **untested**.
No real phone, Telegram account, game controller or commercial ROM has been
used yet. The pilot steps in `PILOT_GUIDE.md` close those gaps.

| Mark | Meaning |
|---|---|
| ✅ | Verified by an automated test in this repository: headless Chromium 141 (Playwright) and Node 22.22 on Linux x86-64. See `TESTING.md`. |
| 🔎 | Verified in the official docs or the client's source code (`docs/research/`), but not run on that platform. |
| ⏳ | Untested. Needs a real device, account, controller or ROM. |
| ❌ | Not possible on that platform (by design of the platform). |
| — | Not applicable. |

## 1. Platforms × features

Telegram's in-app browsers:

| Client | In-app browser engine |
|---|---|
| iOS | WKWebView |
| Android | System WebView (Chromium) |
| Desktop on Windows | WebView2 |
| Desktop on macOS | WKWebView |
| Desktop on Linux | WebKitGTK |
| Web | The user's browser, inside an iframe |

"Ext. browser" is the one-time handoff link that opens the arcade outside
Telegram.

| Feature | iOS | Android | Desktop Win | Desktop mac | Desktop Linux | Telegram Web | Ext. browser |
|---|---|---|---|---|---|---|---|
| Launch from the group's URL button with the group's context (`start_param`, `chat_type`) | 🔎 | 🔎 | 🔎 (asks "Open app?" each time for unverified bots) | 🔎 | 🔎 | 🔎 | — |
| Server checks the signed launch data and the user's group membership | ✅ (server side, any client) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (one-time handoff) |
| WebAssembly core (single-threaded, no SharedArrayBuffer needed) | 🔎 · ❌ in Lockdown Mode (a banner explains) | 🔎 | 🔎 | 🔎 | 🔎 | 🔎 | ✅ Chromium |
| WebSocket connection, live room state, chat | 🔎 | 🔎 | 🔎 | 🔎 | 🔎 | 🔎 | ✅ Chromium |
| Lockstep replica stays identical to the server (hash checks, resync) | ⏳ (JavaScriptCore not tested) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ✅ Chromium, 0 desyncs |
| Keyboard controls | — | ⏳ (hardware keyboard) | ⏳ | ⏳ | ⏳ | ⏳ | ✅ Chromium |
| Touch controls (multi-touch, editable layout, left edge kept clear for Telegram's swipe) | ⏳ | ⏳ | — | — | — | ⏳ | ✅ Chromium touch emulation |
| Game controller (Gamepad API) | ⏳ (needs a tap first; implemented) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ (permissions policy allows it, 🔎) | ⏳ |
| Sound (AudioWorklet; iOS silent-switch workaround) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ (headless runs muted) |
| Fullscreen via `requestFullscreen` (Bot API 8.0) | 🔎 | 🔎 | 🔎 | 🔎 | 🔎 | 🔎 | — (browser fullscreen) |
| Orientation lock (`lockOrientation`, current orientation) | 🔎 | 🔎 | ❌ (no-op) | ❌ | ❌ | ❌ | — |
| Swipe-to-close disabled while playing (`disableVerticalSwipes`) | 🔎 | 🔎 | — | — | — | — | — |
| Minimize / restore (`activated` / `deactivated`, visibility) releases held buttons | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ✅ (visibility path) |
| 60 fps when the webview throttles `requestAnimationFrame` to 30 fps (accumulator pacing) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ✅ (pacing logic) |
| Downloaded game files cached on the device (IndexedDB) and clearable | ⏳ (IndexedDB is off in Lockdown Mode) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ✅ Chromium |
| Web upload to the shelf from the Mini App | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ✅ (HTTP endpoint tests) |
| "Open in browser" handoff (one-time, 60 s) | ⏳ (`openLink`) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ✅ (server side) |

Notes:
- **Apple Lockdown Mode** turns off WebAssembly, IndexedDB, the Gamepad API
  and Web Audio in web content (🔎 WebKit source). Games cannot run there. The
  app says so, and chat, votes and records still work. Users can exclude
  Telegram in the Lockdown Mode settings.
- **Telegram Desktop for Linux:** WebKitGTK builds may lack WebRTC. That
  does not matter, because lockstep uses only WebSocket and WebAssembly.
- **Telegram for macOS (the separate native app):** not researched in
  depth. Its public source is stale, so treat it like Desktop on macOS until
  tested.
- **Weak Android phones:** the main performance risk. CPS-2 and Neo Geo cost
  about 2–2.4 ms per frame on the test Xeon. A budget phone may be 3–5×
  slower, which still fits a 16.7 ms frame (an estimate, ⏳). The status line
  shows the device's own emulation time per frame, and a warning appears when
  it needs more than 75% of the frame budget.

## 2. Game content

| Content | How it was obtained | What is verified | Status |
|---|---|---|---|
| ATC test cabinet ROMs (`native/testroms/`): versus, co-op 4P, turns, solo | Our own, MIT, built with ca65 | Adapters read results, game over, continues, turns, scores and credits from the real ROM in the real core. Two players and a spectator played the versus ROM in Chromium, and the match result was recorded. | ✅ |
| RHDE: Furniture Fight (2P), Thwaite (co-op), Concentration Room (turns), Squirrel Domino (versus), allpads (4P input test), Escape from Pong | Built from pinned upstream sources by `native/testroms/fetch-homebrew.sh` (GPL/zlib/BSD/all-permissive; not redistributed here) | Boots and draws. Separately booted replicas agree under 2P/4P input. A replica restored from a snapshot converges over 3,600 frames. They run with manual results (no adapters). | ✅ (Node). Browser play ⏳ |
| FBNeo CPS-1 (`sf2`, `captcomm`), CPS-2 (`ssf2t`), Neo Geo (`kof98`) | Synthetic romsets: random bytes with the right names, sizes and CRC-32s. No copyrighted data. | Driver starts. Separate boots agree. A savestate restored in a fresh instance stays bit-identical for 40 s (after our savestate patch). Macro, diagnostic and service buttons are masked. | ✅ determinism only. Real gameplay ⏳ |
| Commercial romsets you own (CPS-1/2, Neo Geo with `neogeo.zip`, NES) | Your uploads | Upload validation (zip safety, CRC identification, parent/BIOS dependencies, boot test) is tested with synthetic sets. | ⏳ until you upload real sets |
| Neo Geo games | Every set needs SNK's `neogeo.zip` (BIOS). It is not bundled and is never downloaded automatically. | Dependency detection is verified against the real core. | ⏳ |

Arcade games use **manual results** (report a result, "I'm out", host/deputy
adjudication) until an adapter is written and verified against your exact
ROM. The shelf labels each game's capabilities honestly.

## 3. Product features × verification

| Feature (SPEC section) | Implemented | Verified by |
|---|---|---|
| One shared game per group; players and spectators see the same run (§1, §4) | Yes | ✅ e2e `slice`: 2 players and 1 spectator, simultaneous input, 0 desyncs |
| Seat transfer without restarting (§4, §7) | Yes | ✅ e2e `slice` (spectator becomes player 2, frames continue, P1's score kept) |
| Creator/host disconnects, game keeps running (§4, §14) | Yes | ✅ e2e `slice` (seat grace, then freed; group admin becomes acting host) |
| Measured latency and spectator delay (§4) | Yes | ✅ e2e `slice`; numbers in `TESTING.md` |
| Server-authoritative seats and queue, offers, grace, multi-device (§7) | Yes | ✅ unit `room-seats` (10 tests) |
| Fighters: winner stays, no streak limit, draws, mixed control not counted (§8) | Yes | ✅ unit `room-modes` + ATC versus adapter |
| Co-op: rotate at an individual game over only when someone waits (§9) | Yes | ✅ unit `room-modes` + ATC co-op adapter (4 players, Four Score) |
| Single player: spectating, pass controller, optional timed rotation (§10) | Yes | ✅ unit |
| Turn-based (shared controller and multi-controller), collaborative pass-around (§11) | Yes | ✅ unit + ATC turns adapter |
| Adapters pinned to ROM hashes; honest capability labels (§12) | Yes | ✅ unit `atc-adapter` |
| Voting, quorum/majority, switch at a safe point, override, failed switch keeps the room (§13) | Yes | ✅ unit `room-governance` |
| Host, deputies, succession, Telegram admin mapping, live role changes (§14) | Yes | ✅ unit; ✅ e2e (acting host) |
| Fair controls: no turbo/macros, SOCD cleaning, server-only coins (§15) | Yes | ✅ unit + FBNeo macro masking |
| Text chat with limits, mute, delete (§16) | Yes | ✅ unit; ✅ e2e (chat visible) |
| Records: verified/manual results, scores with control segments, ranking only within build / fresh / continues categories, dedup, corrections, audit (§17) | Yes | ✅ unit `records`, `room-modes`; ✅ e2e (verified match recorded) |
| Telegram identity, membership, group isolation, revocation, handoff (§5) | Yes | ✅ e2e `identity` (6 tests) + unit `telegram-*` (45 tests) |
| Uploads via Telegram (≤ 20 MB on the cloud Bot API; larger with a local Bot API server) (§6) | Yes | ✅ unit with a fake Bot API. Real Telegram ⏳. Local Bot API server ⏳ |
| Uploads via the Mini App (up to `MAX_UPLOAD_BYTES`) (§6) | Yes | ✅ unit (HTTP streaming, limits). Browser UI ⏳ |
| Malicious uploads (zip bombs, traversal, encryption, lies, nesting) (§6) | Yes | ✅ unit `zip-safety` (21 tests), `shelf-ingest` |
| Private ROM storage, group-bound downloads, no public URLs (§6) | Yes | ✅ e2e `identity` (cross-group 404, anonymous 401) |
| Checkpoints, resume after restart or crash, no phantom seats or duplicate sessions (§18) | Yes | ✅ e2e `restart` (2 tests), unit `lockstep` (worker crash) |
| Backups and restore (§21) | Yes | ✅ unit `backup` (3 tests) |
| Docker image (cores built from source), Compose, Caddy | Yes | ✅ image built and booted read-only, healthy. Caddyfile validated. Compose config validated. Public HTTPS deployment ⏳ |
| Real Telegram groups, real devices, controllers, real ROMs | — | ⏳ (see `PILOT_GUIDE.md`) |
