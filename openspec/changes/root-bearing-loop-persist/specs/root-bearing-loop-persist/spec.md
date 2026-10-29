# Spec delta — root-bearing-loop-persist

## ADDED Requirements

### Requirement: A deferred loop-body write waits, bounded, for a publication owed for the current committed state

The deferred loop-body write SHALL treat a snapshot publication that is **owed for the loop's current committed
state** as a transient settle condition, whether or not the selected root is already coverage-current. The wait SHALL
be bounded by a wall-clock window and SHALL write no bytes and leave the loop-write stage unchanged while it waits.
After the window expires the write SHALL proceed with the passes-authoritative body.

#### Scenario: Publication owed for the committed state is waited for

- **WHEN** a loop with committed passes has a snapshot publication in flight for that same committed state and its
  selected root is one revision behind
- **AND** the deferred save reaches the loop write's `Header` stage inside the settle window
- **THEN** `decideLoopPersistBodyFormat` returns `AwaitSettled`
- **AND** no bytes are written and the loop-write stage stays `Header`
- **AND** the slot temp file is not left open across the wait

#### Scenario: The same state settles inside the window

- **WHEN** the owed publication completes before the window expires
- **THEN** the next writer turn selects the root-bearing (LSV1) body
- **AND** the persisted body passes the loader's root-equivalence gate without repair

#### Scenario: An uncovered root with nothing owed never waits

- **WHEN** the selected root is not coverage-current and no publication is owed
- **THEN** the decision is `PassesAuthoritative` immediately, regardless of remaining budget

#### Scenario: The window expires

- **WHEN** the publication is still owed after the window
- **THEN** the decision is `PassesAuthoritative`
- **AND** the body is written and the record from the next requirement is set

### Requirement: A persisted body without a root is re-persisted once that slot's root settles

The persistence owner SHALL record, per slot, that the loop body it finalized carries no root. While that record is
set and the slot's root is settled, the owner SHALL re-admit exactly that slot through the existing
`markLoopSlotMaterialDirty` + `admitLoopSlotPersist` + `requestDeferredSaveState` path and clear the record. It SHALL
NOT admit anything while the slot has no settled root, and SHALL drop the record when the slot no longer holds
committed passes.

#### Scenario: Rootless body is re-written with its root

- **WHEN** a slot with committed passes is saved passes-authoritative
- **THEN** the slot's root-rewrite record is set and no second save is queued
- **AND** when the slot's root becomes settled, one `LoopPersist` admission is made for that slot and the record is
  cleared
- **AND** the re-written body carries the root and the reloaded slot reports a current root

#### Scenario: No settled root, no admission

- **WHEN** a slot's root-rewrite record is set but the slot has no settled root
- **THEN** no persistence work is admitted for it and the record stays set

#### Scenario: A root-bearing body clears the record

- **WHEN** a slot's body is finalized with an embedded root
- **THEN** the record for that slot is clear

#### Scenario: Content removed

- **WHEN** a flagged slot no longer holds committed passes
- **THEN** the record is dropped and no write is admitted

### Requirement: The writer's root-equivalence admission is unchanged

Nothing in this capability may widen the writer's admission: a body SHALL embed a root only when that root is proven
equivalent to the passes being serialized, using the loader's own comparator.

#### Scenario: Divergent root is still written passes-authoritative

- **WHEN** the settled root's committed event stream is not equivalent to the passes about to be serialized
- **THEN** the body is written passes-authoritative, exactly as before this change
