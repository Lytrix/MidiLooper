# Display visual-cache wrap compose hold

**Status:** Implemented — device wrap 2–8 PASS, wrap 1 FAIL [`232910`](../../captures/session_20260831_232910.log)  
**Date:** 2026-08-31  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1k  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Debugging boundary:** 1i wrap/stop first paint is closed (`42b00cde`). 1j playhead tails are paint-only (`26dab0cd`). Do not merge the sealed wrap pass into `visualCache` (1d). Do not start Stage 2.  
**Evidence:** [`230400`](../../captures/session_20260831_230400.log), [`231953`](../../captures/session_20260831_231953.log)

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

[`231953`](../../captures/session_20260831_231953.log) wrap first `DISP` drops while cache holds:

| Wrap | Last before | First after | Cache |
|---|---|---|---|
| 1 | 109 / 63 | **65 / 63** | 63 |
| 2 | 118 / 107 | **110 / 107** | 107 |
| 3 | 127 / 113 | **115 / 113** | 113 |
| 4 | 133 / 123 | **125 / 123** | 123 |
| 5 | 147 / 130 | **131 / 130** | 130 |
| 6 | 156 / 146 | **147 / 146** | 146 |

4-bar loop (`len=3072`): `havePaintWindow` is false. `committedLayerChanged` is true because wrap increments `playbackRevision`. `rebuildOverdubCommittedDisplayLayer` then `resize(liveDisplayCacheCommittedNoteCount_)` — the committed prefix is the pre-wrap record layer. Capture notes that just sealed are no longer on the preview and are not in `visualCache` (1d). First paint is cache-sized plus a few new preview rows.

`preserveComposedNotesOnOverdubWrapInvalidate` existed and was tested; no wrap call site used it.

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
| Reuse | YES — wire `preserveComposedNotesOnOverdubWrapInvalidate` via `shouldHoldComposedNotesOnOverdubWrapRevision`; do not `resize` to committed prefix on wrap revision |

## Implementation

- `shouldHoldComposedNotesOnOverdubWrapRevision`: wrap revision + dirty cache + nonempty compose + not grown.
- `rebuildOverdubCommittedDisplayLayer`: idle grow still assigns first. Wrap hold keeps `liveDisplayNotes` and promotes `committedCount` to that size so `replaceCaptureLayer` does not strip sealed rows.
- 1d stays: wrap tick does not merge the sealed pass into `visualCache`.

## Verification

Native (`test_display_window_utils`): wrap-revision hold predicate. RAM1 locals **6848**.

Device [`232910`](../../captures/session_20260831_232910.log): wraps 2–8 first `DISP` ≥ last-before-wrap. Wrap 1 `104/63` → `92/89`: `append_resume` notes **89** before first paint; `shouldAssignGrownVisualCacheOverOverdubHold(89, committedCount=63)` assigns cache and drops compose **104**. Next frame `104/101`. Grow must compare to the composed hold, not the pre-wrap prefix.

## Explicitly out of scope

- 1l Off@0 leftover pair
- 1b same-start stack
- Stage 2 snapshot-note copy
- Overlay return
