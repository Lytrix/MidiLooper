# Overdub wrap seal — occupy Hide must not Delete shortened notes

**Status:** Implemented — RAM1 locals **6848**. Native **1537/1537**. Device recapture required.  
**Date:** 2026-09-01  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41)  
**Parent:** [`overdub_occupy_hide_on_hold_current_tick_bugfix.md`](overdub_occupy_hide_on_hold_current_tick_bugfix.md)  
**Evidence:** [`005757`](../../captures/session_20260901_005757.log)

**Does not authorize:** Stage 2 snapshot-note copy; Overlay → false on `tryGather`; OpenSpec `3.5` / `3.6`; changing `classifyEditSessionInteraction` / NOTE_EDIT Shorten.

---

## Debugging boundary

```text
occupy On upsert Hide into pendingNoteChanges_
    ← previous slice; wrap sealed those as Delete
OverlapNoteOff accumulate → Hide entire
    ← previous slice; wrap/stop sealed Delete
commitOverdubWrapAtSessionStart → sealPendingNoteChangesToEditPasses
    ← this slice: paint Hide is not a companion
OverlapNoteOff durable
    ← this slice: Shorten / Length again
```

## Problem

[`005757`](../../captures/session_20260901_005757.log): one 4-bar overdub, 10 wraps. Each wrap calls `sealPendingNoteChangesToEditPasses`. Occupy Hide and OverlapNoteOff Hide became `EditActionType::Delete`. NOTE_EDIT `vch` at 129s: 37 Delete companions (`tid=69..72`, `p=60`, first-pass spans 528/432). Oracle notes=33. During overdub, wrap `adopt_partial` stayed 67–68 while live grew to 108. First STOPPED `DISP` 74 then 70.

The original scale still played during OVERDUBBING (`MO` 69–84, 4 per loop). After stop, Delete companions removed the earlier C notes from resolved content. That is why shortened remnants were gone from both paint and playback.

## Invariant

```
occupy Hide is paint-only (`pendingPaintHides_`) and is not sealed
OverlapNoteOff accumulate emits Shorten and seals Length
CompleteCover / OverlapNoteOn Hide still seals Delete
paint overlay still drops the occupied committed row at currentTick
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO |
| Reuse | YES — `pendingPaintHides_` next to `pendingNoteChanges_`; same seal owner |
