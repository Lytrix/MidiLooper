# Task 6.4 — the windowed leg's display oracle pairs a phase by device time, and the drained capture decides

**Date:** 2026-10-19 · **Base:** `f49b1305` · **Branch:** `feature/unified-rolling-playback-path` · **Task:** 6.4
(verification half) · **Type:** host tooling + host tests. **No firmware line changed**, no upload, no device run in
this slice. The evidence is re-derived from the captures the 2026-10-19 device leg already recorded.

## The gap this closes

The 2026-10-19 device leg left a named defect in the verification half: the reused leg's display oracle
(`scripts/hitl/verify/display_window.py`) bracketed its phases by **stream position**, while the Tier-A ring
**retains** a row produced under transport and delivers it only after the transport stops. In a drained capture the
retained row therefore lands far below the marker that ends its phase, and position brackets the wrong rows.

Measured on the leg's own evidence (run 2, `captures/i5-s64-windowed-parity-02`):

| Reading | Freeze interval | Rows read into the freeze | Verdict |
|---|---|---|---|
| Stream position (before) | `enter_idx 16335` … `snap_idx 16387` | **0** — the retained rows sit at lines 29 460+ | `insufficient_disp_during_note_edit_freeze count=0` (run 1) or `freeze_disp_sparse:marker_fallback` (run 2) |
| Device time (after) | `533.074 s` … `544.850 s` | **1** — line 29460, device `533101690`, `window_start 4836`, `window_bars 16` | frozen, no fallback |

Both stamp families are the same clock, so time is the correct pairing key: `#CAP,<micros>` and the human
`[<sec>.<ms>]` are both device uptime. `windowed_lane.row_us` already reads both, and `record_snapshot.py` already
imports it — this slice reuses it rather than adding a second reader.

A second, coupled defect: the **combined** verifier (`verify_windowed_content_parity`) read its `leg` component from
the standalone report the runner records **before** the drain, while reading `lane` and `snapshot` from the drained
capture. The component that most needed the retained rows was the one read without them.

## What changed

| File | Change |
|---|---|
| `scripts/hitl/verify/display_window.py` | `_stamp_us` / `_phase_rows` / `_rows_up_to` — a phase's rows are bracketed by **device time** where the markers are stamped, with position kept only for markers that carry no stamp (a plain `entered note edit mode`, or the fixtures). Applied to the freeze interval, both fallback samplings, the long-press snap comparison, and the hold-to-track rows. The now-unused `_rows_after_line` is gone. |
| `scripts/hitl/verify/windowed_lane.py` | `verify_windowed_content_parity` now re-derives `leg` from the lines it is handed (the drained capture) and carries the runner's pre-drain report as `leg_run_report` provenance. No component is read from a narrower window than another. |
| `scripts/test_long_loop_display_serial_verify.py` | `PhasePairingTests` — the retained-row shape (a freeze row delivered below the snap is read into the freeze; a row produced after the snap is not). |
| `scripts/test_windowed_content_parity_verify.py` | The leg-composition contract: the drained capture decides in both directions, the pre-drain report travels as provenance, and no runner report means no `leg_run_report` key. |

## Verification evidence (re-derived, no new device run)

`verify_long_loop_display_window` with the leg's own args (`--record-bars 24`, `hold_track_ms 4000`, the leg's
`phase_markers`) over both recorded captures:

| Capture | Before | After |
|---|---|---|
| `i5-s64-windowed-parity-01` session (20 505 lines) | `ok=False`, `insufficient_disp_during_note_edit_freeze count=0` | **`ok=True`, `issues=[]`, `warnings=[]`** (hold 4 rows, `disp_window_rows`) |
| `i5-s64-windowed-parity-02` session (32 016 lines) | `ok=True`, `warnings=['freeze_disp_sparse:marker_fallback']` | **`ok=True`, `issues=[]`, `warnings=[]`** |

`verify_windowed_content_parity` over the same drained session logs, with the run's own recorded `leg` handed in as
`windowed_leg_run_report`:

| Capture | Recorded pre-drain leg | Drained leg (verdict) | Lane | Snapshot | Combined |
|---|---|---|---|---|---|
| run 1 (case-1 artifact) | `ok=False` — `missing_bounded_window_disp_rows`, `insufficient_disp_during_note_edit_freeze count=0`, `missing_playing_bounded_window_disp` | `ok=True` | `ok=True` | `ok=True` | **`ok=True`, `issues=[]`** |
| run 2 (recorded green) | `ok=True` | `ok=True` | `ok=True` | `ok=True` | `ok=True`, `issues=[]` |

The run-1 case-1 artifact on disk is the one that failed with `leg_exit_code: 2`; its **capture** was sufficient all
along and only the pairing was wrong. That is the point of the slice: no re-run was needed to establish it, and none
was spent.

Host tests:

| Suite | Result |
|---|---|
| `scripts.test_long_loop_display_serial_verify` + `scripts.test_windowed_content_parity_verify` | **60 OK** |
| `scripts.test_windowed_content_parity_verify`, `scripts.test_long_loop_display_serial_verify`, `scripts.test_live_record_display_serial_verify`, `scripts.test_hitl_import_gate`, `scripts.test_hitl_result_contract`, `scripts.test_hitl_report_bundle` | **93 OK** |
| `pio test -e native` (firmware gate, unchanged tree) | **3,330 cases / 7 skipped / 3,323 succeeded** |

## What stays open

* **6.4 itself.** This slice repairs the leg's *reading*; it does not claim F-7 (content parity across a window
  boundary) or F-8 (length-independent root residency. Both remain open, and `oracle_complete` stays `False`.
* **No firmware line, no `DEC-###`.** The device verification the change rests on is unchanged; the tooling that
  reads it is what moved.
* The `hold` check still finds its bracket from host markers that the serial log does not carry, so it falls to the
  last bounded rows after the snap. That is unchanged by this slice and remains the weakest of the leg's three
  claims; a device-side row naming the hold interval would let it be paired by time like the freeze now is.
