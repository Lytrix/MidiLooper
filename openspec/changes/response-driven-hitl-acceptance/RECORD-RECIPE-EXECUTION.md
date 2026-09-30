# Execution record — the record recipe (4.2)

Slice: `response-driven-hitl-acceptance` task 4.2. Landed 2026-09-30. Host tooling only; firmware untouched, no
build, no device action.

Authority: design decision 5's **record** recipe — *"prepare empty record targets; establish running transport;
start a real record Capture on A and observe its mode/interval before sending notes. A single B slot gesture while
A captures must accept A's stop and carry the record start to B. Observe the committed switch and B's new record
interval. Send B content, then issue one legal B stop gesture. … If the branch produces playback or overdub, stop
the test as wrong setup/route, not a successful substitute."* — and the spec's *Record successor* scenario
(*"one slot gesture accepts A's stop and starts the intended record successor through the production route"* /
*"global transport does not stop"*).

## What landed

| Clause | State |
|---|---|
| Genuinely empty SD-aware A/B | **landed** — the recipe prepares **both** targets through the 4.1 scoped clear (`prepare_empty`) and *seeds nothing*; each target is additionally required to be **idle** (`require_idle`), because the firmware's own arm rule makes an armed target a different gesture |
| Real record mode | **landed** — A's press is confirmed against the firmware's record-start rows (`Loop A: Start Recording` / `Queued recording (loop phase or bar)` / `Immediate punch-in`) *and* A's slot-scoped `RECORDING` execution row, so a queued start is observed through its execution |
| Switch-carried B start | **landed** — the plan's one slot gesture is B's, and its accept stage is the firmware's carrier (`Loop B: Record start carried on the switch`) or its retained twin (`Track T loop B: switch and record start retained (stop not yet admitted)`), with B's own `RECORDING` row as the execution |
| One stop per interval | **landed** — A's stop *is* the switch and B has exactly one record press; the plan is asserted as data (no second stop, no transport gesture anywhere) |
| Production dispatch verified | **landed** — the flow's route wait (`_await_record_route`) and the oracle's capture predicate (`L5b_b_start_route`) are two independent readings of the same vocabulary: the flow refuses to drive on a wrong route, the verdict refuses to pass on one |
| Host scheduling | **landed** — delayed observations (a late execution row, a retained carrier), premature retirement (an early `canonical_release` does not stop the leg), wrong-mode routes (playback/overdub rows → named abort, no second press) |
| A populated-A mutation must fail | **landed** — a populated A that cannot be cleared stops the run by name before any gesture, and a capture whose switch took the playback branch fails `L5b_b_start_route` |
| The device leg | **open — 6.3** |

## The plan, and why the old one could not work

The record leg used to be `select_track → a_record_start → a_stop(transport) → b_switch_start → b_stop`: a
**global transport stop** as A's stop. That is what the measured-window predicate (5.1) now fails, and the spec's
record scenario forbids it outright ("global transport does not stop"). The recipe is now:

```
select_track(track) → a_record_start (record button) → b_switch_start (B's slot button) → b_stop (record button)
```

One slot gesture carries **both** edges. The firmware is the reason it can:
`MidiButtonActions::handleToggleRecordForSlot` selects the target, calls
`finalizeCaptureAndSelectSlot` (which admits or binds A's record stop), and — because the target has no committed
passes (`!track.hasCommittedPassesInSlot`) — *queues* B's record start in the same gesture, or **retains** it when
A's stop is accepted but not yet admitted (S4f.6 / S4f.3). Nothing in the plan touches the transport.

**Genuinely empty, and idle.** The carrier path exists only for a target with no committed passes, so the record
leg must not seed: `prepare_empty` (task 4.1's census → scoped clear → `CLEARED`/`ALREADY_EMPTY`) now runs for
both targets and `seed_take` is only the overdub leg's. B is prepared first so that **A is the selected target**
when the leg starts (the record button acts on the selected slot), which is why a refused clear names B first.
`require_idle` then refuses an armed or recording target by name: `handleArmedRecordPress` answers "armed +
transport running" by *cancelling* the arm and "armed + transport stopped" by starting the transport, so driving
into it would silently consume the recipe's start press.

## The route check, in two places

- **The flow** (`_await_record_route`, over task 3.1's `CorrelatedGesture`): recognize → accept → execute, with a
  conflict vocabulary. A playback/overdub row makes the stage `CONTRADICTED` and the leg returns a **named**
  result (`WRONG_MODE_ROUTE`), never a substitute; a swallowed press and an uncarried switch are named apart
  (`SWITCH_NOT_CARRIED` vs `B_RECORD_START_UNCONFIRMED`). Nothing in the wait presses, so a wrong route costs no
  second gesture.
- **The oracle** (`L5b_b_start_route`, record leg only): the same vocabulary read over the **route decision
  span** — from A's accepted record stop to B's record start. The span is the point: measuring the whole capture
  would read the switched loop resuming (`Playback started`, which `PendingSwitchGate` owns as "the switch
  landed") as a wrong route. Carrier present ⇒ `ok`; wrong-route row and no carrier ⇒ `fail`; neither ⇒
  `unknown` (an evidence gap, never a pass). The mutation proof covers exactly that boundary: with the span
  widened, the "a later playback row is not the route" test goes red.

**One stale reading corrected.** The oracle's record-leg finalization note said the record stop "publishes
inline, so no window is the expected shape there". That is no longer true of this firmware — the record branch of
`finalizeCaptureAndSelectSlot` admits or retains a Record stop *"exactly like an overdub stop"*, and
`retainRecordStopGeometryObligation` is the same deferred obligation — so the spec's record scenario ("its single
stop acceptance during A's ownership window") asks for that window, and its absence is the same staging outcome
for both legs. Behaviour is unchanged (it was already a setup-kind FAIL with the `NOT_EXERCISED` reading in its
detail); the reason is now right.

## Evidence

- `scripts/test_accepted_sequence.py` **126/126**, of which 15 are new: `RecordRecipeRouteTests` (8 — the recipe's
  own receipts, a quantized start observed through its execution, a retained carrier, an early retirement, two
  wrong-mode routes, and the two halves of an uncarried switch), `RecordStartRouteOracleTests` (5 — the carrier,
  the retained form, the wrong-route failure, the span boundary, and the absence reading), and two
  preparation-level pins (an armed target refused; a populated A that cannot be cleared stops the run).
- Neighbours green together: corpus 3/3 (every retained device fixture keeps its verdict), scenario 38/38,
  authorization 24/24, `test_retention_handoff_plan` 33/33, `test_hitl_correlated_actions` 23/23.
- Full host sweep **1,053 tests / 75 suites**, with only the seven pre-existing unrelated failures (plus the
  zero-test `test_note_edit_fader_motor_timing`, a harness artifact).
- **8 mutations red-proved** with byte-identical restores (`mutate_42.py`): the wrong-route vocabulary, the
  execution stage's wires, the retained-switch guard's scope, the idle check, B's preparation, a re-added
  transport stop in the plan, the oracle's wrong-route branch, and the oracle's route span.
- One mutation **survived the first run** (the route span), which is why
  `test_a_wrong_route_row_after_b_started_is_not_the_route` now asserts `unknown` with no carrier present —
  the carrier's precedence over a later row had been masking the span rule.

## What this slice did not do

- **The device leg.** No hardware run has been made: the recipe's accept vocabulary is read from the firmware's
  own emitters, and 6.3 is where it is confirmed on real rows (and where a leg that does *not* carry the start
  would surface as `SWITCH_NOT_CARRIED` rather than as a silent substitute).
- **4.3** — the overdub recipe's own route checks (no separate A stop, no playback re-affirmation, no wait for
  predecessor retirement) are its task; this slice only reused the proven prefix.
- **5.2/5.3** — content parity, the timing verifiers and the replayed failure corpus are their own tasks.
