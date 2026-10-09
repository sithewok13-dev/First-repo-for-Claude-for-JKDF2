# Emulator cores for arcade fighters / beat 'em ups in WebAssembly

Research date: 2026-10-08. This report feeds the choice between (a) server-side emulation with video streaming and (b) server-authoritative deterministic lockstep, where every client runs the same WASM core and the server runs it in Node.

Each claim carries one of three labels:
- **VERIFIED**: I read the primary source text or code myself. The path and line are given.
- **REPORTED**: the claim comes from a secondary source or a search summary.
- **ASSUMED**: the claim is my own inference.

Downloads and clones used for this report are under the research workspace (not committed; sources are cited inline). The FBNeo source was read from `/home/user/libretro/fbneo`, a clone of libretro/FBNeo at commit `7a276b6b`, dated 2026-10-08. All `src/...` paths below are relative to that clone.

---

## 0. Key points for the decision

1. **FBNeo is a good fit for lockstep. Netplay is a supported use case.**
   - The libretro port sets only `RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT` (VERIFIED, `src/burner/libretro/libretro.cpp:1244-1245`).
   - libretro core-info marks it `savestate_features = "deterministic"` (VERIFIED, `core-info/fbneo_libretro.info`, and the same for the cps12 and neogeo subsets).
   - It has a dedicated netplay savestate context (VERIFIED, `src/burner/libretro/retro_memory.cpp:288-325`).
   - Fightcade, the main arcade rollback platform, is "Fightcade emulator built with FBNeo and GGPO" (VERIFIED, fightcadeorg/fightcade-fbneo `README.md` and `src/burner/win32/fbn_ggpo.cpp`).
2. **FBNeo has a "netgame" switch, `kNetGame`, that removes wall-clock and random-seed nondeterminism.**
   - With it set, the Neo Geo uPD4990A calendar gets a fixed 2018-06-01 date, and `BurnRandom` gets a constant seed. Without it, the core uses `time(NULL)` and `localtime` (VERIFIED, `src/burn/burn.cpp:1752-1823`).
   - In the libretro port, `kNetGame` is only set lazily, inside serialize calls. Game init, which seeds the RNG and the RTC, runs before that. The frontend must therefore force `kNetGame=1` before `retro_load_game`, or always start replicas from the server's post-boot savestate.
   - The project's own frontend already forces it: `fe_core_pre_load() { kNetGame = 1; }` in `telegram-arcade/native/frontend/shim_fbneo.cpp:113` (observed).
3. **Audio must be emulated identically on every replica, including the server.**
   - Sound-chip internal state advances only when sound is rendered (VERIFIED):
     - Neo Geo YM2610 updates only if `pBurnSoundOut` is set (`src/burn/drv/neogeo/neo_run.cpp:5116`).
     - The YM2610 render rate depends on the sample rate and the FM interpolation setting (`src/burn/snd/burn_ym2610.cpp:411-423`).
     - The MSM6295 step depends on `nBurnSoundRate` (`src/burn/snd/msm6295.cpp:545`).
   - The server may discard audio output, but it must not "hard-disable" audio.
   - Video can be skipped, except for drivers flagged `BDF_RUNAHEAD_DRAWSYNC`, which include Konami X-Men, Mystic Warriors, Moo Mesa and Atari Gauntlet.
4. **WebAssembly arithmetic is deterministic, with three exceptions:** NaN bit patterns, relaxed-SIMD, and shared-memory threads (VERIFIED, WebAssembly/design `Nondeterminism.md` lines 18-44).
   - The same `.wasm` in Node and in browsers removes the cross-platform savestate issue.
   - The emscripten build already disables `-ffast-math` (`FASTMATH = 0`) and uses the portable C Musashi 68000 core rather than Cyclone (VERIFIED).
   - Recommendation: do not use threads or relaxed-SIMD in the core build. Avoid relying on float NaN payloads.
5. **Licensing is the biggest non-technical constraint.**
   - These cores are non-commercial: FBNeo, Genesis Plus GX, PicoDrive, snes9x, and MAME-derived code.
   - FBNeo also forbids asking for donations, and requires that its license text be included verbatim and that source changes be published (VERIFIED, `src/license.txt:1-7`).
   - Serving the WASM to Telegram clients is redistribution. Any monetization (Stars, ads, donations) is incompatible with FBNeo, GPGX, PicoDrive and snes9x.
   - The permissive or copyleft options are FCEUmm (GPLv2), Nestopia (GPLv2), Mesen (GPLv3) and mGBA (MPL-2.0).
6. **EmulatorJS 4.3.0-pre netplay is host video streaming, not lockstep.**
   - The host captures the canvas with `captureStream(30)` and sends it over WebRTC, preferring H.264. Guests send inputs over a data channel, and their emulator is paused ("Stop local emulation for guests (they watch host's stream instead)") (VERIFIED, `emulatorjs/data/src/netplay.js:676-690, 770-830, 1105-1143`).
   - RetroArch netplay is TCP rollback (VERIFIED, libretro docs `netplay.md:35-36`), so it cannot run in a browser as-is.
   - Nostalgist.js has no netplay.
   - No existing open browser project does server-authoritative WASM lockstep, so it has to be built. The libretro primitives are all present.

---

## 1. FBNeo (FinalBurn Neo)

### 1.1 Version
- **VERIFIED.**
  - `src/burn/version.h` gives 1.0.0.03.
  - All DATs say `<version>1.0.0.03</version>`, for example `dats/FinalBurn Neo (ClrMame Pro XML, Arcade only).dat`, header lines 1-15.
  - The libretro core-info `display_version` is "v1.0.0.03", from `core-info/fbneo_libretro.info` (libretro-core-info commit `5a74858a`, 2026-09-15).
  - The libretro/FBNeo HEAD is `7a276b6bb3d5`, dated 2026-10-08.

### 1.2 License: `src/license.txt`
- **VERIFIED**, lines 1-7. Free use, modification and distribution of source and binaries, with these restrictions:
  - "You may not sell, lease, rent or otherwise seek to gain monetary profit from FB Neo".
  - "You must make public any changes you make to the source code".
  - "You must include, verbatim, the full text of this license".
  - "You may not distribute FB Neo with ROM images unless you have the legal right to distribute them".
  - "You may not ask for donations to support your work on any project that uses the FB Neo source code."
- **VERIFIED**, line 11. FBNeo is also subject to the MAME license. Lines 101-136 contain the old MAME non-commercial license ("Redistributions may not be sold, nor may they be used in a commercial product or activity"; modified redistributions must include complete source). Lines 79-87 contain the original Final Burn terms, "don't do so commercially", plus "distribute or link to the source code".
- **VERIFIED**, libretro docs `docs/library/fbneo.md:29-40`. The FBNeo team's position:
  - The libretro port "doesn't contain any GPL-licensed code".
  - On commercial libretro frontends: "we are not concerned as long as they neither redistribute FBNeo nor use it as some mean of advertisement".
- **ASSUMED.** A Telegram Mini App that serves `fbneo_*.wasm` to clients redistributes FBNeo. It therefore needs to:
  - be non-commercial, with no donations requested;
  - ship `license.txt` verbatim, for example on an "About / licenses" page and next to the wasm;
  - publish our patched FBNeo source, including any shim compiled into the core binary.
- **ASSUMED.** A GPL frontend linked into the same wasm as FBNeo would create a license conflict. Keep the FBNeo wasm free of GPL code. EmulatorJS is GPL-3.0, so do not merge its GPL glue into the FBNeo binary.

### 1.3 Emscripten build
- **VERIFIED.** `src/burner/libretro/Makefile:413-420` contains:
  ```make
  else ifeq ($(platform), emscripten)
      TARGET := $(TARGET_NAME)_libretro_$(platform).bc
      ENDIANNESS_DEFINES := -DLSB_FIRST
      INCLUDE_7Z_SUPPORT = 1
      EXTERNAL_ZLIB = 1
      STATIC_LINKING = 1
      FASTMATH = 0
  ```
  - The output is a static `.bc` archive meant to be linked into a frontend.
  - Line 9 has `USE_SPEEDHACKS=1` by default. Lines 557-564 contain the comment that "-ffast-math is actually not safe for fbneo"; emscripten sets `FASTMATH=0`.
- **VERIFIED.** Subsets come from `Makefile:65-71` and `Makefile.cps12` / `Makefile.neogeo`:
  - `SUBSET=cps12` gives target `fbneo_cps12`, with `-DBUILD_CAPCOM`, the m68k, z80, ym2151, msm6295, qsound and eeprom drivers, and `d_cps1.cpp`/`d_cps2.cpp`.
  - `SUBSET=neogeo` gives `fbneo_neogeo`, with `-DBUILD_NEOGEO`.
  - libretro docs `fbneo.md` list `SUBSET=all|neogeo|cps12`.
- **VERIFIED, observed in this environment.**
  - Logs in `/home/user/tools/*.log` show `emmake make platform=emscripten SUBSET=cps12 EXTERNAL_ZLIB=0 INCLUDE_CHD_SUPPORT=0`, built with emsdk 6.0.12.
  - The project ships linked wasm in `telegram-arcade/native/build/`:
    - `fbneo_cps12.wasm` is 6.1 MB.
    - `fbneo_neogeo.wasm` is 5.9 MB.
    - `fceumm.wasm` is 1.1 MB.
    - These are uncompressed.
  - By comparison, EmulatorJS's full FBNeo build (RetroArch plus all drivers) is `fbneo_libretro.wasm` at 43,095,367 bytes uncompressed, or 8.27 MB 7z-compressed. Source: npm `@emulatorjs/core-fbneo@4.2.3`, `fbneo-wasm.data`, built 2025-06-14 according to `reports/fbneo.json`.
  - Subsets are therefore about 7x smaller.
- **VERIFIED.** The libretro CI builds FBNeo for emscripten as a static library linked with RetroArch master (`.gitlab-ci.yml:94-96, 236-240`).
  - RetroArch's `Makefile.emscripten` links `libretro_emscripten.bc` (lines 302-309) using `-s MODULARIZE=1 -s EXPORT_ES6=1 -s ALLOW_MEMORY_GROWTH=1` (lines 188-191). It sets `HAVE_THREADS ?= 0` by default, and threads need COOP/COEP headers (lines 62-64).
- **VERIFIED.** EmulatorJS builds cores with `emsdk 3.1.74` (`ejs-build/build_env.sh`) and runs `emmake make -f Makefile platform=emscripten`, plus `EMULATORJS_THREADS=1` and `EMULATORJS_LEGACY=1` variants (`ejs-build/build.sh:76-98`).
  - It then links each `.bc` with its RetroArch fork using `build-emulatorjs.sh` (lines 360-385).
  - The FBNeo core repo is EmulatorJS/FBNeo, a fork (`core.json` inside `fbneo-wasm.data`).
- **VERIFIED.** `info->need_fullpath = true; info->block_extract = true` (`libretro.cpp:453-454`).
  - The core opens the zip by path, so the frontend must place zips in the Emscripten FS.

### 1.4 ROM sets: identification and lookup
- **VERIFIED.** DAT files are in `dats/` (ClrMamePro XML, v1.0.0.03): Arcade only, Neogeo only, Megadrive only, and others. The Arcade DAT has 8421 `<game>` entries; the Neogeo DAT has 689.
  - Each `<game name="sf2ce" ...>` lists `<rom name size crc>`.
  - Clones carry `cloneof`/`romof`, for example `sf2ceua cloneof="sf2ce"`.
  - Neo Geo games carry `romof="neogeo"`, for example `kof98` at Arcade DAT line 194127.
- **VERIFIED.** `libretro.cpp:952-1110` (`open_archive`) does the lookup:
  - It looks up up to three archives: the romset, its parent, and the BIOS, by driver short name (`BurnDrvGetZipName`).
  - `locate_archive` (lines 881-945) searches four locations: `system/fbneo/patched/` (only if patched romsets are enabled; CRC ignored), the ROM's own directory, `system/fbneo/`, and `system/`.
  - Each required ROM is matched by **CRC32** (`find_rom_by_crc`, line 823). Name matching is only a fallback in patched or romdata mode (lines 1027-1037).
  - Size mismatches are flagged as `STAT_SMALL`/`STAT_LARGE`. Missing non-optional ROMs fail the load (lines 1086-1103).
- **VERIFIED.** The CRCs used come from the zip central directory, `FileInfo.crc` (`src/burner/zipfn.cpp:129`).
  - **ASSUMED:** the server-side validator should compute real CRC32/SHA-1 of the decompressed entries rather than trust the stored header.
  - Identification recipe: zip basename = driver short name. Every non-optional `<rom>` in the DAT must have an entry with the same CRC and size, in this zip or its parent/BIOS zip.
  - The project already exposes driver metadata and ROM CRCs from the core (`telegram-arcade/native/frontend/shim_fbneo.cpp`, observed).
- **VERIFIED.** CPS-2 decryption keys are inside the game zip, for example `ssf2t.key`, 20 bytes, crc 524d608e (Arcade DAT, `ssf2t` entry, line ~180517).
  - **CPS-1 and CPS-2 need no BIOS zip.** `sf2ce` has no `romof`, and the Arcade DAT contains no `qsound` set (0 occurrences). FBNeo uses its own QSound HLE (`src/burn/drv/capcom/qs*.cpp`).
- **VERIFIED.** Neo Geo needs `neogeo.zip` (`isbios="yes"`, Arcade DAT line 228398; driver `neogeoRomDesc`, `src/burn/drv/neogeo/d_neogeo.cpp:1623-1675`).
  - Required: the selected BIOS (`sp-s3.sp1` by default), `sm1.sm1` (Z80 BIOS), `sfix.sfix` (fix-layer tiles) and `000-lo.lo` (zoom table). Other BIOS variants are `BRF_OPT`.
  - The default BIOS DIP in `neodefaultDIPList` is `0x80` for DIP 0x02, so (0x80 & 0x3f) = 0, which selects "MVS Asia/Europe ver. 6 (1 slot)" = `sp-s3.sp1` (`d_neogeo.cpp:641-648, 810-811`).
  - The core option `fbneo-neogeo-mode` can force MVS_EUR, MVS_USA, MVS_JAP, AES_EUR, AES_JAP or UNIBIOS (`retro_common.cpp:403-421`).
  - The libretro info lists `fbneo/neogeo.zip` as firmware (`core-info/fbneo_neogeo_libretro.info:35-37`).
- **VERIFIED.** There is an open-source Neo Geo BIOS for lawful testing.
  - ngdevkit's `nullbios` (LGPL-3.0) builds a complete `neogeo.zip` with `sp-s2.sp1`, `sm1.sm1`, `sfix.sfix` (zero-filled), and `000-lo.lo` generated by `zoom-rom.py` (`ngdevkit/nullbios/Makefile.in:17-74`).
  - Its CRCs will not match FBNeo's DAT. **ASSUMED:** it would need the `system/fbneo/patched/` path with `fbneo-allow-patched-romsets=enabled`, where "crcs will be ignored but sizes and names must still match" (`retro_common.cpp:265-279`), or FBNeo's romdata feature (`src/burner/libretro/romdata.cpp`). This must be tested.
  - FBNeo also lists "NeoOpen BIOS v0.1 beta" (`neopen.sp1`) as a BIOS choice (`d_neogeo.cpp:1665`).

### 1.5 Savestates
- **VERIFIED**, `retro_memory.cpp:217-412`.
  - `retro_serialize_size`, `retro_serialize` and `retro_unserialize` run `BurnAreaScan` with `ACB_FULLSCAN|ACB_READ` or `|ACB_WRITE`. The scan covers NVRAM, memory card, RAM and driver data, but not ROM (`src/burn/state.h:31-46`).
  - It also serializes the libretro frame counter `nCurrentFrame`, because "This value is sometimes used in game logic (xmen6p, ...)" (lines 270-277).
  - After unserialize, the core calls `BurnRecalcPal()`.
- **VERIFIED.** `TweakScanFlags` (lines 288-325) asks `RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT`. On `RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY` it sets `EnableHiscores=false; kNetGame=1; nAction |= ACB_NET_OPT`.
  - If the frontend does not support the context, it falls back to the `GET_AUDIO_VIDEO_ENABLE` bit 4 ("fast savestates") as a netplay hint.
  - Support is probed once in `retro_init` with `environ_cb(RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT, NULL)` (`libretro.cpp:1279-1284`). The frontend must return `true` for a NULL data pointer.
- **VERIFIED.** `ACB_NET_OPT` (`state.h:45`) is only consumed by `cps3run.cpp`. For CPS-1/2 and Neo Geo it changes nothing.
- **VERIFIED.** The only quirk is `RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT` (`libretro.cpp:1244`). The size is fixed per loaded game; there is no `CORE_VARIABLE_SIZE`.
- **ASSUMED, from code.** Rough state sizes: about 340-400 KB uncompressed for CPS-2 and about 280-330 KB for a Neo Geo cart. These states compress well.
  - CPS RAM areas (`src/burn/drv/capcom/cps_mem.cpp:463-484`): CpsRam90 0x30000 + CpsRamFF 0x10000 + CpsReg 0x100 + Z80 2×0x1000 + CpsRam708 0x10000 + EEPROM + CPU contexts.
  - Neo Geo (`neo_run.cpp:1395-1470`): NVRAM 64K + 68K RAM 64K + Z80 RAM 2K + palettes 2×8K + graphics RAM 128K + chips.
  - Measure on real sets.
- **VERIFIED.** Fightcade's GGPO save callback does the same full `BurnAreaScan` (with `ACB_FULLSCANL`) into a 16 MB static buffer. It has a 6-int header, `'GGPO'`, holding the version, detector state and scores (`fightcade-fbneo/src/burner/win32/fbn_ggpo.cpp:349-426`). The approach is proven at 60 Hz with rollbacks.

### 1.6 Determinism, wall-clock time and RTC
- **VERIFIED.** `BurnGetLocalTime` (`src/burn/burn.cpp:1752-1789`):
  - If `is_netgame_or_recording()` (= `kNetGame` under `__LIBRETRO__`, `src/burn/burnint.h:91-98`), it returns the fixed date 2018-06-01 00:00:00, or the movie's stored time when recording.
  - Otherwise it returns `time(NULL)` and `localtime()`.
- **VERIFIED.** `BurnRandomInit` (`burn.cpp:1815-1822`) uses the constant seed `0x303808909313` under netgame, otherwise `time(NULL)`.
  - It is called from `BurnDrvInit` (line 885) before the driver's `Init`.
  - The seed is serialized via `BurnRandomScan` (line 1803) in drivers that use it.
- **VERIFIED.** The Neo Geo uPD4990A RTC is set from `BurnGetLocalTime` at init (`src/burn/drv/neogeo/neo_upd4990a.cpp:52-62`). It is serialized with `SCAN_VAR(uPD4990A)` (lines 153-163) and ticks in emulated CPU cycles (`uPD4990AInit(12000000, SekTotalCycles)`, `neo_run.cpp:4202`).
- **Consequence (ASSUMED, high confidence).** The libretro port sets `kNetGame` only inside serialize calls. A stock frontend therefore boots each replica with its own local time and seed. Two ways to make it deterministic:
  1. Set `kNetGame=1` before `retro_load_game`. This is a one-line hook; the project's `shim_fbneo.cpp:113` does it.
  2. Start every replica from the server's savestate taken after boot. That state carries the RTC and the RNG seed.
  - Do both.
- **VERIFIED.** Other direct `time(NULL)` users ignore `kNetGame`: `d_seta.cpp` msm6242 (11650-11715), SNES `epsonrtc.cpp` (316-489), GBA RTC (`gba.h:294`). Treat those drivers as non-lockstep-safe. None are in the cps12 or neogeo subsets.
- **VERIFIED.** C `rand()` is used in about 20 driver, device and sound files.
  - Examples: `d_gaiden.cpp:1263-1352`, which is render-only noise, and `d_metro.cpp:4038` for kokushi RAM init.
  - No use was found in `drv/capcom`, `drv/neogeo`, m68k, z80, `qs*` or the fm cores.
- **VERIFIED.** The project frontend returns `RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY` and AV-enable `1|2|4` (`telegram-arcade/native/frontend/fe.cpp:128, 340-347`, observed).

### 1.7 Audio and video gating
- **VERIFIED**, `libretro.cpp:1386-1424` and 1476-1477.
  - `pBurnDraw = bEnableVideo && !bSkipFrame ? pVidImage : NULL`.
  - `pBurnSoundOut = bEmulateAudio ? pAudBuffer : NULL`.
  - Video is disabled only when the frontend clears AV bit 1 and the driver lacks `BDF_RUNAHEAD_DRAWSYNC`.
  - Audio emulation stops only on bit 8, "Hard Disable Audio". Bit 2 only controls presenting audio.
- **VERIFIED.** Drivers flagged `BDF_RUNAHEAD_DRAWSYNC` (`burn.h:558`), where rendering affects emulation state:
  - `konami/d_xmen.cpp` (xmen6p, xmen6pu);
  - `konami/d_mystwarr.cpp` (5 entries);
  - `konami/d_moo.cpp` (5);
  - `atari/d_gauntlet.cpp` (28);
  - `taito/d_taitosj.cpp` (29);
  - `sega/d_turbo.cpp` (12);
  - `taito/d_crbaloon.cpp` (2).
  - Frameskip can still null `pBurnDraw` for these drivers, so pin frameskip to 0.
- **VERIFIED.** Sound-chip state depends on rendering:
  - Neo Geo `BurnYM2610Update` runs only if `pBurnSoundOut` (`neo_run.cpp:5116-5117`).
  - The YM2610 internal rate depends on `nFMInterpolation` and `nBurnSoundRate` (`burn_ym2610.cpp:411-423`).
  - ADPCM "arrived end" flags are set during stream calculation (`fm.c:2762`).
  - The MSM6295 step size is `(nSamplerate<<12)/nBurnSoundRate` (`msm6295.cpp:545`).
  - CPS-2 QSound renders only if `pBurnSoundOut` (`qs.cpp:95`).
- **ASSUMED.** A replica that disables audio, or uses a different sample rate or interpolation, will diverge in sound-chip state. Status bits readable by sound CPUs can then change behaviour. At minimum, full-state hashes will disagree.
  - The server must render audio with the same settings and discard the output.
  - Hashing only main RAM (`retro_get_memory_data`) would hide sound-side drift. Hashing the full serialize output catches it.

### 1.8 Frame timing and audio
- **VERIFIED.**
  - CPS-1 and CPS-2 use `BurnSetRefreshRate(59.63)` (`src/burn/drv/capcom/cps.cpp:2231-2236`).
  - Neo Geo uses `NEO_HREFRESH 15625.0` and `NEO_VREFRESH (NEO_HREFRESH/264.0)`, which is 59.1856 Hz (`neo_run.cpp:85-87, 3910`).
  - `nBurnFPS = (INT32)(100.0 * rate)`, giving 5963 and 5918 (`burn.cpp:1085`).
  - The libretro timing is `fps = nBurnFPS/100.0` and `sample_rate = fps * nAudSegLen` (`libretro.cpp:1575`).
  - `nAudSegLen = (sample_rate*100 + fps/2)/fps` (`libretro.cpp:1660`).
  - At 48 kHz this gives a fixed **805 samples/frame for CPS (48002.15 Hz effective)** and **811 samples/frame for Neo Geo (47994.98 Hz)**. That is my arithmetic from the verified formulas.
  - A constant per-frame audio block suits lockstep. Clients resample to device rate.
- **VERIFIED.** `fbneo-force-60hz` overrides the rate with the monitor's refresh (`RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE`, `retro_common.cpp:1610-1623`; `burn.cpp:1079-1083`).
  - CPS CPU cycles per frame derive from `nBurnFPS`, `nCPS68KClockspeed*100/nBurnFPS` (`cps.cpp:2258`), so this option changes emulation. Keep it **disabled** everywhere.

### 1.9 Core options that matter for lockstep
All option keys and defaults are VERIFIED in `src/burner/libretro/retro_common.cpp:86-440`. The "Pin to" column is ASSUMED unless noted.

| Option key | Default | Effect | Pin to |
|---|---|---|---|
| `fbneo-force-60hz` | disabled | Changes nBurnFPS, cycles per frame, and reads the host refresh rate | disabled |
| `fbneo-cpu-speed-adjust` | 100% | `nBurnCPUSpeedAdjust` scales CPS cycles (`cps_run.cpp:359,446`) | 100% |
| `fbneo-samplerate` | 48000 | Sound-chip state (see 1.7) | identical, e.g. 48000 |
| `fbneo-sample-interpolation`, `fbneo-fm-interpolation` | 4-point | YM2610 internal render rate | identical (explicit) |
| `fbneo-lowpass-filter` | disabled | Output DSP only (`DspDo`) | disabled |
| `fbneo-hiscores` | enabled | Writes hiscore.dat tables into RAM; disabled in netplay context (`retro_memory.cpp:290-291, 306`) | disabled |
| `fbneo-allow-patched-romsets` | **enabled** | Lets `system/fbneo/patched/` override CRC checks | disabled (enabled only for test BIOS) |
| `fbneo-diagnostic-input` | **Hold Start** | Frontend-side hold counter `nDiagInputHoldCounter`, serialized only for runahead (`retro_memory.cpp:281-283`, `retro_input.cpp:3015-3055`). A late joiner can diverge. | None |
| `fbneo-fixed-frameskip`, `fbneo-frameskip-type` | 0 / disabled | Nulls `pBurnDraw` (matters for DRAWSYNC drivers) | 0 / disabled |
| `fbneo-socd` | 3 | In-core SOCD cleaner; its state is serialized (`joyprocess.h:123-136`, `cps_rw.cpp:108`) | identical (project uses 0 and cleans server-side) |
| `fbneo-neogeo-mode` | DIPSWITCH | Selects the BIOS | fixed, e.g. MVS_EUR |
| `fbneo-memcard-mode` | disabled | Neo Geo memory card file I/O | disabled |
| `fbneo-dipswitch-*`, `fbneo-cheat-*`, IPS and romdata options | per game | Change machine config or RAM | identical; cheats off |
| `fbneo-cyclone` | not built for wasm | ARM asm 68k; "breaks savestates cross-platform compatibility (including netplay)" (`retro_common.cpp:73-78`) | n/a |

- **VERIFIED.** RetroAchievements independently disallows `fbneo-allow-patched-romsets=enabled`, `fbneo-cheat-*`, `fbneo-cpu-speed-adjust` below 100%, and UniBIOS for achievements (`rcheevos/src/rc_libretro.c:72-77`). This confirms the dip and cheat option key prefixes.
- **Observed.** The project's pinned set is in `telegram-arcade/shared/systems.ts:36-46`. It does not yet pin `fbneo-force-60hz`, `fbneo-sample-interpolation` or `fbneo-fm-interpolation` explicitly. Their defaults are deterministic and identical with one wasm. ASSUMED: pinning them explicitly is cheap insurance.

### 1.10 Files the core reads or writes outside the ROM
- **VERIFIED.**
  - It loads `<save>/fbneo/<game>.fs` (NVRAM) after init (`libretro.cpp:2195-2210`).
  - EEPROM games read `<save>/fbneo/<game>.nv` (`src/burn/devices/eeprom.cpp:97`). CPS-2 uses an EEPROM.
  - It reads hiscore.dat, cheats, IPS, romdata and samples from `system/fbneo/...` (`libretro.cpp:1900-1950`).
- **ASSUMED.** Give each session an empty in-memory save and system directory, holding only the validated ROM and BIOS zips. The server alone owns NVRAM and EEPROM and ships them inside savestates. Otherwise client-local settings files make replicas boot differently.

### 1.11 Netplay pedigree
- **VERIFIED.** fightcadeorg/fightcade-fbneo (HEAD `c9595014`, 2025-06-22):
  - The README says "Fightcade emulator built with FBNeo and GGPO".
  - `fbn_ggpo.cpp:480-500` sets `kNetGame = 1; bForce60Hz = 0;` for sessions, and uses GGPO callbacks for begin, save, load and advance frame (lines 197-450, 508-514).
  - It supports a synctest mode (`ggpo_start_synctest`, line 557) and spectator streaming (`ggpo_start_streaming`, line 566).
- **VERIFIED.** Fightcade "detectors" are per-game `detector\<game>.inf` files. Each line is `target=name,area,op,0xPTR,value,bits`, with targets `start`, `player1`, `player2`, `char1` and `char2`.
  - `area` is a FBNeo BurnArea name such as `CpsRamFF`, and the offset is into that area (`fightcade-fbneo/src/intf/video/win32/vid_overlay.cpp:192-220, 333-383`).
  - A ranked match reports winner and score to the server (lines 735-743).
  - This is the same design our match-result adapters need. The .inf files themselves are not in the repo.
- **VERIFIED.** The libretro docs list FBNeo Netplay as ✔ (`docs/library/fbneo.md:93`).
  - RetroArch blocks netplay and runahead for cores below the `deterministic` savestate level (`RetroArch/core_info.h:33-43`).

### 1.12 CPU cost
- No primary measurement is available here; the ROMs needed for benchmarking are not present.
- **REPORTED:** a 2019 USENIX ATC paper found WebAssembly 45-55% slower than native on SPEC on average, up to 2.5x (Jangda et al., "Not So Fast"). This is dated.
- **ASSUMED:**
  - CPS-2 is a 68000 at about 11.8 MHz plus a Z80 plus QSound HLE. Neo Geo is a 68000 at 12 MHz plus a Z80 plus the YM2610.
  - These are among the lighter FBNeo targets. A replica should fit well inside a 16.7 ms frame on 2020+ phones in WASM single-threaded.
  - A Node server should run many sessions per core.
  - Measure ms/frame on low-end Android and on iPhone Telegram WebViews before committing.
- **REPORTED:** EmulatorJS on iOS 18.2-18.3 failed with "RangeError: Maximum call stack size exceeded" in WebAssembly (WebKit bug 284752, now "RESOLVED CONFIGURATION CHANGED"). The RomM docs say it was fixed in iOS 18.4. Test the iOS Telegram WebView, which uses WKWebView, with deep emulator call stacks.

---

## 2. libretro API essentials for a minimal frontend (FBNeo specifics)

- **VERIFIED.** The core API is in `libretro-common/include/libretro.h:7834-8294`:
  - `retro_set_environment`, `retro_set_video_refresh`, `retro_set_audio_sample_batch`, `retro_set_input_poll`, `retro_set_input_state`;
  - `retro_init`, `retro_load_game(const retro_game_info*)`, `retro_get_system_av_info`, `retro_set_controller_port_device`;
  - `retro_run`, `retro_reset`;
  - `retro_serialize_size`, `retro_serialize`, `retro_unserialize`;
  - `retro_get_memory_data` / `retro_get_memory_size`, with `RETRO_MEMORY_SYSTEM_RAM = 2` (line 518).
  - The input callback is `int16_t input_state(port, device, index, id)`. `RETRO_DEVICE_ID_JOYPAD_MASK = 256` (line 380) returns all 16 buttons at once when `GET_INPUT_BITMASKS` is supported.
- **VERIFIED.** Environment calls FBNeo makes (grep of `src/burner/libretro/*.cpp`):
  - Variables: `GET_VARIABLE` (×59), `SET_CORE_OPTIONS_V2`.
  - Directories: `GET_SYSTEM_DIRECTORY`, `GET_SAVE_DIRECTORY`.
  - Display: `SET_PIXEL_FORMAT`, `SET_GEOMETRY`, `SET_ROTATION`.
  - Input: `SET_CONTROLLER_INFO`, `SET_INPUT_DESCRIPTORS`, `GET_INPUT_BITMASKS`.
  - Savestates: `GET_SAVESTATE_CONTEXT`, `GET_AUDIO_VIDEO_ENABLE`, `SET_SERIALIZATION_QUIRKS`.
  - Other: `SET_MEMORY_MAPS`, `GET_TARGET_REFRESH_RATE`, `GET_VFS_INTERFACE`, `SET_AUDIO_BUFFER_STATUS_CALLBACK`.
  - If no system dir is given, the core uses the ROM dir (`libretro.cpp:1912-1920`).
- **VERIFIED.** Input:
  - `InputMake` calls `poll_cb()` then reads per-port state (`retro_input.cpp:3359-3400`).
  - `MAX_PLAYERS 6` ("xmen6p", `retro_input.h:29`).
  - Port device types are Classic, Modern, 6-Button Panel, mouse, pointer, lightgun and others (`retro_input.cpp:3137-3147`). The device type changes the button mapping.
  - **ASSUMED:** fix port device types identically on all replicas. Lockstep should carry the canonical RetroPad bitmask per port per frame, mapped client-side from touch, gamepad or keyboard.
- **VERIFIED.** Memory exposure:
  - `retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)` returns a driver area found by name in `CheevosInit` (`retro_memory.cpp:14-215`):
    - CPS-1/CPS-2: `"CpsRamFF"`, the 64 KB 68K work RAM at 0xFF0000-0xFFFFFF (`cps_mem.cpp:27, 407, 470`).
    - CPS-3: `"Main RAM"`.
    - Neo Geo and PGM: `"68K RAM"`, the 64 KB at 0x100000 (`neo_run.cpp:1420, 3968`).
    - Psikyo, Cave and Toaplan: named areas.
    - Everything else: `"All Ram"`/`"All RAM"`. That is the driver's concatenated `RamStart..RamEnd` block (for example `d_ddragon.cpp:1585`, `sys16_run.cpp:3733-3735`, and tmnt, xmen, simpsons, vendetta, mystwarr, rohga, ddragon3, taitof2, m92). Its layout is driver-specific, not CPU addresses.
  - Mega Drive, NES and NGP publish `SET_MEMORY_MAPS` descriptors instead, for example MD RAM at 0xFF0000.
- **VERIFIED.** 68000 RAM is stored as host-endian 16-bit words. Byte reads XOR the address with 1 (`src/cpu/m68000_intf.cpp:250-262`, `a ^= 1`).
  - **ASSUMED:** the byte at 68K address A in CPS work RAM is `ram[(A-0xFF0000)^1]`, and similarly for Neo Geo, `ram[(A-0x100000)^1]`.
  - stable-retro's FBNeo config uses the overlay `["=", ">", 2]` (`stable-retro/cores/fbneo.json`), which is consistent with this.
- **VERIFIED.** FBNeo's cheat and hiscore engines read and write through the CPU bus (`cpu_core_config` read/write; `src/burn/cheat.cpp:28-92`, `hiscore.cpp:552-610`), using CPU addresses.
  - `retro_cheat_set` is a no-op in the libretro port (`libretro.cpp:1532`). Cheats are applied via `fbneo-cheat-*` core options.
- **VERIFIED.** RetroArch netplay design for reference (`docs/development/retroarch/netplay.md:3-80`):
  - It needs a deterministic core, gamepad/analog-only input, and identical core and content.
  - The server is canonical. It gives joining clients a frame count plus a serialized savestate.
  - Only the server may reset or load state. Spectators send no input.
  - It runs over TCP. This maps directly onto the server-authoritative lockstep design.

---

## 3. Console cores

| Core | License (VERIFIED file) | libretro `savestate_features` (VERIFIED) | libretro docs "Netplay" (VERIFIED) |
|---|---|---|---|
| Genesis Plus GX | Non-commercial, MAME-style. "Redistributions may not be sold, nor ... used in a commercial product or activity"; modified redistributions need full source (`libretro/Genesis-Plus-GX/LICENSE.txt:1-37`). Bundled parts: Nuked OPN2 LGPL-2.1 and others. | deterministic (v1.7.4) | ✔ |
| PicoDrive | Same MAME-style non-commercial text (`libretro/picodrive/COPYING:1-30`); info says "MAME" | deterministic | ✔ |
| snes9x | Non-commercial: "Permission to use, copy, modify and/or distribute Snes9x ... for non-commercial purposes"; "freeware for PERSONAL USE only" (`snes9xgit/snes9x/LICENSE:171-184`) | deterministic (1.61) | ✔ |
| snes9x2010 | Non-commercial | deterministic | n/a |
| FCEUmm | GPLv2 (`libretro-fceumm/Copying`, local clone `7a542dab`) | deterministic | ✔ |
| Nestopia | GPLv2 (`libretro/nestopia/COPYING`) | deterministic | ✔ |
| Mesen (NES) | GPLv3 (`SourMesen/Mesen/LICENSE`) | deterministic | ✔ |
| Mesen-S | GPLv3 | (not set in info) | ✔ |
| mGBA | MPL-2.0 (`libretro/mgba/LICENSE`) | deterministic | **✕** |
| bsnes | GPLv3 | serialized (not deterministic) | – |
| MAME 2003-Plus | MAME non-commercial | basic | – |

Sources: `core-info/*.info` (libretro-core-info `5a74858a`) and libretro docs `docs/library/*.md` Features tables (libretro/docs `4a0c09f2`, 2026-10-08).

- **ASSUMED.** GPL cores (FCEUmm, Nestopia, Mesen) linked into a wasm with our frontend glue require that glue to be GPL-compatible, and require offering source.
- **ASSUMED.** The non-commercial cores (GPGX, PicoDrive, snes9x) carry the same no-monetization constraint as FBNeo.
- **ASSUMED.** For Genesis, PicoDrive is the alternative to GPGX, but both are non-commercial. No permissively licensed Genesis core of comparable quality was identified.

---

## 4. Existing browser emulator projects

### EmulatorJS
EmulatorJS/EmulatorJS HEAD `f4f0f1c7`, 2026-09-19. `package.json` version 4.2.4. CHANGES.md top section is "4.3.0-pre".
- **License:** GPL-3.0 (VERIFIED, `LICENSE` and `package.json`).
- **Netplay (VERIFIED, `data/src/netplay.js`):**
  - Header: "WebRTC-based multiplayer ... video/audio streaming, and input sync" (lines 1-7).
  - The host captures `emuCanvas.captureStream(30)` plus audio into a MediaStream, adds the tracks to RTCPeerConnections and prefers H.264 (lines 770-830, 1140-1150).
  - Guests: `freezeGuest()`, "Stop local emulation for guests (they watch host's stream instead)" (676-690). They render remote tracks (`pc.ontrack`, 1223-1260).
  - Guest inputs arrive at the host on a data channel `"inputs"` (`ordered:true, priority:"high"`) and are applied with `simulateInput` at the host's current frame (1105-1125, 1578-1588).
  - Signaling is via socket.io (`webrtc-signal`, `open-room`, `join-room`).
  - CHANGES.md line 32: "Added netplay over WebRTC" (4.3.0-pre).
  - This is architecture (a) with a browser as host. It is not lockstep.
- **npm (VERIFIED, registry.npmjs.org):**
  - `@emulatorjs/emulatorjs` latest 4.2.3, GPL-3.0.
  - `@emulatorjs/cores` 4.2.3.
  - `@emulatorjs/core-fbneo` 4.2.3, license field `src/license.txt`, repository EmulatorJS/FBNeo. Its `.data` files are 7z archives containing `fbneo_libretro.js`, `.wasm`, `core.json`, `build.json` and `license.txt`.
  - The JS glue exports EmulatorJS-RetroArch functions: `_simulate_input`, `_get_current_frame_count`, `_load_state`, `_save_state_info`, `_set_cheat`, `_toggleMainLoop` and others.

### Nostalgist.js
npm `nostalgist` 0.22.0, MIT, modified 2026-08-30.
- **VERIFIED.** It loads RetroArch emscripten builds from `cdn.jsdelivr.net/gh/arianrhodsandlot/retroarch-emscripten-build@v1.22.2/retroarch/<core>_libretro.zip` (`dist/nostalgist.js:723-732`).
- Its API includes `saveState`, `loadState`, `press`, `pressDown` and `pressUp` (`dist/nostalgist.d.ts:3185-3600`).
- It has no netplay implementation. It only nulls the RetroArch netplay hotkeys (`nostalgist.js:701-702`).
- It is a possible reference for a JS shell around RetroArch, but it adds the RetroArch GPL layer.

### RetroArch web player
- **VERIFIED.** `Makefile.emscripten` sets `HAVE_NETWORKING ?= 1`, `HAVE_NETPLAYDISCOVERY ?= 0`, `HAVE_THREADS ?= 0` (threads need COOP/COEP), and `HAVE_RUNAHEAD ?= 1` (lines 15-66).
- RetroArch netplay is TCP-based (docs `netplay.md:35`).
- **ASSUMED:** it cannot connect browser to browser without a socket proxy, so it is not usable for our case.

---

## 5. Game RAM data sources for adapters (results, health, game over, score)

| Source | What it gives | Address space | License | Status |
|---|---|---|---|---|
| **FBNeo `metadata/hiscore.dat`** (18,827 lines, in libretro/FBNeo) | Hiscore table ranges per set: `@:maincpu,program,<addr>,<len>,<start>,<end>`. Examples: the sf2ce group's first line is `@:maincpu,program,ffd276,28,00,20`; ffight is `ff850c` / `ff80a0`. These are high-score tables, **not live score or health**. Fighters such as kof98 are often absent. | CPU program addresses | No license statement in the file ("Based on the Unofficial hiscore.dat file from highscore.mameworld.info", lines 18-21) | VERIFIED |
| **FBNeo-cheats** (finalburnneo/FBNeo-cheats, `cheats/*.ini`, 3,420 files) | Named RAM pokes, for example sf2ce "Infinite Energy PL1" `0xFF83E9=0x90` and `0xFF857B=0x90`, and "Infinite Time" `0xFF8ABE`. These give health and timer addresses. kof98 has "Infinite Energy PL1/PL2". | CPU addresses | **No LICENSE file** in the repo (tree has only README.md and cheats/) | VERIFIED |
| **Farama stable-retro** (`stable_retro/data/stable/*/data.json`, HEAD `fd956c76`, 2026-09-17) | Named variables with types (see examples below) | Genesis: 68K addresses (rambase 0xFF0000, `cores/genesis.json`). FBNeo: offsets into `RETRO_MEMORY_SYSTEM_RAM` | **MIT** (`LICENSE`, `LICENSES.md`: "Copyright (c) 2017-2018 OpenAI") | VERIFIED |
| **RetroAchievements code notes** | Per-game memory notes. rcheevos fetches them with `dorequest.php` `r=codenotes2&g=<id>` and appends no user credentials (`rcheevos/src/rapi/rc_api_editor.c:13-35`). | Arcade has no console memory regions, so offsets are into `RETRO_MEMORY_SYSTEM_RAM` (`rc_libretro.c:497-515, 727-744`; `consoleinfo.c` default `rc_memory_regions_none`) | Terms of use for bulk reuse unknown | VERIFIED (fetch path); data not fetched |
| **Fightcade detectors** | Per-game start, player1/2 win and character detection | BurnArea name + offset (for example `CpsRamFF`) | Not published in the repo | VERIFIED (format only) |

stable-retro examples (VERIFIED, `data.json` files):
- **StreetsOfRage2-Genesis:**
  - `lives` at 0xFFEF83 `|i1`; `score` at 0xFFEF95 `>d4`.
  - The scenario is done when lives == -1.
- **StreetFighterIISpecialChampionEdition-Genesis:**
  - `health` at 0xFF8042 `>i2`; `enemy_health` at 0xFF82C2 `>i2`.
  - `matches_won` at 0xFF81DA `|u1`; `enemy_matches_won` at 0xFF8457 `>u4`.
  - `continuetimer` at 0xFF81D5; `score` at 0xFF81E8 `>d4`.
- **MortalKombatII-Genesis:**
  - `health` at 0xFFB623; `enemy_health` at 0xFFB713.
  - `rounds_won` at 0xFFEEA9; `enemy_rounds_won` at 0xFFEEAB.
  - Also x/y positions.
- **MortalKombat2-Arcade** (`metadata.json`: `"system": "FBNeo"`, `"original_rom_name": "mk2.zip"`):
  - `p1_health` at 0xBCA0 `>u2`; `p2_health` at 0xBC88; `round` at 0xC38E. These are offsets in FBNeo system RAM.
- Also present: FatalFury2-Genesis, StreetsOfRage/3, GoldenAxe/III, DoubleDragon, FinalFight-Snes 1/2/3, Battletoads, CaptainCommando-Snes and others.
- Integration counts: about 676 Genesis, 596 NES and 368 SNES files under stable/, and 2 Arcade files (one integration).
- Each integration also has `rom.sha` (SHA-1 of the ROM) and `.state` savestates. The `.state` files are made with stable-retro's cores and may not be portable.

**ASSUMED.**
- For CPS-1/2 and Neo Geo arcade adapters, combine FBNeo-cheats addresses (CPU space) with the word-swap rule to read from `retro_get_memory_data`. Validate each address against recorded gameplay.
- Alternatively, add a small exported `read_cpu_byte(addr)` to the FBNeo shim using the cheat `cpu_core_config`. Bus reads of I/O ranges could have side effects, so restrict reads to RAM.
- Because these RAM reads are deterministic, the server can run adapters alone on its authoritative replica. Clients never need them.

---

## 6. WebAssembly determinism
- **VERIFIED.** WebAssembly/design `Nondeterminism.md:15-44` lists all specified nondeterminism:
  - feature support differences;
  - host call ordering and inputs;
  - shared-memory threads;
  - NaN bit patterns and sign;
  - relaxed SIMD;
  - resource exhaustion.
- **ASSUMED.** Single-threaded, non-relaxed-SIMD FBNeo/FCEUmm wasm, fed identical inputs, options and initial state, is bit-identical across V8 (Chrome, Android WebView, Node), JavaScriptCore (iOS WKWebView) and SpiderMonkey. The remaining risks are:
  - NaN payloads observed by emulated code;
  - host-call ordering (time, random, FS) leaking in;
  - stack-depth or memory exhaustion on iOS.
  - Periodic state hashes catch all of these.

---

## 7. Notes on the current project build (observed, not verified as correct)
- `telegram-arcade/native/build/build-info.json` records:
  - emcc 6.0.12;
  - fbneo `7a276b6b`;
  - fceumm `7a542dab`.
- `shim_fbneo.cpp` sets `kNetGame=1` before load and exports driver and ROM metadata.
- `fe.cpp` answers `GET_SAVESTATE_CONTEXT = ROLLBACK_NETPLAY` and AV enable `1|2|4`, which keeps audio emulated.
- `shared/systems.ts` pins the FBNeo options listed in section 1.9.
- Suggested additions:
  - pin `fbneo-force-60hz=disabled` and both interpolation options;
  - never set AV bit 8 on the server;
  - keep `DRAWSYNC` drivers rendering on the server.

---

## Sources

FBNeo, libretro and RetroArch:
- libretro/FBNeo `7a276b6b`, local clone `/home/user/libretro/fbneo`: `src/license.txt`, `src/burner/libretro/{Makefile,Makefile.cps12,Makefile.neogeo,libretro.cpp,retro_common.cpp,retro_memory.cpp,retro_input.cpp,retro_input.h,romdata.cpp}`, `src/burn/{burn.cpp,burn.h,burnint.h,state.h,version.h,cheat.cpp,hiscore.cpp}`, `src/burn/drv/capcom/{cps.cpp,cps_mem.cpp,cps_run.cpp,cps_rw.cpp,qs.cpp}`, `src/burn/drv/neogeo/{neo_run.cpp,neo_upd4990a.cpp,d_neogeo.cpp}`, `src/burn/snd/{msm6295.cpp,burn_ym2610.cpp,fm.c}`, `src/cpu/m68000_intf.cpp`, `src/burner/zipfn.cpp`, `dats/*.dat`, `metadata/hiscore.dat`, `.gitlab-ci.yml`.
- libretro/libretro-core-info `5a74858a`: `fbneo*_libretro.info`, `genesis_plus_gx`, `picodrive`, `snes9x`, `fceumm`, `nestopia`, `mesen`, `mgba`, `bsnes`, `mame2003_plus`.
- libretro/docs `4a0c09f2`: `docs/development/retroarch/netplay.md`, `docs/development/cores/developing-cores.md`, `docs/library/{fbneo,genesis_plus_gx,picodrive,snes9x,fceumm,nestopia,mesen,mesen-s,mgba,bsnes}.md`.
- libretro.h (copy in the FBNeo tree, `src/burner/libretro/libretro-common/include/libretro.h`).
- RetroArch master: `Makefile.emscripten`, `core_info.h`, `core_info.c`, `dist-scripts/dist-cores.sh`.

Fightcade:
- fightcadeorg/fightcade-fbneo `c9595014`: `README.md`, `src/burner/win32/fbn_ggpo.cpp`, `src/intf/video/win32/vid_overlay.cpp`, `src/burn/state.h`.

Browser emulator projects:
- EmulatorJS/EmulatorJS `f4f0f1c7`: `data/src/netplay.js`, `CHANGES.md`, `LICENSE`, `package.json`.
- EmulatorJS/build `7f4d2d73`: `build.sh`, `build_env.sh`, `cores.json`.
- npm: `@emulatorjs/core-fbneo@4.2.3` (tarball inspected), `@emulatorjs/cores`, `@emulatorjs/emulatorjs`, `nostalgist@0.22.0` (tarball inspected).

RAM data sources:
- Farama-Foundation/stable-retro `fd956c76`: `LICENSE`, `LICENSES.md`, `cores/{fbneo,genesis}.json`, `src/emulator.cpp`, `stable_retro/data/stable/{StreetsOfRage2-Genesis-v0,StreetFighterIISpecialChampionEdition-Genesis-v0,MortalKombat2-Arcade-v0,MortalKombatII-Genesis-v0,FatalFury2-Genesis-v0}/*`.
- finalburnneo/FBNeo-cheats `084b13ed`: `cheats/sf2ce.ini`, `cheats/kof98.ini`.
- RetroAchievements/rcheevos master: `src/rcheevos/consoleinfo.c`, `src/rc_libretro.c`, `src/rapi/rc_api_editor.c`.

Licenses, BIOS and WebAssembly:
- License files fetched via raw.githubusercontent: libretro/Genesis-Plus-GX `LICENSE.txt`, libretro/picodrive `COPYING`, snes9xgit/snes9x `LICENSE`, libretro/nestopia `COPYING`, libretro/mgba `LICENSE`, SourMesen/Mesen and Mesen2 `LICENSE`.
- dciabrin/ngdevkit `b36a345d`: `README.md`, `nullbios/Makefile.in`.
- WebAssembly/design `Nondeterminism.md`.

REPORTED (search only):
- Jangda et al., USENIX ATC 2019, "Not So Fast" (https://www.usenix.org/conference/atc19/presentation/jangda).
- WebKit bug 284752 (https://bugs.webkit.org/show_bug.cgi?id=284752).
- RomM EmulatorJS player docs (https://docs.romm.app/4.2.0/Platforms-and-Players/EmulatorJS-Player/).
- neogeodev NeopenBios page (https://www.neogeodev.org/neopenbios.html).

---

## Verification
Adversarial fact-check, 2026-10-09. I re-opened every high-relevance claim from primary sources. These are the sources I used:
- libretro/FBNeo `7a276b6b` (local clone; its `src/license.txt` is byte-identical to upstream finalburnneo/FBNeo `master`);
- the MAME license file at mamedev/mame `5cef4e1f` (`docs/mamelicense.txt`);
- libretro-docs `4a0c09f2`;
- RetroArch `master` (`core_info.h`, `core_info.c`, `runahead.c`, `network/netplay/netplay_frontend.c`, fetched raw);
- libretro-core-info `master` (fetched raw);
- the upstream license files of GPGX, PicoDrive, snes9x, Mesen/Mesen2, mGBA, Nestopia and FCEUmm, fetched raw (all byte-identical to the copies in `licenses/`);
- fightcade-fbneo `c9595014`;
- EmulatorJS `f4f0f1c7`, plus the npm tarball `@emulatorjs/emulatorjs@4.2.3`;
- WebAssembly/design `Nondeterminism.md`;
- emscripten `main` `src/lib/{libcore,libtime,libwasi}.js` and `src/settings.js`.

Downloads are in `research/emulator-cores/verify/`.

| # | Claim (short) | Verdict | Evidence / correction |
|---|---|---|---|
| 1 | FBNeo license terms + MAME non-commercial | **CONFIRMED** | `src/license.txt:1-11` (5 restrictions), `:79-89` (Final Burn: "as long as you don't do so commercially… must either distribute or link to the source code"), `:90-99` (BSD-3 MAMEdev portions) and `:101-136` (MAME non-commercial text). The linked MAME license at `5cef4e1f` is the non-commercial "MAME License" (VERIFIED). Two nuances. The restrictions are scoped to "the FB Neo original material". "You must make public any changes you make to the source code" is **not conditioned on distribution**. |
| 2 | FBNeo team position in libretro docs | **CONFIRMED** | `docs/library/fbneo.md:29-40`. Also: "only a manual installation of the core by the user will be considered legal and supported" for commercial frontends (line 38). |
| 3 | Emscripten Makefile target/flags, subsets | **CONFIRMED** (nuance) | `Makefile:413-420`, `:64-71`, `Makefile.cps12:52-53`, `Makefile.neogeo:31-32`. The Makefile contradicts itself. Line 7 says "no known issue with fbneo", and the **default `FASTMATH = 1`** (line 8). Lines 557-564 say "evidence that -ffast-math is actually not safe", yet still add `-ffast-math -frounding-math` for every non-MSVC platform except emscripten, which forces `FASTMATH = 0`. So native libretro FBNeo builds are fast-math by default. The server must run the **same wasm** in Node, not a native `.so`. A `Makefile.pre68k` subset also exists. |
| 4 | Only quirk ENDIAN_DEPENDENT; scan = FULLSCAN + nCurrentFrame | **DOWNGRADE** | The literal facts hold (`libretro.cpp:1244-1245`, `retro_memory.cpp:266-286`), but the declared quirk understates the problem. Upstream state **omits resampler and clock-accumulator state**: `burn_ym2610.cpp:19` `nFractionalPosition` is not in `BurnYM2610Scan` (`:495-508`), and `msm6295.cpp:48-53` marks `nFractionalPosition` and others "Not scanned", nor are `nPreviousSample`/`nCurrentSample` (`:108`). libc `rand()` state is not serialized either. The core does not declare `RETRO_SERIALIZATION_QUIRK_INCOMPLETE` (`libretro.h:3738-3745`, "should not be relied upon for… netplay"). **Corrected:** states round-trip within one instance (rollback), but loading into a *fresh* instance (late join, resync) can drift unless the resampler state is added to the scan. The project's `native/patches/fbneo-cross-instance-savestates.patch` does this and reports the drift was found by its test (observed; I did not re-run it). `TweakScanFlags` also runs in `retro_serialize_size` and `retro_unserialize`, not only in serialize. |
| 5 | GET_SAVESTATE_CONTEXT / AV bit 4 fallback / NULL probe | **CONFIRMED** (nuance) | `retro_memory.cpp:288-325`, `libretro.cpp:1279-1285`. Bit 4 is `RETRO_AV_ENABLE_FAST_SAVESTATES` (`libretro.h:3585`, deprecated in favour of the context). Two nuances. (a) The fallback **assigns** `kNetGame = (av & 4) ? 1 : 0`, so it can *clear* a `kNetGame=1` forced before load. (b) If the AV call is unsupported, `nAudioVideoEnable` stays `-1`, and `-1 & 4` gives `kNetGame=1`. With context support, NORMAL and RUNAHEAD contexts leave `kNetGame` untouched. |
| 6 | BurnGetLocalTime / BurnRandomInit with and without kNetGame | **CONFIRMED** | `burn.cpp:1752-1789`, `:1815-1822`, call at `:885` before `Init()` at `:889`; `burnint.h:91-98`. Under Emscripten, `time()` maps to `clock_time_get`, which uses `Date.now`, and `localtime()` maps to `_localtime_js`, which uses JS `Date` and the host timezone (`libwasi.js:154-158`, `libtime.js:103-126`). `d_starwars.cpp:947` calls `BurnRandomInit` again. |
| 7 | uPD4990A init from BurnGetLocalTime, serialized, cycle-ticked | **CONFIRMED** (nuance) | `neo_upd4990a.cpp:52-62`, `:153-163`, `neo_run.cpp:4202`. The helper `nuPD4990ATicks` (`:25`) is not scanned, but it is reset at each frame start (`uPD4990ANewFrame`, `neo_run.cpp:4847`), so it is harmless. The tick rate also scales with `nBurnCPUSpeedAdjust` (`neo_run.cpp:4804`). |
| 8 | Must force kNetGame before load and/or sync from the server's post-boot state | **DOWNGRADE** | The reasoning holds. `kNetGame` is assigned only at `libretro.cpp:55` (=0) and `retro_memory.cpp:307,317`, and `retro_serialize_size` returns before `TweakScanFlags` when no game is loaded. **Corrected:** "and/or" must be "**and**". Savestate-only sync is not sufficient for mid-game snapshots in upstream FBNeo (see #4, plus libc `rand()` and the frontend diag counters). Forcing `kNetGame` lasts only if the frontend answers `GET_SAVESTATE_CONTEXT` or sets AV bit 4 (see #5). Also pin `fbneo-hiscores=disabled`. Its default is "enabled" (`retro_common.cpp:252-264`), and `HiscoreApply()` runs on every `BurnDrvFrame` (`burn.cpp:1009`) before the first serialize turns it off. Keep save dirs empty so NVRAM `.fs` autoload (`libretro.cpp:1336, 2200`) is identical everywhere. |
| 9 | Audio gating; YM2610/QSound only advance with pBurnSoundOut; rate dependencies | **CONFIRMED** | `libretro.cpp:1386-1424, 1476-1477` (only in non-`FBNEO_DEBUG` builds), `neo_run.cpp:5116-5117`, `burn_ym2610.cpp:411-426`, and the mid-frame `YM2610Render` early return at `:61`. Also `msm6295.cpp:545`, `fm.c:2762` (`adpcm_arrivedEndAddress`, readable by the CPU) and `qs.cpp:95`. FM timers (`BurnTimer`) advance regardless. |
| 10 | Server must render audio with identical settings | **CONFIRMED as inference** (stays ASSUMED) | Follows from #9. The libretro spec also says the audio pipeline state "should not be affected" by the AUDIO bit (`libretro.h:3564-3567`). |
| 11 | DRAWSYNC drivers; frameskip nulls pBurnDraw | **CONFIRMED** (precision) | `burn.h:558`, `libretro.cpp:1394, 1452, 1476`; 7 driver files, 83 flags. Scope: X-Men is only `xmen6p`/`xmen6pu`, so the 2P/4P X-Men sets are **not** flagged. Section 0's "Konami X-Men" overgeneralizes. Mystic Warriors is only the 5 `mystwarr*` sets (not Violent Storm, Metamorphic Force, Martial Champion or Gaiapolis). Moo Mesa is only the 5 `moomesa*` sets (not Bucky O'Hare). The flag is a runahead maintenance list, not proof that other drivers are render-independent (ASSUMED). |
| 12 | force-60hz uses host refresh; cpu-speed-adjust scales CPS | **CONFIRMED** (precision) | `retro_common.cpp:1610-1623`, `burn.cpp:63-65, 1074-1085`, `cps.cpp:2258`, `cps_run.cpp:359, 446`. The host rate is used only if 59 < r < 61; otherwise 60.00. It applies only to games above 50 Hz. Default is disabled. `nBurnCPUSpeedAdjust` is used in **60 driver files**, including Neo Geo 68K/Z80/RTC (`neo_run.cpp:4785-4812`), not only CPS. |
| 13 | diagnostic-input default Hold Start; counter serialized only for runahead | **CONFIRMED** | `retro_common.cpp:229-251`, `retro_input.cpp:3015-3055`, `retro_memory.cpp:281-283`. `bDiagComboActivated` is never serialized. Activation zeroes every `GIT_SWITCH` input. It reads port 0 only. |
| 14 | Romset lookup: 3 archives × 4 locations, CRC from central directory, name fallback in patched/romdata | **CONFIRMED** | `libretro.cpp:881-949, 976-1069`, `zipfn.cpp:129`. Extras: every found location is scanned, not just the first; each name tries `.zip` then `.7z` (`zipfn.cpp:30, 45`); data CRC is re-checked on extraction (`UNZ_CRCERROR` fails the load, `zipfn.cpp:238-239`); a size mismatch is treated as missing (`libretro.cpp:1065-1068`). |
| 15 | DATs 1.0.0.03, Arcade 8421 / Neogeo 689; sf2ce no romof; ssf2t.key; no qsound | **CONFIRMED** | DAT headers and `<game` counts; `sf2ce` at 169669 has no `romof`; `ssf2t.key` is inside `ssf2t`; no `qsound` set and no `dl-1425` anywhere; `version.h` 1.0.0.03. |
| 16 | Neo Geo required files = selected BIOS + sm1/sfix/000-lo | **DOWNGRADE** | `d_neogeo.cpp:1622-1675`: `sp-s3.sp1` is the only BIOS without `BRF_OPT`. libretro's missing-ROM check (`libretro.cpp:1084-1104`) requires every non-OPT ROM. **Corrected:** `sp-s3.sp1`, `sm1.sm1`, `sfix.sfix` and `000-lo.lo` are always required, *regardless* of the selected BIOS. A non-default BIOS selected by DIP or `fbneo-neogeo-mode` must also be present. |
| 17 | Fightcade = FBNeo+GGPO; kNetGame=1, bForce60Hz=0; full-state save; detectors | **CONFIRMED** (nuance) | README; `fbn_ggpo.cpp:349-360, 492-497`; `vid_overlay.cpp:192-220, 333-383, 735-743`. Three nuances. It scans `ACB_FULLSCANL`, which **excludes `ACB_CPS3_CRAM`** (`state.h:29-30`). `SetBurnFPS` (`fbn_ggpo.cpp:43-75`) turns `bForce60Hz` back on for older protocol versions and for `umk3uc`. GGPO loads states into the *same* instance, so this does not prove fresh-instance completeness (#4). "Main emulator" is REPORTED wording. |
| 18 | RetroArch gates netplay/runahead on DETERMINISTIC; core-info labels | **CONFIRMED** | `core_info.h:27-43`; `core_info.c:3022-3077` (`supports_netplay`/`supports_runahead` require DETERMINISTIC); `netplay_frontend.c:9818`; `runahead.c:1747, 2096`. Labels were re-fetched from libretro-core-info `master`. Nuances: the user setting `core_info_savestate_bypass` overrides the gate (`core_info.c:3026-3027`). An info file without `savestate_features` defaults to DETERMINISTIC (`:1880`); `mesen-s` is such a case. The label means same-binary netplay works, not cross-engine determinism. |
| 19 | Console core licenses; libretro docs netplay rows | **CONFIRMED** | Upstream files fetched independently: GPGX and PicoDrive have the MAME-style non-commercial text; snes9x says "non-commercial purposes" and "freeware for PERSONAL USE only" (`LICENSE:171-184`); FCEUmm and Nestopia are GPLv2; Mesen and Mesen2 are GPLv3; mGBA is MPL-2.0. Docs netplay rows: ✔ for GPGX, PicoDrive, snes9x, FCEUmm, Nestopia, Mesen and Mesen-S; ✕ for mGBA (even though core-info says "deterministic"). |
| 20 | EmulatorJS netplay = host video streaming | **CONFIRMED** (version caveat) | `netplay.js:676-690` (guest freeze), `:785` (`captureStream(30)`), `:1105-1124` (`inputs` channel, applied at `currentFrame`), `:976-983` (guest `simulateInput` sends on the channel), `:1322-1336` (prefers H.264). **Caveat:** this exists only on unreleased `main`. CHANGES.md says 4.3.0-pre and `package.json` says 4.2.4. npm `latest` is **4.2.3** (2025-07-05), which ships the *older* socket.io-relayed input sync with savestate transfer (`emulator.js:5600-5730`, comment "control syncing - broken"). |
| 21 | retro_get_memory_data mapping | **CONFIRMED** (nuance) | `retro_memory.cpp:14-215`, `cps_mem.cpp:27, 407, 470`, `neo_run.cpp:1416-1420, 3966-3972`. Nuances: Neo Geo 68K RAM is **1 MB** with the RAM hack, IPS or romdata (`neo_run.cpp:4223`). NeoCD also publishes memory maps. Psikyo, Cave, Toaplan-Raizing and SMS/GG use their own area names. The data is only available after `CheevosInit` at load (`libretro.cpp:2191`). |
| 22 | Wasm nondeterminism list | **CONFIRMED** | `Nondeterminism.md:15-44`; the upstream file is identical to the local copy. It is the non-normative design repo. It also lists the flexible-vectors proposal (`:46-48`). |
| 23 | Same wasm is bit-identical in V8 / Android WebView / iOS JSC | **UNCHECKABLE** (stays ASSUMED) | There is no JSC or iOS runtime here, and no primary cross-engine evidence was found by search. Verified leak paths to close: emscripten `time`/`localtime`/`random_get` are JS host calls (#6; `libwasi.js` uses `crypto.getRandomValues`). `JS_MATH` must stay `false` (`settings.js:635-640`: JS math "may give different results… can also vary between browsers"). iOS Lockdown Mode disables Wasm (REPORTED, see webview report). A cross-engine soak test is required. |

### Missed facts (added by verification)
- **Resampler state is not serialized** (VERIFIED, see #4). Late join and resync from a mid-game savestate can desync CPS-1 (YM2151 + MSM6295), CPS-2 (QSound) and Neo Geo (YM2610) unless that state is patched into the scan. Treat the project's patch as mandatory, and re-check every other sound glue before enabling more drivers.
- **libc `rand()` in game logic** (VERIFIED). `d_blackt96.cpp:136-177` (Black Touch '96, `GBF_SCRFIGHT`, a beat 'em up) and `d_pacman.cpp:1918` (Ali Baba) feed `rand()` into CPU reads. 19 burn files call `rand()`, and none of them are in the cps12 or neogeo subsets. That state is outside savestates, so savestate-based joins are unsafe for these drivers.
- **`TweakScanFlags` can clear a forced `kNetGame`** (VERIFIED, `retro_memory.cpp:317`) when the frontend lacks `GET_SAVESTATE_CONTEXT` and returns AV flags without bit 4.
- **License scope** (VERIFIED text; legal reading ASSUMED). The project's FBNeo patches and shim must be published, because the source-change publication duty is unconditional. MAME's "nor may they be used in a commercial product or activity" restricts *use in an activity*, not only distribution. So even architecture (a), server-side streaming with no binary shipped, cannot be monetized.
- **EmulatorJS's released 4.2.3 used input relay plus state sync, and `main` replaced it with video streaming** (VERIFIED code). ASSUMED reading: the project moved away from browser lockstep.
