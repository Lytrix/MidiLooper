# Codebase hygiene — technical debt review

**Date:** 2026-07-19 (updated 2026-08-03 — plans hygiene)  
**Branch:** `chore/codebase-hygiene-sprint1`  
**Authority:** Product scope remains [CURRENT_WORK.md](../runtime/CURRENT_WORK.md). This doc is hygiene backlog only.

---

## Verdict

Safe zero-behavior and rename hygiene on this branch is **complete**. Remaining debt is gated product work (StorageManager extract, `NoteEditManager` rename, `PersistenceQueue` name) or optional leftovers (mass `docs/plans/` purge, runtime `isTrackAudible`, HITL shim import polish).

Sprint refinement plans are marked **Status: Done** (see [plans README — Hygiene sprint](README.md#hygiene-sprint-chorecodebase-hygiene-sprint1)). Mass archive of ~150 historical Cursor plans is **not** done in this pass.

---

## Shipped on this branch

| Commit | Change |
|--------|--------|
| `04c603f` | Dead `Looper` transport + no-op fader schedule APIs; review artifact; `PROJECT_STATE` drift |
| `c5758f0` | Collapse `ensurePassesMaterializedStore` → `materializeEditViewFromPasses` |
| `30a967f` | `EditNoteHomeState` → `EditStates/`; `PersistenceWorkQueue.cpp` → `src/StorageManager/` |
| `9c5e26c` | HITL shared helpers → `hitl/control_constants`, `midi_io`, `serial_collector`, `edit_controls`, `capture_transitions` |
| `373566a` | Thin `host_midi_automation_*.py` CLI shims; bodies in `hitl/legacy_*_baseline.py` |
| `f6cc7c2` | `PlaybackWindow` merge cache → `PlaybackMergedMidiEvents` (OpenSpec slot-performance-interaction Phase −1) |
| `d35407d` | Vocabulary: `published`→`committed` / `copyEventsTo` / `readEvents` / `mutEvents` (locked map) |
| `cbf2b7a` | Track stop DRY + Take/audible boot vocabulary |
| `443c4be` | Display `copySortedCaptureEvents` |
| `5730d44` | `playCommittedLoopMidi` + `advancePlaybackCursor` |
| `6a4822f` | Shared `RecordStopLength` |
| `1cb7ffa` | Clear-slot re-arm after playing clear |

---

## Evidence snapshot (size — post-hygiene)

| File | ~Lines | Notes |
|------|--------|--------|
| [`src/StorageManager.cpp`](../../src/StorageManager.cpp) | ~4568 | Still monolithic; extracts under [`src/StorageManager/`](../../src/StorageManager/) |
| [`scripts/hitl/legacy_edit_baseline.py`](../../scripts/hitl/legacy_edit_baseline.py) | ~4448 | Was top-level edit baseline |
| [`scripts/hitl/legacy_record_baseline.py`](../../scripts/hitl/legacy_record_baseline.py) | ~3986 | Was top-level record baseline |
| [`scripts/host_midi_automation_baseline.py`](../../scripts/host_midi_automation_baseline.py) | **~32** | Thin shim |
| [`scripts/host_midi_automation_edit_baseline.py`](../../scripts/host_midi_automation_edit_baseline.py) | **~32** | Thin shim |
| [`src/DisplayManager.cpp`](../../src/DisplayManager.cpp) | ~3345 | Unchanged size |
| [`src/NoteEditManager.cpp`](../../src/NoteEditManager.cpp) | ~2556 | Name still misleading |
| [`src/Track.cpp`](../../src/Track.cpp) | ~2457 | Stop-path DRY via `commitCaptureForStop` / prep / fold |
| [`src/Utils/NoteMovementUtils.cpp`](../../src/Utils/NoteMovementUtils.cpp) | ~2094 | Unchanged |

---

## A. Complexity / ownership hotspots (still open)

| # | Finding | Status |
|---|---------|--------|
| 1 | `StorageManager::saveState` still monolithic | **Queued** — continue extract when persistence hardening is CURRENT_WORK |
| 2 | Four near-clone capture stops (`stopRecording` / `ToStopped` / overdub twins) | **Done** — `commitCaptureForStop` / `prepareRecordStop` / `handleNoteEditFold`; [`track_stop_dry_refinement.md`](track_stop_dry_refinement.md) |
| 3 | Edit split: `EditManager` vs `NoteEditManager` (misnamed) vs `LoopEditManager` | **Queued** — rename `NoteEditManager` only with approved new name (large blast radius) |
| 4 | `DisplayManager::resolveDisplayNotes` + repeated `capture.store.copyEventsTo` | **Done** — `copySortedCaptureEvents`; removed unused `findCaptureOpenNoteOns` |
| 5 | `playMidiEvents` / `playMidiEventsForSlot` twin wrap walks | **Done** — `playCommittedLoopMidi` + `advancePlaybackCursor`; [`playback_cursor_advance_dry_refinement.md`](playback_cursor_advance_dry_refinement.md) |
| 6 | Note-edit geometry in `NoteMovementUtils` + `NoteEditFocus` | **Open** — algorithmic depth; not a rename |

---

## B. Stale / dead APIs

| # | Finding | Status |
|---|---------|--------|
| 7 | Dead `Looper` transport + stub FSM | **Done** — removed; kept `setup` / `update` |
| 8 | Empty fader `scheduleOtherFaderUpdates` forwarders | **Done** — removed |
| 9 | `ensurePassesMaterializedStore` alias | **Done** — public API is `materializeEditViewFromPasses` only |
| 10 | `Track::legacyMidiEventsFromCommitted` bridge | **Keep** until edit consumers leave the legacy scratch path |

---

## C. DRY / scripts / naming / layout

| # | Finding | Status |
|---|---------|--------|
| 11 | Capture `copyEventsTo` call-site duplication; test-local length helpers | **Done** — display `copySortedCaptureEvents`; `RecordStopLength` shared with Track + [`record_stop_length_shared_helpers_refinement.md`](record_stop_length_shared_helpers_refinement.md) |
| 12 | HITL fat baselines | **Done** — thin CLI shims + `hitl/legacy_*` + shared modules |
| 13 | Vocabulary `published` / `flatten` | **Done** — locked rename in code + Guides; CAP string `"published"` intentionally kept |
| 13b | Leftover `Take` in tests/telemetry; `audible` in boot restore | **Done** — `isBootPlaybackSlot`, `restorePlaybackAfterSlotClear`, `sourceEventCount` / `loopMidiEventsFromPasses` |
| 14 | Merge-cache name `PlaybackWindow` | **Done** — `PlaybackMergedMidiEvents`; domain `makeFullLoopPlaybackWindow` unchanged |
| 15 | `PersistenceQueue` vs `PersistenceWorkQueue` naming clash | **Queued** — rename chunk queue when persistence hardening is active |
| 16 | `EditNoteHomeState` / `PersistenceWorkQueue.cpp` placement | **Done** |

---

## D. Docs drift

| # | Finding | Status |
|---|---------|--------|
| 17 | `PROJECT_STATE` deferred-job-scheduler archive link | **Done** |
| 18 | ~157 `docs/plans/` + historical `docs/Refinements/` | **Partial** — hygiene sprint plans marked Done + indexed in [README.md](README.md#hygiene-sprint-chorecodebase-hygiene-sprint1); mass purge/move of historical Cursor exports still queued |

---

## Do not touch without gate / CURRENT_WORK

Protected by [OpenSpec-Phase-Gate](../../.cursor/rules/OpenSpec-Phase-Gate.mdc) and [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md):

- Capture stop / commit paths (`Track::stopRecording*`, `stopOverdubbing*`, `finalizeCommitSideEffects`, `Loop::commitCapturePass`, …)
- Persistence `saveState` / work queue / mid-pass chunk paths
- Renaming `NoteEditManager` without an approved replacement name
- Emptying `Utils/` into domain folders without ownership decisions

---

## Next hygiene slices (priority)

1. **StorageManager** — continue extraction until `saveState` leaves the root TU (with persistence CURRENT_WORK)
2. **`NoteEditManager` rename** — only after user-approved name (control-surface owner, not edit session)
3. **`PersistenceQueue` → mid-pass/chunk-oriented name** — with persistence hardening
4. Optional: mass `docs/plans/` purge / archive of historical Cursor exports (item 18 remainder)
5. Optional: finish moving scenario imports off thin shims onto `hitl.legacy_*` / shared modules only

---

## Sprint checklist (this branch)

- [x] Review artifact
- [x] Dead `Looper` transport / stub FSM
- [x] No-op fader schedule forwarders
- [x] `PROJECT_STATE` archive-link drift + `EditManager` header comment
- [x] Collapse `materializeEditViewFromPasses` alias
- [x] Layout: `EditNoteHomeState`, `PersistenceWorkQueue.cpp`
- [x] HITL helpers + thin CLI shims
- [x] `PlaybackMergedMidiEvents` Phase −1
- [x] Vocabulary `committed` / `copyEventsTo`
- [x] Track stop DRY (`commitCaptureForStop` / `prepareRecordStop` / `handleNoteEditFold`)
- [x] Vocab leftovers: `Take` / boot `audible` → playback / passes / `sourceEventCount`
- [x] Display `copySortedCaptureEvents` DRY (+ drop unused `findCaptureOpenNoteOns`)
- [x] Playback cursor advance DRY (`playCommittedLoopMidi` + `PlaybackCursorAdvance`)
- [x] Shared `RecordStopLength` helpers (Track + native tests)
- [x] Clear-slot re-arm after playing clear ([`clear_slot_rearm_after_playing_clear_bugfix.md`](clear_slot_rearm_after_playing_clear_bugfix.md))
- [x] Plans hygiene (Status: Done on sprint plans + README index; no mass purge)
- [x] Native tests + `teensy41-capture-serial` build (per change)
