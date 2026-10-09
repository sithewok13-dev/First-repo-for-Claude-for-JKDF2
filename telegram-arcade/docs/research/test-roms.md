# Lawful test content for the multiplayer emulation arcade

Area: freely redistributable homebrew ROMs with clear licenses, to validate fceumm/nestopia, Genesis Plus GX and FBNeo in both candidate architectures (server-side streaming, and server-authoritative lockstep with WASM replicas).
Research date: 2026-10-09. Everything under the research workspace (not committed; sources are cited inline).

Status legend:
- **VERIFIED** means I read the primary text or code myself (repo path and line given) or built the ROM myself here.
- **REPORTED** means it comes from a web-search summary or another secondary source.
- **ASSUMED** means my own inference.

---

## 1. Short version (what to use)

| Test need | Pick | System / core | License (short) | Players | How to get it | Ship in a test fixture? |
|---|---|---|---|---|---|---|
| (1) 2P simultaneous "fighter" / versus | **RHDE: Furniture Fight** (2P real-time strategy brawler). The true fighter, **Super Tilt Bro**, is the backup. | NES, NROM (mapper 0), CHR-RAM | GNU All-Permissive (RHDE). WTFPL code plus CC-BY music and art (STB). | 2, simultaneous | RHDE: build from source in about 1 s with apt `cc65` + Python/Pillow. STB: needs 6502-gcc built from source plus a forked xa65 and huffmunch; prebuilt only on itch.io. | RHDE: **yes**, keep the notice. STB: yes, with attribution, but download it at test time because the toolchain is heavy. |
| (2) 2P co-op action ("beat 'em up" stand-in) | **Thwaite** (2P co-op missile defence) | NES, NROM-256 | GPL-3.0-or-later | 1 or 2, simultaneous co-op | Build from source with apt cc65 | Yes, if the source ships next to it (or pin the commit and offer the source) |
| (3) 4P simultaneous | **allpads** (Four Score input test, zlib) as a diagnostic. Our **own test-cabinet ROM** for 4P gameplay (recommended). Possible extra: m-mccaffrey/NES-Test `shooter.nes` (4P rail shooter), but it has **no license**. | NES + Four Score | zlib (allpads). None (NES-Test). | 4 | Build with apt cc65 | allpads: **yes**. NES-Test: **no**, unless the owner adds a license. |
| (4) 1P with a clear game over | **Gentris** (SGDK sample, MD). On NES, **Thwaite** in 1P mode. | Genesis Plus GX / NES | MIT (SGDK) / GPL-3.0+ | 1 | Prebuilt `rom.bin` tracked in the SGDK repo / build | Yes |
| (5) Hot-seat, shared controller, turn-based | **Concentration Room**, 2-player mode | NES, NROM-128 | GPL-3.0-or-later, with an exception for exact author binaries | 2, alternating, **one controller can be passed** | Build with apt cc65 (build embeds a timestamp, see §4) | Yes, with source |
| 2P simultaneous versus puzzle (extra) | **Squirrel Domino** | NES, NROM-128 | zlib (code). Author asks that the background art not be reused. | 2, simultaneous | Build with apt cc65 | Probably yes. Safer to build at test time. |
| Smoke test via the Ubuntu archive | **Escape from Pong** (`efp` package) | NES, NROM-128 | BSD-3-Clause | 1 (no win, no lose) | `apt-get download efp` | Yes |
| MD input diagnostic, up to 8 pads | **SGDK joy-test** | Genesis Plus GX | MIT | TeamPlayer / EA 4-Way | Prebuilt `rom.bin` in the SGDK repo | Yes |

All the NES picks I built here produce byte-identical ROMs on rebuild, **except Concentration Room**, which embeds the build time with ca65 `.time`. They all use mapper 0 (NROM), apart from Nova, which is MMC1. That makes them ideal for deterministic lockstep and state-hash tests.

**Two gaps are still open:**
- I found no open-licensed **2P beat 'em up** or **true 2P fighter that builds with apt tools**.
- I found no open-licensed **multiplayer Genesis, SNES or arcade/FBNeo game** that can be redistributed.

My recommendation for (3), and for MD multiplayer, is to write a small in-house "test cabinet" ROM, licensed by us:
- For NES: ca65, based on `pinobatch/nrom-template` (all-permissive) plus the allpads Four Score reader (zlib).
- Optionally, an SGDK twin.

---

## 2. Toolchain availability (Ubuntu 24.04 "noble" archive). VERIFIED with `apt-cache policy`

| Package | Candidate | Notes |
|---|---|---|
| `cc65` | 2.19-1 (noble/universe). Already **installed** in this container. | `ca65 --version` prints `ca65 V2.18 - Ubuntu 2.19-1`. There is no separate `ca65` package; ca65 and ld65 ship inside `cc65`. |
| `xa65` | 2.4.0-0.1 | Super Tilt Bro needs the **forked** `sgadrat/xa65-stb` ("increased memory limits", per its README). The apt xa65 is not enough. |
| `dasm` | 2.20.14.1-2 | 6502 assembler |
| `acme` | 0.97~svn20211115 | 6502 assembler |
| `64tass` | 1.59.3120-1 | 6502 assembler |
| `sdcc` | 4.2.0 | Z80, for SMS/GG |
| `pasmo`, `z80asm` | available | Z80 |
| `binutils-m68k-linux-gnu`, `gcc-m68k-linux-gnu` (13.2), `gcc-12-m68k-linux-gnu` | available | Linux-target m68k cross compiler. Not the `m68k-elf` toolchain that SGDK ships. Using it for SGDK would need makefile work (ASSUMED). |
| `default-jre-headless` | available | SGDK's rescomp needs Java (REPORTED via search) |
| `python3-pil` | 10.2.0 | Pillow 12.3.0 is already importable here |
| `faketime` / `libfaketime` | 0.9.10 | Could pin `.time` for reproducible Concentration Room builds (ASSUMED that it works with ca65) |
| `asm6f`, `nesasm`, `sgdk`, `wla-dx`, `vasm`, `rgbds`, `ophis`, `asl`, `m68k-elf` | **not in the archive** | |

**ROMs in the Ubuntu archive.** I downloaded `dists/noble/Contents-amd64.gz` (51,301,092 bytes) and searched for `.nes .sfc .smc .fds .gba`. The only real ROMs are `usr/share/nes/efp.nes` and `usr/share/nes/efpbw.nes`, both in package `efp` (universe/games, 1.6-3, "Escape from Pong NES game"). VERIFIED.
- `.deb` SHA256: `3161cc6b618ad58b8634ada5bf4d81c604857290183f1c0c22218711b940758d`
- The package `copyright` file says **BSD-3-clause**, Copyright 2004-2018 Adam Gashlin.
- `efp.nes` SHA256: `20567889a92934710f535ad9e92c5c42637202fce423c29c2e92e49defbc64f5`
- It is 16 KiB PRG, mapper 0, 1 player, and per its docs "There is no way to actually 'win' the game, but then again there is no way to 'lose'".
- There are no SNES or Genesis ROM packages.

Other related packages exist but carry no ROMs: `fceux`, `nestopia`, `mednafen`, `ares`, `retroarch`, `libretro-nestopia`, `libretro-genesisplusgx` (multiverse), `libretro-snes9x`, `libretro-bsnes-mercury-*`, `mame`, `mame-data`, `mame-extra`.

---

## 3. Candidate-by-candidate findings

Commits inspected (all shallow clones under the research workspace (not committed; sources are cited inline)):

```
thwaite-nes          00e36745 2024-05-19     rhde-nes     f3330e30 2026-09-23
croom-nes            ed19c3c0 2024-05-22     NovaTheSquirrel e9e79ae5 2025-12-08
allpads-nes          e70cc843 2021-05-30     SquirrelDomino  96557700 2024-05-06
neskit (bitbucket)   03a0cf91 2022-05-27     sgdk (sparse)   ee6870a6 2026-10-01 (SGDK 2.11)
super-tilt-bro       b132fd25 2026-02-01     fbneo (sparse)  63541541 2026-10-09
nes-test-mmccaffrey  48b1cf95 2026-10-03     nrom-template   d5ce5d89 2026-04-08
libretro-fceumm      7a542dab 2026-09-26
```

### 3.1 RHDE: Furniture Fight (pinobatch/rhde-nes). Best 2P simultaneous pick

- **License** (VERIFIED, `rhde-nes/LICENSE.txt:1-4` and `README.md:25-33`):
  > "Copyright 2011-2016 Damian Yerrick. Copying and distribution of this file, with or without modification, are permitted in any medium without royalty provided the copyright notice and this notice are preserved in all source code copies. This file is offered as-is, without any warranty."

  This is the GNU All-Permissive License.
- **Players** (VERIFIED, `USAGE.html`): "a real-time strategy video game for **two players**". Both players act at the same time in the timed Furnish, Battle and Build phases, and "either player can press the Start button to pause".
  - **Correction to the brief:** RHDE is **2P, not 4P via Four Score**. The repo has no Four Score code (I grepped for 4016/4017 and "four score").
  - I found no CPU opponent in the docs (ASSUMED that it is 2-human only).
- **Build** (VERIFIED): `make all` with apt cc65 2.19, Python 3 and Pillow.
  - `tools/donut.py:345` calls `os.get_terminal_size(stderr)` and crashes without a TTY. Workaround: `script -qec "make all" /dev/null`, or patch it to `shutil.get_terminal_size()`.
  - Output: `rhde.nes`, 32,784 bytes, 32 KiB PRG, CHR-RAM, mapper 0, vertical mirroring.
  - SHA256 `a6131a45fe5ec5f8e5778abfcd2031477231a023f30af8533c6d25acf8297a43`. A clean rebuild gives an identical hash.
- **Prebuilt:** a GitHub tag `v0.07` exists; its release asset names are unknown. pineight.com hosts "source and NES binary" (REPORTED; pineight.com is not reachable from this container).
- **Fixture:** redistributable. Keep the notice.

### 3.2 Super Tilt Bro (sgadrat/super-tilt-bro). True 2P platform fighter, heavy toolchain

- **License** (VERIFIED, `LICENSE`): WTFPL v2, "You just DO WHAT THE FUCK YOU WANT TO."
- **Third-party assets carry their own terms** (VERIFIED, `data-sources/theme_*.txt` show `COPYRIGHT "CC-BY"` with authors Tuï and Kiliran). `game/data/menu_credits/credits.asm:78-136` credits music and art to Kilirane, LemonyB., Ozzed, Tui, Tyson Tan, David Revoy, Zi Ye, VGS Staff and Matt Hughson. Redistribution therefore needs those **attributions** (ASSUMED that a credits file is enough).
- **Players** (VERIFIED, `README.md:43`): "Each controller controls a character and the goal is to send the other out of screen". This means 2P simultaneous.
- **Build** (VERIFIED, `README.md:9-39`, `deps/build-deps.d/*`):
  - It needs the xa65-stb fork, **6502-gcc built from `itszor/gcc-6502-bits`** (a full GCC build, which I did not attempt within the 20-minute budget), huffmunch, and Python + Pillow.
  - `build.sh:175-178` builds the variants `tilt_no_network_(E)`, `tilt_rainbow512_(E)`, `tilt_no_network_unrom512_(E)` and `tilt_no_network_unrom_(E)`.
  - The main `Super_Tilt_Bro_(E).nes` uses the RAINBOW mapper, which the README says "is not yet included in any emulator". **libretro-fceumm has no Rainbow mapper** (VERIFIED: grepping `src/` for "rainbow" only finds game names). fceumm does support **UNROM 512, mapper 30** (`src/ines.c:520`). So use the `_unrom_` or `_unrom512_` builds.
- **Prebuilt:**
  - itch.io lists `Super_Tilt_Bro_(E).nes` (512 kB) for v2.6, "name your own price" (REPORTED).
  - GitHub `/releases/latest/download/<name>` returns 404 with no redirect, which suggests there are no GitHub release assets (ASSUMED). Tags go up to `beta-6`.
  - FBNeo's NES database has `nes_supertiltbro` "(HB, v2.6)", ROM size 524,304 bytes, CRC `0xfa4edd45`, 2 players (VERIFIED, `src/burn/drv/nes/d_nes.cpp:26590-26606`).
- **Fixture:** legally fine, but practically it is "download at test time from itch.io, or build in a dedicated CI image".

### 3.3 Thwaite (pinobatch/thwaite-nes). 2P co-op action and 1P game over

- **License** (VERIFIED, `README.md:33-49`): "free software: you can redistribute it and/or modify it under the terms of the GNU General Public License ... either version 3 of the License, or (at your option) any later version." "Some source code files, those less specific to this game, are under a more permissive license similar in effect to the license of zlib."
- **Players** (VERIFIED, `USAGE.html`):
  - "select between a single-player game and a 2-player cooperative game".
  - "In a 2-player game ... Player 1 controls Milo's cursor, and player 2 controls Staisy's cursor."
  - It also supports the SNES Mouse.
- **Game over** (VERIFIED): "The game is over once both silos or all ten houses have been destroyed, or once you have survived all seven nights."
- **Build** (VERIFIED): `make all` takes 0.9 s.
  - `thwaite.nes`: 40,976 bytes, 32 KiB PRG + 8 KiB CHR, mapper 0, SHA256 `ee51cd9562f28195ba015d9857c6c4fc9bf67cdfb213e95f655e586b92195173`.
  - `thwaite128.nes` is also produced.
  - Reproducible across clean rebuilds.
- **Fixture:** GPLv3 allows binary redistribution if the corresponding source is provided. Vendor the source or pin the commit next to the ROM.

### 3.4 Concentration Room (pinobatch/croom-nes). Hot-seat / shared controller

- **License** (VERIFIED, `README.md:99-103`):
  > "The accompanying program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License, version 3 or later. As a special exception, you may copy and distribute exact copies of the program, as published by Damian Yerrick, in iNES or UNIF executable form without source code."
- **Modes** (VERIFIED, `README.md:27-40`):
  > "2 Players: Two players take turns turning over cards. They can pass one controller back and forth or use one controller each. If a pair doesn't match, the other player presses the A and B Buttons and takes a turn."

  The other modes are 1P Story, Solitaire and Vs. CPU. This makes it the ideal **hot-seat** test: two seats share port 1, and seat handoff happens mid-game.
- **Build** (VERIFIED): takes 0.9 s and produces `croom.nes`, 24,592 bytes, 16 KiB PRG + 8 KiB CHR, mapper 0.
  - **The build is not reproducible.** `src/litetitle.s:649-651` embeds `"Build time: "` followed by `decbytes .time` (ca65 `.time`). Two rebuilds differed in 2 bytes.
  - `SOURCE_DATE_EPOCH` is **ignored** by ca65 2.19 (tested).
  - Fixes: pin the hash of one build; patch `.time` to a constant (a GPL-permitted modification, which must be marked); or try `faketime`.
  - The copy in `built/` has SHA256 `acac6448...`, which is only valid for that build.
  - `shufflemode = -r` in the makefile means "reverse order" (deterministic), not random (`tools/shuffle.py:228-262`).
- **Fixture:** our build is not "as published by Damian Yerrick", so plain GPLv3 terms apply: include the source.

### 3.5 Squirrel Domino (NovaSquirrel/SquirrelDomino). 2P simultaneous versus puzzle

- **License** (VERIFIED, `LICENSE.txt`): "The zlib License ... Copyright (c) 2019, 2024 NovaSquirrel".
- The README adds: "The code is available under the zlib license, but please don't reuse the background graphics for other projects."
- The audio library is FamiTone2 (VERIFIED, `famitone/readme.txt:26`): "released into the Public Domain".
- **Players** (VERIFIED, `menu.s:115,217,269`): "Versus mode", "In versus mode it's a race". Win and lose states exist (`puzzlegame.s:449`).
- **Build** (VERIFIED): `ca65 squirrel_domino.s && ld65 -C nrom128.x`. Output is 24,592 bytes, SHA256 `b7b869f44ef6bfea96122d0315d1c261066841f79c92df5955490410e1b244fe`, reproducible.
- **Fixture:** the code license allows redistribution. The status of the background art is ambiguous (it is a request, not a grant). Building at test time is the safer option (ASSUMED).

### 3.6 allpads (pinobatch/allpads-nes). 4-controller input diagnostic

- **License** (VERIFIED, `LICENSE.txt`): zlib, "Copyright 2016 Damian Yerrick ... Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely".
- **Function** (VERIFIED, `README.md`):
  - It detects the NES controller, the **NES Four Score (NES-034)**, the Famicom hardwired controllers, the SNES controller, the Zapper, the Arkanoid controller, the Power Pad and the SNES Mouse.
  - Press a controller's primary fire button to begin an input test.
- **Build** (VERIFIED): `make allpads.nes` gives 32,784 bytes, NES 2.0 header, mapper 0, CHR-RAM. SHA256 `9ce022805eb472e4ad9a9a7b101a685a86267c6448cc47176b14ee95d0c4afb9`, reproducible. The default `make` target also tries to launch `fceux`.
- **Fixture:** yes.

### 3.7 m-mccaffrey/NES-Test `shooter.nes`. 4P simultaneous rail shooter, but unlicensed

- VERIFIED from `README.md`:
  - It is a "4-player rail shooter (NES Four Score / Famicom 4-player)".
  - "Players 2–4 join any time".
  - "At 0 lives it's game over".
  - It includes cynes-based emulator tests and a Four Score patch.
- **There is no LICENSE file.** Only `lib/neslib/COPYING` (zlib, Shiru and Lauri Kasanen) is licensed.
- The commit author is `Claude <noreply@anthropic.com>`, dated 2026-10-03.
- It builds with apt cc65 (`make shooter`, 40,976 bytes, SHA256 `b8ea08e6...`, reproducible).
- **Do not redistribute** unless the repo owner adds a license. If it belongs to this project's team, adding MIT or 0BSD would make it the ideal 4P fixture (ASSUMED).

### 3.8 Nova the Squirrel (NovaSquirrel/NovaTheSquirrel). 1P platformer

- **License** (VERIFIED, `README.md:36-40`):
  > "All code is available under the GPL license version 3 or later. Assets (graphics, levels, etc.) are available under Attribution-NonCommercial-ShareAlike 4.0 International ... Permission is not granted to use the character and design of Nova in anything else."
- The assets are **NonCommercial**, so avoid shipping them in any product fixture.
- **Build** (VERIFIED, `mk.bat`): `python3 NtS1LevelConvert.py levels; ca65 src/nova.s; ld65 -C src/nova.x`, taking 3.3 s.
  - Output: 262,160 bytes, 256 KiB PRG, CHR-RAM, **mapper 1 (MMC1), battery SRAM**, NES 2.0 header. SHA256 `f5b3ae45...`, reproducible.
  - Useful as an MMC1 + battery-SRAM test of savestates and lockstep state hashing (battery RAM must be part of the hashed state).

### 3.9 tsone 2048 (neskit on **Bitbucket**, not GitHub)

- The repo is `https://bitbucket.org/tsone/neskit`, which is reachable from this container. `examples/2048/README.md:36-44` says "2048 example is released under MIT license. Copyright (c) 2013 Valtteri Heikkila". VERIFIED.
- Credits: "Font and tile graphics are borrowed from 2048 Sega Megadrive version of by oerg866". The provenance of those graphics is unclear.
- A **prebuilt `2048.nes` is committed** (16 KiB PRG, CHR-RAM, mapper 0), SHA256 `6344b7b6...`. The builder is `build.py`, a Python assembler (nkasm).
- Single player. I did not confirm the game-over screen in the source (ASSUMED that it exists, as standard 2048 has one).
- FBNeo lists it as `nes_2048` "2048 (HB)" by Tsone (VERIFIED, `d_nes.cpp:14328`).

### 3.10 Zap Ruder (pinobatch/zap-ruder). Not suitable

- License: all-permissive (VERIFIED, `LICENSE.txt`).
- It is a **Zapper light-gun test** (VERIFIED, `README.md`: "Zap Ruder is a Zapper test program"). It is useless for pad, touch or keyboard play.

### 3.11 SGDK samples (Stephane-D/SGDK, SGDK 2.11, MIT)

- **License** (VERIFIED, `license.txt:1`): "MIT License - Copyright (c) 2025 Stephane Dallongeville".
- `readme.md:11-12` adds: "GCC compiler and libgcc are under GNU license (GPL3) and any software build from it (as the SGDK library) is under the GCC runtime library exception license".
- **Prebuilt `out/release/rom.bin` files are committed** for about 30 samples (VERIFIED with `git ls-tree`). Useful ones:
  - **`sample/joy-test`**: 131,072 bytes, SHA256 `67c9de14...`. A multi-pad tester. `src/main.c` configures `JOY_SUPPORT_TEAMPLAYER`, recognises `PORT_TYPE_EA4WAYPLAY`, and supports mouse, Menacer, Justifier, trackball and Phaser. It is the MD 4P+ input diagnostic.
  - **`sample/game/gentris`**: SHA256 `d74d0a02...`. A 1P Tetris clone with a "game over menu", by "werton playskin, 08/2026". Its header says "all graphics and sound effects are generated procedurally from code, no external assets". This is the cleanest 1P game-over test on MD. Input is **JOY_1 only** (`src/input.c:99`).
  - `sample/basics/hello-world`: SHA256 `bb92580...`. A boot smoke test.
- **Avoid:**
  - `sample/game/sonic` (Sega characters).
  - `sample/game/platformer`, which bundles `res/sound/sonic2Emerald.vgm` (Sonic 2 music; VERIFIED file name).
  - `sample/demo/bad-apple` (third-party video and music; ASSUMED copyrighted).
- **Building SGDK yourself** needs the m68k-elf GCC + Java toolchain, which is not in apt (see §2). Rely on the committed binaries, or the official SGDK Docker image (REPORTED, not checked).

### 3.12 Genesis 2P/4P open-licensed games: none verified

- Search found only itch.io freeware: zhamul "Tag" (2P, made with SGDK, no license stated), Dr. Ludos "Break An Egg" (source uploaded, license unclear) and "Prefrontal Mega" (REPORTED).
- Mojon Twins MD and NES games are CC BY-NC-SA (REPORTED, not checked), which is NonCommercial.
- **Recommendation:** write an in-house SGDK 2P/4P test game, MIT-licensed by us.

### 3.13 MAME "free ROMs" (mamedev.org/roms) and FBNeo support

- **Terms** (REPORTED; `www.mamedev.org` is not reachable from this container):
  - Released "for free, non-commercial use".
  - ArcadeShelf and a DigitPress thread report that the legal notice says the ROMs are approved for distribution **from mamedev.org only**, and that rights remain with the holders.
  - **So do not put them in fixtures.** At most, an operator downloads them manually from mamedev.org.
- **FBNeo support** (VERIFIED, driver structs in the FBNeo master tree, 2026-10-09):

| MAME set | FBNeo driver | Players | Notes |
|---|---|---|---|
| `gridlee` | `pre90s/d_gridlee.cpp:620` | 2 | Optional samples ("gridlee"). Trackball game (ASSUMED that this is awkward on touch). |
| `robby` (Robby Roto) | `pre90s/d_astrocde.cpp:2798` | 2 | Arcade set, no BIOS in its ROM list (`d_astrocde.cpp:2782-2793`) |
| `alienar` (Alien Arena) | `pre90s/d_williams.cpp:3363` | 2 | **Separate P1 and P2 joysticks and buttons** (`AlienarInputList`, `d_williams.cpp:381-404`). REPORTED as 2P simultaneous. |
| `teetert` (Teeter Torture, prototype) | `pre90s/d_exidy.cpp:3943` | 2 | Dial-controlled (ASSUMED) |
| `supertnk` (Super Tank, arcade) | **not in FBNeo** | n/a | Only an unrelated SG-1000 `sg1k_supertnk` exists (`sg1000/d_sg1000.cpp:3741`) |

  Other Exidy sets that MAME reportedly offers are also in FBNeo (VERIFIED driver presence, REPORTED free status): `sidetrac`, `targ`, `spectar`, `hardhat`, `fax`, `circus`, `robotbwl`, `crash`, `ripcord`, `mtrap`, `venture`. I did not verify which of these are actually on the mamedev free list.

### 3.14 FBNeo homebrew ("HB") sets. The flag is not a license

- `BDF_HOMEBREW = (1 << 8)` (VERIFIED, `src/burn/burn.h:553`).
- I parsed every `BurnDriver` with this flag: 77 Neo Geo, 70 NGP, 3 Taito L, 2 Astrocade, 1 Pac-Man hack, plus 625 NES and 341 MD entries in FBNeo's console drivers.
- Many are **commercial homebrew or ports with copyrighted assets**, for example:
  - Xeno Crisis, Cyborg Force, Captain Barrel.
  - "Golden Axe Neo Geo", "Cabal - Neo Geo Conversion", "Xevious - Neo Geo Conversion", "Robocop - Neo Geo Conversion".
- **Neo Geo homebrew also needs SNK BIOS files** (VERIFIED, `src/burn/drv/neogeo/d_neogeo.cpp:1665-1674`):
  - The open replacement `neopen.sp1` ("NeoOpen BIOS v0.1 beta") is optional.
  - `sm1.sm1` (Z80 BIOS), `sfix.sfix` (text tiles) and `000-lo.lo` (zoom table) have no `BRF_OPT` flag and are SNK data.
  - So **no Neo Geo set is fully lawful to redistribute.**
- **Taito L homebrew** in FBNeo: `sokoban` "Sokoban LE" (cmonkey), `speccies` "Speccies 2" and `sqij` "SQIJ!" (Sokurah), all 2P puzzle (VERIFIED, `taito/d_taitol.cpp:4412,4438,4463`). Their licenses are unknown (open question). Taito L needs no BIOS (ASSUMED).
- **FBNeo's own license** (VERIFIED, `src/license.txt:3-7`; this affects the whole project, not just the ROMs):
  > "You may not sell, lease, rent or otherwise seek to gain monetary profit from FB Neo; You must make public any changes you make to the source code; ... You may not distribute FB Neo with ROM images unless you have the legal right to distribute them; You may not ask for donations to support your work on any project that uses the FB Neo source code."
- **Conclusion:** I found no FBNeo-supported multiplayer arcade or Neo Geo set that we can redistribute. Use NES and MD homebrew to validate lockstep and seat logic. Test FBNeo only with operator-supplied sets, for example the mamedev downloads, which are non-commercial.

---

## 4. Implementation notes for the test harness

1. **Build in CI with apt `cc65`.** Pin the repo commits above and assert the SHA256 values (`built/SHA256SUMS`).
   - thwaite, rhde, allpads, squirrel_domino, nova and shooter rebuilt byte-identically.
   - croom does not, because of `.time`.
   - RHDE needs a pty (`script -qec`) or a one-line patch to `tools/donut.py:345`.
2. **Four Score in fceumm is not automatic for homebrew** (VERIFIED, `libretro-fceumm src/drivers/libretro/libretro.c:1520-1556,3547,4227-4245`).
   - Four Score is enabled when ports 3 or 4 (index 2 or 3) are set to `RETRO_DEVICE_GAMEPAD = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1)` (that is 0x201) via `retro_set_controller_port_device`.
   - Otherwise it is enabled only if the ROM CRC is in `fourscore_db_list`.
   - The frontend must therefore set port devices explicitly for allpads or a 4P test ROM.
3. **Battery SRAM** (Nova, MMC1) must be included in lockstep state hashes and late-join savestates.
4. **Concentration Room 2P** is the canonical hot-seat test. Both human seats map to **port 1**, the turn passes on a miss, and the "pass the controller" handoff must not reset the game.
5. **Suggested matrix:**

| Test | ROM |
|---|---|
| boot / smoke | `efp.nes` (apt) and `sgdk-hello-world` |
| 2P simultaneous plus seat swap mid-match | `rhde.nes`, `squirrel_domino.nes` |
| 2P co-op plus 1P game over | `thwaite.nes` |
| hot-seat | `croom.nes` |
| 4P input | `allpads.nes` |
| MD input / 4P input | `sgdk-joy-test` |
| MD 1P game over | `sgdk-gentris` |
| MMC1 + SRAM | `nova.nes` (internal only, NC assets) |
| fighter | STB `tilt_no_network_unrom_(E).nes` (download or build separately) |

6. **In-house test-cabinet ROM** (recommended):
   - A 4P NES ROM on `nrom-template` (all-permissive, VERIFIED `nrom-template/README.md:321-328`) plus the allpads pad reader (zlib).
   - It should show each port's buttons, a frame counter, a PRNG seeded only from input, a simple 2-4P mini-game with a game-over state, and an "assert-hash" RAM signature.
   - License it ourselves (MIT or 0BSD) so it can be committed as a fixture. Do the same for MD with SGDK if MD multiplayer needs coverage.

---

## 5. Redistribution summary (can it live in our repo's test fixtures?)

| ROM | Verdict | Condition |
|---|---|---|
| rhde.nes | Yes | Keep the all-permissive notice |
| allpads.nes | Yes | Keep the zlib notice. Do not misrepresent the origin. |
| efp.nes | Yes | Keep the BSD-3 notice. Also fetchable with `apt-get download efp`. |
| SGDK joy-test / gentris / hello-world | Yes | Keep the MIT notice |
| thwaite.nes, croom.nes | Yes | GPLv3: ship the corresponding source, or a pinned source archive next to it |
| squirrel_domino.nes | Probably | zlib code. Ambiguous art request, so building at test time is preferred. |
| 2048.nes (neskit) | Probably | MIT, but the font and tiles are borrowed from oerg866 |
| Super Tilt Bro | Yes, legally | WTFPL plus CC-BY attributions. Download or build at test time in practice. |
| nova.nes | Internal / non-commercial only | Assets are CC BY-NC-SA 4.0 |
| NES-Test shooter.nes | No | No license |
| MAME free ROMs | No | Non-commercial, distributed by mamedev.org only |
| FBNeo Neo Geo homebrew | No | SNK BIOS files required, and the HB flag is not a license |

---

## Sources

- Repos read (primary): github.com/pinobatch/{rhde-nes, thwaite-nes, croom-nes, zap-ruder, allpads-nes, nrom-template, 240p-test-mini (raw README and LICENSE)}, github.com/sgadrat/super-tilt-bro, github.com/NovaSquirrel/{NovaTheSquirrel, SquirrelDomino}, bitbucket.org/tsone/neskit, github.com/Stephane-D/SGDK, github.com/finalburnneo/FBNeo (plus raw `src/license.txt`), github.com/libretro/libretro-fceumm, github.com/m-mccaffrey/NES-Test.
- Ubuntu: `apt-cache policy/show`, `http://archive.ubuntu.com/ubuntu/dists/noble/Contents-amd64.gz`, `efp_1.6-3_all.deb`.
- Web search (REPORTED):
  - MAME free ROMs: https://www.mamedev.org/roms/gridlee/, https://www.mamedev.org/roms/robby/, https://www.mamedev.org/roms/supertnk/, https://www.mamedev.org/roms/alienar/, https://www.mamedev.org/roms/teetert/, https://arcadeshelf.com/games/free, https://forum.digitpress.com/forum/showthread.php?153256-Exidy-went-public-domain=
  - Super Tilt Bro: https://sgadrat.itch.io/super-tilt-bro
  - RHDE: https://www.nesdev.org/wiki/User:Tepples/RHDE, https://pineight.com/nes/
  - 2048 NES: https://nesdev.nes.science/f22/t11269.xhtml
  - Genesis homebrew: https://zhamul.itch.io/tag, https://drludos.itch.io/breakanegg, https://capriciousday.itch.io/prefrontal-mega/devlog/725701/prefrontal-mega-released-it-spaceship-it
  - NES-Test: https://github.com/m-mccaffrey/NES-Test
- Not reachable from the container: mamedev.org, pineight.com, itch.io, GitHub release pages and API for repos outside the session (release *download* URLs do work when the asset name is known).

---

## Verification

Adversarial fact-check run on 2026-10-09 by an independent checker. Evidence statuses: **VERIFIED** means I re-read the primary text or code myself, or re-ran it with trusted system tools. **REPORTED** means a search summary only. **ASSUMED** means inference.

**Method.** All local clones are clean (`git status --porcelain` is empty). Their HEADs are: rhde-nes f3330e3 (2026-09-23), thwaite-nes 00e3674, croom-nes ed19c3c, allpads-nes e70cc84, libretro-fceumm 7a542da (2026-09-26), SGDK ee6870a (2026-10-01), FBNeo 6354154 (2026-10-09). I re-fetched key files from raw.githubusercontent.com and diffed them against the clones; they matched.

I did not run any downloaded Python tools or makefiles. Reproducibility was re-checked differently: I re-assembled and re-linked each ROM twice with the system `ca65`/`ld65` (cc65 2.19-1). The inputs were the repo `src/*.s` plus the already-generated `obj/nes` intermediates. Each run went to a scratch directory.

| # | Claim (short) | Verdict | Evidence / correction |
|---|---|---|---|
| 1 | Ubuntu noble apt toolchain (cc65 2.19-1, no `ca65` package, etc.) | CONFIRMED | `apt-cache policy` re-run. cc65 2.19-1 is installed from noble/universe and ships `/usr/bin/ca65` and `ld65`. Both report "V2.18 - Ubuntu 2.19-1". No `ca65` package exists. Candidates: xa65 2.4.0-0.1, dasm 2.20.14.1-2, acme 0.97~svn20211115, 64tass 1.59.3120, sdcc 4.2.0, gcc-m68k-linux-gnu 13.2.1, faketime 0.9.10. There is no candidate for asm6f, nesasm, sgdk, wla-dx, vasm, rgbds or gcc-m68k-elf. |
| 2 | RHDE license, 2P, 32 KiB CHR-RAM, SHA a6131a45, donut.py:345 needs a tty | CONFIRMED (quote truncated) | LICENSE.txt and README say "**based on** the GNU All-Permissive License". The quote omits the final words "...preserved **in all source code copies**". This makes it a variant: binary copies need no notice. The title menu offers only "2 Players N Rounds / 10 Rounds / Endless" (src/title.s:630-634), with no CPU opponent. `src/pads.s` reads $4016 and $4017 only, so there is no Four Score support. The header is PRG 2x16K, CHR 0, mapper 0. Re-link gave `a6131a45...` twice. `tools/donut.py:345` calls `os.get_terminal_size(sys.stderr.fileno())`. `src/title.s:583` defines `BUILDDAY = (.TIME/86400)-15928`, but the symbol is never referenced, so the ROM bytes are not affected. |
| 3 | Thwaite GPL-3.0+, 1P / 2P co-op, game over, 40,976 B NROM-256, SHA ee51cd95 | CONFIRMED | README.md Legal section says "version 3 ... or (at your option) any later version". It also notes that some files carry a zlib-like license. USAGE.html: "2-player cooperative game", "Player 1 controls Milo's cursor, and player 2 controls Staisy's cursor", and "The game is over once both silos or all ten houses have been destroyed". Re-link gave `ee51cd95...` twice, 40,976 bytes. The "<1 s" build time was not re-measured. |
| 4 | Concentration Room GPL+exception, turn-based 2P, `.time` breaks reproducibility | CONFIRMED (with an important consequence) | README.md:34-38 and :103 are verbatim. `src/memorygame.s` uses a single `activePad` that changes hands on A+B, so it is true hot-seat play. `src/litetitle.s:651` contains `decbytes .time`. I tested `ca65` with `SOURCE_DATE_EPOCH=0` and `=1000000000`: `.time` still returned wall-clock time. Upstream cc65 master still does `GenLiteralExpr((long) time (0))` (src/ca65/expr.c:1286), so this is not specific to 2.19. The diff bytes are file offsets 0x1FC7-0x1FC8, inside "Build time: NNNNNNNNNN" at 0x1FB3. More digits change as more time passes. The `.shuffle` preprocessing runs in fixed reverse order (`shufflemode = -r`), so it is deterministic. **Consequence:** a locally built croom is never an "exact copy ... as published by Damian Yerrick". The no-source exception therefore never covers our builds, and full GPLv3 source obligations apply. The latest tag is v0.02a, while master is newer. |
| 5 | allpads zlib, device list, 32 KiB NES 2.0, SHA 9ce02280 | CONFIRMED | LICENSE.txt is the zlib text, and README.md:20-27 lists the devices. The header is NES 2.0: PRG 2x16K, CHR-RAM shift 7 (8 KiB), mapper 0. Re-link gave `9ce02280...` twice. Caveat (ASSUMED): detection relies on open-bus and serial signature bits. Its console-model guess (NES-001, NES-101 or Famicom) depends on how faithfully the core emulates open bus. Within one core it will be deterministic. |
| 6 | fceumm Four Score only via port 2/3 = 0x201, or the CRC DB | CONFIRMED (incomplete) | libretro.c:114-115 and :1520-1556 match upstream master byte-for-byte; `RETRO_DEVICE_SUBCLASS(1,1)` = 0x201. Passing `RETRO_DEVICE_JOYPAD` (1) to port 2/3 enables nothing unless the CRC is in the DB. **Missed:** (a) `retro_load_game` calls `FCEUI_DisableFourScore(1)` at :4227 and never re-applies port 2/3 settings, so the frontend must call `retro_set_controller_port_device(2/3, 0x201)` **after** load. (b) `FSDisable` is not part of `FCEUCTRL_STATEINFO` (src/input.c:402-406), so every late-join or resync replica must set the same port config before loading state. (c) The NES 2.0 expansion-device byte 0x02 (Four Score) is ignored. Only 0x03, the Famicom 4P adapter, is mapped (src/ines.c:212). (d) A separate Famicom 4P path exists: port 4 = `RETRO_DEVICE_FC_4PLAYERS` (0x301). |
| 7 | SGDK 2.11 MIT, ~30 prebuilt `out/release/rom.bin`, joy-test, gentris, avoid platformer and sonic | DOWNGRADE | The MIT license, GCC runtime exception text, joy-test TeamPlayer support (main.c:34-35, 60-61) and EA 4-Way detection (main.c:132; `JOY_init` auto-detects it in src/joy.c:101-103) are confirmed. gentris main.c:1-22 is confirmed (procedural graphics and sound, game-over menu, 1P only per input.c:99). 30 sample `out/release/rom.bin` files are tracked. **Corrections:** v2.11 is still the latest tag (git ls-remote). However, at tag v2.11 the prebuilt ROMs live at `out/rom.bin` (`out/release/rom.bin` returns 404), and `sample/game/gentris` does not exist there (404; the header says "written ... 08/2026"). Pin commit ee6870a, not "2.11". **Also avoid** these samples, which bundle commercial game audio: `snd/xgm-player` (Actraiser, Super Metroid, Shinobi ...), `snd/sound-test` (Sonic 1 SFX, Road Rash, Streets of Rage 2, Toy Story, Bad Apple), `linkcable/pacman` (Comix Zone, Contra Hard Corps, MK "fatality", Sonic, sega.wav, Pac-Man name) and `demo/bad-apple`. The readme states MIT for the "library and custom tools" only. Sample licensing is implied by the repo, which is acceptable for asset-free samples (joy-test, hello-world, gentris are code-only). |
| 8 | MAME free ROMs: non-commercial, mamedev.org-only distribution | UNCHECKABLE (stays REPORTED) | mamedev.org is denied by the egress proxy (CONNECT 403), and the Wayback Machine is unreachable. Search summaries agree: Alien Arena is free non-commercial with an acknowledgement checkbox, Gridlee is non-commercial, and the site notice says "approved for free distribution on this site only ... must obtain permission from the original owners". Robby Roto is sometimes called public domain (Edwards 2005, arcade-museum), but Fenton's 1999 grant reads "free non-commercial uses". The conclusion not to bundle these ROMs stands. |
| 9 | FBNeo HB flag is not a license, 77 Neo Geo HB sets, SNK sm1/sfix/000-lo required | CONFIRMED (count is 78) | `BDF_HOMEBREW (1<<8)` is at burn.h:553. d_neogeo.cpp:1672-1674 lists sm1.sm1, sfix.sfix and 000-lo.lo without `BRF_OPT`, and the same applies in the MVS descriptor (:1720-1722). neopen.sp1 (:1665) is `BRF_OPT`. Every HB driver uses the `"neogeo"` BIOS parent. My parse at 6354154 finds **78** HB drivers. The earlier list misses `midnight` ("Midnight Wanderers ... (HB, Demo)", 2026, Z-Team), which is also a conversion of a copyrighted game. |
| 10 | FBNeo license: no profit, no donations, no ROMs without rights | CONFIRMED (incomplete) | Upstream raw `src/license.txt` matches the clone, and lines 3-7 are verbatim. **Missed:** "You must make public any changes you make to the source code". FBNeo "is also subject to the terms of the MAME license" pinned at mame@5cef4e1. That old MAME license says "Redistributions may not be sold, nor may they be used in a commercial product or activity", and modified redistributions must include complete source. **Architecture impact (ASSUMED interpretation):** option (b) lockstep ships the FBNeo WASM core and the ROM bytes to every client. That is literally "distribute FB Neo with ROM images" for any user-uploaded set. Option (a) streaming keeps ROM bytes on the server. |
| 11 | A self-authored NES cabinet ROM closes the 4P and arcade gaps | DOWNGRADE (still ASSUMED) | The licenses are as stated: nrom-template is all-permissive (README.md:321-328; tools/pilbmp2nes.py has its own all-permissive header) and allpads is zlib. However, `allpads/src/pads.s` is a 2-pad reader "used by lowlevel and serialwatch only"; the Four Score 24-bit logic is in `identify.s`/`padtest.s`. A NES ROM exercises only the NES core path. It does not close the **FBNeo arcade** fixture gap, because no FBNeo set is redistributable. One possible route exists (code VERIFIED, feasibility ASSUMED): libretro-FBNeo loads ROMs with unknown CRC from `system/fbneo/patched/` when "Allow patched romsets" is on (default `true`, name and size must match; retro_common.cpp:60, :266-270; libretro.cpp:884-895, :1031-1036), and also in "romdata" mode. A self-written ROM for a BIOS-less FBNeo driver is therefore technically loadable. |

**Additional findings**

- fceumm `fceumm_ramstate` defaults to "fill $ff". Its "random" option is seeded from the ROM MD5 via a local xorshift32 (src/fceu.c:376-458), so it is deterministic across replicas. Pin it explicitly anyway.
- The fixture table in section 5 should mark croom as GPLv3-with-source only, because of the `.time` embedding.
- The SGDK fixtures should reference commit ee6870a.
