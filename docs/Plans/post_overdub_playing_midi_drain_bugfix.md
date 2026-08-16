# Post-overdub PLAYING MIDI drain

**Status:** Device PASS for scheduling ceiling — [`034702`](../../captures/session_20260816_034702.log)  
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

## Device [`034702`](../../captures/session_20260816_034702.log)

`clockrate` 47 through PLAYING overdub stops. Repeated post-stop windows:

| After stop | `ODUB,stop` | `idle_maint` | `load_frame` | `persist_save` | **`midi_gap`** |
|------------|------------:|-------------:|-------------:|---------------:|---------------:|
| 69.2 s | 22 ms | 48 ms | 21 ms | 1 | **48 ms** |
| 106.8 s | 28 ms | 59 ms | 21 ms | 1 | **59 ms** |
| 113.2 s | 20 ms | 58 ms | 23 ms | 1 | **58 ms** |
| 116.3 s / 119.1 s | 20 ms | 61 ms | 22 ms | 1 | **61 ms** |
| 140.5 s | 24 ms | 67 ms | 30 ms | 1 | **67 ms** |

`midi_gap` equals `idle_maint`. load_frame and persist are unstacked from the first gap. vs [`031229`](../../captures/session_20260816_031229.log) 99–132 ms.

Later LoopPersist finalize still **78 ms** `persist_save` (148.6 s) — second problem, unchanged.

One PLAYING hitch remains: 64.1 s stop then `midi_gap` **208 ms** at 65.4 s (`idle_maint` 46 ms, `persist_save` 1, `RING,overflow`, BPM 90–99). That gap is not idle+load stacked. STOPPED LCR `6a,nat` 356 ms at 202.4 s (`idle_maint` 396 ms, `clockrate` 0) is the device-gate complete, not this slice.

## Follow-up (this commit)

- Stop dumping `emitArchitectureMetricsSnapshot` on `ODUB,stop,display` — that flood is in the 64.1 s `RING,overflow` window.
- Overdub→PLAYING uses `silenceTrackMidiOutput` (this track’s channel) instead of `sendAllNotesOff` (CC123 on every non-LED channel). Transport stop still uses `sendAllNotesOff`.

Still parked: LoopPersist finalize CRC (RAM1), visual-cache idle grain, LoadLoopJob 915 ms, LCR `6a,nat` while STOPPED.

## Out of scope

Visual-cache implementation, LoopPersist CRC, LoadLoopJob, 6.3, LCR, interval reservation, all-PLAYING drain.

## Tests

- Native: `test_midi_service_drain` — policy booleans only
- Device: same three overdub stops as [`031229`](../../captures/session_20260816_031229.log); compare window maxes
