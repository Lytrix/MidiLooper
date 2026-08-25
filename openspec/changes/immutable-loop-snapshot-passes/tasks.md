## 1. Architecture and specification gate

- [ ] 1.1 Complete formal architecture reassessment for immutable snapshots, including ownership, transitions, execution bounds, storage, and compatibility removal.
- [ ] 1.2 Record the approved decision in `docs/DECISION_LOG.md` and update the refinement plan with the accepted boundary.
- [ ] 1.3 Select this change in `docs/Runtime/CURRENT_WORK.md` and post the phase architecture gate before firmware edits.
- [ ] 1.4 Complete `PREFLIGHT.md` / `DECISION_REVIEW.md` for each ownership or stop-path implementation phase.

## 2. Immutable snapshot model

- [ ] 2.1 Define immutable note and non-note event objects with loop-scoped stable references and a measured memory/admission contract.
- [ ] 2.2 Implement structurally shared reference storage and complete immutable snapshot records under `Loop` / `LoopPasses`.
- [ ] 2.3 Implement candidate construction from a const active snapshot and an existing semantic mutation with atomic publication and rollback.
- [ ] 2.4 Add native tests for snapshot completeness, unchanged-object sharing, changed-object replacement, non-note preservation, and older-snapshot stability.
- [ ] 2.5 Add native tests for loop scope, retained roots, clear-slot isolation, and mutable-state aliasing.

## 3. Capture and edit publication

- [ ] 3.1 Adapt `Loop::commitCapturePass` at its existing boundary to publish one snapshot per record or overdub session without changing wrap boundaries.
- [ ] 3.2 Adapt NOTE_EDIT close/commit to publish one snapshot for a settled edit batch.
- [ ] 3.3 Retain compatibility pass records and add parity tests for overlap, LIFO pairing, wrap pairs, equal-tick ordering, and non-note events.
- [ ] 3.4 Keep candidate construction off the MIDI interval path and record an explicit bound and overrun policy.

## 4. Direct consumers

- [ ] 4.1 Make playback preparation read the active snapshot directly and use snapshot identity for freshness while retaining safe fallback.
- [ ] 4.2 Preserve `Track` / `LoopPlaybackRuntime` ownership of cursor, order, ledger, and horizon state; add output parity tests.
- [ ] 4.3 Convert `LoopContentResolution` to stateless explicit-snapshot queries without parent traversal or maintained replay.
- [ ] 4.4 Migrate display, NOTE_EDIT, overdub analysis, and validation callers independently with rollback gates.
- [ ] 4.5 Verify record, overdub, wrap, short/long loop, slot, load, undo/redo, and active-capture separation behavior.

## 5. Undo, load, and persistence

- [ ] 5.1 Make `TrackUndo` and clear-slot restore select before/after snapshot identities while preserving strict LIFO and runtime-only undo/redo.
- [ ] 5.2 Approve the physical storage version/layout for immutable objects, roots, metadata, active tip, and local handle rebuild.
- [ ] 5.3 Implement the new storage reader/writer and off-MIDI-path compatibility migration with rollback behavior.
- [ ] 5.4 Add native tests for round trips, selected snapshot equivalence, grouped undo, clear-slot restore, and old-format migration.
- [ ] 5.5 Verify deferred persistence remains owned by `StorageManager` without blocking playback or `handleMidiInput()`.

## 6. Compatibility retirement and closeout

- [ ] 6.1 Prove no normal consumer depends on maintained LCR history, `passesMaterializedStore_`, or full replay before removing each path.
- [ ] 6.2 Remove superseded maintained-LCR state and replay APIs only after caller migration and rollback gates pass.
- [ ] 6.3 Remove compatibility effective-state derivation and old readers only after documented triggers close.
- [ ] 6.4 Run capture-serial firmware build first with RAM1 locals at least 4000, then `pio test -e native`, followed by required HITL verification.
- [ ] 6.5 Update runtime docs, deliverable tracking, the refinement plan, and archived specs; sync and archive only after all gates pass.
