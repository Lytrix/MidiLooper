# Formal architecture reassessment — resumable MIDI-boundary playback maintenance

**Date:** 2026-09-13
**Status:** Approved for implementation under the listed evidence gates
**Scope:** OpenSpec task 1.1
**Change:** [`resumable-midi-boundary-playback-maintenance`](.)
**Decision:** [DEC-058](../../../docs/DECISION_LOG.md#dec-058--runtime-owned-derived-playback-views-and-bounded-stop-admission)

## Reason triggered

The active derived playback-order lifecycle moves from mutable fields and
storage on `Loop` to fixed per-slot views on `LoopPlaybackRuntime`. The change
also adds an active transport state transition (`OVERDUB_STOPPING`) and changes
which portions of existing load work are eligible on the MIDI interval path.
Those are ownership, state-transition, and execution-bound triggers.

## Current architecture

| Scope | Current owner | Constraint |
|---|---|---|
| Canonical capture passes, revisions, persistence, and undo/redo | `Loop` / `LoopPasses` | Must retain established seal and recovery semantics. |
| Derived order, dirty bit, and cursor | `Loop::playbackOrder_`, `playbackOrderDirty`, `nextEventIndex` | Active `playCommittedLoopMidi()` can rebuild and sort it synchronously. |
| Per-slot merged events and active-note ledger | `LoopPlaybackRuntime` | Already runtime-only, prewarmed per `Track`. |
| Load execution | `DeferredJobScheduler::runFrame()` | No new scheduler or hidden unbounded Commit cleanup on active transport. |
| MIDI interval placement | `main.cpp` and existing `MidiServiceDrain` policy | No new `handleMidiInput()` call site or reordered dispatch. |

## Selected evolution

`LoopPlaybackRuntime` becomes the sole owner of active derived playback views:
their event/order pairing, source identity, coverage, cursor anchor, bridge
state, and maintenance continuation. `Loop` retains canonical pass content,
revision, invalidation notifications, persistence, and undo/redo. `Track`
requests or consumes runtime-view work at existing interval boundaries.

`OVERDUB_STOPPING` is an active state, not a cleanup idle state. It preserves
playback and MIDI priority while reserved finalization grains complete. A later
transport Stop has precedence and invalidates any pending return to PLAYING.

The load/display owner remains in `main.cpp`; `runFrame()` remains the
load-job child. Any phase that cannot demonstrate a complete bounded body stays
STOPPED-only.

## Alternatives rejected

| Alternative | Reason |
|---|---|
| Keep rebuilding `Loop::playbackOrder_` on the clock path | Full gather/sort/reanchor remains content-sized and violates DEC-043. |
| Add `RuntimeWorkBudget` or another scheduler | Rejected by DEC-043 and the runtime scheduling architecture. |
| Publish a partial successor | Breaks event/order identity and exactly-once MIDI semantics. |
| Make canonical `Loop` content runtime-owned | Would duplicate canonical ownership and change persistence/undo scope. |

## Migration and compatibility

1. Prewarm fixed runtime-view and bridge capacity; retain `Loop`’s complete
   order builder as a STOPPED-only oracle.
2. Differential-test the runtime views and bridges against that oracle; deny
   active work that lacks capacity or coverage proof.
3. Remove direct active-transport order rebuilding and compound load-frame
   execution only after the OpenSpec’s native, firmware-size, and HITL gates.

The compatibility layer is runtime-local. It has no on-disk format, does not
become a second canonical timeline, and must be removed by task 5.4 at this
OpenSpec’s closeout.

## Approval

The user authorized implementation by requesting this feature branch on
2026-09-13. Approval covers the documented transfer and evidence-gated
implementation, not bypassing the required native, capacity, or HITL gates.
