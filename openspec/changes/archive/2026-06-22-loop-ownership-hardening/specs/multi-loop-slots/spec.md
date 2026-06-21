## ADDED Requirements

### Requirement: Slot loopId resolves without silent alias

Resolving a **Slot** to its **Loop** SHALL NOT fall back to pool index 0 when **loopId** is unknown
or invalid. Callers SHALL use the slot's pool index when **loopId** is **kInvalidLoopId** or fails
validation.

#### Scenario: Unknown loopId uses pool index

- **WHEN** **slots_[s].loopId** is not found in **LoopPool**
- **THEN** **loopForSlot(s)** returns **loopPool_.at(s)**
- **AND** no other slot's **Loop** is returned

#### Scenario: Valid loopId uses findById

- **WHEN** **slots_[s].loopId** equals pool index id for slot s (1:1 stable ids)
- **THEN** **loopForSlot(s)** returns the **Loop** at that pool index

### Requirement: SD load repairs invalid slotLoopId

When loading v4 state, if a persisted **slotLoopId** is outside `0 .. MAX_LOOPS_PER_TRACK-1`, the
loader SHALL rewrite it to the pool index for that slot and continue load.

#### Scenario: Corrupt slotLoopId repaired

- **WHEN** SD contains **slotLoopId** = 0xFFFFFFFF for track T slot S
- **THEN** after load **slots_[S].loopId** = S (stable 1:1 id)
- **AND** load completes successfully
- **AND** a warning is logged
