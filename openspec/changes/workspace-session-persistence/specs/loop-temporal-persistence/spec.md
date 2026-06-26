## MODIFIED Requirements

### Requirement: startLoopTick round-trip on SD load

When a loop snapshot is written with non-zero **startLoopTick**, loading that snapshot SHALL
restore the same **startLoopTick** on the in-memory **Loop**. This applies to per-file CurrentSet
loads, SavedSet loop files, and v5 monolith migration.

#### Scenario: Save and load preserve phase origin from CurrentSet file

- **WHEN** a loop is written to `Sets/_current/loop_01_02.bin` with **startLoopTick** = N (N > 0)
- **AND** the loop file is loaded on boot
- **THEN** the loaded loop has **startLoopTick** = N

#### Scenario: Zero startLoopTick remains valid in per-file load

- **WHEN** a per-file loop blob has **startLoopTick** = 0
- **AND** state is loaded from CurrentSet
- **THEN** the loaded loop has **startLoopTick** = 0

### Requirement: Deferred full validate eventually completes

The system SHALL run **validateAndCleanupMidiEvents** for a track within
**Config::deferredValidateMaxDelayMs** wall-clock time when **finalizeLoopAtStop** schedules
deferred full MIDI validation, even if transport remains in **PLAYING**. Per-slot CurrentSet
files do not change deferred validate scheduling; validate still runs per track after load from
CurrentSet.

#### Scenario: Continuous playback still gets validate after CurrentSet load

- **WHEN** a track finishes overdub stop with deferred validate queued
- **AND** transport stays in **PLAYING** for longer than **deferredValidateMaxDelayMs**
- **THEN** **validateAndCleanupMidiEvents** runs once for that track
- **AND** **deferredFullMidiValidate** is cleared
