## Purpose

Overdub capture SHALL remain stable when overdub-start is triggered more than once on an active
overdub session. Shipped in **loop-ownership-hardening** (archived 2026-06-22).

## Requirements

### Requirement: Overdub start is idempotent

When a track is already in **TRACK_OVERDUBBING** with an open **Capture** phase of **Overdub**,
a subsequent overdub-start command SHALL NOT clear or reinitialize **capture.store**.

#### Scenario: Double overdub command preserves capture

- **WHEN** the track is **TRACK_OVERDUBBING** and **capture.phase** is **Overdub** with events in **capture.store**
- **AND** the user triggers overdub start again (e.g. **OVERDUB_FOR_SLOT** on the active slot)
- **THEN** **capture.store** event count and content are unchanged
- **AND** no second **beginCapture** runs

#### Scenario: First overdub start still opens capture

- **WHEN** the track transitions from **TRACK_PLAYING** to **TRACK_OVERDUBBING**
- **THEN** **beginCapture(Overdub)** runs
- **AND** **capture.store** is empty at overdub entry

### Requirement: Capture buffer single writer during overdub

While **capture.phase** is **Overdub**, only **appendCaptureEvent** and explicit discard paths
(**discardCapture**, successful **commitCapturePass**, undo discard) SHALL mutate **capture.store**.

#### Scenario: State transition without commit does not publish capture

- **WHEN** overdub stops and **commitCapturePass** publishes
- **THEN** **capture.store** is cleared and **capture.phase** is **None**
- **AND** published chunks live only under **passes.overdubPasses[]**
