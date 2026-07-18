## ADDED Requirements

### Requirement: Slot select may request hydration

Selecting or focusing a loop slot that still needs SD load SHALL request slot hydration so the slot can reach COMMITTED. Interactive ready at boot SHALL NOT require every slot on every track to be COMMITTED.

#### Scenario: Focus unloaded slot requests load

- **WHEN** the user focuses a slot that `needsSlotLoad` reports as needing SD
- **AND** transport is idle (or the request is queued per lazy-slot-hydration MVP rules)
- **THEN** deferred or sync restore is requested for that track/slot
- **AND** after Commit the slot has committed passes available for playback and display

### Requirement: Boot interactive ready is audible-scoped

`bootInteractiveReady` (or its successor) SHALL become true when the audible boot set is COMMITTED and boot load work for that set is complete — not when the entire SD payload set is COMMITTED.

#### Scenario: Ready with unloaded non-audible slots

- **WHEN** audible slots are COMMITTED after boot
- **AND** other SD payload slots remain unloaded
- **THEN** boot interactive ready is true
- **AND** finish-boot USB / piano-roll entry MAY proceed
