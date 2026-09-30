# Bounded cancellation for a HITL run (task 3.2)

Host-only slice: no firmware, no build, no device action. Authority is the change's own spec —
[Bounded cancellation and immutable evidence](specs/hitl-automation/spec.md): *every attempt SHALL have a
bounded managed lifecycle and SHALL preserve a report on setup failure, exception, cancellation or timeout;
cancellation MUST prevent subsequent test gestures; capture SHALL remain available through persistence-aware
shutdown; shutdown failure SHALL prevent overall success* — plus its scenario *Timeout during a wait* (*pending
work cannot send another test gesture*, *a partial non-success report survives safe shutdown*).

## The defect this closes

A managed run is bounded by a wall-clock watchdog, but the **scenario** that overran it kept running: the
watchdog returned, the managed session released the capture child, and the scenario was still on the wire,
still pressing, with the serial port being torn down underneath it. Three properties were missing, and each now
has one owner in `scripts/hitl/actions/cancellation.py`:

| Property | Owner |
|---|---|
| nothing is pressed after the abort | `GuardedMidiOut.send` refuses once the run is cancelled, counts the refusal, and raises `RunCancelled` |
| the notes the run was holding are released | `GuardedMidiOut.release_held_notes` (one `note_off` per held note, plus `All Sound Off`/`All Notes Off` per channel the run touched) |
| the teardown still happens, and is not a test gesture | `GuardedMidiOut.teardown()` / `teardown_call(...)`, which permit sends for the shutdown's transport stop and count them separately |

Cancellation is **cooperative and observable**: `CancelScope.check()` raises `RunCancelled` with the reason at
an action boundary, `cancellable_sleep` stops within one slice, `CorrelatedGesture.wait_for(cancel=…)` stops
waiting at once, and the first cancellation wins (a watchdog and a caught interrupt racing cannot leave two
causes). The scope of the run in progress is registered (`begin_run` / `active_cancel_scope` /
`cancel_active_run`) because the watchdog lives in `managed_capture` while the driver owns the scenario.

`GuardedMidiOut.run()` is the shape a driver adopts: an **outer** context around the scenario body that absorbs
`RunCancelled`, releases the held notes, records the guard's counters and unregisters the run. It is outer on
purpose — the driver's `DeviceSession` is the inner context, so the drain still runs first — and it needs no
re-indentation of the body, which is why both drivers could adopt cancellation without restructuring.

## Ordering, which is the point

`managed_capture._run_with_watchdog` now, on a ceiling breach:

1. **cancels** the registered run (`cancel_active_run("managed run watchdog <n>s")`);
2. waits a **bounded grace** (`HITL_CANCEL_GRACE_S`, default 30 s) for the scenario to leave the wire;
3. reports whether it stopped, and returns;

and only then does the caller's `managed_capture_session` context release the capture child. With no run scope
registered there is nothing to cancel and nothing that promised to stop, so the earlier behaviour — release and
return immediately — is kept (that is the pre-existing `test_watchdog_aborts_stalled_run_and_returns`
contract), and a scenario that ignores cancellation is abandoned with a named warning rather than waited on
forever.

## The drivers

Both device drivers now open their output through the guard, register the run, keep the shutdown as the inner
context, and report what happened:

* `hitl_accepted_sequence.py` — `scene = begin_run(...)`, `out_port = GuardedMidiOut(mido.open_output(...), scope=scene)`,
  `with out_port.run() as run_record, DeviceSession(...)` whose `send_transport_stop` runs inside
  `out_port.teardown_call(...)`; the receipt wait (`_await_slot_disp`) and the marker wait (`_await_markers`)
  consult the registered scope, and the leg's 8 s settle became `cancellable_sleep`. After the block the report
  carries `midi` (the guard's own record: edges, teardown edges, release edges, refusals, held notes) and, when
  cancelled, `result = "CANCELLED"` with `cancel_reason`. A cancelled run reaches the same final report as any
  other, so the partial evidence is written and the process exits nonzero.
* `hitl_retention_handoff.py` — same shape (`run_record`, the guard, `teardown_call` around the shutdown's
  transport stop), its `_wait_for_any` and `_await_seed_receipt` are cancel-aware, and its long body sleeps
  (2 s + 0.4 s + 2 s + 4 s around the successor note and the final stop) became `cancellable_sleep`.

Both waits raise their run's own reason rather than returning a false negative: a cancelled wait must not be
read as "the device never answered", which is what the driver used to report.

## Evidence

| Suite | Result | What is new |
|---|---|---|
| `scripts/test_hitl_cancellation.py` | **21/21** | the whole layer: first-reason-wins, `check()`, the registry (including a second run refusing to start over a live one), refusal with nothing on the wire (also through a real press helper), the teardown phase permitted and counted apart, `teardown_call`, exact release + idempotence, a dying port, the guard's report, `run()` absorbing only `RunCancelled`, sleep slicing, a cancelled wait |
| `test_managed_capture.py` | **13/13** | the watchdog cancels a registered run, **joins it** (the scenario's own 0.3 s stop is observed before the call returns), refuses the post-cancel send, releases exactly the held note, and stays bounded |
| `test_accepted_sequence.py` | **93/93** | a cancelled run writes `CANCELLED` + reason, puts no further test edge on the wire (only the guard's release went out), still runs its teardown, and releases the capture **after** the shutdown (`exits == ["shutdown", "capture"]`); and a cancel landing between a press's note-on and its note-off releases the held note |
| `test_retention_handoff_plan.py` | **33/33** | the driver's seed and marker waits leave a cancelled run with `RunCancelled` (its own reason, not a missing receipt), and an uncancelled wait keeps its own answer |

Neighbouring suites green together: `test_hitl_correlated_actions` 23/23, `test_hitl_actions` 2/2,
`test_hitl_import_gate` 2/2, `test_hitl_device_time` 16/16, `test_hitl_command_forwarding` 26/26,
`test_managed_record_drain` 21/21, `test_hitl_serial_timing` 33/33, `test_retention_probe` 43/43,
`test_device_shutdown` 15/15, `test_capture_transitions` 61/61, `test_accepted_sequence_corpus` 3/3 —
**405 tests / 15 suites** for this slice.

Seven mutations, each red-proved and restored byte-identically (sha256 before and after every file):

| Mutation | Tests that caught it |
|---|---|
| the guard sends after the cancel | refusal, the real press helper, `run()` absorbing, the teardown phase |
| held notes are never released | exact release, idempotence, `run()`'s release |
| owning a run leaves it registered | the registry's second-run refusal and both `run()` cases |
| owning a run re-raises the cancellation | both driver cancel cases (no `CANCELLED` report) |
| the watchdog does not cancel the run | the watchdog cancel-and-join case |
| the watchdog does not wait for the run to stop | the same case (via the scenario's own stop) |
| a wait ignores its scope | a cancelled wait stops at once |

## Not done, and not implied

* **3.3 is untouched**: the managed metadata path is still `write_metadata_best_effort`, the report schema has
  no version key, `verify-only replay` is not implemented, and the import-gate facade is unchanged. The guard's
  `as_dict()` and `run_record.as_dict()` are *inputs* to that schema, not the schema.
* **Not every wait or sleep is cancel-aware.** The startup phases that run before `begin_run` (upload, census,
  reset handling, `_ensure_transport`'s own bounded wait) and sub-second sleeps inside a plan stay as they are:
  they are bounded, and any send after a cancel is refused anyway. Making *every* boundary injectable is 3.3's
  observation-wait facade.
* **No firmware row, build or device action is part of this slice, and none is implied.** The device legs of
  task 2.2 and the reload/`LIBOBS` poll remain where
  [S22-LATCH-EVIDENCE-PACKET.md](S22-LATCH-EVIDENCE-PACKET.md) recorded them.
