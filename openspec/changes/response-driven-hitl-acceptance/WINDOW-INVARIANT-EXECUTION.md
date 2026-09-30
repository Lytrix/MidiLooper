# Execution record — the measured window's own invariants (5.1)

Slice: `response-driven-hitl-acceptance` task 5.1, the clause *"transport stop and clock change yields the
specified non-PASS result"*. Landed 2026-09-30. Host tooling only; firmware untouched, no build, no device
action.

Authority: the spec requirement *Continuously playing acceptance window* — "Transport MUST remain playing
throughout each measured window. Preparation, clearing, seeding and final stopped-only verification SHALL have
**explicit boundaries outside that window**" — and its two scenario clauses *"global transport does not stop"*
(record and overdub successors). Task 5.1's own text named the gap: the oracle had no predicate for either.

## What landed

| Piece | Owner | What it is |
|---|---|---|
| `leg_window_end_row` | `hitl/flows/accepted_sequence.py` (`drive_leg`, `LegOutcome`) | the window's **end**: the line count when the leg's plan returned, taken before the settle and before the run's final stopped-only stop |
| `leg_start_row`, `leg_window_end_row` in the report | `hitl/accepted_sequence_run.py` | the window travels with the run's own report, so a verdict names the span it was read over |
| `_run_window(run_report)` | `hitl/accepted_sequence_probe.py` (CLI) | an ordered, non-empty `[start, end)` pair of line offsets, or `None` — never a fabricated window |
| `continuous_transport`, `clock_source_stable` | `hitl/accepted_sequence_probe.py` (`analyse(..., window=)`) | the two predicates, kind `window`, so a FAIL yields `ACCEPTED_SEQUENCE_FAIL` and an unbounded span yields `UNKNOWN` (never `OK`) |

**The window's boundaries are the spec's, not a guess.** The requirement puts the leg inside the window and
"preparation, clearing, seeding and final stopped-only verification" outside it. The driver therefore takes
one mark immediately before the leg's first gesture (`leg_start_row`) and one **when the plan returned**
(`leg_window_end_row`) — the settle and the final transport stop that follow are the run's teardown, and the
torn-down transport is precisely the "final stopped-only verification" the requirement keeps out of the span.

**Why the mark's position is load-bearing, measured on real data** (`captures/s4f22-dec096-od-13`, the
recorded run the retained fixture is cut from): the leg's last press is at device 59.141 s and the driver's
final `Transport stopped` at 67.747 s — 8 s and thousands of device rows later. Grading that capture through
the oracle CLI, with the run's own marks:

| Window end | `continuous_transport` | Verdict |
|---|---|---|
| the flow's mark (plan returned, `[21677, 21800)`) | `ok` | **`ACCEPTED_SEQUENCE_PASS`** |
| the run's own end (after the settle and the final stop) | `FAIL` — *"1 `Transport stopped` row(s) inside the measured window … `[67.747] [INFO] Transport stopped`"* | `ACCEPTED_SEQUENCE_FAIL` |

So a mark taken after the leg returned would have failed a conforming run on the run's own teardown, and the
test `test_the_measured_window_ends_at_the_plan_not_at_the_runs_own_stop` pins that difference in both
directions (the run's report, and the oracle reading both windows over the same log).

**What the predicates read is the device's own vocabulary**, not an inference from volume:

- `Transport stopped` — printed by `ClockManager::stopTransport` as a human row carrying the device clock
  (written straight to `Serial`, so it is not a ring row and its absence is live evidence);
- `#CAP,<us>,CS,<from>,<to>` — the one-shot `DebugSessionCapture::clockSource` emits when the clock moves
  between `INT` and `EXT`, i.e. "the evidence either side is not one timeline".

**Absence is graded as absence.** A capture that cannot bound its window reports both invariants as
`UNKNOWN` (⇒ `INSUFFICIENT_EVIDENCE`), never as stability. Without run marks the oracle falls back to the
leg's own `Loop N:` stop-request rows and **says so** in `evidence["window"]` ("the leg's own press span");
two of those rows are the minimum (one row bounds nothing), and a capture with neither returns
`"unbounded: the two window invariants above are UNKNOWN, not measured"`.

**The shed rule is scoped to the window.** `CS` is ordinary (non-Tier-A) ring text, so a row dropped *inside*
the span means absence is not evidence — `UNKNOWN`. A drop *outside* it says nothing about the span, so the
check stays `OK` and the row set is the evidence (`evidence["window_shed_rows"]`). Measured reason: the same
device run shed one ordinary row at device 51.4 s, before its leg began; a capture-wide rule would have called
that run's clock source unmeasurable for a drop that could not have touched the window.

## The record-leg consequence, said out loud rather than left to be inferred

The record leg's plan still stops the global transport itself (`leg_gestures`' `a_stop`, `Gesture("a_stop",
"transport", 0)`), while the spec's *Record successor* scenario has the switch to B carry B's record start.
A record-leg run therefore fails `continuous_transport` — correctly, since the transport did stop inside the
window — and the oracle records a **limitation** naming the plan rather than the device:

> On the record leg this failure names the *plan* (`leg_gestures`' `a_stop`) rather than the device: the spec's
> record-successor scenario has the switch to B carry B's record start, which is task 4.2's change.

That is the oracle **4.2 is measured against**: making the record leg switch-carried is what turns this
predicate green, and the check is now the visible gate for it instead of prose.

## Evidence

- `scripts/test_accepted_sequence.py` **112/112** (12 new pins: the fallback boundary, a stop inside the
  window, a stop after the leg's last press, the run's marks as the boundary, a clock transition inside, a
  shed row inside and outside, one press row not being a window, the malformed/absent mark shapes, the
  record-leg limitation, and the driver's own mark ordering).
- Neighbours green together: `test_accepted_sequence_corpus` 3/3 (every retained device fixture keeps its
  verdict), `test_accepted_sequence_scenario` 38/38, `test_hitl_stimulus_authorization` 24/24,
  `test_retention_handoff_plan` 33/33.
- Full host sweep **1,039 tests / 75 suites**, with only the seven pre-existing unrelated failures
  (`test_foundation_runner`, `test_layered_verifiers`, `test_occupy_paint_serial_verify`,
  `test_6_3_machinery_closeout_gate`, `test_compatibility_consumer_inventory`,
  `test_edit_session_state_serial_verify`, `test_revision_load_serial_debug`) — none of which references this
  slice's modules. `test_note_edit_fader_motor_timing` is a standalone script with **no** `unittest` tests
  (it reports "NO TESTS RAN" under a module sweep), which is a harness artifact, not a failure.
- **7 mutations red-proved** with every file restored byte-identically (`mutate_51.py`): the mark-pair
  ordering guard, the stop scan, one-press-row bounding, the flow's end mark, the shed rule's scope, the
  record-leg limitation, and the run scope taking its own (post-teardown) end.
- Real-device data, not only synthetic logs: `ACCEPTED_SEQUENCE_PASS` on the full
  `captures/s4f22-dec096-od-13` capture with the run's own marks (the leg's rows alone were already the
  retained fixture's PASS), and `ACCEPTED_SEQUENCE_FAIL` on the same capture with the mark taken after the
  run's final stop.

## What this slice did not do

- **4.2** — the record leg is still transport-carried: the oracle now names that as the plan's own violation
  (above), and the *mixed* record-predecessor/overdub-successor scenario and the route refusals remain 4.2/4.3.
- **5.2/5.3** — content-parity and timing verifiers, the independent shutdown veto, the replayed failure
  corpus and the `pio test -e native` gate are their own tasks.
- **Device qualification** — 4.1's device legs and 6.x need a per-session upload/target authorization. This
  slice re-derived its conclusions from an existing capture; it drove nothing.
