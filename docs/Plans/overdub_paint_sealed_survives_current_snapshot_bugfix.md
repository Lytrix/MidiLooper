# Overdub paint — sealed survives current snapshot

**Status:** Host shipped. Device recapture [`013709`](../../captures/session_20260902_013709.log) held sealed through six wraps (no bar-2/3 fallback). Next defects are equal-tick reconstruct and source-view pairing, not this owner.  
**Date:** 2026-09-02  
**Kind:** bugfix  
**Evidence:** [`012714`](../../captures/session_20260902_012714.log) wrap 2 at 35.756s / snap-current at 40.645s  
**Does not replace:** [`overdub_paint_stop_snapshot_wrap_end_bugfix.md`](overdub_paint_stop_snapshot_wrap_end_bugfix.md) (after-stop visual authority)  
**Does not authorize:** OVERDUBBING reconstruct, Stage 4, RC2b tails, snapshot pairing rewrite

---

## Debugging boundary

```
storage SEVT linear (no F@0)
    ← trust
086480f9 + session flag: gather snapshot while capture or session
    ← stay
keep-larger visual over snapshot during session
    ← revert (locks previous pass on unread bars)
snap-current releases sealed
    ← this slice
WRAP dump after stop (64 rows)
    ← parked pairing; not this owner
```

## Problem

4-bar loop. Wrap 2 at 35.756s: `sealed=47` `snap=0` frame **127→148** while the playhead is on bars 0–1. At 40.645s the successor snapshot goes current: `sealed=0` `snap=1` frame **129**. No `draw_src,diff` for `bar,2` or `bar,3` after wrap 2. Last two bars fall back to the older snapshot membership.

`sealedCapturePreviewIsCovered` treated `publishedSnapshotIsCurrent()` as loop-wide membership proof. `012714` shows a current snapshot can omit the just-sealed lap.

Keep-larger (`011906` follow-up) then painted adopted visual **129** as the second-overdub prefix (`visual=129` frame **132–139**), so bars 2–3 stayed on the previous pass until the playhead recrossed them.

After first stop: **64** `WRAP` rows, **0** `SEVT,F,0`. Storage is linear; display pairing is wrap-shaped. That is snapshot reconstruct / RC2b, not gather coverage.

## Invariant

```
sealed stays until visualCache.builtFromRevision covers the seal
snap-current is not coverage
union sealed only while capture or overdub session is active
openOverdubSession clears leftover record sealed
after stop, nonempty visualCache is paint authority (no sealed suffix)
do not replace a live-session snapshot gather with a larger visualCache
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — `Loop::sealedCapturePreviewIsCovered`, `gatherDrawSourceBars` |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** |

Reuse: extend those owners. Host twin `gatherDrawSourceDisplayNotes` uses the same predicates.

## Pre-implementation review

### Ready

- `Loop::sealedCapturePreviewIsCovered` in [`LoopCapture.cpp`](../../src/Loop/LoopCapture.cpp)
- `gatherDrawSourceBars` in [`DisplayNoteWindowGather.cpp`](../../src/DisplayManager/DisplayNoteWindowGather.cpp)
- Host twin in `test_loop_take_survival`

### Resolved

| Topic | Decision |
|-------|----------|
| Snap-current coverage | Removed — `012714` 40.645s |
| Keep-larger during session | Reverted |
| After stop | Nonempty `visualCache` (adopt) is paint authority |
| Snapshot pairing / RC2b | Parked — 64 `WRAP` vs 0 `SEVT,F,0` |

### Open before coding

None.

### Proceed?

YES

## Tests

Host: wrap-seal a bar-3 note, publish until `publishedSnapshotIsCurrent()`, leave `visualCache` behind the seal. `sealedCapturePreviewIsCovered()` is false and gather still contains `64@2304→2784`.
