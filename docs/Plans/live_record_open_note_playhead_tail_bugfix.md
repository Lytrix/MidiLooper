# Live record open-note playhead tail (false wrap to tick 0)

**Status:** Device PASS (playhead tail + tick-0 NoteOn flicker)  
**Branch:** `bugfix/long-overdub-display-freeze`  
**Regression report:** After RC5; open NoteOns in early/growing RECORD lengthened a head from tick 0 until NoteOff.  
**Follow-up evidence:** [`session_20260811_181659.log`](../../captures/session_20260811_181659.log) — temporary off OK; 1px flicker on tick-0 grid at each NoteOn.  
**Final verify:** [`session_20260811_182949.log`](../../captures/session_20260811_182949.log) — both symptoms cleared (`8de682c`).

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

## Tick-0 grid flicker on each NoteOn (follow-up)

**Status:** Device PASS  
**Evidence after clamp-only attempt:** [`session_20260811_182528.log`](../../captures/session_20260811_182528.log) — 1px blips still on each NoteOn pitch at the left grid.  
**Verify:** [`session_20260811_182949.log`](../../captures/session_20260811_182949.log) — left-grid NoteOn blips gone after `mapDisplayNoteBarTicksForLoopPaint` (`8de682c`).

### First attempt (insufficient)

MIDI capture can stamp `note.startTick` one tick ahead of the display frame’s growing `loopLength`. `drawNoteBar` treated `endTick > lengthLoop` as wrap geometry.  
**Partial fix:** `clampNonWrapDisplayNoteBarTicks` in `drawNoteBar` / playhead-tail apply.

### Actual paint-path root cause (`182528`)

During growing RECORD (`windowRelativeTicks == false`), `drawAllNotes` did:

`(n.startTick - jamStartTick + loopLength) % loopLength`

A frontier NoteOn has `startTick == loopLength` (same tick as growing length). That modulo maps to **0**, then `drawNoteBar` paints a 1px bar on the left vertical grid at that pitch — **before** `drawNoteBar`’s clamp can help (inputs are already 0).

Same hazard in `filterDisplayNotesToWindow` via `normalizeTick(loopLength) == 0` once the bounded window is active.

### Fix

1. `mapDisplayNoteBarTicksForLoopPaint` — clamp frontier overflow, then apply jam-origin modulo only when `jamStartTick != 0` (live record uses jam origin 0).
2. `drawAllNotes` uses that mapper on the non-window path.
3. `filterDisplayNotesToWindow` clamps before `normalizeTick`.

## Tests

`test_noteutils_reconstruct`: `test_is_live_wrap_head_continuation_false_when_loop_shorter_than_wrap_window`.  
`test_display_window_utils`: `test_clamp_non_wrap_display_note_bar_ticks_frontier_overflow`, `test_map_display_note_bar_ticks_frontier_equals_length_stays_at_end`, `test_filter_display_notes_to_window_clamps_frontier_equals_length`.
