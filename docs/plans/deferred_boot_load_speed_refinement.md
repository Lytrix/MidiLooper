# Deferred boot load speed — refinement

**Kind:** refinement  
**Date:** 2026-07-15  
**Status:** Implemented (boot UI + cooperative restore slice)

## Follow-up (2026-07-15)

Display still froze during deferred restore (`session_20260715_121844.log`): focus slot **1/3** was synchronously loaded during `loadState` (~5 s in `setup()`), then each heavy slot triggered full piano-roll rebuilds (352–1379 ms `DISP` lines).

Additional changes:

1. **`prioritizeLoopSlotRestoreForFocus`** — skip `requestLoopSlotRestoreFromSd` while `isBootLoadInProgress()` or `hasPendingLoopSlotRestore()`; reprioritize queue only.
2. **`processDeferredLoopSlotRestore`** — during boot deferred phase, drain the full restore queue in one call (no 35 ms slice); after boot, keep ~35 ms cooperative slices for idle slot loads.
3. **`markLoopPublishedChunksPersistedFromSdLoad`** — after each successful slot load, mark all published chunk IDs `Persisted` so post-boot `mid_pass` does not re-write `.sealj` for SD-loaded data.
3. **`DisplayManager::update`** — lightweight `Loading...` view while `hasPendingLoopSlotRestore()` instead of piano-roll rebuild.

## Problem

Cold-boot deferred slot restore slowed to ~0.6–1.9 s per track with data after extmem published-pass routing. Root cause: SD read path sealed chunks and queued `mid_pass` re-writes to `.sealj` even though authoritative data was already in `loop_TT_SS.bin`.

## Changes

1. **`PersistenceQueue::markChunkPersistedFromSdLoad`** — chunks loaded from loop slot files enter `Persisted` without mid_pass queue.
2. **`LoopEventStore::setSdLoadStaging(true)`** on SD read staging — chunk fills during parse call `markChunkPersistedFromSdLoad` instead of `admitSealedChunk`.
3. **Batch SD read** — up to `CHUNK_CAPACITY` events per `ioRead` in `readCapturePassSlotFileHeader`.
4. **`shouldRunMidPassWriter(..., bootSlotRestorePending)`** — returns false while `hasPendingLoopSlotRestore()`.

## Verification

- Native: `pio test -e native` — **631/631 PASS**
- Hardware: after upload, capture boot and run:
  ```bash
  .venv/bin/python scripts/capture_session.py --port /dev/cu.usbmodemXXXX --boot-wait 15
  .venv/bin/python scripts/verify_boot_restore_timing.py --follow-current-session
  # or one explicit file (do not pass shell globs):
  .venv/bin/python scripts/verify_boot_restore_timing.py captures/session_20260715_121011.log
  ```
- Pass: no `PERS,mid_pass` between deferred restore lines; restore span ≤ 3 s for stress set.
- Baseline (pre-fix): [`session_20260715_031837.log`](../../captures/session_20260715_031837.log) — 71 mid_pass during restore, ~7.6 s span.

## Key files

- `src/StorageLoopIo.cpp`
- `src/LoopEventStore.cpp`
- `src/PersistenceQueue.cpp`
- `src/PersistenceFailurePolicy.cpp`
- `src/StorageManager.cpp`
