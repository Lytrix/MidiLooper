# Historical decision review — resumable MIDI-boundary playback maintenance

**Date:** 2026-09-13
**Scope:** `resumable-midi-boundary-playback-maintenance` task 1.1

## Searched

- [x] [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md)
- [x] This active OpenSpec change and `openspec/specs/`
- [x] [`docs/Plans/runtime_scheduling_admission_model_architecture.md`](../../../docs/Plans/runtime_scheduling_admission_model_architecture.md)
- [x] [`docs/Plans/runtime_scheduling_owner_boundary_admission_refinement.md`](../../../docs/Plans/runtime_scheduling_owner_boundary_admission_refinement.md)
- [x] [`docs/Authority/Architecture/ExecutionBounds.md`](../../../docs/Authority/Architecture/ExecutionBounds.md)

**Search terms used:** `playbackOrder`, `LoopPlaybackRuntime`, `track_order`,
`load_frame`, `prewarm`, `overdub stop`, `execution bound`, and `ownership`.

## Relevant findings

- [DEC-043](../../../docs/DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property)
  requires a declared structural or empirical-plus-policy bound before interval
  work is treated as safe. A caller-side time budget is insufficient.
- [DEC-027](../../../docs/DECISION_LOG.md#dec-027-deferred-job-scheduler-north-star)
  keeps `DeferredJobScheduler` as the existing load-job owner. This change may
  refine its job protocol and `main.cpp` orchestration but creates no scheduler.
- [DEC-038](../../../docs/DECISION_LOG.md#dec-038-overdub-wrap-commit-and-session-undo)
  and [DEC-051](../../../docs/DECISION_LOG.md#dec-051-overdub-wrap-seals-at-loop-end-not-session-start)
  keep canonical overdub-wrap content in `Loop` and seal at phase zero.
- [DEC-049](../../../docs/DECISION_LOG.md#dec-049-overdub-stop-admits-the-seal-and-accelerates-snapshot-publication)
  forbids unbounded stop work. The new bounded deferred-stop state refines its
  timing while retaining `Loop` canonical ownership.
- The runtime scheduling plans reject `RuntimeWorkBudget` and generic interval
  reservation. They require each owner’s unit to be bounded, resumable, or
  excluded from timing-critical transport.

## Existing reusable pattern

`Track::playbackRuntime` already owns per-slot merged playback events, runtime
generation, and the active-note ledger. `DeferredJobScheduler::runFrame()` and
the existing `main.cpp` load/display owner remain the load-frame extension
points. `Loop::rebuildPlaybackOrder()` is retained only as the stopped-state
oracle during migration.

## Reuse decision

**YES.** Extend `LoopPlaybackRuntime`, `Track`, `LoadLoopJob`, and the existing
`main.cpp` owner. Do not add a scheduler, manager, mutable shadow timeline, or
second MIDI service call site.

## Trigger and authority outcome

The derived active playback-order lifecycle crosses from `Loop` to
`LoopPlaybackRuntime`, so the ownership-transfer protocol applies. The transfer
is recorded in [OWNERSHIP_TRANSFER.md](OWNERSHIP_TRANSFER.md), the formal
reassessment, and DEC-058. No persistent schema, canonical pass ownership, or
MIDI dispatch-order transfer is authorized.

## Evidence gates

This review authorizes the implementation direction only. Before active
transport selects a new grain, implementation must prove its fixed capacity,
declared quantum, continuation, overrun policy, coverage or stop contract, and
MIDI-trace parity as required by the OpenSpec tasks.
