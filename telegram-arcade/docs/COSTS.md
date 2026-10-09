# Costs

**Nothing has been purchased or subscribed to.** Every number here is an
estimate for you to approve or reject. Prices were researched on 2026-10-08
(`research/hosting-cost.md`, verification in `research/VERIFICATION.md`).
Labels are as in `RESEARCH.md`. Vendor pricing pages that could not be opened
from the research environment are **REPORTED** and may be stale. Check them
before ordering.

## What drives cost

The arcade sends each viewer controller inputs, not video.

| Resource | Lockstep (this design) | Video streaming (rejected) |
|---|---|---|
| Egress per viewer | about 12–46 kbps of input records (computed from the wire format), plus a one-time download of the game files (cached per device) | 2–4 Mbps |
| Egress per 8-person room-hour | about 0.16 GB, plus first-time file downloads | 7–14 GB |
| Pilot month (about 120 room-hours) | under 50 GB, including file downloads | 0.9–1.7 TB |
| Server CPU per running room | one emulator worker. **Measured:** NES 0.47, CPS-1 1.6, CPS-2 1.9, Neo Geo 4.7 ms per frame on a 2.8 GHz Xeon (3–28% of one core at 60 fps; FBNeo measured with synthetic data) | emulator + video encoder: 0.5–1 vCPU |
| RAM | about 150 MB for the server + about 64–256 MB per running room | + encoder buffers |
| Disk | your ROM library (quota per group, default 2 GB) + database + 3 checkpoints per room | same |
| Special networking | none (HTTPS + WebSocket on 443) | UDP / TURN relays |

At pilot scale, bandwidth is irrelevant on every provider. What matters is
an always-on public HTTPS endpoint, a CPU that is not throttled, and latency
to the players.

## Monthly estimates for the pilot (lockstep)

The scenario is 1–3 groups, at most 8 people per room, about 120 room-hours a
month, a peak of 3 rooms at once, and up to 50 GB of ROMs.
- A domain costs about $0.87/month ($10.46/year), but a free subdomain from
  a tunnel provider also works.
- Off-site backup on Cloudflare R2 or Backblaze B2 costs under $1/month at
  50 GB.

| Option | Monthly | Notes |
|---|---|---|
| **Mac mini you already own + Cloudflare Tunnel (free)** | **≈ $3–5** | Electricity at about 15 W average, plus a domain and backup. No open ports. Depends on home power, internet uptime and upload. Tunnel restarts drop connections briefly (clients reconnect). Request bodies are limited to 100 MB on the free plan. |
| Oracle Cloud Always Free (Ampere A1, 2 OCPU / 12 GB) | ≈ $1.50 | VERIFIED quota. Risks: capacity shortages, reclamation of idle instances, and quotas already cut once in 2026. |
| Hetzner CX23 / CX33 (EU), if in stock | ≈ $12 | Cheapest mainstream. REPORTED: often out of stock since September 2026. The fallback CPX22 is about $28. US locations are pricier. |
| Vultr / DigitalOcean / Linode, 2 vCPU / 4 GB / 80 GB | ≈ $21–26 | Predictable and in stock. Prices are REPORTED. |
| Fly.io performance-1x + 50 GB volume | ≈ $43–48 | Shared Fly vCPUs are throttled to a 6.25% baseline (VERIFIED). Unsuitable for a 60 fps emulator. |
| Hybrid (cloud front + home compute) | ≈ $3–12 | Only pays off for video streaming, which we do not use. |

**Recommendation for the pilot:**
- If you already have an always-on Mac mini or similar machine, start there
  with Cloudflare Tunnel (≈ $3–5).
- Otherwise use a 2 vCPU / 4 GB VPS near your friends (≈ $12–26).

The same Docker Compose file works on both, and moving is a backup and
restore.

## One-off and optional costs

| Item | Cost | When needed |
|---|---|---|
| Telegram bot, Mini App, Bot API | free | always |
| Self-hosted Telegram Bot API server (uploads over 20 MB through Telegram) | free software; needs your `api_id`/`api_hash` from my.telegram.org | only for large uploads through Telegram. The web upload in the Mini App handles up to `MAX_UPLOAD_BYTES` without it. |
| Domain name | about $10–12/year | for a VPS with Caddy, or a named Cloudflare Tunnel |
| Game controllers for testing | your own | device pilot |
| Commercial ROMs and BIOS | your own lawful copies | to play arcade games. Never bundled or downloaded by this project. |

## Limits that keep cost bounded

All of these are configurable in `.env.example`:
- `MAX_ACTIVE_ROOMS` (default 3) caps how many emulators run at once.
- `MAX_VIEWERS_PER_ROOM` (default 16) caps viewers per room.
- `GROUP_QUOTA_BYTES` (default 2 GB) and `GLOBAL_STORAGE_LIMIT_BYTES`
  (default 40 GB) cap storage.
- Idle rooms are checkpointed, paused and released after
  `ROOM_IDLE_RELEASE_SEC`.
- Removed games are deleted after `REMOVED_GAME_RETENTION_DAYS`.

## What would need your authorization

- Any server, VPS, domain or storage purchase.
- Paid TURN or video services. Not needed by this design.

Nothing on this list is required to run the pilot on hardware you already
own.
