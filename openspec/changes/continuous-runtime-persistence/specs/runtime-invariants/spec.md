# runtime-invariants

Normative architectural invariants for continuous capture-chunk persistence. Guide: [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](../../../../docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md).

## ADDED Requirements

### Requirement: Six core invariants

Continuous runtime persistence SHALL satisfy:

1. Runtime ownership is independent of persistence state.
2. A sealed chunk is immutable.
3. Persistence is cooperative and budget-driven.
4. Persistence preserves chunk seal order.
5. Runtime playback and recording always take precedence over persistence.
6. Memory reclamation is independent of persistence completion.

#### Scenario: Sealed chunk readable during capture

- **WHEN** a capture pass is open and one or more chunks are sealed
- **THEN** playback merge MAY read sealed chunks regardless of whether those chunks are persisted
- **AND** persistence SHALL NOT mutate sealed chunk contents in RAM

#### Scenario: Persisted chunk remains allocated while referenced

- **WHEN** a chunk is persisted to SD
- **AND** `LoopPasses`, playback, or an undo snapshot still holds a reference
- **THEN** the chunk SHALL remain allocated in the pool
- **AND** the chunk SHALL NOT return to the free list until all runtime owners release references

### Requirement: Runtime precedence governing rule

Runtime recording and playback correctness SHALL always take precedence over persistence progress.

#### Scenario: Persistence yields to runtime work

- **WHEN** MIDI clock, note output, or playback servicing is due in the same main-loop iteration as a persistence slice
- **THEN** runtime work SHALL complete first
- **AND** persistence SHALL perform at most one bounded slice before yielding

#### Scenario: Playback timing not delayed for SD

- **WHEN** persistence is behind (non-empty queue)
- **THEN** playback timing SHALL NOT be delayed solely to increase SD write throughput

### Requirement: Pass ownership independent of storage ownership

Pass ownership (`Loop`, `LoopPasses`) and storage ownership (persistence subsystem) SHALL remain independent.

#### Scenario: Open pass with persisted chunks

- **WHEN** a capture pass is open
- **AND** one or more sealed chunks within the pass are persisted
- **THEN** the pass MAY remain open
- **AND** pass-close SHALL finalize pass metadata without being the first gate for SD persistence

### Requirement: Capture-pass scope only

Continuous runtime persistence SHALL apply only to append-only capture-pass storage.

Edit passes, undo snapshots, and other mutable runtime structures SHALL continue using the existing deferred persistence model unless explicitly extended by a future proposal.

#### Scenario: Note edit unchanged

- **WHEN** a note edit session is active (`NoteEditSession`, `editPasses[]`)
- **THEN** note-edit persistence behavior SHALL remain on the existing deferred path
- **AND** continuous capture-chunk persistence SHALL NOT alter note-edit save semantics in v1
