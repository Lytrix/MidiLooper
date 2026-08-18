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

### Device [`035822`](../../captures/session_20260816_035822.log)

Track-channel silence **PASS**. 13 overdub→PLAYING; **0** `All Notes Off sent`. The 8 ANO lines are the 150.059 s transport stop (`PLAYING→STOPPED` × 8 tracks). `ODUB,stop,set_state` duration **1.0–1.3 ms** vs [`034702`](../../captures/session_20260816_034702.log) **14.0–14.2 ms**.

Architecture snapshot **gone** (`ARCH,` = 0). `RING,overflow` **not gone** — 12 of 13 overdub stops still overflow at the stop (4–11 ms after the button). The empty 62.739 s stop did not. [`034702`](../../captures/session_20260816_034702.log) also had 12 RING lines and 0 `ARCH,` in the file (overflow ate the dump). Removing the snapshot did not stop overflow.

Clean post-stop windows still sit on the scheduling ceiling (`midi_gap` = `idle_maint`):

| After stop | `midi_gap` | `idle_maint` rem | later `persist_save` rem |
|------------|-----------:|-----------------:|-------------------------:|
| 117.125 s | 69 ms | 69 ms | 69 ms |
| 124.694 s | 72 ms | 72 ms | 72 ms |
| 134.655 s | 78 ms | 78 ms | 70 ms |

`clockrate` **47–48** through PLAYING overdubs.

Not gone:

- **402 ms** `midi_gap` at 140.515 s — owner located below (not the 139.544 s stop).
- **171 ms** `persist_save` at 101.179 s — LoopPersist finalize, parked.
- STOPPED `6a,nat` **358 ms** at 201.942 s (`idle_maint` 389 ms, `midi_gap` 402 ms, `clockrate` 0) — device-gate, not this slice.

### 402 ms owner ([`035822`](../../captures/session_20260816_035822.log) 135.508–140.515)

The 5 s dump at 140.515 s is a window max. The 402 ms sample is **not** the 139.544 s stop.

Short-overdub cluster:

| t | What |
|---|------|
| 134.066–134.655 | overdub 589 ms, then stop. Drain armed. `VCACHE,slice_clean` 134.852 clears the arm. |
| 136.696 | **double-press undo** (`kind=1` OverdubPassAdded). 3 ms inside `handleMidiInput`. Dirties cache, `admitLoopPersist`. Does **not** re-arm PLAYING drain. |
| 136.793–138.698 | LoopPersist slices from that undo (`persist_save` per iteration **≤ 16 ms** in this window). Idle rem 72–78 ms. `load_frame` **90 ms** at 136.972. |
| **137.867–138.277** | **410 ms CAP silence.** Next rem is idle **76 ms**. |
| 138.699–139.544 | next short overdub 845 ms. `begin_capture` 10.4 ms, `manager_done` 31 ms. Persist still slicing (`already_pending` on the 139.544 stop). |

Same 5 s window maxes: `idle_maint` **80 ms**, `load_frame` **90 ms**, `persist_save` **16 ms**, `midi_input` **37 ms**. Those four cannot be the 402 ms.

`midi_gap` is exit→next-enter of `handleMidiInput()`. After undo the PLAYING drain is off, so one `loop()` iteration is: top poll → **untimed prefix** → idle 76 ms → load → persist → end poll. The 410 ms hole is that iteration: ~330 ms with no CAP before idle starts, then the 76 ms idle rem. Sum matches the 402 ms sample.

**Attribution: OPEN — prefix owner unknown.** Next slice is measurement only: [`post_undo_playing_loop_prefix_measurement.md`](post_undo_playing_loop_prefix_measurement.md). Do not re-arm drain. Do not name a prefix child.

Still parked: visual-cache idle grain, LoadLoopJob 915 ms, LCR `6a,nat` while STOPPED. LoopPersist finalize CRC relanded (boot gate). RING flood owner on the stop path is a new question — not the snapshot.

## Out of scope

Visual-cache implementation, LoopPersist CRC, LoadLoopJob, 6.3, LCR, interval reservation, all-PLAYING drain.

## Tests

- Native: `test_midi_service_drain` — policy booleans only
- Device: same three overdub stops as [`031229`](../../captures/session_20260816_031229.log); compare window maxes
