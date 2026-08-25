## Why

The current loop model derives effective musical state by replaying capture passes and edit rows. It also carries a maintained `LoopContentResolution` representation, duplicating state and making playback, undo, and consumer freshness depend on reconstruction. This change defines complete immutable snapshot passes so the active loop state can be selected directly without continuing the maintained-LCR architecture.

## What Changes

- Introduce complete immutable `LoopPass` snapshots containing structurally shared references to immutable note and non-note MIDI event objects.
- Keep `Loop` / `LoopPasses` as the snapshot publication and storage boundary.
- Keep capture finalization, overlap, note pairing, and NOTE_EDIT geometry semantics in their existing owners.
- Make undo, redo, and clear-slot restore select existing snapshot identities rather than replay history.
- Migrate playback, display, NOTE_EDIT, analysis, persistence, and load consumers in separately gated stages.
- **BREAKING**: historical replay and maintained LCR state become compatibility-only and are retired after caller migration gates pass.
- **BREAKING**: a future storage version is required for immutable objects and snapshot structure; its physical layout is a separate design.

## Capabilities

### New Capabilities

- `immutable-loop-snapshots`: Complete immutable loop snapshots, stable loop-scoped references, candidate publication, lifetime, and direct consumer selection.

### Modified Capabilities

- `timeline-passes`: Complete immutable snapshots become the normal effective-state representation; pass records remain compatibility data during migration.
- `loop-content-history`: Runtime undo/redo selects snapshots, and future persistence serializes immutable objects and snapshot structure without persisting undo history.

## Impact

- Affects `Loop` / `LoopPasses`, playback preparation, `TrackUndo`, `StorageManager`, NOTE_EDIT and display consumers, and `LoopContentResolution`.
- Requires native parity and lifetime tests, then firmware RAM1, native, and HITL gates for implementation phases.
- Open decisions: persistent reference structure, reclamation, storage layout/version and migration, optional indexes, bounded consumer span access, candidate construction bounds, long-span projection candidate limits, and per-phase overrun behavior.
- No change is authorized here to horizon size, projection math, cursor or ledger semantics, capture finalization, overlap ownership, NOTE_EDIT geometry ownership, or interval reservation.
- Any runtime candidate construction must first be classified against
  [`ExecutionBounds.md`](../../docs/Authority/Architecture/ExecutionBounds.md):
  a fixed-cap resumable unit, or an empirically bounded unit with an explicit
  overrun guard and failure policy. “Off-clock” or deferred placement alone is
  not an execution bound.
- `LoopSnapshot` stores canonical immutable content, not projection-specific
  intervals or consumer windows. Large-loop consumers must select bounded span
  ranges before calling `IntervalProjection`; a complete snapshot does not
  authorize full-loop projection on a timing-critical path.
