# Live record open-note playhead tail (false wrap to tick 0)

**Status:** Fixed  
**Branch:** `bugfix/long-overdub-display-freeze`  
**Regression report:** After RC5; open NoteOns in early/growing RECORD lengthened a head from tick 0 until NoteOff.

## Problem

During growing live RECORD, held notes showed a wrap-style head from tick 0 to the playhead instead of a temporary display note-off at the current capture tick.

## Root cause

`applyCapturePlayheadTails` used `isLiveWrapHeadContinuationDisplay` whenever `closeTick < noteOnTick` and the note sat in the wrap-tail region of the *growing* length. On a non-wrapping timeline that condition means “playhead still catching up to a frontier note-on” (playhead close is clamped into `[0, loopLength-1]`), not “playhead wrapped past 0”. Result: false tail→`loopLength-1` + head from tick 0 until NoteOff.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — `applyCapturePlayheadTails` / `isLiveWrapHeadContinuationDisplay` |
| State transition change? | **NO** |

## Fix

1. Growing RECORD: `allowWrapContinuation = false` in `applyCapturePlayheadTails` — linear extend to playhead only.
2. `isLiveWrapHeadContinuationDisplay`: return false when `wrapTailStartTick == 0` (loop shorter than wrap window).
3. `resolvePlayheadInLoop` growing/fixed RECORD: temporary close at `min(displayTick, loopLength - 1)` (current tick, in-range).

## Tests

`test_noteutils_reconstruct`: `test_is_live_wrap_head_continuation_false_when_loop_shorter_than_wrap_window`.
