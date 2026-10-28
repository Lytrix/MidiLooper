## Why

The owner reports that the display freezes after several overdubs on the image that repaired stop throughput. Restore display responsiveness without rolling back the working stop lane or guessing from transition-only telemetry.

## What Changes

- Add a regression contract for display progress and qualified content during an overdub chain, alongside the existing stop-throughput contract.
- Attribute the failing boundary using actual display-phase/completion observations and deterministic production-path tests, not gaps between `disp_turn` transitions.
- Repair only the demonstrated scheduling/admission regression within existing owners; retain bounded overdub bursts, committed-history reuse, source qualification and Record isolation.
- Keep boot first-paint delay and perceived MIDI drift as separately measured symptoms, not assumed consequences of the same defect.

## Capabilities

### New Capabilities

- `overdub-chain-display-progress`: display service and content-progress acceptance while overdub stops retire.

### Modified Capabilities

None. Existing [DMA transport](../../specs/recoverable-display-dma-transport/spec.md) and [deferred load scheduling](../../specs/deferred-job-scheduler/spec.md) requirements remain unchanged; this contract covers the interaction above them.

## Impact

Existing owners: `main.cpp` display/deferred dispatch, `Track::processDeferredIdleMaintenance`, `Loop` bounded display source and `DisplayManager` paint phases. Tests: `test_live_capture_display_boundary`, `test_shared_capture_finalization`, `test_midi_service_drain` and existing DMA controls. No new Manager, persistent representation, source lifetime, or SD format.

See [INVESTIGATION.md](INVESTIGATION.md), [display authority](../../../docs/Authority/Architecture/Display.md), [delivery tracking](../../../docs/DELIVERABLE_TRACKING.md) and [design](design.md).

## Evidence and limits

- VERIFIED: the owner reports a display regression after the stop-throughput fix; the retained capture contains a live overdub chain.
- VERIFIED: `emitDisplayTurnReceipt` suppresses repeated same-interval fresh frames. Missing `disp_turn` rows cannot establish missing paints.
- ASSUMPTION: the burst changed scheduling sufficiently to cause the freeze. This needs a discriminating regression, not a tuning-only patch.
- UNKNOWN: exact refusing boundary, physical frame cadence, and whether boot/MIDI symptoms share a cause.
- Product decisions: none required for the narrow regression repair; no new UX.

## Non-goals

No restore/malformed-slot changes, SD mutation, general scheduler redesign, source-lifetime redesign, unconditional snapshot publication during Capture, unrelated MIDI-clock changes, or automatic upload. No claim to fix boot delay or MIDI drift without separate evidence.
