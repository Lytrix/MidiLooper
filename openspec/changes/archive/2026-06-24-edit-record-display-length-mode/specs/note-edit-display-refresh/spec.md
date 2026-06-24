# note-edit-display-refresh Specification (delta)

## Purpose

During NOTE_EDIT, the OLED sidebar (NOTE / VEL / **LEN**) and piano roll SHALL reflect the edit RAM store within bounded latency after move, pitch, or length mutations.

## ADDED Requirements

### Requirement: Sidebar length matches store after edit

After a successful position, pitch, or length edit mutation that calls `Track::invalidateCaches()`, the displayed LEN field SHALL match the reconstructed span of the selected note within one `DisplayManager::update()` cycle.

#### Scenario: Length edit updates LEN

- **WHEN** the user lengthens the selected note in length-edit mode and the store records a new end tick
- **THEN** the sidebar LEN value SHALL update on the next display frame without requiring fader 1 reselect
- **AND** `#CAP DNTE` length field (when capture enabled) SHALL match within tick tolerance

#### Scenario: Position move updates bracket and LEN

- **WHEN** the user moves the selected note start in position-edit mode
- **THEN** the piano roll bracket and sidebar NOTE/LEN fields SHALL reflect the new start and unchanged gate length on the next display frame

### Requirement: DNTE emission on store mutation

The firmware SHALL emit `#CAP DNTE` (or equivalent note-info capture) when the selected note's storage start or length changes due to edit mutation, not only on fader 1 select events.

#### Scenario: Post-move DNTE

- **WHEN** `Moved note events` is logged for the selected note
- **THEN** a DNTE capture line with matching storage start and length SHALL appear before the next user fader action (≤200 ms at default loop rates)

## MODIFIED Requirements

*(none — new capability)*
