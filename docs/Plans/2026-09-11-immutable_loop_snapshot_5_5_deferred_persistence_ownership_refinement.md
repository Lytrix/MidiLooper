# OpenSpec 5.5 — StorageManager deferred persistence ownership (non-blocking)

**Status:** **5.5 SHIPPED** (2026-09-11)  
**OpenSpec:** `immutable-loop-snapshot-passes` task **5.5**  
**Depends on:** [5.4 plan](immutable_loop_snapshot_5_4_lsv1_verification_refinement.md) (SHIPPED), [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write)  
**Kind:** verification (no firmware owner change)

## North star (task 5.5)

Verify deferred persistence **remains owned by `StorageManager`** and does **not block playback or `handleMidiInput()`**.

LSV1 dual-write (5.3) and the slot-routing fix ([`deferred_persist_loopid_track_routing_bugfix.md`](deferred_persist_loopid_track_routing_bugfix.md)) still execute as deferred `StorageManager` units. Record/overdub stop **admits** work; it does not serialize the slot file on the stop stack.

## Split vs 5.6

| Task | Verifies |
|---|---|
| **5.5** (this plan) | Owner is still `StorageManager`. Runtime execute is `processDeferredSaveState` after MIDI/playback. Persist work is sliced `PERS,work` with no `PERS,drain` on the HITL `base` path. Playback/`handleMidiInput` keep running. |
| **5.6** | Large-loop save/migration **progress**, overrun/failure preservation, active-snapshot availability during save, and **absence of content-sized synchronous work** at stop, input dispatch, clock dispatch, and track-tick (including whether `handleClearTrack` / SavedSet `saveState()` drains must change). |

## Architecture gate (Phase 4 / task 5.5)

| Question | Answer |
|---|---|
| **Owner module** | `StorageManager::processDeferredSaveState` (execute); `StorageManager::requestDeferredSaveState` / `admitLoopSlotPersist` (admit); `src/main.cpp` remainder + `MidiServiceDrain::allowDeferredSave` (when). `StorageLoopIo` is the writer called from StorageManager work items. |
| **Primary invariant** | Snapshot serialization, deferred save, load, and compatibility migration stay on `StorageManager`. LSV1 does not add a second persist owner and does not run content-sized save from `handleMidiInput()`, playback, or clock dispatch. |
| **Ownership change?** | NO |
| **State transition change?** | NO |
| **Execution bound change?** | NO — verify existing ExecutionBounds rows **14** / **15** |
| **Behavior-preserving?** | YES |
| **Reuse** | YES — extend HITL `base` persistence handoff; add host sliced-work / no-drain check; add source call-site gate. No new Manager, scheduler, or HITL stimulus. |
| **Phase scope** | Task 5.5 verification only |

### Architecture checkpoint

1. **Ownership?** NO  
2. **State transitions?** NO  
3. **Unbounded interval owner?** NO

## Verification matrix

| Requirement | Gate | Status |
|---|---|---|
| Persist execute owner is `StorageManager::processDeferredSaveState` | `scripts/test_deferred_persist_nonblocking.py` call-site allowlist | **PASS** |
| Record/overdub stop admits, does not `saveState()` | Same gate (`TrackCaptureStopCommit.cpp` has zero execute calls) | **PASS** |
| `handleMidiInput` / playback do not execute persist | Same gate (`MidiHandler.cpp`, `TrackPlaybackHotPath.cpp` have zero execute calls) | **PASS** |
| Deferred slices, not a blocking drain, on HITL `base` | `LoopPersist` `PERS,work` ≥ 2 with `start`; `PERS,drain` = 0; `persistence_handoff` `result=ok` | **PASS** [`200657`](../../captures/session_20260911_200657.log) `loop_persist=295 sliced=True drain=0` |
| Playback continues across persist | HITL `base` clock/note PASS; `MO=600` during LoopPersist windows | **PASS** [`200657`](../../captures/session_20260911_200657.log) |
| ExecutionBounds rows 14/15 unchanged | No new bound class | **PASS** |

### Device / host evidence

| Step | Evidence | Result |
|---|---|---|
| Host call-site + unit tests | `.venv/bin/python scripts/test_deferred_persist_nonblocking.py` (8 tests) | **PASS** |
| HITL `base` track 5 / slot 1 | [`200657`](../../captures/session_20260911_200657.log) / report [`200742`](../../captures/host_midi_automation_baseline_20260911_200742.json) | **PASS** — `LoopPersist,slot:4:0` sliced; 5× `PERS,result` ok; `MO` during persist |
| Prior HITL `base` (pre-slot-key) | [`181945`](../../captures/session_20260911_181945.log) | **PASS** (sliced, no drain; `MO` not required) |
| Prior manual LSV1 | [`192637`](../../captures/session_20260911_192637.log) | **PASS** (sliced + `MO` interleave) |

## Stages

| Stage | Scope | Status |
|---|---|---|
| **5.5.1** | Host call-site gate + unit tests | **SHIPPED** |
| **5.5.2** | HITL `base` persistence handoff: sliced `LoopPersist`, no `PERS,drain` | **SHIPPED** |
| **5.5.3** | Device HITL `base` PASS | **SHIPPED** [`200657`](../../captures/session_20260911_200657.log) |
| **5.5.4** | Close `tasks.md` 5.5 + runtime docs | **SHIPPED** |

## Explicit non-goals (5.5)

- Bounding or removing `handleClearTrack` / SavedSet `saveState()` drains (5.6)
- Large-loop progress, overrun policy, active-snapshot-during-save proof (5.6)
- Boot sync load LSV1 (`CurrentSetBootLoad`)
- Dropping `PersistedLoopSnapshot` dual-write (6.x)
- New persist owner, scheduler, or stimulus

## Pre-implementation review

### Proceed?

**YES** — verification complete.
