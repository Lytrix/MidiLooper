## ADDED Requirements

### Requirement: Arrangement capture mode records into a target slot

The system SHALL provide an arrangement capture mode that writes performance events
into a designated `recordTargetSlot` while global transport advances on `currentTick`.

#### Scenario: Loop button switch during capture is stored

- **WHEN** arrangement capture is active on track T with target slot S
- **AND** the user presses a loop button to switch the audible/view slot
- **THEN** the switch event is captured into slot S's timeline at `currentTick`
- **AND** capture continues until the user stops arrangement recording

### Requirement: View slot may differ from capture target during arrangement

During arrangement capture, the system SHALL allow the **view/playback slot** to differ
from `recordTargetSlot` per phase-3 browse-while-recording rules.

#### Scenario: Browse slot B while capturing into slot A

- **WHEN** capture target is slot A and the user selects slot B for view/playback
- **THEN** display and jam playback follow slot B
- **AND** captured MIDI remains attributed to slot A

### Requirement: Merge model must be declared before playback implementation

The system SHALL document whether captured arrangement data uses model **A** (flattened
MIDI stream at `currentTick`) or model **B** (structural references to source events)
before arrangement playback (D14) is implemented.

#### Scenario: Spec locks R1 before replay code

- **WHEN** implementation of arrangement playback begins
- **THEN** the chosen merge model is recorded in the change design artifact
- **AND** native tests assert replay behavior for that model

## MODIFIED Requirements

### Requirement: Per-slot record and overdub lifecycle

Each slot SHALL support record/overdub/clear semantics scoped to that slot's `Loop`,
**and** MAY enter arrangement capture mode where `recordTargetSlot` is explicit and
may differ from the view slot.

#### Scenario: Arrangement mode does not replace per-slot overdub

- **WHEN** the user performs a normal per-slot overdub on slot S
- **THEN** behavior matches existing overdub lifecycle without arrangement routing
- **AND** arrangement capture is only active when explicitly entered (D13)
