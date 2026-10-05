# Task 6.4 — the windowed leg's driver: registered, selectable, and exercised only off-device

Evidence for `EXECUTION.md` in this directory. This is task 6.4's **exercise** half, host-tooling only: the profile
`STIM-WINDOW-CONTENT-01` (`windowed_content_parity`) can now be selected by its own preset and read by a verifier, and
the driver is the **existing** content-verification leg rather than a second pipeline — as `design.md` and `S4C45`
require. No firmware, no build, no upload, no device action.

## The driver, and why it is not new code

`run_windowed_content_parity` delegates to `run_long_loop_display_window`, unchanged: that leg already records a loop
far longer than the served two-bar window (`--record-bars`, default 24; the run itself refuses `<= 16`) and keeps the
transport running across the boundaries, which is the only way a boundary can be crossed. Its verifier is likewise
reused: `verify_windowed_content_parity` = that leg's oracle **and** the windowed lane's checks, so a run where either
half fails is not a pass.

```
$ python -c "from hitl.registry import resolve_scenarios; print(resolve_scenarios(preset='windowed_content_parity', scenario_ids=None))"
['windowed_content_parity']
$ python scripts/host_midi_hitl.py stimulus list | grep WINDOW
STIM-WINDOW-CONTENT-01@v1  windowed_content_parity  scenario:windowed_content_parity  targets=track 8 slots 4,5  …
```

## What the checks judge, and what they refuse to

`hitl/verify/windowed_lane.py` is pure over capture lines. Rows are read in the emitters' own formats
(`src/Utils/DebugSessionCapture.cpp`), and "under transport" is read from the **stream's order** — a row between a
`Transport started` and the following `Transport stopped` — because the lane's rows and the transport's human rows do
not share a unit and the ordering is what the claim is about.

| Judged check | Fails when |
|---|---|
| `readiness_receipts_under_transport` | no `win_readiness` row inside a running interval — a receipt that only arrives after the stop is exactly what the direct-when-USB-has-room path exists to avoid (AC-8) |
| `windowed_service_coverage_under_transport` | no `win_gap` block with `running=1` **and** opportunities > 0 inside a running interval |
| `no_windowed_overrun_under_transport` | a `win_gap_overrun` row with `running=1` inside a running interval (the **named** exceedance is a failure, not a number to smooth over) |
| `window_boundaries_crossed_under_transport` | fewer than two `build_complete` publications inside running intervals |

**Deferred, each with the surface to ask:** `content_parity_across_window_boundary` → the existing content pipeline
(`content_deduction.py`, `mixed_content_report.py`); `root_demoted_residency_over_loop_lengths` → **unidentified row
set** (the library-observation path is the candidate) and recorded as unknown rather than guessed. Every result carries
`oracle_complete: False`, so a green run is never read as F-7/F-8 qualification.

## The pin, and its red-proofs

`scripts/test_windowed_content_parity_verify.py` (new, 8 tests) covers: the transport intervals from stream order; the
parsers' field positions; a healthy lane judging four checks and naming two deferred terms; a receipt only after the
stop (fails); an overrun under transport (fails) and the same row after the stop (does not); coverage blocks with no
opportunities (fail); a single publication (not a boundary crossing); and that the profile verifier requires **both**
halves (lane-healthy/leg-failing and lane-only both fail, with the failing half named).

| ID | Change (host tooling) | Result |
|---|---|---|
| **MR-6.4c** | any receipt counts, dropping the under-transport bound | `test_a_receipt_only_after_the_transport_stopped_fails` fails; the other 7 stay green |
| **MR-6.4d** | the named overrun no longer fails the leg | `test_an_overrun_raised_under_transport_fails_by_name` fails; the other 7 stay green |

`scripts/hitl/verify/windowed_lane.py` was restored **byte-identically** after each (`sha256 f92f043fc671e426…` before
and after).

**One correction the pin forced, recorded rather than hidden:** the first parser read `permitted`/`overruns` one field
late (`win_gap`'s declaration fields are 16/17 with the literal schema `3` at 19). The field-position test caught it
immediately, and the corrected map is what the packet's oracle table now states.

**Two guard updates, both deliberate:**
- The stimulus pin's absence clause **inverted**: `windowed_content_parity` is now in `list_scenarios()` and resolves
  from its preset. That clause existed to turn red when the driver landed — it did, and it is inverted rather than
  deleted, so a *removed* driver fails there too.
- `test_accepted_sequence_scenario`'s exact-preset guard failed on the new preset and now names it explicitly, so the
  preset set cannot drift unnoticed. That failure was the guard doing its job, not a defect.

## Verification

Host suites (from `scripts/` with `PYTHONPATH=.`):

| Suite | Result |
|---|---|
| `test_windowed_content_parity_verify` (new) | 8 tests, OK |
| `test_hitl_stimulus_contract` | 26 tests, OK |
| `test_hitl_stimulus_authorization` | 24 tests, OK |
| `test_stimulus_yaml_view` | 3 tests, OK |
| `test_long_loop_display_serial_verify` (the reused leg) | 6 tests, OK |
| `test_accepted_sequence_scenario` | 40 tests, OK |
| `test_clear_before_record_precondition` | 39 tests, OK |
| `test_edit_mode_precondition` | 9 tests, OK |
| `test_collapse_legacy_cli_args` | 2 tests, OK |
| `test_note_edit_overdub_compose_serial_verify` | 45 tests, OK |

- `pio test -e native` — **2796 cases / 2789 succeeded / 7 skipped / 0 failed** (the C++/firmware tree is untouched).
- `openspec validate windowed-playback-generation --strict` — valid. `git diff --check` — clean, and
  `git diff --stat -- src include` is empty.
- **Device: NOT RUN.** No upload was requested or performed.

## Not claimed

- That the leg has **run on a device**: the lane's device-side inventory at a window boundary has never been observed,
  so the checks are written from the emitters and pinned on synthetic rows. The **first authorized run may require
  correcting this oracle**, and that is stated here rather than discovered later.
- F-7 or F-8 qualification, or any part of AC-8 beyond "a receipt arrived while the transport ran".
- The residency row set: it is deferred as **unidentified**, not guessed.
- Any qualification at all: 6.4 stays **open** (an exercised, qualified run is what closes it), and 6.5's hardware half
  stays **NOT RUN**.

## Next

1. **6.5's hardware half** under a dedicated HITL preset — it needs the upload authorization, and this leg is the one
   that would drive it.
2. When the first run happens: read the corrected numbers back into this oracle rather than adjusting the firmware to
   fit it.
