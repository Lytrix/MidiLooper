# Overdub-stop MIDI dump during PLAYING

**Status:** LoadLoopJob PLAYING skip closed. `UndoStacks` / `LoopUndoHistory` stall closed — Stage 3 device PASS [`030147`](../../captures/session_20260814_030147.log). **Follow-up shipped (native):** content-only paths admit `LoopPersist` only; `SlotMeta` bundle on overdub stop should drop. Device gate open.  
**Date:** 2026-08-14  
**Evidence:** fail [`session_20260813_034408.log`](../../captures/session_20260813_034408.log); Stage 1 device [`session_20260813_105505.log`](../../captures/session_20260813_105505.log); bundle [`112104`](../../captures/session_20260813_112104.log) / [`154823`](../../captures/session_20260813_154823.log)  
**Relief plan:** [`loop_layer_history_persistence_architecture.md`](loop_layer_history_persistence_architecture.md) (DEC-035)  
**Not:** RC-K3 restore, RC-J, observation Gates 0–4, interval reservation, DEC-024 Phase 2 move of `GlobalUndoStack`.

---

## What the dump is

Each `OVERDUBBING → PLAYING` still sends CC 123 (`sendAllNotesOff`) — same as RC-K3 [`225803`](../../captures/session_20260812_225803.log). Immediate notes after the transition are 0–2.

The audible burst is **~4 s later**: 12–17 notes in 131–375 ms, BPM 200–280. That is `playMidiEvents` catching up ticks queued while `loop()` was stuck. Do not add catch-up suppression; remove the stall.

Three stops in [`034408`](../../captures/session_20260813_034408.log): last CAP before silence is `BAR`; `DIAG,msi` 3.96–4.17 s; `midisvc` 5–61 ms.

## Ruled out

- RC-K3 source-view / note-off reconstruct (`noterecon` 0, `notechg` ~1.2 ms).
- Stored-MIDI verification (L3b idle-only; armed 0).
- Documented **RC-J**: unthrottled deferred save when `timingCriticalTrackActive` goes false at **STOPPED**. Here the track is **PLAYING**.
- Persistence writer as the 4 s grain: `PERS,work,LoopUndoHistory,start` only at 64.66 s after PLAYING→STOPPED; `sliceDone=0`, `peakLat=0`.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — extend `stepSubmittedLoadJobsImpl` / `LoadLoopSelectionPolicy::shouldStepLoadLoopJob`. No `cancel*` on overdub stop. |
| **State transition change?** | NO — OVERDUBBING→PLAYING unchanged. Background load already must skip during PLAYING (`SkipKeepParked`). |
| **RC-J / S0b?** | NO — different owner. Do not widen `timingCriticalTrackActive`. |

## Stage 0 — remainder split (shipped)

`loop()` after BAR records three spans into `RuntimeTimingTelemetry` (emitted with the 5 s DIAG window) and a Tier-A one-shot `DIAG,loop_rem,<span>,<us>,<track>,<slot>,<phase>,<focus>` when any span exceeds 50 ms:

| Span | Owner |
|------|--------|
| `idle_maint` | 8× `Track::processDeferredIdleMaintenance` |
| `load_frame` | `runDeferredLoadAndDisplayFrame` |
| `persist_save` | `StorageManager::processDeferredSaveState` |

`StorageManager::probeActiveLoadLoopJob` fills track/slot/phase/focus. Phase 0=Reading, 1=Parsing, 2=Committing; 255=none.

## Stage 1 — LoadLoopJob PLAYING admission (shipped)

[`stepSubmittedLoadJobsImpl`](../../src/StorageManager/LoadLoopJob.cpp) used to step **Committing** before `canRunBackgroundLoadLoopNow()`. Non-focus Commit then called `commitLoadLoopJobPublish` → `applySnapshotToLoop` → `markDisplayCachesStale()` with no slice budget.

Fix: `LoadLoopSelectionPolicy::shouldStepLoadLoopJob(jobIsFocus, backgroundAllowed)` — focus always steps; non-focus only when background is allowed. Committing of a parked Low job waits until STOPPED. Do not abandon mid-publish.

Native: `test_load_loop_selection_policy` (three new cases), `test_runtime_timing_telemetry`, `test_capture_line_tier`.

## Persistence bundle telemetry — added

`stepRuntimeBundleWorkItem` now emits one Tier-A capture line when a runtime bundle completes:

```text
#CAP,<us>,PERS,bundle,<workType>,<totalUs>,<maxSliceUs>,<sliceCount>,<undoEntryCount>,<snapshotCount>
```

This separates total bundle occupancy from the largest individual slice and records the undo
history payload present when the bundle starts. It is observation-only; bundle scheduling and
slice behavior are unchanged.

## Device verify — [`105505`](../../captures/session_20260813_105505.log)

`noterecon` 0 throughout. Remainder DIAG emitted.

### Stage 1 LoadLoopJob — closed

Post-overdub PLAYING `load_frame` is 16–32 ms, never ~4 s. The only `DIAG,loop_rem,load_frame` is **4.670 s at 11.107 s** (job identity 255) **before** first PLAYING — boot load, not overdub stop.

LoopUndoHistory starts at first PLAYING→STOPPED (~49.3 s), not delayed until session end as in [`034408`](../../captures/session_20260813_034408.log).

### Dump pass criteria — FAIL

| Stop | OVERDUBBING→PLAYING | Result |
|------|---------------------|--------|
| 1 | 20.101 s | Hold: CAP gap 37 ms, BPM 125, `msi` 42 ms, `clockrate` 47 |
| 2 | 30.960 s | `msi` 4.30 s in the window that includes the stop; remainder 17+28+0 ms. BPM 296 at 28.586 s (PLAYING after stop 1) |
| 3 | 45.220 s | ~3 s CAP silence then PLAYING→STOPPED |
| 4 | 64.438 s | Dump match: BPM 274 at 64.444 s, then `BAR` at 64.980 s, then **5.59 s** CAP silence; `msi` 5.62 s at 70.605 s; remainder idle 33 / load 16 / persist 26 ms |
| 7 | 112.181 s | `msi` 7.05 s, `midisvc` 815 ms; `persist_save` one-shot 90 ms |

BPM 200–280 also appears during OVERDUB after long `begin_capture`. `clockrate` is mixed (0 / 2 / 9 / 38 / 42 / 47 / 56 / 89).

### Remainder probe — remaining owner is none of the three spans

Windows with 4–7 s `msi` show `idle_maint` / `load_frame` / `persist_save` at 16–45 ms (except STOPPED `persist_save` 90–124 ms). The 4 s grain is **outside** those spans (work before `idle_maint`, or between `load_frame` and `persist_save`). Stop and re-gate. Do not patch RC-J here. Do not add catch-up suppression.

### Separate stall (not this dump owner)

`ODUB,stage,begin_capture`: 79 ms / **4.35 s** / **5.36 s** / 82 ms / **6.95 s** / **7.01 s** / **5.71 s**. First overdub matches RC-L1; later entries are a different gate.
