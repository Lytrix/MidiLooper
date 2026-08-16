# Persist LoopPersist finalize slice (MIDI after overdub stop)

**Status:** Native shipped — device gate open  
**Date:** 2026-08-16  
**Kind:** bugfix  
**Evidence:** [`031229`](../../captures/session_20260816_031229.log) LoopPersist last-slice→`done` 73.2 / 77.4 / 82.5 / 83.1 ms. Workspace CRC slice (`48bd36f`) already grains `runtime.bundle.bin`.

## Invariant

One `processDeferredSaveState` call during PLAYING must not CRC-walk a whole loop temp file. `finalizeDeferredLoopSlotTemp` resumes like `CurrentSetCompletion`.

## Owner

`StorageManager` — `stepFinalizeDeferredLoopSlotTemp` / `stepLoopPersistWorkItem`. No new admit/cancel. No interval reservation. Visual-cache `idle_maint` is out of scope.

## Root cause

`finalizeDeferredLoopSlotTemp` calls one-shot `finalizeEpochFileHeaderCrc` on the loop temp path after EditTail, in the same persist step as verify + rename.

## Change

1. `DeferredLoopFinalizeStage` — WriteToken → EpochCrcBody → EpochCrcHeader → VerifyAndRename.
2. CRC body uses `continueEpochFileBodyCrc` / `kEpochFileCrcSliceBytes` and `loopFile`.
3. One-shot `finalizeEpochFileHeaderCrc` stays for Footer temp and revision/sync.

## Tests

Native CRC grain already covered by `test_epoch_file_body_crc_slice_matches_one_shot`. Device: LoopPersist last-slice→`done` must not stay at 73–83 ms.

## Device gate

PLAYING after overdub stop: `persist_save` max must not stay at 72–83 ms from LoopPersist finalize. `idle_maint` 69–78 ms may remain. `clockrate` ~47.
