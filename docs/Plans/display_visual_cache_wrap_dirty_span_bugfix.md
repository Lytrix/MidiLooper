# Display visual-cache wrap dirty span

**Status:** Implemented — native **1521/1521**; RAM1 locals **6944**; device recapture [`221925`](../../captures/session_20260831_221925.log) — wrap `dcnt` inconclusive (linear pass hit every bar); first two wraps filled; wrap 3 idle did not `slice_clean`; STOPPED lengthen/hide is not a 1g dirty-span fail  
**Date:** 2026-08-31  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1g  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Debugging boundary:** 1f last-resort Overlay reject is closed (`5628bad4`). Do not reopen 1b–1f paint. Do not start Stage 2. Do not change Overlay return.  
**Evidence:** [`220337`](../../captures/session_20260831_220337.log)

**Does not authorize:** Overlay → false on `tryGather`; assigning dirty `visualCache` as STOPPED paint; `rebuildDisplayNotesInWindow`; OpenSpec `3.5` / `3.6`.

---

## Debugging boundary

```text
resolveDisplayNotesCommitted last-resort Overlay reject
    ← closed (1f). Paint path unchanged.
markTickSpanDirty / markAffectedDisplayCacheRanges
    ← this slice
Idle Overlay reject / Stage 2 snapshot-note copy
    ← unchanged
```

---

## Problem

[`220337`](../../captures/session_20260831_220337.log) wrap and stop emit `VCACHE,stale_range` with `first=0,last=7,total=8,dsz=8,dcnt=8`. Idle then `slice_clean` of the whole loop (`127→206`, `206→276`).

`markTickSpanDirty` treats `endTick < startTick` as a wrap-held span and marks `startBar…last` plus `0…endBar`. A wrap-held pair whose On is in bar 0 (for example On@192 Off@16) fills every remaining bar. Those interior bars are not wrap-edge changes.

Linear On–Off spans stay inclusive. A sealed pass note that actually occupies bar 2 still dirties bar 2 only (`test_overdub_wrap_keeps_clean_cache_notes_and_marks_wrap_span`).

## Invariant

```
wrap-held span → dirty On bar and Off bar only
linear span → dirty inclusive bars the note occupies
```

Wrap must not dirty interior bars that only appear because a wrap-shaped pair was expanded around the loop.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO — same pass walk |
| Reuse | YES — extend `markTickSpanDirty` only |

## Implementation

- `endTick < startTick`: mark `startBar` and `endBar` only.
- `endTick >= startTick`: keep inclusive `startBar…endBar`.
- Per-event `markBarDirty(event.tick)` unchanged.
- Companion row / target-note spans use the same helper.

## Verification

Native (`test_overdub_source_view`):

- Existing wrap note in bar 2 still dirties bar 2 only; bars 0 / 7 / 15 stay clean.
- Wrap-held On in bar 0 / Off in bar 0 does not dirty bars 1–7.
- Wrap-held On in last bar / Off in bar 0 dirties those two bars only.

Device ([`220337`](../../captures/session_20260831_220337.log) shape): wrap `stale_range` `dcnt` is the wrap-edge (and any linear pass bars), not automatically `total`. Idle must not `slice_clean` the whole loop from one wrap-held pair.

Recapture [`221925`](../../captures/session_20260831_221925.log) (4-bar loop, `len=3072`): every wrap `stale_range` is `dcnt=4==total`. That matches linear 39/45/51 hits in every bar, not wrap-held interior fill. Wrap 1 `slice_clean` `63→102`. Wrap 2 `slice_clean` `102→124`. Wrap 3: `idle_src,OverlayRejected` / `src,None` until stop; no `slice_clean`; cache `124` then `128`. First STOPPED `DISP` **128 / 137** (no `paint_src`). Stop `overlap_hold` `hide=8` `add=8`. Stop `DNTE` stacks `39@48` and `39@432` at lengths **144 and 256**. `SEVT` sequential pairs for 39/45/51 include **256** next to **144/160**. `WRAP` dump (ticks 1680–3024) shows 45/51 length **304** and 39 length **192**. MIDI **47 (B2)** is absent. 1g does not own hide or wrap-pair length.

## Explicitly out of scope

- Stage 2 snapshot-note copy
- 1f last-resort paint reconstruct
- Making the snapshot current earlier
- Overlay return / playback
- OpenSpec `3.5` / `3.6`
