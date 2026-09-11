# NOTE_EDIT exit must persist the still-selected (and parked) mover

**Status:** RC1 native + compose HITL PASS [`225149`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_225149.json) (13/13). Recapture of the 223532 select-away exit is still the device proof for parked persist.  
**Kind:** bugfix  
**Evidence:** [`session_20260909_223532.log`](../../captures/session_20260909_223532.log)  
**Does not reopen:** RC1/RC2 piano-roll retain
([`note_edit_overdub_exit_piano_roll_revert_bugfix.md`](note_edit_overdub_exit_piano_roll_revert_bugfix.md)),
DEC-056, DEC-037 bake “no display-diff replace”, compose `STIM-NE-OD-COMPOSE-01` / `02` / `03` / `04`

## Debugging boundary

```text
NOTE_EDIT geometry apply + currentState + applyOwnedEditPassRows
    ← trust EditSessionAction (223532 type=5 noteId=62 → pitch 0 @312)
empty-step deselect → commitAllPendingNoteEditActions while mover is focus
    ← works; last != commitBaseline
select-away during NOTE_SELECTION_GRACE_PERIOD (750 ms)
    ← 449.647 position queue, 449.663 select note 15 @836, no commit parity
cycleEditSession → commitAllPendingNoteEditActions
    ← RC1: canonical=0, apply_owned=1, bake keeps earlier rows=2
close bake DEC-037 (no display-diff replace)
    ← keep; do not invent persist from paint
```

## Problem

[`223532`](../../captures/session_20260909_223532.log) short-press Edit Mode at `453.984` with a note still selected (no empty-step deselect). Close:

```
NOTE_EDIT commit parity ok canonical=0 pre_commit=0
NOTE_EDIT commit parity mismatch canonical=0 apply_owned=1 first=0
NoteEditPass close keep committed editPass=0 rows=2 (no display-diff replace)
```

The two kept rows are the overdub-entry seal of noteId=64 at `434.140` (`Length 1033–1127` + `Pitch 79`). They are not the later edit.

Last geometry before the select-away:

- `449.193` `EditSessionAction: type=5 noteId=62 start=312 end=408 pitch=0`
- `449.647` position edit queued at 312 (GEOM_APPLY then `done=0` after select preempt)
- `449.663` select note 23 / noteId=15 at tick 836 (`reason=note_changed`)
- No `NOTE_EDIT commit parity` and no `macro commit skipped` at that select
- `449.687` `DNTE,62,836,836,55,23` — new selection, not note 62

`isNoteEditMacroCommitDeferred` is `NOTE_SELECTION_GRACE_PERIOD` (750 ms) after geometry activity. The select at `449.663` is 16 ms after the 312 queue, so it returns before `commitAllPendingNoteEditActions`. Focus rebuilds onto note 15 (`last == commitBaseline`). `applyOwnedEditPassRows` still holds the pitch row for note 62.

Before RC1, exit used the same commit owner but `buildCommitRowsFromCurrentState` emitted only the current mover (`last` vs `commitBaseline`) and overlap participants. Note 62 was neither. Overlap pitch diffs are skipped (`live.pitch != baseline.pitch` → `continue`). Canonical rows were empty. `applyOwnedEditPassRows` was then cleared and the function returned without `commitEditAction`.

Empty-step deselect already committed while the mover was still focus, so the new location was stored. Before RC1, exit after a deferred select-away (or any commit that no longer pointed focus at the dirty note) discarded that location.

## Invariant

`commitAllPendingNoteEditActions` must persist every Existing Visible current-state row whose `currentSpan` differs from `committedSpan`, and must not clear a non-empty `applyOwnedEditPassRows` without committing those rows when current-state/focus built none.

## Owner

`buildCommitRowsFromCurrentState` / `buildPreCommitEditPasses` in
[`NoteEditFocusPreCommit.cpp`](../../src/EditManager/NoteEditFocusPreCommit.cpp)
and `commitAllPendingNoteEditActions` in
[`NoteEditSessionCommit.cpp`](../../src/EditManager/NoteEditSessionCommit.cpp).

Reuse `currentStateRowGeometryDiffersFromCommitted`, `currentStateRowIsExistingAndVisible`, and `buildCommitRowsFromApplyOwned`. No new persist owner. DEC-037 bake stay.

## Architecture checkpoint

1. **Ownership change?** NO — extend the existing commit row builder and commit entry pending check.
2. **State transition change?** NO — exit and deselect already call `commitAllPendingNoteEditActions`.
3. **Unbounded MIDI-interval owner?** NO — current-state walk on the commit/exit path, same bound as today’s overlap scan.

## Tests

- Native: `test_commit_rows_seal_parked_mover_pitch_on_exit_223532` — focus on note 15 (`last == commitBaseline`); note 62 current pitch 0 vs committed 48 at 312–408; expect a Pitch row for 62.
- HITL verifier on compose (do not edit `01`/`02`/`03`/`04`): close must not log `canonical=0` with `apply_owned>0`.
- Host: absent / `canonical=1` pass; `223532`-shaped `canonical=0 apply_owned=1` fail.

## Pre-implementation review

### Ready

- Owners and call sites confirmed: `cycleEditSession` / `exitEditMode` already commit then close.
- `buildCommitRowsFromApplyOwned` exists and is unused on the live commit path (parity-only).
- `test_canonical_commit_rows_do_not_depend_on_apply_owned_diagnostic_rows` stays: current-state remains primary; apply-owned is fallback only when canonical is empty.

### Resolved

| Topic | Decision |
|---|---|
| Commit source | Current-state dirty Visible Existing rows first; apply-owned only when that build is empty |
| Bake | Unchanged DEC-037 keep-committed |
| Compose stimuli | Do not edit `01`/`02`/`03`/`04` |

### Open before coding

None.

### Proceed?

YES

## Shipped behavior

`buildCommitRowsFromCurrentState` / `buildPreCommitEditPasses` emit parked Existing Visible `currentSpan` ≠ `committedSpan` after the current mover and Active overlap rows. `commitAllPendingNoteEditActions` treats that dirty current-state as pending and, when canonical rows are empty, commits `applyOwnedEditPassRows` via `buildCommitRowsFromApplyOwned`.

Living guide: [`MOVE_NOTE_LOGIC.md`](../Guides/MOVE_NOTE_LOGIC.md) § Commit / macro seal. Storage: [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) `commitAllPendingNoteEditActions`. Normative: [`note-edit-modification-session`](../../openspec/specs/note-edit-modification-session/spec.md) parked-mover scenario.

Compose `02` [`225149`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_225149.json) **13/13** (`note_edit_close_persists_apply_owned` judged 1). Native `test_commit_rows_seal_parked_mover_pitch_on_exit_223532`. Do not treat DEC-037 bake as the persist owner.
