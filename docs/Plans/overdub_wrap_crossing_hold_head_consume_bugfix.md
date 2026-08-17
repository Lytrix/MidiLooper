# Overdub wrap-crossing hold — consume head + tail as one hold

**Status:** Native **PASS** 1300/1300 (`e1f57c5`). HITL **PASS** [`155450`](../../captures/session_20260817_155450.log) — NOTE_EDIT at tick 64 is **1/2** (86 + one `60@64–240`); not [`153213`](../../captures/session_20260817_153213.log) **1/3**. Exit `VCACHE`/`DFRAME` **5** (not `DISP 8`).  
**Date:** 2026-08-17  
**Kind:** bugfix  
**Parent:** [`overdub_overlap_hold_same_start_bugfix.md`](overdub_overlap_hold_same_start_bugfix.md)  
**Evidence:** [`153213`](../../captures/session_20260817_153213.log) two `DNTE` 60s at tick 64 after wrap-held overdub; [`122152`](../../captures/session_20260817_122152.log) linear pass `hide=1`; [`170449`](../../captures/session_20260813_170449.log) `hide=14` was head as a **second** hold  
**Does not start:** RC11/RC12 consume formula; RC8 ahead-of-hold JIT; DEC-039 `applyNoteEditPass` pitch+start lookup; `rebuildVisualCacheFromPasses` on MIDI wrap

---

## Invariant (one sentence)

**One wrap-crossing incoming note occupies `[S, L) ∪ [0, E)` as a single hold; Shorten/Hide run on notes that overlap either segment; emit one Add.**

170449 forbid: do **not** treat the head as a second incoming hold / second `noteOffs`.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — extend `Loop::accumulatePendingNoteChangesForIncomingNote` |
| **State transition change?** | Consume interval only — same note-off, union window. Not a new mode. |

Reuse: YES — extend `accumulatePendingNoteChangesForIncomingNote`. No new owner.

---

## Root cause

Wrap-held note-off is stored as `endTick < startTick`. The function clipped consume to the tail:

```text
consumeStart = S
consumeEnd   = loopLength
```

A linear pass `[64, 240)` consumes `60@64–288`. A note-on just before wrap with off after wrap never asks geometry about `60@64`. [`153213`](../../captures/session_20260817_153213.log) NOTE_EDIT then shows `60@64–176` stacked on `60@64–224`.

RC11/RC12 stay FROZEN: they complete `selected` **inside** the consume window. They cannot see notes the window excludes.

---

## Fix

When `endTick < startTick`:

1. Union `selected` from `[S, L)` **and** `[0, E)` (JIT hold fill + source-view walk for both).
2. Apply Hide/Shorten to that union (`accumulatePendingNoteChangesFromSourceNotes` once per linear segment, same `causingId`; `upsertSourceTransform` last-wins).
3. One Add with the original wrap pair. `noteOffs` increments once.

Do not call `rebuildVisualCacheFromPasses` on the MIDI wrap path. Do not change DEC-038.2 undo grain.

---

## Tests

- Replace `test_pending_wrap_crossing_incoming_skips_head_hide` with 1-bar `60@64–288`, incoming `S=L-40` / `E=240`: Hide or Shorten of the source id, exactly one Add, `noteOffs=1`.
- Keep `test_pending_shorten_wrap_crossing_incoming_tail` (tail-only source still Shortens).
- Wrap-held long source `0–4099` vs incoming `4000/200` on loop 4100 now Hides (head `[0, E)` is OverlapNoteOn last-wins). Same as loop-4000. One Add, `noteOffs=1`.

---

## HITL

1-bar occupied `60@64`, overdub `64–240`. After stop, NOTE_EDIT at tick 64 is **one** 60 plus `86` (`1/2 notes at this position`), not stacked `176` + `224` in the select inventory. [`155450`](../../captures/session_20260817_155450.log) **PASS** vs [`153213`](../../captures/session_20260817_153213.log) `1/3`. `overlap_hold` `hide=1` `add=1` (not [`170449`](../../captures/session_20260813_170449.log) `hide=14`). Exit `slice_clean`/`DFRAME` **5**.

The `64–240` overdub in [`155450`](../../captures/session_20260817_155450.log) is linear after wrap (`COORD` storage `64`→`240`). Wrap-crossing `60` in that log is on@0 off@8, not hold-through-240.
