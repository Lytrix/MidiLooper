# Execution record — the overdub recipe's route checks (4.3)

Slice: `response-driven-hitl-acceptance` task 4.3. Landed 2026-09-30. Host tooling only; firmware untouched, no
build, no device action.

Authority: design decision 5's **overdub** recipe — *"seed B and A, confirm both durable, establish running
transport and start A's overdub. One B slot gesture accepts A's stop and switches. After matching B activation,
one record-control gesture starts B's overdub; after its actual start, inject content and issue one B slot stop
gesture. No separate A stop before the switch."* — the spec's *Overdub successor* scenario, and the task's own
three clauses: **no separate A stop**, **no playback re-affirmation**, **no wait for predecessor retirement in
the critical window**.

## What this slice adds

The recipe itself was already the retention prefix (confirmed seeds via `prepare_empty` + `seed_take`, the proven
single capture-switch, B's `Overdubbing started @ tick` gate, one B stop request). What was missing was the
*checking* of the three clauses, and the switch step still accepted the wrong route:

| Clause | Before | Now |
|---|---|---|
| No separate A stop | the plan was pinned (no `a_stop` step, no A slot note before B's) | unchanged, **plus** the oracle's reading: A's own stop press shows up as a *second* stop request for A, and `L7_one_press_per_stop` fails on it |
| No playback re-affirmation | the `b_switch` wait **accepted** `Loop B: Queued/Reaffirmed playback switch` as its confirmation, then relied on the *next* gesture's `_switch_pending` guard to abort | the switch is a **route wait** (`_await_route`, the same helper the record leg uses): the production route is the capture-switch's own row (`Loop B: Switch gesture from slot A`), and a playback-queue answer is `CONTRADICTED` → the leg stops at the press with **`PLAYBACK_REAFFIRMED_INSTEAD_OF_SWITCH`** and no second press |
| No wait for predecessor retirement | implicit | pinned: a driven leg whose capture retires A **nowhere** (no `canonical_release`, no `Reentry released slot=3`, no pending-switch row) still reaches `DROVE_LEG`, so no critical-window wait depends on the release edge |

**The oracle reads the same route.** `L5b_b_start_route` (task 4.2's check) now covers both legs with each leg's
own vocabulary. For the overdub leg the route rows are the **press** rows themselves and are mutually exclusive
at the press, so the route is the *first* such row for B:

- the capture-switch row present ⇒ `ok`;
- a playback row first **and B's overdub start admitted** ⇒ `fail` (`ACCEPTED_SEQUENCE_FAIL`) — this is "B started,
  but through the playback route", which is exactly the substitute the design refuses;
- a playback row first with **no** start ⇒ `unknown`: a capture that never got that far is the existing L5 gap,
  not a second finding, which is why the pinned `s4f13-od4-*` fixture keeps its `SETUP_INCOMPLETE` verdict;
- neither ⇒ `unknown` (an evidence gap, never a pass).

A later playback row (B's own stop press can queue one) does not count: the *first* route-family row is the route
decision, the same precedence rule the record leg applies to its carrier row. On the retained device capture
(`s4f22-dec096-od-13`, the run the od fixture is cut from) the check reads **ok** and names the row it read
(`[57.162] [INFO] Loop 5: Switch gesture from slot 4`), so the production route is confirmed on real evidence and
not only on synthetic chains.

## Evidence

- `scripts/test_accepted_sequence.py` **132/132**, of which 6 are new: `OverdubRecipeRouteTests` (2 — the recipe
  driving with **no** retirement anywhere in the capture, and a playback re-affirmation refused at the press with
  the pressed-gesture list asserted) and `OverdubRecipeOracleTests` (4 — the capture-switch is the production
  route; a playback route that carried B fails; a playback row *without* a start is a gap; a separate A stop is a
  second request and fails L7). One existing pin was updated rather than added:
  `SwitchRepeatGuardTests.test_driver_refuses_a_playback_route_at_the_switch_press` — same single press, same
  "nothing sent afterwards", now refused one step earlier.
- Neighbours green together: corpus 3/3 (every retained device fixture keeps its verdict, including
  `s4f13-od4-*`), scenario 38/38, authorization 24/24, `test_retention_handoff_plan` 33/33,
  `test_hitl_correlated_actions` 23/23.
- Full host sweep **1,059 tests / 75 suites**, with only the seven pre-existing unrelated failures (plus the
  zero-test `test_note_edit_fader_motor_timing`, a harness artifact).
- **4 mutations red-proved** with byte-identical restores (`mutate_43.py`): the route wait's conflict vocabulary
  (a playback answer becomes a mere timeout), the oracle's production rule, the oracle's "only when the successor
  started" rule, and the missed-overlap staging classification.
- One attempted mutation **was equivalent rather than red**: adding the playback row to the flow's *accept* set
  changes nothing, because `CorrelatedGesture.consider` checks the conflict vocabulary *before* the stage's own
  wires. Recorded as an equivalence (the conflict stage is what protects the route), not as a coverage gap.

## What this slice did not do

- **The device leg.** No hardware run has been made for this leg either: the route vocabulary is read from the
  firmware's own emitters and the retained capture, and 6.3 is where a driven leg confirms it.
- **5.2/5.3** — content parity, the timing verifiers, the independent shutdown veto and the replayed failure
  corpus are their own tasks.
