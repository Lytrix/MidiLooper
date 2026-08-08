## Why

Record/overdub stop responsibility is split across synchronous record stop, deferred overdub stop,
scattered `cancelDeferredOverdubStop` call sites, persistence requests, playback projection, and
display updates. Recent overdub queue/freeze work added compensating patches without a single owner
for **capture commit** progression after the user stops recording or overdubbing.

Arm-record regressions, empty live display during capture, and stop-path faults are difficult to
bisect because extraction, scheduling, ownership, and API renames were attempted together.

Agent plan: [`docs/Plans/unified_capture_stop_driver_refinement.md`](../../../docs/Plans/unified_capture_stop_driver_refinement.md).

**Per-phase review:** [ARCHITECTURE-REVIEW.md](./ARCHITECTURE-REVIEW.md) (architecture + implementation gates each phase).

**Relationship:** Complements DEC-020 (`continuous-runtime-persistence`) — persistence scheduling
stays independent; this change owns **when** capture commit runs and coordinates post-commit
side effects. Does not replace `Loop::commitCapturePass` or `Track::finalizeCommitSideEffects`.

## Primary architectural invariant

> Exactly one runtime component owns an in-progress **capture commit** from the moment recording
> or overdubbing stops until the loop reaches its next stable runtime state.

## What Changes

- **Capture stop ≠ capture commit** — stop entry paths (record/overdub preparation) remain
  separate; shared **capture commit pipeline** owns flush → seal → finalize → runtime
  finalization.
- **`Track::commitCaptureForStop`** — single pipeline orchestrator (Phases 1–2 extraction;
  behavior-preserving).
- **Deferred commit owner** — record migrates onto existing overdub deferred FSM (Phase 3; first
  intentional behavior change).
- **Lifecycle abort** — `cancelCaptureCommit` / `resetCaptureCommitSession` replace scattered
  cancel paths (Phase 4).
- **Symbol cleanup** — Phase 5 rename only after behavioral stability.

## What Stays Unchanged (through Phase 2)

- `Loop::commitCapturePass`, `Track::finalizeCommitSideEffects`, `CommitReason`, `CommitResult`
- HITL capture semantics, telemetry ordering, runtime state transitions
- DEC-020 persistence scheduler and mid-pass writer
- Note-edit fold bypass semantics (consolidated into pipeline in Phase 2, not redesigned)

## Scope

| In scope | Out of scope |
|----------|----------------|
| `Track.cpp` stop/commit paths | DEC-020 Phase 5 recovery |
| Native regression guards | Single-press auto-start transport |
| Naming glossary (Phase 0) | Note-edit continuous persistence |
| Incremental Phases 1–5 per design | Playback-window Phase A–C unless required for pipeline centralization |

## Capabilities

### New

- `capture-commit-owner` — invariant, pipeline boundary, naming glossary, phased migration

### Modified

- `timeline-passes` — normative capture commit ownership at stop
- `capture-state-guards` — lifecycle abort and freeze cleared on record entry

## Impact

- Firmware (Phases 1+): `Track.cpp`, `Track.h`, `TrackManager.cpp` (Phase 4), button managers (freeze hooks only)
- Guides: cross-link [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- Verification: `pio test -e native`; Phases 1–2 HITL identical to baseline; Phase 3+ arm→record→stop; 64+64 after Phase 5

## Phased delivery

| Phase | Deliverable | Behavior change? |
|-------|-------------|------------------|
| 0 | OpenSpec + DEC + runtime docs | No |
| 1 | Extract `commitCaptureForStop` | No |
| 2 | Centralize post-finalize in pipeline | No |
| 3 | Record → deferred commit owner | Yes |
| 4 | `cancelCaptureCommit` lifecycle | Yes (abort) |
| 5 | Rename `OverdubStop*` symbols | No |
