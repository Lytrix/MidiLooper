# Display visual-cache held-note wrap pair

**Status:** Implemented — native **1523/1523**; RAM1 locals **6944**  
**Date:** 2026-08-31  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1h  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Debugging boundary:** 1g wrap dirty span is closed (`fecac77d`). Do not reopen 1b–1g paint. Do not start Stage 2. Do not close overdub held tails at `L-1`. Do not widen `isWrappedLoopNotePair`.  
**Evidence:** [`221925`](../../captures/session_20260831_221925.log), [`222349`](../../captures/session_20260831_222349.log)

**Does not authorize:** Overlay → false on `tryGather`; Stage 2 snapshot-note copy; `openTailCloseTick = L-1`; OpenSpec `3.5` / `3.6`.

---

## Debugging boundary

```text
markTickSpanDirty wrap-held On/Off bars only
    ← closed (1g)
appendCanonicalSpansFromMidiRange Off@0 vs linear Off
    ← this slice
rebuildOverdubCommittedDisplayLayer / STOPPED last-resort
    ← Stage B (1i) native **1525/1525**
```

---

## Problem

[`221925`](../../captures/session_20260831_221925.log) USB B3 is balanced (`17/17`). `SEVT` has `N@3008` plus `F@0`. `WRAP,1856,0,4,59` is On/Off ticks. Reconstruct’s stack fallback assigned `Off@0` to a mid-loop On (`1856`) whose start is after the off tick, then `finishCanonicalSpansOpenNotes` drew that On to `loopLength`.

[`222349`](../../captures/session_20260831_222349.log) B3 is the only pitch held at seal. `Track::finalizePendingNotes` already closes at playhead (`test_stop_closes_open_tail_at_playhead_not_loop_end`). No `wrap_synth` change.

## Invariant

```
Off@0 wrap-splits a tail-window On only
mid-loop On + linear Off → linear span, never start→loopLength
overdub held tail closes at playhead, not L-1
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO |
| Reuse | YES — extend `appendCanonicalSpansFromMidiRange` pairIndex fallback |

## Implementation

- Stack fallback to the last On is allowed only when `isPreferredWrapTailForHeadOffInSpanBuild` and `startTick >= tailStart`.
- Otherwise `Off@0` does not linear-steal a mid-loop On; `tryPairWrappedTailOn` may still take a true wrap-held tail.
- `finalizePendingNotes` / `openTailCloseTick` unchanged.

## Verification

Native (`test_noteutils_reconstruct`):

- `N@1856` / `F@1912` / `N@3008` / `F@0` on a 3072 loop: `1856` length 56 or 64, not `1856→3071`. `3008` may be tail-only to `L-1`.
- `Off@0` after both Ons still keeps the linear `1856–1912` span.

Device: after Stage B recapture; Stage A is reconstruct-only.

## Explicitly out of scope

- Stage B first-paint hold
- Stage 2 snapshot-note copy
- Overlay return
- Changing `isWrappedLoopNotePair`
