## Why

Repeating S4f.13 — accepted-sequence acceptance currently requires repairing gesture timing, fixture assumptions and evidence interpretation. The user’s DROID workflow keeps transport playing; the harness must reproduce that workflow from observed responses, without a manual reference pass or a reboot between runs.

## What Changes

- Add a response-driven acceptance scenario using the existing HITL action, session, protocol, verification and reporting owners. Keep the existing standalone entry point as a thin compatibility wrapper during migration.
- Distinguish MIDI sent, gesture recognized, intent accepted, operation executed and content durable. Correlate boot/session, run, track, slot and interval identities; do not advance on sleeps or unqualified human log messages.
- Prepare and clear only explicitly authorized targets through production DROID MIDI gestures, verify RAM/SD and persistence completion, and repeat without resetting or uploading. Stopped preparation is allowed; measured sequences remain continuously playing.
- Add bounded capture-gated read-only observations and identity events where current telemetry is insufficient, without bypassing the real gesture or capture owners.
- Complete separate record and overdub acceptance recipes, truthful oracle outcomes, content/timing integration and deterministic regression coverage through the real button processor.
- Qualify both legs across consecutive runs and preserve immutable evidence bundles. No manual pass is a prerequisite.

## Capabilities

### New Capabilities

None; this extends the existing HITL automation capability.

### Modified Capabilities

- `hitl-automation`: response-driven acceptance scenarios, same-boot preparation, continuously playing sequences, correlated evidence, deterministic input tests and repeated-run qualification.

## Impact

- Primary host files: `scripts/hitl_accepted_sequence.py`, `scripts/hitl/accepted_sequence_probe.py`, `scripts/hitl/actions/`, `scripts/hitl/flows/`, `scripts/hitl/serial/protocol.py`, `scripts/hitl/verify/`, `scripts/hitl/session.py`, `scripts/hitl/managed_capture.py`, `scripts/capture_session.py`, `scripts/hitl/reporting.py`, registry/CLI wiring and associated tests.
- Firmware instrumentation only: existing `MidiButtonProcessor`, `MidiButtonActions`, `Track` capture/accepted-interaction owners, `TrackManager` selection owner, `DebugSessionCapture` and `StorageManager::processHitlSerialCommands`. Product behavior fixes exposed by the test remain in the runtime change under their own packets.
- Dependency of [S4f.13 — accepted-sequence acceptance](../runtime-working-set-ownership/tasks.md); does not close that task through host tests. The older `hitl-cli-rebuild` scope lock remains intact; this change explicitly owns the additional scenario integration.
- Existing authority: [HITL automation spec](../../specs/hitl-automation/spec.md), [HITL architecture](../../../docs/Authority/Architecture/HITL_ARCHITECTURE.md), [delivery tracking](../../../docs/DELIVERABLE_TRACKING.md), [current implementation scope](../../../docs/Runtime/CURRENT_WORK.md#now-implementing).

## Evidence and boundaries

VERIFIED: current driver seeds A even for its record leg; uses global transport Stop; embeds sleeps and raw parsing; only partially confirms target identity. Its oracle cannot pass either leg because one-press evidence is always unknown. Record-only telemetry does not establish overdub intervals. `stopOverdubbing` can emit denial vocabulary then latch the accepted stop. `GS` exists but is not Tier-A. The current import gate excludes the proposed observation-wait facade even though the architecture describes one.

ASSUMPTION TO MEASURE: bounded response delivery and legal DROID gestures can stage B’s stop inside A’s natural finalization window for a declared fixture. A missed window is NOT_EXERCISED, never a pass. No firmware delay hook is used to manufacture hardware acceptance.

UNKNOWN: response latency and telemetry budget on the exact working display profile, and repeatability of the completed scenario. These are explicit measurement gates, not assumed results.

## Non-goals and product decisions

No manual golden pass, firmware control bypass, scheduler/finalizer pause on hardware, automatic SD quarantine/delete, reset-to-recover, DMA repair, broad legacy HITL rewrite, or relaxed runtime/content/timing gate. No new Manager or competing active-slot authority. No open product decision: the user specifies DROID controls and continuous playback. Initial target identities and destructive authorization are run inputs, not permission implied by this proposal. Safe final shutdown occurs outside the measured acceptance window under the existing shutdown contract.


## Firmware prerequisite clarification (2026-09-27)

Shared record/overdub deferred finalization is already required by the runtime OpenSpec.
Its [firmware implementation contract](../runtime-working-set-ownership/SHARED-CAPTURE-FINALIZATION.md)
now closes the record-route gap. This HITL change retains full overlap acceptance for both
modes and covers the mixed record-A → overdub-B sequence; firmware work belongs to the runtime
change, not an instrumentation-only exception here.
