# NOTE_EDIT edit-pass replay row payload — bugfix

**Status:** native shipped; device gate open
**Owner:** `LoopPasses::materializeToEventVector` (`applyActiveEditPassesMidi`), `applyNoteEditPassSequence`
**Sibling:** [`note_edit_overlap_shorten_commit_seal_bugfix.md`](note_edit_overlap_shorten_commit_seal_bugfix.md) (RC before this one), [`note_edit_length_replay_loop_boundary_bugfix.md`](note_edit_length_replay_loop_boundary_bugfix.md)

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | **NO** — replay stays in `applyActiveEditPassesMidi` / `applyNoteEditPassSequence`; row apply stays in `applyNoteEditPass` |
| State transition change? | **NO** — no session/capture transition touched |

Removes dead lookup-key rewriting left behind by the Phase B stable-NoteId migration. Minimal patch.

## Symptom

Two reports on the same underlying defect, both after the commit seal fix landed:

| Capture | Report |
|---|---|
| `session_20260813_210821` | An overlapped note that was shortened is restored to its previous length |
| `session_20260813_210945` | Same, plus: hiding a note moves the mover back to its last position |

## Root cause

`applyActiveEditPassesMidi` carried a `tracked` span across rows and rewrote a later row for the
same note:

```cpp
if (tracked && resolved.targetNoteId == trackedNoteId) {
  resolved.startTick = trackedStart;
  resolved.endTick = trackedEnd;   // overwrote the payload
}
```

Before stable NoteId (commit `e9cc97f`) a row identified its target by span
(`NoteRef trackedBaseline` + `resolved.target.startTick/endTick`), so a row committed before an
earlier row moved the note had to be **re-pointed** at the moved span. That rewrite touched a
**lookup key**.

`e9cc97f` replaced the key with `targetNoteId` and left the rewrite in place, mechanically
retargeted from `resolved.target.startTick` to `resolved.startTick`. `startTick` / `endTick` are
now **payload**: `endTick` *is* the value of a `Length` row and `startTick`/`endTick` *are* the
destination of a `NoteRange` row. So any second row for a note that already had a `NoteRange` or
`Length` row was silently replayed with the earlier row's span — the row became a no-op.

`findNoteOnById` locates the note by id regardless of prior moves, so no re-pointing is needed
and the whole `tracked` mechanism is dead weight.

## Evidence

### `210821` — shorten reverts

Note 10 was moved to `801–1568` and pitched to 36 in `session_20260813_205054`, so its pass list
already held `NoteRange 10 → 801–1568`.

| Time | Line |
|---|---|
| `30.842` | `EditSessionAction: type=1 noteId=10 start=801 end=1055` — overlap shorten under mover 18 |
| `31.988` | `commit parity ok canonical=3 apply_owned=3`, rows `Length 10 801→1055`, `NoteRange 18 1056–1151`, `Pitch 18 36` |
| `31.988` | `commitEditAction incoming ChangeLength targetNoteId=10 start=801 moverBaselineEnd=575 newEnd=1055` |
| `32.833` | reselect paints `DNTE,36,801,801,767,11` — **767 ticks**, the pre-shorten length |

The seal was correct and the row was saved. On replay the `Length 10 → 1055` row hit
`trackedNoteId == 10` (set by the `205054` `NoteRange` row) and its `endTick` was rewritten to
`1568`, which equals `refEnd`, so `applyChangeLengthById` returned early.

### `210945` — mover snaps back

| Time | Line |
|---|---|
| `99.273` | rows `Length 10 801→959`, `NoteRange 77 960–1104`, `Pitch 77 36` |
| `115.986` | rows `Delete 10`, `NoteRange 77 768–912` |

On replay the second `NoteRange 77 → 768–912` was rewritten to the first row's `960–1104`, so the
mover reappeared at its previous position after the host was hidden.

## Fix

Drop the `tracked` rewrite from both replay paths and apply each row as stored:

- `applyActiveEditPassesMidi` (`src/Loop/LoopPasses.cpp`) — used by
  `LoopPasses::materializeToEventVector(MidiEventVec&)`
- `applyNoteEditPassSequence` (`src/EditManager/EditApply.cpp`) — used by the
  `SessionMidiEventVec` overlay, session-undo restore, and commit-time session overlay

## Tests

`test/test_edit_apply`:

- `test_length_row_after_earlier_move_row_on_same_note_seals_210821` — pass list
  `NoteRange 2 → 801–1568`, `Pitch 2 → 36`, `Length 2 → 1055`, `NoteRange 1 → 1056–1151`,
  `Pitch 1 → 36`; host must reconstruct at `801–1055`, mover at `1056–1151`.
- `test_second_move_row_on_same_note_wins_over_earlier_span_210945` — pass list
  `NoteRange 1 → 960–1104`, `Pitch 1 → 36`, `Delete 2`, `NoteRange 1 → 768–912`; mover must
  reconstruct at `768–912`.

Both fixtures were confirmed to fail against the pre-fix source with exactly the reported
numbers (`Expected 1055 Was 1568`, `Expected 768 Was 960`), then pass after.

`pio test -e native`: **1113 / 1113**.

## Device gate

1. Park a shorter same-pitch note inside a long note that was **moved in an earlier session**,
   deselect, reselect — the host holds its shortened length across the reselect.
2. Move a note, deselect, hide the host note, move the same note again, deselect — the mover
   stays at its newest position instead of snapping back.
