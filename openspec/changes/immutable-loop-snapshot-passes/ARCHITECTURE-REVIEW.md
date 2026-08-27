# Architecture review — immutable-loop-snapshot-passes

**Decision:** Approved for Phase 3 tasks `3.5`/`3.6` design/specification;
firmware gated
**Plan:** [`loop_pass_snapshot_effective_state_refinement.md`](../../../docs/Plans/loop_pass_snapshot_effective_state_refinement.md)
**Reassessment:** [`ARCHITECTURE-REASSESSMENT.md`](ARCHITECTURE-REASSESSMENT.md)
**North star:** Each published `LoopSnapshot` is a complete immutable effective-state root indexed as pitch → bar → membership lists of `NoteRef`s and bar → membership lists of `EventRef`s. Its logical content is completely determined by its own reference root. `LoopPass` remains the historical/compatibility operation record. `Loop` publishes and selects snapshots; consumers read one selected snapshot directly; historical replay is compatibility-only.

The change remains design-only outside the activated Phase 3 tasks `3.5`/`3.6`.
Firmware for that slice is not authorized until its phase gate, fixed-capacity
directory measurement, enforced construction bounds, and device gates pass.
The logical indexes are selected; the fixed-capacity physical directory is a
Phase 3 admission prerequisite.

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
selection, and firmware remain separately gated. Later-phase preflight and
historical decision review are recorded in [`PREFLIGHT.md`](PREFLIGHT.md) and
[`DECISION_REVIEW.md`](DECISION_REVIEW.md); completing them does not select a
later implementation phase.

### Phase 1 — Native snapshot model

| Question | Required |
|---|---|
| **Owner module** | `Loop` / `LoopPasses` |
| **Primary invariant** | Each candidate or published root is complete and self-contained; it cannot mutate objects or references reachable from an older snapshot, and unchanged references are reused |
| **Ownership change?** | YES — only after Phase 0 approval |
| **State transition change?** | NO — no production publication wiring |
| **Execution bound change?** | NO — native/offline model |
| **Behavior-preserving?** | YES for existing consumers |
| **Reuse decision** | YES — no new manager |
| **Phase scope** | Snapshot objects, selected reference collection, builder, and invariant-protecting native checks |

Phase 1 is selected in `CURRENT_WORK.md`. Its native locality checkpoint
selects a sparse fixed-capacity paged directory as the physical implementation
experiment; snapshot nodes use 32 KB bump slabs owned by `Loop`. Page
capacity, peak temporary memory, and reclamation remain measured parameters.
Native candidate construction may
freeze, validate, and retain a candidate identity, but it must not make that
candidate active or wire publication into runtime paths.
`LoopPasses::materializeToEventVector()` remains an independent parity oracle
and is not a construction dependency. Candidate reads must not consult a
parent snapshot for missing content.

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

The direct-consumer phase includes overdub source resolution. Its normal source
is the selected complete snapshot's pitch/bar `NoteRef` index, not
`overdubSourceViewNotes_`, `overdubSourceSpanCacheNotes_`, `visualCache`, or
maintained pass replay. Those structures may remain as consumer-owned
representations or parity/rollback sources only until their migration gates
pass.

### Phase 3 wrap-publication sub-gate (tasks 3.5–3.6)

| Question | Required |
|---|---|
| **Owner module** | `Loop` publishes and selects the complete provisional `O:` snapshot; existing capture/pairing and overlap owners supply the bounded settled mutation journal |
| **Primary invariant** | After a settled wrap, the selected snapshot contains only the latest immutable reference for each effective `NoteId`; geometry updates preserve `NoteId` and update only affected memberships |
| **Ownership change?** | NO new owner — extend `Loop`; direct overdub-source migration remains a later task |
| **State transition change?** | YES — settled wraps publish provisional `O:` snapshot identities, while overdub stop records one global pass-based undo transition |
| **Execution-bound change?** | YES — fixed-capacity membership pages, persistent bar cursors, and bounded mutation-journal fragments are required through the existing Ring B maintenance cursor; incomplete candidates remain invisible |
| **Behavior-preserving?** | YES for overlap, pairing, wrap geometry, and emitted musical content; publication policy and future source freshness change |
| **Reuse decision** | YES — extend existing `Loop` publication and existing capture/pairing owners; do not create a source-cache authority or new manager |
| **Phase scope** | Tasks `3.5`/`3.6` only: wrap journal/publication, pair-`NoteId` and non-note `EventId` handling, fixed-capacity construction, retry/retention, native parity, firmware-size, `T_RT_reserve`, and required HITL gates. Direct consumer migration, persistence, and snapshot-identity undo are later phases |

The first implementation slice ends before consumer migration. Its device gate
must report the maximum MIDI gap attributable to snapshot construction,
maintenance time per slice, and total construction wall time. Total wall time
alone is not a sufficient performance result. An overrun or failed admission
retains the previous complete selected snapshot.

For this sub-gate, the post-root successor restriction in `DEC-045` is
superseded. Ordered bounded successor construction may step during
`PLAYING`/`OVERDUBBING` after the first root exists. This applies only to tasks
`3.5`/`3.6` and requires the replacement `DEC-047` decision record, the
activated `CURRENT_WORK.md` scope, and the updated execution-bound inventory.

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

### Phase 1 implementation review

- [x] Phase architecture gate posted in the session transcript.
- [x] Ownership, transition, and execution-bound checkpoint answered; no
      production owner or runtime transition was added.
- [x] Native invariant checks pass without a new test architecture.
- [x] Capture-serial build passes with RAM1 locals at 4640.
- [x] HITL gate not required; no capture, playback, or consumer behavior
      changed.
- [x] Candidate construction has explicit native/offline progress and failure
      preservation; runtime admissibility remains unclassified as required.
- [x] Existing `LoopPass` compatibility paths remain intact.
- [x] Runtime scope and result are recorded in `CURRENT_WORK.md`,
      `PROJECT_STATE.md`, `PREFLIGHT.md`, and `tasks.md`.

Phase 1 verification is complete. Phase 2 remains separately gated by a
runtime construction-bound classification in `ExecutionBounds.md`.

### Phase 2 implementation review

- [x] Phase architecture gate posted in the session transcript.
- [x] Ownership, transition, and execution-bound checkpoint answered.
- [x] Native invariant/parity checks pass without a new test architecture.
- [x] Capture-serial build passes with RAM1 locals at 4640.
- [x] HITL gate not required; playback/consumers still use compatibility sources.
- [x] `ExecutionBounds.md` classifies `LoopSnapshotConstruction::step` and
      session-exit / NOTE_EDIT close publication as empirically bounded with
      discard-candidate / keep-previous-published failure.
- [x] Compatibility `LoopPass` records remain intact.
- [x] Runtime scope and result are recorded in `CURRENT_WORK.md`,
      `PROJECT_STATE.md`, `PREFLIGHT.md`, and `tasks.md`.

Phase 2 verification is complete. Phase 3 (direct consumers) remains
separately gated.

Amendment (2026-08-25): overdub session-exit publication is queued at stop and
stepped from idle (`DEC-045`). Record and NOTE_EDIT close remain synchronous.
This does not add a new unbounded owner; idle uses
`LoopSnapshotConstruction::step(1)`.

### Idle-publish implementation review (DEC-045)

- [x] Architecture gate posted in the session transcript.
- [x] Ownership NO; transition YES (user-approved); no new unbounded owner.
- [x] Native invariant/parity checks pass without a new test architecture.
- [x] Capture-serial build passes with RAM1 locals at 4640.
- [ ] Device gate: long-loop overdub stop must not block MIDI/display for seconds.

### Phase 3 implementation review (4.1–4.2 only)

- [x] Phase architecture gate posted in the session transcript.
- [x] Ownership, transition, and execution-bound checkpoint answered.
- [x] Native invariant/parity checks pass without a new test architecture.
- [x] Capture-serial build passes with RAM1 locals at 4640.
- [ ] HITL gate not run in this slice; 4.5 remains open.
- [x] Playback uses existing row 2 owner; no new unbounded owner.
- [x] Cursor, order, ledger, and horizon stay on `Track` / `LoopPlaybackRuntime`.
- [x] Compatibility gather remains the fallback when no snapshot is selected.
- [x] Runtime scope and result are recorded in `CURRENT_WORK.md`,
      `PROJECT_STATE.md`, `PREFLIGHT.md`, and `tasks.md`.

Tasks 4.3–4.6 remain open. LCR conversion, display/NOTE_EDIT/analysis
migration, HITL, and identity recording on those remaining representations
are not authorized by this slice.

### Phase 3 implementation review (4.4 display only)

- [x] Phase architecture gate posted in the session transcript.
- [x] Ownership, transition, and execution-bound checkpoint answered.
- [x] Native invariant/parity checks pass without a new test architecture.
- [x] Capture-serial build RAM1 locals at 4640.
- [ ] HITL gate not run in this slice; 4.5 remains open.
- [x] Display uses existing `DisplayManager` resolve and `Loop` visual-cache
      / window gather owners; no new unbounded owner.
- [x] `visualCache` stays the display list owner.
- [x] Compatibility gather remains the fallback when no snapshot is selected.
- [x] Runtime scope and result are recorded in `CURRENT_WORK.md`,
      `PROJECT_STATE.md`, `PREFLIGHT.md`, and `tasks.md`.

Task 4.4 is not checked off. NOTE_EDIT, overdub analysis, and validation
callers are not authorized by this slice. Task 4.3 stays skipped.

### Phase 3 implementation review (3.5–3.6 wrap publication)

- [ ] Phase architecture gate and `DEC-047` supersession are recorded.
- [ ] `CURRENT_WORK.md` names tasks `3.5`/`3.6` as the active slice.
- [ ] Fixed-capacity membership representation is selected and measured.
- [ ] Mutation count, affected-bar, membership-entry, and journal-byte caps
      are enforced structurally.
- [ ] Capture/pairing assigns `NoteId` to each logical NoteOn/NoteOff pair
      before pass promotion without a promotion-time content scan; `EventId`
      remains for non-note events only.
- [ ] Persistent bar-cursor progress replaces upfront changed-span
      enumeration; construction does not copy an unbounded membership list.
- [ ] Ordered fragments retain exact parent identity and survive retry;
      incomplete candidates are never selected.
- [ ] Hide and geometry replacement update paired event references by
      `NoteId` while preserving pair identity and canonical order; non-note
      events retain `EventId`.
- [ ] Global overdub undo remains pass-based while construction is pending.
- [ ] Native snapshot/take-survival parity fixtures pass.
- [ ] Capture-serial build passes with RAM1 locals at least 4000.
- [ ] The measured `T_RT_reserve` MIDI-gap/deadline-lateness device gate
      passes with the documented retry behavior.

### Admission starvation implementation review — 2026-08-27

- [x] The existing `MidiServiceDrain` admission owner now recognizes pending
      session-exit snapshot work after wrap-journal fragments reach zero.
- [x] The existing STOPPED idle gate admits the Snapshot owner when pending
      publication exists, even if the transport clock remains running.
- [x] Construction remains one bounded `LoopSnapshotConstruction::step(1)`
      unit per admitted idle turn; no USB-host ownership or interval-path
      owner was added.
- [x] Capture-serial build passes with RAM1 locals **7008**.
- [x] Full native suite passes: **1484/1484** tests.
- [ ] Device recapture must show the queued session-exit pass completing with
      `pending=0`, then measure the remaining `T_RT_reserve` gate.

### Session-exit start fragment implementation review — 2026-08-27

- [x] Architecture gate posted: owner remains `startNextPendingConstruction`;
      no ownership or transition change.
- [x] Session-exit start now emits at most one journal-capped pass-change
      fragment through a persistent `CommittedEventRange` cursor.
- [x] Publication still waits until every queued fragment completes.
- [x] Capture-serial build passes with RAM1 locals **7008**.
- [x] Full native suite passes: **1485/1485** tests.
- [ ] Device recapture must remeasure `T_RT_reserve` on
      [`202635`](../../../captures/session_20260827_202635.log)-class content.

### Wrap publication completeness implementation review — 2026-08-27

- [x] Architecture gate posted: owner remains `stepPendingSnapshotPublish`;
      no ownership or transition change.
- [x] Wrap-journal completion no longer calls `storePublishedSnapshot` while
      queued session-exit passes, construction progress, or companion
      changes remain.
- [x] Native fixture
      `test_wrap_journal_drain_keeps_first_record_while_session_exit_pending`
      asserts the first record remains in playback gather until publication
      completes.
- [x] Capture-serial build passes with RAM1 locals **7008**.
- [x] Full native suite passes: **1486/1486** tests.
- [ ] Device recapture must confirm first-record playback after wrap-journal
      drain on [`204136`](../../../captures/session_20260827_204136.log)-class
      content.

### Stage 1 STOPPED first-paint admission — 2026-08-27

- [x] Architecture gate posted: owner remains
      `MidiServiceDrain::allowSnapshotIdleStep`; no ownership, transition, or
      execution-bound change.
- [x] Pending session-exit, in-flight, or wrap-journal Snapshot work now wins
      over `visualCacheNeedsFirstPaint` in `STOPPED`.
- [x] Native fixture `test_stopped_pending_snapshot_wins_over_first_paint`
      covers the stopped / first-paint / pending-work combination.
- [x] Capture-serial build passes with RAM1 locals **7008**.
- [x] Full native suite passes: **1487/1487** tests.
- [x] Device recapture [`221054`](../../../captures/session_20260827_221054.log)
      closed Stage 1 publication ordering after the later pending-track
      selection slice.
- [ ] Do not check off tasks `3.5`/`3.6` on this Stage 1 policy slice.

### Stage 1 STOPPED pending-track selection — 2026-08-27

- [x] Architecture gate posted: owner remains
      `main.cpp::runDeferredIdleMaintenance` plus
      `Track::processDeferredIdleMaintenance`; no ownership, transition, or
      execution-bound change.
- [x] STOPPED Snapshot turns scan every slot and select a track with pending
      publication instead of `sStoppedIdleTrackCursor`.
- [x] Capture-serial build passes with RAM1 locals **7008**.
- [x] Full native suite passes: **1489/1489** tests.
- [x] Device recapture [`221054`](../../../captures/session_20260827_221054.log):
      first STOPPED `snapshot_idle` at +0.070 s; candidate stayed `root=0x0`
      until +77.765 s; then `pending=0` published `root=0x70400ac8` /
      `root_rev=10`.

### Stage 2 construction child timing — 2026-08-27

- [x] Architecture gate posted: owner remains
      `Loop::stepPendingSnapshotPublish` plus
      `appendSnapshotIdleCaptureLine`; no ownership, transition, or
      execution-bound change.
- [x] Capture-serial `snapshot_idle` emits `start_us`, `pass_change_us`,
      `step_us`, `finish_us`, `publish_us`, and `probe_us`.
- [x] `DIAG,snapshot_idle` is Tier-A.
- [x] Capture-serial build passes with RAM1 locals **6976**.
- [x] Full native suite passes: **1489/1489** tests.
- [x] Device recapture [`222401`](../../../captures/session_20260827_222401.log):
      `start_us` owns the 10–12 ms first-record and 5–8 ms STOPPED
      session-exit slices; parent max 16.252 ms; candidate stayed
      `root=0x0` until `pending=0` / `root=0x7039436c` / `root_rev=7`.
- [ ] Child fields after `start_us` were truncated at 191 bytes on
      [`222401`](../../../captures/session_20260827_222401.log). A second
      `DIAG,snapshot_idle,child` line now carries `start` / `pass_change` /
      `step` / `finish` / `publish` / `probe`. Capture-serial RAM1 locals
      **6976**; native **1490/1490**.
- [x] Device recapture [`224842`](../../../captures/session_20260827_224842.log)
      closed the child-line budget (12789/12789 complete child rows) and
      named `start` / `pass_change` / `step`. Record-stop leftover is
      7.558 ms outside those children; the file ended before publication.
- [x] Leftover probes (`enqueue`, `discard`, `emit`) are on the existing
      child line. Recapture [`225659`](../../../captures/session_20260827_225659.log)
      names the record-stop leftover as
      `discardPendingIfLoopLengthChanged` (`discard=15262`). Exclusive
      leftover after that probe is ≤ 39 µs. Publication closed at
      `pending=0` / `root=0x70431844` / `root_rev=8`.
- [ ] Stage 2 falsification review is in
      [`snapshot_publication_timing_bugfix.md`](../../../docs/Plans/snapshot_publication_timing_bugfix.md):
      discard remains a real content-sized destructor for non-fit.
      Session-exit length handoff device PASS
      [`003015`](../../../captures/session_20260828_003015.log):
      record-stop PLAYING `discard=47`; first root stayed `root=0x0`
      until `pending=0` / `root=0x703248cc` / `root_rev=1`.
      The record/overdub pass identity
      invariant supports a proposed caller-scoped pitch lookup for
      `startNextPassChangeFragment`, while its inner ownership and
      pitch-local cost remain unmeasured;
      no `step` rewrite is justified yet; and freezing the minimum observed
      reserve is rejected as a guarantee. Do not remove the +16 live-record
      bars.
