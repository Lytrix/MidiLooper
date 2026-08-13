# NOTE_EDIT Length replay — loop-boundary end treated as full-loop wrap

**Status:** Active — native shipped; device gate open  
**Date:** 2026-08-13  
**Kind:** bugfix  
**Owner:** `applyChangeLengthById` in [`EditApply.cpp`](../../src/EditManager/EditApply.cpp)  
**Sibling (emit-side filter, keep as-is):** [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](note_edit_overlap_action_drop_and_wrap_stub_bugfix.md)  
**Evidence:** [`session_20260813_193838.log`](../../captures/session_20260813_193838.log) @122.822 / 129.470; later boots [`200154`](../../captures/session_20260813_200154.log), [`201948`](../../captures/session_20260813_201948.log), [`203140`](../../captures/session_20260813_203140.log)

Persisted `ChangeLength targetNoteId=14 start=2256 newEnd=2304` on a 2304-tick loop is replayed as a wrap. Same-pitch notes are shortened to **2255** (`newStart - 1`) or deleted when they start at tick 0. The selected note then paints at the wrong length and select-away looks like a delete.

---

## Debugging boundary

```
Loop::passes / rematerializeEditView     ← capture takes stay intact (take_only)
        ↓
applyChangeLengthById                    ← this plan
        ↓
NoteUtils::notesOverlap                  ← do not change; wrap contract is correct
        ↓
shouldSkipOverlapDiffToLoopEnd           ← RC2/RC3 emit filter; keep
        ↓
overdubSourceViewNotes_ / overlapNoteIds ← do not read
```

Do not fold note 5 visual-cache pairing, mover pitch-11 drop, or undo-warm into this plan.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `applyChangeLengthById` already owns Length replay geometry. |
| **State-transition change?** | NO. NOTE_EDIT open / commit / rematerialize-at-open unchanged. |
| **Reuse** | YES — extend `applyChangeLengthById`. Do not change `notesOverlap`. |

---

## Root cause

`displayNewEnd = newEnd % loopLength` collapses `2304` to **0**. `notesOverlap(2256, 0, …)` takes the `wrapped1` branch; `unwrappedEnd1 = loopLength`, so every same-pitch note is overlapping. Then:

- `note.startTick < newStart` shortens to `newStart - 1` = **2255** (`EditApply.cpp` is the only `newStart - 1` end writer in `src`)
- `note.startTick == displayNewEnd` (tick 0) → `applyDeleteNoteById`

An unpaired wrap-stub note-on uses `refEnd = refStart`, so `newEnd=2304` is treated as a lengthen and the wrap path runs on every rematerialize.

### Evidence chain

| Capture | Fact |
|---------|------|
| [`193838`](../../captures/session_20260813_193838.log) @122.822 | `DNTE,71,720,720,47` — note is 47 ticks |
| [`193838`](../../captures/session_20260813_193838.log) @129.470 | `saved ChangeLength targetNoteId=14 start=2256 … newEnd=2304` |
| [`200154`](../../captures/session_20260813_200154.log) / [`201948`](../../captures/session_20260813_201948.log) / [`203140`](../../captures/session_20260813_203140.log) | same note paints **1535** ticks → end 2255 |
| [`203140`](../../captures/session_20260813_203140.log) @36.623 | `DNTE,71,1488,1104,767` — second note also ends 2255 |
| [`203140`](../../captures/session_20260813_203140.log) @42.731 | `take_only: M71 start=1488 end=1536` — capture passes intact |

`moverBaselineEnd=2255` at 42.718 is this corruption feeding the next overlap shorten of note 5 to 911.

The 30→29 note loss at 26.398 in `203140` is the explicit `Delete targetNoteId=7` overlap row — not this defect.

---

## Fix

1. **Do not wrap an end at or below the boundary.** Only `newEnd > loopLength` normalizes via modulo. `newEnd == loopLength` stays linear. `notesOverlap(2256, 2304, 720, 767, 2304)` is false; the existing `continue` skips the note.

2. **An unpaired note-on with `newEnd == loopLength` is a no-op.** Reconstruction already paints an open tail as ending at `loopLength`. Do not set `refEnd = loopLength` for every `findNoteOffForOnIndex` miss: that helper also returns −1 when a later same-pitch note-on sits before the real off (overdub companion seal). Only the loop-boundary Length row returns early.

No SD migration. `take_only` still holds the real geometry; correct replay reconstructs it.

---

## Test

`test_length_replay_loop_boundary_does_not_shorten_same_pitch_neighbors_203140` in [`test_edit_apply.cpp`](../../test/test_edit_apply/test_edit_apply.cpp).

`loopLength = 2304`, pitch 71: note A `720–766`, note B `1488–1535`, wrap stub on `2256` with no off, same-pitch note `0–143`. Apply `Length` `endTick = 2304` on the stub. Reconstruct keeps those off ticks; tick-0 note still present; no end at 2255; no `NoteOff` at 2304 for that pitch.

Keep `test_change_length_rematerialize_keeps_p0_off_not_loop_end` unchanged.

---

## Device gate

Same 3-bar loop. First NOTE_EDIT paint: pitch-71 at 720 is **47** ticks; pitch-71 at 1488 is **48**. No `DNTE` pair sharing end 2255.

Env: `teensy41-capture-serial`. Ask before upload.

---

## Pre-implementation review

### Ready

- Production Length replay owner is `applyChangeLengthById`.
- `notesOverlap` wrap contract stays; only the `% loopLength` input changes.
- Capture takes are intact (`take_only`); no repair pass.

### Resolved

| Topic | Decision |
|-------|----------|
| Owner | `applyChangeLengthById` |
| `newEnd == loopLength` | linear boundary end, not wrap |
| Unpaired `refEnd` | `loopLength` |
| Persist rows | leave in place; replay becomes a no-op |

### Open before coding

None.

### Proceed?

YES.
