# Scoped edit pass persistence investigation - tasks

**Status:** **Closed** — investigation closeout complete 2026-06-23. Archived after spec merge.  
**Prerequisites shipped:** `edit-session-state`, `scoped-edit-pass-payload` (archived 2026-06-23).  
**Regression fixes shipped:** 2026-06-23 (`f946d82`) — stale pass, session undo pass-id filter.

**Skip during payload impl** unless a scenario fails — do not duplicate payload row work here.

## Done when (closeout criteria)

- [x] Exit matrix documented: **`exitEditMode`** commits+closes+save vs **`cycleEditSession`** toggles only.
- [x] HITL capture shows **`NoteEditPassClosed`**, **`PERS,result,...,ok`**, post-exit undo/redo lines.
- [x] One native test locks exit-after-in-edit-undo/redo does not leave stale **`editPass`** rows
      (already in **`test_note_edit_session_undo`** — reference, do not rewrite).

## 1. Open change artifacts

- [x] 1.1 Create proposal, design, tasks, and `timeline-passes` spec delta for
      `scoped-edit-pass-persistence`.
- [x] 1.2 Vocabulary: **`EditSessionType`** (live) vs **`EditPassType`** (stored pass row).
      Evidence: `edit-session-state` archive D6; naming vocabulary rule.

## 2. Exit-path requirements and scenario matrix

- [x] 2.1 Define separate required behavior for NOTE_EDIT full exit (`exitEditMode`) and scope
      toggle (`cycleEditSession` — depends on **`edit-session-state`**).
      Evidence: merged into `openspec/specs/timeline-passes/spec.md`; `EditManager.cpp`.
- [x] 2.2 Add scenario matrix entries that verify pass append/close behavior for both paths.
      Evidence: main spec scenarios *Full NOTE_EDIT exit* and *session toggle does not close pass*.
- [x] 2.3 Add scenario matrix entries for in-edit undo/redo and post-exit undo/redo replay checks.
      Evidence: HITL `20260623_232352` `session_undo_routing.ok`; main spec *Replay checks after exit*.

## 3. Merge-boundary investigation

- [x] 3.1 Trace and verify `commitAllPendingNoteEditActions -> saveNoteEditPass` on NOTE_EDIT leave.
      Evidence: `EditManager::exitEditMode` → `commitAllPendingNoteEditActions`.
- [x] 3.2 Trace and verify `closeNoteEditPass -> NoteEditPassClosed` after successful pass append.
      Evidence: `exitEditMode` → `closeNoteEditPass`; serial `NoteEditPassClosed` in `20260623_232352`.
- [x] 3.3 Add explicit failure-point checks for focus-inactive and empty pre-commit paths.
      Evidence: existing native + HITL coverage; no failing boundary found.

## 4. Persistence-boundary investigation

- [x] 4.1 Trace and verify `requestUrgentEditSave/processEditAutosave/requestDeferredSaveState`
      handoff markers on NOTE_EDIT leave.
      Evidence: `exitEditMode` when `isEditStateDirty()`; serial `PERS,request` in `20260623_232352`.
- [x] 4.2 Trace and verify deferred writer completion and persisted edits-tail write path
      (`writePersistedEditsTail`) markers.
      Evidence: serial `PERS,result,...,ok` in `20260623_232352_serial.log`.
- [x] 4.3 Verify deferred-save completion behavior under playback without introducing blocking save
      in playback hot paths.
      Evidence: `DEFERRED_RUNTIME_PERSISTENCE` guide; HITL run with transport active.

## 5. Evidence gates

- [x] 5.1 Reproduce with `scripts/host_midi_automation_edit_baseline.py` and capture serial evidence
      (2026-06-23 passes: `captures/host_midi_automation_edit_baseline_20260623_232352.json`).
- [x] 5.2 Require evidence mapping for `NoteEditPassClosed`, `PERS,result,ok`, and scoped post-exit
      undo/redo markers.
      Evidence: `20260623_232352` markers + baseline `_verify_session_undo_redo_routing`.
- [x] 5.3 Add native reproduction checks for the isolated boundary before implementation.
      Evidence: `test_note_edit_session_undo` stale-row / `replaceNoteEditPass` tests.

## 6. Validation and closeout

- [x] 6.1 Run `openspec validate scoped-edit-pass-persistence`.
- [x] 6.2 Run `pio test -e native`.
- [x] 6.3 Investigation evidence and validation pass — no firmware implementation tasks required.
