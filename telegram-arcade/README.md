# Telegram Arcade

A Telegram bot and Mini App that turn a private Telegram group into a shared
retro arcade:
- One game runs per group, and everyone sees the same live game.
- Players take seats, queue, pass the controller and vote on what to play.
- The group keeps a private shelf of its own games and its own records.

**How it works**
- The server runs the one authoritative copy of each game: a WebAssembly
  emulator core in a worker process.
- Every frame it fixes the controller inputs of whoever holds each seat, and
  sends them to every player's and spectator's device.
- Each device runs the same core as a verified replica. Picture and sound are
  native, bandwidth is tiny, and nobody can change the game except through
  their own seat.
- See `docs/ARCHITECTURE.md`.

## Status (2026-10-09)

**Demonstrated:** real gameplay in headless Chromium against the real server
(`test/e2e/slice.test.ts`):
- 2 players and 1 spectator in one shared game.
- Simultaneous input.
- A verified match result.
- A seat handed to the spectator without restarting.
- The creator disconnecting while the game keeps running.
- Touch input.
- Measured latency: median input-to-screen of 31 / 77 / 126 ms at
  0 / 50 / 100 ms round trip.

The rest of the product (identity, shelf, queue, modes, voting, roles, chat,
records, recovery, backups, packaging) is implemented. 164 automated tests
cover it: 155 unit and 9 end-to-end (see `docs/TESTING.md`).

**Not yet verified** (needs your devices, account and ROMs; see
`docs/COMPATIBILITY.md` and `docs/PILOT_GUIDE.md`):
- Any real Telegram client: iPhone, Android, Desktop, Web.
- Game controllers. Sound on real devices.
- Commercial arcade romsets. Only synthetic, CRC-matched test sets were
  used, for determinism.
- Performance on budget phones.
- The local Bot API server option.
- A public HTTPS deployment.

## Quick start (local, no Telegram)

Requirements: Node ≥ 22.18. You also need the emulator cores, from one of:
- `npm run build:cores`, which needs emsdk 6.0.12;
- copying them out of the Docker image (`docs/OPERATIONS.md` §2C).

```sh
npm ci
npm run dev          # prints one sign-in link per test user
```

Open two or three of the printed links in separate browser windows. In one of
them, go to **Shelf** → **Upload from device** →
`native/testroms/build/atc-versus.nes`, then **Start**. Take seats in two
windows and watch in the third.

## Deploy

`docs/OPERATIONS.md` covers:
- a VPS with Caddy and automatic HTTPS;
- a Mac mini / home server behind Cloudflare Tunnel;
- running without Docker.

`docs/PILOT_GUIDE.md` has the exact BotFather and group steps and a device
test plan.

```sh
cp .env.example deploy/.env    # fill in BOT_TOKEN, BOT_USERNAME, PUBLIC_URL, PUBLIC_HOST
docker compose -f deploy/docker-compose.yml up -d --build
```

## Tests

```sh
npm run typecheck
npm test             # unit: rooms, modes, governance, lockstep, cores, shelf, zip safety, Telegram, backups
npm run test:e2e     # real server + headless Chromium: gameplay slice, identity, restart/crash recovery
```

The FBNeo determinism and homebrew tests need the cores in `native/build`.
The homebrew tests also need `native/testroms/fetch-homebrew.sh`, and are
skipped otherwise.

## Repository layout

| Path | What |
|---|---|
| `server/` | Node 22 server (TypeScript, run directly). `room/` is the rule engine. `session/` and `emu/` are authoritative emulation. `shelf/` handles uploads and validation. `telegram/` is the bot and membership. `ws/` and `http/` are the gateway and API. `db/` holds SQLite and migrations. `ops/` holds backup and restore. |
| `client/` | Mini App (Preact): replica runner, input (touch, keyboard, gamepad), UI |
| `shared/` | Emulator core wrapper, wire protocol, system definitions |
| `native/` | Deterministic libretro frontend, FBNeo savestate patch, core build script, ATC test ROMs |
| `deploy/` | Docker Compose, Caddyfile |
| `scripts/` | dev server, client build, catalog, backup/restore, benchmarks |
| `test/` | unit and end-to-end tests |
| `docs/` | spec, architecture, research, compatibility, testing, operations, pilot, costs, licences, progress |

## Documents

| Document | Contents |
|---|---|
| `docs/SPEC.md` | the agreed product specification |
| `docs/PROGRESS.md` | what is done, what is staged, what is next |
| `docs/ARCHITECTURE.md` | decisions with sources |
| `docs/RESEARCH.md` | summary of the research |
| `docs/research/` | full reports and the verification ledger |
| `docs/COMPATIBILITY.md` | verified vs untested, per platform and feature |
| `docs/TESTING.md` | test evidence and how to reproduce it |
| `docs/OPERATIONS.md` | deploy, backup and restore, upgrades, limits, capacity |
| `docs/PILOT_GUIDE.md` | BotFather setup and the device test session |
| `docs/COSTS.md` | hosting estimates; nothing has been purchased |
| `docs/LICENSES.md` | emulator licences and the decisions they require |

## Content and licences

No commercial ROMs or BIOS files are included, and nothing downloads them.
Groups upload their own lawful copies, which stay private to the group.

FBNeo is non-commercial: no paid access and no donation requests. Its source
changes must be public. See `docs/LICENSES.md` for this and the GPL-2.0
obligations of FCEUmm.
