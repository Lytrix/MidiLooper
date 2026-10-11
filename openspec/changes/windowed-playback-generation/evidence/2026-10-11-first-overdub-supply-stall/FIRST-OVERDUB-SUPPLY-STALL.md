# The first overdub's playback supply stalls ~2.8 s and then bursts the backlog

**Measured, device-side, on the S7.5a base leg — no fix attempted, no task checkoff.** The owner
reported that during the **first overdub** the device looked stalled (nothing new on the display) and
then "burst" the notes. That is reproducible, it is in the device's own rows, and it is present in a
leg that ran **before** the run-end transport-gesture change, so it is unrelated to that change. This
packet records the measurement and the owner rows it comes from; it does **not** claim a root cause.

Owner change, report and measurement context: the run-end `Stop` work of
[`2026-10-11-s75a-continuity-envelope/HOST-GRID-RECORD-PHASE.md`](../../../runtime-working-set-ownership/evidence/2026-10-11-s75a-continuity-envelope/HOST-GRID-RECORD-PHASE.md)
§3c (its §3b/§3c legs are the same preset this stall was seen on).

## 1. What was seen

While the base preset played its first overdub, the display showed no new notes for over a second and
then jumped. The device's outbound MIDI tells the same story in numbers:

| Reading (from the device's rows) | leg 05 `…-t8s4-05` | leg 03 `…-t8s4-03` | leg 01 `…-t8s4-01` (**before** the gesture change) |
|---|---|---|---|
| last outbound row before the gap | tick **720** @ 28.545 s | tick 720 @ 28.535 s | tick 720 @ 28.614 s |
| silence before the stream resumes | **2837 ms** | 2769 ms | **1446 ms** |
| musical content across the gap | **48 ticks (1 grid step)** | 48 ticks | 48 ticks |
| first row after the gap | tick **768** @ 31.382 s | tick 768 @ 31.304 s | tick 768 @ 30.060 s |
| that row's own burst | **46 rows in 30 ms**, ticks 768 → 1488 | 46 rows / 30 ms, 768 → 1488 | 46 rows / 30 ms, 768 → 1488 |
| display note count across the gap | 32 → 33 (frozen), then **47** | 32 → 33 → 34, then 47 | 32 → 33, then 40 → 43, 49+ |
| second overdub (same run), for control | first row **+6 ms**, **0** `win_readiness` rows | — | — |

So nothing musical is *missing*: the stream resumes at the very next grid step (720 → 768) and then
burns 15 grid steps of content (720 ticks) in ~30 ms before returning to real time. What is lost is
**wall time** — 1.4 s (leg 01), 2.8 s (legs 03/05) — because the loop's supply falls behind and then
dumps the accumulated steps at once. The owner's "burst afterwards" is exactly that 46-row / 30 ms
flush, and the display, which advances with the supplied notes, shows the same step: it sits on the
same count for over a second and then jumps by seven or more.

## 2. The legs this was measured on

Canonical entry point, authorized slot, **no upload and no flash**, single track:

```
host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 \
    --boot-reset --capture-boot-wait 30 --clock-source host --tempo-bpm 300 \
    --stop-after-overdub --out-dir captures/host-grid-record-t8s4-05
```

`…-t8s4-05` also re-confirms the run-end gesture change on a fresh run:
`run_end_transport_stop=host_realtime_stop`, `transport_press_count=1` (only
`_prepare_deferred_save_before_clear`'s setup press), record `1536/1536` (`raw 1536`),
`record_grid_first_note_tick=0`, shutdown `complete/2/ok`, no latch. Verdict stays
`INCONCLUSIVE_CAPTURE (EVIDENCE_MISSING)` on its pre-existing
`persistence_active_snapshot_unavailable_during_save` residual.

## 3. What kept running *inside* the 2837 ms gap (leg 05, 28.545 → 31.382 s)

This is the part that makes it a supply stall rather than a hang: the device was busy the whole time.

| Row family | rows in the 2837 ms gap |
|---|---|
| `DIAG,win_gap` (window service) | **206** (≈ one every **13.8 ms**) |
| `PERS,…` (persistence) | 89 |
| `DIAG,snapshot_unit` | 79 |
| `LED,0` / `LED,1` | 81 / 66 |
| `MI,U` (USB MIDI in) | 37 |
| `DIAG,stop_grain` | 35 |
| `DIAG,lcr` | 28 |
| `COORD,abs` | 23 |
| `DIAG,win_readiness` | **13** |
| `DISP,3,…` | **2** (30.023 s count 32, 30.272 s count 33) |
| `DFRAME` | **2** (29.023 s, 31.063 s) — a healthy device emits one every ~1.5 s |
| `MO,…` (MIDI out) | **0** |

and the firmware's own log lines in the same 3 s window are 41, including the host's gesture being
served (`Record pass undo reset for slot 3: removed=4` 29.210 s, `Button press: Record/Overdub
(short)` 29.936 s, `Live Overdub`, `Overdub session opened: events=0, undo_entries=2` 30.014 s).

The outbound stream is the *only* thing that stopped — capture telemetry, the LED path, USB input and
the window service all kept producing rows. The display frame monitor also slowed to 2 rows in 2.8 s,
which is why the screen looked frozen.

## 4. The resume, and the rows that bracket it

```
#CAP,30048000,DIAG,win_readiness,7,3,owes_currency,window_rebuild,1,912      (then 13 such rows, ~200 ms apart)
#CAP,29937000,DIAG,lcr,srcskip,at=copy,reason=not_finished,fin=0,kept=0,pass=0,rev=1,prep=0,live=1536,sess=0,spans=0
#CAP,30033000,DIAG,win_gap_overrun,64,1,96,1,7
#CAP,31382000,DIAG,playback_order,1442,96
#CAP,31382000,MO,128,8,63,0,768        <- first output after the gap (the burst's first row)
#CAP,31509000,DIAG,playback_build,snapshot,1448,0,1536,1,7,3,1,31379082,760,368,96
#CAP,31830000,DIAG,late_route,144,8,0,0,54,0      <- notes that did leave: max 0 µs
#CAP,31830000,DIAG,env_dist,late_on,0,255,0,255,54
```

Owners, so the next reader starts at the right place:

* `DIAG,win_readiness` — `src/Utils/DebugSessionCapture.cpp` reporting the playback-window owner
  (`src/Track/TrackPlaybackWindowBuild.cpp`), whose state name `owes_currency`
  (`PlaybackWindowReadiness::OwesCurrency`) and action `window_rebuild` say the window owed currency
  and was being rebuilt. It is retried on a ~200 ms cadence for the whole gap.
* `DIAG,lcr,srcskip,at=copy,reason=not_finished` — `src/LoopContentResolution.cpp`: the prepared
  source copy was skipped because the source was **not finished** at that moment.
* `DIAG,win_gap` / `DIAG,win_gap_overrun,64,1,96,1,7` — the window service's own cadence and an
  overrun against the permitted advance (the same `64`-tick term the envelope work cites).
* `DIAG,playback_build,snapshot,…` — the snapshot playback build completing at 31.509 s, i.e. just
  after the burst starts.
* `DIAG,late_route,144,8,0,0,54,0` — 54 note-ons with **max 0 µs** route lateness: the MIDI route was
  ready; whatever was late is **upstream** of the route, which is why this stall does not show up in
  the route-lateness metrics.

## 5. Second symptom, folded in: the output goes silent while the loop keeps ticking

After an *interrupted* run left the board playing, a read-only 8 s attach (`hitl_boot_census.py
--no-reset --settle-s 8`, `captures/host-grid-record-t8s4-04-census/`) found the firmware **alive**
but its output stream silent:

* alive: `[Memory] ext,…` once per second and every query answered (`#MEM_OWNERS`,
  `#CAP,…,DIAG,envelope_instr,…`);
* silent: **zero** `DFRAME`/`DISP`/`#CAP` rows in the whole window; `boot_banner=false`,
  `checkpoint_reached=false`, `walk_rows=0`;
* the screen was dark, which is the same reading seen from the other side.

A soft `!REBOOT` (the managed runner's `--boot-reset`; no upload, no flash) restored it immediately —
`BOOT,scan,done` / `BOOT,load,ok`, then `DFRAME` every ~2 s and `DISP` counts live again. This is the
class already recorded for the capture ring ("rows held until a new attach"); here it took the display
frames with it. It is a *different* observation from §1–§4: in the §1 gap the telemetry kept flowing.

## 6. Lead, stated as a lead

The measured shape — 2.8 s with 206 window-service rows, 13 `owes_currency` retries, 89 persistence
rows, 35 stop-grain rows, and no output, ending exactly when `playback_build,snapshot` lands — points
at the **window supply for a sounding slot that is simultaneously carrying the previous take's
finalization/persistence**, i.e. the `TrackPlaybackWindowBuild` currency/build path, not the MIDI
route and not the host. What would confirm it, in order of cost: (a) read the window owner's state
transitions for that interval (why `owes_currency` persists for 13 retries); (b) a native fixture that
holds a sounding slot's window behind a slow/undone prepared source and asserts the served interval is
*not* starved (the 3.15a one-reader oracle's shape); (c) an A/B on whether the previous take's
deferred save is in flight during the gap.

## 7. Not claimed

No root cause, no fix, no firmware line, no build/upload/flash, no SD action, no limit/envelope
statement, no `DEC-###`, no task checkoff: **3.15a–f, 3.7b, 3.10 and S7.5a/S7.5b/S7.5 stay
unchecked**. Nothing here reclassifies the S7.5a host-grid work: the run-end gesture change is
unaffected by this stall (it is before it in the run), and the legs' verdicts stand as recorded.

## Appendix — exact rows quoted above (leg 05, `captures/host-grid-record-t8s4-05/session_20261011_144802.log`)

```
#CAP,28545000,MO,176,8,74,7,720                      last row before the gap
#CAP,31382000,MO,128,8,63,0,768                      first row after the gap (burst)
#CAP,32349000,MO,176,8,74,115,1296                   last row of the overdub window
#CAP,30048000,DIAG,win_readiness,7,3,owes_currency,window_rebuild,1,912
#CAP,30251000,DIAG,win_readiness,7,3,owes_currency,window_rebuild,1,720
#CAP,30462000,DIAG,win_readiness,7,3,owes_currency,window_rebuild,1,520
#CAP,29937000,DIAG,lcr,srcskip,at=copy,reason=not_finished,fin=0,kept=0,pass=0,rev=1,prep=0,live=1536,sess=0,spans=0
#CAP,30033000,DIAG,win_gap_overrun,64,1,96,1,7
#CAP,31382000,DIAG,playback_order,1442,96
#CAP,31509000,DIAG,playback_build,snapshot,1448,0,1536,1,7,3,1,31379082,760,368,96
#CAP,31512000,DIAG,playback_build,incremental,0,0,1536,2,7,3,1,0,4294967295,0,0
#CAP,31830000,DIAG,late_route,144,8,0,0,54,0
#CAP,31830000,DIAG,env_dist,late_on,0,255,0,255,54
#CAP,31830000,DIAG,env_dist,midi_gap,0,255,1024,1279,15680
#CAP,30023000,DISP,3,OVERDUBBING,1536,32,0,32,32,1,0,2,32
#CAP,30272000,DISP,3,OVERDUBBING,1536,33,0,33,33,1,0,2,33
#CAP,31582000,DISP,3,OVERDUBBING,1536,47,0,47,47,1,0,2,47
#CAP,41000000,MO,128,8,55,0,384                      second overdub, first row (+6 ms, no gap)
```

Measurement helper used for the window/bucket/gap arithmetic (scratch, not committed):
`analyze_overdub_stall.py` — reads a session log and prints the transport-state windows, per-250 ms
`MO` row counts, inter-emission gaps, display rows and the DIAG families inside the window.
