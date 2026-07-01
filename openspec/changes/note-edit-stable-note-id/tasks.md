# Tasks — note-edit-stable-note-id

**Status:** Phase 0 **shipped** — type aliases in `EntityIds.h`. Phase A **shipped** — NoteRef selection gates + windowed nav. Phase B blocked on Phase A exit review.

**Gate:** Phase B implementation starts only after Phase A exit criteria in [design.md](./design.md) pass (`note-edit-fader-feedback-regression` selection-driven refactor stable).

**Authority:** [design.md](./design.md) (architectural contract) → [specs/](./specs/) → this file.

**Before firmware edits:** DECISION_REVIEW + PREFLIGHT; search [DECISION_LOG.md](../../docs/DECISION_LOG.md).

Run `pio test -e native` before push.

---

## 0. Prerequisite — OpenSpec + plan backup

- [x] 0.1 Create `openspec/changes/note-edit-stable-note-id/` with proposal, design, specs, tasks
- [x] 0.2 Copy plan to [docs/plans/note_edit_stable_note_id_enhancement.md](../../docs/plans/note_edit_stable_note_id_enhancement.md)
- [x] 0.3 Register change in [PROJECT_STATE.md](../../docs/runtime/PROJECT_STATE.md)

---

## 1. Phase A — displayIdx / fader selection (prerequisite — separate branch work)

**Owner:** `note-edit-fader-feedback-regression` / current fader refactor branch.

- [x] 1.1 Gate motor sync / apply on selection identity (**NoteRef** equality pre-NoteId) — not `displayIdx` / `selectedNoteIdx` alone
- [x] 1.2 Remove `displayIdx` from persisted selection; derive list index for OLED only
- [x] 1.3 Windowed inventory — `filterSelectableDisplayNotes` + `filterDisplayNotesToWindow`; rebuild on window scroll
- [x] 1.4 Remove dead overlap/chord list-index selection helpers superseded by fader + `SelectNavigation`
- [x] 1.5 Native: `test_note_edit_fader_feedback` selection gate tests pass
- [x] 1.6 HITL: slow fader-1 sweep — every visible note selected exactly once; chord pitch order; `select_ignored_rate` ≈ 0

---

## 2. Phase 0 — Type aliases (no behavior change)

- [x] 2.1 Add `using NoteId = uint32_t` and `using TrackId = uint32_t` on public API surfaces (`kInvalidNoteId = 0`) — [`include/EntityIds.h`](../../../include/EntityIds.h)
- [x] 2.2 Pick and document `kInvalidTrackId` sentinel — **`UINT32_MAX`** (matches `LoopId`); see design D0
- [x] 2.3 `pio test -e native` — no behavior change
- [ ] **TBD — Resolve `EntityIds.h` scope/naming** (open decision [proposal.md](./proposal.md), design **D0a**): rename (A), expand (B), or document-only (C); apply chosen option before Phase B schema work

---

## 3. Phase B slice 1 — Types + allocators

- [ ] 3.1 Add `uint32_t noteId` to `MidiEvent` (note-on only meaningful)
- [ ] 3.2 Add `Loop::nextNoteId_` and `allocateNoteId()`
- [ ] 3.3 Assign on record/overdub note-on append in `Track::recordMidiEvents`
- [ ] 3.4 Safety net: assign stragglers in `Loop::sealCapture` and `foldLiveCaptureIntoNoteEditSession`
- [ ] 3.5 Assign on manual Add (`createNoteAtTick` → Create row note-on)
- [ ] 3.6 `assignMissingNoteIds()` on `openNoteEditSession` with WARNING log per assign

---

## 4. Phase B slice 2 — EditPass + EditApply

- [ ] 4.1 Replace `EditPass.target` (`NoteRef`) with `EditPass.targetNoteId`
- [ ] 4.2 Rewrite `applyNoteEditPass` / `EditApply.cpp` to use `findNoteOnById`
- [ ] 4.3 Update `buildSessionStoreEditPasses` diff to emit `targetNoteId` rows
- [ ] 4.4 Remove `NoteRef` note-targeting helpers (keep `ControlChangeRef` until Phase D)

---

## 5. Phase B slice 3 — Resolve + delete + DisplayNote

- [ ] 5.1 Add `DisplayNote.noteId`; propagate in `reconstructNotesImpl` (wrap split shares id)
- [ ] 5.2 Implement `findNoteOnById`, `resolveDisplayNoteById`, `deleteNoteById` (after pair-resolution)
- [ ] 5.3 Implement `filteredDisplayNoteIndexForNoteId` (derived index only)
- [ ] 5.4 Native tests: allocation regression list (record, move, Add, overlap restore, undo, assign guard rail)

---

## 6. Phase B slice 4 — EditorSelection + focus

- [ ] 6.1 Replace `NoteEditSelection` with `EditorSelection` on `EditManager`
- [ ] 6.2 `NoteEditFocus`: `movingNoteId`, overlap maps and `baselineMap` keyed by `NoteId`
- [ ] 6.3 Session undo snapshots store `EditorSelection` + `targetNoteId` edit rows
- [ ] 6.4 `SelectNavSlot.noteId` in `SelectNavigation`; fader resolve updates `primaryNote`
- [ ] 6.5 Remove stored `selectedNoteIdx` / `displayIdx` as selection identity

---

## 7. Phase B slice 5 — Fader feedback gate

- [ ] 7.1 Gate note-select motor sync on `primaryNote` / `selectedNotes` set change (not list index)
- [ ] 7.2 Native test: inventory rebuild without id change does not false-trigger sync
- [ ] 7.3 HITL: fader sweep with NoteId gates after Phase B landed

---

## 8. Phase B slice 6 — SD v6 + test factories

- [ ] 8.1 Bump slot format version; add `PersistedLoopSnapshot.nextNoteId`
- [ ] 8.2 `writePersistedEditPass` / read: `targetNoteId` wire; larger `MidiEvent`
- [ ] 8.3 Reject v5 files on load; document dev wipe
- [ ] 8.4 Update all native test factories and fixtures for `noteId` / `targetNoteId`
- [ ] 8.5 `pio test -e native` full pass

---

## 9. Verification and closeout

- [ ] 9.1 Run allocation regression native suite (`test_note_id_allocation` or extended `test_edit_apply`)
- [ ] 9.2 HITL edit fader sweep + baseline record/overdub (ids on sealed note-ons)
- [ ] 9.3 Update [PROJECT_STATE.md](../../docs/runtime/PROJECT_STATE.md) and [CURRENT_WORK.md](../../docs/runtime/CURRENT_WORK.md)
- [ ] 9.4 Archive change after gates pass; merge specs into `openspec/specs/`

---

## Follow-up (separate OpenSpecs — not this tasks file)

| Phase | Scope |
|-------|--------|
| **C** | Record/overdub in edit + capture display merge |
| **D** | `ControlChangeId` |

See [design.md](./design.md) and [docs/plans/note_edit_stable_note_id_enhancement.md](../../docs/plans/note_edit_stable_note_id_enhancement.md).
