# Overdub paint — second overdub keeps adopted cache when snapshot is stale

**Status:** Host shipped — device recapture next  
**Date:** 2026-09-02  
**Kind:** bugfix  
**Evidence:** [`092344`](../../captures/session_20260902_092344.log)  
**Does not replace:** [`overdub_paint_stale_snapshot_after_wrap_bugfix.md`](overdub_paint_stale_snapshot_after_wrap_bugfix.md) (`004351` dirty-cache wrap)  
**Does not authorize:** OVERDUBBING reconstruct, Stage 4, prefix-upsert of closed preview, persist-lane cadence

---

## Debugging boundary

```
closed preview after committedDisplayEnd
    ← trust (005439)
allowVisualCacheReconstructSlice == false
    ← trust
dirty/empty cache during a live session still gathers last snapshot
    ← trust (004351)
clean nonempty adopt + snap=0 after openOverdubSession
    ← this slice
```

## Problem

[`092344`](../../captures/session_20260902_092344.log): OD1 stop `adopt_partial notes,164 dirty,0`. OD2 reopens the session with `snap=0`. Paint gathered the last published snapshot. Frame started at **113**, `hidden=39`, and rewrote adopted `39@2976→3055` to wrap-held `2976→2784`. New notes only appeared via `capturePreview` 1→24.

Capture/session active alone selected snapshot. That exception is required while `visualCache` is still the record-sized dirty cache. It must not leak across `adopt_partial → openOverdubSession`.

## Invariant

```
hasPublishedSnapshot is a hard gate
current snapshot → snapshot
else dirty or empty visualCache → last snapshot
else clean nonempty visualCache → visualCache
after stop, nonempty visualCache is paint authority
visualCacheDirty is Loop::visualCacheDirty (adopt/stale), not pendingNoteChanges_
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** |

Reuse: `DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource`. Host twin `gatherDrawSourceDisplayNotes` uses the same predicate.

## Tests

- `test_display_window_utils`: truth table including no-snapshot rows
- `test_loop_take_survival`: `004351` dirty wrap still gathers snapshot; `092344` clean adopt keeps `2976→3055`; no-snapshot clean cache; no-snapshot empty dirty fallback

## Device gate

Preset `overdub_occupy_paint` (managed capture). First OD2 `draw_src,layers`: `snap=0` and `visual` does not shrink below the OD1 `adopt_partial` count. Preview may grow. The adopted prefix must not reconstruct from the stale snapshot. Also: OD1 `overlap_hold` shorten=0 hide=0; OD2 `overlap_hold` shorten≥2 and hide≥2; at least two half-bar wrap-crossing pairs on 48/52/56/60 (`WRAP` or OD2 `MI,U`); two `Overdub undone` then two `Overdub redone`.
