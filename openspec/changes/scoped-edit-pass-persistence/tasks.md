# Scoped edit pass persistence investigation - tasks

**Status:** investigation / closeout — **not** a firmware feature change.  
**Blocked on:** `edit-session-state` (toggle vs exit naming in scenarios).  
**Regression fixes shipped:** 2026-06-23 (`f946d82`) — stale pass, session undo pass-id filter.

**Skip during payload impl** unless a scenario fails — do not duplicate payload row work here.

## Done when (closeout criteria)

- [ ] Exit matrix documented: **`exitEditMode`** commits+closes+save vs **`cycleEditSession`** toggles only.
- [ ] HITL capture shows **`NoteEditPassClosed`**, **`PERS,result,...,ok`**, post-exit undo/redo lines.
- [ ] One native test locks exit-after-in-edit-undo/redo does not leave stale **`editPass`** rows
      (already in **`test_note_edit_session_undo`** — reference, do not rewrite).

## 1. Open change artifacts

- [x] 1.1 Create proposal, design, tasks, and `timeline-passes` spec delta for
      `scoped-edit-pass-persistence`.
- [ ] 1.2 Vocabulary: **`EditSessionType`** (live) vs **`EditPassType`** (stored pass row).

## 2. Exit-path requirements and scenario matrix

- [ ] 2.1 Define separate required behavior for NOTE_EDIT full exit (`exitEditMode`) and scope
      toggle (`cycleEditSession` — depends on **`edit-session-state`**).
- [ ] 2.2 Add scenario matrix entries that verify pass append/close behavior for both paths.
- [ ] 2.3 Add scenario matrix entries for in-edit undo/redo and post-exit undo/redo replay checks.

## 3. Merge-boundary investigation

- [ ] 3.1 Trace and verify `commitAllPendingNoteEditActions -> saveNoteEditPass` on NOTE_EDIT leave.
- [ ] 3.2 Trace and verify `closeNoteEditPass -> NoteEditPassClosed` after successful pass append.
- [ ] 3.3 Add explicit failure-point checks for focus-inactive and empty pre-commit paths.

## 4. Persistence-boundary investigation

- [ ] 4.1 Trace and verify `requestUrgentEditSave/processEditAutosave/requestDeferredSaveState`
      handoff markers on NOTE_EDIT leave.
- [ ] 4.2 Trace and verify deferred writer completion and persisted edits-tail write path
      (`writePersistedEditsTail`) markers.
- [ ] 4.3 Verify deferred-save completion behavior under playback without introducing blocking save
      in playback hot paths.

## 5. Evidence gates

- [x] 5.1 Reproduce with `scripts/host_midi_automation_edit_baseline.py` and capture serial evidence
      (2026-06-23 passes: `captures/host_midi_automation_edit_baseline_20260623_174320.json`).
- [ ] 5.2 Require evidence mapping for `NoteEditPassClosed`, `PERS,result,ok`, and scoped post-exit
      undo/redo markers.
- [ ] 5.3 Add native reproduction checks for the isolated boundary before implementation.

## 6. Validation and closeout

- [ ] 6.1 Run `openspec validate scoped-edit-pass-persistence`.
- [ ] 6.2 Run `pio test -e native`.
- [ ] 6.3 Move to implementation tasks only after investigation evidence and validation pass.
