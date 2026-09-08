# Snapshot Publication Reserve Admission

**Status:** Parked — remaining-interval yield proof is not device-testable;
`kSnapshotIdleReserveYieldEnabled` is false; yield logs stay compiled.
OpenSpec `3.5` / `3.6` are closed without this recapture.  
**OpenSpec:** `immutable-loop-snapshot-passes` tasks `3.5` / `3.6` (closed)  
**Decision:** [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)  
**Parent timing plan:** [`snapshot_publication_timing_bugfix.md`](snapshot_publication_timing_bugfix.md)

## Decision

A timing-critical Snapshot idle turn admits the existing two construction units
only when their classified worst-case cost fits the already-knowable remaining
interval. Otherwise the turn yields, retains journals and in-flight
construction, keeps the previous complete root selected, and retries on a later
eligible idle turn.

```text
admitted_us = min(10000, max(0, remaining_us − T_RT_safety_margin))
if two_unit_cost > admitted_us → yield
```

`STOPPED` bypasses this timing gate. Do not use this turn’s retrospective
`reserve_us` or a local elapsed-time timer. Do not introduce
`RuntimeWorkBudget::admit`.

## Architecture gate

| Question | Answer |
|---|---|
| **Owner module** | `MidiServiceDrain` admission plus `Track::processDeferredIdleMaintenance`. `Loop::stepPendingSnapshotPublish` remains the sole construction/publication owner. |
| **Primary invariant** | Two Snapshot units run only when they fit the remaining MIDI interval; yield retains the previous complete root. |
| **Ownership change?** | NO |
| **State transition change?** | NO — yield keeps the existing pending/in-flight state and atomic publication boundary. |
| **Execution bound change?** | YES — enforce admission for the already-approved row-22 `N = 2` turn. No new interval-path owner. |
| **Behavior-preserving?** | YES for effective content and publication atomicity; insufficient-reserve turns are deferred. |
| **Reuse decision** | YES — extend `MidiServiceDrain` and `Track::processDeferredIdleMaintenance`. |
| **Phase scope** | Reserve admission, capture-gated yield telemetry, capture-only serial inject, native proofs, device HITL. No consumer migration. |

Architecture-checkpoint: ownership NO; transitions NO; no new unbounded interval owner.

## Implementation

1. Pure admission helpers in `MidiServiceDrain`:
   - `T_RT_reserve_ceiling` = 10,000 µs
   - `T_RT_safety_margin` = 1,000 µs working hypothesis (unfrozen)
   - classified two-unit cost = `kSnapshotIdleConstructionUnitCostUs × 2`
2. Remaining interval is `RuntimeTimingTelemetry::remainingUntilNextPlaybackServiceUs`
   (next playback-service deadline). Unknown cadence admits.
3. `Track::processDeferredIdleMaintenance` calls
   `stepPendingSnapshotPublish(2, …)` only after admission succeeds.
4. Capture-gated `DIAG,snapshot_idle,yield` on rejection.
5. Capture-only serial `!SNAPSHOT_YIELD` arms one constrained-reserve turn.

## Native gates

- `test_midi_service_drain`: two-unit fit, exact-fit, insufficient reserve,
  safety-margin floor-to-zero, STOPPED bypass.
- `test_loop_take_survival`: yield retains the previous root and pending work;
  retry publishes only at `pending=0`.
- `test_runtime_timing_telemetry`: remaining-interval arithmetic.
- `test_capture_line_tier`: yield line is Tier-A and fits the 191-byte budget.

## Device HITL

Managed base preset with `--arm-snapshot-yield` and an extended serial drain.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset base \
  --midi-out "Teensy" --midi-in "Teensy" \
  --track-number 5 --loop-slot 1 --midi-channel 5 \
  --record-bars 2 --overdub-bars 2 \
  --no-fixed-grid-notes --start-transport \
  --overdub-start-delay-bars 0 --overdub-start-delay-beats 1 \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 3000 \
  --serial-close-drain-ms 60000 \
  --arm-snapshot-yield
```

Verify-only:

```bash
.venv/bin/python scripts/hitl/verify/snapshot_reserve_admission.py \
  captures/<session>.log
```

Pass requires:

- `snapshot_idle,yield` during `RECORDING` / `PLAYING` / `OVERDUBBING`
- selected root unchanged through the yield
- a later idle retries and reaches `pending=0`, `journals=0`, `construction=0`
- no incomplete prefix becomes authoritative
- STOPPED rows are excluded from reserve scoring
- existing base note-count, transition, clock, and undo/redo gates still apply

Device [`115043`](../../captures/session_20260908_115043.log):

- `snapshot_reserve_admission.py` `{ok: True, yield_count: 6389, yield_state: RECORDING, yield_root_rev: 0, complete_root_rev: 9, complete_pending: 0}`
- Every live yield is `remaining=0`, `pending=1`, `root_rev=0`
- Last yield at `#CAP,84386206` before `PLAYING → STOPPED` at `#CAP,84455812`
- First `snapshot_idle` construction at `#CAP,84560401` (STOPPED)
- Complete publish at `#CAP,90248476`: `pending=0`, `journals=0`, `construction=0`, `root_rev=9`
- No yield after STOPPED
- Host base still FAIL for known non-slice checks (`record_first_note_too_late`, 2304-vs-1536, `dispatch`, SEVT window)

Parked 2026-09-08: no testable live yield on firmware. Remaining-interval
reject is off (`kSnapshotIdleReserveYieldEnabled = false`).
`allowSnapshotIdleReserve`, `DIAG,snapshot_idle,yield`, and `!SNAPSHOT_YIELD`
stay in tree. OpenSpec `3.5` / `3.6` close on wrap-journal publication plus
[`101815`](../../captures/session_20260908_101815.log) /
[`104353`](../../captures/session_20260908_104353.log).
`T_RT_safety_margin` stays unfrozen.

## Exclusions

- Task `4.3` / consumer migration
- Freezing `T_RT_safety_margin` or an observed unit as `T_RT_reserve`
- Reverting `kSnapshotIdleConstructionUnitsPerTurn` to 1
