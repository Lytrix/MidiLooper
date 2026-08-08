# Note Edit Display and Commit Stream Refactor

## Purpose

Unify note-edit live display and commit persistence around the applied action stream.

Phase A creates one display projection driver. Phase B records commit rows from applied
`EditSessionAction`s. Phase C retires the remaining baseline rediff and display side channels so
`applyEditSessionActions()` owns canonical post-apply edit-session store invariants and editPass row
serialization becomes deterministic.

## Evidence

- In [`captures/session_20260805_141706.log`](../../captures/session_20260805_141706.log), pitch
  23 produces geometry actions for a shortened overlap plus mover move, but the immediate `DNTE`
  line comes from `NoteMovementUtils` movement telemetry, not from the OLED draw list.
- Pitch 22 later produces a geometry `HideNote` plus `MoveNote`, while display telemetry continues
  to report the mover through separate paths.
- In [`captures/session_20260805_144520.log`](../../captures/session_20260805_144520.log),
  `DNTE,71,0,0,3072,41` appears after an edit in a loop with length `3072`; the displayed note is
  open to loop end, proving display still observes malformed session-store geometry.
- Live-store mutation already has one owner:
  [`RunEditSessionGeometryPipeline.cpp`](../../src/RunEditSessionGeometryPipeline.cpp) calls
  [`applyEditSessionActions`](../../src/ApplyEditSessionActions.cpp).
- Active note-edit display still has multiple producers:
  [`DisplayManager.cpp`](../../src/DisplayManager.cpp),
  [`EditManager.cpp`](../../src/EditManager.cpp),
  [`NoteEditFocus.cpp`](../../src/NoteEditFocus.cpp), and
  [`NoteMovementUtils.cpp`](../../src/Utils/NoteMovementUtils.cpp).

## Architecture Decision

- Ownership change: YES. This is a design refactor, not another minimal display patch.
- State transition change: NO. Driver boundaries, selection transitions, commit timing, and undo
  boundaries stay as they are.
- Reuse decision: YES. Extend existing `NoteEditFocus`, `EditManager`, and
  `applyEditSessionActions` ownership. Do not add a new top-level manager or new domain noun.
- Refactor boundary: `ActionBuilder` decides `EditSessionActions`; `applyEditSessionActions()`
  owns the canonical post-apply store invariant and produces apply-owned `editPass` rows for the
  open `noteEditPass` batch; Commit serializes only; Display projects only.

## Explicit Apply Contract

`applyEditSessionActions()` owns the canonical post-apply edit-session store invariant. Any local
repair algorithm is an implementation detail of that invariant, not the architecture boundary.

After every Apply execution:

- Every visible note has exactly one `NoteOn` and one `NoteOff`.
- Hidden notes remain internally consistent and cannot leak open notes into display.
- No duplicate note pairs exist for a `NoteId`.
- No dangling `NoteOn` events exist.
- No orphan `NoteOff` events are introduced by overlap actions.
- Scoped overlap resolution is complete for the edit operation.
- Display projection performs no geometric repair.
- Commit performs no geometric repair or baseline/live comparison.

Ownership split after this refactor:

```text
ActionBuilder
  -> EditSessionActions
  -> applyEditSessionActions
       -> EditSessionStore
       -> editPass rows for open noteEditPass batch
  -> Commit serializes noteEditPass batch

Display
  -> projects EditSessionStore only
```

## Phase A — Single Display Projection

- Add one active note-edit projection helper in
  [`NoteEditFocus.cpp`](../../src/NoteEditFocus.cpp), replacing `filterSelectableDisplayNotes` as
  the single producer of selectable display notes during note edit.
- The helper reads live session MIDI, `NoteEditFocus.last`, `baselineMap`,
  `changedOverlapNoteIds`, channel, and loop length.
- Route `DisplayManager::resolveDisplayNotes`, `EditManager::selectableDisplayNotesAtEditSelect`,
  `EditManager::selectableDisplayNotesForEditUi`, and
  `NoteMovementUtils::finalReconstructAndSelect` through the same projected `DisplayNoteVec`.
- Collapse duplicate note-edit display caches into the existing `EditManager` session/cache owner.
- Remove or rename movement-side `SC_DNTE`; `DNTE` should mean the selected note displayed by
  `DisplayManager`.

## Phase B — Apply-Owned editPass Rows

- Add apply-owned `editPass` rows as the first-class output of
  [`applyEditSessionActions`](../../src/ApplyEditSessionActions.cpp), using existing `EditPass` /
  `EditPassVec` row payloads for persistence.
- Store those rows on the existing note-edit session owner in
  [`EditManager.h`](../../include/EditManager.h) / [`EditManager.cpp`](../../src/EditManager.cpp),
  as the in-memory rows for the open `noteEditPass` batch.
- Coalesce rows by `targetNoteId` and action/property so repeated fader ticks leave the final
  semantic commit state.
- Preserve action order: overlap delete/shorten rows before mover rows when the applied action order
  requires it.
- Keep temporary `NOTE_EDIT pre-commit row` logging with source labels showing apply-owned rows versus old
  diff path during parity only.

## Phase C — Retire Legacy Rediff

- Remove `buildPreCommitBaselineLiveDiffOverlapPasses` as a production commit source after Phase B
  tests and HITL prove parity with the apply-owned rows.
- Remove baseline/live comparison from commit entirely. Commit becomes a pure serializer that drains
  the open `noteEditPass` batch rows.
- Collapse D19d/D19e cleanup into commit drain: committed overlap `Delete` clears restore authority;
  committed overlap `Update` promotes shortened baseline.
- Remove remaining display-side dependencies on `overlapNotes` scratch once display reads only the
  canonical session store.
- Remove compatibility shims that only existed to protect the retired rediff path.

## Phase D — Pitch Change Uses Apply Ownership

- Treat pitch change as the same geometry owner path as tick movement. `applyPitchChange` must not
  directly mutate note-on/off pitch and return from `applySimplePitchChange` during an active
  `NoteEditSession`.
- Route active-session pitch edits through `runEditSessionGeometryPipelineForCausingNote` so
  `EditSessionActionType::ChangePitch`, overlap restore/shorten/hide actions, apply-owned
  `editPass` rows, and `enforceEditSessionStoreInvariant` run in one transaction.
- Keep direct pitch mutation only as a non-session fallback, or remove it if native tests show every
  active path has a valid `movingNoteId`.
- Use the target pitch lane as the overlap lane, but preserve the mover's current linear
  start/end exactly like tick movement preserves length. The current pitch lane is only a restore
  candidate source, not a second mutation owner.
- Verify non-overlap display notes after pitch changes by checking the full projected
  `DisplayNoteVec`, not only selected-note `DNTE` telemetry. The new capture proves `DNTE` is
  selected-note telemetry, while the visual bug can affect another top-lane note.

## Tests

- Add native end-to-end pipeline tests for pitch 23 and pitch 22 overlap projection.
- Add a cache regression test for repeated move steps without reselect.
- Add Phase B parity tests where apply-owned rows match the rows currently logged by
  `buildPreCommitEditPasses` for the `session_20260805_134610.log` shorten/delete/move scenario.
- Add the `session_20260805_144520.log` pitch-71 loop-end regression under
  `test_apply_edit_session_actions` first; projection tests only assert that display consumes the
  canonical store without repair.
- Add reusable native verifier `verifyEditSessionStoreInvariant(...)` for edit-session stores. The
  verifier checks balanced note pairs, unique `NoteId` pairs, event ordering, hidden-note
  consistency, positive note lengths, no unintended loop-end sentinels, and no orphan `NoteOff`
  events.
- Use the verifier after every apply-focused native regression.
- Add Phase C retirement tests proving commits no longer call the overlap baseline rediff source.
- Add `session_20260805_151910.log` pitch-change regressions:
  - Simple pitch changes in an active session must not bypass the geometry pipeline.
  - Pitch changes into a target lane with overlap candidates must emit the same Hide/Shorten/Restore
    action shapes as tick movement for the same linear span.
  - Full projected display notes after pitch change must contain no non-overlap top-lane note whose
    end reaches the loop length.
- Run `pio test -e native`.
- Build `pio run -e teensy41-capture-serial`; ask before upload.
- After upload, re-run the overlap scenario and validate display `DNTE` source, pitch 22/23 overlap
  geometry, and apply-owned `editPass` rows.

## OpenSpec / Docs

- Update
  [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/edit-session-action-geometry/ARCHITECTURE-REVIEW.md)
  with a display/commit stream gate before firmware edits.
- Update [`tasks.md`](../../openspec/changes/edit-session-action-geometry/tasks.md) with focused
  Phase A/B/C tasks under the current note-edit geometry work.
- Update runtime docs after implementation only if this becomes the active work slice for the
  session.
