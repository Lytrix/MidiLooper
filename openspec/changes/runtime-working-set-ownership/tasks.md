# Tasks — runtime-working-set-ownership

Scope: `docs/Runtime/CURRENT_WORK.md` § Now implementing.

**Ownership change.** Before any firmware edit: run
[`docs/Templates/DECISION_REVIEW.md`](../../../docs/Templates/DECISION_REVIEW.md), the preflight, and the
transfer protocol in [`docs/Authority/ARCHITECTURE_RULES.md`](../../../docs/Authority/ARCHITECTURE_RULES.md);
search [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md) (RC-MEM-2/RC-MEM-5, RC-MEM-3 and §5.13 are
direct predecessors). The `DEC-###` for the ownership rule lands at S0; S6 needs its own amendment.

Every task: no allocation on a timing-critical edge; re-read the RAM1 line (~870 B of padding before
`code + padding` crosses a FlexRAM bank); and hold the invariants in
[`design.md`](design.md) § Invariants — a task that violates one is not complete, and an invariant with no
enforcement point is a gap to record rather than a note to skip.

## Execution boundary (phases)

This change is an **umbrella roadmap**, not one implementation RC. Phases, each with its own device gate before
the next begins:

| Phase | Stages | Gate |
|---|---|---|
| **A** | S0-S2 | the runtime memory law is *proved*: whole-root residency removed from active playback (S2.9), P/C/D/I measured, N derived |
| **B** | S3-S5 | lease ownership, recycling, stale-generation protection, inactive derived-state demotion |
| **C** | S4d, S4e, S2.6 quantisation | bounded caches and fine switching behave as the library grows |
| **D** | S6 | persistence-backed canonical residency + dirty-generation pinning (own DEC amendment) |
| **E** | S7 | large-library scaling and continuity acceptance |

**Standby is optional.** The base 60-slot requirement must be satisfied by the law alone; standby excerpts and
visual detail windows are bounded optimisations on top, never a precondition for usability.

## S0 — pin the memory law (decisions before code)

> **Findings resolved 2026-09-18 ([`FALSIFICATION.md`](FALSIFICATION.md) records the answers):** F1 -- the
> envelope is **N derived**, today 8 tracks x 1 slot, and must not preclude 16/32/48/64 (slots-as-virtual-tracks
> is a future requirement). F2 -- active playback must become **windowed/streamed**; the root narrows to the
> undo/save checkpoint authority. F3 -- finalization is **one role**; a second stop while it is occupied is
> **denied non-destructively** (DEC-067), and the descriptor-ring depth is not evidence of multi-finalizer
> storage. F4 -- 8 tracks today with **bank switching to 16 MIDI channels** as the target. F5 -- tiers
> fold into `activeLoopIndex`/`CapturePhase`/`TrackState`. F6 -- resolved by S2 + G.2.

- [ ] S0.1 `DEC-###`: role tiers `COLD` / `SELECTED` / `PREPARED-ACTIVE` / `CAPTURE-ACTIVE` / `DEFERRED`,
      with selection explicitly cheap and "about to sound"/arm/preload as the only cold admission boundary.
- [ ] S0.1a `DEC`: **role classes and cardinalities** -- `PlaybackWindowLease` **N, derived from the budget**
      (8 today; must not preclude 16/32/48/64 sounding slots), `LiveCaptureLease` 1,
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
- [ ] S0.6 Record the **resumable work-item contract** as a convention plus `DEC` (Revision 16): shared
      `WorkIdentity { owner, generation, role }`, one terminal vocabulary, declared cost, declared deadline,
      existing admission/accounting enums, non-destructive failure. Explicitly *not* a framework: header-only
      vocabulary, concrete owner types, no vtables, no allocation. Retrofit is opportunistic and belongs in its
      own change, not here.
- [ ] S0.5 **Pin the topology** (do not leave it open): **8 tracks are implemented by this change**; 16-channel
      bank switching and multi-slot-per-track playback are **future capabilities this memory law must not
      preclude**. No task here implements them, and no acceptance test claims them. Record the
      **transfer-overlap depth** the pool must cover.
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
- [ ] S2.3 Prove the class budget `fixed + N x P + C + D + I + transfer overlap + canonical allowance +
      reserve <= PSRAM budget`, with the numbers recorded, and **derive and state the maximum N** the
      budget supports. Only `P` is multiplied by N. Report N at today's capability (8 x 1 slot) and the
      headroom toward 16/32/48/64.
- [ ] S2.4 Show what N the budget supports and record it as a capability, with the arithmetic; note the shapes
      that need a smaller P to reach 32 or 64 sounding slots.
- [ ] S2.5 Prove length-independence **at today's supported topology**: sounding slots of very different loop
      lengths (across tracks, since today is one sounding slot per track) have comparable resident working sets,
      and playback crosses window boundaries with no timing discontinuity. Multi-slot-per-track playback is a
      future capability; test the budget projection toward it, do not claim it.
- [ ] S2.8 **Visual sizing**: size the per-bar envelope (bytes/bar/slot, whole library) and the rendered
      detail window at typical density (~36 notes/bar, `DisplayNote` = 16 B) and at the representable ceiling
      (~2,048 notes/bar), and derive the visual byte cap and eviction policy alongside S2.6/S2.7.
- [ ] S2.7 **Standby sizing**: size a per-slot standby excerpt at BOTH typical density (~72 events/bar) and
      the representable ceiling (4,096 refs/bar), across 1/4-bar, 1-bar and 2-bar windows, and derive the
      standby byte cap and its eviction policy (LRU or candidate set). Record the arithmetic; an all-slots
      standby is a policy outcome the cap permits, never an assumption.
- [ ] S2.6 **Window-size sweep**: make the readiness window configurable (pixels of size: resident bytes,
      switch granularity, publication load) and sweep from bar-based (1,536-tick merged window today) down
      toward **1/32 beat = 6 ticks** (1/16 beat = 12 ticks at 192 PPQN). Record the smallest window that still
      meets the readiness deadline under load; that number bounds the achievable queued-switch quantisation
      and feeds the derived N from S2.3.
- [ ] S2 gates: S1 gates plus the lease-size measurement and the cap derivation recorded; existing
      playback-prewarm fixtures still green.

- [ ] S2.9 **GATE -- decouple active playback from whole-loop snapshot residency.** Until this passes, `N x P`
      is not the real memory law, because an unaccounted whole-loop root would still sit behind every sounding
      slot:
      - define the immutable **PlaybackWindowGeneration** authority, and let playback consume only that bounded
        immutable generation;
      - keep `publishedSnapshot` as the **checkpoint authority** for undo/save where required;
      - no whole-loop snapshot root may be required *merely because a slot sounds*;
      - on a preparation miss the **previous immutable window stays valid** for that pass (the pinned
        one-pass-late rule, S4c.3);
      - prove the chain mutation -> deadline -> publication -> playhead consumption;
      - measure snapshot-root bytes **separately from P** and demonstrate that adding sounding slots adds no
        whole-loop roots.
      S2.3's N arithmetic is **not accepted** until this gate passes. It is the prerequisite half of S4c, which
      continues into the reuse inventory and the broader readiness machinery.

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
- [ ] S4.5 **Finalization denial**: one finalizer; native fixture for A-stops-and-finalizes / B-records /
      A-releases / B-stops-and-takes-the-lease, and for a stop arriving while A still finalizes — denied with no
      partial stop, B still recording/overdubbing, next stop succeeding after the role frees. Emit
      `finalization_lease_stop_denied`. Do **not** build a queue: `CaptureStopJobs::kCapacity` is descriptor
      depth, and concurrent finalization needs its own capacity/ownership decision first.
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

## S4c — immutable readiness at re-entry (prerequisite for the envelope, from F2)

- [ ] S4c.0 Conform the readiness owner to the S0.6 contract (identity, cost, admission kind, accounting tag,
      deadline, shared terminal vocabulary) before writing it.
- [ ] S4c.1 Change the **publication unit** from whole-loop to interval/window with a **readiness deadline**:
      an update covering tick T must be immutable before the playhead returns to T.
- [ ] S4c.2 **Reuse inventory, then reuse**: enumerate what the existing resumable deferred publication path
      already provides -- snapshot construction / journal, `stepPendingSnapshotPublish`, the wrap journal, the
      deferred idle-owner scheduling (`MidiServiceDrain`), the sliced capture-stop jobs -- and record what, if
      anything, must be added. Adding a second incremental-publication system is a failure mode, not a plan.
- [ ] S4c.3 **DECIDED: rule (a)** -- the previous immutable generation stays authoritative for that pass and
      the new one takes effect on the next pass; never torn, never held, never timing-deferred. The
      user-visible consequence (an edit just behind the playhead is heard one pass later) is recorded.
- [ ] S4c.4 Narrow the published root to the **checkpoint authority for undo and save**, removing the
      playback dependency on a resident whole-loop root.
- [ ] S4c.5 Native fixtures: an update is immutable before the playhead returns; playback never consumes an
      in-progress representation; the resident immutable set is interval-bounded, not whole-loop; a missed
      deadline follows the recorded rule; undo/save still obtain a checkpoint.
- [ ] S4c gates: the budget shows no `N x whole-root` term; existing playback content parity preserved.

## S4d — standby tier (bounded, scheduled, immutable)

- [ ] S4d.1 Build a per-slot standby excerpt as resumable deferred work, scheduled against the **known next
      arrival** of the ticks it covers (loop start tick is known after a stop).
- [ ] S4d.2 Invalidate on any mutation covering the excerpt's ticks, falling back to rule (a) until
      re-prepared.
- [ ] S4d.3 Enforce the standby byte cap with the declared eviction policy; prove inactive-slot memory stays
      flat as the library grows.
- [ ] S4d.4 Candidates-only standby first (next-up and recently used); an all-slots policy only after S6's
      descriptor-based rehydration exists.
- [ ] S4d.5 Native fixtures: a switch with no preparation at the boundary; invalidation on mutation;
      cap-eviction under growth; no torn or stale standby used for playback.
- [ ] S4d gates: standby appears as its own budget term; the S2.7 cap is respected on device.

## S4e — visual tier (envelope for all slots, capped window)

- [ ] S4e.1 Per-bar **visual envelope** for every slot (extend what `overviewCaptureBandMask_` already does
      beyond capture preview), sized so the whole library fits.
- [ ] S4e.2 **Rendered detail window** within the S2.8 cap, produced through the existing idle-sliced renderer
      (`rebuildVisualCacheIdleSlice`) and validated by the existing `builtFromSnapshotIdentity` /
      `builtFromRevision`.
- [ ] S4e.3 Invalidate on any mutation covering the envelope's bars; fall back to the windowed gather.
- [ ] S4e.4 Revive `TrackManager::prewarmSelectedDisplayVisualCache` as the visual scheduler entry point
      (it currently has no caller); confirm which mechanism a dev-branch visual preload uses, and record the
      two caller-less display paths (`resolveDisplayNotesCommitted`, `resolveDisplayNotesLiveCapture`) as
      either reused or deleted rather than leaving a third variant.
- [ ] S4e.5 Native fixtures: switch shows the new slot immediately from the envelope; detail resolves through
      the existing renderer; mutation invalidates; cap and eviction hold under library growth.
- [ ] S4e gates: envelope and window appear as budget terms; no new renderer or validity counter introduced.

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

## S6 — persistence-backed canonical residency and windowed streaming (own gate + DEC amendment)

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
- [ ] S7.4d **Visual switch acceptance**: switch display across a 60+ slot library and require immediate visual
      feedback from the envelope, no full whole-loop rebuild on switch, no stale envelope after a mutation, and
      the visual byte cap respected.
- [ ] S7.4c **Standby switch acceptance**: with a warm standby set, queue switches at 1/16 and 1/32 beat and
      require zero preparation at the boundary, no stale standby output after a mutation, and the standby byte
      cap respected under a 60+ slot library.
- [ ] S7.4b **Fine-quantisation switch acceptance**: with transport playing and slots sounding, queue a slot
      switch at 1/16 beat and at 1/32 beat; require the inbound slot's immutable state to be ready at the
      boundary, no torn or held output, and the MIDI-boundary timing evidence clean. Record the quantisation
      that is actually achievable under load.
- [ ] S7.5 **Continuity acceptance**: with transport playing, switch track and slot repeatedly while up to
      today's supported topology (8 tracks x one sounding slot) sound, start record/overdub, and run undo/redo --
      the transport never stops and
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
