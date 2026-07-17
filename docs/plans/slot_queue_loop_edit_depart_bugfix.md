# Slot queue LOOP_EDIT depart — bugfix

**Kind:** bugfix  
**Date:** 2026-07-17  
**Status:** Firmware + native done — device gate next  
**Evidence:** [`captures/session_20260717_234742.log`](../../captures/session_20260717_234742.log)

## Symptom

Playing slot1 and queueing slot2 hard-hangs near Loop 1 end. Note-info / playhead regressions around queued preview.

## Root cause

1. LOOP_EDIT baseline captured SD `loopStartTick` (e.g. 3216) before transport.
2. `Track::reanchorPlaybackProjection(preserve=false)` zeros live `loopStartTick` for the playback frame.
3. Queue → `commitLoopEditOnDepart` called `revertSessionGeometryToBaseline`, writing 3216 back onto the **playing** loop mid-play.
4. LoopEnd wrap detection / commit path corrupted; hang near length boundary; no `LoopEnd playback commit` log.

## Fix

| Change | Owner |
|--------|--------|
| Transport-active depart: clear pending only — **no** geometry write | `LoopEditManager::commitLoopEditOnDepart` |
| After transport reanchor, sync LOOP_EDIT baseline to live geometry | `Track::reanchorPlaybackProjection` → `onGlobalGeometryRestored` |
| Log when LoopEnd commit cancels for missing RAM data | `TrackManager::updateAllTracks` |
| Policy helpers | `Utils/LoopEditDepartGeometry.h` |

## Follow-up: queued launch countdown (2026-07-18)

While a LoopEnd slot switch is pending, the bottom info time field shows **remaining musical time** until commit as `-BB:BB:SS:TT` (e.g. `-04:00:00:00` … `00:00:00:00`). Piano-roll playhead stays parked at the queued loop launch bracket (display phase 0).

Helper: `ticksRemainingUntilLoopEndLaunch` in `Utils/SlotFocusDisplay.h`.
