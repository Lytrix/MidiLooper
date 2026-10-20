# 6.4 — windowed profile multi-run qualification: measured, **not qualified**

Date: 2026-10-20. Task: 6.4 (windowed HITL profile), qualification half. Branch:
`feature/unified-rolling-playback-path`. Image: lane build
`teensy41-capture-serial-window-qualification`, HEX sha256 `441d5e0e017083c6cb77ca25c9fc26701de9cade5a45b64cae49f063b382a806`,
built and uploaded once at git `46e2be4c` (4.5 landed). **No firmware line changed during this slice**; the host tooling
below changed, so the tree is dirty for the host files only.

Profile: `STIM-WINDOW-CONTENT-01@v1` / preset `windowed_content_parity`, registered target track 8 / slot 4, 24 bars
(`--record-bars 24`, 18 432 ticks), one managed capture per run. No stimulus was edited, no oracle term was relaxed,
and no captured `command.json` was executed.

## Verdict

**6.4 stays open.** Ten runs produced **one** combined green (run 02). Three consecutive green runs never happened, so
the declared transition oracle ("the same image hash on every run, `ok: true` with the same five lane checks, no overrun
under transport, unchanged `deferred`") is **not** met. The runs do establish what blocks qualification, below.

## Host-tooling corrections found by measurement

All in `scripts/hitl/scenarios/long_loop_display_window.py`; the reuse rules (registered runner, registered stimulus,
no hand-rolled MIDI) are unchanged.

1. **The note overlay is entered *and* left with short presses.** The Edit button's own map is short =
   `CYCLE_EDIT_MODE` (`EditManager::cycleEditSession`: `NOTE_EDIT` ↔ `LOOP_EDIT`; from no session it opens
   `NOTE_EDIT`) and long = `EXIT_EDIT_MODE`. The leg's print said "exit NOTE_EDIT" but its gesture was the **long**
   press, i.e. it left edit mode altogether rather than closing the note overlay. Replaced with the short-press cycle
   and its own ack (`Edit session: LOOP_EDIT`), via `_ensure_note_edit_exited`; an unconfirmed exit fails the leg with
   its own cause instead of proceeding. Run 09 confirms on wire:
   `enter NOTE_EDIT attempt 1/2 → NOTE_EDIT enter confirmed → … → exit NOTE_EDIT (short press cycles to LOOP_EDIT) →
   NOTE_EDIT exit confirmed (LOOP_EDIT)`.
2. **The entry re-press is state-aware.** The old retry pressed again after 5 s whenever the ack was late, which cycles
   an *unobserved* `NOTE_EDIT` straight back to `LOOP_EDIT`. Now the re-press happens only when a **fresh**
   `LOOP_EDIT` ack arrived since our own press (`note_edit_repress_needed`). The ambiguous confirm token
   `MIDI Edit Mode: Short press` was removed from the entry set: it is printed for every cycle press, including the one
   that leaves `NOTE_EDIT`.
3. **The record-*stop* retry is state-aware.** Run 09 measured the mirror trap of the start-press fix: the take had
   already completed and returned to `PLAYING`, the lagging log still showed it unfinished, the leg pressed record
   again — and the device answered `Record stop retarget: phase=2 dest=1` → `Transport stopped`. The NOTE_EDIT phases
   then ran with the transport stopped. `record_stop_needs_retry` now presses again **only** while the device's own
   latest state is still `RECORDING`.
4. (From the earlier slice, retained) the record-*start* retry is state-aware (`record_press_needs_retry`).

Host tests: `scripts/test_long_loop_display_serial_verify.py` **19/19** (new: edit-session mode read, re-press predicate,
exit press/ack path, no-op when already `LOOP_EDIT`, refusal when the mode is unknown, no second press on a lagging ack,
stop-retry predicate); neighbours `scripts/test_windowed_content_parity_verify.py` **57/57**.

## Device runs

`leg` = the reused `long_loop_display_window` oracle; `disp` = `DISP` rows present in the leg's own copy; `lane`/`snap`/
`ring` = the combined verifier's windowed-lane, record-snapshot and ring-integrity sections. Boot mode as recorded in each
run's `invocation.json`.

| Run | Boot mode | Leg | disp | Lane | Snap | Ring | Combined |
|---|---|---|---|---|---|---|---|
| 01 | `--build-upload` (post-flash) | red: `missing_playing_bounded_window_disp` | 3 | ok | red | ok | red |
| 01b | default | red (3 issues) | 2 | red | ok | ok | red |
| **02** | default | **ok** | 8 | **ok** | **ok** | **ok** | **ok** |
| 03 | default | ok | 8 | red (`tier_a_evicted=1`) | red | red | red |
| 04 | default | red (3 issues) | 8 | ok | ok | ok | red |
| 05 | `--boot-reset` | red: `missing_playing_bounded_window_disp` | 3 | red (`tier_a_evicted=1`) | red | red | red |
| 06 | default | not run (record press unconfirmed twice) | – | ok | red | ok | red |
| 07 | default | red (3 issues) | 15 | red (`tier_a_evicted=1`) | ok | red | red |
| 08 | default | ok | 8 | – | – | – | **no report (hung)** |
| 09 | `--boot-reset` | red: `missing_playing_bounded_window_disp` | 3 | red (`tier_a_evicted=1`) | red | red | red |
| 10 | default | red (3 issues; drained set has **no** `PLAYING` `DISP` row) | 2 | **ok** | **ok** | **ok** | red (leg only) |

Every run carries the image pin above (`metadata.json` `firmware_hex_sha256`), `oracle_complete: false`, and the unchanged
`deferred` list (`content_parity_across_window_boundary` → content pipeline; `root_demoted_residency_over_loop_lengths` →
the per-slot row sweep). Green lane terms measured under transport, for reference: run 02 receipts **23/23**, coverage
425/462 blocks, boundaries 394; run 10 receipts **24/24**, coverage 216/276, boundaries 206, overrun 0 in all runs.

## Findings

- **F-13 is the qualification blocker, and it is measured now.** The device holds `DISP` rows in its capture buffer and
  flushes them at the transport stop: run 10's session log carries **448** `DISP` rows, the leg's own copy **2**, the
  post-stop drain **446**; run 02 is 795 / 8 / 787. The leg's display half therefore depends on how many rows leak
  *during* the run (2–15 observed), and the **combined verifier's re-derivation from the drained capture does not rescue
  it**: run 10's drained set has 448 `DISP` rows but **none with `state=PLAYING`** (444 `RECORDING`, 4 `STOPPED`), so
  `insufficient_disp_during_note_edit_freeze` stays `count=0` and `missing_playing_bounded_window_disp` stands. Run 02
  had **8** `PLAYING` rows and passed; run 03 had **3**. Run 10 is the sharpest statement of the split: **all six lane
  checks, the snapshot checks and ring integrity are green** while the leg sees no playing-window row at all.
- **Ring eviction is an independent coin flip.** `tier_a_evicted=1` appears in 03, 05, 07 and 09 (never in 02/10). It is a
  truthful refusal — an evicted Tier-A row invalidates absence/count conclusions — not a verifier defect.
- **Run 08 hung after a green leg.** The leg report (`ok: true`, 8 `DISP` rows) was written at 09:35:30; 22 minutes later
  the runner had produced no combined report, no shutdown receipt and no MIDI-cleanup receipt, while the capture kept
  writing the session log. The process was killed by the operator. Cause is **UNKNOWN**; the run is diagnostic only and
  is not counted in the verdict table's green column.
- **Warm-up order matters.** Post-flash (01) and fresh-boot (`--boot-reset` 05/09) runs are not verdict-grade: the boot and
  restore flood occupies the link/ring while the leg's phases run, and the leg reads a log that is still behind.

## What this does not claim

- No parity verdict (F-7) and no residency verdict (F-8): both terms stay deferred to their owners, as the run reports
  state (`judged: nothing` for the residency term).
- No firmware change, no oracle weakening, no stimulus edit, no 3.15f progress.
- 3.7d's device read remains owed and was not reached (no run names `win_blocked_by_request`).

## Next

The remaining blocker is a capture-flush/tier policy (F-13) — firmware-side — or a leg that reads the *drained* rows with
timestamp ordering instead of line position (verifier-side). The 6.4 declaration for this slice forbids both, so either
needs its own scope decision. Until then, qualification repeats as a lottery, and run 10's all-lane-green/leg-only-red
capture (`captures/i5-s64-qual-24bar-10`) is the reference for what the lane itself already proves.
