# EVIDENCE — S4b gate's dedicated repeated-switch device run (three switches, one finalization window)

**Date:** 2026-10-01
**Run:** `captures/s4b8-repeated-switches-04` (session `session_20261001_135646.log`)
**Image:** hex sha256 `cdab16f4e5f65988a7383b9fd4633d965c76a77c226c73ce462abfa6e7afde3c` — the S4b.5-onward image,
re-flashed unchanged for this run (build byte-identical before upload)
**Entry point:** `scripts/hitl_s4b8_slot_switch_during_finalization.py` (the S4b.8 runner with its sequence
support), `--track-number 5 --loop-slot 1 --switch-loop-slot 2,3,2 --switch-interval-ms 4500`
**Authorization:** track 5 and all its slots, and upload — granted by the user for this session.

## Why this run exists

The S4b gate's own wording asks for the MIDI-boundary timing evidence *across repeated switches*. The earlier
two-switch evidence came from runs whose subject was something else, so it was **observed but not aimed**. This
run aims it: three switch presses on the predecessor's track, spaced past one loop period (~4 s at 120 BPM,
2-bar loop) so each switch gets its own boundary chance.

## The gesture (from the runner's own ledger, `intervention.jsonl`)

| stage | offset from the stop-button release |
|---|---|
| `stop_button_release_sent` | 0 (arming edge; `delay_s` 0.305) |
| `switch_slot_select_sent` target 2 | +0.432 s |
| `switch_slot_select_sent` target 3 | +5.061 s (4.629 s after the previous press) |
| `switch_slot_select_sent` target 2 | +9.692 s (4.631 s after the previous press) |
| `intervention_finished` | `stream_seen: true, armed: true, sent: true, error: null` |

## The device's rows (verbatim)

```
[67.787] [INFO] MIDI Button A: Stop Overdub
[67.787] [INFO] [TRACK] Reentry window open slot=0
[68.219] [INFO] Button press: Loop 2 (short)
[68.246] [DEBUG] [MIDI] Sent loop length CC feedback: length=0 -> CC=0 (1 bar default)
[68.293] [INFO] Loop 2: Queued playback switch
[72.849] [INFO] Button press: Loop 3 (short)
[72.883] [DEBUG] [MIDI] Sent loop length CC feedback: length=0 -> CC=0 (1 bar default)
[72.915] [INFO] Loop 3: Queued playback switch
[77.536] [INFO] Loop 2: Queued playback switch
[85.484] [INFO] Slot switch hold slot_switch_hold_exceeded track=4 target=1 age=3073 ticks
```

and, for the predecessor, its stop is admitted, bound and released exactly once:

```
#CAP,62682653,DIAG,reentry,admit,829,2,0,0
#CAP,67787863,DIAG,reentry,bound,2788,2,0,0
#CAP,67787911,DIAG,reentry_window_open,0,2788,1536
#CAP,68159786,DIAG,canonical_release,0,33,1536,8
```

## What is claimed

1. **The MIDI-boundary timing gates are green across three repeated switches** — the gate's own wording. From the
   run's report:

   ```
   {"intervals": [{"state": "RECORDING",    "start_ts": 56699756,  "stop_ts": 61399615},
                  {"state": "PLAYING",      "start_ts": 61399838,  "stop_ts": 62682640},
                  {"state": "OVERDUBBING",  "start_ts": 62682640,  "stop_ts": 67787976},
                  {"state": "OVERDUB_STOPPING", "start_ts": 67787976, "stop_ts": 67787995},
                  {"state": "PLAYING",      "start_ts": 67787995,  "stop_ts": 105068004}],
    "overflow_free": true, "call_site_gate_ok": true}
   ```

   `timing_warnings` is the empty list. The last `PLAYING` interval (67.788 s → 105.068 s, 37 s) **contains all
   three presses and the hold report**, so "no active-interval overflow across repeated switches" is measured on
   the switches themselves, not on a neighbouring window.

2. **The S4b.7 bounded hold fires on device, once, in exactly the shape its native fixture describes.** At
   85.484 s — 4.0 bars after the third press's queue tick — the device reports
   `slot_switch_hold_exceeded track=4 target=1 age=3073 ticks` (`kBoundBars = 4` × `TICKS_PER_BAR` 768 = 3072;
   the row's 3073 is the inclusive bound). It is a **report, not a decision**: the pending switch stays queued and
   nothing else moves — the terminal names the pending target at the moment it fires (the third press re-queued
   Loop 2, so `target=1`). This is the terminal's **first device observation** (DEC-104's report half).

3. **Why the switches did not commit here, stated from the rows.** The destinations had **no runtime occupancy**
   in this boot: the press rows themselves say so (`Sent loop length CC feedback: length=0`), because their
   on-demand restores — queued at the presses, `RRQ,4,2,queued` 68.237 s and `RRQ,4,3,queued` 72.869 s — were only
   served at **105.18–105.40 s**, i.e. after the harness's final transport stop at 105.068 s. Every *other*
   coordinate's restore was queued even later (108.3–108.9 s) and committed 110.9–114.8 s, so the service window
   opens when the transport is not running; the code names the same condition
   (`canRunBackgroundLoadLoopNow()` returns false while `sequencerRunning`, or while any track is recording or
   audibly playing — `src/StorageManager/LoadLoopJob.cpp:261`). This run therefore contains **no
   `LoopEnd playback commit` row at all**: the switch correctly stayed queued and the hold correctly reported it,
   which is the "no occupancy ⇒ no commit ⇒ bounded report" path this stage's S4b.6/S4b.7 slices implemented, now
   seen on hardware.

## Holder census (from the run's own `[Memory] ext` rows)

| device time | live | blocks | chunks_free | heap_free | refused |
|---|---|---|---|---|---|
| 41.67 s | 1836 KB | 95/162 | 479 | 168 KB | 0/0B |
| 65.70 s | 3251 KB | 252/278 | 468 | 164 KB | 0/0B |
| 90.71 s | 3298 KB | 269/370 | 465 | 156 KB | 0/0B |
| 114.72 s | 4354 KB | 789/857 | 416 | 56 KB | 0/0B |

The pool never refuses here (`refused=0/0B`), and `chunks_free` falls by 63 across the run — the restores that ran
after the stop consume the chunks, not the switches.

## Not claimed

* **Not a green baseline.** The base recipe's own verdict on this run is `INCONCLUSIVE_CAPTURE
  (SETUP_UNCONFIRMED)` on its documented overdub issues (`overdub_unmatched_open_notes`,
  `overdub_out_of_range_notes`, `display_empty_after_overdub_stop`). What this evidence is, is the switch rows, the
  hold row, the census and the timing gates.
* **The commit path is not re-evidenced here.** The switch-while-finalizing commit remains
  [`../2026-10-01-s4b8-switch-during-finalization/EVIDENCE.md`](../2026-10-01-s4b8-switch-during-finalization/EVIDENCE.md)
  (leg 03, on a state whose destination had occupancy).
* **One attribution is deliberately left open:** why the destinations' restores were not served between the presses
  (68.2 s) and the final stop (105.07 s). The rows support "the service window opens when the transport is not
  running" (which the cited code condition names); they do **not** separate that from the per-slot live-owner gate
  (`queuedRestoreMayBegin` = `noLiveOwnerForSlot`), because no `waiting_slot_owned` row was emitted for these
  coordinates.
* **The run's precondition needed a longer wait than the harness default** (25 s instead of 8 s) on this device
  state; the state that made the difference is recorded in
  [`../2026-10-01-restore-retry-churn/FINDING.md`](../2026-10-01-restore-retry-churn/FINDING.md), which is also
  what kept this run's serial log noisy (~272 retry transitions for another coordinate).
