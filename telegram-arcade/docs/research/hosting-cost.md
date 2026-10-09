# Hosting options and pilot cost: private Telegram arcade

Research date: **2026-10-08**. Area: hosting and monthly cost for a small pilot. The pilot has 1-3 Telegram groups and up to about 8 concurrent users per group. Usually 1 room is active, with a peak of 2-3. ROM storage is about 10-50 GB.

Status labels:
- **VERIFIED**: I read the primary text or code myself. A repo path and line are cited.
- **REPORTED**: from a secondary source or a web-search summary of a vendor page I could not open.
- **ASSUMED**: my own inference or a modelling assumption.

Environment limit: vendor pricing pages for Hetzner, DigitalOcean, Vultr, Linode, Apple, Backblaze, Twilio and Tailscale could not be fetched from this container (proxy 403). Their prices are therefore **REPORTED**. Primary text was readable for these:
- Fly.io docs (GitHub `superfly/docs`)
- Cloudflare docs (GitHub `cloudflare/cloudflare-docs`)
- Oracle's free-tier data file (oracle.com)
- Caddy docs (`caddyserver/website`)
- Tailscale client source
- Docker docs

Local copies are under the research workspace (not committed; sources are cited inline).

---

## 0. Bottom line

1. **Lockstep (architecture b) is very cheap to host. Streaming (architecture a) is not.**
   - Lockstep egress is about **0.16 GB per room-hour**, which is about 20 GB for 120 room-hours a month (ASSUMED model, §1).
   - Streaming egress is about **7-14 GB per room-hour**, which is about 0.9-1.7 TB a month for the same usage.
   - That is a 45-90x difference in bandwidth. Streaming also needs video-encode CPU and UDP/TURN.
2. **Pilot cost for lockstep:**
   - **About $0-10/month** on a small VPS: Oracle Always Free, or a Hetzner CX23/CX33 if it can be ordered.
   - **About $2-8/month** on an already-owned Mac mini behind Cloudflare Tunnel. That is electricity plus an optional domain and backup.
   - **About $20-25/month** on DigitalOcean, Vultr or Linode at 2 vCPU / 4 GB / 80 GB.
   - **About $42-47/month** on Fly.io. Fly needs a `performance` CPU because its `shared` vCPUs are throttled to a 6.25% baseline (VERIFIED, §2.5).
3. **Pilot cost for streaming:** about **$10-45/month** on Hetzner EU (20 TB included), about **$95-120/month** on Fly. TURN is the cost trap:
   - Twilio TURN at **$0.40/GB** (REPORTED) could add about $100+/month.
   - Cloudflare TURN is **$0.05/GB after a 1,000 GB free tier** (VERIFIED).
   - Self-hosted coturn costs only VPS egress.
4. **The Mac mini is a good free compute node for lockstep. It is a poor origin for streaming.**
   - Cloudflare Tunnel public hostnames carry HTTP/WebSocket but not WebRTC UDP. Cloudflare's terms restrict serving video over the CDN on Free, Pro and Business plans (VERIFIED, §3.3).
   - Home upload must carry 16-32 Mbps per streamed room.
5. **Hetzner caveats (2026):**
   - Prices rose on 2026-04-01 and again for new orders on 2026-06-15.
   - The cheap CX and CAX lines have been **mostly unorderable since early September 2026** (REPORTED).
   - The fallback CPX22 is about €19.49-19.99.
   - Treat Hetzner as "great if you can get a CX23/CX33/CAX". Otherwise compare against DO, Vultr and Linode.
6. **Oracle Always Free was cut.** The live Oracle data file now says Ampere A1 is **1,500 OCPU-hours and 9,000 GB-hours per month** (2 OCPU / 12 GB). It was previously 4 / 24. Outbound transfer is still up to **10 TB/month** (VERIFIED). The risks are capacity and idle reclamation.

**Recommended pilot setup (ASSUMED synthesis):**
- Run the Node server, with its authoritative WASM emulator, in Docker Compose on one host. That host is either an already-owned Mac mini or a small cloud VM.
- For the Mac mini, expose it with Cloudflare Tunnel (WebSocket) and keep ROMs on the box.
- Back up the DB, records and ROM library nightly with restic to Cloudflare R2 or Backblaze B2. This costs under $1/month at 50 GB.
- Keep the same Compose file deployable to a $6-24 VPS. Use it as a fallback, or as the primary host if home reliability or latency to players is a problem.

---

## 1. Workload model (drives every number below)

All of §1 is **ASSUMED** unless marked otherwise.

| Item | Lockstep (b) | Streaming (a) |
|---|---|---|
| Server CPU per room | One authoritative FBNeo WASM instance in Node at 60 fps. CPS-2 and Neo Geo are light (68000 + Z80); see `docs/research/emulator-cores.md` §1.12. Guess: 10-30% of one modern vCPU per room. **Must benchmark.** | Emulator plus a video encoder per room (x264 or HW encoder) plus WebRTC/SRTP. Guess: 0.5-1 vCPU per room in software. |
| RAM | Node about 100-200 MB, plus per-room core heap with the ROM loaded (64-256 MB). About 1 GB at a 3-room peak. | Same, plus encoder buffers. 2-4 GB. |
| Disk | OS + app about 10 GB, ROMs 10-50 GB, SQLite/records < 1 GB | Same |
| Egress per client | About 92 B on the wire per frame message × 60 Hz = **about 44 kbps (20 MB/h)**. Basis: the project's own `Frames` message (`shared/protocol.ts:58-62`) is about 16 B payload + 2 B WS header + 22 B TLS 1.3 record + 52 B TCP/IPv4 with timestamps. Batching lowers this. | 2-4 Mbps per viewer (H.264, 60 fps, arcade 384×224 upscaled) = 0.9-1.8 GB/h |
| Egress per room (8 users) | **About 0.16 GB per room-hour** | **7.2-14.4 GB per room-hour** |
| Monthly egress, typical (120 room-hours: 1 room × 4 h/day) | about 19 GB, plus ROM/core first downloads (about 5-20 GB; clients cache) → **under 50 GB** | **0.86-1.73 TB** |
| Monthly egress, heavy (360 room-hours) | about 57 GB → under 100 GB | **2.6-5.2 TB** |
| Pathological (3 rooms 24/7) | about 350 GB | 16-32 TB |
| Inbound needs | HTTPS + WSS on 443 only | HTTPS + UDP media ports (or TURN/TCP-443 fallback) |

Consequences:
- For lockstep, **bandwidth allowances are irrelevant on every provider** (0.5-20 TB included). The choice comes down to CPU behaviour, price, region and reliability.
- For streaming, **egress price and UDP reachability dominate**.

---

## 2. Small cloud VMs

Exchange rate used: **€1 ≈ $1.17 (ASSUMED)**. Prices exclude VAT.

### 2.1 Comparison table

| Provider / plan | vCPU / RAM / disk | Price per month | Included egress; overage | Status |
|---|---|---|---|---|
| **Hetzner CX23** (cost-optimized, x86, EU only) | 2 shared / 4 GB / 40 GB | **€5.49** (was €3.99 before 2026-06-15; one snapshot says €5.99) + **€0.50 IPv4** | 20 TB (EU); about €1/TB over | REPORTED (docs.hetzner.com price-adjustment page via search; IPv4 page) |
| **Hetzner CX33** | 4 shared / 8 GB / 80 GB | **€8.49** (one snapshot €8.99) + €0.50 | 20 TB | REPORTED |
| **Hetzner CAX11** (ARM Ampere, EU only) | 2 / 4 GB / 40 GB | **€5.99** (one snapshot €6.49) + €0.50 | 20 TB | REPORTED |
| **Hetzner CAX21** (ARM) | 4 / 8 GB / 80 GB | **€10.49** | 20 TB | REPORTED |
| **Hetzner CPX22** (fallback when CX/CAX are sold out) | 2 / 4 GB | **€19.49-19.99** (was €7.99) | 20 TB EU; US/SIN plans have 0.5-5 TB, and SIN overage is about €7.40/TB | REPORTED |
| Hetzner CCX13 (dedicated vCPU) | 2 dedicated / 8 GB | **€42.99** (was €15.99, +169%) | 20 TB+ | REPORTED |
| **DigitalOcean Basic** | 1 / 2 GiB / 50 GiB → $12; 2 / 4 GiB / 80 GiB → **$24** ($4 512 MiB and $6 1 GiB tiers exist) | $12 / $24 | 2,000 / 4,000 GiB, pooled per team; **$0.01/GiB** over | REPORTED (digitalocean.com/pricing/droplets via search). Per-second billing from 2026-01-01 (REPORTED). |
| **Vultr Regular Performance** | 1 / 1 GB / 25 GB → $5; 1 / 2 GB / 55 GB → $10; 2 / 4 GB / 80 GB → **$20** | $5 / $10 / $20 | 1 / 2 / 3 TB; **$0.01/GB** over | REPORTED. The search index of vultr.com/pricing may be about 1.5 years old. Uses previous-generation Intel. |
| **Akamai / Linode Shared** | Nanode 1 / 1 GB / 25 GB → $5; 1 / 2 GB / 50 GB → $12; 2 / 4 GB / 80 GB → **$24** | $5 / $12 / $24 | 1 / 2 / 4 TB pooled; **$0.005/GB** over (most regions) | REPORTED (akamai.com/cloud/pricing via search) |
| **Fly.io** `performance-1x` 2 GB | 1 dedicated / 2 GB | **$33.00** (iad/ewr), **$38.08** (fra), $34.27 (ams) | none included; **$0.02/GB** NA/EU, $0.04 APAC, $0.12 Africa/India | **VERIFIED** (`flydocs/about/pricing.mdx:20-33`; region multipliers `flydocs/snippets/RegionPricingSelector.jsx:3-27`) |
| Fly.io `shared-cpu-1x` 1 GB / `shared-cpu-8x` 2 GB | shared | $6.70 / $17.55 (iad) | same | VERIFIED. Prices computed from the snippet's per-second rates. |
| Fly volume | n/a | **$0.15/GB-month** (50 GB = $7.50); snapshots $0.08/GB after 10 GB free | n/a | VERIFIED (`pricing.mdx:28-29`) |
| **Oracle Cloud Always Free** A1 (Arm) | **2 OCPU / 12 GB total** ("1,500 OCPU hours and 9,000 GB hours per month"); 200 GB block | **$0** | **Up to 10 TB/month** | **VERIFIED** (oracle.com `/a/ocom/docs/oci-free-tier_v1.json`, fetched 2026-10-08, saved at `hosting-cost/oracle/oci-free-tier_v1.json`) |

### 2.2 Hetzner notes
- **2026 repricing (REPORTED).** Hetzner applied a general increase of up to about 37% on 2026-04-01, covering existing and new products (Hetzner press statement). A second increase applied to new orders and rescales from **2026-06-15**: CX and CAX rose about 1.3-1.4x, CPX and CCX about 2.1-2.8x. Existing servers keep their price unless rescaled.
- **Availability (REPORTED).** Since about 2026-09-02, the cost-optimized CX and CAX plans show "not available", with brief restocks. Hetzner's status page lists it as "limited availability of Cloud plans". Trackers (hetzner.thegoated.dev, bex.co 2026-09-24, stackvaluelab) report intermittent restocks in FSN/HEL that sell out within minutes to hours.
- **Locations (REPORTED, docs.hetzner.com/cloud/general/locations).**
  - Falkenstein, Nuremberg, Helsinki (EU); Ashburn VA and Hillsboro OR (US); Singapore.
  - CX and CAX are EU-only. US and SIN have only the shared/dedicated AMD lines and smaller traffic allowances.
- Volumes cost about €0.0572/GB-month after April 2026 (REPORTED, costgoat). 50 GB is about €2.86.
- **CPU behaviour (ASSUMED).** Shared vCPU with no hard burst quota like Fly's. Noisy neighbours are possible, so benchmark steal time.
- **WebSockets.** It is a plain VM with a public IPv4/IPv6, so any TCP/UDP service works. This is ASSUMED, but it is the nature of an IaaS VM.

### 2.3 DigitalOcean, Vultr, Linode notes
- All three are plain VMs, so WebSockets, UDP and TURN work. Each has many regions in NA, EU and APAC (REPORTED).
- DO's transfer is pooled across a team and billed at $0.01/GiB overage. Inbound is free (REPORTED, docs.digitalocean.com/platform/billing/bandwidth).
- Linode raised Shared and Dedicated plan prices by 20%, leaving the Nanode at $5. It also cut overage from $0.01 to **$0.005/GB** (REPORTED, Akamai blog "pricing update").
- Benchmarks (REPORTED, Better Stack): in Geekbench 6 single-core, Vultr AMD scored about 1,926 and DO Intel about 772 on 2 vCPU / 4 GB. Single-thread speed matters for the per-room emulator.

### 2.4 Oracle Always Free notes
- **VERIFIED** from Oracle's free-tier data file:
  - A1: "Arm-based Ampere A1 cores and 12 GB of memory usable as 1 VM or 2 VMs", "1,500 OCPU hours and 9,000 GB hours per month".
  - "Outbound Data Transfer: Up to 10 TB per month".
  - "Block Volume: Up to 2 block volumes, 200 GB total. Plus 5 volume backups."
  - Object Storage: 20 GB.
  - Also 2 AMD micro VMs (1/8 OCPU, 1 GB).
- **VERIFIED** (oracle.com/cloud/free FAQ): "Accounts left idle for 30 days or more may be deemed abandoned and become eligible for suspension or termination." Also: "Availability to Free Tier is subject to capacity limits."
- **REPORTED** (InfoQ, 2026-07): the cut from 4 OCPU / 24 GB to 2 / 12 took effect around **2026-06-15** without announcement.
- **REPORTED** (docs.oracle.com resourceref via search): idle Always Free compute may be **reclaimed**. "Idle" means that over 7 days, 95th-percentile CPU < 20%, network < 20%, and memory < 20% (A1 only). A pilot arcade that is idle most days would match this. Mitigation: convert to Pay-As-You-Go, which stays free within limits (REPORTED).
- **ASSUMED:** Always Free compute is created only in the tenancy's home region. "Out of host capacity" errors are common for A1 in popular regions. Free-tier support is community-only.
- Verdict: a great $0 option for a pilot, but **not something to depend on without a fallback**.

### 2.5 Fly.io notes (all VERIFIED from `superfly/docs` @ f3525ef, 2026-10-08)
- **No free tier.** The trial lasts up to 2 hours of Machine runtime or 7 days (`pricing.mdx:95-99`). Credit card required (`:115`).
- **The CPU quota is the decisive issue** (`machines/cpu-performance.mdx:9-24`):
  - A `shared` vCPU gets a **5 ms per 80 ms (6.25%) baseline quota**, with a burst balance (initial 5 s, maximum 500 s).
  - When the balance runs out, the cgroup is throttled for the rest of each 80 ms period.
  - A 60 fps authoritative emulator that needs 10-30% of a core will drain the burst balance in minutes. It then stalls up to about 75 ms at a time, which breaks lockstep pacing.
  - So use `performance-1x` ($33-38/month).
  - A `shared-cpu-8x` ($17.55, iad) has a 40 ms per 80 ms baseline, about 0.5 core. It **might** sustain 1-2 rooms (ASSUMED; benchmark first).
- **WebSockets** work through Fly Proxy: the docs' Node WebSocket chat example (`app-guides/6pndemochat.mdx:17`) and the deep-dive list (`deep-dive/index.mdx:34`). Idle timeout is configurable with `http_options.idle_timeout` (`reference/configuration.mdx:373-378`).
- **UDP** (WebRTC/TURN) needs a **dedicated IPv4 at $2/month** and binding to `fly-global-services` (`networking/udp-and-tcp.mdx:10-20`; `pricing.mdx:30`).
- Egress costs $0.02/GB NA/EU (`pricing.mdx:33`). Inbound is free. Volumes cost $0.15/GB-month (`:28`). The first 10 single-host TLS certificates are free (`:32`).
- 17 regions are listed in the pricing selector: ams, iad, ord, dfw, fra, jnb, lhr, lax, cdg, sjc, gru, ewr, sin, arn, syd, nrt, yyz (`RegionPricingSelector.jsx:3-21`).

---

## 3. Mac mini at home

### 3.1 Power and electricity
- Apple support article 103253 (REPORTED: search summary of support.apple.com/103253; not fetchable here). Figures are measured at the wall, including PSU losses:
  - **Mac mini M4** (16 GB / 256 GB): **4 W idle, 65 W max**.
  - M4 Pro: 5 W idle, 140 W max.
  - **Mac mini M2** (24 GB / 2 TB): **7 W idle, 50 W "CPU max"**.
  - M2 Pro: 7 W idle, 100 W max.
- Independent measurement (REPORTED, ServeTheHome): M4 at 4-6 W idle and about 40-45 W under heavy load.
- Electricity at $0.15-0.30/kWh, 730 h/month (computed):

| Average draw | kWh/month | $/month at $0.15-0.30 |
|---|---|---|
| 4 W (M4 idle) | 2.9 | $0.44-0.88 |
| 7 W (M2 idle) | 5.1 | $0.77-1.53 |
| 15 W (idle most of the day + a few hours of 1-3 rooms; ASSUMED typical lockstep pilot) | 11.0 | **$1.64-3.28** |
| 30 W (constant moderate load / streaming encode) | 21.9 | $3.28-6.57 |
| 65 W (M4 max, 24/7: worst case) | 47.5 | $7.12-14.24 |

- US reference (REPORTED): EIA's STEO projects an average residential price of 18.2 ¢/kWh for 2026. Electric Choice, citing EIA, gives 18.83 ¢/kWh in April 2026, ranging from 12.35 ¢ (ND) to 46.62 ¢ (HI).
- Hardware, only if not already owned (REPORTED): M4 base $599 MSRP, often discounted to $499-549 in 2026. One source says US prices rose by $100 after a 2026-08-25 refresh. Amortised over 36 months, $599 is about **$16.6/month**. Buying a Mac only for this costs more than a $6-24 VPS.

### 3.2 Exposing it: Cloudflare Tunnel (free plan)
- **VERIFIED** (`cloudflare-docs` @ baa31ec, 2026-10-08):
  - Tunnel is marked available on all plans (`tunnel/index.mdx:26` `<Plan type="all" />`).
  - It uses outbound-only connections with no inbound ports. "Each tunnel maintains four long-lived connections to two Cloudflare data centers" (`tunnel/index.mdx:30-45`).
  - Limits: 1,000 tunnels per account and 25 replicas per tunnel (`cloudflare-one/account-limits.mdx:65-71`).
- **WebSockets: supported on all plans** (`network/websockets.mdx:12, 51-53`). Caveats:
  - *"When Cloudflare releases new code to its global network, we may restart servers, which terminates WebSockets connections"* (`:69`). Use keepalives (`:73`).
  - Restarting or upgrading `cloudflared` also drops WebSockets: *"When the first instance of cloudflared is stopped, long-lived HTTP requests (for example, Websocket) ... will be dropped"* (`tunnel/features/locally-managed-tunnels/configuration-file.mdx:174`).
  - So the arcade must reconnect and resync cheaply. A savestate plus frame catch-up already fits architecture (b).
- **Timeouts** (`fundamentals/reference/connection-limits.mdx:15-36`):
  - Client-side keep-alive and HTTP/2 idle: 400 s.
  - Origin proxy idle timeout: 900 s; proxy read timeout 125 s (524). Tunnels have their own origin parameters.
  - A 60 Hz frame stream is never idle. Send app pings during lobby idle.
- **Upload limit: Free = 100 MB, Pro = 100 MB, Business = 200 MB per request** (`cache/concepts/default-cache-behavior.mdx:86-99`). ROM uploads through the Mini App over the tunnel must be **chunked below 100 MB**.
- **Large-file and video terms:**
  - *"Public hostname routes proxy traffic through Cloudflare. On Free, Pro, and Business plans, the service-specific terms require you to use a specific paid service to serve video and other large files"* (`tunnel/concepts/routing.mdx:31-35`).
  - *"...appears to be serving videos or a disproportionate amount of large files without using the appropriate paid service ... Cloudflare may redirect your content or take other actions"* (`fundamentals/reference/policies-compliances/delivering-videos-with-cloudflare.mdx:20-30`).
  - The restriction "does not apply to private network routes" (`:23`).
  - For lockstep this is a non-issue: small binary WS frames, plus occasional ROM and core downloads to a handful of users.
  - For streaming video through a Tunnel public hostname it is a real ToS risk. That includes WebCodecs chunks over WebSocket (ASSUMED interpretation).
  - REPORTED (search summary of cloudflare.com service-specific terms): the named paid services are "the Developer Platform, Images, and Stream". The older blanket "non-HTML content" clause was removed from the self-serve terms.
- **Protocols** (`partials/cloudflare-one/tunnel/protocols-table.mdx:9-21`): public hostnames proxy HTTP/HTTPS/UNIX. TCP, SSH, RDP and SMB need `cloudflared` on the client. **There is no public UDP, so WebRTC media cannot come in through a Tunnel public hostname.**
- **Quick Tunnels** (`trycloudflare.com`) have "no uptime guarantee", are limited to 200 in-flight requests, have a random URL each run, and are for testing only (`tunnel/get-started/quick-tunnels/index.mdx:13-16, 91-99`). A named tunnel needs a domain on Cloudflare. A .com is about **$10.46/year** at Cloudflare Registrar (REPORTED, domainoffer.net, 2026-08).
- Latency (ASSUMED): client → nearest Cloudflare PoP → PoP holding the tunnel → home. This usually adds a few ms to tens of ms over a direct connection. **Measure RTT from each player through the tunnel.**

### 3.3 Exposing it: Tailscale Funnel
- **REPORTED** (tailscale.com/kb/1223/funnel via search):
  - Funnel listens only on **443, 8443, 10000**.
  - It is TLS-only on a `*.ts.net` name, so no domain purchase is needed.
  - Traffic goes through Tailscale's Funnel relay servers, which do not decrypt.
  - It is *"subject to non-configurable bandwidth limits"*, with no figure published.
  - It needs MagicDNS and HTTPS enabled in the tailnet.
- **VERIFIED** (tailscale/tailscale @ 76648a0, `ipn/serve.go:666-720`): the client does not hard-code the ports. It reads them from the control-plane node attribute `funnel-ports?ports=...`, so Tailscale can change them server-side.
- **REPORTED** (SSD Nodes, 2026-08): the free Personal plan was revised on 2026-04-08 to 6 users with unlimited user devices. Whether Funnel is included on the free plan was not confirmed.
- Telegram webhooks accept ports **443, 80, 88, 8443**. That is VERIFIED from the mirrored Bot API text, the research workspace (not committed; sources are cited inline)txt/core.bots.api.txt:550` (Bot API 10.3, 2026-08-24). Funnel's 443/8443 would work for a webhook. Long polling (`getUpdates`) needs no inbound connection at all.
- Verdict: fine for a dev or demo URL. The undisclosed bandwidth limit and relay path make Cloudflare Tunnel the better pilot front door (ASSUMED).

### 3.4 Home upload bandwidth
- Lockstep needs about **0.35 Mbps up per 8-user room**. ROM and core first-downloads are bursts: a 25 MB ROM to 8 players is 200 MB, about 80 s at 20 Mbps up.
  - Mitigations: client-side caching in Cache API or IndexedDB; staggered downloads; or serving ROM blobs from the cloud side in the hybrid (§4).
- Streaming needs **16-32 Mbps up per room** (8 viewers × 2-4 Mbps), so 48-96 Mbps at a 3-room peak.
- REPORTED (secondary write-ups of Ookla data): US median upload is about 56-57 Mbps (Dec 2025 / early 2026). Cable is often 10-45 Mbps (Xfinity typically 20-45, Spectrum low-to-mid 20s). Fiber medians are 250-310 Mbps. **Streaming from a cable-connected home cannot handle the peak.**

### 3.5 Reliability checklist (ASSUMED)
- Home power and ISP outages; a UPS costs about $60-150 one-off.
- macOS updates and reboots.
- Disable sleep, and enable "Start up automatically after a power failure".
- Router NAT is not an issue with a Tunnel.
- Residential ISP terms may forbid "servers"; check yours. Dynamic IP is not an issue with a Tunnel.
- Single disk: backups (§6) are mandatory.
- Docker on macOS:
  - **Docker Desktop is free for "small businesses (fewer than 250 employees AND less than $10 million in annual revenue), personal use, education, and non-commercial open source"**. VERIFIED: `docker/docs` `content/includes/desktop-license-update.md:3`.
  - It runs containers in a Linux VM. Colima and OrbStack are alternatives (REPORTED).
  - Node can also run natively under launchd with no Docker.
- Architecture: an arm64 server replica must hash-match x86 and phone clients. WASM integer code is deterministic, but verify FP/NaN paths with state hashes. See the emulator-cores report.

---

## 4. Hybrid: cloud edge + Mac mini compute

| Variant | What runs where | Monthly cost | Notes |
|---|---|---|---|
| **H0. Mac mini + Cloudflare Tunnel** (not really hybrid) | Everything on the Mac; Tunnel for HTTPS/WSS; static client could go on Cloudflare Pages | $2-5 electricity + $0.87 domain + < $1 backup → **about $3-7** | Simplest. Cloudflare terminates TLS, so Caddy is optional. |
| **H1. VPS front + Mac compute over Tailscale/WireGuard** | VPS: Caddy (auto-HTTPS), bot, static assets, ROM/core cache, WS fan-out. Mac: authoritative emulators, reached over a Tailscale (free) or WireGuard link. | Hetzner CX23 about €6 (about $7) or Oracle Free $0 + Mac $2-5 → **about $3-13** | VPS↔home RTT adds directly to input latency unless the VPS only fans out (ASSUMED). The VPS can also run rooms itself if the Mac is down, using the same Compose stack. |
| **H2. Mac encodes video; cloud SFU fans out** (streaming only) | Mac uploads 1 stream per room (2-4 Mbps); SFU serves N viewers | Self-host SFU (LiveKit/mediasoup) on Hetzner 20 TB: about €6-9. Cloudflare Realtime SFU: **$0.05/GB after 1,000 GB/month free**, shared with TURN (VERIFIED, `realtime/sfu/platform/pricing.mdx:11-13`). At 0.9-1.7 TB that is about $0-36; at 5.2 TB about $210. | Solves the home-upload problem. Mac→SFU still needs outbound UDP (fine behind NAT). |

---

## 5. If architecture (a), video streaming, were used

### 5.1 TURN
| Option | Price | Status |
|---|---|---|
| **Cloudflare Realtime TURN** | **$0.05/GB** of data sent from Cloudflare to the TURN client. **First 1,000 GB/month free** (shared with SFU). STUN is free and unlimited. Free when used with the Realtime SFU. | VERIFIED (`realtime/turn/faq.mdx:13-21`, `realtime/turn/index.mdx:13`) |
| **Twilio Network Traversal** | **$0.40/GB** in US and EU; $0.60 Singapore, Mumbai, Tokyo; $0.80 Sydney and São Paulo; STUN free | REPORTED (twilio.com/en-us/stun-turn/pricing via search) |
| **coturn, self-hosted** | Software $0. You pay VM egress (Hetzner EU: inside 20 TB; DO/Vultr $0.01/GB over; Linode $0.005/GB; Fly $0.02/GB + $2 IPv4) | ASSUMED (cost model) |

- How much traffic needs TURN (REPORTED, vendor estimates): about 10-25% of sessions. The only primary measurement is from 2017 (17.7%).
- With a **public-IP media server**, clients need TURN only when UDP is blocked; then they use TURN over TCP/TLS 443. Expect the low end (ASSUMED). A home Mac behind NAT without port-forwarding pushes all traffic through TURN or an SFU.
- Example: typical streaming month of 1.73 TB, 20% relayed → 346 GB:
  - Twilio: **about $138**.
  - Cloudflare: **$0** if within the free 1 TB (shared with SFU), otherwise about $17.
  - coturn on Hetzner EU: **€0 extra**.

### 5.2 Egress for streaming

At 0.86-1.73 TB/month typical and 2.6-5.2 TB heavy:

| Host | Typical month | Heavy month |
|---|---|---|
| Hetzner EU (20 TB incl.) | €0 extra | €0 extra |
| DO $24 (4 TB pool) | $0 | about $0-12 (up to 1.2 TB over at $0.01/GiB) |
| Vultr $20 (3 TB) | $0 | about $0-22 |
| Linode $24 (4 TB) | $0 | about $0-6 |
| Fly ($0.02/GB NA/EU) | $17-35 | $52-104 |
| Oracle Free (10 TB) | $0 | $0 |
| Cloudflare SFU ($0.05/GB after 1 TB) | $0-36 | $80-210 |

### 5.3 Streaming compute
- Software encode of 1-3 low-resolution 60 fps streams plus emulation wants **dedicated or unthrottled CPU** (ASSUMED). Options:
  - Hetzner CX33 (4 shared vCPU, €8.49, if orderable)
  - Hetzner CCX13 (€42.99)
  - DO/Vultr/Linode 2-4 vCPU ($20-48)
  - Fly performance-2x ($66 iad / $76 fra)
- The Mac mini has a hardware H.264/HEVC encoder (VideoToolbox), so encode is nearly free there (ASSUMED). The constraint is upload (§3.4).

---

## 6. Deployment: Docker Compose, Caddy, backups

### 6.1 Compose layout (ASSUMED design; the same file works on a VPS or the Mac)
```yaml
services:
  arcade:            # Node 22 server: bot (long polling or webhook), WS lockstep, emulator workers
    image: arcade:latest
    restart: unless-stopped
    volumes: [ "roms:/data/roms", "db:/data/db" ]
    expose: [ "8080" ]
  caddy:             # VPS only: auto-HTTPS + WS reverse proxy
    image: caddy:2
    ports: [ "80:80", "443:443", "443:443/udp" ]
    volumes: [ "./Caddyfile:/etc/caddy/Caddyfile", "caddy_data:/data" ]
  cloudflared:       # Mac/home only: replaces caddy's public role
    image: cloudflare/cloudflared:latest
    command: tunnel run --token ${TUNNEL_TOKEN}
  # optional: telegram-bot-api (local Bot API server for >20 MB ROM downloads, see telegram-bot-files-groups.md)
  # optional (streaming only): coturn with network_mode: host for the UDP relay range
  backup:            # restic to R2/B2 nightly
    image: restic/restic
volumes: { roms: {}, db: {}, caddy_data: {} }
```
Pin image versions in the real file.

Caddyfile (VPS):
```
arcade.example.com {
  reverse_proxy arcade:8080 {
    stream_close_delay 5m
  }
}
```

### 6.2 Caddy facts (VERIFIED, `caddyserver/website` @ 19575198, 2026-10-03)
- Public names get certificates automatically from Let's Encrypt or ZeroSSL, are renewed, and HTTP→HTTPS redirect is on by default (`src/docs/markdown/automatic-https.md:43-46, 260`).
- The HTTP-01 challenge needs port 80 reachable and TLS-ALPN needs 443 (`:180, :189`). Behind Cloudflare Tunnel, use DNS-01 or let Cloudflare terminate TLS.
- `.ts.net` names get their certificates from the local Tailscale daemon (`:93`).
- `reverse_proxy` handles WebSocket upgrades natively. **On config reload WebSockets are forcibly closed by default.** `stream_close_delay` (e.g. `5m`) avoids a reconnect storm. `stream_timeout` caps connection age (`caddyfile/directives/reverse_proxy.md:424-446`).

### 6.3 Backup targets
| Target | Price | Pilot cost (50 GB) | Status |
|---|---|---|---|
| **Cloudflare R2** Standard | **$0.015/GB-month**; Class A $4.50/M, Class B $0.36/M; **egress free**; free tier 10 GB-month + 1M A + 10M B ops | (50-10) × 0.015 = **$0.60** | VERIFIED (`r2/pricing.mdx:28-57`) |
| **Backblaze B2** | **$6.95/TB-month**; first 10 GB free; egress free up to 3× average stored, then $0.01/GB | about **$0.28** | REPORTED (backblaze.com/cloud-storage/pricing via search) |
| **Hetzner Storage Box BX11** | 1 TB for **€3.20** (pre-April-2026 listing; current price unconfirmed; may be higher); SFTP/borg/restic; FSN/HEL | €3.20+ | REPORTED (whtop.com, dated 2026-02-13) |
| Hetzner Object Storage | **€6.49/month base**, including 1 TB storage + 1 TB egress (was €4.99) | €6.49 | REPORTED |
| Oracle Object Storage (Always Free) | 20 GB free | $0 if < 20 GB | VERIFIED (Oracle JSON) |
| Fly volume snapshots | $0.08/GB after 10 GB free (VM-level only, not off-site) | about $3.20 | VERIFIED |

Recommendation (ASSUMED): use restic or Kopia to R2 or B2, nightly, with a 30-day retention. ROM blobs are immutable, so they deduplicate well and the monthly delta is tiny.

---

## 7. Monthly cost estimates for the pilot

Assumptions (ASSUMED): 1-3 groups; ≤ 8 users per room; typical 120 room-hours/month; peak 3 concurrent rooms; ROMs 50 GB; backups 50 GB; domain $10.46/year (= $0.87/month) where a custom domain is needed; €1 = $1.17; prices exclude VAT.

### 7.1 Lockstep (architecture b), the planned design

| Scenario | Line items | **Monthly** |
|---|---|---|
| **A. Oracle Always Free** (A1 2 OCPU / 12 GB, 200 GB block) | $0 VM + $0 egress (< 10 TB) + domain $0.87 + R2 $0.60 | **≈ $1.50** (risks: capacity, reclamation, cut quotas) |
| **B. Hetzner CX33** (4 vCPU / 8 GB / 80 GB, EU), if orderable | €8.49 + €0.50 IPv4 = €8.99 ($10.52) + domain $0.87 + R2 $0.60 | **≈ $12** |
| B'. Hetzner CX23 + 50 GB volume | €5.49 + €0.50 + €2.86 = €8.85 ($10.35) + $1.47 | **≈ $12** |
| B''. Hetzner CPX22 (when CX/CAX sold out) + volume | €19.49-19.99 + €0.50 + €2.86 ≈ €23 ($27) + $1.47 | **≈ $28** |
| **C. Vultr Regular 2 vCPU / 4 GB / 80 GB** | $20 + $1.47 | **≈ $21.50** (REPORTED price may be stale) |
| **D. DigitalOcean Basic 2 vCPU / 4 GiB / 80 GiB** | $24 + $1.47 | **≈ $25.50** |
| D'. DO 1 vCPU / 2 GiB / 50 GiB (ROMs ≤ about 35 GB) | $12 + $1.47 | ≈ $13.50 (one vCPU: tight at a 3-room peak) |
| **E. Linode 4 GB (2 vCPU / 80 GB)** | $24 + $1.47 | **≈ $25.50** |
| **F. Fly.io performance-1x 2 GB + 50 GB volume** | iad $33.00 / fra $38.08 + $7.50 + egress 50 GB × $0.02 = $1.00 + R2 $0.60 (Fly certificates free; custom domain $0.87) | **≈ $43 (iad) / $48 (fra)** |
| **G. Mac mini at home (already owned) + Cloudflare Tunnel** | electricity $1.64-3.28 (15 W average) + Tunnel $0 + domain $0.87 + R2 $0.60 | **≈ $3-5** (worst case 65 W 24/7 at $0.30: ≈ $16) |
| G'. Mac mini + Tailscale Funnel (no domain) | electricity + R2 | **≈ $2-4** |
| **H. Hybrid: Hetzner CX23 front + Mac compute** | €5.99 ($7.01) + Mac $1.64-3.28 + $1.47 | **≈ $10-12** |
| H'. Hybrid: Oracle Free front + Mac compute | $0 + $1.64-3.28 + $1.47 | **≈ $3-5** |

### 7.2 Streaming (architecture a), for comparison

Egress is 0.86-1.73 TB/month typical. TURN covers 20% of traffic where relevant.

| Scenario | Line items | **Monthly** |
|---|---|---|
| Hetzner CX33 + coturn on same box | €8.99 ($10.52); egress inside 20 TB | **≈ $12** (if CX33 is orderable and shared CPU keeps up with software encode) |
| Hetzner CCX13 (dedicated) + coturn | €42.99 + €0.50 = €43.49 ($50.88) | **≈ $52** |
| DO 2 vCPU / 4 GiB + coturn | $24 (4 TB pool covers typical) + backup | **≈ $25-37** (heavy month overage up to ~$12) |
| Fly performance-2x + dedicated IPv4 + volume + egress | $66 + $2 + $7.50 + $17-35 | **≈ $93-110 (iad)**; ≈ $103-120 (fra) |
| Any host + **Twilio TURN** for 20% of traffic | + 173-346 GB × $0.40 | **+ $69-138** |
| Any host + **Cloudflare TURN** | + $0 within 1 TB free; else $0.05/GB | **+ $0-17** |
| Mac mini direct (needs UDP port-forward or TURN; 16-32 Mbps up per room) | electricity $3-7 + TURN | ≈ $4-25; **does not work for peak 3 rooms on cable upload**; Tunnel cannot carry the media |
| Mac mini + Cloudflare Realtime SFU (H2) | electricity $3-7 + SFU $0-36 | **≈ $4-45** |

**Takeaway:** the planned lockstep design costs about $0-12/month on cheap hosts and about $25 on mainstream ones. A streaming design costs 2-10× more and adds UDP, TURN and ToS work.

---

## 8. Things to test before committing

1. Per-frame CPU time of the FBNeo WASM core in Node, measured as ms per frame for CPS-2 and Neo Geo with audio on. Run it on:
   - Hetzner CX23 / CAX11 (shared x86 / Ampere)
   - DO Basic and Vultr Regular (older Intel)
   - Fly shared-cpu-8x vs performance-1x
   - Oracle A1
   - Mac mini M2/M4

   This decides how many rooms each host can run and whether Fly `shared` throttling breaks pacing.
2. State-hash equality between an arm64 server replica (Mac mini, CAX, A1) and x86 and phone clients over long sessions.
3. RTT and jitter from real players (iPhone, Android, Desktop Telegram) to:
   - (a) a Cloudflare Tunnel hostname backed by the home Mac
   - (b) Tailscale Funnel
   - (c) a Hetzner FSN/HEL VM
   - (d) a US VM, if players are in the US

   WebSocket frame pacing at 60 Hz through Cloudflare. Check for Nagle effects and batching at the edge.
4. Behaviour when a WebSocket drops mid-match: `cloudflared` restart, Caddy reload, Cloudflare edge restart. Reconnect plus savestate resync time.
5. ROM upload through the Tunnel: chunking below 100 MB, resume, and throughput from phones.
6. Home upload under a ROM-download burst while a match is running (QoS effect on frame latency).
7. Oracle: whether an A1 instance can be provisioned in the chosen home region, and whether a low-duty pilot gets flagged idle.
8. Hetzner: whether a CX23/CX33/CAX can actually be ordered in FSN/NBG/HEL. Otherwise accept the CPX22 price.
9. For streaming only: TURN relay share on mobile carriers (CGNAT), and Cloudflare TURN billing when the server is the TURN client.

## 9. Open questions
- Where are the players? EU vs US decides between Hetzner EU, Hetzner US (pricier, smaller transfer), DO/Vultr/Linode, and the home Mac.
- Is a Mac mini already owned, and is the home on fiber or cable? Can it sit on a UPS?
- Current exact Hetzner prices (June list €5.49 vs API snapshot €5.99 for CX23), Storage Box after April 2026, and whether CX/CAX restock.
- Is Tailscale Funnel included on the free Personal plan after the 2026-04-08 plan change? What is its bandwidth cap?
- Does Cloudflare treat repeated ROM and core blob downloads through a Tunnel as "disproportionate large files"? At pilot scale, probably not (ASSUMED). R2 with a custom domain is the sanctioned path for blobs.
- Cloudflare TURN billing when the media server, not the viewer, is the TURN client. The FAQ diagram marks the TURN-server↔peer leg as "Not part of billing" (`realtime/turn/faq.mdx:30-40`). Confirm before relying on it.

---

## 10. Sources

Primary, read locally (VERIFIED):
- Fly.io docs, GitHub `superfly/docs` @ f3525efe (2026-10-08): `about/pricing.mdx`, `snippets/RegionPricingSelector.jsx`, `machines/cpu-performance.mdx`, `networking/udp-and-tcp.mdx`, `reference/configuration.mdx`, `app-guides/6pndemochat.mdx`, `deep-dive/index.mdx` → https://fly.io/docs/about/pricing/
- Cloudflare docs, GitHub `cloudflare/cloudflare-docs` @ baa31ec5 (2026-10-08): `r2/pricing.mdx`, `realtime/turn/{faq,index}.mdx`, `realtime/sfu/platform/pricing.mdx`, `network/websockets.mdx`, `cache/concepts/default-cache-behavior.mdx`, `fundamentals/reference/connection-limits.mdx`, `fundamentals/reference/policies-compliances/delivering-videos-with-cloudflare.mdx`, `tunnel/{index,concepts/routing,get-started/quick-tunnels/index,features/locally-managed-tunnels/configuration-file}.mdx`, `cloudflare-one/account-limits.mdx`, `partials/cloudflare-one/tunnel/protocols-table.mdx` → https://developers.cloudflare.com/
- Oracle free tier: https://www.oracle.com/cloud/free/ and its data file https://www.oracle.com/a/ocom/docs/oci-free-tier_v1.json (fetched 2026-10-08)
- Caddy docs, GitHub `caddyserver/website` @ 19575198 (2026-10-03): `src/docs/markdown/automatic-https.md`, `caddyfile/directives/reverse_proxy.md`
- Tailscale client, GitHub `tailscale/tailscale` @ 76648a00 (2026-10-08): `ipn/serve.go:666-720`
- Docker docs, GitHub `docker/docs` main: `content/includes/desktop-license-update.md:3`
- Telegram Bot API 10.3 (2026-08-24) mirror: the research workspace (not committed; sources are cited inline)txt/core.bots.api.txt:550`; spec JSON https://raw.githubusercontent.com/PaulSonOfLars/telegram-bot-api-spec/main/api.json
- Project protocol: `telegram-arcade/shared/protocol.ts:1-62`

Secondary (REPORTED; via web search 2026-10-08):
- Hetzner: https://docs.hetzner.com/general/infrastructure-and-availability/price-adjustment/ ; https://www.hetzner.com/pressroom/statement-price-adjustment/ ; https://docs.hetzner.com/general/others/ipv4-pricing/ ; https://docs.hetzner.com/cloud/general/locations/ ; https://northflank.com/blog/hetzner-cloud-server-price-increases ; https://privatedevops.com/news/hetzner-june-2026-cloud-price-increase-what-to-do ; https://byteiota.com/hetzner-june-2026-price-shock/ ; https://bex.co/blog/2026/09/24/hetzner-cheap-tier-unavailable-fleet-planning ; https://stackvaluelab.com/hetzner-cx-cax-unavailable/ ; https://hetzner.thegoated.dev/ ; https://costgoat.com/pricing/hetzner ; https://www.whtop.com/plans/hetzner.com/128269
- DigitalOcean: https://www.digitalocean.com/pricing/droplets ; https://docs.digitalocean.com/platform/billing/bandwidth/
- Vultr: https://www.vultr.com/pricing/ ; https://www.vultr.com/products/regular-performance-compute/
- Akamai/Linode: https://www.akamai.com/cloud/pricing/north-america ; https://www.akamai.com/blog/cloud/akamai-cloud-computing-price-update
- Oracle: https://docs.oracle.com/en-us/iaas/Content/FreeTier/resourceref.htm ; https://infoq.com/news/2026/07/oracle-cloud-free-tier-limits/
- Apple: https://support.apple.com/103253 ; https://servethehome.com/the-apple-mac-mini-m4-sets-the-mini-computer-standard/3 ; https://www.macprices.net/macmini.shtml
- Electricity: https://www.eia.gov/outlooks/steo/report/elec_coal_renew.php ; https://www.electricchoice.com/electricity-rates-by-state/
- Tailscale: https://tailscale.com/kb/1223/funnel ; https://www.ssdnodes.com/learn/is-tailscale-free-plan-limits
- Twilio: https://www.twilio.com/en-us/stun-turn/pricing
- TURN share estimates: https://openvidu.io/blog/2026/06/09/turn-key-considerations/ ; https://www.forasoft.com/learn/video-streaming/articles-streaming/turn-bandwidth-calculator
- Backblaze: https://www.backblaze.com/cloud-storage/pricing
- Upload speeds: https://www.fierce-network.com/broadband/ookla-municipal-broadband-upload-speeds-beat-cable-still-trail-fiber-isps ; https://blog.ting.com/what-speeds-you-actually-get
- Cloudflare terms: https://www.cloudflare.com/service-specific-terms-application-services/ ; Registrar price: https://domainoffer.net/tld/com/cloudflare
- Benchmarks: https://betterstack.com/community/guides/web-servers/digitalocean-vs-vultr/

---

## Verification

Adversarial fact-check, 2026-10-09. I re-opened the primary sources myself and did not reuse the researcher's local copies:
- Fly docs: `superfly/docs` main @ a2b424b0. The pricing and CPU files are byte-identical to the researcher's f3525ef copies.
- Cloudflare docs: `cloudflare/cloudflare-docs` production @ baa31ec5, re-downloaded from raw.githubusercontent.com.
- Oracle: oracle.com free-tier JSON, `/cloud/free/` and `/cloud/free/faq/`, all re-fetched.

Hetzner, DigitalOcean, Vultr and Linode sites and APIs are blocked from this container (proxy 403). For those I used third-party GitHub repos that hold copies of the vendors' API JSON.

My copies are under the research workspace (not committed; sources are cited inline)verify/`.

| # | Claim (short) | Verdict | Evidence / correction |
|---|---|---|---|
| 1 | Fly `shared` vCPU 5 ms / 80 ms quota, so the emulator needs `performance` (or maybe shared-cpu-8x, $17.55) | **DOWNGRADE** | **Mechanics VERIFIED** (`machines/cpu-performance.mdx:9-24`): 5 ms (6.25%) per 80 ms per shared vCPU; initial burst 5 s, maximum 500 s; tasks throttled for the rest of the 80 ms period; quota shared across a machine's vCPUs (`:16`). shared-cpu-8x 2 GB = $17.55 (iad), recomputed from `RegionPricingSelector.jsx:25-35`. **The "therefore" is ASSUMED and overstated.** Baseline scales per vCPU: shared-cpu-2x = 12.5% (2 GB $13.39), shared-cpu-4x = 25% (1 GB **$8.78**, 2 GB $14.78). Either could carry one room if the benchmark shows ≤ about 10-20% of a core. A full 500 s burst balance lasts about 35-60 min at a 20-30% load, not "minutes". A fresh machine's 5 s balance lasts only about 20-36 s. |
| 2 | Oracle A1 "1,500 OCPU hours and 9,000 GB hours" (2/12, down from 4/24); 10 TB egress; 200 GB block; 20 GB object; 30-day idle; capacity limits | **DOWNGRADE** | **VERIFIED** in the re-fetched JSON (identical to the researcher's copy): the A1 line, "Up to 10 TB per month", "Up to 2 block volumes, 200 GB total" (boot volumes count; the label is "Boot and block volume storage"), and "Up to 20GB total for standard, infrequent and archive". The FAQ idle sentence is VERIFIED. **Corrections:** (a) 2 OCPU / 12 GB is *derived* (1,500 / 730 h). "Down from 4/24" is only REPORTED (linuxiac, terminalbytes and others; prior figure 3,000 / 18,000); the current Oracle files do not show it. (b) The "**Availability to Free Tier is subject to capacity limits**" footnote describes capacity estimates for *Free Trial credits*. It is not a statement about A1 host capacity. "Out of host capacity" remains REPORTED/ASSUMED. |
| 3 | Hetzner June-2026 prices CX23 €5.49, CX33 €8.49, CAX11 €5.99, CAX21 €10.49, CPX22 €19.49, CCX13 €42.99; IPv4 €0.50; 20 TB EU, about €1/TB; April increase | **CONFIRMED** (strong REPORTED; not VERIFIED) | A copy of Hetzner Cloud API `/server_types` + `/pricing` JSON in `pierre-lebret/cloud-desktop-manager` `rdpm/hetzner/static_seed.json` (commit 975e5dc, 2026-09-29) gives exactly: cx23 5.49, cx33 8.49, cax11 5.99, cax21 10.49, cpx22 19.49, ccx13 42.99 (fsn1/nbg1/hel1, net). It also shows IPv4 0.50/mo, volume 0.0572/GB-month, EU `included_traffic` 20 TiB, and `price_per_tb_traffic` 1.00 (SIN **7.40**). A third-party live API read (`jikig-ai/soleur` plan dated 2026-08-06) also gives cx23 5.49 and cpx22 19.49 net. **Correction:** the "one API snapshot shows €5.99/8.99/6.49/19.99" is not a conflicting snapshot. Each figure is exactly the net price + €0.50 IPv4. So §7.1 B'' (€19.49-19.99 + €0.50) double-counts IPv4. Older snapshots (spawn fixture 2026-02-17: cx23 3.49, cx33 5.99, cax11 3.99, cpx22 6.99) are consistent with two 2026 increases. The size of the April increase ("up to 37%") stays REPORTED. |
| 4 | CX/CAX mostly "not available" since about 2026-09-02; EU-only; US/SIN only AMD lines with smaller traffic | **DOWNGRADE** | **The start date is too late.** A third-party live probe of `/v1/datacenters .server_types.available` (`jikig-ai/soleur` `variables.tf:110-117`, `expenses.md:18`) recorded the **entire CX line and entire CAX line orderable in 0 of 3 EU DCs on 2026-07-26**. CX23 was unorderable in hel1 on 2026-08-04 and orderable everywhere on 2026-08-06, while **CAX11 was still unavailable in all three**. A search summary says Hetzner's status notice dates from June. The EU-only / US-SIN part is corroborated by the 2026-09-29 API copy: ash/hil list only cpx11-51 and ccx13-63; sin lists cpx11-51, cpx12-62 and ccx. Included traffic there is 0.5-8 TB. **The missed point is that US prices are about 3x EU:** cpx11 €17.49, cpx21 €31.99, ccx13 €43.49. |
| 5 | Cloudflare WebSockets on all plans; edge releases and cloudflared stop drop WS | **CONFIRMED** | `network/websockets.mdx:12, 53, 69, 73`; `tunnel/features/locally-managed-tunnels/configuration-file.mdx:174`; same text in `tunnel/guides/update-cloudflared.mdx:167`. Also `:87`: Cloudflare closes idle WebSockets (no value given; Enterprise can change it). `:40`: Argo is not compatible with WebSockets. |
| 6 | Upload limit 100 / 100 / 200 MB / up to 5 GB; chunk or go unproxied | **CONFIRMED** | `cache/concepts/default-cache-behavior.mdx:86-99`. Also `:105`: maximum cacheable file is 512 MB on Free/Pro/Business. **Caveat (ASSUMED):** the "DNS-only (unproxied)" option does not exist for a Tunnel-only home origin, so chunking is the only path there. |
| 7 | Tunnel public hostnames are subject to the video/large-file terms; private routes exempt | **CONFIRMED** | `tunnel/concepts/routing.mdx:31-35`; `delivering-videos-with-cloudflare.mdx:12, 20-24, 30`. **Caveat (ASSUMED from the linked docs):** private-network routes need the Cloudflare One/WARP client on each viewer device, so the exemption is unusable for Telegram Mini App users. |
| 8 | Tunnel protocols HTTP/HTTPS/UNIX; TCP/SSH/RDP/SMB need client cloudflared; no public UDP; all plans; 1,000 tunnels / 25 replicas; Quick Tunnels limits | **CONFIRMED** | `partials/cloudflare-one/tunnel/protocols-table.mdx:7-21` (no UDP service type); `tunnel/index.mdx:26` `<Plan type="all" />`; `cloudflare-one/account-limits.mdx:67-71`; `tunnel/get-started/quick-tunnels/index.mdx:15-16, 93-97`. Quick Tunnels also "do not support Server-Sent Events" (`:95`). In the Tunnel docs, UDP appears only for cloudflared↔edge transport (QUIC 7844) and private-network flows. |
| 9 | Lockstep about 92 B × 60 Hz ≈ 44 kbps/client, 0.16 GB per 8-user room-hour, about 19 GB per 120 h; streaming 45-90x more | **CONFIRMED** (as an ASSUMED model; arithmetic and code checked) | `shared/protocol.ts:58-98`: header 11 B + 2 B per port per frame + 1 B ack count, so 2 ports and no acks = **16 B**. The server sends one Frames message per tick (`server/session/session.ts:220-250`), and `perMessageDeflate: false` (`server/ws/gateway.ts:65`). 16 + 2 (WS) + 22 (TLS 1.3) + 52 (TCP/IPv4 + timestamps) = 92 B. 92 × 60 × 3600 × 8 = 159 MB/h. × 120 = 19.1 GB. Ratio 7.2/0.16 = 45, 14.4/0.16 = 90. **Caveats:** IPv6 clients add 20 B (112 B, about 54 kbps); 4-port games add 4 B; each input ack adds 13 B; the server runs at 60 Hz (`frameMs = 1000/60`). The streaming bitrate is an assumption. |
| 10 | Pilot cost table (lockstep and streaming) | **CONFIRMED** (arithmetic reproduces from the inputs; remains ASSUMED) | Recomputed: Oracle $1.47; CX33 €8.99 → $10.52 + $1.47 = $11.99; CX23 + volume $11.82; Fly perf-1x $42.97 iad / $48.05 fra; Mac $3.11-4.75; hybrid $10.12-11.76; Twilio 173-346 GB × $0.40 = $69-138; Fly streaming $92.5-110.5 iad / $102.7-120.7 fra. Third-party API fixtures corroborate DO s-2vcpu-4gb $24 / 4 TB (spawn, ≤ 2026-03) and Vultr vc2-2c-4gb $20 / 3 TB (burn402, undated). Twilio $0.40/GB US/EU is REPORTED (search of twilio.com). **Corrections:** CPX22 fallback ≈ **$25** (€19.49 + €0.50; its 80 GB disk makes the volume optional), or $28 with a volume, not $28 with IPv4 double-counted. Fly lockstep could be about **$19-25** (shared-cpu-4x 1-2 GB $8.78-14.78 + $7.50 volume + $1 egress + $1.47) if shared-cpu-4x passes the benchmark (row 1). The CX "≈ $12" rows need stock that has been intermittently or entirely absent since late July (row 4). |

Verification sources (accessed 2026-10-09):
- https://raw.githubusercontent.com/superfly/docs/main/machines/cpu-performance.mdx
- https://raw.githubusercontent.com/superfly/docs/main/about/pricing.mdx
- https://raw.githubusercontent.com/superfly/docs/main/snippets/RegionPricingSelector.jsx
- https://www.oracle.com/a/ocom/docs/oci-free-tier_v1.json ; https://www.oracle.com/cloud/free/faq/
- https://raw.githubusercontent.com/cloudflare/cloudflare-docs/production/src/content/… (files listed above)
- https://raw.githubusercontent.com/pierre-lebret/cloud-desktop-manager/main/rdpm/hetzner/static_seed.json (Hetzner API copy, 2026-09-29)
- https://github.com/jikig-ai/soleur: `knowledge-base/project/plans/archive/20260806-150344-…-repin-registry-host-cx23-to-cpx22-plan.md`, `apps/web-platform/infra/variables.tf`, `knowledge-base/operations/expenses.md` (third-party live Hetzner API probes)
- https://raw.githubusercontent.com/OpenRouterLabs/spawn/main/fixtures/{hetzner/server-types.json,digitalocean/sizes.json}
- https://raw.githubusercontent.com/aykk/burn402/main/fixtures/vultr/plans.json
- REPORTED via search: https://linuxiac.com/oracle-quietly-cuts-free-tier-ampere-a1-resources-in-half/ ; https://stackvaluelab.com/hetzner-cx-cax-unavailable/ ; https://www.vincentschmalbach.com/hetzner-cheap-cloud-unavailable-price-increases/ ; https://heise.de/-11185981 ; https://www.twilio.com/en-us/stun-turn/pricing

Additional findings from verification:
- **Oracle FAQ (VERIFIED, `/cloud/free/faq/`):**
  - "if you have more Ampere A1 Compute instances provisioned than are available for an Always Free tenancy, all existing Ampere A1 instances are disabled and then deleted after 30 days unless you upgrade to a paid account."
  - With the 2/12 cut, an old 4/24 A1 VM on a pure free account is at risk.
  - The same FAQ says "Resources identified as Always Free will not be reclaimed" (in the trial-expiry context). This sits in tension with the REPORTED idle-reclamation policy on docs.oracle.com, which I could not reach.
- **Fly docs inconsistency (VERIFIED):**
  - `about/pricing.mdx:99` says "You don't need a credit card to start the trial".
  - `:115` says all organizations "require a credit card on file".
  - The report's "Credit card required" is therefore true only after the trial.
- **Hetzner US/SIN (third-party API copy, 2026-09-29):**
  - US: CPX is about 3x the EU price. cpx11 (2 vCPU / 2 GB) costs €17.49 with 1 TB traffic. There is no CX, CAX or CPX22 in the US.
  - SIN: overage is €7.40/TB.
  - For US-based players, Hetzner is no longer the cheap option. Compare DO, Vultr and Linode instead.
- **Hetzner stock history:** shortages are older and longer than "since early September". For CX and CAX, plan on stock being absent, not on a brief gap.
