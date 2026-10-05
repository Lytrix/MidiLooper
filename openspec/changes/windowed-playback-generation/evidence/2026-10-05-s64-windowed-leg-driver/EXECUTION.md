# Task 6.4 — the windowed leg's driver: the pipeline's own run, plus the lane's checks

Execution packet, written **before** the edits. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), task 6.4's wording, `design.md`
row 145 (*"content verification: fed the windowed/demoted cases (new stimulus profile), not replaced"*) and `S4C45`
(*"F-7/F-8 extend the existing content-verification pipeline; they do not build a second one"*).

## Identity and scope

- **Slice:** task 6.4's **exercise** half, host-tooling only — the driver that makes `STIM-WINDOW-CONTENT-01`
  (`windowed_content_parity`) selectable and read: a channel entry, a preset entry, and the windowed lane's checks
  composed with the **existing** content-verification leg's oracle. No firmware, no build, no upload, no device action.
- **Primary files:** `scripts/hitl/verify/windowed_lane.py` (new), `scripts/hitl/scenarios/windowed_content_parity.py`
  (new), `scripts/hitl/registry.py`, `scripts/test_windowed_content_parity_verify.py` (new),
  `scripts/test_hitl_stimulus_contract.py`, `scripts/test_accepted_sequence_scenario.py`.
- **Non-goals:** no new gesture or driver logic; no second content pipeline; no F-7/F-8 qualification claim; the
  residency row set is not guessed; no device run.

## The reuse decision, and why it is the required one

F-7/F-8 are *"an extension of the existing content-verification pipeline, not a second one"*, and the repo already has a
leg whose **shape** is exactly the windowed case: `run_long_loop_display_window` records a loop far longer than the
served two-bar window (`--record-bars`, default 24, and it refuses `<= 16` itself), keeps the transport running across
the boundaries, and has a host-tested verifier. So the windowed profile's `run` **is** that leg's run, unchanged, and its
`verify` is that leg's oracle **plus** the windowed lane's checks. A new driver would have been a second pipeline under a
different name.

## Oracle fixed before behaviour edits

Rows are read in the formats the emitters write (verified in `src/Utils/DebugSessionCapture.cpp`, not from memory):

| Row | Fields used |
|---|---|
| `#CAP,<us>,DIAG,win_readiness,…` | 4 track, 5 slot, 6 readiness, 7 holder, 8 revision, 9 leadTicks |
| `#CAP,<us>,DIAG,win_gap,…` | 4..19; **16** `permitted`, **17** `overruns`, **19** the literal schema `3` |
| `#CAP,<us>,DIAG,win_gap_overrun,…` | 4 permitted, 5 overruns, 6 overrunMax, 7 running, 8 epoch |
| `#CAP,<us>,DIAG,playback_build,<path>,…` | 4 path, 6 loopLen, 8 revision, 9 track, 10 slot |

"Under transport" is read from the **stream's order** (a row between a `Transport started` and the following
`Transport stopped`), not from device time: the two row families do not share a unit, and ordering is what the claim is
about.

| Judged check | Term it answers | Fails when |
|---|---|---|
| `readiness_receipts_under_transport` | `readiness_receipts_under_transport` (AC-8) | no receipt inside a running interval (a post-stop arrival is what the direct-when-USB-has-room path exists to avoid) |
| `windowed_service_coverage_under_transport` | `windowed_service_coverage_under_transport` | no service block with `running=1` **and** opportunities > 0 inside a running interval |
| `no_windowed_overrun_under_transport` | the same term's named failure | a `win_gap_overrun` row with `running=1` inside a running interval |
| `window_boundaries_crossed_under_transport` | `content_parity_across_window_boundary` (its precondition) | fewer than two `build_complete` publications inside running intervals |

**Deferred, named with the surface that owns the verdict** (never restated): `content_parity_across_window_boundary` →
the existing content pipeline; `root_demoted_residency_over_loop_lengths` → **unidentified row set** (the
library-observation path is the candidate), recorded as unknown rather than guessed. The result carries
`oracle_complete: False` always, so a green run cannot be read as F-7/F-8 qualification.

## Controls and coverage

- The verifier is **pure** over capture lines: `running_intervals`, `readiness_rows`, `gap_rows`, `overrun_rows`,
  `build_rows`, `verify_windowed_lane`, `verify_windowed_content_parity`.
- **Red-proofs planned:** MR-6.4c makes any receipt count (dropping the under-transport bound); MR-6.4d stops the named
  overrun from failing. Each must fail exactly its own test and be restored byte-identically.
- **Guard updates, deliberate:** the stimulus contract pin's absence clause inverts (the driver now exists — that is the
  reminder doing its job), and `test_accepted_sequence_scenario`'s exact-preset guard gains the new preset explicitly,
  so the set cannot drift unnoticed.

## Verification recipe

- Host: the new suite plus every suite that imports the registry/scenarios, run from `scripts/` with `PYTHONPATH=.`.
- `pio test -e native` (C++/firmware untouched); `openspec validate windowed-playback-generation --strict`;
  `git diff --check`.
- **Device: NOT RUN.** No upload requested or performed; the leg's device behaviour is unexercised, so the first
  authorized run may require correcting this oracle — recorded, not smoothed.

## Completion record

- [x] Driver registered: `windowed_content_parity` in the channel registry, its preset resolving to
  `["windowed_content_parity"]`, and the run **reused** from the existing long-loop leg (no second pipeline).
- [x] Lane checks implemented and pinned (`scripts/test_windowed_content_parity_verify.py`, 8 tests); **MR-6.4c** (any
  receipt counts) and **MR-6.4d** (the named overrun no longer fails) each red-prove exactly their own test, with the
  file restored byte-identically (`sha256 f92f043fc671e426…`). One correction the pin forced is recorded: the `win_gap`
  field map (declaration fields 16/17, literal schema `3` at 19).
- [x] Guard updates, both deliberate: the stimulus pin's absence clause **inverted** (the reminder fired, as designed),
  and the exact-preset guard now names the new preset.
- [x] Host suites green (8 / 26 / 24 / 3 / 6 / 40 / 39 / 9 / 2 / 45 tests); native **2796 / 2789 succeeded / 7 skipped /
  0 failed**; strict validation valid; `git diff --check` clean; `src include` diff empty.
- [x] Docs: `EVIDENCE.md` here; task record updated.
- [x] Task checkoff: **6.4 stays open** — the leg is selectable and read off-device, but it has not been **exercised on a
  device**, and its first run may correct this oracle.
