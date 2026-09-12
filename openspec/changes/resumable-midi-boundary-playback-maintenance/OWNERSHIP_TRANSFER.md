# Ownership transfer proposal — runtime derived playback views

**Change:** `resumable-midi-boundary-playback-maintenance`
**Date:** 2026-09-13

## Current owner

`Loop` owns the mutable active derived order (`playbackOrder_`,
`playbackOrderDirty`, and `nextEventIndex`) alongside canonical passes.

## Target owner

The per-slot `LoopPlaybackRuntime` owned by `Track` owns active derived
playback-view events, order, coverage, cursor, bridge, and continuation state.

## Reason

An active order must bind its event source, order, cursor, and coverage
immutably. Keeping its active mutation in `Loop` requires a full synchronous
rebuild on the clock path and allows order/source identity to drift. Extending
the existing runtime owner reuses its ledger and per-slot lifecycle without
moving canonical content or adding a new manager.

## Migration strategy

1. Reserve and prewarm fixed runtime-view/bridge capacity per active slot.
2. Build and verify runtime views against `Loop`’s complete builder while the
   latter remains a STOPPED-only oracle.
3. Redirect active playback to published runtime views; `Loop` supplies only
   canonical source and revision invalidation.
4. Delete direct active-transport `Loop::playbackOrder_` rebuilding after task
   5.4’s native, size, and HITL gates pass.

## Temporary compatibility

| Layer | Description |
|---|---|
| Type / location | `Loop::playbackOrder_` and `Track::rebuildPlaybackOrder()` remain the STOPPED-state parity oracle. |
| Creation date | 2026-09-13 |
| Removal condition | OpenSpec task 5.4 confirms no active-transport full rebuild or compound frame path remains. |
| Maximum lifetime | This OpenSpec change through archive. |

## Removal trigger

Delete the active-path compatibility use when task 5.4 has passing differential
tests, RAM1/firmware-size validation, and overflow-free HITL evidence for the
4b.3 criterion. The stopped-state oracle may remain only if a follow-on change
records its owner and scope.

## Validation approach

| Gate | Scope |
|---|---|
| Native tests | View/order/bridge differential traces, capacity pressure, deferred-stop and command-precedence fixtures. |
| Firmware build | Fixed RAM1 and firmware-size budgets with simultaneous views, bridges, and capture reservations. |
| HITL | Overflow-free active transport, long-loop handoffs, source changes, stop completion, and post-overdub MIDI gaps. |

## Approval

- [x] Architecture reassessment completed
- [x] Authority conflict check completed
- [x] DECISION_LOG entry drafted (`DEC-058`)
- [x] User approved transfer

**Approved by:** User, 2026-09-13
