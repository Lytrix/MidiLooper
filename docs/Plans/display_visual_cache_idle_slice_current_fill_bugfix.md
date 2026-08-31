# Display visual-cache idle slice current fill

**Status:** Implemented — native **1518/1518**; RAM1 locals **6944**; device recapture required  
**Date:** 2026-08-31  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1e  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Debugging boundary:** wrap-tick visual merge is closed (`d35d64a7`). Do not reopen 1b–1d. Do not start Stage 2 snapshot-note copy. Do not make the snapshot current earlier.  
**Evidence:** [`205917`](../../captures/session_20260831_205917.log)

**Does not authorize:** Overlay → false on `tryGatherPublishedSnapshotEventsInWindow`; wrap-tick visual merge; LCR as a second `visualCache`; cooperative `reconstructDisplayNotes`; all-track idle fan-out; N / pages knobs; OpenSpec `3.5` / `3.6`.

---

## Debugging boundary

```text
wrap-tick appendLastCommittedOverdubPassToSourceView
    ← closed (1d). Source view + span restamp only.
rebuildVisualCacheIdleSlice source selection
    ← this slice
LoopSnapshot publication timing / Stage 2 note copy
    ← unchanged
tryGather Overlay contract (playback)
    ← unchanged
```

---

## Problem

[`205917`](../../captures/session_20260831_205917.log) is not a dirty-bar race. After wrap 2, `slice_clean` is `dsz=0` within ~85 ms and stays clean ~16 s while paint equals cache (`DISP` `243,243,243,243`). The longs are already in `visualCache.notes`.

Idle `rebuildVisualCacheIdleSlice` called `tryGatherPublishedSnapshotEventsInWindow`. That function returns true whenever a published root exists. A stale root (`!publishedSnapshotIsCurrent()`) still returns Overlay: stale membership ∪ unpublished pass events ∪ a companion subset, then `reconstructDisplayNotes` (pairing off). Snapshot record Ons and overdub Ons at the same pitch+start share one Off pool, so reconstruct emits a short pair and a longer sibling. The slice marked those bars clean. Wrap 3 dirtied again and a later source shortened (`266→255`). Snapshot stayed `root_rev=1` while `playback_rev` was 5 / 7 / 9.

Stage 1d only stopped wrap-tick merge. Idle fill is the remaining owner.

## Invariant

```
wrap → dirty → first eligible idle slice → authoritative committed content → clean
```

Only a representation whose event membership is current committed content may clear visual-cache dirty bars. Overlay is valid for playback and is never valid for clearing committed visual-cache dirtiness.

| `tryGather` path | Visual-cache idle |
|---|---|
| `Snapshot` | Accept (current root only) |
| `Overlay` | Discard `flat`; `usedSnapshot=false`; do not reset fallback; continue to prepared LCR or budgeted `gatherCommittedEventsInWindow` |
| `None` | Existing fallback |

Prepared LCR is allowed only through existing `tryResolvePreparedWindow(..., playbackRevision)`. Incomplete budget returns without clearing dirty bars.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO — fallback gather already budgeted |
| Reuse | YES — extend `rebuildVisualCacheIdleSlice` only |

## Implementation

- After `tryGather`, Overlay clears `flat` and leaves `usedSnapshot = false` so the existing success branch does not `resetFallbackGatherSession`.
- `lastVisualCacheIdleGatherSource` / `lastVisualCacheIdleOverlayRejected` record the accepted source and the reject.
- Capture line: `#CAP,...,VCACHE,idle_src,<path>,src,<source>,root_rev,...,playback_rev,...` (`OverlayRejected` when rejected).
- `rebuildVisualCacheFromPasses` and playback `tryGather` Overlay contract unchanged.

## Verification

Native (`test_overdub_source_view`):

| Root | Expected source |
|---|---|
| current Snapshot | `Snapshot` — overlayRejected false |
| stale Overlay | overlayRejected; Fallback or Prepared; cache ends match `gatherCommittedEventsInWindow` + reconstruct (pairing off) |
| no Snapshot | Fallback; overlayRejected false |
| fallback budget incomplete | dirty remains; source `None` |

Device (`205917` shape): after wrap 2 first idle neighborhood, committed == `visualCache` == paint. Must not show `39/45/51 @ 192` length **256**. Wrap 3 must not be the first shorten. `paint == cache` while both are wrong is the dangerous state.

## Explicitly out of scope

- Stage 2 snapshot-note copy
- Making the snapshot current earlier
- Source-view consume / occupy pairing
- Changing Overlay return value for playback
- All-track idle fan-out, N/pages, OpenSpec `3.5` / `3.6`
