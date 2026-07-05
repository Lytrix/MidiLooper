## ADDED Requirements

### Requirement: Selected slot index is UI and editor focus

Each track SHALL maintain a **selected slot index** (`selectedSlotIndex`) distinct from **active loop index** (`activeLoopIndex`):

- **`selectedSlotIndex`** — UI/editor focus: piano roll, loop row LEDs, edit session binding, loop geometry edit target
- **`activeLoopIndex`** — playback/capture focus: audible slot, record/overdub target, `getActiveLoop()`

Normal user interaction MAY synchronise both via explicit caller policy. Workflows MAY intentionally keep them different (multi-slot playback, audition, **`finalizeCaptureAndSelectSlot`** after capture).

#### Scenario: Display follows selected slot

- **WHEN** `selectedSlotIndex != activeLoopIndex` during multi-slot playback
- **THEN** the piano roll and loop edit UI show the **selected** slot's loop data
- **AND** playback continues from the **active** slot until caller policy changes active index

#### Scenario: Edit session binds to selected slot

- **WHEN** loop edit or note edit is active
- **THEN** edit operations and session rematerialisation target the **selected** slot's `Loop`
