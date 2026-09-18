# Tasks — runtime-working-set-ownership

Scope: `docs/Runtime/CURRENT_WORK.md` § Now implementing.

**Ownership change.** Before any firmware edit: run
[`docs/Templates/DECISION_REVIEW.md`](../../../docs/Templates/DECISION_REVIEW.md), the preflight, and the
transfer protocol in [`docs/Authority/ARCHITECTURE_RULES.md`](../../../docs/Authority/ARCHITECTURE_RULES.md);
search [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md) (RC-MEM-2/RC-MEM-5, RC-MEM-3 and §5.13 are
direct predecessors). The `DEC-###` for the ownership rule lands at S0; S6 needs its own amendment.

Every task: no allocation on a timing-critical edge; re-read the RAM1 line (~870 B of padding before
`code + padding` crosses a FlexRAM bank).

## S0 — pin the memory law (decisions before code)

> **Blocked by [`FALSIFICATION.md`](FALSIFICATION.md):** F1 (is "2 sounding slots per track" real capability
> or a restated target?), F2 (add the per-sounding-slot published-root term to the budget), F3 (cite the
> stop-job admission guard), F4 (make the 16-track requirement satisfiable). F5 is resolved by S0.1/S0.1a
> stating how the role tiers compose with `activeLoopIndex`, `CapturePhase` and `TrackState`.

- [ ] S0.1 `DEC-###`: role tiers `COLD` / `SELECTED` / `PREPARED-ACTIVE` / `CAPTURE-ACTIVE` / `DEFERRED`,
      with selection explicitly cheap and "about to sound"/arm/preload as the only cold admission boundary.
- [ ] S0.1a `DEC`: **role classes and cardinalities** -- `PlaybackWindowLease` up to 32, `LiveCaptureLease` 1,
      `DeferredFinalizationLease` **1** (rotating: one finalizing predecessor + one live successor, preserving
      DEC-067 successor re-entry; **not** two simultaneous finalizations), `InteractiveLease` 1 -- composable,
      each owning only the resources its operation needs.
- [ ] S0.1b Record that a second finalization lease is **not** provisioned preemptively, and that the evidence
      path is the `finalization_lease_stop_denied` counter; note the constrained shapes (very short captures,
      rapid punch-stop/start/stop).
- [ ] S0.2 `DEC` invariant: lease identity is `slot + generation + role`; leases transfer and release by
      identity; a generation-mismatched release is a rejected, reported no-op.
- [ ] S0.3 Pin the **cold undo policy** (open decision 1) from the four candidates in `design.md`, as a
      product decision with its user-visible consequence recorded. Required before any flat-cold-memory claim.
- [ ] S0.4 Record that **canonical residency is a separate gated phase** (S6) with its own `DEC`
      amendment, and that the cold contract is descriptor-based.
- [ ] S0.5 Confirm the **track count** for the envelope (16 in the requirement; `NUM_TRACKS` is 8 today) and
      whether that lands inside this change or as its own workstream, and record the **transfer-overlap
      depth** the pool must cover.
- [ ] S0 gates: decisions recorded; no code change.

## S1 — characterize the current scaling (measurement only)

- [ ] S1.1 Per-owner accounting: working-set bytes by owner (views, capture staging, companion
      reconstruction/apply/bridge-map/passes, canonical pass inputs, transfer horizon, stop-job capacity,
      prepared pair), per-loop derived bytes (snapshot slabs, whole-loop `VisualCache`), lease holder
      count, owning slot, role.
- [ ] S1.2 **Red scaling fixture**: assert that growing recorded slots with a constant active set does
      *not* grow working-set residence. It must **fail** on today's architecture; that failure is the
      baseline this change exists to remove. Reuse the same fixture as the green acceptance test.
- [ ] S1.3 Device slope to the **current failure point** (not a 60-slot baseline -- the resident event
      store holds only ~28 sixty-four-bar loops today): grow the library until residency or canonical
      capacity fails and record the slope, the failure point and the error. Add a synthetic
      metadata-only 60-slot characterisation for the law itself.
- [ ] S1.4 Undo residency baseline: how much snapshot residency cold slots retain today via undo roots.
- [ ] S1 gates: native full; `teensy41-capture-serial` RAM1 line; the red fixture failing and recorded;
      the slope and failure point in this file as numbers.

## S2 — window-size the working set (prerequisite, promoted from G.1)

- [ ] S2.1 Derive the **playback window lease** capacities from the resolved window instead of the
      2048-event ceiling (playback views and their ordering/provenance only). This is the promotion of
      RC-MEM-2's outstanding item, and it is the term multiplied 32 times.
- [ ] S2.2 Measure each class separately: `P` playback window lease, `C` live-capture extension, `D`
      deferred-finalization extension, `I` interactive extension. Do **not** size the specialized classes by
      the playback target.
- [ ] S2.3 Prove the class budget: `fixed + 32P + C + D + I + transfer overlap + canonical allowance +
      reserve <= PSRAM budget`, with the numbers recorded. Only `P` is multiplied by the envelope.
- [ ] S2.4 Either show the envelope fits, or state the achievable simultaneous-playback capability as a
      bound (`k` slots per track, `k < 2`) with the arithmetic behind it.
- [ ] S2.5 Prove length-independence: two slots of very different loop lengths on one track have comparable
      resident working sets, and playback crosses window boundaries with no timing discontinuity.
- [ ] S2 gates: S1 gates plus the lease-size measurement and the cap derivation recorded; existing
      playback-prewarm fixtures still green.

## S3 — introduce the reusable lease pool (no release yet)

- [ ] S3.1 `WorkingSetLease` with owner = slot + generation + role, and a pool provisioned for the
      measured active envelope. Route today's prewarm through it so behaviour is unchanged but ownership
      is expressible.
- [ ] S3.2 Explicit liveness predicates per owner kind: capture stop job, deferred stop commit, undo/redo
      root, prepared playback handover. Native fixture per kind.
- [ ] S3.3 Selection acquires nothing: assert the SELECTED tier and browsing cause no lease churn.
- [ ] S3 gates: S2 gates plus `test_runtime_working_set_ownership`; `test_playback_prewarm` unchanged.

## S4 — release, recycle and stale-owner protection

- [ ] S4.1 Release when the slot leaves the active set and the guard reports no live owner; release is
      total.
- [ ] S4.2 Generation-mismatched release rejected and reported (native fixture reproducing the
      A-releases / C-reuses / late-A-cleanup sequence).
- [ ] S4.3 Reuse: activating C after A releases uses A's capacity with no net pool growth.
- [ ] S4.4 Declared cap policy when activation is requested beyond the envelope (refuse vs queue),
      reported rather than resolved by allocating.
- [ ] S4.5 **Finalization-lease rotation and denial**: emit `finalization_lease_stop_denied`; native fixture
      for A-stops-and-finalizes / B-records / A-releases / B-stops-and-takes-the-lease, and for the
      pathological shape where a stop arriving before A releases is denied non-destructively with the slot
      still recording/overdubbing and the next stop succeeding.
- [ ] S4 gates: S3 gates plus the reuse and stale-owner fixtures; a device run showing holder count
      rising and falling with activation/deactivation.

## S4b — role transfer (the never-stop requirement)

- [ ] S4b.1 Transfer the single complex role (record / overdub / passes / undo/redo / display, resumable
      small-packet logic inside MIDI boundaries) to a newly selected slot or track without a transport stop.
- [ ] S4b.2 Pre-provision the inbound role before releasing the outbound one, with the overlap bounded and
      accounted in the S2 budget; the switch point performs no allocation, and the transfer must not steal
      resources from a still-live capture or deferred-finalization owner.
- [ ] S4b.3 Land the switch on a MIDI boundary; the switch is a cold admission boundary by construction.
- [ ] S4b.4 Native fixture: switch roles across tracks/slots while playing and assert the inbound holder
      serves record/overdub/passes/undo/redo/display with no stop and no allocation at the switch.
- [ ] S4b gates: S4 gates plus the MIDI-boundary timing evidence (`overflow_free`, `call_site_gate_ok`, no
      active-interval overflow) across repeated switches.

## S5 — demote inactive slots' derived state

- [ ] S5.1 Drop the published snapshot root and whole-loop `VisualCache` for a slot leaving the active
      set. Reactivation derives **only the bounded runtime representation the active consumer needs**; a
      full published root only where an authority requires one (open decision 5).
- [ ] S5.2 Apply the S0.3 cold undo policy.
- [ ] S5.3 Native fixture: demote then re-activate, assert identical playback content and that
      re-derivation is not on a timing-critical edge.
- [ ] S5.4 Device: snapshot slab bytes track the active set, not the recorded library.
- [ ] S5.5 **Cold-save persistence parity**: with a slot demoted (no full published root), saving must use the
      defined pass-authoritative / root-optional path, serialize no stale root, and reload to exact
      passes/playback/display. This guards the LSV1 root/save authority work from being reopened through
      runtime demotion.
- [ ] S5 gates: S4 gates plus content parity for a demoted/re-activated slot (existing
      content-verification pipeline).

## S6 — canonical residency architecture gate (own gate + DEC amendment)

- [ ] S6.0 Architecture gate: `DEC` amendment for persistence-backed cold canonical content, including
      the descriptor-based cold identity. Do not start S6 before it is accepted.
- [ ] S6.1 Cap resident canonical event content per the chosen policy (fixed cap, LRU over the active set,
      or derived from loop length) and load cold loops' events on demand from SD.
- [ ] S6.1a **Dirty-generation pinning**: canonical content is eviction-eligible only after persistence
      durably acknowledges the exact generation being evicted; unpersisted content pins residency; a
      persistence failure or delay reduces capacity and never discards accepted input.
- [ ] S6.2 Prove the ceiling arithmetic (~28 sixty-four-bar loops resident at the observed density, so a
      60-slot library cannot be event-resident).
- [ ] S6.2a Adversarial test: dirty -> demote -> storage deliberately delayed -> pressure requests eviction ->
      content stays resident; save completes for that generation -> evictable; reload exact. And a save that
      completes for G must not make G+1 evictable.
- [ ] S6.3 Reload is byte-identical; loading an active loop stays within the existing admission contract.
- [ ] S6.4 Consider a capture-admission length cap if the policy requires one.
- [ ] S6 gates: S5 gates plus a 60-slot load/reload parity run.

## S7 — scaling acceptance

- [ ] S7.1 The scaling curve on a genuine 60+ slot, up-to-64-bar library: boot and steady state flat in
      recorded slots; one step per activation; deactivate A returns its lease; activate C reuses it with
      no net growth. Record pool `live`, per-owner bytes, holder count, per-loop derived bytes per step.
- [ ] S7.2 **Adversarial owner acceptance** (top-level case, not only a fixture): A active -> A creates a
      deferred job -> A's UI/playback notion deactivates -> attempt C activation -> A's lease stays pinned
      -> A's deferred job retires -> lease released -> C acquires the recycled capacity -> a late cleanup
      from A cannot touch C's lease.
- [ ] S7.3 Re-run the reported reproducer on the large library: arm a track, external clock + MIDI notes,
      and require it to reach `RECORDING`; also arm-and-play internally.
- [ ] S7.4 Require no `FATAL`, no `#MEM_EXT_REFUSED`, live device throughout.
- [ ] S7.4a Review `finalization_lease_stop_denied` across S7 runs: if denials are frequent in normal use,
      evaluate a second finalization lease from that evidence; if rare or absent, record 1 as sufficient.
- [ ] S7.5 **Continuity acceptance**: with transport playing, switch track and slot repeatedly while up to
      two slots per track sound, start record/overdub, and run undo/redo -- the transport never stops and
      the MIDI-boundary timing verifier reports no degradation on any switch.
- [ ] S7 gates: all prior gates plus S7.1-S7.4 recorded with numbers.

## Safeguards (valuable, not the strategy)

- [ ] G.2 `immutable-loop-snapshot-passes` §5.13: **failed admission must be non-destructive and must not
      cancel ARMED** -- derive the required set, acquire it completely, and on failure create no partial
      authority, stay armed, and report a resource wait/denial. Partial capacity only where an owner
      explicitly supports a smaller bounded grant.
- [ ] G.3 §5.13: `RuntimeCompanionReconstruction`'s single 64 KiB buffer split into blocks.
- [ ] G.4 Explicitly **not** the fix: releasing arm reserves on disarm (the same view set serves playback
      of the slot's committed content).
- [ ] G.1 is **not** here: window-derived sizing moved to prerequisite S2.

## Closeout

- [ ] `DEC-###` appended to [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md) (ownership rule,
      role tiers, generation-qualified leases, cold undo policy) plus the S6 amendment; ownership transfer
      recorded in `ARCHITECTURE_RULES.md` if a documented owner moves.
- [ ] `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md`,
      `docs/DELIVERABLE_TRACKING.md` updated.
- [ ] Architecture and guide docs describing slot-resident working sets or the prewarm contract updated
      to the active-role model (documentation closeout).
- [ ] Archive only after S7 passes; safeguards either landed or explicitly deferred with a reason.
