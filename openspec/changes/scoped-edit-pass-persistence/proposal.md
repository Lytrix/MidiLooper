## Why

The edit baseline showed a persistence gap after NOTE_EDIT workflows: the run records
`NoteEditPassClosed`, but verification still reported `insert_missing_after_in_edit_redo` in
some sessions. This change tracks exit-path and deferred-save evidence.

**Update 2026-06-23:** Regression fixes landed (`f946d82` — visualCache materialize, session undo
`editPassIdsAtPush`, close-boundary pass replacement). Latest edit HITL passes
(`insert/reorder: redo_ok=True`). Remaining work is formalizing exit paths under renamed overlay
mode (**`note-edit-overlay-mode`**) and marker matrix closeout—not re-investigating the original
display/undo bugs unless they recur.

Runtime policy is fixed: MIDI timing during playback must not be delayed by save-state work.
Deferred save remains the runtime save mechanism while transport is active.

## What Changes

- Add an investigation-focused OpenSpec delta for `timeline-passes`.
- Define explicit exit-path requirements for:
  - NOTE_EDIT full exit (`exitEditMode`) commit and pass-close boundary.
  - NOTE_EDIT/LOOP_EDIT mode toggle (`cycleMainEditMode`) behavior and persistence expectations.
- Define deferred-save verification requirements for NOTE_EDIT exit:
  - request markers,
  - completion markers,
  - replay behavior after in-edit undo/redo and post-exit undo/redo.
- Add investigation tasks that gather serial/HITL evidence and native reproduction before any fix.

## Non-goals

- No new edit domain types or vocabulary changes.
- No immediate implementation refactor in this proposal.
- No transport hot-path blocking save on playback.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `timeline-passes`: clarify NOTE_EDIT exit persistence boundary and deferred-save evidence gates.

## Impact

- **Code paths under investigation:** `src/EditManager.cpp`, `src/NoteEditManager.cpp`,
  `src/Loop.cpp`, `src/LoopPasses.cpp`, `src/StorageManager.cpp`, `src/StorageLoopIo.cpp`.
- **Automation evidence:** `scripts/host_midi_automation_edit_baseline.py` and serial captures.
- **Validation:** `openspec validate scoped-edit-pass-persistence` and `pio test -e native`.
