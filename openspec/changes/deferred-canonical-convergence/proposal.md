## Why

A destination-Playing overdub stop whose predecessor (Record-return) publication has not yet landed starts its canonical-base build against a stale source, then mid-build the source supersedes it (`prefix_restart → record_return_superseded → fill_order_denied`), so the stop commits raw and loses its canonical/companion representation. Device evidence (`session_20261027_214639.log`) shows the first two overdubs after a record-return commit raw-only while later overdubs build a full companion — the divergence is a handoff race, not a content difference. DEC-171's escape-on-denied removed the multi-second head-of-line stall but left the content-fidelity gap: those early stops still retire raw.

## What Changes

- **BREAKING ownership boundary:** the capture-stop job may retire raw the moment its immutable committed representation has transferred; eventual canonical/companion convergence is no longer owned by the stop job but by a single slot-level obligation serviced outside the capture-stop FIFO.
- **Core invariant:** any stop that retires raw because its canonical derivation was unavailable **or became superseded before completion** SHALL create or coalesce the slot-level convergence obligation. This covers both the before-bind case (`causalBaseCurrent == false` at `CanonicalPrefix` admission) and the mid-build case (a previously valid inline build whose authority is superseded later, i.e. the measured device race).
- Introduce a **slot-level deferred convergence obligation** (one per slot, never stacked): `Idle → Owed(targetCommittedGeneration, coverage) → Building(buildSourceIdentity, targetCommittedGeneration) → Ready → Idle`, coalescing forward to the newest committed generation and re-resolving a fresh authoritative source on any source change.
- At the `CanonicalPrefix` decision, `causalBaseCurrent` gates **two** paths: *before bind* — `false` means never bind the stale source, commit raw, mark `Owed`, retire; *during build* — a superseding source or a terminal `Denied` from `stepCanonicalBase` because causal authority changed also commits raw, marks `Owed`, retires (this is the measured race). The healthy `causalBaseCurrent == true` inline full-companion path is otherwise unchanged.
- Canonical convergence becomes **independent of interaction completion**: its failure or delay affects presentation/readiness only, never the capture-stop FIFO head.
- Grow `LoopPlaybackRuntime` by the measured obligation state and re-base the I5 size pin (`7768` target / `9896` host) as an explicit architectural cost; RAM1 is unchanged (runtime is externally allocated).

## Capabilities

### New Capabilities

- `deferred-canonical-convergence`: a single per-slot, identity-qualified, coalescing deferred obligation that rebuilds the canonical/companion representation through a target committed generation once an authoritative source is available, independently of capture-stop completion.

### Modified Capabilities

<!-- None. The stop-may-retire-raw semantics is already covered by `loop-commit-semantics`
     ("Committed state is immutable; derived is disposable" — derived state SHALL NOT be required
     for correctness); this change adds the deferred rebuild obligation as a new capability. -->

## Impact

- `LoopPlaybackRuntime` (gains the deferred-convergence obligation state; I5 size-pin re-base).
- `TrackOverdubLifecycle.cpp` (the `CanonicalPrefix` gate and raw-retire → `Owed` handoff).
- `TrackPlaybackRuntime.cpp` (deferred owner turn to service `Owed`/`Building`/`Ready` and re-resolve the authoritative source).
- `TrackDeferredMaintenance.cpp` (route a slot-level owner turn without holding the stop grain).
- Native fixtures in `test/test_shared_capture_finalization/` (race, deferred convergence, coalescing, FIFO liveness, mid-build supersede, convergence-failure non-blocking, autonomous wakeup).

Ownership remains with `Track`/`Loop`/`LoopPlaybackRuntime`; no new Manager, scheduler, or dynamically owned per-slot object is introduced.

## Non-goals

- Not moving the healthy `causalBaseCurrent == true` inline canonical build out of the stop path in this slice.
- Not changing the raw committed representation or the pass/undo/companion data model.
- Not a general background builder queue; the obligation is scoped to "deferred canonical convergence after an inline causal-base supersession".
