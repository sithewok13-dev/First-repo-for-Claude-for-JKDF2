# Patches to upstream cores

Applied by `native/build-cores.sh` to the pinned upstream revisions.

## fbneo-cross-instance-savestates.patch

FBNeo's savestates are designed for save/load and rollback **within one
running instance**. Several sound-chip glue layers keep resampler state that
survives from frame to frame but is not part of the savestate:

| File | Unsaved state |
|---|---|
| `src/burn/snd/burn_ym2151.cpp` | `nYM2151Position`, `nFractionalPosition`, carried samples at the start of each channel buffer |
| `src/burn/snd/burn_ym2610.cpp` | `nFractionalPosition`, carried FM and AY samples |
| `src/burn/snd/msm6295.cpp` | per-chip `nFractionalPosition`, `nPreviousSample`, `nCurrentSample` |
| `src/burn/drv/capcom/qs_c.cpp` | `nDelta` (chip-clock accumulator), `interpolate_buffer` |
| `src/burn/snd/fm.c` (YM2610) | postload rebuilt ADPCM-A channels from a stale register shadow (overwriting saved IL/end/volume); now saves `adpcmTL`/`adpcmreg` and only reconnects pan pointers, as FBNeo already does for the YM2608 |
| `src/burn/snd/ymdeltat.c` | postload replaced the saved `now_data` with ROM data |

When a savestate is loaded into a *fresh* instance (a late joiner, a client
resync, or the server restoring a crashed worker), these values start from
zero, the chips are clocked a slightly different number of times per frame,
and the instances drift apart. For MSM6295 and QSound the chip is clocked
inside the renderer, so the drift reaches the sound CPU through status/ready
flags. Found by `test/unit/fbneo-determinism.test.ts`; this patch adds the
values to the existing scan functions. It changes the savestate layout, so
checkpoints are tied to the patched build (the compat key includes the build
info). Other FBNeo sound glue files have the same pattern (see the list in
`docs/ARCHITECTURE.md`); they are not used by the CPS-1/CPS-2/Neo Geo subsets
built here and must be patched and tested before enabling more drivers.
