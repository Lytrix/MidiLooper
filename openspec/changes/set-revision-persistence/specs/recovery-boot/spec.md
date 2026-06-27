## ADDED Requirements

### Requirement: Current remains recovery source

On boot the system SHALL attempt recovery in this order:

1. **Current workspace** — highest valid epoch under `MidiLooper/current/`
2. **Derived revision** — exact `derivedFromRevisionId` on Set `derivedFromSetId`
3. **Latest validated revision** on that Set
4. **Recovery checkpoints** — `MidiLooper/recovery/checkpoints/`
5. **Empty** Current

Current workspace remains the primary recovery source. Partial epochs SHALL be ignored.

Loading a Set SHALL populate Current via deferred load. Set files SHALL remain immutable.

Flow: `sets → load → current`

Incomplete revision `.tmp` files under `MidiLooper/sets/` SHALL be discarded during boot hygiene.

#### Scenario: Valid Current epoch wins over revision

- **WHEN** `MidiLooper/current/` epoch 41 validates on boot
- **AND** `derivedFromRevisionId == 9` on Set `S0003`
- **THEN** the system loads Current epoch 41 into RAM
- **AND** does not load `v0009.bin`

#### Scenario: Corrupt Current loads exact derived revision

- **WHEN** no valid Current epoch exists
- **AND** `workspace.bin` reports `derivedFromSetId == S0003`, `derivedFromRevisionId == 9`
- **AND** `S0003/revisions/v0009.bin` validates
- **THEN** the system loads `v0009` into Current and RAM
- **AND** rebuilds `MidiLooper/current/` via deferred persistence

#### Scenario: Exact derived revision missing falls forward to latest

- **WHEN** no valid Current epoch
- **AND** `derivedFromRevisionId == 9` but `v0009.bin` is invalid
- **AND** `v0010.bin` is latest validated on `S0003`
- **THEN** the system loads `v0010`

#### Scenario: Fresh device with no valid data

- **WHEN** steps 1–4 all fail
- **THEN** boot starts with empty Current

### Requirement: Eight-hour failsafe commits silent revision

The system SHALL retain an eight-hour idle failsafe that requests revision commit when:

- `lastCommittedEpoch != currentEpoch` (workspace dirty)
- Last validated commit is older than 8 hours (including never committed since boot)
- Transport idle and capture inactive

Failsafe SHALL use the same deferred commit FSM as explicit Save.

#### Scenario: Failsafe creates revision without user gesture

- **WHEN** `currentEpoch > lastCommittedEpoch` for ≥ 8 hours without commit **COMPLETE**
- **AND** transport is idle and capture is inactive
- **THEN** a new validated revision is appended on the derived Set (or new Set if never saved)
