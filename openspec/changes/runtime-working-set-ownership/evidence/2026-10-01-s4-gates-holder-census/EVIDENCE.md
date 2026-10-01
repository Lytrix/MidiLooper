# EVIDENCE — S4 gates' device clause: holders rise and fall with activation/deactivation

**Date:** 2026-10-01
**Run:** `captures/s4b8-newimage-01` (session `session_20261001_*.log`), image
`a1f4e4c0d2f43ee354782cc6e4fa56ab4c8e268cb774e60a252f763189b2696c`
**Clause:** the S4 gates line's device requirement — *"A device run showing holder count rising and falling with
activation/deactivation."*
**Why this run:** it is the S4b.8 leg re-run on the current image, so it is a clean run (`capture_integrity:
COMPLETE`, `overflow_free`/`call_site_gate_ok` true, `timing_warnings: []`) with a real activation
(record on a slot), a transfer (a switch inside a finalization window) and a retirement in one session.

## The rows

The working-set holder magnitude is the census's `live`/`blocks`/`chunks_free` (the same surface S4b.8's holder
table used), and the *release* has its own row (`DIAG,ws_release,<slot>,<generation>,<outcome>`):

```
#CAP,611010079,DIAG,ws_release,0,0,released
[622.685] [INFO] [TRACK] Reentry window open slot=0        <- the leg's predecessor stop
[623.139] [INFO] Loop 2: Queued playback switch            <- the switch, inside that window
#CAP,623367300,DIAG,canonical_release,0,1,2304,8           <- that slot's content release
[628.703] [INFO] LoopEnd playback commit track=4 0->1 start=0 len=1536
```

| device time | live | blocks | chunks_free | what the run is doing |
|---|---|---|---|---|
| 596.14 s | 4755 KB | 1143/1254 | 386 | before the leg's activation (the prelude's state) |
| 609.36 s | 4769 KB | 1150/1254 | 385 | the leg's record leg is up |
| **610.36 s** | **5090 KB** | **1160/1254** | **381** | **rising** — the record leg's activation |
| **611.36 s** | **6463 KB** | **1353/1354** | **372** | **+1.37 MB / +193 blocks in one step**, with the `ws_release` row at 611.010 s in the same second |
| 615.36 s | 6498 KB | 1381/1381 | 372 | the take's committed content is held |
| **616.36 s** | **6496 KB** | **1324/1448** | 371 | **falling** — a retirement begins |
| **617.37 s** | **6471 KB** | **1277/1448** | 371 | **−104 blocks** from the peak, held down |
| 619–622 s | 6471 KB | 1276/1448 | 370 | steady at the lower level through the finalization window |
| **623.37 s** | 6472 KB | 1279/1448 | **369** | the switch queues; `canonical_release` at 623.367 s |
| 626.37 s | 6508 KB | 1403/1448 | 366 | the destination's occupancy is taken (the commit is at 628.703 s) |
| 629–633 s | 6505 KB | 1298/1448 | 366 | **settled** at a new level rather than accumulating |

## What is claimed, and what is not

* **Claimed:** in one session the holder magnitude **rises** with activation (609.4 → 611.4 s: `live` 4769 → 6463 KB,
  `blocks` 1150 → 1353), **falls** with deactivation (615.4 → 617.4 s: `live` 6498 → 6471 KB, `blocks` 1381 → 1277),
  stays down through the switch window, and **settles** rather than drifting upward afterwards — with the run's two
  release rows named (`DIAG,ws_release,0,0,released` at 611.010 s, `canonical_release,0,1,2304,8` at 623.367 s).
* **Not claimed:** which *internal* event each census step is — the census is sampled once a second and the run has
  both a prelude and the leg, so the steps are attributed to the phase they sit in (a record leg up, a retirement
  under way, a switch inside its window) and not to a named lease transition beyond the two release rows. The
  `ws_release` row's own fields (`slot=0, generation=0`) are reported verbatim rather than interpreted.
* **Not claimed:** that this closes the S4 gates line. Its text also requires **S4.5's P3 acceptance**, which closes
  with the remaining S4f work; the device clause above is the half this run answers.
