# Persist FinalizeWorkspace slice (MIDI Input Gap)

**Status:** Native shipped — workspace CRC sliced; remaining PLAYING `persist_save` 72–83 ms is LoopPersist finalize, not `runtime.bundle.bin`  

**Date:** 2026-08-16  
**Kind:** bugfix  
**Evidence:** [`192334`](../../captures/session_20260815_192334.log) `midi_gap` 135/119/138 ms tracks `persist_save` 77/118/109 ms and `FinalizeWorkspace` `PERS,result` 70–72 ms. [`022147`](../../captures/session_20260816_022147.log) repeats.

## Invariant

One `processDeferredSaveState` call during PLAYING/OVERDUBBING must not walk the whole `runtime.bundle.bin` body. `CurrentSetCompletion` resumes like Footer.

## Owner

`StorageManager` — `stepDeferredSaveJobCurrentSetCompletion` / `finalizeEpochFileHeaderCrc`. No new admit/cancel. No interval reservation.

## Root cause

`kCurrentMetaPath` is `runtime.bundle.bin`. After `patchLastActiveUnix`, `finalizeEpochFileHeaderCrc` reads the entire body in one step (256-byte loop, file closed only at the end). Slice budget while transport is active is 300 µs.

## Change

1. `DeferredCompletionWriteStage` — one SD op per persist step (patch unix, CRC body grain, CRC header write, `workspace.bin`, optional quarantine).
2. CRC body uses `continueEpochFileBodyCrc` at `kEpochFileCrcSliceBytes` (256), same `crc32Continue` as today’s `finalizeEpochFileHeaderCrc`. Keep the file open across grains.
3. One-shot `finalizeEpochFileHeaderCrc` stays for Footer temp finalize and revision/sync.

## Tests

Native: `test_epoch_file_body_crc_slice_matches_one_shot` — chunked `continueEpochFileBodyCrc` matches the device writer’s 256-byte `crc32Continue` loop (`finalizeEpochFileHeaderCrc`), not a single `crc32Continue` over the whole body. `crc32Continue` is not associative across an unchunked body.

## Device gate

PLAYING/OVERDUBBING `persist_save` max in a 5 s window stays near the 300 µs budget except one SD grain. `FinalizeWorkspace` `PERS,result` may still be tens of ms wall-clock (sum of grains). `clockrate` stays ~47.

## Device [`031229`](../../captures/session_20260816_031229.log)

Workspace CRC slice holds: each `FinalizeWorkspace` is 6 `finalize_slice` + `done`. `PERS,result` 113–156 ms wall-clock (sum). After persist settles, PLAYING `persist_save` is 256–318 µs. `clockrate` 47 while PLAYING.

Post-overdub-stop `persist_save` 72–83 ms is **not** `runtime.bundle.bin`. Every `LoopPersist` last-slice→`done` is 73.2 / 77.4 / 82.5 / 83.1 ms. That step is `finalizeDeferredLoopSlotTemp`: one-shot `finalizeEpochFileHeaderCrc` on the loop temp file, then verify + rename. Same CRC walk, different path.

Same windows: `idle_maint` 69–78 ms (`rebuildVisualCacheIdleSlice` after `visualCacheDirty`). `midi_gap` 99–132 ms. Do not fold visual-cache into a persist CRC commit.
