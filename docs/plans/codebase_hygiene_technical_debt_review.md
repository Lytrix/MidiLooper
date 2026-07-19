# Codebase hygiene — technical debt review

**Date:** 2026-07-19  
**Scope:** Ranked debt across `src/`, `scripts/`, `docs/`; first safe sprint executed same session.  
**Authority:** Operational next product work remains [CURRENT_WORK.md](../runtime/CURRENT_WORK.md). This doc is hygiene backlog only.

---

## Verdict

The tree has real technical depth (passes/materialize, deferred persistence, note-edit overlap) but also concentrated god files, near-duplicate stop/playback paths, documented dead APIs, and vocabulary drift (`published` / `flatten` / `Take` vs committed / materialize / pass). Structural hygiene is uneven: `src/StorageManager/` extraction is underway; `DisplayManager`, HITL baselines, and `Utils/` remain catch-alls.

**Sprint 1 (2026-07-19):** zero-behavior only — review artifact, dead `Looper` transport API, no-op fader schedule forwarders, `PROJECT_STATE` archive-link drift, `EditManager` header comment.

---

## Evidence snapshot (size)

| File | ~Lines |
|------|--------|
| [`src/StorageManager.cpp`](../../src/StorageManager.cpp) | 4568 (partial extract in [`src/StorageManager/`](../../src/StorageManager/)) |
| [`scripts/host_midi_automation_edit_baseline.py`](../../scripts/host_midi_automation_edit_baseline.py) | 4551 |
| [`scripts/host_midi_automation_baseline.py`](../../scripts/host_midi_automation_baseline.py) | 4303 |
| [`src/DisplayManager.cpp`](../../src/DisplayManager.cpp) | 3345 |
| [`src/NoteEditManager.cpp`](../../src/NoteEditManager.cpp) | 2556 |
| [`src/Track.cpp`](../../src/Track.cpp) | 2457 |
| [`src/Utils/NoteMovementUtils.cpp`](../../src/Utils/NoteMovementUtils.cpp) | 2094 |

---

## A. Complexity / ownership hotspots

| # | Finding | Evidence | Why debt |
|---|---------|----------|----------|
| 1 | `StorageManager::saveState` still monolithic | Root TU + `WorkspaceSave` / `RevisionCommit` / `RevisionLoad` extracts | Highest merge/conflict and review cost |
| 2 | Four near-clone capture stops | `Track::stopRecording`, `stopRecordingToStopped`, `stopOverdubbing`, `stopOverdubbingToStopped`; shared `finalizeCommitSideEffects` + `Loop::commitCapturePass` | High regression risk when stop rules change |
| 3 | Edit split across three managers | `EditManager` (session/store), `NoteEditManager` (buttons/faders — name reads as edit owner), `LoopEditManager` | Callers must know which owner owns side effects |
| 4 | `DisplayManager::resolveDisplayNotes` hotspot | Large cold path; repeated `capture.store.flatten` | Display, capture overlay, edit, windowing tangled |
| 5 | `playMidiEvents` / `playMidiEventsForSlot` fork | Both in `Track.cpp`; wrap/index twins | Fixes land in one path and miss the other |
| 6 | Note-edit geometry concentration | `NoteMovementUtils`, `NoteEditFocus` | Largest algorithmic surface after storage |

---

## B. Stale / dead APIs

| # | Finding | Evidence | Sprint 1 |
|---|---------|----------|----------|
| 7 | Dead `Looper` transport + stub FSM | `startRecording` / `stopRecording` / `startPlayback` / `stopPlayback` / `startOverdub` / `stopOverdub` / `getState` / `handleState` / `requestStateTransition` — no callers outside `Looper.cpp`; live path is `TrackManager` / `MidiButtonActions` | **Removed**; kept `setup` / `update` |
| 8 | Empty fader schedule forwarders | `MidiFaderProcessor::scheduleOtherFaderUpdates` no-op; `MidiFaderManager` only forwards; live work is `NoteEditManager::scheduleOtherFaderUpdates` | **Removed** |
| 9 | `Loop::ensurePassesMaterializedStore` alias | → `materializeEditViewFromPasses` | Queued (rename/alias collapse) |
| 10 | `Track::legacyMidiEventsFromPublished` bridge | Edit APIs still consume “legacy published flat” | **Keep** until edit consumers move; document only |

---

## C. DRY / scripts / naming / layout

| # | Finding | Notes |
|---|---------|-------|
| 11 | Capture flatten copy-paste | Display / edit / loop; test-local length helpers in `test_record_stop_length` |
| 12 | HITL baselines still ~4k lines | Thin `host_midi_hitl.py`; scenarios still import baseline modules |
| 13 | Vocabulary drift | `published` / `Publish*`, `flatten` / `FlatVec`, `Take` in tests/telemetry, `audible` in boot restore |
| 14 | `PlaybackWindow` misnamed | OpenSpec / `slot-performance-interaction` → `PlaybackMergedMidiEvents` when that change is CURRENT_WORK |
| 15 | `PersistenceQueue` vs `PersistenceWorkQueue` | Chunk mid-pass vs semantic jobs — rename when persistence hardening is active |
| 16 | Layout inconsistency | `EditNoteHomeState` outside `EditStates/`; `PersistenceWorkQueue.cpp` at `src/` root vs `StorageManagerInternal/` header |

---

## D. Docs drift

| # | Finding | Sprint 1 |
|---|---------|----------|
| 17 | `PROJECT_STATE` § In flight pointed at live `openspec/changes/deferred-job-scheduler/` after Phase B archive | **Fixed** → archive path; Active OpenSpec table notes archive |
| 18 | ~153 `docs/plans/` + historical `docs/Refinements/` | Authority remains CURRENT_WORK + OpenSpec; mass purge later |

---

## Do not touch without gate / CURRENT_WORK

Protected by [OpenSpec-Phase-Gate](../../.cursor/rules/OpenSpec-Phase-Gate.mdc) and [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md):

- Capture stop / commit: `Track::stopRecording*`, `stopOverdubbing*`, `commitCaptureForStop`, `finalizeCommitSideEffects`, `Loop::commitCapturePass`, `finalizeLoopAtStop`
- Persistence: `StorageManager::saveState`, deferred save / work queue / mid-pass chunk paths
- Playback wrap twins: `playMidiEvents` / `playMidiEventsForSlot` (behavior-preserving extract only with tests)
- Renaming `NoteEditManager` (large blast radius)
- Emptying `Utils/` into domain folders (needs ownership decisions)

---

## Queued hygiene slices (when CURRENT_WORK allows)

1. **Track stop DRY** — single parameterized stop pipeline (`record|overdub` × `playing|stopped`) extending `finalizeCommitSideEffects`; align with [`unified-capture-commit-owner`](../../openspec/changes/unified-capture-commit-owner/)
2. Continue **StorageManager** extraction until `saveState` leaves the root TU
3. Finish **HITL** helper extraction so baseline files shrink to shims
4. Spec’d rename **`PlaybackWindow` → `PlaybackMergedMidiEvents`** under `slot-performance-interaction`
5. Vocabulary rename pass (`published`→`committed`, flatten API) as dedicated OpenSpec change — not drive-by
6. Collapse `ensurePassesMaterializedStore` alias; move `EditNoteHomeState` into `EditStates/`; rehome `PersistenceWorkQueue.cpp`

---

## Sprint 1 checklist

- [x] This review artifact
- [x] Remove unused `Looper` transport / stub FSM; keep `setup` / `update`
- [x] Remove no-op `scheduleOtherFaderUpdates` on `MidiFaderProcessor` / `MidiFaderManager`
- [x] Fix `PROJECT_STATE` deferred-job-scheduler archive link + Active OpenSpec note
- [x] Fix stale `EditManager` header comment (`EditModeManager` / `LoopManager`)
- [x] `pio test -e native` + `pio run -e teensy41-capture-serial` (session verify)
