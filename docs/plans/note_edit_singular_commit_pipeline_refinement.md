# Note Edit Singular Commit Pipeline

## Architectural Decision
- Canonical post-apply `NoteEditSession.store` is the single source of truth during NOTE_EDIT.
- `commitAllPendingNoteEditActions` derives persistent `EditPass` rows exclusively from the transaction baseline and canonical post-apply session state.
- Apply-owned rows are temporary diagnostics during migration and are never persistence authority.

## Architecture

### Commit Invariant
- Commit is a pure serialization of canonical post-apply edit state relative to the transaction baseline.
- Commit output is independent of edit action ordering, action coalescing, restore history, and apply-owned row generation.
- Guiding principle: commit serializes what the edit became, not how the edit happened.

### Target Ownership
- ActionBuilder: `buildEditSessionActions` computes edit intent only.
- Apply: `applyEditSessionActions` is the sole writer of canonical `NoteEditSession.store` geometry.
- Projection: `projectNoteEditDisplayNotes` and display snapshot helpers render canonical session state plus `EditorSelection`.
- Commit: `commitAllPendingNoteEditActions` serializes canonical state diff into `EditPass`.
- Selection: resolves logical `EditorSelection` onto projected display state; it does not write persistence.
- Undo: replays committed `EditPass`; it does not depend on action-builder history.
- Apply-owned rows: diagnostics and parity validation only while migrating.

### Source of Truth
- During editing: canonical `NoteEditSession.store`.
- During display: canonical `NoteEditSession.store` plus `EditorSelection`.
- During commit: transaction baseline plus canonical `NoteEditSession.store`.
- Never use `applyOwnedEditPassRows` as source of truth for persistence.

### Consumer Responsibilities
- Every downstream consumer derives its output independently from the canonical state appropriate to its responsibility.
- Projection derives temporary display composition.
- Commit derives persistent `EditPass`.
- Selection derives highlight/bracket/display selection resolution.
- Undo derives replay from committed `EditPass`.

### Consumer Independence
- Projection, Commit, Selection, and Undo are independent consumers of canonical edit state.
- No consumer derives its state from another consumer.
- Display logic must not influence persistence, and persistence logic must not influence display or selection beyond invalidating the canonical state they consume.

## Review Findings (historical — migration landed 2026-08-05)

- **Resolved:** `commitAllPendingNoteEditActions` always serializes via `buildPreCommitEditPasses` (canonical baseline/live diff). The `fromApplyOwned` persistence branch is removed; apply-owned rows are parity diagnostics only.
- **Resolved:** `session_20260805_171134` shape covered by native regressions (mover + overlap deselect/reselect).
- **Ongoing diagnostic:** `NOTE_EDIT commit parity mismatch` (`canonical=2 apply_owned=1`) under `SESSION_CAPTURE` — canonical output wins; not a corruption signal.
- **Design alignment:** Commit model in `design.md` (one `noteEditPass` batch from transaction baseline vs final live store) matches implementation.

## Migration

### Canonical Builder Introduction
- Add or rename a single canonical commit builder around the existing `buildPreCommitEditPasses` path. Its architectural inputs are the transaction baseline and canonical post-apply session state; implementation may continue using existing `NoteEditFocus` fields internally.
- Candidate names must be checked against the codebase before implementation. Current candidates are `buildCanonicalEditPasses`, `serializeCanonicalEditPasses`, or `buildEditPassesFromCanonicalState`.
- The canonical builder should produce all committed `EditPass` rows from canonical state diff, including overlap edits and mover edits.
- Change `commitAllPendingNoteEditActions` to always call that canonical builder. Remove the `fromApplyOwned` persistence branch; log apply-owned rows separately only for parity diagnostics.
- Update `openspec/changes/edit-session-action-geometry/tasks.md` and `openspec/changes/edit-session-action-geometry/design.md` to resolve the conflict: commit authority is canonical state diff, not apply-owned rows.

### Transitional Validation
- During migration, compare canonical `EditPass` rows with apply-owned rows when apply-owned rows exist.
- Log parity differences under `SESSION_CAPTURE`; canonical output always wins.
- Remove parity comparison after native tests and HITL captures show no unexpected differences.
- Apply-owned rows must not influence canonical edit state, projection, selection, persistence, or undo.

### Selection Resolution
- Treat selection as a consumer of canonical committed/session state, not part of the commit algorithm.
- Make empty-step deselect a first-class selection transition: update `applySelectNav` / select handling so deselect refreshes display and surface state consistently.
- Revisit selection resync after commit so commit triggered by selecting a different note does not leave filtered index or DNTE anchored to an unrelated note; derive the selected index from `EditorSelection.primaryNote` when valid and otherwise clear note highlight intentionally.
- Keep selection changes out of commit-row generation except where commit invalidates projection caches that selection consumes.

## Tests
- Add native coverage in `test_note_edit_focus` for canonical commit rows from baseline/live diff in the `session_20260805_171134` shape: mover `noteId=36`, overlap `noteId=31`, deselect/reselect after move.
- Add coverage in `test_apply_edit_session_actions` proving apply-owned rows are diagnostic only and cannot change commit output.
- Add or extend selection tests around empty-step deselect so `applySelectNav(..., kInvalidNoteId)` clears highlight/display consistently and emits the intended surface refresh path.
- Run `pio test -e native`, then `pio run -e teensy41-capture-serial`. After successful build, ask before upload.
- HITL check after upload: reproduce `session_20260805_171134` move-overlap-deselect-reselect; expected result is no unrelated DNTE highlight, no reset/removal of the moved note position, and commit rows matching canonical session state.

## Migration Exit Criteria
- Native regression suite passes.
- HITL captures produce committed `EditPass` output from canonical state with no unexpected parity differences.
- `SESSION_CAPTURE` parity logging reports no unexpected canonical/apply-owned divergence.
- No production code depends on `applyOwnedEditPassRows` for persistence decisions.

## End State
- NOTE_EDIT has one canonical edit state.
- Projection consumes canonical state.
- Commit serializes canonical state.
- Selection resolves canonical state onto projected display.
- Undo replays committed state.
- No production code depends on action-history-derived persistence.
