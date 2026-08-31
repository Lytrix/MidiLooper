# Display visual-cache wrap compose hold

**Status:** Proposed — not implemented  
**Date:** 2026-08-31  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1k  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Debugging boundary:** 1i wrap/stop first paint is closed (`42b00cde`). 1j playhead tails are paint-only (`26dab0cd`). Do not merge the sealed wrap pass into `visualCache` (1d). Do not start Stage 2.  
**Evidence:** [`230400`](../../captures/session_20260831_230400.log)

**Does not authorize:** Overlay → false on `tryGather`; Stage 2 snapshot-note copy; `openTailCloseTick = L-1`; OpenSpec `3.5` / `3.6`.

---

## Debugging boundary

```text
shouldHoldDirtyOverdubLayerBeforeWindowGather resize(committedCount)
    ← closed (1i) — still drops wrap first paint
last liveDisplayNotes across wrap playbackRevision bump
    ← this slice
Off@0 leftover tail pair / finishOpenNotes
    ← 1l
```

---

## Problem

[`230400`](../../captures/session_20260831_230400.log) wrap first `DISP` drops while cache holds:

| Wrap | Last frame | First after wrap | Cache |
|---|---|---|---|
| 1 | 105 | **66** | 63 |
| 2 | 133 | **104** | 102 |
| 3 | 140 | 130 | 128 |
| 4 | 156 | **136** | 133 |

4-bar loop (`len=3072`): `havePaintWindow` is false. `committedLayerChanged` is true because wrap increments `playbackRevision`. `rebuildOverdubCommittedDisplayLayer` then `resize(liveDisplayCacheCommittedNoteCount_)` — the committed prefix is the pre-wrap record layer. Capture notes that just sealed are no longer on the preview and are not in `visualCache` (1d). First paint is cache-sized plus a few new preview rows.

`preserveComposedNotesOnOverdubWrapInvalidate` exists and is tested; no wrap call site uses it.

1i cannot hold those sealed notes: its hold is committed-prefix only.

## Invariant

```
first OVERDUBBING DISP after wrap → last composed liveDisplayNotes
                                    while visualCache is dirty
idle slice may grow cache past that hold
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO — skip rebuild |
| Reuse | YES — wire `preserveComposedNotesOnOverdubWrapInvalidate`; do not `resize` to committed prefix on wrap revision |

## Proposed implementation

- When wrap bumps `playbackRevision` and `liveDisplayNotes` is nonempty and cache is dirty, do not rebuild the committed layer from cache or source view.
- Keep the last composed vector (committed + the capture rows that just sealed).
- Idle `shouldAssignGrownVisualCacheOverOverdubHold` still assigns when cache grows past the hold.
- 1d stays: wrap tick does not merge the sealed pass into `visualCache`.

## Verification

Device ([`230400`](../../captures/session_20260831_230400.log) shape): wrap first frame ≥ last-before-wrap. Cache may stay at the pre-wrap count until `slice_clean`. Recapture must include `#CAP,...,DIAG,dnte,src,reconstruct` and `src,frame` plus sliced `WRAP` lines.

## Explicitly out of scope

- 1l Off@0 leftover pair
- 1b same-start stack
- Stage 2 snapshot-note copy
- Overlay return
