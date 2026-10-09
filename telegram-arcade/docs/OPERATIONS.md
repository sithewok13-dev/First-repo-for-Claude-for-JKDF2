# Operations

How to deploy, back up, restore, upgrade and watch the arcade. First-time
Telegram and BotFather setup is in `PILOT_GUIDE.md`. Every setting is in
`.env.example`.

## 1. What runs

| Piece | What it does | Where |
|---|---|---|
| `arcade` | One Node 22 process: HTTP API, WebSocket rooms, Telegram bot (polling or webhook), one emulator child process per running room, ROM validation in short-lived child processes | Docker image built from `Dockerfile` |
| `caddy` (profile `public`) | HTTPS with automatic certificates, reverse proxy, WebSockets | `caddy:2.10.2-alpine` |
| `tunnel` (profile `tunnel`) | Cloudflare Tunnel, for home hosting without open ports | `cloudflare/cloudflared:2026.9.0` |
| `backup` | Daily online backup into `deploy/backups/` | same image as `arcade` |
| `bot-api` (profile `local-bot-api`, optional) | Self-hosted Telegram Bot API server, for uploads over 20 MB through Telegram | `aiogram/telegram-bot-api:10.3` (**untested**) |

**Data.** Everything lives in one volume (`/data`, or `DATA_DIR`):
- `arcade.sqlite`: groups, members, shelf, records, chat, audit, settings.
- `blobs/`: private ROM files, content-addressed, 0700/0600 permissions.
- `checkpoints/`: the last 3 save points per room.
- `tmp/`: uploads in progress.

The image runs as an unprivileged user with a read-only root filesystem.
Emulator and validator children get no secrets in their environment.

## 2. Deploy

### A. VPS (or any always-on Linux box) with HTTPS on ports 80/443

```sh
git clone <this repository> && cd <repo>/telegram-arcade
cp .env.example deploy/.env
# edit deploy/.env:
#   BOT_TOKEN, BOT_USERNAME, MINIAPP_SHORT_NAME, PUBLIC_URL=https://arcade.example.com,
#   PUBLIC_HOST=arcade.example.com, COMPOSE_PROFILES=public
chmod 600 deploy/.env
# the backup job runs as the image's unprivileged user (uid 1000)
mkdir -p deploy/backups && sudo chown 1000:1000 deploy/backups
# Point the domain's DNS A/AAAA record at the server first.
docker compose -f deploy/docker-compose.yml up -d --build
docker compose -f deploy/docker-compose.yml logs -f arcade
```

The first build compiles the emulator cores from pinned sources. It takes
about 3–10 minutes and needs network access to GitHub and the npm registry.
Nothing proprietary is downloaded.

### B. Mac mini or home server, no open ports (Cloudflare Tunnel)

1. In Cloudflare Zero Trust, create a tunnel and add a public hostname, for
   example `arcade.example.com`, with service `http://arcade:8080`. Copy the
   tunnel token.
2. In `deploy/.env`, set `COMPOSE_PROFILES=tunnel`,
   `CLOUDFLARE_TUNNEL_TOKEN=...`, `PUBLIC_URL=https://arcade.example.com`
   and `UPDATES_MODE=polling`. Polling needs no inbound connection.
3. Keep `MAX_UPLOAD_BYTES` at or below about 95 MB. The free plan limits
   request bodies to 100 MB.
4. Run `mkdir -p deploy/backups` (on Linux also
   `sudo chown 1000:1000 deploy/backups`), then
   `docker compose -f deploy/docker-compose.yml up -d --build`.

On macOS, use Docker Desktop or OrbStack.
- **Not yet verified:** building on Apple Silicon. Node and Caddy publish
  arm64 images; check that the emsdk image runs or builds for arm64. If it
  does not, build the image on an x86 machine and copy it with
  `docker save | docker load`.
- Set the Mac to never sleep. Enable "Start up automatically after a power
  failure".

### C. Without Docker

The arcade needs Node ≥ 22.18. It has one runtime dependency (`ws`).

```sh
npm ci
npm run build:cores      # needs emsdk 6.0.12 (EMSDK=/path/to/emsdk), git, make, gcc, perl
npm run build:client
cp .env.example .env && $EDITOR .env   # then export it, e.g. `set -a; . ./.env; set +a`
npm start
```

You can also skip compiling the cores and copy them out of the Docker image:

```sh
id=$(docker create telegram-arcade:latest)
docker cp "$id":/app/native/build ./native/
docker rm "$id"
```

Put the server behind any HTTPS reverse proxy that supports WebSockets. Set
`TRUST_PROXY=1` only in that case.

### Telegram updates: polling or webhook

- **`polling`** (default) works behind NAT and tunnels.
- **`webhook`** needs `WEBHOOK_SECRET` and a public https URL on port 443,
  80, 88 or 8443. Telegram then posts to `PUBLIC_URL/telegram/webhook`.

## 3. Health and logs

- `GET /healthz` returns `{ok, db, cores, rooms, sessions}`. It answers 503
  if the database or cores are unavailable. Docker uses it as the container
  health check.
- Logs go to stdout in text form. Levels are set by `LOG_LEVEL`. The logger
  redacts bot tokens. Launch data, ROM contents, file paths from a local Bot
  API server and IP addresses are not logged. Caddy keeps no access log.
- Worth alerting on:
  - repeated `worker restarted` lines (an emulator crash loop);
  - `storage` warnings;
  - `sessions were interrupted by an unclean stop` at startup;
  - `/healthz` failing.

## 4. Backups and restore

**What a backup holds:**
- a consistent online snapshot of the database (`VACUUM INTO`);
- every ROM file the database references;
- the current save points;
- a manifest of SHA-256 hashes.

Blobs shared with the previous backup are hard-linked, so daily backups cost
little space. Backups contain your groups' private ROMs: keep them as private
as the server itself.

- **Automatic:** the compose `backup` service writes
  `deploy/backups/arcade-<UTC time>/` daily and keeps `BACKUP_KEEP` (14 in
  compose). Copy that directory off the machine, for example with
  `restic`/`rclone` to R2 or B2 (see `COSTS.md`).
- **Manual:**
  `docker compose -f deploy/docker-compose.yml exec arcade node scripts/backup.ts /data/manual-backups`
  (or `npm run backup -- <dir>` without Docker).
- **Check a backup:** `node scripts/restore.ts <backup dir> --verify-only`
  re-hashes every file and runs SQLite's integrity check.

**Restore drill** (tested by `test/unit/backup.test.ts`; rehearse it once):

```sh
docker compose -f deploy/docker-compose.yml stop arcade
docker compose -f deploy/docker-compose.yml run --rm --no-deps \
  -v "$PWD/deploy/backups:/backups:ro" arcade \
  node scripts/restore.ts /backups/arcade-20261009T030000Z --force
docker compose -f deploy/docker-compose.yml start arcade
```

`restore` checks the whole backup before writing anything. With `--force`,
it moves the current data into `/data/.before-restore-<time>/` instead of
deleting it. Delete that folder once you are satisfied. On start, the server
applies any newer database migrations. Rooms then offer **Resume** from the
restored save points.

**Moving to another machine** is the same: back up, copy the backup
directory, restore, start.

## 5. Upgrades

```sh
git pull
docker compose -f deploy/docker-compose.yml up -d --build
```

What happens on stop:
- `SIGTERM` makes the server save a checkpoint for every running room.
  Compose allows 30 s.
- Players see "server restarting" and reconnect automatically.
- The host taps **Resume** to continue.

Database migrations (`server/db/migrations/NNN_*.sql`) run automatically and
are never edited after release.

**Core rebuilds** change the core build id. A rebuild happens when the
pinned FBNeo or FCEUmm revision, the patches, the frontend or the core
options change. Save points are tied to ROM + core build + options + adapter
(the *compat key*). Save points from an older build are refused, with a
clear "Start fresh" message, rather than risking a desynced game. Records
keep the compat key, so results from different builds stay distinguishable.

## 6. Limits and capacity

Default limits (all in `.env.example`):

| Limit | Default | Why |
|---|---|---|
| `MAX_ACTIVE_ROOMS` | 3 | one emulator process per running room |
| `MAX_VIEWERS_PER_ROOM` | 16 | WebSocket fan-out; players, queued people and moderators always get in |
| `MAX_UPLOAD_BYTES` | 64 MB | upload size |
| `MAX_EXTRACTED_BYTES` | 256 MB | zip contents after unpacking |
| `MAX_COMPRESSION_RATIO` | 200 | zip-bomb guard |
| `GROUP_QUOTA_BYTES` | 2 GB | storage per group |
| `GLOBAL_STORAGE_LIMIT_BYTES` | 40 GB | storage for the whole server |
| `ROOM_IDLE_RELEASE_SEC` | 300 | an empty room is checkpointed, paused, then released |
| `WORKER_HEAP_MB` | 192 | per emulator process (WASM memory is capped at 1 GB by the build) |
| per user | 4 connections | |
| chat | rate-limited, 500 characters | |
| votes | cooldown | |
| uploads | 1 validation at a time; 1 web upload in flight per user; 30 web uploads per group per hour | |

**Measured CPU cost per running room**, on one core of a 2.8 GHz Xeon at
60 fps (`scripts/bench-cores.ts`, `test-results/bench.json`):

| System | ms/frame | Share of one core |
|---|---|---|
| NES | 0.47 | 3% |
| CPS-1 | 1.6 | 10% |
| CPS-2 | 1.9 | 12% |
| Neo Geo | 4.7 | 28% |

FBNeo was measured with synthetic romsets, whose CPUs run garbage code.
Real games may cost more; re-measure with your own ROMs:
`node scripts/bench-cores.ts`.

**Pilot sizing:**
- 2 vCPU / 4 GB comfortably runs the default 3 rooms with 16 viewers each.
- Network use is about 12–46 kbps per viewer, plus each viewer's first
  download of the game files.
- Each viewer's device also runs the game. Weak phones are the real limit.
  The status line shows each device's own emulation cost, and the app warns
  when a device cannot keep up. See `COMPATIBILITY.md`.

## 7. Security operations

- **Secrets.**
  - `BOT_TOKEN` and `WEBHOOK_SECRET` live only in `deploy/.env` (mode 600).
  - If the token leaks: `/revoke` in BotFather, put the new token in
    `deploy/.env`, and restart. All sign-ins stay valid, because they are
    the server's own tokens.
- **ROM privacy.**
  - No public URLs: every file download needs a signed-in member of the
    owning group, and the server never lists files across groups.
  - Files are cached on the devices of people who play or watch, inside the
    app's private storage. Each person can remove them under *Controls →
    Game files on this device*.
- **Access control.**
  - Membership is checked with Telegram at sign-in and on every WebSocket
    connection, and is cached for `MEMBERSHIP_TTL_SEC`.
  - When the bot is an admin, leaving the group ends access at once.
  - `ALLOWED_CHAT_IDS` restricts which groups the bot serves.
- **Removing a group's data.** Remove the bot from the group: sessions are
  revoked and the room stops. A full purge of a group's shelf and records is
  an operator task (SQL plus blob cleanup) and is not yet a command. It is
  tracked in `PROGRESS.md`.
- **Dev mode.** `DEV_MODE=1` must never be used on a public server. The dev
  sign-in only answers direct loopback requests without proxy headers, and
  production config refuses non-https `PUBLIC_URL`s.

## 8. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Button opens the app but it says "Could not verify your Telegram identity" | `BOT_TOKEN` differs from the bot the button belongs to, or the clock is off | check the token; sync time (NTP) |
| "This launch link is too old" | launch data older than `INITDATA_MAX_AGE_SEC` | reopen from the group |
| "Only its members can open it" | user not in the group, or the bot is not an admin and Telegram hides members | add the bot as admin (no rights needed) |
| Uploads in the group are ignored | bot is not an admin (privacy mode) | make it admin, or reply `/add` to the file |
| "too big for Telegram's bot download" | over 20 MB on the cloud Bot API | upload from the Mini App's Shelf tab, or run the local Bot API server |
| Everyone's game stutters (status line: low fps, growing "behind live", stalls) | server CPU saturated (check `docker stats`) or network | fewer `MAX_ACTIVE_ROOMS`, bigger VM |
| One phone shows "This device is struggling to run this game" | that device needs more than 75% of the frame time just to emulate (shown as "emulation N ms/frame") | play on a faster device; watching still works |
| Room did not resume after a restart | save point incompatible (core or ROM changed) | Start fresh |
