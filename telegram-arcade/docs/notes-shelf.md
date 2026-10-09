# Shelf, uploads and validation

Engineering notes for the private per-group game shelf (`server/shelf/`),
the implementation of `ShelfService` (`server/shelf/types.ts`). Spec
references: SPEC.md sections 5, 6, 18 and 19.

## What is built

| File | Purpose |
| --- | --- |
| `server/shelf/zip.ts` | Defensive ZIP reader written from scratch (no dependency). |
| `server/shelf/nes.ts` | iNES / NES 2.0 header validation. |
| `server/shelf/catalog.ts` | FBNeo romset catalog and identification by CRC-32 + size. |
| `server/shelf/blobs.ts` | Private content-addressed blob store. |
| `server/shelf/validator.ts` | Host: runs validation jobs in child processes (queue, timeout, kill). |
| `server/shelf/validate-worker.ts` | Child process: inspect (safety + identification) and boot test. |
| `server/shelf/shelf.ts` | `Shelf` / `createShelf()`: the `ShelfService` implementation. |
| `server/shelf/upload-http.ts` | `handleWebUpload()`: streaming web upload to a temp file. |
| `scripts/build-catalog.ts` | Writes `native/build/catalog-<core>.json` from the compiled cores. |

Tests: `test/unit/zip-safety.test.ts`, `test/unit/shelf-ingest.test.ts`,
`test/unit/shelf-upload-http.test.ts` (helper: `test/helpers/zipgen.ts`, a
zip writer that can also forge hostile archives and force CRC-32 values).

## Upload pipeline

1. **Transport.** Telegram uploads are downloaded by the bot; the web fallback
   (`POST`, `application/octet-stream`, URL-encoded `X-File-Name`) is streamed
   by `handleWebUpload()` into a 0600 temp file with a hard byte limit. Past
   the limit the body is no longer read, the partial file is deleted and an
   `UploadError(413)` is thrown; the router must answer with
   `Connection: close`.
2. **`shelf.ingest()`** records an `uploads` row (`validating`), then runs an
   **inspect** job in a child process:
   * file type by magic bytes, never by extension: zip, iNES; 7z, RAR, gzip,
     FDS and UNIF get a specific "not supported" message;
   * zip safety checks (below), then either the NES path (a zip holding
     exactly one `.nes`) or arcade romset identification.
3. **Dedup in this group**: the SHA-256 of the content to store (the zip as
   uploaded, or the bare `.nes`) is looked up in this group only. A live entry
   with the same content returns `duplicate: true` and no second entry.
4. **Quotas** (below), then the content is stored once in the blob store.
5. The entry is built: dependencies are resolved against THIS group's shelf,
   and a **boot test** runs in a child process when everything is present.
6. A new parent/BIOS entry re-evaluates (boot-tests again) this group's
   entries that were waiting for it (`needs_dependency` -> `ready`).

Every step is asynchronous and every risky step runs in a separate process,
so an upload never interrupts the room's running game (which has its own
emulation worker process).

### What becomes what

| Outcome | Result |
| --- | --- |
| Unsafe/malformed archive (bomb, encryption, overlap, CRC, ...), unsupported container (7z, RAR, gzip), invalid NES header, several NES ROMs in one zip, quota or storage full | upload `failed` with a message and `findings`; **nothing is stored** |
| Valid zip that is not a supported, complete, unambiguous romset (unknown/modified set, other system, nested zips only, incomplete, ambiguous) | entry with status `needs_attention` and a clear message |
| Identified set whose parent / BIOS is not on this group's shelf | `needs_dependency`, `validation.missing` lists the set and the exact files |
| Identified, all files present, boot test fails | `needs_attention` (`compat: needs_attention`), boot detail shown |
| Identified, boot test passes | `ready` (`compat: untested` until a session reports `working`) |
| BIOS/board set (e.g. `neogeo`), complete | `kind: 'bios'`, `ready`, not launchable |
| Parent set FBNeo cannot run itself but whose clones it serves | `kind: 'parent'`, `ready`, not launchable |

A romset FBNeo flags as not working keeps `status` from the boot test but
gets `compat: 'needs_attention'` and a warning.

## Archive safety (`zip.ts`)

* Single-disk archives only; EOCD must end exactly at end of file (no trailing
  data); the central directory must end exactly at the (zip64) end record
  (rejects prepended data, gaps, overlaps). Zip64 EOCD, locator and extra
  fields are parsed with 64-bit bounds checks.
* Every central record's local header must exist before the central
  directory and agree on name bytes, method, encryption/descriptor flags and
  (without a data descriptor) CRC and sizes. Entry ranges must not overlap
  (defeats "non-recursive" overlapping-entry bombs).
* Methods: stored (0) and deflate (8) only. Encrypted entries (flag bits 0/6,
  AES method 99) are rejected.
* Limits (configurable): `MAX_ARCHIVE_ENTRIES` (checked before parsing
  entries), `MAX_EXTRACTED_BYTES` on the declared total, and
  `MAX_COMPRESSION_RATIO` per entry and for the whole archive. The ratio
  check applies to entries/archives above 1 MiB uncompressed: real romsets
  contain small blank ROMs and PLD dumps that compress far beyond any ratio.
* Decompression: `inflateRawSync` with `maxOutputLength` = declared size; the
  output must have exactly the declared size and CRC-32. So the actual total
  can never exceed the declared total, which is capped before anything is
  inflated. One entry is in memory at a time.
* Duplicate names (also case-insensitive) are rejected. Names with `..`,
  absolute paths, drive letters, backslashes, NUL, control characters or
  invalid UTF-8 are reported as warnings: names are never used as paths.
  Nothing is ever extracted to disk by name.
* Nested archives (by extension or magic bytes) are recorded as a warning and
  never opened.

## Romset identification (`catalog.ts`)

The catalog is the driver list of **our own compiled cores** (so it matches
exactly what the emulator can run): `node scripts/build-catalog.ts` writes
`native/build/catalog-fbneo_cps12.json` (826 sets) and
`catalog-fbneo_neogeo.json` (694 sets). Each set: name, fullname, parent,
board, samples, players, genre/driver flags, year, manufacturer, hardware and
ROMs `{n, s, c, t}`. Games that list their board's BIOS ROMs unchanged store
`boardBios: 1` instead of repeating them (687 Neo Geo sets). The Neo Geo
build has two drivers named `neogeo`; the board (`BDF_BOARDROM`) one is kept.
If a catalog file is missing, the validator builds it from the core in memory
(slower, same result).

Rules mirrored from the libretro FBNeo core (`open_archive`,
`locate_archive` in `src/burner/libretro/libretro.cpp`):

* it looks in up to three zips **in the game's directory**: `<set>.zip`, the
  board zip (`szBoardROM`, e.g. `neogeo.zip`) and the parent's zip;
* ROMs are matched by **CRC only** (names do not matter) and must have the
  exact size;
* `BRF_NODUMP`, zero CRC/size/type ROMs are never needed; `BRF_OPT` ROMs may
  be absent; everything else is required.

For Neo Geo games the set's list includes the board's BIOS ROMs; with our
fixed `fbneo-neogeo-mode = MVS_EUR` the required BIOS files are `sp-s3.sp1`,
`sm1.sm1`, `sfix.sfix`, `000-lo.lo` (all other BIOS images are `BRF_OPT`).
`test/unit/shelf-ingest.test.ts` checks this against the real core: without
them FBNeo refuses to start and names them; with exactly these four it runs.

Identification of a zip:

1. Candidate sets are those with at least one **distinctive** file in the zip
   (a ROM of the set that is in neither its parent's nor its board's list).
2. Each candidate's required ROMs are classified: in the zip, provided by the
   board zip, provided by the parent zip, or missing from the set itself.
3. Complete candidates (nothing missing from the set itself) win. Ties are
   broken by: the file name naming one of them; a merged parent zip (the
   parent and all its clones complete -> the parent, with a note); more
   required ROMs present, then fewer unrelated files. Anything still tied is
   reported as **ambiguous** (never guessed).
4. Without a complete candidate, the closest one is reported as
   **incomplete** with the missing / different-CRC / wrong-size files; with
   no candidate at all the zip is **unknown** (with a hint for other systems'
   file extensions and for zips of zips).

Room mode from the catalog: versus fighters (`GBF_VSFIGHT`) -> `versus`;
beat 'em ups (`GBF_SCRFIGHT`) -> `coop`; otherwise `single` for one player
and `coop` for more. Players come from the driver (capped by the system's
ports). Both are editable.

## NES

`.nes` files, or a zip with exactly one `.nes` (other files ignored), are
checked by `nes.ts`: magic, iNES vs NES 2.0, PRG/CHR sizes (NES 2.0
exponent notation), 512-byte trainer, file at least as long as declared
(trailing data is a warning), "DiskDude!" junk headers (mapper high nibble
ignored), VS/PlayChoice/region warnings, Four Score from NES 2.0 byte 15.
The stored content is the `.nes` itself (also when it came in a zip), so the
adapter registry can match it by SHA-256. Mode/players: from a verified
adapter (e.g. `atc-versus`), else 2 players co-op (4 for a Four Score
header), always editable. Session ports: adapter players, else 4 when more
than two players are set and the game supports the Four Score (header,
adapter, or an editor raising the player count, which is their statement
that it does), else 2.

## Boot test

`validate-worker.ts` loads the right wasm core, writes the files at the paths
a session uses (FBNeo: `/roms/<set>.zip` plus `/roms/<parent>.zip` and
`/roms/<board>.zip` when needed; NES: `/roms/game.nes`), applies
`SYSTEMS[system].options` and port devices, loads, re-applies options, and
runs 300-600 frames (rendering every 30th). It passes when a frame with at
least two different pixel values appeared. **Important:** FBNeo's libretro
`retro_load_game` returns success even when ROMs are missing (it then shows
its own "FBNeo Error" screen, which would pass a picture check); the boot
test therefore also requires an active driver (`core.driverInfo().name`).

## Processes and limits

Each validation job is a fresh `fork()` of `validate-worker.ts` with a
minimal environment (`PATH`, `NODE_ENV`; no tokens or secrets),
`--max-old-space-size=max(256, WORKER_HEAP_MB)`, bounded stdout/stderr
capture, and a hard timeout (`VALIDATION_TIMEOUT_MS`) after which it gets
`SIGKILL`. At most `MAX_CONCURRENT_VALIDATIONS` jobs run at once (FIFO queue).
Note that `--max-old-space-size` does not bound Buffers or wasm memory: the
real bounds are `MAX_UPLOAD_BYTES` (file read), `MAX_EXTRACTED_BYTES` (largest
single inflated entry), and the cores' own wasm memory caps.

| Setting | Default |
| --- | --- |
| `MAX_UPLOAD_BYTES` | 64 MiB |
| `MAX_EXTRACTED_BYTES` | 256 MiB |
| `MAX_ARCHIVE_ENTRIES` | 512 |
| `MAX_COMPRESSION_RATIO` | 200 (above 1 MiB) |
| `GROUP_QUOTA_BYTES` (per group, `groups.quota_bytes`) | 2 GiB |
| `GLOBAL_STORAGE_LIMIT_BYTES` | 40 GiB |
| `VALIDATION_TIMEOUT_MS` | 60 s |
| `MAX_CONCURRENT_VALIDATIONS` | 1 |
| `REMOVED_GAME_RETENTION_DAYS` | 7 |

Timings on the dev container: inspect ~0.2-0.3 s; NES boot ~0.35 s; CPS-1 and
Neo Geo boot 0.9-1.2 s (including process start and core load).

## Storage, privacy and dedup

* Blobs live in `DATA_DIR/blobs/<aa>/<sha256>` (dirs 0700, files 0600),
  written to `blobs/tmp/` and renamed into place (atomic), verified after
  copying. Paths are derived from the hash only.
* Files are only ever served through `blobForGroup(group, sha256)`: the blob
  must be referenced by one of that group's live entries or pinned by that
  group's running session. There are no public URLs and no catalog of what
  other groups have.
* Identical content uploaded by two groups is stored once, but the second
  upload goes through full validation and the same quota checks as a new
  file (the global limit counts the bytes as new even if they exist), gets
  its own entry, uploader and date: nothing reveals the other group.

## Quotas

* Per group: the sum of the sizes of the **distinct** blobs referenced by the
  group's non-removed entries (BIOS and parent sets included), against
  `groups.quota_bytes`. Checked before storing and again inside the insert
  transaction (concurrent uploads).
* Global: bytes of blob files present on disk plus the new file, against
  `GLOBAL_STORAGE_LIMIT_BYTES`.

## Removal, pinning, retention and deletion

* `remove()` is a soft removal (`status = 'removed'`, `removed_at/by`, audit
  row). Entries that depended on a removed parent/BIOS go back to
  `needs_dependency`. Re-uploading the same content later revives the same
  entry id (records keep pointing at it).
* `pin(group, hashes)` (the room manager pins `spec.clientFiles` when a
  session starts) keeps files servable and undeletable; `unpin(group)`
  releases them. A removed entry can still be restarted (`sessionSpec`) by the
  session that has it pinned, so removing the active game never breaks it.
  Pins are in memory: after a restart the room manager must pin again.
* `cleanup(now)` (run periodically by the app) deletes a blob file only when
  no live entry of any group references it, no group has it pinned, no ingest
  is using it, no **valid** checkpoint's game (or that game's parent/BIOS
  sets) needs it, and `REMOVED_GAME_RETENTION_DAYS` have passed since the
  later of its creation and its last removal. The `blobs` row is kept while
  removed entries reference it (records history); rows nobody references are
  deleted. Stale temp files (> 1 day) are swept.
* A blob created by an upload that then failed (quota race, error) is deleted
  immediately if nothing references it.

## Backup and restore

* Use `scripts/backup.ts` / `scripts/restore.ts` (`server/ops/backup.ts`,
  `docs/OPERATIONS.md` section 4). A backup holds a consistent `VACUUM INTO`
  copy of the database, every blob it references and the valid checkpoints,
  with a SHA-256 manifest. Restore verifies all of it before writing.
* If a blob is missing or damaged after a restore, re-uploading the same file
  repairs it. The duplicate path re-stores the file, and an existing blob is
  hash-verified and replaced when damaged.
* The catalogs in `native/build/` are build artifacts. They are rebuilt in
  memory when their FBNeo commit differs from `build-info.json`.
* ROM files are private user data: backups must be access-controlled like
  the database.

## Metadata, artwork, authorization

* `update()`: display name 1-80 characters, notes up to 500, handoff rule up
  to 200 (plain text: NFC, control and bidi-override characters stripped;
  clients must still render it as text), mode (one of the room modes) and
  players (1..driver/adapter maximum; versus/co-op/turns-multi need 2+).
* Artwork: nothing is scraped or downloaded. `metadata.artwork` is `null` and
  clients draw a generated placeholder (e.g. initials/colours from the name).
* **Authorization is the caller's job**: the HTTP layer decides who may edit
  (uploader, host, deputies) and who may remove (host, deputies). The shelf
  trusts its `actor` argument and only enforces group isolation.
* `ShelfError` carries a `code` for the caller to map: `not_found` (404),
  `invalid` (400), `not_ready` / `not_playable` / `missing_dependency` /
  `removed` (409), `blob_missing` / `blob_corrupt` (500).

## Known gaps

* No 7z/RAR support (by design: a second untrusted-archive parser is not
  worth it for the pilot; users re-pack as zip).
* CPS-1/CPS-2 and Neo Geo only (the cores we build). Other arcade systems are
  reported as unknown romsets.
* Identification uses only CRC-32 + size (as FBNeo does); SHA-1 verification
  is not available from the driver data.
* Samples (`sfz3mix`) are reported but not loaded.
* Neo Geo games without a BIOS DIP switch use the driver's default BIOS
  instead of `MVS_EUR`; if that default is an optional BIOS image, the
  identification may under-report the requirement. The boot test (with the
  real core) catches it.
* A merged parent zip identifies as the parent; its clones are not playable
  from it (upload the clone's own zip).
* NES has no game database: player count and mode are defaults unless a
  verified adapter matches; editors can change them.
* Boot test success means "loads and draws"; playability is confirmed by a
  real session (`markPlayed(..., 'working')`).
* Pins are in memory; checkpoint retention (invalidating old checkpoints so
  their blobs can go) belongs to the session/recovery code.


## Review updates (2026-10-09)

An adversarial review fixed 11 defects, each with a regression test (shelf
area: 46 tests):
* **Zip parser differential.**
  * Archives must read identically under our parser and minizip, which FBNeo
    uses. Otherwise they are rejected with `zip_ambiguous`.
  * Rejected cases: a second archive hidden in the comment, an end record
    outside minizip's search window, a zip64 locator not declaring exactly
    one disk, and 65535-entry zip64 directories.
* **Web upload robustness.**
  * A request aborted before the handler runs is rejected at once (no hang,
    no temp file).
  * A temp-file write error is a 500 and is logged.
* **Validator replies.** The child's reply is shape-checked, and `romPath`
  must be a path the server gave it.
* **Compat key v2.** It includes the emulated controller hardware (NES Four
  Score), so 2-pad checkpoints never resume as 4-pad.
* **Race fixes.**
  * Dependencies are re-resolved after a boot test.
  * Revalidation re-reads the row before writing.
* **Input validation.**
  * `update()` validates types; bad input is a 400 with `invalid`.
  * An ingest for an unknown group still consumes its temp file.
* **Display safety.**
  * Users see generic validation errors; details go to the operator log,
    since child errors can contain server paths.
  * Bidi override characters in entry names are flagged and neutralized.

Residual risks, tracked in `docs/PROGRESS.md`:
* Validators run as the same OS user as the server. A separate uid or
  container without network, plus a cgroup memory limit, would harden this.
* Web uploads are limited to one in flight per user and 30 per group per
  hour (`server/http/server.ts`). Uploads from the Telegram chat do not yet
  share that cap.
* A slight timing signal remains for "another group already stores this
  file".
* A crash between a blob rename and its row insert leaves an orphan file
  that `cleanup()` does not see.
* `needs_attention` (unknown) entries are not re-identified after a core
  update.
