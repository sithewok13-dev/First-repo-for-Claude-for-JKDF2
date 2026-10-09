# Latency and synchronization: streamed emulation vs synchronized client-side emulation

Area: input-to-photon latency, netcode, transports, spectators, determinism and bandwidth for a Telegram-group arcade (fighting games and beat 'em ups) on iPhone, Android and PC Telegram clients.
Date: 2026-10-08/09. Working files: the research workspace (not committed; sources are cited inline) (sources, scripts, measurements).

Status labels:
- **VERIFIED**: I read the primary source text or code myself. The path and line are given.
- **MEASURED (here)**: I ran it in this container. The script and output are given. Results are hardware- and content-specific.
- **REPORTED**: from secondary sources or search summaries. I could not open the primary page (arxiv, WPI, infil.net, Digital Foundry and similar sites were not reachable from this container).
- **ESTIMATE / ASSUMED**: my own model or inference. The assumptions are stated.

---

## 0. Summary and recommendation

**Recommendation: architecture (b), a server-authoritative deterministic input stream with WASM replicas on clients.** Build it in two steps:

1. **Delay-based lockstep first.** It is simpler and needs no rollback CPU. The server stamps each input into the next frame it has not yet finalized. Each client keeps a small adaptive playout buffer.
2. **Optional client-side rollback (prediction) later,** for seated players on devices that pass a CPU benchmark. Spectators never need rollback.

Use WebSocket as the universal transport. An unreliable WebRTC DataChannel or WebTransport is an optional upgrade for player input only.

Keep streaming (a) only as a possible fallback for devices that cannot emulate a core in real time. It should not be the main path.

Why (key numbers):

| Factor | (a) Server emulation + video stream | (b) Authoritative input stream + local WASM replicas |
|---|---|---|
| Added press-to-photon latency vs local play | Commercial services measured **+40 to +67 ms** (REPORTED: Digital Foundry Stadia +44 to 56 ms; GameStar Stadia/GFN about +40 to 60 ms; PC Gamer Stadia +62 to 67 ms). Model for our case: **RTT + 15 to 40 ms** best case (ESTIMATE, §1.4) | Delay-based: **RTT + about 1 to 2 frames** (RTT + 17 to 33 ms; ESTIMATE, §2.2). **With rollback: a local delay of 0 to 2 frames, whatever the RTT** (up to the 8-frame prediction window; VERIFIED GGPO limit). |
| Bandwidth per viewer (downstream) | 320x224@60 H.264: **0.36 to 1.0 Mbps** on moderate synthetic arcade content, **0.9 to 3.1 Mbps** on noisy stress content for about 33 to 40 dB PSNR. Add audio. (MEASURED here, §6) | **About 12 to 46 kbps** of input records over WSS, depending on batching (computed from the project's message format, §6). Snapshots only on join or resync (NES: 13.7 KB raw, 725 B deflated; MEASURED). |
| Works in every Telegram client | No WebRTC on Telegram Desktop Linux (WebKitGTK); see the sibling report `webview-capabilities.md`. Needs a WebSocket + WebCodecs fallback there. | WebSocket + WASM everywhere. No WebRTC needed. |
| Spectators | One more video viewer each, with the same latency and bandwidth | Input stream with a bigger buffer. About 20 to 45 kbps. Can sit behind by any amount. |
| Visual quality | Compression artifacts on hit flashes and scrolling. Min-frame PSNR fell to 21 to 31 dB in the CBR runs (MEASURED) | Pixel-perfect native output |
| Main risk | Latency, egress cost, server encoder CPU | Client CPU on weak Android (single-threaded WASM), determinism across engines |

**Honest caveat.** Server-relay *delay-based* lockstep is not much lower latency than a well-tuned stream. Both cost about one RTT plus one or two frames, because your own input must travel to the server and back before it executes. The decisive latency win in (b) is that it allows **speculative local execution (rollback)**, which streaming cannot do. It is also more tolerant of jitter and loss: its packets are tiny, with no keyframes and no jitter buffer.

---

## 1. Streaming latency (architecture a)

### 1.1 Measured by others (REPORTED)

| Source | What was measured | Numbers |
|---|---|---|
| Digital Foundry, Nov 2019, via Windows Central / Android Central / forum quotes | Stadia vs local console, display lag excluded | Stadia latency delta of **44 to 56 ms** vs in-home consoles. Destiny 2: Stadia 60 fps at 144 ms vs Xbox One X 30 fps at 100 ms. https://windowscentral.com/google-stadia-doesnt-run-destiny-2-and-red-dead-redemption-2-4k-resolution-exhibits-more-input-lag |
| GameStar, via PCGamesN | Makey-Makey + high-speed camera | Local PC 39.5 / 41.3 ms; Stadia "balanced" 95.4 / 100.5 ms; GeForce NOW "balanced" 97.8 / 81.1 ms (Destiny 2 / Metro Exodus). That is **+40 to 60 ms**. https://www.pcgamesn.com/nvidia/geforce-now-competitive-mode-latency |
| PC Gamer | 240 fps camera | Destiny 2: local 83 ms vs Stadia 150 ms. SotTR: 63 vs 125 ms (**+62 to 67 ms**). GFN was 40 to 100 ms faster than Stadia in a later test. https://www.pcgamer.com/heres-how-stadias-input-lag-compares-to-native-pc-gaming/ , https://www.pcgamer.com/geforce-now-beats-stadia-in-our-input-latency-testing/ |
| TU Darmstadt 2014 (anonymized providers) | Cloud vs local | **+40 to 150 ms** (40 to 90 ms for one provider, 100 to 150 ms for the other). https://www.kom.tu-darmstadt.de/papers/LWD+14.pdf |
| Parsec blog / support docs | Encoder and pipeline | Median NVENC encode **5.8 ms**, AMD VCE 15.06 ms. Median Co-Play ping **32.68 ms**. Whole pipeline on a gigabit LAN at 240 fps: **4 to 8 ms**. At 60 fps, encode and decode should each stay below 15 ms. https://parsec.app/blog/nvidia-nvenc-outperforms-amd-vce-on-h-264-encoding-latency-in-parsec-co-op-sessions-713b9e1e048a , https://parsec.app/blog/parsec-game-streaming-total-latency-at-240-frames-per-second-c0818cc0daa5 , https://support.parsec.app/hc/en-us/articles/32381603663636-Stream-Overlay-Stats-and-Logging |
| NVIDIA (vendor claim) | GFN top tier | "Sub-30 ms click-to-pixel" in best case |
| WebRTC blog (single measurement) | WebRTC pipeline | Jitter-buffer delay about 7 ms local and 10 ms remote. H.264 encode and decode about 10 ms each. https://transitiverobotics.com/blog/webrtc-latency-breakdown/ |
| Local reference, fighting games (EventHubs) | Total input lag incl. game pipeline | SF6 on PS5 / XSX / XSS / PC: **55.4 to 58.9 ms**, about 3.5 frames. https://www.eventhubs.com/news/2023/jun/05/input-lag-tests-sf6-platforms |

Takeaway (REPORTED): well-engineered native cloud-gaming stacks with GPU encoders, regional servers and native clients add about **40 to 65 ms** over local play. Browser clients can be much worse. One report cites Chrome sessions above 300 ms, which is not verified.

### 1.2 Encoder and decoder cost at our resolution (MEASURED here)

Setup: synthetic 2D-fighter-like content at 320x224@60, 1,200 frames (20 s), generated by `scripts/synth_arcade2.py` ("moderate": banded sky, buildings, brick floor, animated crowd, two shaded sprites, HUD, hit flash every 1.5 s, screen shake) and `scripts/synth_arcade.py` ("noisy": random dithered tiles, a stress case). Encoder: ffmpeg 6.1.1 with libx264 `-tune zerolatency` and libvpx `-deadline realtime -cpu-used 8 -lag-in-frames 0`, **one thread**, on an Intel Xeon @ 2.8 GHz. Y-PSNR was computed per frame with numpy (`scripts/psnr.py`). ffmpeg's `psnr` filter misaligned the frames by one, so I did not use it. Raw output: `media/enc_moderate.txt` and `media/enc_noisy.txt`.

| Config | Moderate content: kbps / mean Y-PSNR (min) | Noisy content: kbps / mean Y-PSNR (min) | Encode ms/frame (wall, 1 thread) | Keyframe bytes | P-frame p99 bytes |
|---|---|---|---|---|---|
| x264 ultrafast CRF23 | 1038 / 39.5 dB (33.7) | 3114 / 37.1 (29.1) | 0.93 / 1.11 | 18.6 KB / 51 KB | 9.7 KB / 24.7 KB |
| x264 veryfast CRF23 | 536 / 36.8 (30.9) | 1672 / 32.9 (25.5) | 1.72 / 2.16 | 10.6 KB / 24 KB | 2.3 KB / 15.5 KB |
| x264 veryfast CRF28 | 361 / 32.2 (26.5) | 935 / 28.1 (21.3) | 1.74 / 2.31 | 7.3 KB / 13.9 KB | 1.7 KB / 8.1 KB |
| x264 veryfast CBR 1 Mbps (VBV about 1 frame) | 846 / 40.5 (24.3) | 871 / 27.1 (17.7) | 1.96 / 2.02 | 3.9 KB / 4.1 KB | 3.3 KB / 3.2 KB |
| x264 veryfast CBR 2 Mbps | 1701 / 50.4 (30.5) | 1790 / 32.9 (19.7) | 2.25 / 2.24 | 8.1 KB / 7.7 KB | 6.6 KB / 6.0 KB |
| VP8 realtime CBR 1 Mbps | 956 / 41.3 (30.4) | 975 / 29.0 (24.1) | 2.73 / 3.36 | 14.1 KB / 34 KB | 6.0 KB / 7.7 KB |

- Software decode (ffmpeg h264/vp8, one thread, including writing YUV): **0.4 to 0.7 ms/frame**.
- Takeaway: at 320x224, CPU encoding is cheap. One room costs about 1 to 2.3 ms of one core per frame, or about 6 to 14% of a core. Encoder latency is not the bottleneck.
- The real costs are **frame size versus link and pacing rate**, the jitter buffer, and vsync misalignment.
  - A single 18.6 KB keyframe takes 149 ms to serialize at 1 Mbps and 30 ms at 5 Mbps (computed).
  - So a streaming design needs intra-refresh or tight VBV, plus keyframe avoidance (no keyframe on each viewer join).
- Caveat: synthetic content. Real CPS2/Neo-Geo art is probably between the two sets, but this is unverified. Measure with real footage.

### 1.3 WebRTC receiver and sender internals (VERIFIED)

Source: libwebrtc as vendored in Firefox `main` (`third_party/libwebrtc`, `README.mozilla.last-vendor` says it was updated 2026-10-06). Chrome and Safari use the same libwebrtc codebase; that they behave identically is ASSUMED.

- **Playout-delay RTP header extension** (`docs/native-code/rtp-hdrext/playout-delay/README.md`): for "interactive streaming (gaming, remote access)" the sender sets min = max = 0, meaning render as soon as possible. The extension is still marked experimental and best-effort, with 10 ms granularity.
- **Low-latency rendering path.**
  - It is used only if `min_playout_delay == 0 && max_playout_delay <= 500 ms` (`modules/video_coding/timing/timing.cc:36-38, 76-79`).
  - When it is used, `RenderTime()` returns 0, which means "render as soon as possible" (`timing.cc:197-203`).
  - The default render delay is 10 ms (`timing.h:37`).
- **Only the sender can set max playout delay.** The receiver takes it from the `PlayoutDelayLimits` RTP extension (`video/video_receive_stream2.cc:796-803, 910-911, 1267-1270`). A JS receiver can only raise the minimum (`jitterBufferTarget`; BCD: Chrome 124, Firefox 115, Safari 27). So **our server must negotiate and send the playout-delay extension** if we stream.
  - EmulatorJS's `receiver.playoutDelayHint = 0` (below) is a Chrome-only, non-standard minimum hint.
- **Pacer.** Pacing rate = loss-based target rate × 2.5, or × 1.1 once transport-wide feedback (send-side BWE) is active (`modules/congestion_controller/goog_cc/goog_cc_network_control.cc:55-58, 662-666`). A frame of S bytes therefore adds about S×8/(1.1×BWE) of pacing delay (ESTIMATE). For example, 10 KB at a 5 Mbps estimate is about 15 ms.
- **Decode-queue limit** in zero-playout-delay mode: 8 frames (`video/video_stream_buffer_controller.cc:55`).
- **Real example: EmulatorJS 4.2.4 netplay** (sibling clone `emulator-cores/emulatorjs`, commit f4f0f1c, 2026-09-19) is a *streaming* design.
  - The host captures the canvas at **30 fps** (`data/src/netplay.js:784`).
  - Guests send inputs over a reliable, ordered DataChannel with "high" priority (`netplay.js:1111`).
  - Receivers set `playoutDelayHint = 0` (`netplay.js:1266`). The code comment claims the default is "~200ms+", which is not verified.
  - So browser game streaming already exists in this space, but at 30 fps and with ordered, reliable inputs.

### 1.4 Streaming latency budget for our case (ESTIMATE)

The figure below is added latency over local play. Input polling alignment and display scan-out exist in both cases and cancel out. T = 16.7 ms.

```
uplink RTT/2 + server emulate (1-4 ms) + encode (1-2.3 ms, measured) + pacing (2-15 ms, frame-size dependent)
+ downlink RTT/2 + jitter buffer (0-10 ms with playout-delay 0/0; tens of ms by default or on Wi-Fi jitter)
+ decode (0.5-5 ms) + vsync misalignment of an asynchronously arriving frame (avg T/2 ≈ 8 ms)
≈ RTT + 15-40 ms best case; RTT + 40-80 ms with default jitter buffering or keyframes
```

| RTT to server | Streaming, added (best / typical) | Delay-based lockstep, added (§2.2) | Lockstep + rollback, local feel |
|---|---|---|---|
| 20 ms | 35-60 / 60-100 ms | 37-53 ms (2-3 frames + RTT) | 0-2 frames (0-33 ms) |
| 40 ms | 55-80 / 80-120 ms | 57-73 ms | 0-2 frames |
| 80 ms | 95-120 / 120-160 ms | 97-113 ms | 0-2 frames, rollback about 5 frames |

The best-case rows are consistent with the commercial measurements of +40 to 67 ms at low RTT.

---

## 2. Lockstep and rollback (architecture b)

### 2.1 What the reference implementations do (VERIFIED)

**GGPO** (`pond3r/ggpo` @ 7ddadef, 2019-11-07):
- The frame-delay guidance applies to fighting games: "**any frame delay larger than 1 can be noticed by most intermediate players, and expert players may even notice a single frame of delay**" (`doc/DeveloperGuide.md:178`).
- If a packet takes longer than the frame delay, GGPO uses speculative execution (rollback) for the remainder (`DeveloperGuide.md:176`).
- Prediction window: `MAX_PREDICTION_FRAMES 8` (`src/lib/ggpo/sync.h:18`, `src/include/ggponet.h:34`). If no remote input arrives within 8 frames, `ggpo_synchronize_inputs` fails and the game must stall (`DeveloperGuide.md:89`).
- Time sync: each peer averages its frame advantage over a 40-frame window. If the difference reaches 3 or more frames, the peer that is ahead sleeps, capped at 9 frames (`timesync.h:14-17`, `timesync.cpp:31-85`). It recommends at most once every 240 frames (`backends/p2p.cpp:10, 148-160`).
- Spectators: at most 32 per host (`ggponet.h:35`). They receive **only confirmed inputs** (all players' inputs for frames up to the minimum confirmed frame) and never predict (`backends/p2p.cpp:127-141`).
- Synctest mode does a one-frame rollback every frame and compares checksums to find non-determinism (`DeveloperGuide.md:281`, `backends/synctest.cpp:117-150`).

**RetroArch netplay** (`libretro/RetroArch` master, read 2026-10-08):
- Its input latency adapts to **how fast this machine can replay frames**:
  - `frames_per_frame = 16666 / frame_run_time_avg - 2`.
  - If more frames are unconfirmed than the machine can replay, it adds a frame of input latency, up to `input_latency_frames_max`. It removes one when there is spare capacity (`network/netplay/netplay_frontend.c:8573-8605`).
  - This is the right model for weak phones: rollback depth is bounded by CPU, and the remainder becomes input delay.
- Desync detection: the server sends the **CRC32 of the serialized state every `check_frames`** frames (default **600**, `config.def.h:1615`).
  - On mismatch the client **requests a savestate** (`netplay_frontend.c:3498-3536, 6250-6290`).
  - If the first check mismatches, CRCs are assumed unusable for that core.
- Other limits: `NETPLAY_MAX_STALL_FRAMES 60` and `MAX_CLIENTS 32` (`netplay_private.h:55-61`). A spectate command exists (`netplay_private.h:128-129`).

**Fightcade FBNeo** (sibling clone `emulator-cores/fightcade-fbneo`, c959501, 2025-06-22):
- The frame delay comes from the Fightcade launcher (`quark:served,…,delay,ranked`) and is passed to `ggpo_set_frame_delay`. In one run-ahead mode the delay is forced to at least 2 (`src/burner/win32/fbn_ggpo.cpp:516-535`).
- **State checksums are computed only in debug builds** ("Ignore checksums in release builds for now. It takes a while.", `fbn_ggpo.cpp:282-284`). So the best-known arcade rollback client does not do runtime desync detection.
- Spectating is a separate "stream" session through Fightcade's server (`ggpo_start_streaming`, `fbn_ggpo.cpp:560-567`). Its delay is not visible in this code.

**GGRS** (Rust GGPO reimplementation, v0.13.0): it has optional `DesyncDetection` that exchanges u128 checksums of confirmed frames at an interval (`src/sessions/p2p_session.rs:168-173, 380-389`).

### 2.2 How input delay relates to RTT (ESTIMATE from the mechanisms above; T = 16.7 ms at 60 Hz)

- **P2P delay-based** (classic Fightcade/GGPO with no prediction): delay ≥ one-way latency between peers = RTT_peer/2 ÷ T.
  - REPORTED rule of thumb: delay ≈ ping/2. Capcom's GGPO guidance for *Darkstalkers Resurrection* maps ping 0-50 ms to delay 0, 100 ms to 2-3, and 300 ms to 7 (with rollback covering the rest). https://eventhubs.com/news/2013/jan/11/darkstalkers-resurrection-will-feature-ping-filtering-tool-online-match-making
- **Server-relay, server-stamped lockstep** (our design (b), delay-based):
  - Your own input executes after uplink + wait for the next server frame + downlink + client playout buffer B.
  - Added over local ≈ RTT + B×T, with B = 1 to 2 frames to absorb downlink jitter.
  - The opponent's input arrives after (RTT_A + RTT_B)/2.
  - The project's protocol already acknowledges which frame carries each input (`shared/protocol.ts:58-75`, `InputAck{seq, frame, clientMs}`), so it is server-stamped.
- **Server-relay with client prediction (rollback):** the local input delay D is chosen freely (0 to 2 frames). The rollback depth needed is:

  `R ≈ ceil(((RTT_A + RTT_B)/2 + jitter_p99) / T) − D`

  Examples: RTT 40 + 40 ms with D = 1 gives R ≈ 2 to 3. RTT 80 + 80 ms with D = 1 gives R ≈ 5. GGPO's window is 8.
- **Fairness note.** With server stamping, a player with a higher RTT gets later frame stamps. The disadvantage falls only on that player and the room never stalls. With client stamping plus a server deadline, a late input must either stall everyone or be moved to a later frame.

### 2.3 What latency fighting-game players accept (REPORTED, except GGPO)

- VERIFIED (GGPO guide): intermediate players notice more than 1 frame of added delay, and experts notice 1 frame.
- REPORTED: modern local fighting games already run at about 55 to 59 ms (3.3 to 3.5 frames) total input lag on current consoles (SF6, EventHubs). A Third Strike port averaged 59.3 ms.
- REPORTED (BSc thesis, diva-portal 1322881): in a test fighting game, players' sense of being able to react collapsed at **about 100 ms or more** of network latency. Rollback degraded less than delay-based.
- REPORTED (WPI, CS:GO): performance degrades measurably even below 100 ms. Local latency hurts more than network latency.
- Working targets (ASSUMED):
  - Added delay of ≤ 2 frames feels near-local to casual group play.
  - 3 to 4 frames is clearly felt but playable.
  - Above 6 frames (about 100 ms) is poor for fighters.
  - Beat 'em ups (co-op) tolerate 1 to 2 frames more.

### 2.4 Rollback CPU cost and mobile feasibility

MEASURED here (`scripts/bench_core.mjs`, output in `bench_core_fceumm.json`). This is the project's own build: `native/build/fceumm.wasm`, emcc 6.0.12 -O3, run read-only in Node v22.22.0 on the 2.8 GHz Xeon with the project's NES test ROM `atc-versus.nes`.

| Operation | Mean | p99 |
|---|---|---|
| Frame with video + audio | 0.53 to 0.63 ms | 0.8 to 1.1 ms |
| Frame without render/audio (re-simulation) | 0.45 ms | 0.54 ms |
| `retro_serialize` (13,758 B state; 725 B after raw deflate) | 0.017 ms | 0.04 ms |
| `retro_unserialize` | 0.023 ms | 0.08 ms |
| FNV-1a 64-bit hash of state (project `fe_state_hash`) | 0.025 ms | 0.07 ms |
| Rollback of 2 frames: load + 2 resims + 1 rendered frame | 1.45 ms | 1.61 ms |
| Rollback of 4 frames | 2.38 ms | 2.96 ms |
| Rollback of 7 frames | 4.00 ms | 7.93 ms |
| Rollback of 8 frames | 4.35 ms | 8.20 ms |

Interpretation:
- **NES-class rollback is trivially affordable**, even at the full 8-frame GGPO window.
- **The open question is FBNeo CPS1/CPS2/Neo-Geo.** No lawful ROM was available to run those cores here.
- ASSUMED, to be measured:
  - CPS2 (68000 at 11.8 MHz + Z80 + QSound, 384x224 tile renderer) costs **about 5 to 15× NES per frame**, so roughly 2 to 7 ms/frame in WASM on this Xeon.
  - Budget Android CPUs (Cortex-A75/A55 class) are probably **2 to 4× slower** single-threaded than this Xeon, so about 5 to 25 ms/frame. That makes **even plain 60 fps emulation marginal and rollback infeasible** on the weakest phones.
  - Recent iPhones and mid- to high-end Android are probably at or above Xeon speed, so 4 to 8 frames of rollback is plausible there.
- Supporting evidence:
  - VERIFIED: libretro's FBNeo docs say the core "will already run really well on low-end devices (rpi3, ...)". For slow devices they recommend disabling rewind, runahead and pre-emptive frames, because those are "known for increasing requirements" (`emulator-cores/libretro-docs/docs/library/fbneo.md:385-399`).
  - REPORTED: WASM runs SPEC on average 45 to 55% slower than native, with peaks of 2.1 to 2.5× ("Not So Fast", USENIX ATC'19).
  - VERIFIED from sibling research: there are no WASM threads in Android WebView (BCD `webassembly.threads-and-atomics` webview_android = false), so the core runs on one thread.
- **Design consequence.** Make rollback depth **CPU-adaptive per client**, as RetroArch does (§2.1). Measure the frame time at join. Allow R_max = floor(budget / frame_cost) − 1, and put the remainder of the RTT into input delay. A client that cannot sustain 60 fps of plain emulation cannot be a lockstep player for that core. Offer it spectating with a large buffer, or a streaming fallback.

---

## 3. Transport: WebSocket (TCP) vs WebRTC DataChannel vs WebTransport

### 3.1 Head-of-line blocking: a model (ESTIMATE; `scripts/hol_sim.py`, output `hol_sim_output.txt`)

Model:
- A 60 Hz stream of small messages with Bernoulli loss (real loss is burstier, which is worse for both transports) and a constant one-way delay of RTT/2.
- **TCP:** a lost segment is detected when a later segment's SACK arrives plus a reorder window of RTT/4 (RACK-like), or by a tail-loss probe at 2×SRTT. A lost retransmission falls back to RTO ≥ 200 ms with backoff. Delivery is in order.
- **Datagrams:** each packet repeats the last 3 frames of input.

| Loss | RTT | WebSocket/TCP added delay p99 / p99.9 / max (ms) | Datagram ×3 redundancy p99 / p99.9 / max (ms) |
|---|---|---|---|
| 0.5% | 30 | 30 / 47 / 247 | 0 / 17 / 33 |
| 1% | 60 | 77 / 127 / 677 | 0 / 17 / 33 |
| 2% | 60 | 77 / 243 / 677 | 17 / 17 / 110 |
| 5% | 100 | 292 / 675 / 1525 | 17 / 33 / 150 |

- Each TCP loss stalls *all* following inputs for about one RTT plus one packet interval. In lockstep that becomes a stall or a rollback for everyone watching that player's input.
- Redundant unreliable datagrams turn a loss into at most about 1 frame of delay.
- VERIFIED inputs to the model:
  - Linux (7.3-rc6) `TCP_RTO_MIN = HZ/5` (200 ms) and `TCP_TIMEOUT_INIT = 1 s` (`include/net/tcp.h:162-167`).
  - The tail-loss probe fires at 2×SRTT, plus `rto_min` if only one packet is in flight (`net/ipv4/tcp_output.c:3122-3134`).
  - The `ws` 8.22.0 server disables Nagle (`node_modules/ws/lib/websocket.js:261`).
  - ASSUMED: client OS stacks (iOS, Android, Windows) behave broadly similarly.

### 3.2 Availability inside Telegram webviews

Source: MDN browser-compat-data 8.1.5 (main, read 2026-10-08), with the sibling report `webview-capabilities.md` for Telegram specifics.

| API | Chrome / Android WebView | Safari / WKWebView | Telegram Desktop |
|---|---|---|---|
| WebSocket | yes | yes | yes, all OSes |
| RTCDataChannel (incl. `ordered:false`, `maxRetransmits:0`) | Chrome 24; WebView mirrors Chrome | Safari 11 | Windows WebView2 and macOS: yes. **Linux WebKitGTK: no WebRTC** (VERIFIED by the sibling report) |
| WebTransport (+ datagrams) | Chrome 97 | **Safari 26.4** (Mar 2026; BCD; WebKit blog quoted by the W3C record) | Windows: yes (Chromium). macOS: depends on OS WebKit. Linux: unknown |
| `RTCRtpReceiver.jitterBufferTarget` | Chrome 124 | Safari 27 | — |

BCD marks `webview_ios` and `webview_android` as "mirror" (inferred from Safari/Chrome, not tested in WKWebView or WebView). So WebTransport needs iOS 26.4+ and cannot be the only transport for some time.

### 3.3 TURN and NAT for our topology

- **TURN is mostly unnecessary for (b).** Clients talk to a server with a public IP. A WebRTC DataChannel to a public UDP endpoint (ICE-lite or full ICE on the server) traverses client NATs without a relay (ASSUMED, standard ICE behaviour). TURN statistics (10 to 25%, or 0 to 50%, of P2P sessions relayed; REPORTED from vendor blogs and bloggeek) apply to *peer-to-peer* media.
- **UDP-blocked networks need a TCP path.** REPORTED (Edeline et al., arXiv 1612.07816, citing Google's QUIC data): about 93% of connections succeed over QUIC; UDP is impaired for about 5% (4.5% blocked near the user), and fully blocked on 2 to 4% of access networks, concentrated in enterprises.
  - So keep **WebSocket as the always-available path**. Use DataChannel or WebTransport only as an upgrade for seated players' uplink and their own confirmations.
- **Cost if TURN is ever needed** (REPORTED, vendor pricing pages): Cloudflare Realtime TURN costs $0.05/GB after 1,000 GB/month free (shared with the SFU). Twilio NTS costs $0.40/GB (US/EU), $0.60 (Asia) and $0.80 (Sydney, São Paulo).
  - At 1 Mbps a streamed viewer moves 0.45 GB/hour: $0.18/viewer-hour on Twilio, $0.0225 on Cloudflare.
  - An input-stream viewer moves about 0.01 to 0.02 GB/hour.

---

## 4. Spectators

- **GGPO:** spectators get only *confirmed* inputs, so they are always at least as late as the slowest player's confirmation. Up to 32 per host (VERIFIED, §2.1).
- **RetroArch:** has a spectate mode (VERIFIED command). Spectators receive the server's input stream and the periodic CRCs.
- **Fightcade:** spectators use a separate server "stream" session (VERIFIED code path). The delay is not documented (search found nothing).
- **Broadcast norms** (REPORTED): Twitch Low Latency is about 2 to 5 s. Mux low-latency is about 5 s or more.
- **Recommendation for (b)** (ESTIMATE):
  - Spectators replay the authoritative input stream behind an **adaptive buffer of about 6 to 15 frames (100 to 250 ms)**, so they never stall on jitter.
  - They do not need rollback.
  - When they are more than about 1 s behind, they catch up by running 2 to 4 frames per display frame without rendering. If they are hopelessly behind or desynced, they reload a snapshot.
  - Late join: snapshot (deflated savestate) plus the input records since that frame. The project already has `Bin.Snapshot`, `Bin.Frames` and `Bin.Hash`.
  - In (a), each spectator is another video viewer at the same 0.4 to 3 Mbps.

---

## 5. Determinism across browsers and Node.js, and desync detection

### 5.1 Pitfalls (VERIFIED unless noted)

1. **The WebAssembly spec admits nondeterminism only in a few places** (`WebAssembly/design/Nondeterminism.md`):
   - NaN bit patterns and the NaN sign bit.
   - **Relaxed SIMD** (`relaxed_madd` FMA vs mul+add, `relaxed_min/max` on NaN or ±0, out-of-range swizzle; see `relaxed-simd/Overview.md:31, 172-221`).
   - Shared-memory threads.
   - Resource exhaustion, such as memory growth or stack depth.
   - Otherwise integer and IEEE float ops are deterministic.
   - Consequence: one identical `.wasm`, no relaxed SIMD (Safari only has it in Preview per BCD anyway), no threads, and no `-ffast-math`.
   - Fixed-width SIMD is deterministic but needs Safari 16.4+ (BCD). The project's build flags show no `-msimd128` or `-pthread` (`native/build-cores.sh:47-58`).
2. **Host imports in the emscripten JS glue are nondeterministic:**
   - `clock_time_get(REALTIME)` → `Date.now()` (`src/lib/libwasi.js:158-170`, `libcore.js:1453`).
   - Monotonic clocks use `performance.now()` (`libcore.js:1461-1476`).
   - Entropy uses `crypto.getRandomValues` (`libwasi.js:582-624`).
   - `localtime` uses the **viewer's time zone** via `getTimezoneOffset()` (`src/lib/libtime.js:28-31`).
   - Emscripten **removed `-sDETERMINISTIC` in 5.0.7** (2026-04-30). It only injected `src/deterministic.js`, which overrides `Math.random`, `Date.now` and `performance.now` (`ChangeLog.md:304-308`, `src/deterministic.js`). So determinism must come from the frontend.
   - The project's `fe.cpp` replaces `time`, `gettimeofday`, `clock_gettime`, `localtime`(→UTC), `mktime`, `tzset`, `random` and `srandom` with deterministic versions keyed to a server-set epoch (`native/frontend/fe.cpp:35-106, 481`).
3. **`JS_MATH` must stay off.** JS `Math.*` "may give different results as JS math is specced somewhat differently than libc, and can also vary between browsers" (`emscripten src/settings.js:635-640`; the default is false).
4. **FBNeo's own nondeterminism:**
   - `BurnGetLocalTime` (the Neo-Geo calendar chip) and `BurnRandomInit` use `time(NULL)` unless netgame mode is on (`FBNeo src/burn/burn.cpp:1752-1789, 1814-1821`).
   - In the libretro build, `is_netgame_or_recording()` returns `kNetGame` (`src/burn/burnint.h:90-97`), which starts at 0 (`libretro.cpp:55`). It is set to 1 **only when the frontend reports `RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY` during a serialize call** (`src/burner/libretro/retro_memory.cpp:288-322`), which happens after boot.
   - So a stock frontend boots each replica with a different RNG seed and RTC. The project handles this: `shim_fbneo.cpp` forces netgame mode before load, and `fe.cpp:128, 340-347` reports the netplay context.
5. **Persistent per-device data must be excluded or come from the server:** hiscore.dat (FBNeo disables it in netplay context; the project also forces `fbneo-hiscores=disabled`), NVRAM/EEPROM, and memory-card files.
6. **Core version.** "A core's savestates are only guaranteed to keep working as long as you NEVER update that core … rollback netplay with mismatching core versions can also be affected" (`libretro-docs/docs/library/fbneo.md:508-514`). Pin one `.wasm` hash for the server and all clients. The project ships `.wasm.sha256`.
7. **Core options affect state.** Frameskip, CPU speed, sample rate, low-pass filter and RAM-init pattern must be forced identically. The project forces these in `shared/systems.ts`, e.g. FCEUmm `ramstate: fill $ff`.
8. **Engine bugs and resource limits** (REPORTED):
   - iOS 18.2 broke WASM emulators with "Maximum call stack size exceeded" (WebKit bug 284752). EmulatorJS docs say iOS 18.2 to 18.3 is non-functional.
   - JSC implements f32 ops through f64 conversion (WebKit r189744). That is proven equivalent for single ops, but it shows that engines differ internally.
   - Differential testing of browser WASM engines has found disagreements.
   - **Mitigation:** run a cross-engine soak (V8/Node, JSC/WebKit, Chromium) with random inputs and compare state hashes every frame in CI. Abort the room and resync on mismatch in production.
9. **Frame-rate drift.** CPS2 runs at about 59.6 Hz (ASSUMED from general knowledge; not checked in FBNeo source here) and NES at 60.0998 Hz (FCEUmm reports `fps` 60.0998; MEASURED). Displays run at 60, 90, 120 or 144 Hz. Clients must pace emulation to the *server's* frame clock, not to `requestAnimationFrame`. This is ASSUMED to need the GGPO-style "slow down if ahead" logic (VERIFIED `timesync.cpp`).

### 5.2 Desync detection in practice

| System | Method | Interval | On mismatch |
|---|---|---|---|
| GGPO (VERIFIED) | `save_game_state` returns a checksum. Synctest mode re-simulates every frame | Every frame (synctest) | Raise a sync error (debugging tool) |
| GGRS 0.13 (VERIFIED) | Peers exchange u128 checksums of confirmed frames | Configurable interval | Event to the app |
| RetroArch (VERIFIED) | Server sends the CRC32 of the serialized state | Every 600 frames (10 s) by default | Client requests a savestate (resync) |
| Fightcade FBNeo (VERIFIED) | Checksum code only under `FBA_DEBUG` | — (release builds don't check) | — |
| This project (VERIFIED) | 64-bit FNV-1a of the full serialized state (`fe.cpp:616-626`), server → client `Bin.Hash` | `hashEvery = 120` frames in the test replica (`test/helpers/replica.ts`) | Resync via snapshot |

Hashing is cheap: 0.025 ms for a 13.7 KB NES state (MEASURED). For a state of several hundred KB, byte-wise FNV is probably 0.2 to 1 ms (ASSUMED); a 64-bit-word hash such as xxh64 would be faster if needed. Hashing every 60 to 120 frames is affordable. When a mismatch is found, the server's snapshot is authoritative.

---

## 6. Bandwidth per viewer

**Video (a)** (MEASURED here, 320x224@60, synthetic content, §1.2):
- Moderate content: 0.36 to 1.0 Mbps for about 32 to 40 dB mean Y-PSNR.
- Noisy stress content: 0.9 to 3.1 Mbps.
- That is 0.16 to 1.4 GB/hour per viewer.
- Add audio: Opus at about 32 to 96 kbps (ASSUMED typical), plus RTP/SRTP/UDP/IP overhead at 60 or more packets/s.
- Scaling 320x224 up on the server would raise this. Always send native resolution and scale on the client with nearest-neighbour or a shader.

**Input stream (b)** (computed from the project's `Bin.Frames` layout `[u8 type][u32 session][u32 firstFrame][u8 count][u8 ports] + count×ports×u16 + [u8 nacks]` (`shared/protocol.ts:58-61`) and standard header sizes; ESTIMATE):

| Ports, frames per message | Payload | WSS (TLS 1.3/TCP/IPv4) | DataChannel (DTLS/SCTP/UDP) | WebTransport datagram |
|---|---|---|---|---|
| 2 ports, 1 frame/msg (60 msg/s) | 16 B | 44 kbps | 49 kbps | 36 kbps |
| 2 ports, 2 frames/msg | 20 B | 23 kbps | 25 kbps | 19 kbps |
| 2 ports, 4 frames/msg | 28 B | 12.5 kbps | 13.6 kbps | 10 kbps |
| 4 ports, 1 frame/msg | 20 B | 46 kbps | 50 kbps | 37 kbps |

- Plus about 12 kbps of TCP ACKs upstream from each WSS viewer.
- Players should get one message per frame. Spectators can take 2 to 4 frames per message.
- Per viewer, input streams are about **10 to 100× smaller than video**. Example: 8 viewers for one hour cost about 0.1 GB, against 1.3 to 11 GB for video.
- One-time costs: a snapshot on join or resync. The NES state is 725 B deflated (MEASURED). FBNeo arcade states are unmeasured, probably tens to a few hundred KB (ASSUMED).

---

## 7. Recommendation with assumptions

Assumptions:
- Players are mostly within one region of the server (RTT 20 to 60 ms on Wi-Fi/LTE).
- Typical groups have 2 to 4 players and 0 to 20 spectators.
- Cores are FBNeo CPS1/CPS2/Neo-Geo plus a few console cores, all single-threaded WASM.
- The client device mix includes budget Android phones.

1. **Pick (b).** Run the authoritative emulator on the server (Node, the same `.wasm`). The server also produces snapshots and hashes, which enable seat handoff, late join, creator disconnect, adapters and records.
2. **Netcode v1, delay-based and server-stamped:**
   - The server finalizes frame f at its own clock and stamps arriving inputs into the earliest frame it has not finalized.
   - Each client runs B = 1 to 3 frames behind the newest authoritative frame it holds, adapting B to its measured jitter p99.
   - Expected added latency is about RTT + 17 to 50 ms.
   - Show each player their measured RTT and frame delay.
3. **Netcode v2, optional rollback for seated players:**
   - The client applies its own input locally at frame `now + D` (D = 1 by default, 0 to 2 user-selectable), predicts others by repeating their last input, and sends the input with the requested frame.
   - The server honours the requested frame if it arrives before that frame's deadline. Otherwise it moves the input later and acks the actual frame, and the client corrects by rollback.
   - Cap rollback per client by measured CPU (RetroArch-style). The GGPO-style hard cap is 8 frames. Stall or raise D beyond that.
   - Spectators never roll back.
4. **Transport:**
   - WebSocket (TLS, `setNoDelay`) everywhere for the downstream input stream, snapshots, hashes and chat.
   - Seated players' **uplink** sends every input packet with the last 2 to 4 frames of input repeated. This is cheap and helps on any transport. On TCP it does not remove HOL stalls (§3.1).
   - Upgrade path: an unordered DataChannel with `maxRetransmits: 0` to the server (no TURN needed with a public-IP server), and WebTransport datagrams where available (Chrome/WebView2/Android WebView; iOS 26.4+). Fall back to WebSocket automatically.
5. **Determinism:**
   - Keep the project's deterministic frontend (fixed epoch/UTC, no host randomness, netgame mode forced, hiscores off, forced core options).
   - Use a single pinned `.wasm` for all.
   - No SIMD (or one SIMD build for everyone, if all targets support it), no threads, no `JS_MATH`.
   - State hash every 60 to 120 frames; resync on mismatch.
   - Add a CI cross-engine soak test.
6. **Streaming:** do not build it for v1. If device benchmarks show many players cannot hold 60 fps on CPS2, add a per-client **streaming fallback**:
   - x264 `veryfast` or `ultrafast` with `zerolatency`, about 1 Mbps, native 320x224 resolution, intra-refresh.
   - The playout-delay extension with min = max = 0.
   - WebRTC where available and WebSocket + WebCodecs on Linux.
   - Budget about 1 to 2 ms of server CPU per frame per stream and 0.45 GB/hour per streamed viewer.

---

## 8. Must test on real devices

- FBNeo CPS1, CPS2 and Neo-Geo per-frame WASM time, render vs no render, on a low-end Android phone (Cortex-A55/A75 class, e.g. Helio G85 or Snapdragon 680), a mid-range Android phone, iPhone 11/13/15/16, and Telegram Desktop on Windows, macOS and Linux. This decides whether rollback is possible and which devices can be players at all.
- FBNeo `retro_serialize`/`unserialize` time and state size (raw and deflated), and hash time, per system on those devices.
- Cross-engine determinism soak (Node V8 vs iOS WKWebView JSC vs Android WebView vs WebView2 vs WebKitGTK): 30+ minutes of random inputs on each core, comparing state hashes every frame.
- RTT, jitter and loss distributions (p50/p95/p99) from Telegram webviews to the chosen server region over WebSocket and DataChannel, on Wi-Fi, 4G and 5G. Include HOL stall counts on WebSocket.
- ICE success of a DataChannel to the public-IP server inside each Telegram client, including iOS Lockdown Mode and corporate Wi-Fi.
- WebTransport availability and datagram behaviour in Telegram iOS (iOS 26.4+), Telegram Android WebView and WebView2.
- Frame pacing of 59.64 Hz and 60.1 Hz cores on 60/90/120/144 Hz displays inside Telegram webviews: rAF cadence, iOS Low Power Mode, ProMotion, audio-clock drift.
- End-to-end press-to-photon with a 240 fps camera: local emulation vs lockstep (D = 0/1/2) vs, if built, streaming with playout-delay 0/0, on one iPhone, one Android phone and one PC.
- Behaviour after backgrounding or locking: how long until resync, snapshot size, time to rejoin.

## 9. Open questions

- The real FBNeo CPS2/Neo-Geo cost in WASM on budget phones. If plain 60 fps fails, (b) needs a fallback for those players.
- Whether Safari/WKWebView negotiates and honours the `playout-delay` RTP extension. I verified only the libwebrtc code, and WebKit's build was not checked.
- Real-world loss and jitter on group members' networks, which decides whether a DataChannel or WebTransport uplink is worth building in v1.
- Whether casual groups accept rollback artifacts (teleporting sprites, missed short hit flashes; see the GGPO guide `DeveloperGuide.md:180`) or prefer a steady 2 to 3 frame delay.
- Server placement for groups spread across countries. Server relay costs (RTT_A + RTT_B)/2 instead of P2P RTT/2.
- Fightcade's actual spectator buffering and its delay auto-selection, which are closed source.

## Sources

Primary (read):
- GGPO: https://github.com/pond3r/ggpo (commit 7ddadef). Files: `doc/DeveloperGuide.md`, `src/lib/ggpo/{sync.h,timesync.*,backends/p2p.cpp,backends/spectator.*,backends/synctest.cpp}`, `src/include/ggponet.h`.
- RetroArch: https://github.com/libretro/RetroArch (master, 2026-10-08). Files: `network/netplay/netplay_frontend.c`, `network/netplay/netplay_private.h`, `config.def.h`.
- Fightcade FBNeo: `src/burner/win32/fbn_ggpo.cpp` (sibling clone, commit c959501).
- FBNeo: https://github.com/finalburnneo/FBNeo `src/burn/{burn.cpp,burnint.h,state.h}`. libretro/FBNeo: `src/burner/libretro/{libretro.cpp,retro_memory.cpp}`. libretro docs: `docs/library/fbneo.md`.
- GGRS: https://github.com/gschup/ggrs `src/sessions/p2p_session.rs` (v0.13.0).
- libwebrtc (Firefox vendored copy, updated 2026-10-06): https://github.com/mozilla-firefox/firefox/tree/main/third_party/libwebrtc. Files: `docs/native-code/rtp-hdrext/playout-delay/README.md`, `modules/video_coding/timing/timing.{h,cc}`, `video/video_receive_stream2.cc`, `video/video_stream_buffer_controller.cc`, `modules/congestion_controller/goog_cc/goog_cc_network_control.cc`.
- EmulatorJS 4.2.4: `data/src/netplay.js` (sibling clone, commit f4f0f1c).
- WebAssembly: https://github.com/WebAssembly/design/blob/main/Nondeterminism.md and https://github.com/WebAssembly/relaxed-simd/blob/main/proposals/relaxed-simd/Overview.md
- Emscripten (ChangeLog head 6.0.12, 2026-10-08): `src/settings.js`, `ChangeLog.md`, `src/deterministic.js`, `src/lib/{libwasi.js,libcore.js,libtime.js}`.
- MDN browser-compat-data 8.1.5: `api/{WebTransport,RTCDataChannel,RTCRtpReceiver,VideoDecoder}.json`, `webassembly/{fixed-width-SIMD,relaxed-SIMD,threads-and-atomics}.json`.
- Linux 7.3-rc6: `include/net/tcp.h`, `net/ipv4/tcp_output.c`. ws 8.22.0: `lib/websocket.js`.
- This project (read-only): `shared/emu/core.ts`, `shared/protocol.ts`, `shared/systems.ts`, `native/frontend/{fe.cpp,shim_fbneo.cpp}`, `native/build-cores.sh`, `native/build/build-info.json`, `test/helpers/replica.ts`.

Secondary (REPORTED):
- PCGamesN / GameStar: https://www.pcgamesn.com/nvidia/geforce-now-competitive-mode-latency
- PC Gamer: https://www.pcgamer.com/heres-how-stadias-input-lag-compares-to-native-pc-gaming/ and https://www.pcgamer.com/geforce-now-beats-stadia-in-our-input-latency-testing/
- Digital Foundry Stadia, via Windows Central: https://windowscentral.com/google-stadia-doesnt-run-destiny-2-and-red-dead-redemption-2-4k-resolution-exhibits-more-input-lag
- TU Darmstadt: https://www.kom.tu-darmstadt.de/papers/LWD+14.pdf
- Stadia/GFN/PSNow network analysis: https://arxiv.org/pdf/2012.06774
- Parsec: https://parsec.app/blog/nvidia-nvenc-outperforms-amd-vce-on-h-264-encoding-latency-in-parsec-co-op-sessions-713b9e1e048a , https://parsec.app/blog/parsec-game-streaming-total-latency-at-240-frames-per-second-c0818cc0daa5 , https://support.parsec.app/hc/en-us/articles/32381603663636-Stream-Overlay-Stats-and-Logging
- WebRTC latency breakdown: https://transitiverobotics.com/blog/webrtc-latency-breakdown/
- W3C playout delay issue: https://lists.w3.org/Archives/Public/public-webrtc/2022Nov/0032.html
- x264 zerolatency thread: https://mailman.videolan.org/pipermail/x264-devel/2010-April/007115.html
- SF6 input lag: https://www.eventhubs.com/news/2023/jun/05/input-lag-tests-sf6-platforms
- Darkstalkers ping → delay mapping: https://eventhubs.com/news/2013/jan/11/darkstalkers-resurrection-will-feature-ping-filtering-tool-online-match-making
- infil netcode explainer: https://words.infil.net/w02-netcode-p5.html
- Fighting-game latency thesis: https://www.diva-portal.org/smash/get/diva2:1322881/FULLTEXT01.pdf
- WPI CS:GO: https://web.cs.wpi.edu/~claypool/papers/csgo-net-22/paper.pdf
- "Not So Fast" (WASM vs native): https://usenix.org/conference/atc19/presentation/jangda
- UDP blocking: https://ar5iv.arxiv.org/html/1612.07816
- TURN pricing: https://developers.cloudflare.com/realtime/turn/faq/ and https://www.twilio.com/en-us/stun-turn/pricing
- TURN share: https://bloggeek.me/webrtcglossary/turn/
- Safari 26.4 WebTransport: https://www.w3.org/wiki/WebTransport/Meetings2023
- WebKit bug 284752 (iOS 18.2 WASM stack): https://bugs.webkit.org/show_bug.cgi?id=284752
- EmulatorJS player docs: https://docs.romm.app/3.8.1/Platforms-and-Players/EmulatorJS-Player
- Twitch low latency: https://www.streamscheme.com/what-is-low-latency-twitch/
- Mux latency: https://docs.mux.com/docs/live-streaming-latency
- Gaffer on Games on UDP in browsers: https://gafferongames.com/post/why_cant_i_send_udp_packets_from_a_browser/

---

## Verification

Adversarial fact-check of the 15 high-relevance claims above. Done 2026-10-09 by a separate checker, from primary sources re-fetched independently into the research workspace (not committed; sources are cited inline)verify/`. Measurements were re-run in the same container.

Verdicts:
- **CONFIRMED**: independent primary evidence agrees.
- **DOWNGRADE**: partly wrong or overstated; a corrected claim is given.
- **REFUTED**: the evidence contradicts the claim.
- **UNCHECKABLE**: no evidence reachable from here.

### Summary table

| # | Claim (short) | Verdict |
|---|---|---|
| 1 | Cloud gaming adds roughly +40 to 67 ms | **DOWNGRADE**: the measured range is wider, about +27 ms to +216 ms |
| 2 | libwebrtc low-latency path needs min 0 / max ≤ 500 ms; max comes only from the sender; render delay 10 ms | **CONFIRMED**, with one nuance (a receiver-side field trial) |
| 3 | H.264 320x224@60 synthetic: 0.36–1.0 / 0.9–3.1 Mbps; keyframes 7–51 KB | **CONFIRMED** (re-derived from the encoded files) |
| 4 | Streaming adds RTT + 15–40 ms best case | **DOWNGRADE**: the pacing term is overstated, and audio, A/V sync and the mobile decoder are missing |
| 5 | Delay-based lockstep adds RTT + 1–2 frames; rollback depth formula | **DOWNGRADE**: the formula does not fit the proposed server-deadline design |
| 6 | GGPO: >1 frame noticeable; 8-frame prediction window; 32 spectators | **CONFIRMED** |
| 7 | RetroArch: CPU-adaptive input latency; CRC32 every 600 frames; savestate on mismatch | **CONFIRMED** |
| 8 | FCEUmm WASM bench numbers | **DOWNGRADE** (minor): the numbers do not match the cited JSON; re-run values are about 5–15% higher |
| 9 | CPS2 costs 5–15× NES; budget phones 5–25 ms/frame | **UNCHECKABLE** (no lawful ROM); some supporting data added |
| 10 | HOL model: TCP 77/127 and 292/675 ms; datagrams ≤ 17 ms at p99.9 | **DOWNGRADE**: the datagram figure is 33 ms at 5% loss; the TCP tails are optimistic |
| 11 | tdesktop Linux has no WebRTC; lockstep over WS works in every client | **DOWNGRADE**: Apple Lockdown Mode disables Wasm, and Linux WebRTC is "usually absent", not guaranteed absent |
| 12 | Wasm nondeterminism list; relaxed SIMD only in Safari Preview; no threads in Android WebView | **DOWNGRADE**: relaxed SIMD ships in Chrome, Android WebView and Edge 114+ and in Firefox 146+ |
| 13 | Emscripten host nondeterminism; `-sDETERMINISTIC` removed in 5.0.7; JS_MATH | **CONFIRMED** |
| 14 | FBNeo `time(NULL)` seeding; how libretro sets kNetGame; the project's fix | **CONFIRMED**, with one nuance (a second path sets kNetGame) |
| 15 | Input stream 44/23/12.5 kbps, 10–100× less than video | **DOWNGRADE**: the per-message maths is right, but the one-time ROM and core download per viewer is left out |

### Details

**1. Cloud-gaming latency: DOWNGRADE (all REPORTED).** I could not open the primary pages: `pcgamer.com` and `kom.tu-darmstadt.de` returned CONNECT 403 from this container. I used WebSearch summaries of the same articles.
- Digital Foundry (via Windows Central) matches the claim:
  - Destiny 2: Xbox One X 100 ms vs Stadia 144 ms, which is +44 ms.
  - Shadow of the Tomb Raider (SotTR): 83 vs 139 ms, which is +56 ms.
  - The Destiny 2 pair compares a **30 fps** console with **60 fps** Stadia, so it *understates* the overhead at equal frame rates.
- GameStar: the claim's local, Stadia and GFN "balanced" numbers match. The GFN Metro figure appears as 81.1 or 81.8 ms depending on the secondary source.
  - Omitted: **GFN "Competitive" (720p120, V-Sync off) at 69.0 / 68.6 ms, only +27 to +30 ms over local.**
- PC Gamer: the PC-client 1080p numbers (+62 / +67 ms) match.
  - Omitted: Stadia on a TV at 1080p (163 / 213 ms) and 4K (167 / 279 ms), which is **+80 to +216 ms**.
- TU Darmstadt (Lampe et al. 2014, abstract): +40 to 150 ms (85–800%). That conflicts with summarising the field as "roughly 40–67".
- *Corrected:* measured cloud-gaming overhead ranges from about **+27 ms** (GFN Competitive, 120 fps, V-Sync off) to **+216 ms** (Stadia on a TV at 4K). The 1080p60 PC-client cases cluster at +40 to 67 ms, and the 2014 academic study found +40 to 150 ms. All of this is REPORTED.

**2. libwebrtc playout delay: CONFIRMED.** Source: Firefox `main` third_party/libwebrtc, vendored 2026-10-06 (base 3987dfb7b9).
- `timing.cc`:
  - Lines 36–38: `kLowLatencyStreamMaxPlayoutDelayThreshold = 500 ms`.
  - Lines 76–79: the low-latency path requires `min_playout_delay.IsZero() && max_playout_delay <= threshold`.
  - Lines 197–203: `RenderTime()` returns 0, meaning "render as soon as possible".
- `timing.h`:
  - Line 37: `kDefaultRenderDelay = 10 ms`.
  - Line 62 (new evidence): the **default `max_playout_delay` is 10 s**. Without the extension, the low-latency path is never taken.
- `video_receive_stream2.cc`:
  - Lines 808–816: `SetBaseMinimumPlayoutDelayMs`, which is the JS minimum.
  - Lines 908–912: frame min and max come from `EncodedImage().PlayoutDelay()`.
  - Lines 1218–1270: the minimum is the max of the frame, base and A/V-sync minimums, capped by the frame maximum.
- Nuance 1: the cited `video_receive_stream2.cc:796-803` does not hold that logic in the current tree. The lines that read the extension are **`rtp_video_stream_receiver2.cc:796-803`**.
- Nuance 2: those same lines show a **receiver-side field trial `WebRTC-ForcePlayoutDelay`** (lines 306–307 and 371–372) that can override the extension. Page JS cannot reach it, and a Telegram webview will not set it, so the practical conclusion stands.

**3. H.264 measurements: CONFIRMED (MEASURED, synthetic content).** I re-derived the figures from the `.mkv` files with ffprobe:
- All files are 320x224 at 60/1 with 1,200 packets.
- Bitrates: moderate CRF28 361 kbps, moderate ultrafast CRF23 1,038 kbps, noisy CRF28 935 kbps, noisy ultrafast CRF23 3,114 kbps.
- Maximum keyframes: 7,335 / 18,563 / 13,914 / 51,010 B.

Caveats:
- `-g 600` plus x264 scene-cut keyframes are included in the bitrates.
- The encodes are 4:2:0.
- CPS1/CPS2 output is **384**x224 (VERIFIED FBNeo `cps.cpp:2245`), about 20% more pixels than 320x224.
- The CBR runs drop min-frame PSNR to 17.5–24 dB.

**4. Streaming latency model: DOWNGRADE (remains ASSUMED).**
- **The pacing term (2–15 ms) is overstated for steady-state P-frames.** The libwebrtc pacer may send in bursts up to `send_burst_interval` = **40 ms** of pacing-rate budget, capped at 63 KB:
  - `api/transport/network_types.h:251` (`PacerConfig::kDefaultTimeInterval`)
  - `modules/pacing/pacing_controller.h:84-88,111-114`
  - `pacing_controller.cc:366-375`
  - So 1–10 KB P-frames at ≥1 Mbps usually leave the pacer at once. Pacing delay mainly hits frames larger than about rate × 40 ms, such as keyframes.
- Terms missing from the model:
  - **Audio.** There is Opus framing plus the NetEq jitter buffer. With max playout delay 0, the frame maximum overrides the A/V-sync minimum (`video_receive_stream2.cc:1239-1242`), so video runs ahead of audio and hit sounds lag (code VERIFIED; audio size ASSUMED). In lockstep, audio is generated locally.
  - Buffering in mobile hardware decoders (ASSUMED).
  - Browser compositing of an asynchronously arriving video frame (ASSUMED).
- With min = 0 and max > 0, libwebrtc also sets `max_composition_delay_in_frames` (`video_receive_stream2.cc:1272-1281`), which feeds Chromium's low-latency renderer (Chromium side ASSUMED). Test 0/0 against 0/X.
- *Corrected:* RTT + about 12–40 ms for **video** in the best case, with audio later unless A/V sync is accepted. This is ASSUMED and must be measured.

**5. Lockstep and rollback model: DOWNGRADE (ASSUMED).**
- The delay-based part is consistent. Added latency is about RTT + B·T, plus up to about T/2 of phase misalignment.
- The rollback formula `ceil(((RTT_A+RTT_B)/2 + jitter)/T) − D` assumes peers aligned in absolute time, as in GGPO-style time sync.
- Netcode v2 in §7 is a **server-deadline** design: the server honours a requested frame only if the input arrives before that frame's deadline. In that design:
  - Each client must lead the server by about RTT_i/2 + jitter − D·T.
  - So **R_i ≈ ceil((RTT_i + jitter_i)/T) − D, set by that player's own RTT.**
- Example: RTT 20 / 140 ms with D = 0.
  - The claim's formula gives about 5 frames for both players.
  - The server-deadline design gives about 1–2 frames for A and **about 8–9 frames for B**, which reaches or exceeds the GGPO 8-frame window.
- The two formulas agree only when the RTTs are equal.

**6. GGPO: CONFIRMED.** HEAD is `7ddadef8546a…` (git ls-remote).
- `doc/DeveloperGuide.md:178` has the "larger than 1 … single frame" quote. Line 176 covers delay vs speculative execution.
- `src/lib/ggpo/sync.h:18` has `MAX_PREDICTION_FRAMES 8`.
- `src/include/ggponet.h:33-35` sets 4 players, 8 prediction frames and 32 spectators.
- `backends/p2p.cpp:82-83` enforces the 32-spectator limit.

**7. RetroArch: CONFIRMED.** Master is `fce35fc…`.
- Adaptive input latency is at `netplay_frontend.c:8575-8607`:
  - `frames_per_frame = 16666/frame_run_time_avg`, minus 2.
  - Input latency is raised when frames_per_frame < frames_ahead and lowered when frames_per_frame > frames_ahead + 2.
  - The code carries the comment "FIXME: Using fixed 60fps".
- `frame_run_time` is timed per *replayed* frame and averaged over 120 frames (lines 4234–4285; `netplay_private.h:62`).
- The CRC check is at lines 3498–3536 and the CRC command at 6233–6295, with `DEFAULT_NETPLAY_CHECK_FRAMES 600` at `config.def.h:1615`.
- Nuance: the CRC covers the core-memory part of the savestate container (lines 2424–2439). A mismatch on the first check disables CRCs.

**8. FCEUmm WASM benchmark: DOWNGRADE (minor).**
- The cited `bench_core_fceumm.json` holds a *different* run from the claimed figures: no-render 0.52 ms, full 0.63 ms, rollback 1.76 / 2.59 / 4.29 / 5.03 ms.
- My three re-runs (same script, Node v22.22.0, same VM) gave:

| Measurement | Re-run results |
|---|---|
| Full frame | 0.56–0.61 ms |
| No-render frame | **0.47–0.50 ms** |
| Serialize | 0.016–0.018 ms |
| Unserialize | 0.024–0.025 ms |
| Hash | 0.025–0.026 ms |
| State size | 13,758 B, 725 B deflated (exact) |
| Rollback, 2 frames (mean) | 1.52–1.71 ms |
| Rollback, 4 frames (mean) | 2.43–2.94 ms |
| Rollback, 7 frames (mean) | 3.88–4.76 ms |
| Rollback, 8 frames (mean) | 4.43–4.66 ms |
| Rollback p99 | up to about 9.6 ms (shared VM noise) |

- The build facts check out: emcc 6.0.12 (`build-info.json`), and `-O3` at link time and for the frontend (`build-cores.sh:48,75`).
- The core objects use the cores' own `Makefile.libretro` flags, which I did not check.
- *Corrected:* about 0.5 ms for a no-render frame and about 0.6 ms with A/V. An 8-frame rollback costs about 4.4–5.0 ms mean, with p99 up to about 10 ms here.

**9. CPS2 cost estimate: UNCHECKABLE.** There is no lawful CPS2 ROM (see the sibling `test-roms.md`). Evidence added:
- VERIFIED in FBNeo:
  - CPS2 68000 at 11.8 MHz (`cps.cpp:2248-2251`).
  - Refresh rate 59.63 Hz (`cps.cpp:2232-2235`), which confirms the §5.1 item 9 assumption.
  - The screen is 384 pixels wide.
- REPORTED: a RetroPie forum user saw CPS1 and Neo-Geo at 35–45% CPU on a Pi 3 (Cortex-A53 at 1.2 GHz, older lr-fbalpha). That is about 6–7.5 ms/frame natively. Adding a WASM penalty, this fits the claim's 5–25 ms range for weak phones.
- VERIFIED: the project's `fbneo_cps12.wasm` is 6.1 MB, or 3.1 MB gzipped.

**10. HOL model: DOWNGRADE.**
- `hol_sim.py` re-ran **byte-identical** to `hol_sim_output.txt`, and the WebSocket figures in the claim match.
- But at 5% loss and 100 ms RTT, the datagram ×3 result is **p99.9 33.3 ms and max 150 ms, with 35 of 300k frames unrecovered** (they need a re-request). At 2% loss the max is 110–150 ms. So "at most about 17 ms at p99.9" holds only at ≤2% loss.
- The model's RTO of max(200, 2·RTT) is optimistic for Linux senders. Linux RTO = SRTT + rttvar, where rttvar ≥ `tcp_rto_min` (200 ms) (`tcp_input.c:1112-1130`, `include/net/tcp.h:879-881`, Linux 7.3-rc6). So RTO ≥ RTT + 200 ms, and real TCP tails are worse.
- Loss is modelled as Bernoulli with no jitter.

**11. Telegram Desktop Linux and "lockstep works in every client": DOWNGRADE.**
- Linux WebRTC. On `webkitglib/2.52`, `OptionsGTK.cmake:143` makes `ENABLE_WEB_RTC` an opt-in tied to `ENABLE_EXPERIMENTAL_FEATURES`. But `PeerConnectionEnabled` defaults to **true** for `USE(GSTREAMER_WEBRTC)` builds (`UnifiedWebPreferences.yaml`, 2.52). So WebRTC is *usually* absent, for example in Flatpak, per the sibling report's own verification. It is not guaranteed absent; feature-detect it.
- New: **Apple Lockdown Mode disables WebAssembly entirely.** WebKit `main` `XPCServiceEntryPoint.mm:208-220` sets `JSC::Options::useWasm() = false` and calls `ExecutableAllocator::disableJIT()`.
  - `UnifiedWebPreferences.yaml` marks these `disableInLockdownMode: true`: WebAudio, Gamepads, IndexedDB, CacheAPI, PeerConnection, WebTransport, WebCodecsVideo and WebGL.
  - So on an iPhone or Mac in Lockdown Mode, architecture (b) cannot run, and audio, gamepads and the IndexedDB ROM cache fail as well. Streaming (a) also loses WebRTC and WebCodecs.
  - REPORTED: users can exclude an app from Lockdown Mode (Apple support doc). WebKit bug 273824 reports that Wasm stays blocked even with that exclusion since iOS 17.4.
- *Corrected:* lockstep over WebSocket works in every *mainstream* client, but not under Apple Lockdown Mode. Detect Wasm and show a clear message.

**12. Wasm determinism: DOWNGRADE.**
- The `WebAssembly/design/Nondeterminism.md` list is CONFIRMED. It also lists feature-support differences and host-call values.
- BCD 8.1.5 (npm, 2026-10-08) `webassembly.relaxed-SIMD`:
  - Chrome, Edge and **Android WebView 114+**, Firefox 146+.
  - Safari "preview"; Safari iOS and webview_ios false.
  - So relaxed SIMD is **shipping on Chromium and Android WebView**, not only in Safari Preview. It must be avoided explicitly: never build with `-mrelaxed-simd`. The project's build has no SIMD flags.
- Threads: `webview_android` is false for `threads-and-atomics` and for `SharedArrayBuffer`, which confirms that part of the claim.

**13. Emscripten: CONFIRMED.** HEAD is `e1413c3…`; the ChangeLog head is 6.0.12 (10/08/26).
- `-sDETERMINISTIC` was deprecated in 5.0.6 (line 328) and removed in **5.0.7 – 04/30/26** (lines 299–308).
- `libwasi.js:158-170`: REALTIME → `_emscripten_date_now`.
- `libwasi.js:582-618`: `crypto.getRandomValues`.
- `libcore.js:1453-1476`: `Date.now` and `performance.now`.
- `getTimezoneOffset` appears in `libtime.js:28-31` (mktime) and 120–126 (localtime).
- `settings.js:635-640`: the JS_MATH warning, default false.

**14. FBNeo netgame: CONFIRMED, with one nuance.** libretro/FBNeo HEAD is `7a276b6…`, the same as the project's pin.
- `burn.cpp`:
  - Lines 1752–1789: `BurnGetLocalTime` returns a fixed 2018-06-01 in netgame mode and calls `time(NULL)` otherwise.
  - Lines 1815–1821: the seed is the constant `0x303808909313` in netgame mode and `time(NULL)` otherwise.
  - `BurnRandomInit` is called in `BurnDrvInit` (line 885).
  - The upstream finalburnneo copy of these lines is identical.
- `burnint.h:90-97`; `libretro.cpp:55` sets `kNetGame = 0`.
- `retro_memory.cpp:288-322`: `TweakScanFlags` is called only from the serialize-size, serialize and unserialize paths (lines 337, 363, 388).
- Nuance: a second path also sets kNetGame. If the frontend lacks savestate-context support, kNetGame is set from bit 2 of `GET_AUDIO_VIDEO_ENABLE` (lines 313–322).
- Project:
  - `shim_fbneo.cpp`: `fe_core_pre_load(){ kNetGame = 1; }`, called before load at `fe.cpp:506`.
  - `fe.cpp:35-106` overrides time, time zone and rand.
  - `fe.cpp:128` sets the context to ROLLBACK_NETPLAY, and `fe.cpp:340-347` reports it, with AV-enable bit 2.
  - The `time()` override alone would already make the non-netgame seed deterministic.

**15. Input-stream bandwidth: DOWNGRADE.**
- The per-message maths is right. A 16 B payload plus 2 B WebSocket header, 22 B TLS 1.3 record, 32 B TCP with timestamps and 20 B IPv4 is 92 B per message: **44.2 kbps** at 60 messages/s, 23.0 kbps at 2 frames per message and 12.5 kbps at 4.
- DataChannel overhead depends on the DTLS version and cipher: about 45–52 kbps (ASSUMED).
- Frames messages also carry 13 B of acks per accepted input change, broadcast to every viewer (`protocol.ts:58-61`, `server/session/session.ts:226-250`). This is small.
- **The comparison leaves out the one-time download per viewer.** Every client fetches the ROM and BIOS by hash and caches them in IndexedDB (`client/src/api.ts:88-130`). Raw ROM totals from the project catalogs:

| Set | Raw size |
|---|---|
| CPS1: ffight / sf2ce / dino | 3.5 / 8.2 / 8.0 MB |
| CPS2: ssf2t / sfa2 / sfa3, mvsc, vsav | 24.9 / 28.6 / 46.4 MB |
| Neo-Geo: samsho2 / mslug / rbff2 / kof98 / garou | 26.5 / 27.5 / 70.6 / 90.6 / 92.8 MB |

  - The Neo-Geo totals include about 5.8 MB of listed BIOS variants.
  - The core adds about 3.1 MB gzipped.
- Example: a 10-minute spectator at 1 Mbps of video uses about 75 MB. Under (b), the same spectator uses a CPS2 ROM of about 25–46 MB plus about 3 MB of core and about 1–3 MB of input. That is the same order of magnitude.
- *Corrected:* the 10–100× advantage holds for steady state, or once the ROM is cached. A first-time viewer pays tens of MB up front, which also delays a late join (about 20–40 s for 50–90 MB at 20 Mbps). It also puts a copy of the ROM on every viewer's device, which matters for licensing.

### Missed facts worth adding to the main report
- Apple Lockdown Mode disables Wasm and JIT, plus WebAudio, Gamepad, IndexedDB, WebRTC, WebTransport and WebCodecs (VERIFIED WebKit source). It is the main client where (b) cannot run.
- Downloading the ROM and core to every viewer is a real cost in bandwidth, join time and licensing for (b). It does not appear in §6.
- In a server-deadline design, rollback depth is set by each player's own RTT, so a 140 ms player needs about 8–9 frames.
- The libwebrtc pacer's 40 ms burst budget means pacing rarely delays small P-frames. Keyframes and intra-refresh remain the issue.
- Streaming audio: libwebrtc lets the frame's max playout delay override the A/V-sync minimum, so with 0/0, video leads audio. Lockstep has no network audio path at all.
- Best commercial data point: GFN Competitive adds only about +27–30 ms (REPORTED, GameStar).
- FBNeo CPS runs at 59.63 Hz with a 384-wide screen (VERIFIED). Pace replicas to the server's frame clock.
- Relaxed SIMD ships in Chromium and Android WebView, so a relaxed-SIMD build would diverge across CPUs. Keep it off.

Files: the research workspace (not committed; sources are cited inline)verify/{ggpo,retroarch,webrtc,emsc,wasm,bcd,fbneo,webkit,linux}/`. Re-run outputs: the scratchpad `hol_rerun.txt` was identical to the original, and the bench numbers are in this section.
