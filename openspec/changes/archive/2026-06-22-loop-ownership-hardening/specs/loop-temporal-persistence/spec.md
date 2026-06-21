## ADDED Requirements

### Requirement: startLoopTick round-trip on SD load

When a loop snapshot is written with non-zero **startLoopTick**, loading that snapshot SHALL
restore the same **startLoopTick** on the in-memory **Loop**.

#### Scenario: Save and load preserve phase origin

- **WHEN** a loop is saved with **startLoopTick** = N (N > 0)
- **AND** state is loaded from SD v4
- **THEN** the loaded loop has **startLoopTick** = N

#### Scenario: Zero startLoopTick remains valid

- **WHEN** a loop is saved with **startLoopTick** = 0
- **AND** state is loaded
- **THEN** the loaded loop has **startLoopTick** = 0

### Requirement: Deferred full validate eventually completes

When **finalizeLoopAtStop** schedules deferred full MIDI validation for a track, the system
SHALL run **validateAndCleanupMidiEvents** for that track within **Config::deferredValidateMaxDelayMs**
wall-clock time, even if transport remains in **PLAYING**.

#### Scenario: Continuous playback still gets validate

- **WHEN** a track finishes overdub stop with deferred validate queued
- **AND** transport stays in **PLAYING** for longer than **deferredValidateMaxDelayMs**
- **THEN** **validateAndCleanupMidiEvents** runs once for that track
- **AND** **deferredFullMidiValidate** is cleared

#### Scenario: Active capture defers validate

- **WHEN** any track is **RECORDING** or **OVERDUBBING**
- **THEN** deferred full validate does not run on that track's slot during capture
