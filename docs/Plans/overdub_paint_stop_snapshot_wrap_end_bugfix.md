# Overdub paint — stop gather keeps adopted linear ends

**Status:** Host shipped — device recapture next  
**Date:** 2026-09-02  
**Kind:** bugfix  
**Evidence:** [`010217`](../../captures/session_20260902_010217.log) stop at 284.681s  
**Does not replace:** [`overdub_paint_stale_snapshot_after_wrap_bugfix.md`](overdub_paint_stale_snapshot_after_wrap_bugfix.md) (capture-active stale snapshot)  
**Does not authorize:** OVERDUBBING reconstruct, Stage 4, RC2b tails, snapshot pairing rewrite

---

## Debugging boundary

```
storage Off at capturePhaseTick(currentTick)
    ← trust (COORD 2640→2784/2792)
086480f9: gather published snapshot while captureActive
    ← stay
allowVisualCacheReconstructSlice == false during OVERDUBBING
    ← stay
gather after stop prefers successor snapshot over adopt_partial
    ← this slice
```

## Problem

[`010217`](../../captures/session_20260902_010217.log) last chord: 45/39/51 ON @ 2640. 39 OFF @ 2784. Stop finalize writes 45/51 OFF @ 2792 (`finalized=2`, `phase=2792`). Linear on a 3072-tick loop. No `SEVT,F,0`.

STOPPED paint: `adopt_partial` **132** `dirty=0`, first frame **133**, then after persist frame **109**. NOTE_EDIT `rebuildVisualCacheFromPasses` → visual **136** and `projectedNoteEditDisplayNotes` (not snapshot gather). Frame returns to **133**.

`drawNoteBar` paints through tick 0 only when `endTick < startTick`. NOTE_EDIT rematerialize from the same events is linear.

`gatherDrawSourceBars` used any `publishedSnapshot()` as the committed prefix. After stop, capture is inactive, so preview/sealed no longer correct the snapshot. The successor snapshot replaced the adopted 133-note frame.

## Invariant

```
if captureActive and publishedSnapshot() exists: gather that snapshot
if !captureActive and visualCache.notes nonempty: gather visualCache
visualCache only when no published snapshot exists, or after stop when visual is nonempty
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — `gatherDrawSourceBars` |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** |

Reuse: `DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource`. Host twin `gatherDrawSourceDisplayNotes` uses the same predicate.

## Pre-implementation review

### Ready

- `gatherDrawSourceBars` in [`DisplayNoteWindowGather.cpp`](../../src/DisplayManager/DisplayNoteWindowGather.cpp)
- Host twin `gatherDrawSourceDisplayNotes` in `test_loop_take_survival`
- `DisplayWindowUtils` already owns gather predicates

### Resolved

| Topic | Decision |
|-------|----------|
| Capture-active snapshot | Keep 086480f9 |
| After stop | Nonempty `visualCache` (adopt) is paint authority |
| Snapshot pairing / RC2b | Parked — not this owner |
| Reconstruct during overdub | Off |

### Open before coding

None.

### Proceed?

YES

## Tests

Host: published snapshot has wrap-shaped `45@2640→0` (and 51/39). After stop (`!captureActive`), `adopt_partial` visual has `2640→2792` / `2640→2784`. Gather must return the linear ends, not `endTick < startTick`.
