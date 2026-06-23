## Context

`scoped-edit-pass-model` established scoped edit rows on `passes.editPasses[]` and
`saveNoteEditPass` as the row-append boundary. Current HITL evidence still reports a replay gap
after NOTE_EDIT workflows (`insert_missing_after_in_edit_redo`) even when the session logs
`NoteEditPassClosed`.

Two leave paths exist in the NOTE_EDIT area:

1. `exitEditMode` full exit path (commit pending edit actions, close note edit pass, save request).
2. Overlay toggle path — target **`cycleEditSession`** (**`EditSession::sessionType`**
   **`Loop`** ↔ **`Note`**).

This investigation separates these paths and validates which path guarantees a pass close and
durable persistence handoff.

## Goals / Non-Goals

**Goals:**

- Prove where NOTE_EDIT exit loses edit-pass durability:
  - pre-commit boundary (`commitAllPendingNoteEditActions`),
  - pass append (`saveNoteEditPass`),
  - pass close (`closeNoteEditPass`),
  - deferred persistence handoff (`requestDeferredSaveState` through persisted edits tail).
- Define exact evidence markers for each boundary in serial/HITL and native tests.
- Keep deferred save as the runtime policy while transport is active.

**Non-Goals:**

- No new top-level nouns.
- No broad data-model redesign.
- No blocking save path in playback hot paths.

## Decisions

### D1 - Exit boundary to investigate

`saveNoteEditPass` remains the merge boundary into `passes.editPasses[]`. The investigation tracks
that boundary from NOTE_EDIT leave trigger through `NoteEditPassClosed`.

### D2 - Path split is explicit

`exitEditMode` and **`cycleEditSession`** (legacy **`cycleMainEditMode`**) are treated as
separate behaviors. Land **`edit-session-state`** before finalizing task 2 matrix.

### D2b - Regression fixes shipped

Visual cache, in-session undo baseline, and close-boundary pass replacement fixes are shipped on
`refactor/timeline-data-model`. Persistence investigation focuses on deferred-save markers and
toggle vs full-exit semantics—not legacy **EditChange** payload shape (**`scoped-edit-pass-payload`**).

### D3 - Deferred save runtime policy remains active

The investigation and follow-up fix must preserve deferred-save runtime behavior so playback MIDI
timing is not blocked by save-state work.

### D4 - Evidence-first verification

The change requires marker-level evidence for each boundary:

- `NoteEditPassClosed`
- `PERS,request`
- `PERS,result,ok`
- post-exit scoped undo/redo markers

The same scenario must be verifiable from baseline script output and raw serial capture.

### D5 - Investigation before implementation

Tasks require reproducing the failure and isolating which boundary breaks before introducing
behavioral code changes.

## Risks / Trade-offs

- If mode-toggle and full-exit semantics are mixed, tests can pass one path and fail the other.
- If deferred persistence completion is not tracked at boundary level, replay failures can be
  misattributed to edit materialize logic.
- Tightening guarantees can expose existing low-memory or transport-state edge cases that need
  explicit handling.

## Verification Plan

1. Reproduce the failure with current edit baseline tooling and capture serial evidence.
2. Run scenario matrix for both leave paths and compare:
   - `editPasses[]` state,
   - pass-close marker,
   - deferred-save completion marker,
   - replay markers after in-edit and post-exit undo/redo.
3. Add or update native tests for the isolated boundary once root cause is confirmed.
4. Validate OpenSpec and native suite before moving from investigation tasks to implementation.
