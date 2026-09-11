# Architecture review — immutable-loop-snapshot-passes

**Decision:** Approved through the completed snapshot model, publication,
playback, and stateless-query slices; future consumer migration, undo, load,
persistence, and compatibility-retirement slices remain separately gated.
**Plan:** [`loop_pass_snapshot_effective_state_refinement.md`](../../../docs/Plans/loop_pass_snapshot_effective_state_refinement.md)
**Reassessment:** [`ARCHITECTURE-REASSESSMENT.md`](ARCHITECTURE-REASSESSMENT.md)
**North star:** Each published `LoopSnapshot` is a complete immutable effective-state root indexed as pitch → bar → membership lists of `NoteRef`s and bar → membership lists of `EventRef`s. Its logical content is completely determined by its own reference root. `LoopPass` remains the historical/compatibility operation record. `Loop` publishes and selects snapshots; consumers read one selected snapshot directly; historical replay is compatibility-only.

The change is no longer design-only: tasks 3.5/3.6 and 4.1–4.3 have
implementation reviews and verification records below. Firmware for each
future consumer or persistence slice remains unauthorized until its own phase
gate, declared execution bounds, and required device gates pass. The logical
indexes and fixed-capacity membership representation are implemented; later
consumer bounds and storage layout remain separate decisions.

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
is the [DEC-053](../../../docs/DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)
`T_RT_reserve` contract: per-turn `reserve_us` from snapshot-idle start to the
next timing-critical `handleMidiInput()` entry, `T_RT_reserve_ceiling`
10,000 µs, and yield/retry without prefix publication. Total wall time alone
is not a sufficient performance result. An overrun or failed admission
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

Task 4.4 is closed (2026-09-09 inventory). **4.5** cross-gate verify is
closed (2026-09-10). **4.6** closed (2026-09-11 Stages 1–6; §5 undo/load
excluded to tasks 5.1–5.6). Stage 1 VisualCache `builtFromSnapshotIdentity` + `visualCacheIsCurrentPublishedSnapshot` SHIPPED (2026-09-10): native A→B@R; RAM1 **7008**; native **1821/1**; HITL `base` PASS [`151433`](../../../captures/session_20260910_151433.log); occupy PASS [`151700`](../../../captures/session_20260910_151700.log). Stage 6 HITL `base` PASS [`141547`](../../../captures/session_20260911_141547.log).

### Phase 4 implementation review (task 5.1 only)

- [x] Phase architecture gate posted in the session transcript.
- [x] Ownership NO; transition YES (snapshot selection); no new unbounded owner.
- [x] Native invariant checks pass (`test_loop_take_survival` attach-by-id +
      clear-slot retain); full native **1841/1**.
- [x] Capture-serial RAM1 locals **6880**.
- [x] HITL `base` PASS [`150709`](../../../captures/session_20260911_150709.log)
      (undo/redo 1/1; persistence `result=ok`).
- [x] Pass Active/Disabled and ClearSlot `PersistedLoopSnapshot` restore retained
      (Pin C coexistence).
- [x] Deferred after-attach uses originating `UndoEntryId` only (Pin A).
- [x] Runtime scope recorded in `CURRENT_WORK.md`, `PROJECT_STATE.md`, `tasks.md`.
- [x] Task **5.2** APPROVED 2026-09-11 — [DEC-057](../../../docs/DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write)
      (LoopSnapshot body format version 1; wire discriminator `LSV1` +
      `formatVersion == 1`; dual-write; docs/decision only). Plan:
      [`immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md).
- [x] Tasks **5.3–5.6** closed (reader/writer, load, persistence ownership, large-loop bounds).
- [ ] Task **6.x** remains open (compatibility retirement).

### Phase 4 implementation review (task 5.2 only — docs/decision)

- [x] Phase architecture gate posted in the session transcript.
- [x] Ownership NO; transition NO for 5.2; execution-bound NO in 5.2 (5.3 owns firmware units).
- [x] Layout approved: `LSV1` + `formatVersion == 1` + flags; sections
      `LoopMetadata` / `PersistedLoopSnapshot` / `LoopSnapshot`; legacy =
      absence of `LSV1`; unsupported = wrong `formatVersion`; no compatibility
      fallback when required `LoopSnapshot` is missing/unparseable.
- [x] DEC-057, `design.md` open question closed, `Storage.md` updated, `tasks.md`
      5.2 checked, runtime docs updated.
- [x] No firmware in 5.2 — task **5.3** implements reader/writer + deferred migration.

### Phase 4 implementation review (task 5.5 only — verification)

- [x] Phase architecture gate posted in the session transcript.
- [x] Ownership NO; transition NO; execution-bound NO (rows 14/15 unchanged).
- [x] Host call-site gate: persist execute is `StorageManager::processDeferredSaveState`
      from `main.cpp` remainder; `saveState()` only on clear-track race drain and SavedSet.
- [x] HITL `base` PASS [`200657`](../../../captures/session_20260911_200657.log):
      `VERIFY deferred persist nonblocking` `loop_persist=295 sliced=True drain=0 mo=600 ok=True`;
      persistence `result=ok`.
- [x] Runtime docs, `tasks.md` 5.5, and plan updated.

### Phase 4 implementation review (task 5.6 only — verification)

- [x] Phase architecture gate posted in the session transcript.
- [x] Ownership NO; transition NO; execution-bound NO (rows 14/15 unchanged).
- [x] Host gates: timing-critical paths forbid persist execute; stop path admits only.
- [x] HITL `base` PASS [`203803`](../../../captures/session_20260911_203803.log):
      `VERIFY deferred persist large-loop bounds` `windows=5 progress=True max_pers=44624us
      max_save=44931us active=True stop=True ok=True`; report
      [`203843`](../../../captures/host_midi_automation_baseline_20260911_203843.json).
      Prior verify-only replay [`200657`](../../../captures/session_20260911_200657.log).
- [x] Native matrix + full `pio test -e native` **1823/1**.
- [x] Runtime docs, `tasks.md` 5.6, and plan updated. Phase **5.x** complete; **6.x** next.

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

Historical note: the paragraph above referred to the early 4.4 display-only
slice. Parent 4.4 is closed after 4.4a–4.4h + inventory
([`immutable_loop_snapshot_4_4_closeout_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_4_4_closeout_refinement.md)).

### Phase 4.3 implementation review — stateless snapshot queries

- [x] Architecture gate posted in the session transcript.
- [x] `LoopContentResolution` exposes explicit `LoopSnapshot` query overloads
      for window events, present-at state, display notes, and window notes.
- [x] Snapshot query paths read only the supplied complete snapshot and local
      query buffers; they do not consult parent snapshots, `LoopPasses`,
      `TickIndex`, or maintained checkpoint/replay state.
- [x] Existing pass/index/checkpoint APIs remain compatibility paths for the
      later 4.4 caller migration and are not treated as the new normal source.
- [x] Capture-serial build passes with RAM1 locals **6848**.
- [x] Focused `test_loop_content_resolution` passes.
- [x] Full `pio test -e native` passes.
- [x] No new owner, transition, or interval-path execution-bound owner added.

### Phase 4.4a implementation review — bounded overdub source resolution

- [x] Architecture gate and ownership checkpoint posted in the session
      transcript; no owner or transition changed.
- [x] `Loop::rebuildOverdubSourceSpanCache` gathers selected snapshot
      memberships only for the bounded overdub source window.
- [x] Existing source-cache ownership, fallback order, pending-note overlays,
      and compatibility fallback behavior remain intact.
- [x] Snapshot-backed source resolution does not re-materialize compatibility
      passes for omitted identities; complete snapshot content is authoritative.
- [x] Focused `test_overdub_source_view` passes.
- [x] Capture-serial build passes with RAM1 locals **6848**.
- [x] Full `pio test -e native` passes: **1755 succeeded, 1 skipped**.
- [ ] Full 4.4 caller migration remains open: display fallback/identity,
      NOTE_EDIT, overdub analysis, and validation callers are not completed.

### Phase 4.4b implementation review — display snapshot fallback

- [x] Architecture gate and ownership checkpoint posted in the session
      transcript; no owner or transition changed.
- [x] `rebuildDisplayNotesInWindow` reconstructs from gathered snapshot
      events when the snapshot path succeeds.
- [x] Current-root STOPPED committed display uses the explicit snapshot query;
      stale, absent, and overlay-rejected roots retain pass replay.
- [x] Existing `visualCache`, pending-overlay, capture, and display-window
      ownership remain unchanged.
- [x] Capture-serial build passes with RAM1 locals **6848**.
- [x] Full `pio test -e native` passes: **1755 succeeded, 1 skipped**.
- [ ] Remaining 4.4 caller migration remains open: NOTE_EDIT, overdub
      analysis, validation, and other display identity/fallback paths.

### Phase 4.4c implementation review — span-cache snapshot provenance

- [x] Architecture gate and ownership checkpoint posted in the session
      transcript; no owner or transition changed. Post-stop publication lag
      (Bug B, `rev=11`) is out of scope.
- [x] `overdubSourceSpanCacheReady()` remains storage/coverage at the current
      `playbackRevision`. Reuse uses `overdubSourceSpanCacheReusable()`.
- [x] Snapshot fills stamp `publishedSnapshotRevision()`. Fallback fills stamp
      `0`, so a STOPPED prewarm cannot hide a later current root.
- [x] `rebuildOverdubSourceView`, hold hydration, and prewarm share that
      reuse predicate.
- [x] Native `test_overdub_source_view` pins fallback-cache override and
      snapshot-stamp reuse.
- [x] Capture-serial build passes with RAM1 locals **6848**. Full native
      suite: **1757 succeeded, 1 skipped**.
- [ ] Remaining 4.4 caller migration remains open: analysis, validation,
      other NOTE_EDIT oracles, and remaining display identity paths.

### Phase 4.4d implementation review — NOTE_EDIT committed-base snapshot

- [x] Architecture gate posted: paint/select dirty base only; commit
      rematerialize unchanged; no owner or transition change.
- [x] Dirty + current root uses `resolveDisplayNotes(*snapshot)`.
- [x] No-current-root keeps `passes.materializeToEventVector`.
- [x] Capture-serial RAM1 locals **6848**. Full native suite:
      **1760 succeeded, 1 skipped**.

### Phase 4.4e implementation review — NOTE_EDIT snapshot-root oracles

- [x] Stage 1 architecture gate posted: `rebuildVisualCacheFromPasses`
      current-root notes from `resolveDisplayNotes(*snapshot)`; no owner
      or transition change.
- [x] Native: rebuild stamps `builtFromRevision == publishedSnapshotRevision()`
      and notes equal `resolveDisplayNotes(*snapshot)` span membership.
- [x] Current-root `rebuildVisualCacheFromPasses` skips wrap-held append
      after span resolve (`161433` 114→115).
- [x] Device recapture [`162207`](../../../captures/session_20260908_162207.log)
      questions 1–2 PASS (STOPPED/spans/open visual 98).
- [x] Q3 [`170211`](../../../captures/session_20260908_170211.log) earned
      Stage 2 (`only_store=1`, NoteId `64`).
- [x] Stage 2: `rematerializeEditView` on a current root loads events whose
      `NoteId`s are in `resolveDisplayNotes(*snapshot)` span membership.
      Device [`171444`](../../../captures/session_20260908_171444.log) both
      `current=1` opens `only_visual=0` `only_store=0`.
- [x] Stage 3 host-verified: `commitEditAction` persist/replay and
      `materializedLoopEventsForNoteEditFocus` use
      `Loop::copyNoteEditCommitBase`. Pin survives `saveNoteEditPass`
      revision bumps. Native **1767 succeeded, 1 skipped**. Device PASS
      [`173531`](../../../captures/session_20260908_173531.log): both
      `current=1` opens `only_*=0`; Deletes 56 and 11 on that root.
- [x] Close bake: `bakeNoteEditSessionStoreToPasses` uses
      `copyNoteEditCommitBase`. Device
      [`174646`](../../../captures/session_20260908_174646.log): no
      `live capture baked`; empty exit has no `NoteEditPassClosed`.

### Phase 4.4f implementation review — leftover NOTE_EDIT oracles

- [x] Architecture gate posted: skip `assignMissingNoteIds(loop.midiEvents())`
      on a current root; E: rows fallback copies the opened snapshot root.
      No owner or transition change. `take_only` unchanged.
- [x] Stage 1: current-root open does not flatten `midiEvents()` for leftover
      identity assign.
- [x] Stage 2: `applySessionEditRows` uses
      `Loop::tryCopyNoteEditOpenedSnapshotEvents`; pin miss / no current root
      keeps `materializeExcludingEditPassIds`.
- [x] Native: `test_try_copy_note_edit_opened_snapshot_events_uses_span_note_ids`,
      `test_session_rows_fallback_uses_opened_snapshot_span_note_ids`.
- [x] Capture-serial RAM1 locals **6848**. Full native suite:
      **1772 succeeded, 1 skipped**.
- [x] Device [`191346`](../../../captures/session_20260908_191346.log):
      both `current=1` opens `only_visual=0` `only_store=0` (120 then 119).
      [`192155`](../../../captures/session_20260908_192155.log): `current=0`
      keeps pass rematerialize (`only_store=5`); three `current=1`
      `root_rev=41` opens are **141** / `only_*=0`. `take_only` unchanged.

### Phase 4.4g implementation review — analysis/validation leftover oracles

- [x] Architecture gate posted: idle validate copies current-root
      span-filtered snapshot events. Overdub analysis current-root gather
      already skips pass flatten (4.4a). No owner or transition change.
      SEVT / `take_only` unchanged.
- [x] Stage 1: `Track::validateAndCleanupMidiEvents` uses
      `Loop::copyIdleValidateEvents`. Native
      `test_copy_idle_validate_events_uses_snapshot_span_note_ids`.
- [x] Capture-serial RAM1 locals **6848**. Full native suite:
      **1773 succeeded, 1 skipped**.
- [x] Device [`193700`](../../../captures/session_20260908_193700.log):
      second overdub open `from=snapshot` `root_rev=7` `play_rev=7`
      `notes=159`. First overdub open `from=visual` is before
      `pending=0` `root_rev=1`.
      [`194039`](../../../captures/session_20260908_194039.log):
      NOTE_EDIT open `current=1` `root_rev=8` **177** / `only_*=0`.
      Close `edits=3`, no `live capture baked`.
      [`194438`](../../../captures/session_20260908_194438.log): Stage 1
      idle validate at 600.015 s on current `root_rev=1`. Log-only 2
      orphans, 116 remaining; `DFRAME` **58**. Both overdub opens
      `from=snapshot`.

### Phase 4.4h implementation review — display fallback identity

- [x] Architecture gate posted: windowed fallback copies current-root
      span notes. Overlay gather rejected. No owner or transition change.
      Hydrate / raise `N` unchanged.
- [x] Stage 1: `rebuildDisplayNotesInWindow` uses
      `Loop::tryCopyCurrentSnapshotDisplayNotesInWindow`. Native
      `test_try_copy_current_snapshot_display_notes_in_window_uses_span_note_ids`.
- [x] Capture-serial RAM1 locals **6848**. Full native suite:
      **1774 succeeded, 1 skipped** (4.4h files only).
- [x] Device [`201106`](../../../captures/session_20260908_201106.log):
      second overdub open `from=snapshot` `root_rev=5` `play_rev=5`
      `notes=114`. NOTE_EDIT open `current=1` `root_rev=6` **137** /
      `only_*=0`. `DFRAME` **137** after second stop through session
      end. First overdub open `from=visual` is before `pending=0`.

### Phase 4.4 parent closeout — inventory

- [x] Architecture checkpoint: inventory / checkoff only; no owner,
      transition, or interval-bound change.
- [x] Production `src/` pass-flatten / `resolveDisplayNotes(passes)` /
      `mergeActiveCapturePasses` sites classified as current-root normal
      (none remaining), no-current fallback, capture/oracle, or out of
      4.4 (→ 4.6 / §5 / §6).
- [x] Parent task 4.4 checked off. Plan:
      [`immutable_loop_snapshot_4_4_closeout_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_4_4_closeout_refinement.md).
- [x] 4.5 closed 2026-09-10 (`base` / occupy / compose `02` PASS).
- [x] 4.6 closed (Stages 1–6 + §5 exclusion note); do not start hydrate 4b–5 or raise `N`.

### Close-publish NoteRange pitch reuse

- [x] Architecture checkpoint: `appendEditRowChange` only; no owner or
      transition change.
- [x] `NoteRange` Replace keeps parent pitch (or earlier same-`NoteId`
      change). Does not write `row.pitch`.
- [x] Native:
      `test_note_edit_close_note_range_reuses_published_pitch`,
      `test_note_edit_close_pitch_then_note_range_keeps_updated_pitch`.

### Phase 4.5a implementation review — post-stop snapshot convergence

- [x] Architecture gate posted: hold + elevated session-exit idle; no owner
      or transition change; `N=2`; no `tryPublishSessionExitSnapshot` on
      device stop.
- [x] `refreshViewportAfterOverdubStop` holds compose (`from=hold`).
- [x] Current-root paint uses `resolveDisplayNotes(*snapshot)`; session-exit
      pending does not last-resort to passes or Overlay.
- [x] `selectDeferredIdleOwner` extra Snapshot after each non-Snapshot owner
      while `postOverdubPlayingMidiDrain && sessionExitQueued`.
- [x] Capture-serial RAM1 locals **6848**. Full native suite: **1759 succeeded, 1 skipped**.
- [x] Device [`145633`](../../../captures/session_20260908_145633.log): both
      stops `from=hold` with display child 0 and no stop-stack `pairrows`;
      `post_stop` then `pending=0` `root_rev == playback_rev` (7 and 13);
      next overdub open is `from=snapshot` at rev 7.

### Phase 3 implementation review (3.5–3.6 wrap publication)

Closed 2026-09-08. Remaining-interval yield device proof is parked
(`kSnapshotIdleReserveYieldEnabled` is false). Yield logs stay compiled.

- [x] Phase architecture gate and `DEC-047` supersession are recorded.
- [x] `CURRENT_WORK.md` names task `4.3` as the active slice; `3.5`/`3.6` are prior.
- [x] Fixed-capacity membership representation is selected and measured.
- [x] Mutation count, affected-bar, membership-entry, and journal-byte caps
      are enforced structurally.
- [x] Capture/pairing assigns `NoteId` to each logical NoteOn/NoteOff pair
      before pass promotion without a promotion-time content scan; `EventId`
      remains for non-note events only.
- [x] Persistent bar-cursor progress replaces upfront changed-span
      enumeration; construction does not copy an unbounded membership list.
- [x] Ordered fragments retain exact parent identity and survive retry;
      incomplete candidates are never selected.
- [x] Hide and geometry replacement update paired event references by
      `NoteId` while preserving pair identity and canonical order; non-note
      events retain `EventId`.
- [x] Global overdub undo remains pass-based while construction is pending.
- [x] Native snapshot/take-survival parity fixtures pass.
- [x] Capture-serial build passes with RAM1 locals **6848**.
- [ ] The [DEC-053](../../../docs/DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)
      remaining-interval yield device proof is **parked**. The 10,000 µs
      ceiling stays policy. Do not freeze the minimum observed reserve.

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
- [x] Later device recapture showed the queued session-exit pass completing
      with `pending=0` and closed the `T_RT_reserve` gate; the remaining-
      interval yield proof is intentionally parked.

### Session-exit start fragment implementation review — 2026-08-27

- [x] Architecture gate posted: owner remains `startNextPendingConstruction`;
      no ownership or transition change.
- [x] Session-exit start now emits at most one journal-capped pass-change
      fragment through a persistent `CommittedEventRange` cursor.
- [x] Publication still waits until every queued fragment completes.
- [x] Capture-serial build passes with RAM1 locals **7008**.
- [x] Full native suite passes: **1485/1485** tests.
- [x] Later device recapture remeasured the `T_RT_reserve` gate on the
      session-exit path; the earlier [`202635`](../../../captures/session_20260827_202635.log)
      result remains historical evidence.

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
- [x] Later device recapture confirmed first-record playback after
      wrap-journal drain; [`204136`](../../../captures/session_20260827_204136.log)
      remains historical evidence.

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
      `startNextPassChangeFragment`. Capture-gated inner timings
      (`refill`, `event_scan`, `note_lookup`, `change_build`,
      `construct`) are in tree on `DIAG,snapshot_idle,inner`;
      recapture the 12.4 ms `pass_change` class before that lookup;
      no `step` rewrite is justified yet; and freezing the minimum observed
      reserve is rejected as a guarantee. Do not remove the +16 live-record
      bars.

### Stage 2 pass-change inner timings — 2026-08-28

- [x] Architecture gate posted: owner remains
      `startNextPassChangeFragment` plus `appendSnapshotIdleCaptureLine`;
      no ownership, transition, or execution-bound change.
- [x] Capture-serial `snapshot_idle` emits a third `inner` line with
      `refill` / `event_scan` / `note_lookup` / `change_build` /
      `construct`. Lookup and the 32-change journal cap are unchanged.
- [x] Capture-serial RAM1 locals **6944**.
- [x] Full native suite passes: **1494/1494** tests.
- [x] Device recapture [`004119`](../../../captures/session_20260828_004119.log):
      18216/18216 complete inner rows; 21.096 ms STOPPED session-exit is
      `note_lookup=18293` on `pass_change=21050`; candidate stayed
      `root=0x0` until `pending=0` / `root=0x703f2eb0` / `root_rev=6`.
- [ ] Do not check off tasks `3.5`/`3.6` on this measurement slice.

### Stage 2 caller-scoped pitch lookup — 2026-08-28

- [x] Architecture gate posted: owner remains `LoopSnapshot` plus
      `appendNextPassChangeFragment`; no ownership, transition, or
      execution-bound change.
- [x] Pass-change identity uses `findNoteAtPitch`; generic `findNote`
      still walks other pitches for note-edit.
- [x] Native fixture
      `test_snapshot_find_note_at_pitch_stays_on_event_pitch`.
- [x] Capture-serial RAM1 locals **6944**.
- [x] Full native suite passes: **1495/1495** tests.
- [x] Device recapture [`005527`](../../../captures/session_20260828_005527.log):
      `note_lookup` max 857 µs (was 18293); `pass_change` max 3.219 ms;
      candidate stayed `root=0x0`. Tail
      [`005819`](../../../captures/session_20260828_005819.log)
      publishes `pending=0` / `root=0x70413fec` / `root_rev=10`.
- [ ] Do not check off tasks `3.5`/`3.6` on this lookup slice.
