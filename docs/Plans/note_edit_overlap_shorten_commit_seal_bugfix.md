# NOTE_EDIT overlap shorten — commit drops the row the display shows

**Status:** Active — native shipped (1111/1111); device gate open  
**Date:** 2026-08-13  
**Kind:** bugfix  
**Owner:** `buildCommitOverlapRowsFromCurrentState` in [`NoteEditFocusPreCommit.cpp`](../../src/EditManager/NoteEditFocusPreCommit.cpp)  
**Withdraws:** slice B of [`note_edit_resolver_authority_contracts_refinement.md`](note_edit_resolver_authority_contracts_refinement.md) § Stage 7.5  
**Evidence:** [`session_20260813_204700.log`](../../captures/session_20260813_204700.log) @121 / 135 / 141 / 165–166; [`session_20260807_225025.log`](../../captures/session_20260807_225025.log) @15.404 / 21.545 / 26.291 / 36.927

Two reported symptoms, one mechanism:

1. A shortened overlap note is not committed, so the display reverts to the pre-shorten length on deselect.
2. The shorten lands on a **later** commit instead, so the note's end tick appears to change to the current select tick while the user is editing something else.

---

## Debugging boundary

```
NoteGeometryResolver / EditSessionActionBuilder   ← trust; ShortenNote geometry is correct
        ↓
applyEditSessionActions → currentSpan             ← trust; currentSpan holds the shortened span
        ↓
buildCommitOverlapRowsFromCurrentState            ← this plan
        ↓
commitEditAction store reload from passes         ← do not change
        ↓
determineConstrainedGeometryTargetNoteIds         ← leave-restore defer stays (slice A)
```

Do not fold in overlap `Delete` authority (note 76 in `204700` @161.132), noteId identity across reload, or edit-pass pool reclaim.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `buildCommitOverlapRowsFromCurrentState` already owns commit row authority from `NoteEditCurrentState`. |
| **State-transition change?** | **YES** — when an overlap shorten becomes permanent. User decision recorded below; design session held 2026-08-13. |
| **Reuse** | YES — remove one skip from the existing owner. `participatingNoteVisibleOverlapTailInProgress` stays in `ResolveConstrainedGeometry` and `EditSessionActionBuilder`. |

### Decisions

| Question | Decision |
|----------|----------|
| When does an overlap shorten seal? | **At the commit the user triggers.** What the display shows at deselect is what persists. |
| Host note's tail after the mover | **Discarded** (truncate to `moverStart − 1`). Unchanged from today. |

---

## Root cause

`buildCommitOverlapRowsFromCurrentState` skips an overlap participant on
`participatingNoteVisibleOverlapTailInProgress(participant, focus.last)` — true whenever the
mover's live span still overlaps the participant's `committedSpan`. At a deselect that is true
**by construction**: the mover is parked on the note it just shortened. So the `Length` row is
never in the rows `commitEditAction` saves.

`commitEditAction` then reloads the session store from committed passes
(`loop.passes.materializeToEventVector` → `store.mutStore().loadFromEvents`), so the shortened
span is erased from the store. Only `NoteEditCurrentState.currentSpan` still carries it. Two
continuations:

- Sticky clear ends participation first → `currentStateRowIsOverlapParticipant` is false, no row
  can ever be emitted, shorten lost, note repaints its committed length. **Symptom 1.**
- Another commit happens while participation is still `Active` → the row lands then, against an
  unrelated focus. **Symptom 2.**

`buildPreCommitEditPasses` (parity builder) has no such skip, which is why every one of these
commits logs `NOTE_EDIT commit parity mismatch`.

### Evidence chain — `204700`

| Anchor | Fact |
|--------|------|
| @165.462 | `EditSessionAction: type=1 noteId=10 start=960 end=1247 pitch=46` — note 24's on at 1248 lands inside note 10 (`960–1727`) |
| @166.809 | `parity mismatch canonical=1 apply_owned=2`; only row is `mover_focus targetNoteId=24 Pitch 46` |
| @166.848 | `overlap_baseline_diff targetNoteId=10 Length start=960 end=1247` — one commit late, mover is already note 14 at `1200–1295` |

Same shape three more times: @121.245 → @121.345 (`Length 76 → 1343`), @135.064 → @135.226
(`Length 76 → 911`), @141.281 → @141.442 (`Length 23 → 1199`).

`1247` is `1248 − 1`, the bracket tick where note 24 sat — the "end tick becomes the current
tick" report.

### Slice B was misattributed — `225025`

The guard was added for slice B on the reading that macro commit sealed a wrong length. The
capture shows the sealed length was correct:

| Anchor | Fact |
|--------|------|
| @15.404 | `DNTE,88,2544,2544,534` — note 9 is 534 ticks (`2544–3078`) |
| @21.545 | `DNTE,88,2832,2832,47` — mover 13 parks at `2832–2879`, inside note 9 |
| @26.291 | `Length targetNoteId=9 start=2544 end=2831` — correct truncation to `moverStart − 1` |
| @36.927 | `DNTE,88,2544,2544,287` — reselect shows 287 ticks = `2831 − 2544` |

"Stub was **47**" in the slice-B notes is the **mover's** length, not note 9's. There was no
wrong-length seal. Slice B's own closing note already read "User: lengthened once, resolved
after deselect/reselect".

---

## Fix

Remove the `participatingNoteVisibleOverlapTailInProgress` skip from
`buildCommitOverlapRowsFromCurrentState`. Every other gate stays: participation, visibility,
pitch match, `isPlausibleStorageSpan`, `shouldSkipOverlapDiffToLoopEnd`.

The two builders then agree, so `NOTE_EDIT commit parity mismatch` becomes a real signal again.

Keep the predicate where it was designed — deferring **leave-restore** while closure is active
(`determineConstrainedGeometryTargetNoteIds`, `appendOverlapTargetActions`). This plan does not
change when a note restores.

---

## Test

In [`test_note_edit_current_state.cpp`](../../test/test_note_edit_current_state/test_note_edit_current_state.cpp):

- `test_commit_seals_overlap_length_while_mover_covers_committed_span_225025` — replaces
  `test_commit_skips_overlap_length_while_visible_tail_active_225025`. Note 9 committed
  `2544–3078`, live `2544–2831`, mover `2832–2879`: the `Length` row **is** emitted with
  `end == 2831`.
- `test_deselect_commit_seals_overlap_shorten_under_parked_mover_204700` — note 10 committed
  `960–1727`, live `960–1247`, mover 24 `1248–1343`, all pitch 46 on a 2304-tick loop.
  `buildCommitRowsFromCurrentState` emits `Length 960 → 1247`, and its rows equal
  `buildPreCommitEditPasses` rows.

`test_deselect_clears_overlap_participation_without_geometry_restore_232118` stays unchanged —
`Ended` participation still blocks the row.

---

## Device gate

Long note, park a shorter same-pitch note inside it, deselect. The host note keeps the
shortened length with no revert flash, and no `Length` row appears on the following select step.
`NOTE_EDIT commit parity` reads `ok` for the deselect commit.

Env: `teensy41-capture-serial`. Ask before upload.
