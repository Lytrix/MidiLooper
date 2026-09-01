# Overdub paint — last snapshot stays after wrap

**Status:** Host shipped. Device recapture pending (`004351` wrap 2: frame must not drop when `snap` goes 0).  
**Date:** 2026-09-02  
**Kind:** bugfix  
**Evidence:** [`004351`](../../captures/session_20260902_004351.log) wrap 2 at 76.701s  
**Does not replace:** [`overdub_paint_closed_preview_suffix_hide_bugfix.md`](overdub_paint_closed_preview_suffix_hide_bugfix.md), DEC-045  
**Does not authorize:** OVERDUBBING reconstruct, Stage 4, changing `sealedCapturePreviewIsCovered`

---

## Debugging boundary

```
sealed / closed preview after committedDisplayEnd
    ← trust
allowVisualCacheReconstructSlice == false
    ← trust; visual stays record-sized (63)
gather uses snapshot only when publishedSnapshotIsCurrent
    ← this slice
```

## Problem

4-bar loop. Wrap 1 at 68.55s (`sealed=35`). Snapshot becomes current at 71.391s (`sealed=0`) — wrap-1 notes are in the snapshot. Frame grows to **135** (`snap=1`, `preview=46`).

Wrap 2 at 76.701s: `snap=0`, `sealed=45`, `preview=2`, `visual=63`, frame **135→109**.

`gatherDrawSourceBars` then reads `visualCache` (record 63) + latest sealed (lap 2) + new preview. Wrap-1 notes exist only in the last published snapshot, which is not current, so they leave the frame until snap-current at 82.283s.

## Invariant

```
if publishedSnapshot() exists, gather that snapshot
visualCache only when no published snapshot exists
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** |

Playback still uses `publishedSnapshotIsCurrent`. `sealedCapturePreviewIsCovered` unchanged.

## Tests

Host: publish wrap-1 `72@400→480` into the snapshot, then wrap 2 without publish. Gather while `!publishedSnapshotIsCurrent()` still contains `72@400→480`.
