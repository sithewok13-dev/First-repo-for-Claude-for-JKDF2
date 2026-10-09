# Research summary

This page summarizes the research behind the design. The full reports, with
line-level citations, are in `docs/research/`. The adversarial check of each
report's claims is in `docs/research/VERIFICATION.md`.

| Label | Meaning |
|---|---|
| **VERIFIED** | A primary source was read: the spec, vendor docs, or client/core source at a pinned commit. Or the claim was measured by a test in this repository. |
| **REPORTED** | A secondary source, or a vendor page we could not open from the research environment. |
| **ASSUMED** | Our own inference, still to be confirmed on devices or in production. |

Research dates: 2026-10-08/09. The Telegram facts are against Bot API 10.3
(2026-08-24).

## Verification outcome

Each area was re-checked by an independent verifier who tried to refute the
high-impact claims. Of 90 checked claims:
- **64** were confirmed.
- **23** were downgraded (partly right; the correction was applied).
- **1** was refuted.
- **4** cannot be settled without devices.

| Area | Report | Confirmed / downgraded / refuted / uncheckable |
|---|---|---|
| Telegram Mini App launch and identity | `research/telegram-miniapp-launch.md` | 13 / 2 / 0 / 1 |
| Telegram bot: files, groups, membership, updates | `research/telegram-bot-files-groups.md` | 13 / 2 / 0 / 0 |
| Webview capabilities (input, audio, WASM, fullscreen) | `research/webview-capabilities.md` | 9 / 2 / 1 / 0 |
| Emulator cores (FBNeo, NES) in WebAssembly | `research/emulator-cores.md` | 19 / 3 / 0 / 1 |
| Netcode and latency (streaming vs lockstep) | `research/netcode-latency.md` | 6 / 8 / 0 / 1 |
| Hosting and cost | `research/hosting-cost.md` | 7 / 3 / 0 / 0 |
| Lawful test content | `research/test-roms.md` | 7 / 3 / 0 / 1 |

## Key findings that shaped the design

### Telegram

**Launching in groups**
- **VERIFIED: inline `web_app` buttons, reply-keyboard `web_app` buttons and
  the menu button only work in private chats.** The attachment menu is
  limited to major advertisers. So in a group, the bot posts an inline **URL
  button** to the direct link `t.me/<bot>/<short_name>?startapp=<token>`.
- **VERIFIED in the source of tdesktop, Android, iOS, Web K, Web A and
  TDLib:** that link opens the Mini App in the context of the group it was
  tapped in.
  - Caveat: on Telegram-iOS, the *Main* Mini App link (`t.me/<bot>?startapp`)
    is sent with the bot as peer, not the group. We use the named direct link,
    which every checked client handles correctly.
- **VERIFIED:** Telegram Desktop shows an "Open app?" confirmation on each
  launch from an unverified bot.

**Identity and membership**
- **VERIFIED:** the launch data carries `user`, `start_param`, `chat_type`,
  `chat_instance`, `auth_date`, `hash` and `signature`. It carries **no group
  chat id**.
  - So a launch never proves membership. The server checks it with
    `getChatMember`, which is only guaranteed for other users when the bot is
    an admin.
  - **UNCHECKABLE:** whether `chat_instance` is the same for every member.
    We do not rely on it.
- **VERIFIED:** HMAC validation uses key `HMAC_SHA256("WebAppData",
  bot_token)` over all fields except `hash`, including `signature`.
  Telegram defines no freshness limit; we use 1 hour.
- **VERIFIED:** since Bot API 10.2, Mini App methods only work from the
  origin registered in BotFather.

**Files and updates**
- **VERIFIED:** the cloud Bot API downloads at most **20 MB** per file.
  Users can post up to about 2000 MiB (4000 MiB with Premium). A self-hosted
  `telegram-bot-api --local` removes the download limit. It needs an
  `api_id`/`api_hash` from my.telegram.org and shared storage, and its file
  paths contain the bot token.
- **VERIFIED:** there is no history API, and updates expire after 24 h.
  Uploads must be indexed as they arrive. `/add` as a reply recovers files
  posted while the bot could not see them (reply messages carry the replied
  document).
- **VERIFIED:** admin bots receive all group messages and `chat_member`
  updates. The minimal admin right set is `can_manage_chat` alone.
  - **DOWNGRADE:** basic groups only have all-or-nothing admin.
- **VERIFIED:** rate limits are about 20 messages/min per group, 1/s per
  chat, and 429 responses carry `retry_after`. A group upgrading to a
  supergroup changes its chat id (`migrate_to_chat_id`).

### Webviews

**Engines and threading**
- **VERIFIED engines:**

  | Client | Engine |
  |---|---|
  | iOS | WKWebView |
  | Android | System WebView |
  | Desktop on Windows | WebView2 |
  | Desktop on macOS | WKWebView |
  | Desktop on Linux | WebKitGTK |
  | Telegram Web | Cross-origin iframe |

- **VERIFIED: no SharedArrayBuffer or wasm threads in Android WebView.**
  So the cores are single-threaded.
  - **REFUTED** (and corrected): the claim that this is impossible in
    Telegram Web. Chromium 137+ allows it via Document-Isolation-Policy. Not
    needed by us.

**Graphics, input and audio**
- **VERIFIED:** WebKit halves `requestAnimationFrame` to 30 fps in Low Power
  Mode and in untouched cross-origin iframes. Emulation is therefore paced
  by its own clock, not by rAF.
- **VERIFIED:** gamepads work in every engine. On iOS and macOS the webview
  must be first responder, so the arcade asks for one tap first.
- **VERIFIED:** on iOS the silent switch mutes Web Audio unless
  `navigator.audioSession.type = 'playback'` is set (we do, plus a silent
  looping `<audio>`).
- **VERIFIED:** element fullscreen and `screen.orientation.lock` are
  unavailable in the mobile webviews. `WebApp.requestFullscreen()` and
  `WebApp.lockOrientation()` (Bot API 8.0) are used instead.

**Apple Lockdown Mode**
- **VERIFIED in the WebKit source:** it disables WebAssembly, IndexedDB,
  Gamepad, Web Audio, WebGL and WebRTC. Games cannot run there; the app shows
  a clear message.

### Emulator cores

**Determinism (FBNeo)**
- **VERIFIED: FBNeo** fits server-authoritative lockstep. It has
  deterministic savestates, a rollback-netplay savestate context and a
  netgame flag that fixes the RTC date and RNG seed.
  - **DOWNGRADE + found by our tests:** upstream savestates miss sound-chip
    resampler state (YM2151, YM2610, MSM6295, QSound), and the YM2610 and
    Delta-T postload overwrites saved values. A freshly restored instance
    drifts.
  - Fixed by `native/patches/fbneo-cross-instance-savestates.patch`.
    Determinism is verified for CPS-1, CPS-2 and Neo Geo with synthetic
    sets.
- **VERIFIED:** the netgame flag is only kept if the frontend answers
  `GET_SAVESTATE_CONTEXT`. Ours does, with the rollback-netplay context.
- **VERIFIED:** sound must be emulated on every replica, including the
  server (chip state advances only when audio is rendered). Every audio
  option is pinned, along with force-60hz, CPU speed, hiscores, diagnostic
  input, frameskip, SOCD and Neo Geo mode.
- **UNCHECKABLE here:** bit-identical results between V8 (Node, Chromium) and
  JavaScriptCore (iOS). Host clock and random sources are closed in our
  frontend. The periodic hash check and resync is the safety net, and device
  tests are pending.

**Romsets and BIOS**
- **VERIFIED:** CPS-1/2 need no BIOS (CPS-2 keys are in the game zip).
  Neo Geo needs SNK's `neogeo.zip` (`sp-s3.sp1`, `sm1.sm1`, `sfix.sfix`,
  `000-lo.lo`). Romsets are identified by name, size and CRC-32 against the
  core's own driver list.

**NES (FCEUmm)**
- **VERIFIED:** FCEUmm's Four Score switch is not in its savestate and
  resets on load. The frontend re-applies controller port devices after
  every load.
- **VERIFIED:** FCEUmm ignores the NES 2.0 Four Score flag. Four players
  need port devices `0x201` on ports 3 and 4.

**Licences**
- **VERIFIED:** FBNeo's licence forbids any monetary profit and donation
  requests, requires the verbatim licence text and requires published
  source changes. FCEUmm is GPL-2.0. See `LICENSES.md`.

### Netcode and latency

**Latency**
- **REPORTED:** commercial cloud gaming adds roughly +40–67 ms over local
  play.
  - **DOWNGRADE:** the range is wider. GeForce NOW Competitive is about +27–30
    ms, a Stadia TV setup up to +216 ms, and one study +40–150 ms.
- **ASSUMED model, VERIFIED pieces in libwebrtc:** tuned streaming costs RTT
  + 15–40 ms. Delay-based lockstep costs RTT + 1–2 frames. So lockstep is
  only modestly better on latency alone.
- **MEASURED here (lockstep):** median input-to-rendered-frame is **31 / 77 /
  126 ms at 0 / 50 / 100 ms RTT**, which is RTT + 16–31 ms (`TESTING.md`).

**What decided it for lockstep**
- Bandwidth: about 12–46 kbps per viewer against 0.4–3 Mbps of video.
- No WebRTC or TURN needed.
- Pixel-perfect output.
- Cheap spectators.
- Rollback becomes possible later.

**Costs of lockstep (VERIFIED)**
- Every viewer downloads the game files. CPS-2 sets are 25–46 MB and Neo
  Geo sets up to about 93 MB, plus about 3 MB of gzipped core. Files are
  cached per device and can be cleared from Controls.
- CPU on weak phones is the main risk. **UNCHECKABLE** without devices. The
  estimate is 5–25 ms per CPS-2 frame on budget Android.

**Transport**
- **VERIFIED:** TCP head-of-line blocking adds tail latency under packet
  loss: p99/p99.9 of 77/127 ms at 1% loss and 60 ms RTT. WebSocket stays the
  base path because it works everywhere. Unordered datagrams
  (DataChannel/WebTransport) for player input are a later optimization.

### Hosting and cost

See `COSTS.md`.
- **VERIFIED/REPORTED:** lockstep needs under 50 GB/month of egress at pilot
  usage. Any small VPS or a home Mac mini behind Cloudflare Tunnel is
  enough.
- **VERIFIED:** Fly.io shared vCPUs are throttled to a 6.25% baseline, which
  is unsuitable for a 60 fps emulator.
- **VERIFIED:** Oracle Always Free is now 2 OCPU / 12 GB.
- **REPORTED:** Hetzner's cheapest lines are often out of stock in 2026.
- **VERIFIED:** Cloudflare Tunnel carries WebSockets, but restarts drop them
  (clients reconnect). It limits request bodies to 100 MB on the Free plan.
  Keep `MAX_UPLOAD_BYTES` under that when tunnelling.
- **VERIFIED:** Caddy closes WebSockets on a config reload unless
  `stream_close_delay` is set. `deploy/Caddyfile` sets 5 minutes.

### Lawful test content

- **VERIFIED:** plenty of lawful NES homebrew exists. We build six titles
  from pinned sources (2P simultaneous, co-op, hot-seat, versus, a
  Four Score diagnostic and a smoke test). None is redistributed by this
  repository. Concentration Room is not byte-reproducible (it embeds the
  build time).
- **VERIFIED/REPORTED:** no redistributable multiplayer arcade content
  exists for FBNeo.
  - The MAME "free ROMs" are distributed only from mamedev.org, for
    non-commercial use.
  - FBNeo's homebrew flag is not a licence.
  - Every Neo Geo set needs SNK BIOS files.
  - So arcade gameplay can only be verified with romsets the owner supplies.
    Our tests use synthetic CRC-matched sets to verify determinism and
    validation without copyrighted data.
- Our own **ATC test cabinet** (MIT) fills the gaps: four modes, a 4-player
  Four Score, and a fixed RAM map so adapters can be verified.

## Open questions (need devices or a pilot)

These are tracked in `COMPATIBILITY.md` and the pilot plan.
- Real-device behaviour of gamepads, audio and fullscreen in each Telegram
  client.
- JavaScriptCore determinism on iOS.
- CPU headroom on budget Android phones for CPS-2 and Neo Geo.
- Whether Telegram sends "bot left" for the old basic group after an
  upgrade (handled defensively).
- `chat_instance` stability across members (not relied on).
