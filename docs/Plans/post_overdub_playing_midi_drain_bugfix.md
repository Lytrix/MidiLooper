# Post-overdub PLAYING MIDI drain

**Status:** Native shipped — device gate open  
**Date:** 2026-08-16  
**Kind:** bugfix (scheduling only)  
**Parent:** [`post_overdub_stop_midi_gap_attribution.md`](post_overdub_stop_midi_gap_attribution.md) **CLOSED**

## Invariant

Immediately after OVERDUBBING→PLAYING, deferred idle/load/persist still run the same work, but MIDI is polled around that work. Not all PLAYING. No visual-cache or persist change.

## Owner

`loop()` RC-C C drain + `Track` arm flag. No new Manager. No interval reservation.

## Architecture checkpoint

1. Ownership change? **NO**
2. State transition change? **NO** — OVERDUBBING→PLAYING unchanged

## Why the existing after-load drain is not enough

`captureActiveForMidiDrain` runs **after** `processDeferredIdleMaintenance` + `runDeferredLoadAndDisplayFrame`. Enabling it alone leaves first `midi_gap` = idle + load (~93 ms).

To unstack the attributed first gap, this slice also polls immediately before and after idle maintenance, only while the post-overdub window is active.

## Window (smallest temporal condition)

```text
PLAYING
  AND armed by stopOverdubbing (or in-edit fold to PLAYING)
  AND (visualCacheDirty OR first idle after arm not yet noted)
```

Not all PLAYING. `stopOverdubbingToStopped` does not arm. New `startOverdubbing` clears the arm.

No USB backlog probe — same always-poll as the capture drain.

## Scheduling-only ceiling

Idle maintenance stays one synchronous ~72–78 ms call. Scheduling can remove load_frame and persist from the **first** `midi_gap`. It cannot make that gap smaller than `idle_maint` without graining `rebuildVisualCacheIdleSlice` (option 2, parked).

| Metric | Current [`031229`](../../captures/session_20260816_031229.log) | This slice target |
|--------|---------------------------------------------------------------:|------------------:|
| `ODUB,stop` | 21–25 ms | same |
| `idle_maint` | 72–78 ms | same |
| `load_frame` | ~21 ms | same |
| **first `midi_gap`** | **99–132 ms** | **≤ idle_maint (~72–78 ms)** |
| `persist_save` | 73–83 ms | unchanged (later event) |

MIDI-scale first gap is the hypothesis; the measured ceiling of this experiment is idle_maint.

## Out of scope

Visual-cache implementation, LoopPersist CRC, LoadLoopJob, 6.3, LCR, interval reservation, all-PLAYING drain.

## Tests

- Native: `test_midi_service_drain` — policy booleans only
- Device: same three overdub stops as [`031229`](../../captures/session_20260816_031229.log); compare window maxes
