## 1. Architecture and specification gate

Phase 0 is documentation only. Later sections are blocked until 1.1–1.8 are complete.

- [ ] 1.1 Complete formal architecture reassessment for immutable snapshots, including ownership, transitions, execution bounds, storage, and compatibility removal.
- [ ] 1.2 Record the approved decision in `docs/DECISION_LOG.md` and update the refinement plan, parent runtime architecture, derived-representation authority, storage authority, and naming with the accepted `LoopPass` / `LoopSnapshot` boundary.
- [ ] 1.3 Keep this change selected in `docs/Runtime/CURRENT_WORK.md` as Phase 0 only and post the phase architecture gate before any firmware edits.
- [ ] 1.4 Complete `PREFLIGHT.md` / `DECISION_REVIEW.md` for each later ownership or stop-path implementation phase. Do not start those phases in Phase 0.
- [x] 1.5 Record the persistent reference-root invariant: successor snapshots reuse unchanged event-object references, allocate new objects only for added or modified content, and must not copy the complete reference set.
- [ ] 1.6 Compare a flat reference vector, a persistent chunked / reference-directory structure, and a path-copy ordered tree against update cost, bounded range iteration, Teensy allocation behavior, handle identity, serialization shape, and the maximum indivisible construction step. Record the measurement evidence required before selecting one. Do not treat a structure as selected in Phase 0.
- [x] 1.7 Specify snapshot reachability/reclamation, provisional session snapshots versus global undo snapshots, redo-branch clearing, and latest-saved-snapshot reboot behavior.
- [x] 1.8 Name the candidate-construction operations and require a declared complexity / `ExecutionBounds.md` class for each before runtime publication. Treat any numeric N-candidate quantum as a measurement hypothesis only.

## 2. Immutable snapshot model

Blocked until section 1 is complete and a later approved phase selects the persistent reference structure.

- [ ] 2.1 Define immutable note and non-note event objects with loop-scoped stable references, a declared execution-bound class, a memory allocation contract, and canonical span access for projection consumers.
- [ ] 2.2 Implement the selected structurally shared reference storage and complete immutable `LoopSnapshot` records under `Loop` / `LoopPasses`. Keep `LoopPass` as the compatibility operation record.
- [ ] 2.3 Implement candidate construction from a const active snapshot and an existing semantic mutation with persistent progress, bounded units, atomic publication, and rollback.
- [ ] 2.4 Verify the architectural invariants with existing native gates plus narrowly scoped checks only: snapshot completeness, unchanged-object sharing, changed-object replacement, non-note preservation, older-snapshot stability, and selection identity. Do not add a new test architecture.

## 3. Capture and edit publication

Blocked until section 1 is complete and the selected representation plus construction bounds are recorded.

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
