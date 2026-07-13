# Wrap-head display segment bugfix

Display-only fix for the 1-tick piano-roll blink when a wrap-held note crosses the loop boundary at playhead tick 0.

## Root cause

Live display paths used `headEnd > 0` / `clampedCloseTick > 0`, suppressing the head continuation segment exactly when the playhead wrapped to tick 0. Committed storage already treats `headOff@0` as end-at-boundary (tail-only) in `buildCanonicalSpansFromMidi`.

## Fix

Single authority: `NoteUtils::resolveWrapHeadSegment` + `wrapHeadExclusiveEndForDraw`.

Call sites unified:

- `DisplayManager::appendWrapHeldOpenNoteDisplay` (used by `applyCapturePlayheadTails` and `applyLiveOpenTails`)
- `IntervalProjection::renderProjectedIntervalToDisplayNotes` (`splitHeadTail`)
- `DisplayManager::drawNoteBar` (wrapped head pixels)

## Verification

- `pio test -e native` — `test_resolve_wrap_head_segment_*`, `test_wrap_head_exclusive_end_for_draw_at_zero`
- Manual: sustained/wrap-held note during record — no OLED blink at tick 0 after loop wrap

## Out of scope

MIDI playback re-trigger at wrap; seal / `wrap_synth` storage semantics.
