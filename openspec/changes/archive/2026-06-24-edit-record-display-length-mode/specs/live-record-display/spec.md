# live-record-display Specification (delta)

## Purpose

During RECORD on the recording-focus loop slot, the piano roll SHALL show captured notes and growing loop length **before** record stop — not only after commit to published takes / visual cache.

## ADDED Requirements

### Requirement: Live capture notes during RECORD

The system SHALL display notes from the active capture buffer (`capturePreview` and/or live open note-ons) on the piano roll while track state is RECORDING and the display slot equals the recording-focus slot.

#### Scenario: Fixture note visible mid-record

- **WHEN** the host sends a fixture note-on during RECORD and at least one display frame updates before record stop
- **THEN** the piano roll frame for that slot SHALL include a note segment with matching pitch and start tick
- **AND** `#CAP DISP` (when `SESSION_CAPTURE` enabled) SHALL report `frameNotes > 0` before STOP

#### Scenario: Growing loop length during RECORD

- **WHEN** capture length grows with playhead / current tick during RECORD
- **THEN** the displayed loop length SHALL increase accordingly
- **AND** note positions SHALL be scaled or tick-mapped within that growing length (not deferred until stop)

### Requirement: Stopped-transport growing capture

When RECORD is active without transport playing, the system SHALL still show capture preview notes using the stopped-record playhead rule in `DisplayManager::resolvePlayheadInLoop`.

#### Scenario: ARM then RECORD without transport play

- **WHEN** the user records with transport stopped but capture length grows via clock or manual tick advance
- **THEN** open capture notes SHALL still appear on the piano roll before stop
- **AND** the playhead close tick SHALL follow the growing capture length rule (not wrap to zero incorrectly)

## MODIFIED Requirements

*(none — new capability)*
