# Overdub paint — sealed rows stay out of occupy Hide

**Status:** Host shipped. Device recapture pending (`003558` shape: 5-bar, OD start ~624, multiple wraps).  
**Date:** 2026-09-02  
**Kind:** bugfix  
**Evidence:** [`003558`](../../captures/session_20260902_003558.log) wrap 2/3  
**Does not replace:** [`overdub_paint_open_hold_playhead_tail_bugfix.md`](overdub_paint_open_hold_playhead_tail_bugfix.md), DEC-045  
**Does not authorize:** `allowWrapContinuation` during overdub (RC2b), Stage 4, reconstruct

---

## Debugging boundary

```
open-hold tails update from committedDisplayEnd
    ← trust (001647 / this recapture lap 1 grew 74→165)
allowVisualCacheReconstructSlice == false
    ← trust
sealed union before committedDisplayEnd
    ← this slice
```

## Problem

[`003558`](../../captures/session_20260902_003558.log): 5-bar loop (`3840`), overdub start phase **624**, wraps at 28.465 / 38.466 / 48.468.

| Time | sealed | preview | snap | frame | visual+sealed+preview |
|------|--------|---------|------|-------|------------------------|
| 38.641 wrap 2 | 59 | 2 | 0 | **136** | 75+59+2 = 136 |
| 43.516 | 59 | 31 | 0 | **115** | 165 |
| 48.530 wrap 3 | 59 | 1 | 0 | **135** | 135 |
| 55.507 | 59 | 43 | 0 | **93** | 177 |

`sealed=59` stays on the loop. Painted extras fall from 61 to ~9. Hide/Shorten run on the prefix that includes sealed.

## Invariant

```
committedDisplayEnd is snapshot/visual only
sealed and capturePreview sit after it
occupy Hide/Shorten apply only below that index
sealed upsert/replace only in the suffix
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** |

Reuse: `gatherDrawSourceBars` mark + suffix upsert. Companion overlay Hide still targets the visual prefix (`201956`).

## Tests

Host: `test_draw_source_sealed_row_survives_prefix_hide` — after wrap seal, `committedEnd` excludes the sealed row; prefix Hide of the record leaves `64@768→900` in the frame.

`test_loop_take_survival` still has the parked RC2b pin `test_same_pass_playhead_tails_keep_closed_bar_four_end` (`2976` vs `2296`). Do not patch `applyCapturePlayheadTails` from that red.

Device: after wrap 2, `frame ≈ visual + sealed + preview` must hold as preview grows (sealed contribution stays until snap-current release). Light `DRAW_SRC` stays on.

## Device recapture

Same shape as [`003558`](../../captures/session_20260902_003558.log): 5-bar loop, overdub start near tick 624, at least two wraps in one OVERDUBBING. Do not treat wrap-held `allowWrapContinuation=false` tails as this slice.
