# Tasks — runtime-working-set-ownership

Scope: `docs/Runtime/CURRENT_WORK.md` § Now implementing.

**This is an ownership change.** Before any firmware edit: run
[`docs/Templates/DECISION_REVIEW.md`](../../../docs/Templates/DECISION_REVIEW.md), the preflight, and the
ownership transfer protocol in
[`docs/Authority/ARCHITECTURE_RULES.md`](../../../docs/Authority/ARCHITECTURE_RULES.md); search
[`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md) (RC-MEM-2/RC-MEM-5 and §5.13 are direct
predecessors). The requested `DEC-###` entry is recorded at acceptance and appended at closeout.

Every task: no allocation on a timing-critical edge, and re-read the RAM1 line (~870 B of padding before
`code + padding` crosses a FlexRAM bank).

## S1 — measure the law before changing it (no behaviour change)

- [ ] S1.1 Instrument per-slot working-set bytes by owner (views, capture staging, companion
      reconstruction/apply/bridge-map/passes, canonical pass inputs, transfer horizon, stop-job
      capacity, prepared pair) and per-loop derived bytes (snapshot slabs, whole-loop `VisualCache`),
      with a working-set **holder count** and the owning slot.
- [ ] S1.2 Native test asserting the *scaling* claim rather than a total: adding recorded slots with a
      constant active set does not grow working-set bytes; each activation adds exactly one holder.
- [ ] S1.3 Device baseline on a 60+ slot library: boot and steady-state `live`, per-owner tagged bytes,
      holder count, snapshot slab bytes. This is the "before" curve the change must flatten.
- [ ] S1 gates: native full; `teensy41-capture-serial` RAM1 line recorded; baseline curve recorded in
      this file (numbers, not adjectives).

## S2 — the role pool and the acquire lifecycle (still no release)

- [ ] S2.1 Introduce the working-set pool with a configured **active-envelope ceiling** and an
      explicit acquire path; route today's prewarm through it so behaviour is unchanged but ownership is
      expressible.
- [ ] S2.2 Declare the **live-owner guard** as explicit predicates: capture stop job, deferred stop
      commit, undo/redo roots, prepared playback handover. Native fixture per owner kind.
- [ ] S2.3 Declared cap policy: what happens when activation is requested beyond the ceiling (refuse vs
      queue), reported rather than silently allocating.
- [ ] S2 gates: S1 gates plus the new `test_runtime_working_set_ownership`; existing
      `test_playback_prewarm` unchanged.

## S3 — release on deactivation

- [ ] S3.1 Release the working set when a slot leaves the active set and the guard reports no live
      owner; make release total (every part returns to the pool).
- [ ] S3.2 Prove reuse: activating C after A releases reuses A's capacity with no net pool growth
      (native + device).
- [ ] S3.3 Confirm no timing regression: activation remains a cold boundary, and the transport path
      performs no working-set allocation (assert with the existing allocation-free hot-path fixtures).
- [ ] S3 gates: S2 gates plus the reuse fixture and a device run showing the holder count rising and
      falling with activation/deactivation.

## S4 — demote inactive slots' whole-loop derived state

- [ ] S4.1 Drop the published snapshot root and whole-loop `VisualCache` for a slot that leaves the
      active set, re-deriving on activation through the lazy-hydration path.
- [ ] S4.2 Native fixture: demote then re-activate a slot and assert playback content is identical, and
      that re-derivation does not occur on a timing-critical edge.
- [ ] S4.3 Device: snapshot slab bytes track the active set, not the recorded library.
- [ ] S4 gates: S3 gates plus content parity for a demoted/re-activated slot (existing
      content-verification pipeline).

## S5 — bound resident canonical content

- [ ] S5.1 Cap resident canonical event content (per the TBD policy: fixed cap, LRU over the active set,
      or derived from loop length) and load inactive loops' events on demand from SD.
- [ ] S5.2 Prove the ceiling arithmetic: at the observed density the resident store holds roughly 28
      sixty-four-bar loops, so a 60-slot library cannot be event-resident.
- [ ] S5.3 Reload produces byte-identical content; loading an active loop stays within the existing
      admission contract.
- [ ] S5 gates: S4 gates plus a 60-slot load/reload parity run.

## S6 — the scaling test (acceptance gate)

- [ ] S6.1 Run the scaling test and record the curve: 60+ long slots loaded/created; activate A, activate
      B, deactivate A, activate C; report pool `live`, per-owner bytes, holder count, per-loop derived
      bytes at each step.
- [ ] S6.2 Require: flat in recorded slots; one step per activation; A's capacity reused by C with no
      net growth; no `FATAL`, no `#MEM_EXT_REFUSED`, live device throughout.
- [ ] S6.3 Re-run the reported failure shape (arm a track, external clock + MIDI notes) on the 60-slot
      library and require it to reach `RECORDING`.

## Safeguards (valuable, but not the strategy)

- [ ] G.1 `immutable-loop-snapshot-passes` RC-MEM-2's outstanding item: derive per-window capacities from
      the resolved window instead of the 2048-event ceiling; prewarm only the armed slot.
- [ ] G.2 §5.13: make the record-arm chain non-all-or-nothing so a shortfall cannot exit the arm.
- [ ] G.3 §5.13: `RuntimeCompanionReconstruction`'s single 64 KiB buffer split into blocks.
- [ ] G.4 Explicitly **not** the fix: releasing arm reserves on disarm (the same view set serves playback
      of the slot's committed content).

## Closeout

- [ ] `DEC-###` appended to [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md) with the ownership
      rule as accepted, and the ownership transfer recorded in `ARCHITECTURE_RULES.md` if it moves a
      documented owner.
- [ ] `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md`,
      `docs/DELIVERABLE_TRACKING.md` updated.
- [ ] `docs/Authority/Architecture/*.md` and `docs/Guides/*` that describe slot-resident working sets or
      the prewarm contract updated to the active-role model (documentation closeout).
- [ ] Archive only after S6 passes and the safeguards are either landed or explicitly deferred with a
      reason.
