# Overdub paint capture handoff

**Status:** Host shipped (2026-09-01). RAM1 locals **6880**. Reconstruct stays off during OVERDUBBING. Device recapture open.  
**Date:** 2026-09-01  
**Kind:** bugfix  
**Evidence:** [`234726`](../../captures/session_20260901_234726.log)  
**Does not replace:** [`display_seal_handoff_capture_retention_bugfix.md`](display_seal_handoff_capture_retention_bugfix.md), DEC-045 visual reconstruct gate  
**Does not authorize:** Stage 4, `<16-bar` full-loop reconstruct on paint, OVERDUBBING `rebuildVisualCacheIdleSlice`

---

## Debugging boundary

```
allowVisualCacheReconstructSlice(isOverdubbing) == false
    ← trust; do not restore idle reconstruct
retainCapturePreviewAtSeal / sealedCapturePreviewIsCovered
    ← 150650 release predicate unchanged
resolveDisplayNotesByDrawSource gatherDrawSourceBars
    ← this slice
```

## Problem

[`234726`](../../captures/session_20260901_234726.log): 4-bar overdub, three wraps. `visual` stays **63** until stop `adopt_partial` **95**. `frame` moves (`63→135`). Wrap only emits `VCACHE,stale_range`. Reconstruct is gated.

Non-live bars read snapshot (if current) or that stale cache. Live capture used `selectDrawSourceForBar` only when `barIsLiveCapture`. Closed same-pitch+start preview rows `appendDisplayNoteUniqueById` beside the record row, so the committed bar picture stays the record length until wrap seal upserts.

## Invariant

```
while capture is active, every window bar unions capturePreview
closed / wrap-held preview rows supersede at pitch+start
open holds (endTick == startTick) append
sealed uncovered rows keep the existing upsert
visualCache is not filled on this path
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — `DisplayManager::resolveDisplayNotesByDrawSource` / `gatherDrawSourceBars` still consume `Loop` capture and sealed preview |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — same windowed union, LoadAndDisplay |

Reuse: extend `selectDrawSourceForBar`, `upsertDisplayNoteAtPitchStart`, `gatherDrawSourceBars`. No new Manager.

## Tests

- `test_display_window_utils`: capture-active path is Capture for the whole window; open-hold predicate
- `test_loop_take_survival`: closed same-pitch+start preview replaces the record row with playhead on a later bar; sealed row remains after wrap `beginCapture` before snapshot publish; `visualCache` count unchanged
