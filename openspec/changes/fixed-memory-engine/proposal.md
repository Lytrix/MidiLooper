## Why

The intense overdub run on the DEC-251 image (`captures/session_20261104_190805.log`) survived PSRAM exhaustion but
left Stop in `AtomicTransfer` for 176 s and blanked the piano roll. Today's content model copies each note into 6–7
growable derived containers (about 240 B of PSRAM per accepted note), allocates after Stop has been accepted, and
exhausts two heaps independently. Local containment cannot bound it. The owner ruled a direct cutover to one fixed-memory
engine (2026-11-04, revisions 2 and 3 of the plan).

## What Changes

- One `ContentEngine` becomes the only owner of MIDI content and edit history:
  - fixed PSRAM pages, immutable runs, versioned manifests;
  - global pooled run/manifest/history tables;
  - row reservations for every accepted obligation;
  - bounded workers.
- Playback consumes qualified three-window rings. The piano roll draws from fixed span/density tiles.
- Overlap consequences are derived at composition and are not stored.
- **BREAKING:** every legacy content owner is deleted, with no coexistence, dual-writing, fallback reader or transitional
  route. The legacy owners are `LoopEventStore`, capture stagings, `LoopSnapshot`, `PendingNoteChange`, `EditPasses`,
  the stop-job companion phases, `LoopWrapObligation`, `CheckedCapacity`, `visualCache` and tile preparation.
- Development happens on `rewrite/fixed-memory-engine`; intermediate firmware may be RED or unbootable.

Architecture and budgets: [plan revision 3](../../../docs/Plans/2026-11-04-fixed-memory-convergence.md). Decision:
DEC-252 (one bounded content engine).

## Capabilities

### New Capabilities
- `content-engine`: fixed-capacity content storage, reservation accounting, derived effective content, bounded workers,
  window and tile consumer contracts.

### Modified Capabilities
- Superseded mechanisms are listed in DEC-252 and plan § 12.2; their musical clauses move to the E0 contract register.

## Impact

- New: `include/ContentEngine/`, `test/test_content_engine_core/`, `test/test_engine_semantic_oracle/`.
- Deleted at E9: the legacy owners above and their mechanism tests, after E0 captures the musical clauses they protect.
- Persistence: V1 files import. The save format is an owner decision. The device card is read-only until the owner
  approves saving.
