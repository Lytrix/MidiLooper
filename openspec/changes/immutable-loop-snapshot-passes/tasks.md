## 1. Architecture and specification gate

Phase 0 is documentation only. Later sections are blocked until 1.1–1.8 are complete.

- [x] 1.1 Complete formal architecture reassessment for immutable snapshots, including ownership, transitions, execution bounds, storage, and compatibility removal. Recommendation is recorded in `ARCHITECTURE-REASSESSMENT.md`.
- [x] 1.2 Record the approved decision in `docs/DECISION_LOG.md` and update the refinement plan, parent runtime architecture, derived-representation authority, storage authority, and naming with the accepted `LoopPass` / `LoopSnapshot` boundary. The logical indexes are approved; the physical directory remains a later measurement decision.
- [x] 1.3 Keep this change selected in `docs/Runtime/CURRENT_WORK.md` as Phase 0 only and post the phase architecture gate before any firmware edits. Gate posted in the implementation session; no firmware is authorized.
- [x] 1.4 Complete `PREFLIGHT.md` / `DECISION_REVIEW.md` for each later ownership or stop-path implementation phase. Change-scoped artifacts cover Phases 1–5; no later phase is authorized by them.
- [x] 1.5 Record the persistent reference-root invariant: successor snapshots reuse unchanged event-object references, allocate new objects only for added or modified content, and must not copy the complete reference set.
- [x] 1.6 Select the logical reference structure: notes as pitch → bar → membership lists of `NoteRef`s (span index) and events as bar → membership lists of `EventRef`s (indexed view of existing event-stream order). Path-copy is rejected as the overlap model. The physical directory representation remains Phase 1 / measurement.
- [x] 1.7 Specify snapshot reachability/reclamation, provisional session snapshots versus global undo snapshots, redo-branch clearing, and latest-saved-snapshot reboot behavior.
- [x] 1.8 Record the architectural construction contract (identify, replace, construct root, freeze, publish) and require an `ExecutionBounds.md` class before runtime publication. Treat any numeric N-candidate quantum as a measurement hypothesis only.

## 2. Immutable snapshot model

Blocked until section 1 is complete (formal reassessment 1.1 still open). The logical indexes are selected; the physical directory is not.

- [ ] 2.1 Define immutable note and non-note event objects with loop-scoped stable references, a declared execution-bound class, a memory allocation contract, and canonical span access for projection consumers.
- [ ] 2.2 Implement the selected pitch/bar membership indexes and complete immutable `LoopSnapshot` records under `Loop` / `LoopPasses`. Choose and measure the physical directory so only dirty membership and the minimum path to it are newly allocated. Keep `LoopPass` as the compatibility operation record.
- [ ] 2.3 Implement candidate construction from a const active snapshot and an existing semantic mutation with persistent progress, bounded units, atomic publication, and rollback.
- [ ] 2.4 Verify the architectural invariants with existing native gates plus narrowly scoped checks only: snapshot completeness, unchanged-object sharing, changed-object replacement, non-note preservation, older-snapshot stability, and selection identity. Do not add a new test architecture.

## 3. Capture and edit publication

Blocked until section 1 is complete and dirty-bar construction has a declared `ExecutionBounds.md` class. A long note can still touch every intersecting bar; this gate does not treat that as already bounded.

- [ ] 3.1 Adapt `Loop::commitCapturePass` at its existing boundary to publish one global `LoopSnapshot` per record or overdub session-exit without changing wrap boundaries. Intermediate record prefixes and overdub wraps remain provisional.
- [ ] 3.2 Adapt NOTE_EDIT close/commit to publish one global `LoopSnapshot` for a settled edit batch. In-session batch snapshots remain provisional `E:` revisions.
- [ ] 3.3 Retain compatibility pass records. Use existing verification gates plus invariant-protecting parity checks for overlap, LIFO pairing, wrap pairs, equal-tick ordering, and non-note events.
- [ ] 3.4 Before runtime publication, classify each named candidate-construction operation in `ExecutionBounds.md`; define the unit cap, maximum indivisible step, progress state, completion condition, and explicit overrun/failure policy. Do not introduce content-sized construction in `handleMidiInput()`, clock dispatch, or track-tick paths.

## 4. Direct consumers

- [ ] 4.1 Make playback preparation read the active snapshot directly and use snapshot identity for freshness while retaining safe fallback; select the bounded playback span range before applying projection and keep `playbackEventPhase` scalar in the hot path.
- [ ] 4.2 Preserve `Track` / `LoopPlaybackRuntime` ownership of cursor, order, ledger, and horizon state; verify output with existing playback gates.
- [ ] 4.3 Convert `LoopContentResolution` to stateless explicit-snapshot queries without parent traversal or maintained replay.
- [ ] 4.4 Migrate display, NOTE_EDIT, overdub analysis, and validation callers independently with rollback gates; select bounded canonical span ranges before `IntervalProjection` and define long-span `k` candidate limits or empirical overrun policies as consumer bounds, not snapshot-architecture units.
- [ ] 4.5 Verify record, overdub, wrap, short/long loop, slot, load, undo/redo, and active-capture separation with existing HITL and native gates.
- [ ] 4.6 Update each migrated derived representation to use snapshot identity for source freshness while retaining its existing owner, invalidation, bounded build, and stale/fallback policy; verify no full-loop rebuild is introduced on a timing-critical path.

## 5. Undo, load, and persistence

- [ ] 5.1 Make `TrackUndo` and clear-slot restore select before/after snapshot identities while preserving strict LIFO, runtime-only undo/redo, and redo-branch clearing after new work.
- [ ] 5.2 Approve the physical storage version/layout for immutable objects, the latest saved snapshot, metadata, and local handle rebuild. Do not persist undo history or post-reboot undo lineage.
- [ ] 5.3 Implement the new content-only storage reader/writer through existing `StorageManager` deferred units and compatibility migration with rollback behavior; classify any interval-path unit in `ExecutionBounds.md` and keep runtime addresses and derived representations out of the format.
- [ ] 5.4 Verify save/load round trips, selected-snapshot equivalence, clear-slot restore, and old-format migration with existing persistence gates plus invariant-protecting checks. After load, undo depth is 0 and redo is empty.
- [ ] 5.5 Verify deferred persistence remains owned by `StorageManager` without blocking playback or `handleMidiInput()`.
- [ ] 5.6 Verify large-loop save and migration progress, overrun/failure preservation, active-snapshot availability, and absence of content-sized synchronous work at stop, input dispatch, clock dispatch, and track-tick paths.

## 6. Compatibility retirement and closeout

- [ ] 6.1 Prove no normal consumer depends on maintained LCR history, `passesMaterializedStore_`, or full replay before removing each path.
- [ ] 6.2 Remove superseded maintained-LCR state and replay APIs only after caller migration and rollback gates pass.
- [ ] 6.3 Remove compatibility effective-state derivation and old readers only after documented triggers close.
- [ ] 6.4 Run capture-serial firmware build first with RAM1 locals at least 4000, then `pio test -e native`, followed by required existing HITL verification.
- [ ] 6.5 Update runtime docs, deliverable tracking, the refinement plan, and archived specs; sync and archive only after all gates pass.
