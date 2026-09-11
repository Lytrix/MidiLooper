# NOTE_EDIT cannot reselect a moved note on a stacked sixteenth

**Status:** device PASS [`164516`](../../captures/session_20260909_164516.log) /
[`164615`](../../captures/host_midi_hitl_note_edit_reselect_stacked_20260909_164615.json)  
**Kind:** bugfix  
**Evidence:** [`session_20260909_153550.log`](../../captures/session_20260909_153550.log),
[`session_20260909_161839.log`](../../captures/session_20260909_161839.log)
**HITL:** `STIM-NE-RESELECT-STACKED-01` v1 / preset `note_edit_reselect_stacked`

## Debugging boundary

```text
NOTE_EDIT PLAYING (no overdub) move + pitch + deselect + reselect
    ← this RC
Overdub select/pitch hang (value_only motors)
    ← shipped; not this
Compose Shorten hold / reopen (RC1/RC2)
    ← not this
```

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — extend `findSlotIndexForSelection` / `resolveFader1SelectTarget` |
| State transition change? | NO |
| Unbounded interval owner? | NO |

## Root cause (`153550`)

Note 81 moved and pitched `78@1536–2015` → `82@2304–2783`. Macro commit on select-away wrote those rows. Origin `78@1536` is gone (`DNTE,84,1536` length 48). Destination `2304` is `1/3 notes`. F1 always applies `note_idx=55` / `DNTE,63,2304`. `noteId=81` never appears again.

`resolveFader1SelectTarget` calls `findSlotIndexForSelection(..., selectedNoteIdx=-1, targetTick)`. That returns the **first** slot at the exact tick. Stacked slots are already built (`test_same_step_three_notes_yield_three_nav_slots`). After deselect, F1 cannot reach the later stacked notes (pitch 82 sorts after 63).

## Invariant

When several selectable notes share a sixteenth, intra-step F1 pitchbend
picks among every note start in that step and any note on the F1 grid
tick — not only exact-tick matches, and not only the last off-grid note.

## Implementation

- `selectStackedNoteIndexFromPitchbend` + 8-arg `findSlotIndexForSelection`
- `resolveFader1SelectTarget` passes the live F1 pitchbend into that finder
- Native: `test_find_slot_picks_stacked_note_from_intra_step_pitchbend_153550`,
  `test_find_slot_picks_off_grid_stack_from_grid_tick_161839`
- Dedicated HITL: fixture seed, move the recorded step-8 note onto the
  recorded step-4 sixteenth, deselect, F1 stacked 0 then 1

## Device HITL

Device PASS [`164516`](../../captures/session_20260909_164516.log): recorded
source `9@432`, dest `5@240`; after move+deselect, F1 stacked apply
`note_idx` **3** then **4**. Report
[`164615`](../../captures/host_midi_hitl_note_edit_reselect_stacked_20260909_164615.json).
Native **1806 succeeded, 1 skipped**. RAM1 locals **7008**.
