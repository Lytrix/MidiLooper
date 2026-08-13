# Overdub-stop MIDI dump during PLAYING

**Status:** Stage 0 probe + Stage 1 admission shipped. Device verify open.  
**Date:** 2026-08-13  
**Evidence:** [`session_20260813_034408.log`](../../captures/session_20260813_034408.log)  
**Not:** RC-K3 restore, RC-J (STOPPED deferred save), observation Gates 0–4, S1.

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

`loop()` after BAR records three spans into `RuntimeTimingEnvelope` (emitted with the 5 s DIAG window) and a Tier-A one-shot `DIAG,loop_rem,<span>,<us>,<track>,<slot>,<phase>,<focus>` when any span exceeds 50 ms:

| Span | Owner |
|------|--------|
| `idle_maint` | 8× `Track::processDeferredIdleMaintenance` |
| `load_frame` | `runDeferredLoadAndDisplayFrame` |
| `persist_save` | `StorageManager::processDeferredSaveState` |

`StorageManager::probeActiveLoadLoopJob` fills track/slot/phase/focus. Phase 0=Reading, 1=Parsing, 2=Committing; 255=none.

## Stage 1 — LoadLoopJob PLAYING admission (shipped)

[`stepSubmittedLoadJobsImpl`](../../src/StorageManager/LoadLoopJob.cpp) used to step **Committing** before `canRunBackgroundLoadLoopNow()`. Non-focus Commit then called `commitLoadLoopJobPublish` → `applySnapshotToLoop` → `markDisplayCachesStale()` with no slice budget.

Fix: `LoadLoopSelectionPolicy::shouldStepLoadLoopJob(jobIsFocus, backgroundAllowed)` — focus always steps; non-focus only when background is allowed. Committing of a parked Low job waits until STOPPED. Do not abandon mid-publish.

Native: `test_load_loop_selection_policy` (three new cases), `test_runtime_timing_envelope`, `test_capture_line_tier`.

## Device verify (open)

Same 68-bar three-overdub session on `teensy41-capture-serial`.

Pass:

- No 4 s PLAYING `msi` after OVERDUBBING→PLAYING.
- No BPM 200–280 burst.
- `clockrate` holds.
- If a stall remains, `DIAG,loop_rem` / envelope `idle_maint` / `load_frame` / `persist_save` names the grain — stop and re-gate; do not patch RC-J here.

Background Commits waiting until STOPPED may still surface RC-J. Do not patch it in this work.
