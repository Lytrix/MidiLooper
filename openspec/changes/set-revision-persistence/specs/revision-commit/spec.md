## ADDED Requirements

### Requirement: Save commits immutable packed Revision

The system SHALL provide a **Save** action that commits **Current** into a new immutable packed
revision file (`v####.bin`) per `revision-packed-blob` spec. Commit SHALL reuse the existing
**deferred persistence FSM** infrastructure — not a separate queue worker.

#### Scenario: First save creates Set and v0001

- **WHEN** the user selects **Save**
- **AND** Current has `derivedFromSetId == 0`
- **THEN** the system allocates `S####` and writes packed `revisions/v0001.bin` after VALIDATE
- **AND** Current provenance updates at COMPLETE; Current remains mutable

#### Scenario: Subsequent save increments revision

- **WHEN** the user selects **Save**
- **AND** Set S0003 has `revisionCount == 8`
- **THEN** packed `revisions/v0009.bin` is written via deferred commit FSM
- **AND** `set.bin` updates only after VALIDATE + CATALOG_UPDATE

#### Scenario: Save exits overlay immediately

- **WHEN** the user confirms **Save** from the overlay
- **THEN** revision commit enters **REQUEST** stage
- **AND** the overlay closes before COMPLETE
- **AND** MIDI capture and playback continue unaffected

### Requirement: Revision commit uses deferred persistence FSM

Revision commits SHALL reuse `requestDeferredSaveState()` / `processDeferredSaveState()` (or equivalent
commit hook on the same FSM). Stages:

| Stage | Purpose |
|-------|---------|
| **REQUEST** | User or failsafe requests commit; non-blocking |
| **SNAPSHOT** | Freeze epoch boundary (see snapshot requirement) |
| **WRITE** | Chunk-bounded SD write of packed `v####.bin.tmp` |
| **VALIDATE** | CRC + complete magic |
| **CATALOG_UPDATE** | Update `set.bin` / `index.bin` |
| **COMPLETE** | Sync `lastCommittedEpoch`; clear dirty |

Commit completion SHALL occur only after **VALIDATE** succeeds. Failed commits SHALL NOT update
catalog or consume visible revision ids.

#### Scenario: Save during recording defers without blocking capture

- **WHEN** the user presses **Save** while a track is **RECORDING**
- **THEN** commit **REQUEST** is accepted
- **AND** commit advances incrementally in **WRITE** slices between MIDI work
- **AND** recording continuity is unaffected (no capture lock)

### Requirement: Save snapshots stable epoch boundaries

**Save** SHALL commit the latest **completed** workspace epoch (`lastCommittedEpoch` target = epoch
that finished all required `MidiLooper/current/` files and completion marker).

Runtime changes that begin after **SNAPSHOT** starts SHALL belong to the **next** epoch and SHALL NOT
be included in the revision being written.

#### Scenario: Overdub after snapshot excluded from revision

- **WHEN** epoch 120 is complete and commit **SNAPSHOT** begins for Save
- **AND** a new overdub starts before **WRITE** finishes
- **THEN** the saved revision reflects epoch 120 only
- **AND** new capture belongs to epoch 121

### Requirement: Persistence is budget-limited

Persistence work (Current epoch writes and revision **WRITE** stages) SHALL execute in bounded
slices controlled by `maxPersistenceMicros`:

- During **RECORDING** or **OVERDUBBING**: typically 100–300 µs per slice (configurable)
- When transport idle and capture inactive: unbounded per idle maintenance tick

Budget exhaustion SHALL defer remaining work to the next FSM tick. The system SHALL NOT borrow
future budget or block MIDI/playback/clock to finish persistence.

#### Scenario: Commit yields when budget exhausted during playback

- **WHEN** revision **WRITE** runs during **PLAYING**
- **AND** the slice reaches `maxPersistenceMicros`
- **THEN** WRITE pauses until the next deferred tick
- **AND** playback timing is unchanged

### Requirement: Revision files are never mutated

Once committed, `v####.bin` SHALL NOT be modified. See `revision-packed-blob` for layout.

#### Scenario: Load does not alter source revision

- **WHEN** the user loads revision `v0007` into Current
- **THEN** `v0007.bin` on SD is unchanged

### Requirement: Save SHALL not move or clear Current

Save SHALL NOT move Current, clear Current, or reload Current. Commit flow: current → snapshot →
revision → validate → update catalog.

#### Scenario: Recording continues during Save

- **WHEN** recording is active and Save is requested
- **THEN** revision is generated incrementally via deferred FSM
- **AND** recording continues uninterrupted
