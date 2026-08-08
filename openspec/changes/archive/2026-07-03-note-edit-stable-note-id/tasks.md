# Tasks — note-edit-stable-note-id

**Status:** Phase 0 **shipped**. Phase A **shipped** (2026-07-02). Phase B **shipped** (2026-07-02) — NoteId schema, EditorSelection, SD v6, post-Phase B co-location (`NoteId` → `MidiEvent.h`, `TrackId` → `NoteEditSessionState.h`, `EntityIds.h` deleted).

**Phase A HITL evidence:** `captures/host_midi_automation_baseline_20260702_011228.json` (PASS), `captures/phase_a_slow_fader_sweep_20260702_011229_serial.log` (59 slots, `select_ignored_rate=0`). Handoff: [note_edit_stable_note_id_phase_a_handoff.md](../../docs/Plans/note_edit_stable_note_id_phase_a_handoff.md).

**Gate:** Phase B implementation starts only after Phase A exit criteria in [design.md](./design.md) pass (`note-edit-fader-feedback-regression` selection-driven refactor stable).

**Authority:** [design.md](./design.md) (architectural contract) → [specs/](./specs/) → this file.

**Before firmware edits:** DECISION_REVIEW + PREFLIGHT; search [DECISION_LOG.md](../../docs/DECISION_LOG.md).

Run `pio test -e native` before push.

---

## 0. Prerequisite — OpenSpec + plan backup

- [x] 0.1 Create `openspec/changes/note-edit-stable-note-id/` with proposal, design, specs, tasks
- [x] 0.2 Copy plan to [docs/Plans/note_edit_stable_note_id_enhancement.md](../../docs/Plans/note_edit_stable_note_id_enhancement.md)
- [x] 0.3 Register change in [PROJECT_STATE.md](../../docs/Runtime/PROJECT_STATE.md)

---

## 1. Phase A — displayIdx / fader selection (prerequisite — separate branch work)

**Owner:** `note-edit-fader-feedback-regression` / current fader refactor branch.

- [x] 1.1 Gate motor sync / apply on selection identity (**NoteRef** equality pre-NoteId) — not `displayIdx` / `selectedNoteIdx` alone
- [x] 1.2 Remove `displayIdx` from persisted selection; derive list index for OLED only
- [x] 1.3 Windowed inventory — `filterSelectableDisplayNotes` + `filterDisplayNotesToWindow`; rebuild on window scroll
- [x] 1.4 Remove dead overlap/chord list-index selection helpers superseded by fader + `SelectNavigation`
- [x] 1.5 Native: `test_note_edit_fader_feedback` selection gate tests pass
- [x] 1.6 HITL: slow fader-1 sweep — 59 nav slots (2+2 + second overdub), `select_ignored_rate=0`, sibling sync OK (`run_phase_a_slow_fader_sweep.py`, 2026-07-02)

---

## 2. Phase 0 — Type aliases (no behavior change)

- [x] 2.1 Add `using NoteId = uint32_t` and `using TrackId = uint32_t` on public API surfaces (`kInvalidNoteId = 0`) — [`include/EntityIds.h`](../../../include/EntityIds.h)
- [x] 2.2 Pick and document `kInvalidTrackId` sentinel — **`UINT32_MAX`** (matches `LoopId`); see design D0
- [x] 2.3 `pio test -e native` — no behavior change
- [x] **D0a resolved — `EntityIds.h` scope/naming** (design **D0a**): **(C) document-only** + post-Phase B co-location complete (`NoteId` in `MidiEvent.h`, `TrackId` in `NoteEditSessionState.h`, header deleted)

---

## 3. Phase B slice 1 — Types + allocators

- [x] 3.1 Add `uint32_t noteId` to `MidiEvent` (note-on only meaningful)
- [x] 3.2 Add `Loop::nextNoteId_` and `allocateNoteId()`
- [x] 3.3 Assign on record/overdub note-on append in `Track::recordMidiEvents`
- [x] 3.4 Safety net: assign stragglers in `Loop::sealCapture` and `foldLiveCaptureIntoNoteEditSession`
- [x] 3.5 Assign on manual Add (`createNoteAtTick` → Create row note-on)
- [x] 3.6 `assignMissingNoteIds()` on `openNoteEditSession` with WARNING log per assign

---

## 4. Phase B slice 2 — EditPass + EditApply

- [x] 4.1 Replace `EditPass.target` (`NoteRef`) with `EditPass.targetNoteId`
- [x] 4.2 Rewrite `applyNoteEditPass` / `EditApply.cpp` to use `findNoteOnById`
- [x] 4.3 Update `buildSessionStoreEditPasses` diff to emit `targetNoteId` rows
- [x] 4.4 Remove `NoteRef` note-targeting helpers (keep `ControlChangeRef` until Phase D)

---

## 5. Phase B slice 3 — Resolve + delete + DisplayNote

- [x] 5.1 Add `DisplayNote.noteId`; propagate in `reconstructNotesImpl` (wrap split shares id)
- [x] 5.2 Implement `findNoteOnById`, `resolveDisplayNoteById`, `deleteNoteById` (after pair-resolution)
- [x] 5.3 Implement `filteredDisplayNoteIndexForNoteId` (derived index only)
- [x] 5.4 Native tests: allocation regression list (record, move, Add, overlap restore, undo, assign guard rail)

---

## 6. Phase B slice 4 — EditorSelection + focus

- [x] 6.1 Replace `NoteEditSelection` with `EditorSelection` on `EditManager`
- [x] 6.2 `NoteEditFocus`: `movingNoteId`, overlap maps and `baselineMap` keyed by `NoteId`
- [x] 6.3 Session undo snapshots store `EditorSelection` + `targetNoteId` edit rows
- [x] 6.4 `SelectNavSlot.noteId` in `SelectNavigation`; fader resolve updates `primaryNote`
- [x] 6.5 Remove stored `selectedNoteIdx` / `displayIdx` as selection identity

---

## 7. Phase B slice 5 — Fader feedback gate

- [x] 7.1 Gate note-select motor sync on `primaryNote` / `selectedNotes` set change (not list index)
- [x] 7.2 Native test: inventory rebuild without id change does not false-trigger sync
- [x] 7.3 HITL: fader sweep with NoteId gates — **PASS** `note_edit_select_dependent_faders` 2026-07-03 (`host_midi_hitl_note_edit_select_dependent_faders_20260703_122216.json`)

---

## 8. Phase B slice 6 — SD v6 + test factories

- [x] 8.1 Bump slot format version; add `PersistedLoopSnapshot.nextNoteId`
- [x] 8.2 `writePersistedEditPass` / read: `targetNoteId` wire; larger `MidiEvent`
- [x] 8.3 Reject v5 files on load; document dev wipe
- [x] 8.4 Update all native test factories and fixtures for `noteId` / `targetNoteId`
- [x] 8.5 `pio test -e native` full pass

### 8.6 Post-ship persistence fixes (SD v6 deferred header)

- [x] 8.6.1 `writeDeferredLoopHeader` writes `nextNoteId` (align deferred workspace save with v6 snapshot)
- [x] 8.6.2 Legacy deferred-header read fallback in `readLoopFromCurrentSetFile`
- [x] 8.6.3 `applySnapshotToLoop` → `restorePassesSnapshot` (notes visible after reboot)
- [x] 8.6.4 Native: `test_legacy_deferred_header_without_note_id_reads`

### 8.7 Global undo display + session cycle boundary

- [x] 8.7.1 `getDisplayUndoCount` + `isSessionUndoDisplayActive` — sidebar **`E:nn`** during NOTE_EDIT (session undo stack), **`U:nn`** otherwise (global `TrackUndo`). Phase B regression stub reverted `3348857`.
- [x] 8.7.2 `cycleEditSession` calls `closeNoteEditPass` when leaving NOTE_EDIT

---

## 9. Verification and closeout

- [x] 9.1 Allocation regression covered in extended `test_edit_apply` + `test_storage_loop_io` (`pio test -e native` 336 pass)
- [x] 9.2 HITL edit fader sweep + baseline record/overdub — **PASS** §7.23.6 preset 2026-07-03 (2+2 base + 59-slot sweep)
- [x] 9.3 Update [PROJECT_STATE.md](../../docs/Runtime/PROJECT_STATE.md) and [CURRENT_WORK.md](../../docs/Runtime/CURRENT_WORK.md)
- [x] 9.4 Archive change after gates pass; merge specs into `openspec/specs/`

---

## Follow-up (separate OpenSpecs — not this tasks file)

| Phase | Scope |
|-------|--------|
| **C** | Record/overdub in edit + capture display merge |
| **D** | `ControlChangeId` |

See [design.md](./design.md) and [docs/Plans/note_edit_stable_note_id_enhancement.md](../../docs/Plans/note_edit_stable_note_id_enhancement.md).
