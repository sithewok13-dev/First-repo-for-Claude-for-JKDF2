# Licences and content rules

This is not legal advice. It records what the components' licences say and
what this project does about it.

## Components

| Component | Licence | Shipped how | Obligations and what we do |
|---|---|---|---|
| **FBNeo** (arcade emulator), libretro/FBNeo at a pinned commit | FBNeo licence (non-commercial) + MAME licence for MAME-derived parts | compiled into `fbneo_cps12.wasm` / `fbneo_neogeo.wasm`, served to every player's browser | Four obligations: **no selling, renting or other monetary profit; no donation requests**; the full licence text shipped verbatim; source changes made public; no distribution with ROMs you have no right to distribute. **What we do:** the licence is copied next to the cores (`/cores/LICENSE-FBNeo.txt`). Our changes are `native/patches/*.patch` and `native/frontend/`, with build instructions in `native/build-cores.sh` and a `SOURCES.txt` notice. No ROMs are bundled. |
| **FCEUmm** (NES emulator) | GPL-2.0 | compiled into `fceumm.wasm`, served to browsers | Corresponding source must be available to the people you give the binary to. That covers FCEUmm plus the frontend linked with it. **What we do:** the licence is shipped (`/cores/LICENSE-FCEUmm.txt`), and the upstream revision and our frontend are named in `SOURCES.txt`. |
| Emscripten runtime (in the `.mjs` glue and the wasm) | MIT / University of Illinois NCSA | in the cores | licence shipped as `/cores/LICENSE-Emscripten.txt` |
| `ws` | MIT | server dependency | — |
| Preact | MIT | bundled into the Mini App | licence shipped as `/THIRD-PARTY-NOTICES.txt` |
| esbuild, TypeScript, Playwright | MIT / Apache-2.0 | build and test tools only | not shipped |
| Telegram Web App script | Telegram | loaded from telegram.org by the Mini App | — |
| ATC test cabinet ROMs, font and source (`native/testroms/`) | MIT (ours) | committed | — |
| NES homebrew used in tests | GPL-3.0+, zlib, BSD-3, GNU All-Permissive | **built locally** from pinned upstream sources by `fetch-homebrew.sh`; not committed or redistributed | — |
| Project source (server, client, frontend, patches) | not yet chosen by the owner (`UNLICENSED` in `package.json`) | — | see the decision below |

## Decisions for the owner before going beyond a private pilot

1. **Publish the emulator changes.**
   - FBNeo's licence says "You must make public any changes you make to the
     source code". GPL-2.0 requires FCEUmm's source plus our frontend for
     anyone who receives `fceumm.wasm`. Every player's browser receives it.
   - The simplest compliance is to publish `native/` (patches, frontend,
     build script) in a public repository and link it from `SOURCES.txt`.
     It is self-contained and holds no secrets.
   - This repository is not public at the time of writing. **Your call:**
     publish `native/` (recommended), or keep the pilot to people who can be
     given the source on request.
2. **Pick a licence for the frontend.** The frontend is
   `native/frontend/fe.cpp`, linked into `fceumm.wasm`, so it must be offered
   under GPL-2.0-compatible terms. MIT or GPL-2.0-or-later both work. The rest
   of the project can be licensed however you like.
3. **Stay non-commercial.** No paid access, ads, sponsorships or donation
   links anywhere in a deployment that serves FBNeo.

## Game content rules (built into the product)

- The project bundles **no commercial ROMs and no proprietary BIOS files**,
  and never downloads any automatically. Nothing in the build or the server
  fetches game content from the internet.
- Groups upload their own lawful copies. Uploads stay **private to the
  group**:
  - files are stored content-addressed with 0600 permissions;
  - they are served only to signed-in members of that group;
  - there are no public URLs and no catalogue across groups.
- Neo Geo games need `neogeo.zip`. The shelf asks the group for it; the
  project does not supply it.
- The MAME "free ROMs" may only be distributed from mamedev.org. We do not
  bundle them.
- Because the game runs on each viewer's device, each viewer's Telegram app
  keeps a private cached copy of the game files. Anyone can clear it under
  *Controls → Game files on this device*.
