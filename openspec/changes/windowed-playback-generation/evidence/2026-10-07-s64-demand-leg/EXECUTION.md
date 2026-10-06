# Task 6.4 — the demand-creating device leg: the lane's entry term is named, and the demand is given time

**Date:** 2026-10-07 · **Base:** `a7f7ae48` · **Task:** 6.4 (exercise half) · **Type:** host tooling + host tests.
**No firmware line changed**, no upload, no device run in this slice.

## The gap this closes

Task 6.4's exercise half owes a **demand**: five device runs (2026-10-05) spent on the difference between "this track
owed no window" and "the window was refused, and by which term" (recorded as **F-16**), because every exit of
`Track::serviceRollingPlaybackWindow` returns a typed outcome with no row on the host side and the leg's own verifier
could only report `window_demand_observed: False` without saying why. Two further facts the measurement established:

* the demand's **entry trio** must all hold -- a stamped deadline clock (`ep`), a demoted loop (`av`), and a length that
  is a whole number of two-bar spans (`al`) -- and one measured run failed on `len=19200` (25 bars, `al=0`) while
  another, aligned, failed later at `win_not_servable` (`sv=0`: the active slot holds no published current window);
* on a rootless demoted target the publication needed **well over 30 s** of service at the measured ~105 eligible
  turns/s x 2 units (`captures/s64-progress-01`), while the leg's switch leg waited a fixed **20 s** -- i.e. the leg
  could stop looking before the demand it had just created was ever served, and then report the lane as unexercised.

## What landed

**The lane's own gate rows are read, and the refusing term is named** (`scripts/hitl/verify/windowed_lane.py`):

* `gate_rows()` parses `DIAG,win_gate` by `DebugSessionCapture::winGateRow`'s own field order (`t`,`s`,`x=`,`len=`, the
  sixteen flag bits, `rq=`), and skips a row truncated before `rq` rather than reading it at the wrong offsets;
* `demand_entry_summary()` answers whether the lane was given work from three independent surfaces -- a gap block with
  an eligible/progressing turn, a readiness receipt, or a **gate exit that can only be reached with a frame or a request
  in existence** (`win_requested`, `win_request_dropped`, `win_serving`, `win_ready`, `win_waiting`, `win_blocked`,
  `win_blocked_by_request`, `win_frame_invalidated`) -- and, when there is none, which term refused;
* the demand check's detail keeps F-16's finding in front (a dispatch with nothing owed is not a lane defect) and then
  appends the measurement five runs once lacked: the exit histogram, the dominant exit, the last row's state, and a
  translation in the active slot's own terms -- e.g. `win_not_servable ... -> the active slot holds no published current
  window, so the rolling lane has nothing to roll from (a switch/launch that binds one creates the demand)`, or
  `win_window_ineligible ... -> the length is not a whole number of two-bar spans`. A capture carrying **no** gate row
  says so explicitly (the family is qualification-only), so an unreadable entry is not mistaken for an absent one;
* the reading travels in the result as `demand` (`observed`, `gap_work`, `receipts`, `gate_rows`, `exits`,
  `dominant_exit`, `refusal`, `last`), so a run's report carries the reason rather than requiring it to be re-derived.

**The demand is given time, and the wait is derived rather than chosen**
(`scripts/hitl/scenarios/windowed_content_parity.py`):

* `SWITCH_SETTLE_S` is **45 s** -- the s64-progress-01 measurement (>30 s of service) with margin, documented in place
  against that measurement, the ~8 s switch hold bound and the 360 s managed-run bound;
* `--switch-settle-s SECONDS` overrides it per run, is a **float** (its own splitter, `split_switch_settle_flag`), and is
  refused **before any device contact** when it is non-positive, unparsable (the token is named) or given **without**
  `--switch-target-slot` (the wait belongs to the switch leg; a run that names it without a target would measure
  nothing and could still read as "the lane was not asked").

## Oracle and controls

* `scripts/test_windowed_content_parity_verify.py` — **39/39** (was 28). New: the gate row read at the emitter's field
  positions and a truncated row skipped; an unaligned demoted loop naming `al`; a `win_not_servable` run naming the
  missing published window; the **dominant** exit reported rather than an odd one; `win_requested` counted as a demand
  with no receipt and no eligible turn; a capture without the gate family stating that the image does not compile it;
  the settle flag split out of the forwarded arguments, an unparsable token refused by name, the derived default pinned,
  and both pre-device refusals.
* Full host discover (`python -m unittest discover -s scripts -p "test_*.py"`, 1,254 tests) was run **A/B against HEAD**:
  the failing set is **byte-identical with and without this change** (6 failures + 2 errors, in
  `test_compatibility_consumer_inventory`, `test_foundation_runner`, `test_layered_verifiers`,
  `test_occupy_paint_serial_verify`, `test_6_3_machinery_closeout_gate`, `test_hitl_serial_timing`,
  `test_edit_session_state_serial_verify`), so this slice introduces no host failure and fixes none.
* No firmware or C++ test file changed, so the native suite is unaffected by construction (it was green at
  **2,855 / 7 skipped / 2,848 succeeded / 0 failed** earlier in this session on the same tree).

## What this does NOT establish

* **No device run.** The leg is prepared and host-verified; whether a demand appears is the next authorized run's
  measurement, and the first run may correct this oracle rather than the firmware.
* **The first window's owner is not changed.** The demand's servability term (`sv`) is the launch's: a plain
  record-and-play still owes nothing, and the demoted switch to a filled target is what binds a first window.
* Nothing about timing, the service envelope, `ExecutionBounds.md` rows 27–29, F-7/F-8 or AC-8: those remain
  **NOT RUN** and 6.4/6.5 stay open.
* No capacity, grain, lead or hold constant was touched, and no row family was added to the firmware.

## Next step (needs the owner's upload authorization)

Flash `teensy41-capture-serial-window-qualification` (the lane compiled on; it links inside its RAM1 guard again since
the 2026-10-07 repair: `code 392,312 + padding 904`) and run the registered profile on the authorized bench target with
the demand shape: a **demoted (>16 bars), span-aligned (even bar count)** target, driven through the switch leg
(`--switch-target-slot`) with the settle window in place — then read `win_gate`'s exits and, if the entry terms hold,
the receipts and publications they lead to.
