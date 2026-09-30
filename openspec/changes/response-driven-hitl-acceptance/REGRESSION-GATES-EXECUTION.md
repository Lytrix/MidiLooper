# Execution record — the regression gates and the former bugs' mutations (5.3)

Slice: `response-driven-hitl-acceptance` task 5.3. Landed 2026-09-30. Host only: no firmware file touched, no
build, no upload, no device action.

Authority: task 5.3 — *"Run focused Python suites, architecture/import gates and full `pio test -e native`; record
mutation results for former queue, timing and attribution bugs. Keep host success distinct from hardware
qualification."*

## The gates, as run

| Gate | Result |
|---|---|
| Focused Python suites (per slice, and the whole host sweep) | **1,067 tests / 75 suites**, with only the seven pre-existing unrelated failures (`test_foundation_runner`, `test_layered_verifiers`, `test_occupy_paint_serial_verify`, `test_6_3_machinery_closeout_gate`, `test_compatibility_consumer_inventory`, `test_edit_session_state_serial_verify`, `test_revision_load_serial_debug`) — none of which references this change's modules. `test_note_edit_fader_motor_timing` is a standalone script with no `unittest` tests (a sweep artifact, not a failure). |
| Import gate | `test_hitl_import_gate` **5/5** — the new `hitl/acceptance_evidence.py` is reached from the *oracle*, and the oracle from the scenario, so the gate's flow/scenario rules still hold unchanged. |
| Architecture / compatibility gates | `test_b1_narrow_session_exit_compat_gate` 3/3, `test_b2_b5_b6_narrow_session_exit_compat_gate` 4/4, `test_b3_b4_narrow_session_exit_compat_gate` 5/5, `test_b7_b8_construction_consumer_gate` 3/3, `test_collapse_legacy_cli_args` 2/2, `test_hitl_actions` 2/2 — all green. |
| Full native suite | `pio test -e native` → **2,643 test cases: 7 skipped, 2,636 succeeded** (0 failed), the same fingerprint as the earlier slices of this change: no firmware file was touched by any of them. |

## Mutation record for the *former* queue/timing/attribution bugs

The bugs the 2026-09-29 device-reliability slice fixed are recorded in this change's `tasks.md` §5 comment. Each
was reintroduced and the suite that owns its fix had to go red (`mutate_53.py`, every file restored
byte-identically and sha256-verified):

| Former bug | Mutation | Suite that caught it |
|---|---|---|
| A caller's own Start was "confirmed" by **another press** when its receipt was late (a toggle: `s4f22-base-device-03` start 25.086 s / toggle 25.603 s) | the observation-only branch is dropped, so control falls through to the press path | `test_transport_clock_observation` |
| **Attribution**: outgoing control actions lost the name of the caller that sent them | the ledger entry drops its `caller` | `test_transport_clock_observation` |
| **Queue**: a queued playback switch no longer blocked the next press on the same button (the double-countdown defect, `Queued` → `Reaffirmed` → `Reaffirmed`) | `_switch_pending` always reports "nothing pending" | `test_accepted_sequence` |
| The record leg was scored from the **last** candidate take instead of the phase's chosen one (`s4f22-step8-e2`) | `record_sessions[best]` → `record_sessions[-1]` | `test_hitl_serial_timing` |

**One survived the first run, and that was a real coverage gap.** Removing the observation-only branch changed
nothing any test could see, because the fall-through was stopped by the `allow_transport_press` gate — the rule
"a late ack for *the caller's own* start is never re-pressed" was only reachable with presses explicitly allowed,
and no test did that. The pinning test is now
`test_transport_clock_observation.test_a_late_ack_for_the_callers_own_start_is_never_re_pressed`, and the mutation
is red.

## Host success is not hardware qualification

Everything above is host evidence. This change's host slices (3.1–3.3, 4.1's host half, 4.2, 4.3, 4.4, 5.1, 5.2,
5.3) drove **no** device: no upload, no `--boot-reset`, no target mutation. In particular:

- the acceptance legs' device qualification (does the firmware actually carry the start on the switch, does a
  real run produce A's deferred finalization window, does a live bundle carry the three verifier reports) is
  **6.3**, and 6.1–6.4 need a per-session upload/target authorization (task 4.1's manifest is the mechanism);
- the native suite is a *firmware* gate, not a device gate: it compiles and runs the firmware's own logic on the
  host and says nothing about a board's timing, SD state or USB behaviour;
- so a green record here must not be read as device qualification, and the sealed bundles say the same thing in
  their own vocabulary (`driver_result`, `teardown`, the `unknown_checks` list).
