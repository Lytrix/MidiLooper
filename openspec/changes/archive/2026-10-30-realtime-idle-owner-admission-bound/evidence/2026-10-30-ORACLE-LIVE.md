# Device oracle, live window — 2026-10-30

Second load-bearing attempt, on the no-grain acceptance image. **The live transport window passes the
idle-maint and MIDI-gap targets.** The boot row and the post-stop windowed-lane overrun are recorded
separately; neither is silently dropped.

## Image and run

- Image: `teensy41-capture-serial-no-grain` (`SESSION_CAPTURE_GRAIN=0`, `SESSION_CAPTURE_DROID_QUEUE=0`,
  `SESSION_CAPTURE_LCR=0`), HEX
  `sha256 36160d60529c2650f279baaa2ad2264f548ec39be40b7372072de41a32c987e7`, retained in
  `captures/36160d60/`. Flash: loader 2.3, `Found HalfKay` -> `Programming` -> `Booting`, exit 0. Boot:
  `BOOT,load,ok`, `BOOT,ready,settled=1,tracks=8`, USB `Teensy MIDI`.
- Run: `host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 1 --boot-reset` with
  TEMP variant `STIM-BASE-2-2-01-TEMP-RIAO-HEAVY` and overrides
  `--record-bars 32 --overdub-bars 32 --second-overdub-bars 0 --note-gap-ms 80 --gate-ms 20
  --overdub-step-clocks 6 --overdub-gate-clocks 3`.
- Capture: `captures/riao-oracle-20261030b/session_20261030_091051.log` (1.9 MiB, 34,024 lines).
- Materialized load: **512 record notes + 512 overdub notes**, loop **25,344 ticks (33 bars)**; record
  committed at 86.0 s, overdub interval 94.0-170.0 s. This is the grown-take load the change targets.

## Live-window results

The live window is the transport-running interval (first `ST,Track,...,PLAYING` at 11.86 s; transport
stops at 170.9 s).

| Acceptance | Live-window result |
|---|---|
| `loop_rem,idle_maint` | **0** (the only row is at 9.07 s, before the transport starts) |
| `win_gap_overrun` under transport (`running=1`) | **0** |
| max `midi_gap` during the overdub (94-170 s) | **72.7 ms** (19.2 / 61.8 / 52.8 / 32.2 / 66.8 / 72.7 / 44.1 ms per 5 s block) |
| max `clk` / `extclk_arrival` | 0 / 0 (no external clock in this run) |
| `persist_save` breaches named by step | 12 rows, all named with step + budget; max 62,087 us `work_item` |

The record-start MIDI gaps (375,909 us at 11.6 s and 368,855 us at 21.6 s) are outside the overdub
window; they are the transport-start/record-start host-stream pauses, not the idle-owner turn. The
overdub window's maximum is **72.7 ms**, against the original capture's **332 ms** and 101 late events.
`clk` is 0 because this run has no external MIDI clock source; it is not a claim about the clock path.

## Separately recorded, not accepted here

1. **Boot/load `idle_maint` 135,163 us at 9.07 s** (`track=7 slot=4 phase=0`), before the transport
   starts. This is the boot restore/load lane, not the live overdub turn. Per the owner's decision it is
   a separate load-path finding with its own owner, not an acceptance pass.
2. **One `win_gap_overrun` row at 175.3 s**: `permitted=64, overruns=1, overrunMax=83, running=0`. It is
   after the transport stop (170.9 s) and is not a transport-running overrun. The windowed lane's own
   verifier only fails an overrun raised under transport. The task's literal "zero `win_gap_overrun`" is
   therefore not met by the row; the live-transport criterion is met. Recorded for the owner's ruling.
3. **Base verifier content issues** (`overdub_unmatched_open_notes`, `display_empty_after_overdub_stop`,
   `SEVT` missing). The timing oracle does not claim content acceptance. The display frames themselves
   are not empty (401-407 notes during the overdub; 980 notes after the stop), so the verifier's
   "empty" label is a separate content-check question, not accepted or dismissed here.

## What the run proves

On a 33-bar, 1,024-note grown take with the transport running, the idle-owner lane produced **no
`idle_maint` row** and the overdub window's MIDI gap collapsed from the original 332 ms to **72.7 ms**.
The named persistence steps are visible (report-only, section 6 still owns the split). The boot restore
and the post-stop windowed-lane overrun remain named findings.

## 5.4 measurement (alarm vs residual)

The periodic `DIAG,idle_maint` telemetry (not the 50 ms one-shot row) shows the live-window maxima:
`21.5 ms` at 16.6 s (record), then **46.4 ms** at 96.6 s and **43.1 ms** at 121.7 s during the overdub,
with `overCount` 16/6 against the 8 ms budget. The 46.4 ms turn is surrounded by `snapshot_unit` steps
(~20-40 ms apart), so it is snapshot-construction work, not the idle turn's residual alone. The admitted
child work is not separately timed in this capture, so the residual (`idle_maint` minus child work) is
**not isolatable** from these rows. 5.4 stays open pending per-child timing or a stopped-only sample.
The 50 ms one-shot never fired in the live window.

## 6.1 refined: the over-bar step is a runtime-bundle slice

The 62.1 ms `work_item` row at 109.66 s is immediately preceded by
`PERS,bundle,SlotMeta,587577,61973,241,57,0` and `PERS,work,62039,0,0,SlotMeta,slot:7:7,bundle_done,ok`.
The over-bar slice is therefore a **runtime-bundle slice** (`SlotMeta`), not a loop-payload slice: the
loop payload writes 512-byte quanta (`kCapturePassPayloadWriteQuantumBytes`), while this bundle wrote
587,577 bytes in 241 slices with one **61,973 us** slice and 57 undo snapshots. The split target for 6.2
is the bundle stage that produced that slice (undo snapshot/entry tail or footer CRC), not the loop
payload copy. The two-run evidence is the two captures (`6a84c003…` and `36160d60…`), both naming
`work_item` as the only over-bar step.
