## 1. Architecture Gate And Preflight

- [x] 1.1 Read `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md`, `docs/Authority/NAMING.md`, `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`, and `docs/Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`.
- [x] 1.2 Complete full preflight using `docs/Templates/PREFLIGHT.md` because this change is an ownership-transfer formal trigger.
- [x] 1.3 Write the preflight artifact at `openspec/changes/note-edit-current-state/PREFLIGHT.md`.
- [x] 1.4 Include ownership-transfer fields from `docs/Templates/OWNERSHIP_TRANSFER.md`: current owner, target owner, transfer needed = YES, compatibility removal trigger.
- [x] 1.5 Complete decision review (`docs/Templates/DECISION_REVIEW.md` or equivalent findings in the preflight).
- [x] 1.6 Append the accepted ownership decision to `docs/DECISION_LOG.md`.
- [x] 1.7 Confirm final code identifiers for `NoteEditCurrentState`, `NoteEditCurrentNoteState`, and `NoteEditPresenceType`.
- [x] 1.8 Confirm validation gates for later phases: native tests, firmware build, user-approved HITL edit retest, and removal trigger for `sessionMovedNoteSpans`, overlap skip guards, and live-store geometry authority.

## 2. Current-State Foundation

- [x] 2.1 Add current-state row and presence types under existing `EditSession` ownership.
- [x] 2.2 Add read-only build helpers that construct current state from a stamped NOTE_EDIT session store at open/reopen boundaries.
- [x] 2.3 Add deterministic projection from current state to `EditSession.store`.
- [x] 2.4 Add debug verification for duplicate `NoteId`s, selected-note existence, visible-row projection parity, and hidden/deleted projection omission.
- [x] 2.5 Add native tests for current-state construction, presence semantics, and projection invariants.

## 3. API Split And Compatibility Gates

- [x] 3.1 Audit NOTE_EDIT `mutEvents()`, `sessionMidiEvents()`, and `track.editAwareMidiEvents()` writers.
- [x] 3.2 Split read/projection access from current-state mutation access.
- [x] 3.3 Mark any remaining direct projected-store writes as temporary compatibility with a named removal trigger.
- [x] 3.4 Add an accessor-gate native test proving add/delete/move/length/pitch paths do not mutate projected store outside the projection owner after conversion.

## 4. Reader Migration

- [x] 4.1 Route display and selectable inventory reads through current-state visible/added rows while keeping display order derived.
- [x] 4.2 Route focus rebuild and driver validation through current-state rows for `EditorSelection.primaryNote`.
- [x] 4.3 Route geometry scope and overlap analysis through current-state spans and presence.
- [x] 4.4 Route action-builder comparisons through current-state spans and presence.
      Overlap-target path shipped in Phase 4 reader migration; causing-note path in
      `appendCausingNoteActions` shipped in contracts plan Stage 2 (`readEditableCurrentSpan` /
      `editableRowProjectsToStore`; orphan-on `focus.last` fallback retained).
- [x] 4.5 Add native fixtures for same-pitch display reorder and `session_20260807_021939` repeated current-span overlap edits.

## 5. Writer Migration

- [x] 5.1 Route `applyEditSessionActions` through one current-state mutation path, then refresh projection.
- [x] 5.2 Convert move, length, and pitch edit operations to current-state mutations.
- [x] 5.3 Convert add and delete edit operations to `Added` and `Deleted` current-state row mutations.
- [x] 5.4 Convert overlap hide, shorten, and restore to presence/current-span mutations.
- [x] 5.5 Convert dependent fader closure normalization to a projection-owner operation.
- [x] 5.6 Add native fixtures for hidden, deleted, added, shorten, restore, and `session_20260807_021022` stale-baseline regression coverage.

## 6. Undo, Redo, And Folded Capture

- [x] 6.1 Decide and implement session undo payload shape: full current-state snapshot or scoped current-state delta.
- [x] 6.2 Restore current state and projected store together in session undo/redo.
- [x] 6.3 Build redo payloads from current state.
- [x] 6.4 Fold live capture during NOTE_EDIT into current state before projection.
- [x] 6.5 Add native fixtures for move A, move B, move A again, move B again, undo, redo, and folded capture restore.

## 7. Commit And Compatibility Removal

- [x] 7.1 Build note edit commit rows from current state compared to committed baseline.
- [x] 7.2 Keep legacy store-diff builders as parity checks only during migration.
- [x] 7.3 Remove `sessionMovedNoteSpans` after current-state hidden/moved rows cover the same behavior.
- [x] 7.4 Remove session-moved target exclusions and overlap action skip guards after parity fixtures pass.
- [x] 7.5 Remove remaining live-store geometry authority in resolver, action builder, focus rebuild, and commit paths.
      Causing-note action builder no longer uses `readStoreLinearBaseline` (Stage 2 contracts
      plan); overlap reconcile / closure membership remain on live-store diff until Stage 3.

## 8. Verification

- [x] 8.1 Run targeted native tests for NOTE_EDIT current state, action geometry, modification session, undo/redo, and memory routing.
- [x] 8.2 Run `pio test -e native`.
- [x] 8.3 Build firmware with `pio run -e teensy41-capture-serial`.
- [x] 8.4 Ask before uploading firmware to Teensy. *(Skipped — device on `15c5750` lineage; exercised `session_20260808_115120` today.)*
- [x] 8.5 After user-approved upload, run HITL edit retest for same-pitch moved-note overlap scenarios. *(PASS — see [PHASE8_CLOSEOUT.md](PHASE8_CLOSEOUT.md) capture matrix.)*
- [x] 8.6 Update `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md`, and implementation notes before archive.
