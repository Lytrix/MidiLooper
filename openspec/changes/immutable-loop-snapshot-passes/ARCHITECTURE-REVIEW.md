# Architecture review — immutable-loop-snapshot-passes

**Decision:** PENDING formal architecture reassessment and approval
**Plan:** [`loop_pass_snapshot_effective_state_refinement.md`](../../../docs/Plans/loop_pass_snapshot_effective_state_refinement.md)
**North star:** Each committed `LoopPass` is a complete immutable musical-state snapshot; `Loop` publishes it; consumers select it directly; historical replay is compatibility-only.

This change is design-only until a phase is approved and named in `docs/Runtime/CURRENT_WORK.md`.

## Formal triggers

- **Ownership:** effective state moves from historical replay to immutable snapshots owned by `Loop` / `LoopPasses`.
- **State transitions:** commit, undo, redo, load, and clear-slot select snapshot identities.
- **Execution bounds:** candidate construction is not admissible on the MIDI interval or timing-critical input path until its owner, unit cap, maximum indivisible step, progress state, bound class, and overrun policy are defined under [`ExecutionBounds.md`](../../../docs/Authority/Architecture/ExecutionBounds.md).
- **Projection:** `IntervalProjection` remains a pure consumer-side math owner; a large-loop snapshot consumer must select a bounded span range before projection, and long-span `k` candidate generation requires its own bound or overrun policy.
- **Persistence:** immutable objects and snapshot structure require a separate storage decision.

## Per-phase gates

### Phase 0 — Architecture and specification

| Question | Required |
|---|---|
| **Owner module** | Docs and OpenSpec; no firmware owner change |
| **Primary invariant** | Complete immutable snapshots are self-contained; consumers never obtain content from a parent |
| **Ownership change?** | YES — proposed `Loop` / `LoopPasses` snapshot ownership; formal approval required |
| **State transition change?** | YES — proposed snapshot publication and selection; formal approval required |
| **Execution bound change?** | NO — design only; this phase adds the execution-bound gate and does not admit runtime work |
| **Behavior-preserving?** | YES — no firmware behavior changes |
| **Reuse decision** | YES — extend `Loop` / `LoopPasses`; retain semantic and playback owners |
| **Phase scope** | Change artifacts, refinement plan, decision record, and runtime scope docs |

### Phase 1 — Native snapshot model

| Question | Required |
|---|---|
| **Owner module** | `Loop` / `LoopPasses` |
| **Primary invariant** | A new snapshot cannot mutate objects or references reachable from an older snapshot |
| **Ownership change?** | YES — only after Phase 0 approval |
| **State transition change?** | NO — no production publication wiring |
| **Execution bound change?** | NO — native/offline model |
| **Behavior-preserving?** | YES for existing consumers |
| **Reuse decision** | YES — no new manager |
| **Phase scope** | Snapshot objects, reference collection, builder, and native tests |

### Phase 2 — Commit publication adapters

| Question | Required |
|---|---|
| **Owner module** | Existing capture and NOTE_EDIT commit owners publish through `Loop` |
| **Primary invariant** | One existing commit boundary produces one complete atomic snapshot transition |
| **Ownership change?** | NO after Phase 0 approval |
| **State transition change?** | YES — existing commit boundaries only |
| **Execution bound change?** | YES — runtime construction is introduced at existing commit boundaries; it is allowed only through a declared bounded/resumable unit or an empirically bounded unit with an explicit overrun policy |
| **Behavior-preserving?** | YES with compatibility records and parity oracle |
| **Reuse decision** | YES — extend existing commit boundaries |
| **Phase scope** | Commit adapters, publication, and native parity tests; no playback swap |

### Phase 3 — Direct consumers

| Question | Required |
|---|---|
| **Owner module** | `Loop` publishes; `Track` / `LoopPlaybackRuntime` consumes; LCR is transitional only |
| **Primary invariant** | Consumers use one complete snapshot identity while cursor, ledger, order, horizon ownership, and bounded projection access remain unchanged |
| **Ownership change?** | NO — existing boundaries are extended |
| **State transition change?** | YES — snapshot identity is the freshness boundary |
| **Execution bound change?** | NO new unbounded owner; snapshot selection and any new preparation work must use an existing declared owner boundary |
| **Behavior-preserving?** | YES until compatibility fallback retirement |
| **Reuse decision** | YES — no new manager or adapter |
| **Phase scope** | Playback, stateless LCR queries, display/analysis callers, native and HITL parity |

### Phase 4 — Undo, load, and persistence

| Question | Required |
|---|---|
| **Owner module** | `TrackUndo` selects; `StorageManager` persists; `Loop` rebuilds local handles |
| **Primary invariant** | Undo/redo/load/clear select complete snapshots without normal-path replay |
| **Ownership change?** | NO after Phase 0 approval |
| **State transition change?** | YES — snapshot selection |
| **Execution bound change?** | NO new unbounded interval work; storage work must use declared `StorageManager` units, with no content-sized synchronous work in `handleMidiInput()` or clock dispatch |
| **Behavior-preserving?** | YES for compatibility and round trips |
| **Reuse decision** | YES — extend existing owners |
| **Phase scope** | Runtime undo/load, storage format, migration, and persistence tests |

### Phase 5 — Compatibility retirement

| Question | Required |
|---|---|
| **Owner module** | Existing LCR/materialization callers; removal only after migration |
| **Primary invariant** | No normal consumer depends on maintained LCR history or full replay |
| **Ownership change?** | NO — compatibility removal |
| **State transition change?** | NO new transition |
| **Execution bound change?** | NO new owner |
| **Behavior-preserving?** | YES after parity and rollback gates |
| **Reuse decision** | YES — remove existing compatibility paths only |
| **Phase scope** | LCR/materialization retirement, reader removal, and documentation closeout |

## Implementation review

Before checking off a phase in `tasks.md`:

- [ ] Phase architecture gate posted in the session transcript
- [ ] Ownership, transition, and execution-bound checkpoint answered
- [ ] Required native tests pass
- [ ] Capture-serial build passes with RAM1 locals at least 4000 when firmware changes
- [ ] Required HITL gate passes when behavior requires it
- [ ] Every runtime candidate-construction owner has an `ExecutionBounds.md` row with unit cap, indivisible-step bound, progress, and overrun/failure policy
- [ ] No local timer is used as the sole bound for an indivisible operation
- [ ] No compatibility path was removed before its caller gate passed
- [ ] Runtime docs, decision log, and plan reflect the result
