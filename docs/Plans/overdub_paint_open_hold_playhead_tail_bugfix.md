# Overdub paint — open-hold playhead tail

**Status:** Host shipped. Device recapture next (`DRAW_SRC` on).  
**Date:** 2026-09-02  
**Kind:** bugfix  
**Evidence:** [`001647`](../../captures/session_20260902_001647.log) `id 8` `77@336→384` → `77@336→336` then `new,0,0,0`  
**Does not replace:** [`overdub_paint_capture_handoff_bugfix.md`](overdub_paint_capture_handoff_bugfix.md), DEC-045 reconstruct gate  
**Does not authorize:** Stage 4, OVERDUBBING `rebuildVisualCacheIdleSlice`, RC2b `applyCapturePlayheadTails` wrap rewrite

---

## Debugging boundary

```
allowVisualCacheReconstructSlice(isOverdubbing) == false
    ← trust
gatherDrawSourceBars unions capturePreview across the window
    ← trust (handoff)
retainCapturePreviewAtSeal / 150650 release
    ← unchanged
committedDisplayEnd taken after preview union
    ← this slice
```

## Problem

[`001647`](../../captures/session_20260902_001647.log): occupy Hide removes the record visual ID. The live replacement at the same pitch+start is painted `end==start` (zero length) until note-off. `missed_preview` stays 0 after close. Frame does not empty.

`gatherDrawSourceBars` appends the open hold, then `resolveDisplayNotesByDrawSource` sets `committedDisplayEnd = candidate.size()`. `applyCapturePlayheadTails` searches only from that index, so it does not update the zero-length hold already in the prefix. `applyPendingNoteChangesToDisplayNotes` treats the hold as committed and can Hide it by overlap.

Seal handoff already required: union sealed rows **before** `committedDisplayEnd` is taken.

## Invariant

```
committedDisplayEnd is snapshot/visual + sealed only
open holds sit after that index
Hide/Shorten apply only below it
playhead tails update the open hold in place to playhead
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — `resolveDisplayNotesByDrawSource` / `gatherDrawSourceBars` still consume `Loop` preview |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — same windowed gather; SlideOneBar is skipped while capture is active so the prefix stays honest |

Reuse: extend `gatherDrawSourceBars` and the `committedDisplayEnd` mark. No new Manager.

## Tests

Host `test_loop_take_survival`: occupy Hide of `77@336→384` plus open hold at `336`; after prefix Hide + tails at playhead `360`, painted end is `360`, not `336` or `384`. This suite stubs `Loop::applyPendingNoteChangesToDisplayNotes`; the pin applies the prefix Hide locally.
