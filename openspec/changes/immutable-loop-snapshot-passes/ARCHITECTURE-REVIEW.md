# Architecture review — immutable-loop-snapshot-passes

**Decision:** Approved for design/specification phase; firmware gated
**Plan:** [`loop_pass_snapshot_effective_state_refinement.md`](../../../docs/Plans/loop_pass_snapshot_effective_state_refinement.md)
**Reassessment:** [`ARCHITECTURE-REASSESSMENT.md`](ARCHITECTURE-REASSESSMENT.md)
**North star:** Each published `LoopSnapshot` is a complete immutable effective-state root indexed as pitch → bar → membership lists of `NoteRef`s and bar → membership lists of `EventRef`s. Its logical content is completely determined by its own reference root. `LoopPass` remains the historical/compatibility operation record. `Loop` publishes and selects snapshots; consumers read one selected snapshot directly; historical replay is compatibility-only.

This change is design-only until a phase is approved and named in `docs/Runtime/CURRENT_WORK.md`. Phase 2 firmware is not authorized until formal approval, the later preflight gates, and an `ExecutionBounds.md` row for dirty-bar construction. The logical indexes are selected; the physical directory is not.

## Formal triggers

- **Ownership:** effective state moves from historical replay to immutable snapshots owned by `Loop` / `LoopPasses`.
- **State transitions:** commit, undo, redo, load, and clear-slot select snapshot identities.
- **Terminology:** `LoopPass` and `LoopSnapshot` are distinct objects. A commit may produce both; they are not interchangeable.
- **Execution bounds:** candidate construction is not admissible on the MIDI interval or timing-critical input path until its owner, unit cap, maximum indivisible step, progress state, bound class, and overrun policy are defined under [`ExecutionBounds.md`](../../../docs/Authority/Architecture/ExecutionBounds.md).
- **Projection:** `IntervalProjection` remains a pure consumer-side math owner; a large-loop snapshot consumer must select a bounded span range before projection, and long-span `k` candidate generation requires its own bound or overrun policy.
- **Persistence:** immutable objects and snapshot structure require a separate storage decision. This change persists the latest complete snapshot only; it does not persist undo history or post-reboot undo lineage.

## Core invariant

```text
Immutable event objects
        ^
        | references
┌─────────────────────┐
│   LoopSnapshot N    │
│ immutable root      │
└─────────────────────┘
        |
        | successor
        v
┌─────────────────────┐
│   LoopSnapshot N+1  │
│ new root            │
└─────────────────────┘
```

- Unchanged event: same object, reused reference.
- Changed event: new immutable object, replaced reference.
- Deleted event: reference removed from the new root.
- Added event: new immutable object, new reference.
- Notes are indexed pitch → bar → membership lists of `NoteRef`s; events are indexed bar → membership lists of `EventRef`s as a view of existing event-stream order.
- Only dirty pitch/bar membership and the minimum directory path to those lists are newly constructed; unchanged membership remains shared with the parent.
- Candidate sets deduplicate by `NoteId`. Inspecting a `NoteRef` does not replace it.
- Dirty bars are the union of the incoming span and every resolver-changed geometry or membership, not the incoming span alone.
- A snapshot's logical content is completely determined by its own reference root; resolving a reference or answering a content query never requires consulting a parent snapshot.

`LoopPass` (`RecordPass`, `OverdubPass`, `EditPass`) is not this root. It is the compatibility operation/history record.

## Per-phase gates

### Phase 0 — Architecture and specification

| Question | Required |
|---|---|
| **Owner module** | Docs and OpenSpec; no firmware owner change |
| **Primary invariant** | A `LoopSnapshot` is a complete immutable reference root; `LoopPass` is not that root; consumers never obtain content from a parent |
| **Ownership change?** | YES — proposed `Loop` / `LoopPasses` snapshot ownership; formal approval required |
| **State transition change?** | YES — proposed snapshot publication and selection; formal approval required |
| **Execution bound change?** | NO — design only; this phase adds the execution-bound and representation-selection gates and does not admit runtime work |
| **Behavior-preserving?** | YES — no firmware behavior changes |
| **Reuse decision** | YES — extend `Loop` / `LoopPasses`; retain semantic and playback owners |
| **Phase scope** | Change artifacts, refinement plan, decision record, runtime scope docs, parent runtime architecture, derived-representation authority, storage authority, naming, selected logical pitch/bar indexes, and the still-open physical-directory measurement |

Phase 0 must complete, before any firmware phase:

1. `LoopPass` vs `LoopSnapshot` terminology.
2. Logical indexes: pitch → bar → NoteRef (span) and bar → EventRef (events). Physical directory representation remains Phase 1 / measurement.
3. Snapshot reachability, lifetime, and reclamation.
4. Undo/redo branch semantics.
5. Provisional session snapshots versus global undo snapshots, plus latest-saved-snapshot reboot behavior.

Phase 0 **does** select the logical indexes. It does **not** select the physical directory that implements shared membership.

The formal reassessment and durable decision record approve this ownership
evolution while retaining semantic, playback, undo, persistence, and
derived-representation owners. Runtime publication, physical-directory
selection, and firmware remain separately gated.

### Phase 1 — Native snapshot model

| Question | Required |
|---|---|
| **Owner module** | `Loop` / `LoopPasses` |
| **Primary invariant** | A new snapshot cannot mutate objects or references reachable from an older snapshot; unchanged references are reused |
| **Ownership change?** | YES — only after Phase 0 approval |
| **State transition change?** | NO — no production publication wiring |
| **Execution bound change?** | NO — native/offline model |
| **Behavior-preserving?** | YES for existing consumers |
| **Reuse decision** | YES — no new manager |
| **Phase scope** | Snapshot objects, selected reference collection, builder, and invariant-protecting native checks |

Blocked until the formal reassessment is approved. Phase 1 may implement the selected logical indexes; it must still choose and measure the physical directory.

### Phase 2 — Commit publication adapters

| Question | Required |
|---|---|
| **Owner module** | Existing capture and NOTE_EDIT commit owners publish through `Loop` |
| **Primary invariant** | One existing global commit boundary produces one complete atomic `LoopSnapshot` transition; provisional session prefixes do not become global undo entries |
| **Ownership change?** | NO after Phase 0 approval |
| **State transition change?** | YES — existing commit boundaries only |
| **Execution bound change?** | YES — runtime construction is introduced at existing commit boundaries; it is allowed only through a declared bounded/resumable unit or an empirically bounded unit with an explicit overrun policy |
| **Behavior-preserving?** | YES with compatibility records and parity oracle |
| **Reuse decision** | YES — extend existing commit boundaries |
| **Phase scope** | Commit adapters, publication, and invariant/parity checks; no playback swap |

Blocked until the formal reassessment is approved and the architectural construction contract has an `ExecutionBounds.md` class. The logical indexes are selected; dirty-bar construction is not yet classified as one admissible unit.

### Phase 3 — Direct consumers

| Question | Required |
|---|---|
| **Owner module** | `Loop` publishes; `Track` / `LoopPlaybackRuntime` consumes; LCR is transitional only |
| **Primary invariant** | Consumers use one complete snapshot identity while existing representation ownership, cursor, ledger, order, horizon ownership, and bounded projection access remain unchanged |
| **Ownership change?** | NO — existing boundaries are extended |
| **State transition change?** | YES — snapshot identity is the freshness boundary |
| **Execution bound change?** | NO new unbounded owner; snapshot selection and any new preparation work must use an existing declared owner boundary |
| **Behavior-preserving?** | YES until compatibility fallback retirement |
| **Reuse decision** | YES — no new manager or adapter |
| **Phase scope** | Playback, stateless LCR queries, display/analysis callers, existing native and HITL gates |

### Phase 4 — Undo, load, and persistence

| Question | Required |
|---|---|
| **Owner module** | `TrackUndo` selects; `StorageManager` persists; `Loop` rebuilds local handles |
| **Primary invariant** | Runtime undo/redo/load/clear select complete snapshots without normal-path replay; reboot loads the latest saved complete snapshot and starts with undo/redo empty; `StorageManager` performs large-loop save/load through bounded deferred units |
| **Ownership change?** | NO after Phase 0 approval |
| **State transition change?** | YES — snapshot selection |
| **Execution bound change?** | NO new unbounded interval work; storage work must use declared `StorageManager` units, with no content-sized synchronous work in `handleMidiInput()` or clock dispatch |
| **Behavior-preserving?** | YES for compatibility and round trips |
| **Reuse decision** | YES — extend existing owners |
| **Phase scope** | Runtime undo/load, storage format, migration, and persistence checks |

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
- [ ] Required invariant-protecting native checks pass; no new test architecture was introduced
- [ ] Capture-serial build passes with RAM1 locals at least 4000 when firmware changes
- [ ] Required existing HITL gate passes when behavior requires it
- [ ] Every runtime candidate-construction owner has an `ExecutionBounds.md` row with unit cap, indivisible-step bound, progress, and overrun/failure policy
- [ ] No local timer is used as the sole bound for an indivisible operation
- [ ] Each migrated derived representation records snapshot identity for freshness and retains its existing owner, invalidation, build, and stale/fallback policy
- [ ] `LoopPasses` compatibility records and materialized compatibility sources are removed only after the individual representation migration gate passes
- [ ] Large-loop persistence and migration retain `StorageManager` ownership, use declared deferred units, and do not perform content-sized synchronous work on timing-critical paths
- [ ] No compatibility path was removed before its caller gate passed
- [ ] Runtime docs, decision log, and plan reflect the result
