# Display visual-cache stop compose adopt

**Status:** Implemented — native **1531/1531**, RAM1 locals **6848**. Device PASS [`002145`](../../captures/session_20260901_002145.log) (`88/80→88/88`, `145/144→145/145`).  
**Date:** 2026-09-01  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1m  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Debugging boundary:** 1k wrap compose hold is closed as a grow-vs-prefix hole. Do not adopt a 16-bar window over a long-loop larger cache (6B / `185931` 2403→496). Do not change occupy hide/ledger. Do not start Stage 2.  
**Evidence:** [`000013`](../../captures/session_20260901_000013.log), [`234233`](../../captures/session_20260831_234233.log)

**Does not authorize:** Overlay → false on `tryGather`; Stage 2 snapshot-note copy; occupy `overlap_hold hide`; OpenSpec `3.5` / `3.6`.

---

## Debugging boundary

```text
shouldHoldComposedNotesOnOverdubWrapRevision
    ← closed (1k)
global stop hops tracks and clears liveDisplayNotes (141→31)
    ← this slice
full-loop idle Prepared slice smaller than adopted cache (95→80)
    ← this slice
occupy overlap_hold hide=2
    ← not this slice
```

---

## Problem

[`000013`](../../captures/session_20260901_000013.log) last OVERDUBBING compose **141** / cache **95**. Other tracks STOPPED first (`1162746439` frame **32**). `resolveDisplayNotesCommitted` hop-clears `liveDisplayNotes` (“never paint another loop’s notes”). RC5a `refreshViewportAfterOverdubStop` then has empty compose and keeps cache **95**. Wraps 8–11 lived only on compose; stop drops them. Prepared full-loop idle then `slice_clean` **95→80**.

`overlap_hold hide=2` is occupy-at-stop. This slice does not change occupy.

[`234233`](../../captures/session_20260831_234233.log) stop compose **325/138** then idle Fallback `slice_clean` **176**.

## Invariant

```
short-loop compose > visualCache
    → adopt onto that loop before hop-clear and on wrap-hold / RC5a
long-loop cache > window compose
    → do not adopt (6B)
full-loop idle slice < visualCache
    → slice_keep (do not replace)
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO — O(notes) copy on hop / wrap / stop, not MIDI interval |
| Reuse | YES — `adoptComposedDisplayNotesFromViewport`, `refreshViewportAfterOverdubStop`, hop-clear in `resolveDisplayNotesCommitted`, `rebuildVisualCacheIdleSlice` |

## Implementation

- `shouldAdoptComposedNotesOverVisualCache(composed, cache, loopBars)` — adopt when compose > 0 and (cache empty or `loopBars <= kMaxDetailedWindowBars` and compose > cache).
- `shouldKeepVisualCacheOverSmallerFullLoopSlice(cache, slice)` — full-loop idle (`startBar==0 && endBar+1>=totalBars`) must not replace a larger cache.
- `DisplayManager::adoptComposedDisplayNotesIfAuthoritative` — copy `liveDisplayNotes` when the helper says so.
- Call sites: hop-clear (previous slot/track), `refreshViewportAfterOverdubStop` (not empty-cache-only), wrap-hold in `rebuildOverdubCommittedDisplayLayer`.
- Idle: `slice_keep` before `removeDisplayNotesOverlappingBars`.

## Verification

Native (`test_display_window_utils`): `141>95` 4-bar adopt; `496<2403` 64-bar reject; `95>80` keep.

Device [`000013`](../../captures/session_20260901_000013.log) recapture: first STOPPED on the overdub loop ≥ last OVERDUBBING compose; later PLAYING must not fall to 80; cache must not freeze at 95 while compose grows; no 66-bar `50688/1429` as the settled frame.

## Explicitly out of scope

- Occupy hide / ledger
- Stage 2 snapshot-note copy
- Overlay return
