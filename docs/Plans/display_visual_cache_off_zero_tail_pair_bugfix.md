# Display visual-cache Off@0 leftover tail pair

**Status:** Implemented — native **1531/1531**, RAM1 locals **6848**. Device recapture required.  
**Date:** 2026-08-31  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1l  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Debugging boundary:** 1h held-note wrap pair is closed (`4238b422`). Do not widen `isWrappedLoopNotePair`. Do not close overdub held tails at `L-1`. Do not start Stage 2.  
**Evidence:** [`230400`](../../captures/session_20260831_230400.log), [`225243`](../../captures/session_20260831_225243.log), [`231953`](../../captures/session_20260831_231953.log)

**Does not authorize:** Overlay → false on `tryGather`; Stage 2 snapshot-note copy; `openTailCloseTick = L-1`; changing `isWrappedLoopNotePair`; OpenSpec `3.5` / `3.6`.

---

## Debugging boundary

```text
appendCanonicalSpansFromMidiRange Off@0 must not steal mid-loop On
    ← closed (1h)
leftover tail On + leftover F@0 after linear pairing
    ← this slice
finishCanonicalSpansOpenNotes unmatched On → loopLength
    ← must not run for those leftover tail Ons
same-start 144/256/336 stack
    ← 1b, not this slice
```

---

## Problem

[`230400`](../../captures/session_20260831_230400.log) sequential `SEVT` leaves unmatched `N@2928` (D#2 ×3, A2/D#3) and unmatched `F@0` (D#2 ×2). Reconstruct `finishCanonicalSpansOpenNotes` draws an unmatched On to `loopLength`. 2928 is in the tail window (`>= tailStart` on a 3072 loop). 1h already stops `Off@0` from linear-stealing a mid-loop On. `tryPairWrappedTailOn` rejects leftover tail Ons when mid-loop Ons exist.

`B3 3008→3071` with `wrap_synth=1` is a short tail close at `L-1`. That is not this RC.

## Invariant

```
leftover F@0 + leftover On with startTick >= tailStart
    → Off@0 wrap-split tail (start → exclusiveEndForLoopOff(L-1))
    → not start→loopLength via finishCanonicalSpansOpenNotes
mid-loop On + linear Off stays linear (1h)
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO — same reconstruct walk |
| Reuse | YES — extend `appendCanonicalSpansFromMidiRange` after the event walk; same Off@0 tail split as the stack wrap-split (`exclusiveEndForLoopOff(loopLength - 1)`). Do not call `tryPairWrappedTailOn` for leftover. |

## Implementation

- Count `offZeroPaired[pitch]` whenever the walk creates a span from `noteOffTick == 0`.
- After the walk, `offZeroTotal` from Off@0 events. Leftover stack Ons with `startTick >= wrapTailStartTick` get the Off@0 wrap-split while remaining > 0.
- Mid-loop leftover Ons stay for `finishCanonicalSpansOpenNotes` (1h).

Native: `test_reconstruct_leftover_off_zero_pairs_tail_on_not_open_to_loop_end` — `F@0` + mid-loop `N@48`/`F@192` + leftover `N@2928`. `finishOpenNotes=false` still emits `2928→L-1` and `48→192`, no head at 0.

Device [`231953`](../../captures/session_20260831_231953.log): leftover `N@3008` ×3 + `F@0` ×3 on 59; leftover `N@2736`/`N@2960` on 39. Reconstruct painted `59@3008` length **64** (end 3072). Recapture must not emit leftover-to-`loopLength`.

## Explicitly out of scope

- 1k wrap compose hold
- 1b same-start stack
- Stage 2 snapshot-note copy
- Closing overdub tails at `L-1`
