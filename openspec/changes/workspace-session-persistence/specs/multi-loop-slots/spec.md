## ADDED Requirements

### Requirement: LoopLocation identifies slot address

The system SHALL provide **LoopLocation** with `uint8_t track` and `uint8_t slot` for slot-level
operations including loop import. Slots remain the primary musical interaction unit; track is
structural routing.

#### Scenario: Import resolves LoopLocation to Loop

- **WHEN** import targets **LoopLocation** `{ track, slot }`
- **THEN** firmware resolves `Track::loopForSlot(slot)` on the given track
- **AND** applies imported loop data to that **Loop** instance

### Requirement: Slot is loop import target

IMPORT LOOP SHALL replace only the **Loop** data at the target **LoopLocation**. Slot metadata
(enabled, muted, loopId) on other slots SHALL remain unchanged by import.

#### Scenario: Import does not clear sibling slots

- **WHEN** loop is imported into track T slot S
- **THEN** other slots on track T retain their previous loop content
- **AND** slots on other tracks are unchanged

## MODIFIED Requirements

### Requirement: SD load repairs invalid slotLoopId

The loader SHALL rewrite persisted **slotLoopId** values outside `0 .. MAX_LOOPS_PER_TRACK-1`
to the pool index for that slot and continue load when loading CurrentSet or SavedSet meta.

#### Scenario: Corrupt slotLoopId repaired in CurrentSet meta

- **WHEN** `Sets/_current/meta.bin` contains **slotLoopId** = 0xFFFFFFFF for track T slot S
- **THEN** after load **slots_[S].loopId** = S (stable 1:1 id)
- **AND** load completes successfully
- **AND** a warning is logged
