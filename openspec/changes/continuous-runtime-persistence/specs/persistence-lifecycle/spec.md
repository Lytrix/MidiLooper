# persistence-lifecycle

Persistence work-item lifecycle for sealed capture chunks. Owner: persistence subsystem (`StorageManager`, target).

## ADDED Requirements

### Requirement: Persistence states per work item

Persistence progress for a sealed chunk SHALL be tracked as: `Not scheduled` → `Queued` → `Writing` → `Persisted`.

Persistence state SHALL NOT replace runtime `Free` / `Recording` / `Sealed` state on the chunk.

#### Scenario: Queued while pass open

- **WHEN** a sealed chunk is admitted to the persistence queue during an open capture pass
- **THEN** the chunk's runtime state SHALL remain `Sealed`
- **AND** persistence state SHALL be `Queued` or later

### Requirement: Seal-order queue admission

Sealed chunks SHALL enter the persistence queue in seal order.

#### Scenario: Mid-pass seal order preserved

- **WHEN** chunks A, B, C seal in that order during one open pass
- **THEN** queue admission order SHALL be A, B, C

### Requirement: Seal-order persistence completion

Persistence SHALL complete chunks in seal order within a pass.

#### Scenario: Drain order matches seal order

- **WHEN** multiple sealed chunks are queued for the same pass
- **THEN** the storage backend SHALL write chunk A before B before C
- **AND** recovery SHALL treat the longest valid prefix in that order

### Requirement: Exactly-once queue admission

A sealed chunk SHALL enter the persistence queue exactly once.

#### Scenario: No duplicate enqueue on re-seal check

- **WHEN** a chunk is already `Queued`, `Writing`, or `Persisted`
- **THEN** seal or scheduler paths SHALL NOT enqueue it again

#### Scenario: Re-queue only after explicit invalidation

- **WHEN** a persisted chunk is invalidated by a future copy-on-write or format migration (out of v1 scope)
- **THEN** re-admission SHALL be explicit and documented in a future change
