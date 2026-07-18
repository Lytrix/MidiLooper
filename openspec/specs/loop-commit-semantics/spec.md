## Purpose

Architectural **Commit** makes newly constructed immutable loop state runtime-visible.
Committed state is immutable; derived reconstructions are disposable. Shipped vocabulary and
presence APIs in **unified-commit-lazy-slot-load**.

## Requirements

### Requirement: Commit makes loop state runtime-visible

The system SHALL treat **Commit** as the architectural operation that makes newly constructed immutable loop state runtime-visible. After Commit, consumers (playback, editor, display) SHALL observe only **committed state**. Staging and in-progress load buffers SHALL NOT be consumed as playback truth.

Producers (record, overdub, edit, load, import, paste, undo restore) SHALL satisfy the same Commit semantics. The system SHALL NOT require every producer to call `Loop::commitCapturePass()`.

#### Scenario: Record commit remains via commitCapturePass

- **WHEN** record or overdub stop commits capture into **`passes[]`**
- **THEN** `Loop::commitCapturePass` performs Commit for that producer
- **AND** playback observes the new committed passes

#### Scenario: Load Commit need not call commitCapturePass

- **WHEN** an SD slot load finishes attaching immutable passes for a loop
- **THEN** the slot reaches committed loop state (COMMITTED)
- **AND** the load path MAY use a producer-specific Commit implementation
- **AND** playback SHALL NOT consume partial staging as committed truth

### Requirement: Committed state is immutable; derived is disposable

Committed loop state SHALL NOT be modified in place. Updates SHALL construct a new immutable edition and Commit it. **Derived state** (playback merge windows, editor lookup tables, piano-roll caches, visual geometry, and similar reconstructions) MAY be discarded and rebuilt at any time and SHALL NOT be required for correctness.

#### Scenario: Piano roll before derived caches

- **WHEN** a slot is COMMITTED and derived piano-roll caches are not yet built
- **THEN** display MAY render from committed passes
- **AND** the system SHALL NOT block first interaction solely for DERIVED_READY

### Requirement: Committed-pass presence API

The system SHALL expose a pass-level presence query meaning “this loop has canonical runtime-visible pass content” covering **recordPass**, **overdubPass**, **editPass**, and future committed pass types. After the rename pass, that API SHALL be named with action + scope using **Pass** (for example `hasCommittedPasses`), not Publish/Published and not capture-only wording.

#### Scenario: Empty loop reports no committed passes

- **WHEN** a loop has no rows in **`passes[]`** and no committed chunk refs for passes
- **THEN** the committed-pass presence query is false

### Requirement: Commit vocabulary for committed-truth identifiers

Public identifiers that previously used Publish/Published for committed loop truth SHALL be renamed to Commit/Committed with clear action + scope. New identifiers SHALL NOT contain **Flat**. Transfer helpers SHALL name the object becoming committed (for example `…ToCommittedChunkIds`), not bare `…ToCommitted`.

#### Scenario: Presence API uses Pass scope

- **WHEN** the rename pass completes
- **THEN** call sites that meant published pass presence use the committed-passes API
- **AND** grep for the retired Publish/Published committed-truth family identifiers finds no remaining public uses in product code
