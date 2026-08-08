## Why

Long record baseline runs at 64 bars fail at record stop: firmware reaches `RECORDING -> STOPPED_RECORDING`, but does not transition to `PLAYING`, overdub never starts, and USB serial drops (`Device not configured`).

This blocks long-session validation and risks losing the just-recorded pass after crash/reboot because stop-path finalize and SD persistence do not complete.

## What Changes

- Add a focused investigation and fix track for 64-bar stop-path behavior from `stopRecordingTrack` through finalize, state transition, and persistence.
- Capture deterministic evidence for the failing path (`RECS`, `ST` transitions, finalize completion, persistence completion) and define pass/fail gates for long runs.
- Add stop-path hardening so long `recordPass` stop reliably reaches `PLAYING` and overdub can start.
- Ensure finalized long record data is persisted in the v4 storage path and survives reboot after an interrupted session.
- Add a chunk-stream writer path for persisted capture pass data so SD write does not require one full flattened in-memory vector per pass.
- Add bounded long-loop display rendering: cap visible piano-roll window to 16 bars and add a full-loop overview strip across display width.
- Add native and HITL regression coverage for long record stop, including transition completion and persisted reload checks.

## Capabilities

### New Capabilities
- `long-record-stop-reliability`: Investigate and harden long record stop so 64-bar record stop reaches `PLAYING`, enables overdub, and survives persistence/reload after fault conditions.

### Modified Capabilities
- `capture-state-guards`: Extend stop-path requirements for long record lengths to require successful finalize and `STOPPED_RECORDING -> PLAYING` completion.
- `storage-loop-io`: Tighten long-record persistence requirements so finalized data is present after reboot even when failure occurs immediately after stop.
- `timeline-passes`: Clarify long stop-path behavior around `recordPass` finalize and post-stop availability for overdub.

## Impact

- Affected firmware: `Track.cpp`, `TrackStateMachine.cpp`, `Loop.cpp`, `StorageManager.cpp`, `StorageLoopIo.cpp`, and stop-path verification hooks in capture logging.
- Affected display path: `DisplayManager.cpp` bounded window and overview strip rendering for long loops.
- Affected verification: `scripts/host_midi_automation_baseline.py` long-run assertions and related native tests.
- Brownfield references: `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`, `docs/DELIVERABLE_TRACKING.md`, and `docs/Plans/phase-3-multi-loop.md`.
- Non-goals: Jam D13 capture scope, scene workflow, and unrelated note-edit UX changes.
