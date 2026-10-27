# 2026-10-27 — Boot loop-slot restore retry bound fixed (DEC-171)

Owner asked to add a repair solution "similar to the existing repair logic" so a single corrupt loop slot can be dropped
and the rest of the set salvaged — ideally without a full workspace quarantine or physical SD removal.

## Diagnosis (title-screen hang after DEC-170)

`captures/session_20261027_195357.log`: clean boot (`boot_reset,crash=0`, `BOOT,load,ok`), then
`Queuing boot playback loop slot restore 8 pending; first 7/3`, then `LoadLoopJob parse failed` ×73 and a
`RRQ,7,3 … waiting_retry → running → waiting_retry` loop. The corrupt slot is **track 7 / slot 3**
(`/MidiLooper/current/slots/loop_07_03.bin`); the other 7 saved loops are intact but never reached because the retry on 7/3
never yields.

**Not the DEC-170 change.** DEC-170 is report-only diagnostics + a bitfield repack in `LoopPlaybackRuntime`/`TrackOverdubLifecycle`;
it never touches `LoadLoopJob`/`StorageLoopIo`, and the persisted struct sizes are unchanged. The corrupt file has a valid
trailing SAVE token but a structurally invalid body (a torn write would fail token verification instead), so it was written
by an earlier test session with inconsistent content.

## The bug

`StorageManager::scheduleLoopSlotRestoreRetry` (`LoadLoopJob.cpp`):

```cpp
constexpr uint8_t kMaxLoopSlotRestoreAttempts = 3;
uint8_t loopSlotRestoreAttempts_[NUM_TRACKS][MAX_LOOPS_PER_TRACK]{};
bool scheduleLoopSlotRestoreRetry(...) {
    if (loopSlotRestoreAttempts_[track][slot] >= kMaxLoopSlotRestoreAttempts) {  // 0 >= 3, never
        markRestoreRequestTerminal(..., Refused);  // the repair that never fires
        ...; return false;
    }
    markRestoreRequestWaiting(..., Retry);
    return appendBootLoopSlotRestore(...);  // infinite retry
}
```

`loopSlotRestoreAttempts_` is declared and compared but **never incremented**, so the bound is dead and a present-but-corrupt
slot retries forever. The "give up" terminal (`Refused`) that the design intended is unreachable.

## The fix (one line)

```cpp
++loopSlotRestoreAttempts_[trackIndex][slotIndex];   // count this retry before the bound check
if (loopSlotRestoreAttempts_[trackIndex][slotIndex] >= kMaxLoopSlotRestoreAttempts) { ... terminal ... }
```

After 3 attempts the slot reaches a named `Refused` terminal and is treated as empty; the boot proceeds. A *missing* file
already short-circuits to empty in `beginLoadLoopJob`, so only the present-but-corrupt case changes.

## Partial-content salvage — deliberately not attempted

The corrupt file passes `verifySaveFileTokenAtPath` (a valid trailing token) but fails `stepPersistedLoopSnapshotParse` — the
body is structurally invalid, not truncated. There is no intact prefix to recover, so the slot is dropped rather than
partially salvaged. The "salvage" is the other 7 loops.

## Verification

- Updated `test_c6_transient_failure_is_retried_not_latched` (`test_storage_manager_load`) to drive **2** turns (2 attempts
  < bound) instead of 8; the prior 8-turn drive now reaches the bound and re-encoded the unbounded-retry defect as the oracle.
  `test_storage_manager_load` 35/35.
- Full native **3,526 / 7 skipped / 3,519 succeeded / 0 failed**.
- `teensy41-capture-serial` **SUCCESS** at RAM1 `variables 96,192` / ITCM `code 390,472 + padding 2,744` (unchanged).
- Flashed (loader 2.3, `sha256 49154f8f…`); boot verification pending the PROGRAM-button flash.
