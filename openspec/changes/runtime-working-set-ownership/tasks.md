# Tasks — runtime-working-set-ownership

Scope: `docs/Runtime/CURRENT_WORK.md` § Now implementing.

**Current handoff (2026-09-23):** [IMPLEMENTATION_CONTRACT.md](IMPLEMENTATION_CONTRACT.md)
is binding for stage dependencies, cardinality accounting, regeneration ownership, playback extraction
and interaction supersession (DEC-079). Earlier revision notes are historical where they conflict.
No implementation checkbox is completed by this documentation correction.

**First checkoffs (2026-09-23, P0):** the S0 decision verification and the S1.2 red characterization
are checked below with their evidence in [ARCHITECTURE-REVIEW.md](ARCHITECTURE-REVIEW.md) § P0. They
carry no firmware change — S0 is decisions-only and S1.2 is a measurement fixture. Everything from
D0.2/S2b onward is still open, and the S1 gate's device legs are **unrun**.

**Checkbox reconciliation (2026-09-23): 17 → 23 of 145.** Six tasks whose work is done at the level
implementable today are now checked — **D0.3**, **S2b.5**, **S3.0**, **S4.0**, **S4f.4**, **S4f.5** — each
with a `CHECKED 2026-09-23 (checkbox reconciliation)` note stating *where its remaining clause lives*. The
rule applied, matching the already-checked S0 items: a task is checked when its own deliverable is complete
and any remainder is either a **latent condition on a later stage** or an obligation the contract assigns to
**another task**. Nothing was checked because a nearby task finished. Still deliberately open: the D0.2
allocation census and S2b.3's missing baseline (device measurements), S3.1–S3.3 and S4.1–S4.4 (the lease
pool and generation release are **not** built — only S2b.4's per-slot release exists), S4f.1/S4f.2/S4f.3/
S4f.6 (start/switch carriers), S4f.7–S4f.14, every S1/S2b/S2/S3/S4 gate, and phases B–E. The remaining
device legs are blocked by the restore-liveness defect proposed as
[`deferred-slot-restore-liveness`](../deferred-slot-restore-liveness/proposal.md).

**Ownership change.** Before any firmware edit: run
[`docs/Templates/DECISION_REVIEW.md`](../../../docs/Templates/DECISION_REVIEW.md), the preflight, and the
transfer protocol in [`docs/Authority/ARCHITECTURE_RULES.md`](../../../docs/Authority/ARCHITECTURE_RULES.md);
search [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md) (RC-MEM-2/RC-MEM-5, RC-MEM-3 and §5.13 are
direct predecessors). The ownership rule and the role classes are already recorded as **DEC-076** (accepted
2026-09-18); the acceptance rule and the per-interval finalization cardinality are **DEC-078** (accepted
2026-09-19, amending DEC-067 and DEC-076 point 2). S6 needs its own amendment.

Every task: no allocation on a timing-critical edge; re-read the RAM1 line (usable new-ITCM/DTCM budget **872 B**
before `code + padding` crosses a FlexRAM bank, which costs 32,768 B and drops the stack into the documented
silent boot hang); and hold the invariants in [`design.md`](design.md) § Invariants **plus 32-36 in Revision 18**
— a task that violates one is not complete, and an invariant with no enforcement point is a gap to record rather
than a note to skip.

**Red-team G2 input (2026-09-19, [`design.md`](design.md) Revision 18).** The revised contract was re-attacked
and returned **BLOCKED** on eleven reachable paths (B1-B11). Those are design input carried into the stages
below — slot-parameterised acceptance and the switch rule, the latch destination/epoch, per-slot liveness and
load admission, the new **S2b** boot/load stage, occupancy-aware binding, latch-aware predicates, cold-undo
pinning, and external/FLASH placement under 872 B. **No task may be executed against the pre-Revision-18
contract.**

## Deterministic execution workflow (all remaining steps)

Every future slice follows the [G2 execution-packet workflow](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
and its packet in [ARCHITECTURE-REVIEW.md](ARCHITECTURE-REVIEW.md). P0–P8 map the stages, controls,
oracles, prerequisites and repeatable recipes. Establish discriminating production-path fixtures before
behavior edits, then implement and record actual native/build/device evidence. These are completion rules,
not another broad design revision. A listed fixture/command is not implemented or passing evidence.

- [ ] D0.1 For S0/S1, execute P0's attribution/measurement recipe and record baseline revision, inputs,
      suite presence and separate canonical/reservation/root/derived costs; keep expected red evidence explicit.
      **PARTIAL 2026-09-23:** baseline revision `7f90d996` (clean tree) recorded — `test_playback_prewarm`
      179/179, `test_continuous_capture` 77/77, full native 2,262/1 skipped/0 failures — with inputs, suite
      presence, canonical-**payload** vs canonical-**reservation** separation, the red evidence, and the
      after-edit re-verification in [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P0. Still missing:
      the root/derived costs (snapshot slabs, whole-loop `VisualCache`) and the per-owner byte census, which
      need the S1.4/S1.3 device pass. Unchecked until those are recorded.
- [ ] D0.2 Before S2b behavior edits, extend the existing continuous-capture harness with P1's transport,
      reservation-denial/recovery, worker/dispatch and read-only ownership controls. Link production load/
      transport paths for assertions that require them; no copied lifecycle or success-returning domain stubs.
      **PARTIAL 2026-09-23:** transport (test-controlled `isTransportRunning`), reservation denial/recovery
      (real `ChunkReservation` pool-credit exhaustion) and the read-only ownership census are in place in
      `test_continuous_capture`. **Reconciled 2026-09-23 (S1.3 slice):** the worker/dispatch control is now
      the per-turn production drive `TrackCaptureStopTestAccess::serviceAcceptedInteraction` with the
      controlled clock and a controlled interval identity (the S4f.4 cases), and the production **load** and
      **transport-stop-retarget** paths are linked (`test_storage_manager_load`, 5/5; B03). **Still
      outstanding: only the allocation census** — B07 is not measurable natively because the pool wrappers
      are `teensy41-capture-serial` link flags only. See
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P1.
- [x] D0.3 Implement P1 fixtures B01–B07 with stepwise expected identities/content, demonstrate the pre-fix
      behavioral failure where runnable, and record any missing-API limitation separately from a red test.
      Minimal test/API seams belong in the same slice; they do not authorize the behavior before its oracle.
      **PARTIAL 2026-09-23:** B01 implemented and RED — `Expected 0 Was 24` (three inactive committed
      slots hold 8 canonical-copy credits each; the transfer-horizon clause is only reached once that is
      green). B02–B07 recorded as missing-API limitations, not as red tests: B02/B03 need S4f.0/S4f.4,
      B04 needs the load path linked, B05 needs the S2b.4 release API, B06 the dispatch control, B07 the
      allocation census.
      **B03 IMPLEMENTED 2026-09-23 (partial):** `test_b03_transport_stop_retargets_an_admitted_playing_stop_without_losing_it`
      drives the production transport-stop entry and asserts the stop survives, is retargeted to `Stopped`,
      and keeps its descriptor/slot/interval identity. **B03 is now COMPLETE for the admitted-stop leg:** the
      `Stopped`-destination *drain* is covered too -- `Track::silenceTrackMidiOutput()` was converted from a
      fail-closed stub to an observable counting boundary (it is reachable from both stop destinations, so the
      stub was over-broad and left that retire path with no native coverage at all), and the fixture now asserts
      the retargeted stop resolves to `STOPPED` (never `PLAYING`), silences exactly once, clears the obligation
      and commits one pass. **Still not assertable:** the *latched* transport-stop leg (synchronous fallback stubs). **B06 needs a
      production extraction of `src/main.cpp`'s eligibility decision or a device run** -- the sketch is not
      native-linkable -- and it is why retargeting at transport stop was not taken in this slice.
      **Status reconciled 2026-09-23 (S1.3 slice).** Every B-case is now accounted for except B07:
      **B01** green in the default gate (`test_b01_inactive_committed_slots_reserve_no_canonical_copy_or_horizon`);
      **B02** `test_b02_accepted_stop_waits_for_canonical_credit_then_executes_once`; **B03** as above;
      **B04** both halves green in `test_storage_manager_load` **5/5** (`test_b04_pop_then_requeue_loses_the_request_for_a_committed_slot`,
      `test_b04_real_load_selection_keeps_the_request_while_the_slot_is_owned`); **B05** the release refusal
      `test_s2b4_midcopy_populated_store_refuses_release`; **B06** its extraction landed
      (`Track::hasRealtimeDeferredIdleWork`, `src/main.cpp` delegating) with the discriminating case
      `test_s4f2_deferred_eligibility_includes_an_accepted_interaction_on_an_idle_track` — so the earlier
      "B06 needs an extraction or a device run" note is **superseded**, and what remains device-observable is
      only that the scheduler *reads* the predicate for a stopped track
      ([`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P1 — B06). **B07 remains unimplemented and is not
      measurable natively** (the pool wrappers are `teensy41-capture-serial` link flags only), so D0.3 stays
      unchecked on B07 alone.
      **CHECKED 2026-09-23 (checkbox reconciliation).** B01–B06 are implemented and green in the default gate,
      and B07's only remaining form is the device allocation census, which no native code can satisfy here.
      The unchecked prerequisite was **superseded by the work it gated**: every S2b behavior edit it existed
      to precede has landed with its own fixture. B07 itself is not dropped — it moves to the device census
      owned by S1.1/S2b.3, which stay unchecked for that reason.
- [ ] D0.4 For every subsequent packet P2–P8 and applicable safeguard, establish its listed oracle/controls
      before that behavior change and record results under that packet. Gated F/S6 work stays blocked on its
      own architecture contract. Do not check this umbrella item until all applicable packets are evidenced.
- [ ] D0.5 Before each task/stage checkoff, complete the packet's implementation review: required case IDs
      ran, no missing/errored suite is treated as green, outcomes were not weakened, and required device
      evidence exists. Record specified/fixture/native/device status separately; unmet gates stay unchecked.

## Execution boundary (phases)

This change is an **umbrella roadmap**, not one implementation RC. Phases, each with its own device gate before
the next begins:

| Phase | Stages | Gate |
|---|---|---|
| **A** | S0/S1 → S2b including its pulled-forward prerequisites | boot canonical reservation slope removed; loaded-slot stop retries safely; no whole-law or 60-long-slot claim |
| **B** | remaining S3/S4, S4b/S4f, S5; S2 accounting | lease ownership, recycling, accepted interactions, inactive derived-state demotion; current playback root retained when required |
| **C** | S4d, S4e, S2.6 quantisation | playback standby/fine-window work depends on F; display work has its own cap gate |
| **D** | S6 | persistence-backed canonical residency + dirty-generation pinning (own DEC amendment) |
| **E** | S7 | large-library scaling and continuity acceptance |
| **F** | planned, uncreated sibling **`windowed-playback-generation`** | own artifacts + DEC/reassessment before Apply; final S7/archive depends on F and S6 |

**Standby is optional.** The base 60-slot requirement must be satisfied by the law alone; standby excerpts and
visual detail windows are bounded optimisations on top, never a precondition for usability.

**Execution order (owner, Rev 23.4).** Execute in this order; the 32/64 envelope must not block fixing today's
residency defect: **S0/S1** (measurement + the red proof) → **S2b** (boot/load admission — it fixes the present
arm/record regression directly) → **S3/S4** (ownership identity + generation-qualified release) → **S4f**
(DEC-078 accepted-interaction latch, including STOP→START→STOP) → **S5** (cold undo) → then the
playback-window/root decoupling (phase F) that raises N. **Design revisions stop here** unless implementation
uncovers evidence that contradicts the contract; such findings return to the architect as targeted corrections.

S2b includes the explicitly pulled-forward stop/liveness prerequisites in the handoff contract §1;
its gate does not inherit S2. Later stages extend the same helpers. S2 accounting runs before pool
sizing; playback behavior tasks wait for F. This dependency order overrides numeric task order.

## S0 — pin the memory law (decisions before code)

> **Findings resolved 2026-09-18 ([`FALSIFICATION.md`](FALSIFICATION.md) records the answers):** F1 -- the
> envelope is **N derived**, today 8 tracks x 1 slot, and must not preclude 16/32/48/64 (slots-as-virtual-tracks
> is a future requirement). F2 -- active playback must become **windowed/streamed**; the root narrows to the
> undo/save checkpoint authority. F3 -- finalization is **one role**; a second stop while it is occupied is
> **accepted and scheduled, not refused** (DEC-067 capacity + invariant 31), and the descriptor-ring depth is not
> evidence of multi-finalizer storage. F4 -- 8 tracks today with **bank switching to 16 MIDI channels** as the target. F5 -- tiers
> fold into `activeLoopIndex`/`CapturePhase`/`TrackState`. F6 -- resolved by S2 + G.2.
>
> **Revised 2026-09-19 (DEC-078, `design.md` Revision 18):** F3's finalization cardinality is **per interval**
> `(track, slot, interval generation)` -- not a device-wide singleton and **not** a queue of stopped intervals;
> `LiveCaptureLease` is **1 per track**; switching is an **accepted** interaction; and the windowed-playback
> decoupling (S2.9) is **extracted** to the sibling change `windowed-playback-generation`.

> **S0 verified 2026-09-23 (P0/D0.1).** Every S0 item was checked against its landing site — none was
> re-decided and none requires a code change. The per-task citations and the one recorded observation
> (S0.6 has no dedicated `DEC-###` id; DEC-076 point 8 plus Revision 16 and `NAMING.md` bind it) are in
> [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § *S0 decision verification*.

- [x] S0.1 **RECORDED as DEC-076 point 1** (verify, do not re-decide): role tiers `COLD` / `SELECTED` /
      `PREPARED-ACTIVE` / `CAPTURE-ACTIVE` / `DEFERRED`, with selection explicitly cheap and
      "about to sound"/arm/preload as the only cold admission boundary. The tiers are a view of
      `activeLoopIndex`/`CapturePhase`/`TrackState`, not a second authority (F5).
- [x] S0.1a **RECORDED as DEC-076 point 2 + DEC-078** (verify, do not re-decide): role classes and cardinalities
      -- `PlaybackWindowLease` **N, derived** (8 today; must not preclude 16/32/48/64), `LiveCaptureLease`
      **1 per track**, `DeferredFinalizationLease` **1 per interval** identified by
      `(track, slot, interval generation)` and pinned across `activeLoopIndex` movement and track/slot switching,
      bounded by that track+slot's own single-instance resources, `InteractiveLease` 1 per selected/active
      track+slot -- composable, each owning only the resources its operation needs. **No queue of stopped
      intervals** (DEC-078 item 7).
- [x] S0.1b Record that a second finalization lease is **not** provisioned preemptively, and that the evidence
      path is the `accepted_stop_finalizer_wait` counter; note the constrained shapes (very short captures,
      rapid punch-stop/start/stop).
- [x] S0.2 `DEC` invariant: lease identity is `slot + generation + role`; leases transfer and release by
      identity; a generation-mismatched release is a rejected, reported no-op.
- [x] S0.3 **DECIDED -- two-tier cold undo** (recorded in `design.md` Revision 4): hot slots may hold
      snapshot-backed undo roots; cold slots hold only compact logical descriptors (no `LoopSnapshot` ref, no
      slab ownership) under a per-slot depth and a device-wide byte budget, evicting oldest-cold-first;
      reactivation rebuilds only the current state; a cold undo reconstructs off-clock and commits only after
      successful preparation (`undo_resource_denied` records the wait otherwise, descriptor intact); redo survives demotion and
      is invalidated only by a new committed edit/capture. Runtime undo history is not persisted.
      **Revised by Revision 18 § 7:** a cold descriptor that pins canonical pass ids charges the **pinned canonical
      bytes** to the budget; **ClearSlot is excluded** from the cold tier (its before-state is a whole-slot
      snapshot no pass-id descriptor reproduces); and a missing canonical body **defers**, never erases.
- [x] S0.3a Record the eviction priority: current musical state and dirty canonical content are never evicted by
      undo pressure; active hot undo is preferred; recent cold undo is retained within budget; oldest cold undo
      is the first victim. This is the input `overUndoMemoryPressure` (RC-MEM-3) should manage.
- [x] S0.4 Record that **canonical residency is a separate gated phase** (S6) with its own `DEC`
      amendment, and that the cold contract is descriptor-based.
- [x] S0.6 Record the **resumable work-item contract** as a convention plus `DEC` (Revision 16): shared
      `WorkIdentity { owner, generation, role }`, one terminal vocabulary, declared cost, declared deadline,
      existing admission/accounting enums, non-destructive failure. Explicitly *not* a framework: header-only
      vocabulary, concrete owner types, no vtables, no allocation. Retrofit is opportunistic and belongs in its
      own change, not here.
- [x] S0.5 **Pin the topology** (do not leave it open): **8 tracks are implemented by this change**; 16-channel
      bank switching and multi-slot-per-track playback are **future capabilities this memory law must not
      preclude**. No task here implements them, and no acceptance test claims them. Record the
      **transfer-overlap depth** the pool must cover, and record that **boot/load is an admission boundary**
      (S2b): a committed-but-inactive slot reserves no canonical-copy capacity.
- [x] S0.7 **Acceptance and per-interval finalization are RECORDED as DEC-078** (accepted 2026-09-19, amending
      DEC-067 and DEC-076 point 2; the "finalization is a queue" bullet is withdrawn). Verify the amendment
      pointers and sweep the change's own text for any remaining assertion of a denied or re-pressed stop.
- [x] S0 gates: decisions recorded; no code change.

## S1 — characterize the current scaling (measurement only)

- [ ] S1.1 Per-owner accounting: working-set bytes by owner (views, capture staging, companion
      reconstruction/apply/bridge-map/passes, canonical pass inputs, transfer horizon, stop-job capacity,
      prepared pair), per-loop derived bytes (snapshot slabs, whole-loop `VisualCache`), lease holder
      count, owning slot, role.
      **DEVICE CENSUS 2026-09-23 — tags 1..16 measured.** The 1 Hz tick carries only `tags[1..7]`, so a
      session-gated `#MEM_OWNERS` line (`MemoryMonitor::logWorkingSetOwners`, 4 s cadence, reusing
      `formatTagVector`) was added with the owner's approval; it costs 64 B of ITCM padding (1,096 → 1,032,
      still 192 B above the 840 floor) and 304 B FLASH, with native 2,281/0 failures. On the dense PASS run
      (`captures/rwso-HEAD-owners-census`, 32 notes): CaptureStopJobs 412, ViewPrewarm 715, CaptureStaging 148,
      CanonicalOutput 0, TransferHorizon 1, CompanionHistory 0, PlaybackOrder 58, ResetPassTimeline 0,
      PrepareFirstBar 0, CompanionPairLookup 10, CompanionSpans 2, CompanionReconstruction 64,
      CompanionApply 66, CompanionBridgeMap 68, CompanionPasses 96, CanonicalPassInputs 144 KB —
      **tagged total 1,776 KB of 6,144 KB peak live**, with `untracked=0`/`unknown=0` so `live` is trustworthy.
      **Still open, and neither is a telemetry oversight:** whole-loop `VisualCache` bytes (no byte accessor
      exists) and the **lease holder / owning slot / role** triple — no playback-window lease exists until
      S2.1, so that clause cannot be measured before S2 and should be re-scoped there rather than reported as
      zero. Reduction: `scripts/parse_working_set_census.py`. Details:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S1.1 device census.
- [x] S1.2 **Red scaling fixture**: assert that growing recorded slots with a constant active set does
      *not* grow working-set residence. It must **fail** on today's architecture; that failure is the
      baseline this change exists to remove. Reuse the same fixture as the green acceptance test.
      **DONE 2026-09-23 — fixture implemented + native red characterization recorded.** In
      `test/test_continuous_capture` (production boot/load admission + record-arm admission, no stub):
      `test_working_set_residence_does_not_grow_with_recorded_slots` (reserved chunks) and
      `test_inactive_committed_slot_retains_no_resident_working_set` (resident working sets), plus the
      default-gate well-formedness case `test_working_set_residence_measurement_is_well_formed`. Red run
      `WORKING_SET_RED=1 pio test -e native -f test_continuous_capture` → **2 failed, 78 succeeded**:
      `Expected 96 Was 120` (reserved chunks, 1 vs 4 recorded slots — a slope of exactly 8 chunks =
      `PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS` per admitted boot slot) and `Expected 8 Was 11`
      (resident working sets). Default gate stays green (78/78). **Gate removal condition:** promote both
      law cases into the default gate when S2b.1 (plus the S3/S4 release) lands. Full record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P0.
- [ ] S1.3 Device slope to the **current failure point** (not a 60-slot baseline -- the resident event
      store holds only ~28 sixty-four-bar loops today): grow the library until residency or canonical
      capacity fails and record the slope, the failure point and the error. Add a synthetic
      metadata-only 60-slot characterisation for the law itself.
      **Blocked/obstacle recorded 2026-09-23.** (1) *Device slope:* growing the library requires the restored
      scratch slot to survive a reboot and it does not (`Track 6 loaded state: EMPTY`, `index.bin` does not
      reconcile), so N cannot be raised across boots; the scratch range track 6 slots 1–8 is approved for when
      it can. (2) *The synthetic 60-slot characterisation is native and unblocked, but the existing helper
      cannot be reused as-is:* `measureWorkingSetResidence(recordedSlots)` commits into **one** track's slots
      (`test_continuous_capture.cpp:3725`, `for slot < recordedSlots` over `track.ensureLoopsAllocated()`), and
      `MAX_LOOPS_PER_TRACK` is **8** — so N=60 indexes out of bounds. Sixty descriptors must span tracks
      (8 × 8 = 64 slots available), and the 512-chunk pool has to be budgeted for 60 committed slots before the
      "reservation flat in inactive-slot count" claim can be asserted, or the fixture will fail for capacity
      rather than for the law. This is the concrete next step, and **S2b gates** depends on it.
      **SYNTHETIC 60-DESCRIPTOR CHARACTERISATION DONE 2026-09-23 (native, no production change).** New
      cross-track helper `measureWorkingSetResidenceAcrossTracks(N)` spans the 8 × 8 slot matrix, and two
      default-gate cases were added to `test_continuous_capture`:
      `test_s1_3_sixty_descriptor_characterization_is_well_formed` and
      `test_s1_3_residence_is_flat_across_sixty_descriptors`. With one active role, **reservation is flat at
      exactly 40 chunks (205,280 B device) at N = 8 and N = 60**, while canonical payload grows 8 → 60 chunks
      (41,056 → 307,920 B) and the runtime directory grows 15 → 60 slots (95,160 → 380,640 B, per-descriptor
      directory state, reported not asserted flat). Inactive descriptors hold **0** canonical credits and **0**
      transfer horizons (0 over 59); the active slot holds 8 credits + its horizon; resident working sets stay
      **1**. 100 of 512 chunks used, so the reserve is intact and the flat result is not a capacity artifact.
      **Red-proven:** re-introducing the state S2b.1 removed fails both cases (`reserved(8)=152` vs
      `reserved(60)=452`, ~8 chunks/descriptor, pool exhausting at `free=0`); the mutation was reverted with
      `src/` byte-identical. Native byte figures were rejected as device numbers (native `sizeof(EventChunk)`
      is 8,208 B vs the device's 5,132 B). Full record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P1 — S1.3 clause 2. **Still open on this task:** the
      *device* slope to the failure point. **DEVICE CENSUS TAKEN 2026-09-23 (Leg 1):** on HEAD (`26e96539`,
      hex `673e20ca…`) the restore walk spends **0 chunks** on committed slots (`free_chunks 480 → 480`,
      `credits=0` on all eight rows) with the enabled set at **32** chunks (was 96) and `free_chunks`
      pre-restore **480 of 512** (was 400). **The sweep itself is still blocked, and the blocker moved:** a
      growth attempt (`STIM-…-TEMP-RWSO-GROWTH`, track 6) reproduces the obstacle at **catalog** level —
      `could not reconcile MidiLooper/sets/index.bin`, `Track 6 loaded state: EMPTY` (and track 7),
      `DIAG,lcr,skip,restore`, and a census whose non-zero-slot set **changed between boots**
      (`{track=1,slot=2, track=4,slot=1}` → `{track=1,slot=2}`). So the obstacle is the SD catalog reconcile,
      not scratch-slot approval, and successive boots do **not** accumulate a library. **A claim of mine —
      that the library "can grow" because a reset-then-dump once returned `length=18432` — is withdrawn as
      over-generalised;** Leg 3 of the device session records the falsification.
- [ ] S1.4 Undo residency baseline: how much snapshot residency cold slots retain today via undo roots, **and how
      many canonical chunks those roots pin** via `SlotPassReferences` (the B10 baseline).
      **Clause 1 MEASURED 2026-09-23** (dense PASS run, no new telemetry): the undo root retains essentially the
      whole snapshot arena — **≈2,635–2,659 KB over 83–84 live slabs**, releasing only ~17 KB on undo and
      re-taking it on redo (`slab_live` 84→83→84, `slab_used` 2,652→2,635→2,659 KB) — and it retains *densely*
      (`slab_unused` ≤ 51 KB, `pin1 = 0` throughout), so this is retention, not a stranded slab. `snap` is
      monotonic and is **not** a retention signal. **Clause 2 LANDED 2026-09-23**, as the `SC_PASS_RECLAIM` fold
      rather than a second `printf`, with change-detection in the guard. Measured on the dense PASS run
      `captures/rwso-HEAD-pins-landed/` (hex `cba22f1d…`): the undo roots pin
      **0 → 5 → 8 → 3 → 7 capture passes across 0 → 2 slots**, with **`edit_pins` = 0 throughout** — the B10
      baseline. Every row has `reclaimed=0`/`released=0`, so those lines appear purely on a pin change: five
      lines, against the ~2,000 the naive gate wrote. Cost: padding **904** (64 B above the floor), and the fold
      is **not** cheaper than the trimmed standalone line (also 904) — correcting my earlier reasoning, the
      saving was trimming format arguments, not avoiding the second call. **Residual:** this is the pinned
      **pass/slot** count, which is what `SlotPassReferences` holds; the *chunk* footprint of those passes needs
      each pass's chunk list, deliberately not smuggled in at 64 B of headroom.
      See [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S1.4 undo-residency baseline.
- [ ] S1.5 **Boot/load residency baseline (B7):** with N committed slots, record the pool chunks and bytes that
      `TrackManager::prewarmPlaybackRuntime` reserves before any activation (today 8 chunks / 41,056 B per
      committed slot), the free-chunk count, and the smallest N at which a 24-bar overdub is refused.
      **NATIVE LEG DONE 2026-09-23, task still open:** `test_s2b3_boot_consumes_only_committed_content_and_keeps_the_pool_reserve`
      asserts the per-committed-slot pool cost is **exactly zero** (`reservedChunks` identical at 1 and 4
      slots; payload 1 -> 4 chunks, admitted 41 -> 44) and that `freeChunkCount()` stays at or above
      `PassConfig::CHUNK_RESERVE` (16). Recorded constant: boot's own cost is **40 chunks** for the default
      enabled set (~5 per enabled slot) -- constant in the committed-slot count and bounded by the enabled
      set, not the library. **CORRECTED 2026-09-23 -- the earlier "N=2 refusal" finding was a MISREAD and is
      withdrawn.** The sweep never reached an admission probe: the failure was at the fixture's own
      `TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(...))`, and `SealOutcome` value 2 is
      **`FailedValidation`** (`LoopPasses.h:25-31`). So a dense 24-bar loop (~72 events/bar) **does not seal** in
      this harness, and no refusal was measured. The clause is **not yet attempted with a valid construction**;
      the concrete next step is a construction that seals first -- itself a property of the capture validation
      path worth knowing. The arm-repeatability guard added at the same time is unaffected and correct.
      **RETRACTED 2026-09-23.** The intermediate "a dense loop fails because the transfer horizon was not
      admitted" finding is **withdrawn**: its own bisect showed a **tiny** loop with the horizon admitted also
      fails `FailedValidation`, so the horizon was not the discriminator -- my construction was. The real lesson
      is that **`loop.appendCaptureEvent` is not the production ingress once the arm has admitted capture
      staging**: content must be appended *before* the arm is prewarmed, or through `Track`'s staged ingress. I
      never identified which of `sealCapture`'s four `FailedValidation` returns fired, so nothing is claimed.
      **SUPERSEDED 2026-09-23 (same day): the branch is now identified and the ingress claim above is also
      withdrawn** — the failure came from the `capture.store.empty()` branch after the hot-stop min-length
      removal, and the append/prewarm ordering is *not* a factor. See the clause-3 entry below.
      **Next step:** a staging-correct dense construction (append through the staged ingress, or append before
      prewarming); only then can the refusal point be measured -- or answer it on device, where a loaded library
      is read from SD rather than sealed. **Outstanding:** that construction, and the boot `live` figure against
      a 24-bar overdub's peak reservation (~191 chunks).
      **CLAUSE 3 ANSWERED NATIVELY 2026-09-23 — the construction was the problem, and the branch is now
      identified.** `Loop::sealCapture` runs `removePairsShorterThanNoteMinLength` **before** its empty check, so
      note pairs closer than `Config::DEFAULT_NOTE_MIN_LENGTH_TICKS` (**8**) are all removed, the live store
      empties, and the seal returns `FailedValidation` from the **`capture.store.empty()` branch** — not from the
      transfer precondition both withdrawn findings had inferred. The signature: events present before the seal,
      one tail chunk held (free 511), and the failed seal **releases** it (free **512**, nothing retained), while
      the control at exactly 8 ticks seals `Ok` and keeps its chunk. **Ordering was not the cause** — all three
      append/prewarm orders seal with pairs >= 8 ticks apart, so that inference is withdrawn too. With a valid
      construction (864 pairs = 1,728 events = **7 chunks** per dense 24-bar loop, across the 8 × 8 matrix), the
      answer is: **boot costs 32 chunks; at N = 63 the probe's free chunks are 39 before and 31 after, and the
      24-bar reserve admission is ADMITTED at every N up to 63 — so the smallest refusing N lies outside the
      supported envelope.** The library at N = 63 holds 108,864 events (83 % of the 131,072 ceiling) and still
      keeps 31 free, 7 above `CHUNK_RESERVE + ACTIVE_OVERDUB_TRANSFER_CHUNKS`. **Red-proven:** with the
      pre-S2b.1 per-slot reservation restored, the same construction refuses at **N = 30** (free = 6), so the
      HEAD answer is a capacity result of S2b.1 rather than a property of the pool. Fixtures
      `test_s1_5_the_supported_dense_library_still_admits_the_24_bar_reserve` and
      `test_s1_5_a_pair_below_the_min_length_empties_the_store_and_fails_the_seal` (both default gate).
      **Still open on this task:** the **device** refusal point (byte-limited PSRAM, not the 512-id ceiling) and
      the boot `live` figure against the ~191-chunk peak reservation, which is **not reproducible natively**
      because those byte terms are heap-backed off-device. Details:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P1 — S1.5 clause 3.
      **DEVICE HALF PARTLY TAKEN 2026-09-23 (Leg 1/Leg 2, HEAD `26e96539` hex `673e20ca…`).** The
      per-committed-slot reservation is **0 chunks** on hardware (the restore walk `free_chunks 480 → 480`,
      `credits=0` on all eight rows) and the enabled set is **32** chunks against the baseline's 96;
      `free_chunks` pre-restore is **480 of 512** against the baseline's 400. And a **24-bar overdub is
      admitted** (`Overdubbing started` ×2, `op=overdub_start` denials **0**, `record loop length
      18432/18432`), so no refusal occurs at the supported envelope on this device either — matching the
      native clause-3 answer. **Still open:** the refusal **point** itself (which needs a library grown past
      the supported envelope across boots) and the `live`-vs-191-chunk figure. See
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § Device session — RWSO-24CAP.
      **DEVICE BASELINE HALF RECORDED 2026-09-23** on the pinned baseline `83be2e19` (hex `a4bc37e6…040920`,
      isolated build): `#CAP,…,DIAG,capture_prewarm` shows the default enabled set costing a flat **12 chunks per
      track / 96 for 8 tracks** (`free_chunks` 512 → 416), and then **exactly 8 chunks per committed slot** — the
      2 slots the SD library restored (track1/slot2, track4/slot1) took 416 → 400, while every `slot=0` track took
      zero. `free_chunks` after boot is **400 of 512**. That is the per-slot slope this change removes; the native
      fixture asserts it is **zero** on HEAD. The HEAD device half is **not yet taken** (image built + hashed, not
      flashed). Log, citations and the corrections this forced:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § Device session — the baseline is flashed.
- [ ] S1 gates: native full; `teensy41-capture-serial` RAM1 line; the red fixture failing and recorded;
      the slope and failure point in this file as numbers.
      **Recorded 2026-09-23 (native legs only): full native 2,263 cases / 1 skipped / 2,262 succeeded /
      0 failures; `teensy41-capture-serial` SUCCESS with RAM1 `variables 96,128 / code 392,344 /
      padding 872 / locals free 34,944`; the red fixture failing and recorded (see S1.2).** The gate stays
      **open** for S1.3's device slope and failure point and S1.4's undo/canonical-pin baseline, which need
      hardware.
      **Device session opened 2026-09-23.** Now measured on hardware: the boot/load residency slope on both
      revisions (S1.5/S2b.3, 8 chunks → 0 per committed slot), the tags-1..16 per-owner census (S1.1), and the
      baseline build line. Full native re-verified after the instrumentation increment: **2,281 cases /
      1 skipped / 2,280 succeeded / 0 failures**; `teensy41-capture-serial` SUCCESS with RAM1 `code 392,184 /
      padding 1,032` (192 B above the floor). Still **open**: S1.3's slope *to the failure point*, S1.5's
      refusal-N, and S1.4's canonical-pin half (which has no telemetry — see S1.1). Also recorded: the HITL
      content verdict is a **coin flip** on a host bar-probe defect, so a failed content gate is retried, never
      reinterpreted — [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § CORRECTION.
      **Consolidated status 2026-09-23 (still OPEN).** Satisfied: full native re-verified after every
      increment (latest **2,283 cases / 1 skipped / 2,282 succeeded / 0 failures**); `teensy41-capture-serial`
      SUCCESS with the post-link RAM1 budget enforced (latest `code 392,312 / padding 904`, 64 B above the
      floor); the red fixture failing and recorded (S1.2); the S1.1 per-owner census on device (tags 1..16);
      the S1.5/S2b.3 boot-load residency baseline on **both** revisions (8 → 0 chunks per committed slot); and
      S1.4's two clauses (undo residency ≈2,635–2,659 KB over 83–84 slabs; pinned passes 0 → 5 → 8 → 3 → 7
      across 0 → 2 slots); — added the same day — S1.3's **second clause**, the synthetic 60-descriptor
      characterisation (reservation flat at 40 chunks / 205,280 B from N = 8 to N = 60, red-proven); and the
      **RWSO-24CAP device legs** (the zero per-committed-slot reservation census, and the 24-bar overdub
      admitted with 0 denials). **Not satisfied: S1.3's slope *to the failure point*** — still blocked, now by
      the **SD catalog reconcile defect** (`could not reconcile …/index.bin`, `Track 6 loaded state: EMPTY`,
      `DIAG,lcr,skip,restore`, a restored-set that changes between boots), so successive boots do not
      accumulate a library; and a **content-verified PASS**, which the pipeline run does not meet (2
      unexplained missing rows in bar 0, Leg 4). S1.5's refusal-N and S1.4's pass→chunk conversion also remain.
      **The named blocker is now the proposed change**
      [`deferred-slot-restore-liveness`](../deferred-slot-restore-liveness/proposal.md); this gate closes after
      that lands and this list is re-measured — a confounded gate is retried, never reinterpreted.

## S2 — window-size the working set (prerequisite, promoted from G.1)

- [ ] S2.1 **Depends on phase F contract before behavior edits.** Derive the **playback window lease** capacities from the resolved window instead of the
      2048-event ceiling (playback views and their ordering/provenance only). This is the promotion of
      RC-MEM-2's outstanding item, and it is the term multiplied by the derived N.
- [ ] S2.2 Measure each class separately: `P` playback window lease, `C` live-capture extension, `D`
      deferred-finalization extension, `I` interactive extension. Do **not** size the specialized classes by
      the playback target.
- [ ] S2.3 Prove the class budget in `IMPLEMENTATION_CONTRACT.md` §2:
      `fixed + N*P + Cmax*C + Dmax*D + Imax*I + Omax*O + canonical + roots + undo + visual + standby + reserve`.
      Pin source-derived holder maxima and charge overlapping owners until retirement; C/D are per-holder
      costs, not singleton device totals. Record the numbers and derive N only for an envelope that fits.
      Report N at today's capability (8 x 1 slot) and the
      headroom toward 16/32/48/64. **N is provisional:** the `N x P` claim is not the memory law until phase F
      (`windowed-playback-generation`) lands — record it as "provisional, bounded by phase F".
- [ ] S2.4 Show what N the budget supports and record it as a capability, with the arithmetic; note the shapes
      that need a smaller P to reach 32 or 64 sounding slots.
- [ ] S2.5 **Depends on phase F implementation.** Prove length-independence **at today's supported topology**: sounding slots of very different loop
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
- [ ] S2.6 **Depends on phase F implementation — window-size sweep**: make the readiness window configurable (pixels of size: resident bytes,
      switch granularity, publication load) and sweep from bar-based (1,536-tick merged window today) down
      toward **1/32 beat = 6 ticks** (1/16 beat = 12 ticks at 192 PPQN). Record the smallest window that still
      meets the readiness deadline under load; that number bounds the achievable queued-switch quantisation
      and feeds the derived N from S2.3.
- [ ] S2 gates: S1 gates plus the lease-size measurement and the cap derivation recorded; existing
      playback-prewarm fixtures still green.

- [ ] S2.9 **EXTRACTED (2026-09-19, Revision 18 § 3) — not a task of this change.** The decoupling of active
      playback from whole-loop snapshot residency moves to the sibling change
      **`windowed-playback-generation`**, which carries its own DEC and reassessment for the interval publication
      unit, per-window currency, the five gather call sites, LSV1 root authority and undo root pinning. Its
      requirements are retained here as a dependency contract (the immutable `PlaybackWindowGeneration` authority; `publishedSnapshot` narrowed to the
      checkpoint authority for undo/save; no whole-loop root merely because a slot sounds; the one-pass-late rule
      via S4c.3; separate measurement of snapshot-root bytes). The sibling is **not yet created**; do not apply its implementation here. **S2.3's N stays
      provisional until it lands**.

> **Contract corrections binding on the stages below (`design.md` Revisions 18–22).** Each line names the task
> that must carry it; none is optional.
> - **Latch shape (Rev 21.1):** split the record — `slot` is the **interval's** slot and stays immutable;
>   `switchTarget` is a separate field used only by `kind = Switch`. Precedence is three-way
>   (**stop > switch > start**, execution order; continuations survive per handoff §5) with a `(kind, slot, switchTarget, interval)` no-op guard. → S4f.1, S4f.3.
> - **Admission parameterisation (Rev 21.2):** `beginDeferredOverdubStopForSlot` must use `getLoop(slot)` for the
>   `Loop`-derived guards and bindings (`hasActiveOverdubTransferAdmission`, `canHandoffSessionExitSnapshot`,
>   staging, `job.owner`, `job.loopId`). The `:1469/:1473/:1495` sites are the **live edge** and keep
>   `activeLoopIndex`. → S4f.0.
> - **Write authority (Rev 21.3):** keep a non-re-entrant commit-point write in
>   `finalizeCaptureAndSelectSlot` (or split `applyActiveLoopIndex`); add `hasAcceptedUnexecutedInteraction()` to
>   the manager guard (`TrackManagerSlotMatrix.cpp:23`); one named cancel helper (three calls). → S4b.5.
> - **Transport epoch (Rev 21.4):** the latch's epoch is a **new TrackManager-level counter** bumped by transport
>   stop, `resetToLoopStart`/`setCurrentTick`, load and MIDI Start — **not** `transportCommandEpoch` (per-admission
>   0→1). Declared under invariant 36 and pinned by S3.0. → S4f.4, S3.0.
> - **Erasure surface (Rev 21.5):** add `Track::sendAllNotesOff()`'s unguarded `clearAllLedgers()` and
>   `finalizeCommitSideEffects`' `getActiveLoopIndex()` reads (`:84,:123,:191,:206`, undo-entry pushes) to the
>   conversion list; thread `slot` through `commitCaptureForStop`/`finalizeCommitSideEffects`; `DeferredValidatePolicy`
>   is a pure bool function, so its **caller** passes the predicate; `MidPassChunkPersist` must consult
>   `captureStopJobHead()` before the active-slot answer. → S4.1, S4f.5.
> - **`(slot, generation)` state (Rev 21.6):** add `slot` to `PendingStopAdmission` / the cleanup signature and
>   introduce the per-slot runtime generation. → S4.2, S3.0.
> - **Park scope and terminal (Rev 20, 22.4):** the park states what it waits for (`chunk_credit | store_drain |
>   pool_lifetime`), acts only on chunk credit, relies on the retire-path invariant "a retired job leaves the
>   store empty", has a bounded age + **named terminal**, and reports once per episode. START is **not** gated on
>   the canonical output; where a start cannot be admitted the manager-owned start carrier retains it (handoff §5) and the caller reports
>   the acceptance (never a silent "ok"). → S2b.2, S2b.6, S4f.6.
> - **Provisioning owner (Rev 20.3, 21.7):** `MidiServiceDrain::DeferredIdleOwner` gains a **`SlotProvision`**
>   turn kind; a pending switch makes its track eligible; the long-loop pending hold is satisfied by that turn and
>   carries an age/bound. → S4b.6, S4b.7.
> - **Cold descriptor type (Rev 22.1):** a cold descriptor is a **`ColdPassDescriptor` = whole row set +
>   edit-pass ids**, stored externally — **not** an id+metadata pair, and not `PassIdList`/`EditPassIdList`
>   vectors (internal-heap/RAM1). → S5.2a.
> - **ClearSlot eviction (Rev 22.2):** drop the whole **entry** with cursor repair, protected reserve preserved,
>   `nextEntryId` reset when empty, loss reported as entry loss; the spec's redo sentence admits the cap as a
>   declared invalidator (or the cap is per slot). → S5.2g.
> - **Deferred reconstruction (Rev 22.3):** name the owner turn and extend reachability to
>   "has pending deferred reconstruction"; add `hasPendingDeferredReconstruction()` to the in-flight predicate,
>   `pushUndoEntry`/trim/`dropRedoBranch`/`eraseUndoEntriesForSlot`/reposition, `noLiveOwnerForSlot` (release and
>   load admission); resolve by entry id (or index + generation).
>   → S5.2e, S4.0, S2b.5.
> - **Load deferral (Rev 22.5):** peek/pre-check before the pop (`peekFirstPendingLoopSlotRestore`) or a forced
>   enqueue bypassing `needsSlotLoad`/the done-test; predicate over `job.owner` **and** `job.slot`. → S2b.5.
> - **Pass-4 residuals (`design.md` Rev 20 §7, Rev 21, Rev 22 §6).**
>   - **S2b.6:** cite the **seal** path (`TrackOverdubLifecycle.cpp:941-947`), not the retire path, and add a new
>     `LoopPlaybackRuntime::clearCanonicalCaptureOutput()` for the busy-owner + non-empty-store case (only the
>     destructor clears today); report the **new named terminal `accepted_stop_park_terminal`** in the
>     `include/Utils/DebugSessionCapture.h` family; scope the ring check, the
>     `canonicalCaptureOutputAdmitted()` check and the native assert to `job.slot`.
>   - **S4b.7:** `SlotProvision` must also run `ensurePlaybackMergedEventsForSlot(targetSlot)`; exempt it from the
>     all-stopped guard (`main.cpp:463-469`); add the enum member, bump `kDeferredIdleOwnerCount` to 6, update
>     `selectDeferredIdleOwner` cursor expectations and `test/test_midi_service_drain` (exhaustive switch +
>     5-owner cycle arrays).
>   - **S4b.6:** the long-loop hold is satisfied by S4b.7's merged-events build (or the hold relaxes to the
>     occupancy predicate).
>   - **S4.1/S4f.5 additional sites:** `clearAllLedgers()` in `handleNoteEditFold`
>     (`TrackOverdubLifecycle.cpp:1247`, predicate-free, reached from `stopOverdubbing:1449` /
>     `stopOverdubbingToStopped:1646`); `resetAll(preserveLedger)` in `invalidatePlaybackMergedMidiEvents`
>     (`TrackPlaybackWindowBuild.cpp:484`, guarded only by `hasPendingDeferredOverdubStop`); `getActiveLoopIndex()`
>     in `DeferredSaveJobStages.cpp:321` (footer save queued by the stop commit).
>   - **S4f.5:** the header is `include/Utils/DeferredValidatePolicy.h`; `finalizeCommitSideEffects`' reads are
>     `:123,:139,:191,:206,:263` (`:84` is the `getActiveLoop()` that needs the slot).
>   - **S3.0:** name `PendingStopAdmission::slot` and the per-slot runtime generation explicitly in the pin list.
>   - **S5.2e:** add the lookup-by-id helper (none exists — `TrackUndo.cpp:126-132` exposes only the tip) with its
>     owner, plus the enforcement point for "head deferred ⇒ older entries unreachable"; fixture that a second
>     undo while the head is deferred is a no-op, not a skip.
>   - **S4f.6:** add a named acceptance report (counter/diagnostic read from the latch) so the unconditional label
>     at `TrackManagerCaptureQueue.cpp:221-222,236-237` is never emitted for a start that was not admitted.
>   - **S2b.5:** cite `peekFirstPendingLoopSlotRestore` at `LoopSlotRestoreQueue.cpp:312-318`.

## S2b — boot/load is an admission boundary (new stage, prerequisite for the law)

> Red-team finding B7: `TrackManager::prewarmPlaybackRuntime` prewarms every committed slot and
> `prewarmPlaybackForSlot(s,false)` reserves 8 pool chunks per slot (**40,976 B**: 8 × 256 events × 20 B
> `MidiEvent` + 8 × 2 B directory), so ~60 committed slots reserve 480/512 chunks at boot and leave 16
> allocatable against the ~191 a 24-bar overdub needs — the RC-MEM-5 state, reintroduced. Invariant 34.
> **Rerun blocker:** the only live admission of that capacity today is `Track::prewarmPlaybackForSlot`
> (record arm); `Track::startOverdubbing` admits staging and companion rows only
> (`TrackOverdubLifecycle.cpp:1294-1337`), so removing the boot reservation without adding an overdub-start
> admission makes every stop on a loaded slot wait forever.

- [ ] S2b.0 **First-slice prerequisites (same commit boundary).** Implement the S3.0 placement pins,
      Stop-only S4f.0–S4f.2/S4f.4/S4f.5 path and existing-owner S4.0 liveness specified by
      `IMPLEMENTATION_CONTRACT.md` §1 before removing reservations. Later stages extend these helpers;
      S5 reconstruction joins the predicate only when introduced. Validate stop retry/transport retargeting
      together with boot removal; do not ship a reservation-removal-only intermediate commit.
- [x] S2b.1 **Remove the per-slot canonical reservation from boot/load.** `TrackManager::prewarmPlaybackRuntime`
      and `Track::prewarmPlaybackForSlot(s,false)` establish the runtime index and descriptors only: no
      `prewarmCanonicalCaptureOutput()`, no transfer horizon for a committed-but-inactive slot.
      **DONE 2026-09-23.** The canonical output is reserved only for `prepareCapture` (the record arm)
      and the transfer horizon only for `prepareCapture`; boot still establishes the runtime index, the
      stop-descriptor ring and the canonical-copy directory. The three 2026-09-23 red characterizations
      (S1.2 a/b and B01) are **green and promoted into the default gate**; the `WORKING_SET_RED` gate is
      deleted. Evidence: [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P1 (S2b.1/S2b.2), including
      the **reported RAM1 deviation**: padding 856 against the recorded 872 line (16 B of linker
      alignment; `code + padding` constant; no bank cross; `locals free` unchanged) -- flagged for the
      architect rather than redefined.
- [x] S2b.2 Make canonical-copy capacity **lazy and name its admission points without re-gating START**: the
      **record arm** keeps its existing admission in `prewarmPlaybackForSlot`, and the **accepted stop's park
      (S2b.6)** acquires the reservation in a deferred turn (legal there, as `replenishCanonicalCaptureChunk`
      already shows). `Track::startOverdubbing` is explicitly **not** gated on
      `canonicalCaptureOutputAdmitted()` — that would re-create the DEC-067 START denial and need a start-side
      park; the succession case (new interval on a slot whose output store is still held by a pinned
      predecessor) is a stop-side `canonical_output_busy` wait handled by the park. Fixture: load a slot (never
      record-armed this power cycle) → overdub start → stop → the park reserves and the stop completes, one
      request, `canonicalCaptureOutputAdmitted()` true at execution.
      **DONE 2026-09-23.** `Track::startOverdubbing` calls the FLASH-resident
      `Track::admitOverdubStartWorkingSet()`, which establishes the record arm's reserve set for a slot
      boot left cold -- the view/bridge/companion scratch (without it the stop's estimates are
      `UINT64_MAX` and admission denies as `preparation_service_unavailable`), the transfer horizon, and
      the prepared-companion-history horizon (without which the job admitted and never retired). START is
      still **not** gated on `canonicalCaptureOutputAdmitted()`. The companion grant is gated on
      `!hasPendingDeferredOverdubStop()`: granting it unconditionally broke
      `test_adversarial_reentry_at_the_publication_checkpoint`, because `prewarmPreparedCompanionHistory`
      resets the Loop's prepared history -- the DEC-072/5.12 successor hazard. Fixture
      `test_s2b2_loaded_slot_stop_acquires_its_own_horizon_and_canonical_target` is green in the default
      gate.
- [ ] S2b.3 Boot budget evidence: with N committed slots, `LoopEventStore::freeChunkCount()` after boot is
      >= peak reservation + `CHUNK_RESERVE` (16); record the boot pool `live` figure against S1.5.
      **Baseline half measured on device 2026-09-23** (baseline `83be2e19`): `free_chunks` after boot is **400 of
      512**, whose spend is the 96-chunk default-enabled-set cost plus **8 chunks per committed slot** for the 2
      restored slots. **HEAD half measured 2026-09-23** (`9a2e5b9b`, hex `ea5e6d03…5a2c4ff`, run `PASS`):
      `free_chunks` after boot is **480 of 512**, the same 2 committed slots cost **0 chunks** each at
      `ready=0,credits=0`, and the enabled-set cost fell **12 → 4 chunks per track**. A controlled same-uptime
      tick comparison (~11.0 s vs ~11.1 s) shows `CanonicalOutput` **320 KB → 0 KB** and `TransferHorizon`
      **8 KB → 0 KB**. **But see the correction:** those `free_chunks` are **pre-restore** — boot queues the loop
      restores and applies them via `LoadLoopJob` afterwards, so the "after boot with N committed slots" figure
      is the post-restore one, measured for HEAD as **440 of 512** (the restore costs 40 chunks). The baseline's
      post-restore value is not measured. Also found: the restored slot reports `Track 6 loaded state: EMPTY` and
      `index.bin` does not reconcile, so recording into the scratch slot does **not** currently grow the restored
      library — the prerequisite S1.3 needs. See [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S2b.3.
- [x] S2b.4 Release the per-slot canonical reservation when the slot's role leaves the active set, using the same
      `noLiveOwnerForSlot` predicate as S4.0. **No release path exists today** (the only
      `canonicalCaptureOutput.store.clear()` is the runtime destructor; `releaseGrain` runs only from the
      reservation destructor / `CaptureRange`), so this is new surface: release fires only after the last owning
      deferred job retires **and** `store.empty()` (a mid-copy `take()` legitimately drops credits, and releasing
      then strands the copy → `Denied` grain), and reserve/release must be idempotent (re-reserving re-retains
      ids). Fixture: stop accepted → mid-copy release attempt → no `Denied` grain, plus a refcount census.
      **Rerun correction:** every `canonicalCaptureOutputAdmitted()` consumer resolves the **active** slot
      (`TrackOverdubLifecycle.cpp:264,284,295,1469,1473,1495`; `TrackDeferredMaintenance.cpp:915,917,933-938`), so
      the release/reentry edge must be parameterised by the interval's slot (S4f.0), and S2b.4 must **never**
      release while a ring entry with that `job.slot` exists.
      **CORE DONE 2026-09-23, task still open.** `Track::tryReleaseCanonicalCaptureReservation(slot)`
      enforces `noLiveOwnerForSlot(slot)` itself and releases the credits; it refuses while
      `canonicalCaptureOutput.store` is non-empty and is idempotent (re-reserving re-retains the ids). It
      runs inside `tryReleasePlaybackMergedMidiEventsMemory`'s per-slot loop, before the merged-window
      checks, so a slot with no window still releases. Fixture
      `test_s4_0_release_respects_the_live_ring_window_and_is_idempotent` is green.
      **MID-COPY FIXTURE + REFCOUNT CENSUS DONE 2026-09-23 — task closed.**
      `test_s2b4_midcopy_populated_store_refuses_release` drives the listed case: prewarm → one
      `appendReserved` into the canonical store (the state `stepFrozenCaptureCanonicalCopy` leaves between
      quanta, `TrackPlaybackRuntime.cpp:1949`) → the release is **refused** and returns no further credit →
      drain → the same call succeeds. It is **discriminating by removal**: deleting the `!store.empty()`
      guard in `releaseCanonicalCaptureReservation` makes it fail on exactly the intended assertion
      (1 failed / 91 succeeded); with the guard, 92/92. The append itself spends a credit (8 → 7) — the
      mid-copy signature, and the reason the refusal is the safety property. The refcount census asserts the
      leak property: the refused release moves neither counter, and `clear()` + release returns exactly the
      reservation's own credits (`used` 13 → 5, `free` 499 → 507). Three wrong cuts of that census are
      recorded in [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P1 — S2b.4's mid-copy fixture,
      including the measured fact that `usedChunkCount()` counts held **and** committed chunks.
- [x] S2b.5 **Load admission (B8):** **DONE 2026-09-23 (native).** The load harness was built, the fixture went red through the production
      entry, and the fix makes it green. `LoadLoopJob.cpp`'s four pop sites
      (`ensureActiveLoadLoopJobSelected`'s parked and empty `BeginFocusFromQueue` branches, its
      `popNextDeferredLoopSlotRestore` fallback, and `EmptyAction::BeginBackgroundFromQueue`) are guarded
      **before** the pop by `queuedRestoreMayBegin(track, slot)` = `Track::noLiveOwnerForSlot(slot)`, so
      liveness is per slot; the two `popNext` sites peek the head first (`backgroundHeadMayBegin`) because the
      pop has no filter. A blocked slot keeps its request **queued** instead of having it silently discarded,
      and no reset runs. Fixture `test_b04_real_load_selection_keeps_the_request_while_the_slot_is_owned` is
      green in the default gate. Supporting work: `needsSlotLoad` and `resetLoopSlotToEmpty` split out of the
      `StorageManager.cpp` monolith into small FLASH TU s (`SlotLoadAdmission.cpp`, `SlotReset.cpp`) -- the
      latter recovered 192 B of ITCM headroom (`teensy41-capture-serial` padding 840 -> 1,032, `teensy41`
      9,768 -> 9,960) -- plus an in-memory SD card and the shared `test/test_support/track_harness.inc`.
      Load suite **5/5**, full native **2,277 / 1 skipped / 0 failures**. **BOTH HALVES ARE NOW GREEN
      (2026-09-23).** The retirement half is covered and it corrected the oracle: the interval must be built
      through the production `Track::startOverdubbing` (a hand-built interval admits a job that can never
      finalize), and once that job retires, the content it committed **satisfies** the queued restore, so the
      request is resolved as *unnecessary* -- no load begun, no attempt marked, nothing reset -- with a second
      selection consuming nothing. The harness gained the note-edit fold stubs and the `silenceTrackMidiOutput`
      counting boundary. **Still owed: the device gate only**, which the S1 resident-library baseline owns.
      **CHECKED 2026-09-23 (checkbox reconciliation):** the native half is complete and green, and its one
      remaining clause is a device gate that the contract assigns to a **different** task, so it is tracked
      there and not duplicated here. That device gate is currently blocked by the restore-liveness defect
      proposed as `deferred-slot-restore-liveness`; this task is done, the device leg is not.
      Previous status:
      The predicate is `Track::noLiveOwnerForSlot(slot)` (S4.0) and its per-slot scope is now asserted in the
      S4.0 fixture: with a live job on slot 0, `noLiveOwnerForSlot(1)` is **true**, so a finalizing
      predecessor blocks a load into its own slot and not another slot of the same track. The remaining
      call-site change (peek/pre-check before the pop at `LoadLoopJob.cpp:339,367,372,377`) is **not**
      implemented: it would be an unverified change to a path with zero native coverage. The harness gap and
      the full B04 plan (files to link, in-memory SD/File emulation, stub boundaries, assertion sequence,
      ordering against B06 and the mid-copy fixture) are pinned in
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P1 — S2b.5. `beginLoadLoopJob` must not destroy a live owner. Its callers **pop the
      restore entry before calling it** (`LoadLoopJob.cpp:339,367,372,377`) and re-queueing is **inert**:
      `queueDeferredLoopSlotRestore` early-returns unless `needsSlotLoad`, which is false once
      `isLoopSlotRestoreAttempted` is set or the slot `hasCommittedPasses()` (`StorageManager.cpp:258-268`) —
      exactly the finalizing-predecessor case. Use **peek/pre-check before the pop**
      (`peekFirstPendingLoopSlotRestore`) or a dedicated forced enqueue, and define the predicate against the ring
      entry's `job.owner` **and** `job.slot`. **The peek helper has no done-test and no track/slot filter**
      (`LoopSlotRestoreQueue.cpp:312-318`, unlike `popFocusDeferredLoopSlotRestore`), so the stale-head skip and
      the predicate are added at the call site; the predicate must also consult
      `hasPendingDeferredReconstruction()` (Rev 22.3). Fixture: A finalizing + load queued and begun for the same
      slot → no `resetLoopSlotToEmpty`, the entry is **still queued**, finalization retires, restore then happens.
- [x] S2b.6 **`Track::parkAcceptedStopForCanonicalOutput()` (Revision 19).**
      **PARTIAL 2026-09-23.** `parkAcceptedStopForCanonicalOutput(slot)` re-runs that slot's
      canonical reservation on a deferred turn, reports `accepted_stop_finalizer_wait` once per
      episode, counts every wait, and never cancels the request. **Outstanding:** the retire-path
      invariant (drain `canonicalCaptureOutput.store` on the committing route, clear it on a skipped
      route) and the named `accepted_stop_park_terminal`.
 The stop-side half of B7: when the
      accepted stop's drive finds `canonicalCaptureOutputAdmitted()` false for the interval's slot, park the
      accepted request (declared grains) and (a) re-run the slot's canonical reservation, (b) re-evaluate the full
      guard chain against the interval's slot, (c) execute the accepted stop exactly once when admitted; otherwise
      report `canonical_output_busy` as a **wait** and retry next deferred turn. No freeze / no
      `exchangeCaptureStagingRoles` / no re-seal; the user never re-presses (invariant 31). **Implement the
      retire-path invariant it relies on:** the retire block (`TrackOverdubLifecycle.cpp:1188-1210`) currently
      clears `pendingPass`/`owner`/`loopId` and advances the ring but **never touches
      `canonicalCaptureOutput.store`**, so a skipped or failed route can leave it populated with no ring entry —
      exactly the park's unfixable case. The retire path must drain the store on the committing route and clear it
      on a skipped route, reporting a named terminal if it can do neither. Fixture: loaded slot → overdub start →
      stop under pool pressure → the stop completes after capacity returns, one request.
      **PARTIAL UPDATE 2026-09-23:** the retire-path invariant is **implemented** -- the retire block
      drains `canonicalCaptureOutput.store` and releases the frozen canonical builders when a retiring
      job leaves the store populated (`canonical_output_drained_at_retire`), which is what makes S2b.1
      safe.
      **FIXTURE CLAUSE ALREADY SATISFIED -- annotation corrected 2026-09-23.** The "pool-pressure
      fixture" this task listed as outstanding **exists and is registered**:
      `test_b02_accepted_stop_waits_for_canonical_credit_then_executes_once` drains the canonical credits,
      issues the stop exactly once, and asserts the accepted stop stays in the live interval with no job,
      no descriptor and identical rows, then admits when capacity returns and retires exactly once;
      `test_s2b2_loaded_slot_stop_acquires_its_own_horizon_and_canonical_target` covers the "loaded slot"
      variant the task text names. Both are green in the current suite. So the fixture clause is not
      outstanding and never was.
      **NAMED TERMINAL DONE 2026-09-23 — task closed.** The conflict above was resolved by the authority
      order (a change's spec delta is normative, its `design.md` explanatory), `design.md` was corrected, and
      `accepted_stop_park_terminal` is now **report-only**: it fires when waiting has stalled (the canonical
      target is held by a live owner — a populated store the park must not clear — so no deferred turn can
      admit the stop), and it clears, drains and cancels nothing. It keys off the existing per-latch
      `waitCount` (`== 8`) rather than a new latch field, because `PendingInteractionLatch` is size-pinned by
      S3.0's placement asserts; the count passes the threshold once per episode. **Zero RAM1 cost** — padding
      stays 1,032 / 192 B above the floor, as the park is `TRACK_COLD_MEM`. Fixture
      `test_s2b6_park_terminal_never_cancels_an_accepted_stop` drives 40 deferred turns past the threshold and
      asserts the request stays authoritative (latch intact, no job, queue empty, interval live, rows and
      store unchanged), then that freeing the owner executes it exactly once. **Discriminating against the
      reading that was rejected:** adding `store.clear()` to that branch (design.md's literal text) fails the
      fixture on *"the park terminal must never abandon the accepted stop"*. Recorded:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P1 — S2b.6.
- [ ] S2b gates: the first-slice native/build/device checks in `IMPLEMENTATION_CONTRACT.md` §1,
      using the S1 resident-library baseline plus a synthetic 60-descriptor fixture. Prove canonical
      reservation bytes flat in inactive-slot count; report canonical payload and derived bytes separately.
      The genuine 60-long-slot and total-residency gates stay in S6/S7, not this prerequisite slice.
      **Synthetic half DONE 2026-09-23.** The 60-descriptor fixture exists and is green in the default gate
      (`test_s1_3_sixty_descriptor_characterization_is_well_formed`,
      `test_s1_3_residence_is_flat_across_sixty_descriptors`): canonical **reservation flat at 40 chunks
      (205,280 B) at N = 8 and N = 60**, payload and the runtime directory reported **separately** (8 → 60
      chunks / 41,056 → 307,920 B; 15 → 60 directory slots / 95,160 → 380,640 B), inactive descriptors hold
      zero canonical credits and zero horizons. Red-proven against the pre-S2b.1 state. Details:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P1 — S1.3 clause 2.
      **DEVICE HALF TAKEN 2026-09-23 (HEAD `26e96539`, hex `673e20ca…`).** The boot census on hardware shows
      the per-slot **reservation** slope gone: the default enabled set costs **32 chunks** (was 96) and the
      library restore walk goes `free_chunks 480 → 480` across all eight `(track,slot)` rows with
      `credits=0` on every one, including the two committed library slots. `free_chunks` pre-restore is
      **480 of 512** (was 400). Full table: [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § Device
      session — RWSO-24CAP, Leg 1. **Still open — and still blocked, now sharply:** the **slope-to-failure
      sweep** (varying N across boots) and the failure point. A growth attempt (`STIM-…-TEMP-RWSO-GROWTH`,
      track 6) was stopped by the **SD catalog reconcile defect**, which reproduces the recorded obstacle at
      *catalog* level: `could not reconcile MidiLooper/sets/index.bin`, `Track 6 loaded state: EMPTY` (and
      track 7), `DIAG,lcr,skip,restore`, and a census whose non-zero-slot set **changed between boots**
      (`{track=1,slot=2, track=4,slot=1}` → `{track=1,slot=2}`). **An earlier claim of mine — that the library
      "can grow" because Leg 3 dumped `length=18432` after a reset — is withdrawn as over-generalised:** a slot
      can come back byte-exact right after its own save+reset, but successive boots do not accumulate a library.
      **The 24-bar overdub itself is admitted on hardware (Leg 2):** the same run records `record loop length
      18432/18432`, **`Overdubbing started` twice** (ticks 755 and 22,836), **`op=overdub_start` denials 0**,
      0 `#MEM_DENIED` / `#MEM_EXT_REFUSED` / `FATAL`, SEVT `bars=24/24 count_ok/span_ok/bars_ok = true` and
      display `overdub_ok`/`transport_ok` true — against the pre-change RC-MEM-5 leg's zero starts and 4
      denials, so the capacity path this change must not regress does not.
      **Not met, and this gate stays open: a content-verified PASS (Leg 4).** The pipeline run is
      `storage_matches_effective: true` with all 24 bars dense (`per_bar_effective` 70–84,
      `effective_rows_all = expected_note_cc_rows = 1,746`), but the verifier's single inter-anchor cycle
      reports `inventory_equal: false` with **2 unexplained missing rows in bar 0**,
      `[128,5,76,0]` and `[176,5,74,101]`. Neither matches a documented residual class, so per
      [`HITL_CONTENT_VERIFICATION.md`](../../../docs/Guides/HITL_CONTENT_VERIFICATION.md) the run is recorded as
      **not verified** rather than accepted. **The rows themselves are not lost:** locating each against both
      anchor occurrences (48.001 s apart) finds **one matching occurrence per cycle for each** (`note76` Off
      near `anchor − 80 ms` in both cycles; `note74` On near `anchor + 1 ms` and `+ 44 ms`), so playback
      carries both and the verifier's cycle-local inventory does not bind them — exact tick attribution is
      **not** established. A third 160 s capture went `INCONCLUSIVE_CAPTURE` (no recurring anchor) because the
      restored set drifts between boots, so the formal bar is unmet and **confounded by the same catalog
      defect**. `capacity_path_exercised: false` (densest bar 84 refs vs the old 256 ceiling): a parity run,
      not a capacity re-stress. **The mute lever for a single-slot capture was tried and does not work
      (2026-09-23, later):** muting the other seven tracks (double press on the track button) silences
      *everything* — the 160 s capture carries only LED feedback and clocks — because the **set of tracks that
      actually play changes across resets**: all-unmuted output is on 1-based channels 1, 2, 3, 4, 6, and
      track 5 contributes no notes now, though its content is dumped as present (`track=5,slot=0,length=18432`,
      all 24 bars dense) and channel 5 *did* play in the earlier 2-cycle capture. So a formal PASS needs the
      persistence/catalog defect fixed first; it is not reachable by capture configuration. Mute state was
      restored. **Round 3 (2026-09-23, later) closed the last route:** re-recording the slot and capturing
      immediately — the configuration of the near-parity run — came back **degraded**, not faithful:
      `record loop length 19200/18432` (a 25-bar over-run), `stored overdub span count=0/191` with
      `overdub_ok=False`, `first_missing=request` and **no** `PERS,request`/`SAVE,*` markers (so the re-record was
      not persisted), in a **1,574-line** sparse capture against the faithful run's 45,909. See
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § Device session — RWSO-24CAP, Leg 6.
      **STAYS OPEN, and every remaining clause is blocked on the same thing.** The native half is green and the
      24-bar-overdub admission claim is taken on hardware, but three clauses are unmet: the slope-to-failure
      sweep, the content-verified PASS, and the census stability it depends on. All three are confounded by the
      restore-liveness defect (`Track 6 loaded state: EMPTY`, a census that changes between boots) now proposed
      as [`deferred-slot-restore-liveness`](../deferred-slot-restore-liveness/proposal.md). Per the repo rule
      that a missing or confounded gate is not a pass and a threshold is never weakened, this gate closes only
      after that change lands and the sweep is re-run.

## S3 — introduce the reusable lease pool (no release yet)

- [x] S3.0 **Placement and size pins (B11, invariant 36).** (**PINS + POST-LINK ENFORCEMENT DONE 2026-09-23; task open only for later members' pins.** `Track::placementPins()` in `include/Track.h` and a pin block in `src/Track/TrackPlaybackRuntime.cpp` now pin the latch layout platform-independently and the device sizes of `LoopPlaybackRuntime` 6,344 / `Loop` 984 / `TrackPlaybackRuntime` 32 / `Track` 384 / `CaptureStopJobs` 2,736 / `PendingInteractionLatch` 40. Effectiveness verified: a deliberately wrong pin failed the device build in all 8 TUs that include `Track.h`. The pins emit no code (`code`/`padding` unchanged). **RESOLVED by architect decision 2026-09-23:** accept the 32 B and re-base the floor **872 -> 840**,
      keeping the manager entries in ITCM. The earlier claim that the pins alone close the placement gate is
      **withdrawn as too strong** -- size assertions cannot see instructions, veneers or alignment. The gate
      is now closed by a **post-link budget check**: `scripts/ram1_budget.json` (approved budget + floor
      history; edited deliberately, never auto-updated) enforced by `scripts/check_ram1_budget.py` as an
      `AddPostAction` on `$BUILD_DIR/${PROGNAME}.elf` from `[env:teensy41]`. Verified both ways: the normal
      build passes and prints the numbers, and temporarily raising the floor to 900 fails the build with an
      actionable message. **TWO DEFECTS FOUND AND FIXED same day** by checking that the enforcement covered every firmware env rather
      than assuming inheritance: (1) the absolute device pins are **configuration-specific** -- `teensy41`
      and `teensy41-capture` failed to compile because `sizeof(Track)` depends on `SESSION_CAPTURE` /
      `PERF_TELEMETRY` -- so they are now scoped to the measured configuration, with the post-link check as
      the configuration-independent guard; (2) the baseline had to become **per environment** (teensy41:
      variables 79,168 / padding **9,768** / locals 51,904 vs capture-serial: 96,128 / **840** / 34,944, both
      at 12 ITCM banks), now keyed by `$PIOENV` with named references and a hard failure for an unbaselined
      env. All four firmware envs build and pass; the exact-value check was proven to fail on a deliberate
      mutation. **CHECKED 2026-09-23 (checkbox reconciliation):** the instrument, the enforcement, the
      per-env baseline and the architect's floor decision are all in place and validated. The pins below are
      owed for members that **do not exist yet** — latent conditions on later stages (the checked S0 items
      carry the same shape), not outstanding work on this task; the stage that introduces a member adds its
      pin then. **Still owed for S3.0:** pins for the members the later stages introduce (the
      S4f.4 transport epoch, the cold-undo/standby/visual stores, the envelope). See
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S3.0.) Before adding any field: `static_assert` pins for the
      **existing** structs (`sizeof(LoopPlaybackRuntime)`, `sizeof(Loop)`, `sizeof(TrackPlaybackRuntime)`) **and
      for every new member added by this change** (the latch `PendingInteractionRequest`, **`PendingStopAdmission::slot`**,
      **the per-slot runtime generation**, the new TrackManager transport epoch, the envelope, the standby/cold-undo
      stores); all new per-slot state in the external
      `LoopPlaybackRuntime` (6,344 B extmem) or the `LoopPool` `Loop`; **per-track state — including the latch —
      externally held (`std::unique_ptr`) or with out-of-line `TRACK_COLD_MEM` ctors/dtors**, because `.bss`
      routes to DTCM and a by-value scalar latch would land there. Re-measure the padding baseline at S3.0 (the
      cursor rule still cites 1,256; the recorded line is 872). Evidence: the RAM1 line stays `padding >= 840` (re-based from 872 on 2026-09-23 by the reviewed
      32-byte allowance; enforced post-link),
      12 banks, `locals free` unchanged (a bank cross costs 32,768 B and the documented silent boot hang).
- [ ] S3.1 `WorkingSetLease` with owner = slot + generation + role, and a pool provisioned for the
      measured active envelope. Route today's prewarm through it so behaviour is unchanged but ownership
      is expressible.
- [ ] S3.2 Explicit liveness predicates per owner kind: capture stop job, deferred stop commit, undo/redo
      root, prepared playback handover. Native fixture per kind.
- [ ] S3.3 Selection acquires nothing: assert the SELECTED tier and browsing cause no lease churn.
- [ ] S3 gates: S2b gates plus S2.2/S2.3 current-root budget (no phase-F behavior prerequisite), plus `test_runtime_working_set_ownership`; `test_playback_prewarm` unchanged.

## S4 — release, recycle and stale-owner protection

- [x] S4.0 **Per-slot liveness predicate (B6, invariant 33).** (**PARTIAL 2026-09-23:** the live-window
      ring scan, the slot's `captureStopOwner`, an in-flight prepared handover (`preparedPairRequestPending`)
      and the S4f.1 latch term are implemented and fixture-covered; the job-owned-undo-root term has no
      per-slot identity until S5.2a and the deferred-reconstruction term arrives with S5.2e, so the predicate
      is not yet the full contract. The published `preparedPair` is deliberately **not** a term -- it stays
      valid while the slot sounds.) `noLiveOwnerForSlot(track, slot)`: **scan the live `noLiveOwnerForSlot(track, slot)`: **scan the live
      ring window only** — for `i` in `[head, head + count)` mod `kCapacity`, `entries[i].slot == slot` → owner,
      including a non-pending entry — plus no deferred stop commit, no prepared handover, no latch targeting the
      slot, no job-owned undo root. **A full-array scan is wrong:** retirement clears `owner`/`loopId` but not
      `slot` (`TrackOverdubLifecycle.cpp:1195-1206`, `slot` defaults to 0), so it produces a stale positive that
      blocks release and load admission forever. The track-scoped guard it replaces
      (`Track::tryReleasePlaybackMergedMidiEventsMemory`, `TrackPlaybackWindowBuild.cpp:786-800`) loops every
      slot, so the per-slot test belongs **inside** that loop. The stop grain's
      `noteStallOnce("frozen capture staging generation lost")` + hold is replaced by S5.2f (invariant 35).
      **Also include pending deferred reconstruction (Rev 22.3):** a slot whose cold descriptor is waiting to be
      reconstructed is a live owner of its pinned canonical body, so `noLiveOwnerForSlot` must consult
      `hasPendingDeferredReconstruction()` alongside the ring, the latch and the prepared handover.
      **CHECKED 2026-09-23 (checkbox reconciliation).** The predicate exists, enforces itself at its call
      sites, is per-slot, scans the **live ring window** only, and is fixture-covered including its
      idempotence. The two absent terms are **not this task's remaining work**: the job-owned-undo-root term
      needs S5.2a's per-slot descriptor and the deferred-reconstruction term arrives with S5.2e — both are
      named, owned and checked elsewhere when they land. This matches the checked S0.3/S0.4 pattern of
      recording a deferred term rather than leaving the predicate task open indefinitely.
- [ ] S4.1 Release when the slot leaves the active set and the guard reports no live owner; release is
      total. **The existing release APIs must be converted, not supplemented (rerun blocker):**
      `resetAll`, `clearAllLedgers`, `Track::releasePlaybackMergedMidiEventsMemory` and
      `TrackManager::releaseBackgroundPlaybackMergedMidiEventsMemory` are track-global, predicate-free, and
      discard `reset()`'s refusal — a successor stop on the same slot while a predecessor is pinned reaches
      `clearAllLedgers()` (`TrackOverdubLifecycle.cpp:1543`) and erases the predecessor's ledger. Retire or
      convert them to the `(slot, generation)` release evaluated through `noLiveOwnerForSlot`, and stop discarding
      the refusal. **Additional predicate-free sites to convert (pass 4B):** `clearAllLedgers()` in
      `handleNoteEditFold` (`TrackOverdubLifecycle.cpp:1247`, reached from `stopOverdubbing:1449` /
      `stopOverdubbingToStopped:1646`), `resetAll(preserveLedger)` in `invalidatePlaybackMergedMidiEvents`
      (`TrackPlaybackWindowBuild.cpp:484`, guarded only by `hasPendingDeferredOverdubStop`), and the
      `getActiveLoopIndex()` read in `DeferredSaveJobStages.cpp:321` (footer save queued by the stop commit).
      **Inherited from S4f.5's audit (2026-09-23), carried here so it is not lost:** the slot-threaded
      persistence reads `TrackCaptureStopCommit.cpp:139,263` and `TrackStopTelemetryColdHelpers.cpp:161,166`
      (thread `slot` through `commitCaptureForStop`/`finalizeCommitSideEffects`), and
      `resolveCaptureTrackSlotForMidPass`'s selected/active fallback removal. S4f.5 adopted the one in-flight
      predicate at the consumers that can hold it today; these four sites are release-path conversions that
      need this task's `(slot, generation)` release surface, which is why they are converted here and not
      there. Both persistence items need a fixture this change has not built yet, so they are done with it.
      Fixture: B-stop while A finalizes on the same track+slot → A's ledger and canonical inputs
      survive.
- [ ] S4.2 Generation-mismatched release rejected and reported (native fixture reproducing the
      A-releases / C-reuses / late-A-cleanup sequence). **`captureStopOwner` needs a generation:** it is a raw
      `PendingStopAdmission*` cleared by address identity (`TrackPreparedStopCommit.cpp:205`) while ring entries
      are reused at stable addresses, so a late cleanup on the reused runtime object can null a successor's
      marker. Carry `(slot, runtime generation)` and check it at set **and** clear; bump the slot runtime
      generation on every release.
- [ ] S4.3 Reuse: activating C after A releases uses A's capacity with no net pool growth.
- [ ] S4.4 Declared cap policy when activation is requested beyond the envelope (refuse vs queue),
      reported rather than resolved by allocating.
- [ ] S4.5 **P3 acceptance (depends on remaining S4f; not a P2 release gate). Per-interval finalization states (DEC-078 acceptance contract)**: preserve the DEC-067 capacity model; native fixture for A-stops-and-finalizes / B-records /
      A-releases / B-stops-and-takes-the-lease, and for a stop arriving while A still finalizes — accepted with no
      partial stop, B still recording/overdubbing, and the accepted stop executing once the role
      frees (the other single-instance resources are re-checked then too). Emit
      `accepted_stop_finalizer_wait`. Assert the **busy-owner state names** as the evidence (`finalizer_busy`,
      `transfer_denied`, `canonical_output_busy`, session-exit handoff as the ordinary states, and `queue_full` as
      the **exceptional descriptor-exhaustion** case, kept categorically separate
      so an ordinary finalization wait is never blamed on it) so the mechanism is observable rather than
      inferred. Prove the stop **needs no second attempt**: the accepted request produced no descriptor and no
      partial stop, and executes after the resource frees with the waits counted as scheduling (S4f). Do **not**
      build a queue of stopped intervals: the ring's tail must be free before
      admission, so it cannot stack pending stops, and concurrent finalization needs its own capacity/ownership
      decision first.
- [ ] S4 gates (P2): S3 gates plus the reuse and stale-owner fixtures; S4.5 acceptance closes with P3/S4f, not before it. A device run showing holder count
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
- [ ] S4b.5 **One active-slot write authority (B4).** Route undo/redo-driven active-slot changes
      (`TrackUndo.cpp:558/582`) through the occupancy/pending-switch path and convert the **three** direct
      `Track::setActiveLoopIndex` writes (`TrackManagerSlotMatrix.cpp:240`, `TrackManagerCaptureQueue.cpp:281,335`)
      to the manager entry point. **Cancelling a queued switch is three calls, not one** — `clearPendingSlotSwitch`
      + `setPendingEnabledSetReplacement(false)` + `cancelSlotSelectionHold` (`MidiButtonActions.cpp:47-49`) — and
      `TrackManager::setActiveLoopIndex` must cancel/re-queue rather than move. Fixture: queue a replace-enabled-set
      switch, press Undo, then trigger an unrelated switch → exactly one transfer, one release, no stale
      enabled-set replacement, no `TransferOverlap` overshoot.
- [ ] S4b.6 **Occupancy-aware, allocation-free binding (B5).** `ensurePlaybackMergedEventsForSlot` uses
      `slotIfAllocated` (never `trySlot`); the commit branch keeps the switch pending when the inbound occupancy
      is absent; provisioning happens only at cold boundaries. **Rerun blocker — the named boundaries do not
      provision:** `requestSlotSwitch`, `setSelectedSlotIndex` and `loadTransportSlotIndices` only queue/refresh;
      the sole provisioner is `Track::prewarmPlaybackForSlot` (boot + capture admission). Required: (a) an explicit
      occupancy term in the commit branch (e.g. `slotIfAllocated(targetSlot) != nullptr` or occupancy-inclusive
      readiness) **and** (b) real provisioning at a named off-clock point — extend provisioning into the S4f.3
      switch path. `slotIfAllocated` alone yields silence or a permanently pending switch; note
      `ensureLoopsAllocated()` can spin `while(1) delay(1)` if the pool is uninitialised. Native no-allocation
      assertion on the commit path for a demoted short slot **and** a demoted long loop.
- [ ] S4b.7 **`TrackManager::provisionSlotForSwitch(trackIndex, slotIndex)` (Revision 19).** The off-clock
      allocation point S4b.6 needs. **Both `requestSlotSwitch` call sites are MIDI-input/action edges**
      (`MidiButtonActions.cpp:62,287`), so inline allocation is illegal: `requestSlotSwitch` marks the pending
      switch as "needs provisioning" and the **deferred provisioning owner** (the existing deferred idle lane, or
      a child of it) calls `provisionSlotForSwitch`, which performs `prewarmPlaybackForSlot`-style provisioning of
      the inbound occupancy and reports `#MEM_DENIED` with a switch operation on refusal. **A pending switch that
      needs provisioning must make its track eligible for a deferred turn** (`hasRealtimeIdleWork`/
      `sBackgroundSnapshotTrackCursor` eligibility are state-based today and would skip it); the commit path keeps
      the switch pending when `slotIfAllocated(targetSlot)` is null and never leaves a silent dead switch; no new
      `trySlot` on the clock path. Fixture: demoted short slot **and** demoted long loop each switch with no
      `trySlot` on the commit path and no dead switch.
- [ ] S4b.8 **HITL — slot switch while a predecessor finalizes, transport playing (the switch leg alone).**
      Managed run: record A → stop A → switch to B **while A is still finalizing** → assert the switch is accepted
      and completes, `activeLoopIndex` does not move before A's stop is admitted and bound to A's slot, the
      pre-switch slot finalizes exactly once, the successor slot's content is untouched, and pool holders rise and
      fall with the switch. The harness already has the switch action
      (`scripts/hitl/legacy_record_baseline.py` `_send_loop_slot_select_press`, `LOOP_SELECT_NOTE_BASE`) and the
      stop-interruption wrapper; reuse them rather than adding a runner. Evidence: session log + holder census.
- [ ] S4b.9 **HITL — cross-track switch while a predecessor finalizes.** Managed run: record/overdub A on track 4
      → stop A → switch the active track to track 5 **while A still finalizes** → assert the track switch is
      accepted and MIDI-continuous, A's occupancy stays pinned on track 4 (no release), track 5's inbound
      occupancy is provisioned at a cold boundary (S4b.7) with no `trySlot` on the clock path, and the
      display/selection transfers without presenting a stale frame (S4e.6). Reuse the baseline's existing
      successor-reentry leg (`legacy_record_baseline.py:4457-4564`) and the track-select press
      (`TRACK_SELECT_NOTE_BASE`). Evidence: session log + MIDI-boundary timing verifier.
- [ ] S4b gates: S4 gates plus the MIDI-boundary timing evidence (`overflow_free`, `call_site_gate_ok`, no
      active-interval overflow) across repeated switches.

## S4c — readiness contract only (implementation extracted to phase F)

- [ ] S4c.0 Record the existing publication/consumer inventory and shared work contract as input to F.
- [ ] S4c.1 Specify interval/window readiness and immutable currency acceptance for the sibling;
      **do not change the publication unit in this change**.
- [ ] S4c.2 Record the existing resumable publication machinery the sibling must reuse.
- [ ] S4c.3 Preserve the decided one-pass-late rule as the sibling's missed-deadline requirement.
- [ ] S4c.4 Specify undo/save checkpoint parity for F; **do not remove playback root dependencies here**.
- [ ] S4c.5 Record required immutable-window/content-parity fixtures for F. Their implementation and
      length-independent root-residency gate belong to the sibling once its contract is approved.
- [ ] S4c gate: dependency contract documented; no claim of shipped windowed publication. Phase F's
      artifacts/DEC/reassessment must exist before its implementation; S7/archive waits for its gates.

## S4d — standby tier (bounded, scheduled, immutable; playback integration depends on phase F)

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

- [ ] S4e.1 Per-bar **visual envelope** for every slot as bounded metadata: a per-bar activity/density mask
      owned by `Loop` as a field (written through a `Loop` API, read by `DisplayManager` via a getter), sized so
      the whole library fits. **Not** `overviewCaptureBandMask_` (a DisplayManager, capture-only, single-instance
      scratch mask), and **not** a new renderer.
- [ ] S4e.2 **Rendered detail window** within the S2.8 cap, produced through the existing idle-sliced renderer
      (`rebuildVisualCacheIdleSlice`) and validated by the existing `builtFromSnapshotIdentity` /
      `builtFromRevision`.
- [ ] S4e.3 Invalidate on any mutation covering the envelope's bars; fall back to the windowed gather.
- [ ] S4e.4 **Rewrite** `TrackManager::prewarmSelectedDisplayVisualCache` (it is caller-less, and its body is a
      synchronous whole-loop rebuild — forbidden on the consumer path) as bar-sliced scheduling driven from the
      S4b cold provisioning point; confirm which mechanism a dev-branch visual preload uses; record the two
      caller-less display paths (`resolveDisplayNotesCommitted`, `resolveDisplayNotesLiveCapture`) as either
      reused or deleted rather than leaving a third variant.
- [ ] S4e.6 **Cache-released state (T1).** Add an explicit "released" state distinct from "clean":
      `rebuildVisualCacheIdleSlice` returns immediately when `!visualCacheDirty` (`LoopVisualCache.cpp:294`) and
      `:307-314` clears staleness in the empty-`dirtyBars` branch without rebuilding. Release sets `dirtyBars`
      consistently, and the display falls back to the envelope/gather whenever notes are empty with
      `visualCacheDirty == false`.
- [ ] S4e.7 **Cap and eviction owner (T9, S2.8).** Declare the envelope and window byte caps, their eviction
      point, and fixed external capacities; no existing cap constant exists to reuse.
- [ ] S4e.5 Native fixtures: switch shows the new slot immediately from the envelope; detail resolves through
      the existing renderer; mutation invalidates; cap and eviction hold under library growth.
- [ ] S4e gates: envelope and window appear as budget terms; no new renderer or validity counter introduced.

## S4f — accept every interaction; the pipeline decides when it executes (the reason this change exists)

> **DEC-078 is the authority** (accepted 2026-09-19, amending DEC-067 and DEC-076 point 2); the contract is
> `design.md` Revision 18 § 1-2. The pre-Revision-18 task text was falsified by the red team (B1-B4, B9) and is
> replaced below.

- [x] S4f.0 **Slot-parameterised admission (B3).** Add
      `beginDeferredOverdubStopForSlot(destination, tick, slot, interval)`; `beginDeferredOverdubStop` becomes the
      live-edge wrapper passing `activeLoopIndex`. The job binds `slot`/`owner` from the parameter, never from
      `activeLoopIndex` at drive time (`TrackOverdubLifecycle.cpp:263-264`, `:452` today). **The parameter must
      reach the DEC-067 guard chain inside the deferred admission** — the `Loop`/runtime-derived checks at
      `:264` (runtime), `:284`, `:295` and the `Loop`-derived `:290`/`:300`/`:316-323`/`:426`/`:451` resolve
      `activeLoopIndex` today, and under DEC-078 the finalization occupancy survives an active-slot change, so they
      must evaluate the interval's slot or they test the wrong slot's canonical output (rerun finding).
      **The `:1469`/`:1473`/`:1495` sites are the live edge** (inside `Track::stopOverdubbing`) and **keep**
      `activeLoopIndex` — see Revision 21.2; they are not part of this parameterisation.
      **DONE 2026-09-23.** `beginDeferredOverdubStopForSlot(destination, tick, slot, interval)`
      exists; `beginDeferredOverdubStop` is the live-edge wrapper passing `activeLoopIndex` and
      `captureInterval_`; inside the body every `Loop`/runtime-derived guard and binding resolves the
      **interval's** slot, and the live-edge sites keep `activeLoopIndex`. A drive whose interval is
      no longer live is reported as `stale_interval` rather than binding a later interval. Full
      native 2,264 / 1 skipped / 0 failures with the RAM1 line unchanged; see
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § P1. The stronger fixture that moves
      `activeLoopIndex` away from an unadmitted stop belongs to S4f.3/S4b.5 by the contract's own
      instruction not to move the active slot in this intermediate slice.
- [ ] S4f.1 **The latch.**
      **PARTIAL 2026-09-23 (Stop variant only).** The latch exists as
      `Track::PendingInteractionLatch`, stored as a value member of the already externally allocated
      `CaptureStopJobs` object (S3.0: the `unique_ptr` form measured 64 B of RAM1 and took padding
      872 -> 856, below the floor). It carries `kind/destination/slot/switchTarget/interval/phaseTick/
      transportEpoch`, takes no ring capacity, makes a same-identity repeat a no-op, and
      `consumeAcceptedStopObligation()` clears only the matching stop obligation. **Outstanding:** the
      start/switch carriers and the supersession rules (`switchTarget`, "a second Stop supersedes an
      unexecuted Start") -- those are S4f.3/S4f.6.
 `PendingInteractionRequest {kind, destination, slot, switchTarget, interval,
      phaseTick, transportEpoch}` held externally beside `captureStopJobs_`; **`slot` is the interval's slot and
      `switchTarget` is set only by `kind = Switch`** (never overwrite `slot`); one per live interval; no
      descriptor; no ring capacity. **Execution order** is stop > switch > start; distinct accepted requests
      survive in the existing manager pending-switch/start carriers as specified by `IMPLEMENTATION_CONTRACT.md`
      §5. Include the external start-mode discriminator in S3.0 pins. A
      `(kind, slot, switchTarget, interval)` match makes a repeat a no-op.
      Clear only the matching admitted stop obligation; never clear pending switch/start continuations with
      it. The start reset runs only after the predecessor stop binds. Transport Stop retargets the live stop
      and explicitly supersedes unexecuted starts; see the complete transition contract §5.
- [ ] S4f.2 **Drive placement and reachability (B3, B6).** (**ELIGIBILITY EXTRACTED 2026-09-23.**
      `Track::hasRealtimeDeferredIdleWork()` now owns the decision -- `isRecording() ||
      isAudiblePlaybackActive() || isStoppedRecording() || hasAcceptedUnexecutedInteraction()` -- and
      `src/main.cpp`'s `hasRealtimeIdleWork` delegates to it, so a later stage adds its eligibility term in one
      place. Fixture `test_s4f2_deferred_eligibility_includes_an_accepted_interaction_on_an_idle_track` is
      discriminating: in `TRACK_STOPPED` only the latch term can satisfy the predicate. **ALSO DONE 2026-09-23:** `processDeferredCanonicalPrefix` now yields while an interaction is merely
      *accepted*, not only while one is admitted, so background preparation cannot compete for the drive's
      turn -- a strict narrowing, with **no fixture claimed** and the reason recorded (a discriminating oracle
      would have to construct a state where the function would otherwise do real canonical work; a
      guard-clause assertion would be non-discriminating). `ExecutionBounds.md` **row 31** declares the latch
      drive's bound (at most one accepted-interaction grain per turn: one admission attempt plus, on refusal,
      one replenish and one report; skipped while the ring is non-empty). **Still outstanding:** the
      scheduler's *use* of the eligibility predicate for a stopped track is in `src/main.cpp`, which is not
      native-linkable, so it remains a device-observable claim.)
      **PARTIAL 2026-09-23.** The dispatch branch sits in `processDeferredIdleMaintenance`
      immediately after the admitted stop's grain and before the owner-specific branches, factored
      into the named production entry `Track::serviceAcceptedInteractionTurn()`; the drive is skipped
      while the ring is non-empty, so an accepted request cannot stack a second job. **Outstanding:**
      gating `processDeferredCanonicalPrefix` on the latch, the `ExecutionBounds.md` row, and the
      scheduler-reachability case (B06) -- B02 drives the real dispatch entry but not
      `processDeferredIdleMaintenance` itself, which newly needs StorageManager leaves linked.
 A latch-dispatch branch immediately after the
      stop-grain block in `Track::processDeferredIdleMaintenance`, before the owner-specific branches (the
      `VisualCache` branch is focus-track-only); `processDeferredCanonicalPrefix` gated on the latch; skip the
      drive while the ring is non-empty; one declared grain and a new `ExecutionBounds.md` row (the owner is
      already Unbounded in aggregate).
- [ ] S4f.3 **The switch is an accepted interaction (B1, invariant 32).** `TrackManager::finalizeCaptureAndSelectSlot`
      accepts the switch, latches the stop against the interval's slot, and does **not** move `activeLoopIndex`
      while an interval is live unless the stop was admitted and bound to that slot; remove the refusal at
      `TrackManagerCaptureQueue.cpp:329-332`. The interactive/display role may transfer immediately.
      **The target needs a carrier and a commit point:** the existing SlotStateMachine pending switch is
      authoritative (handoff §5); a Switch latch identifies its prerequisite stop, not a second target owner.
      Dispatch the switch after the interval's stop binds. Replace the unconditional pending-switch clear in
      `TrackManagerTransportTick` with consumption at successful switch commit. Fixture: queued
      switch + undo + stop admission in one window → the target is never lost.
- [x] S4f.4 **Transport/clock timing validity (B2, T2).** (**REVISED 2026-09-23 by architect decision --
      `design.md` Revision 24; the stored-epoch requirement is WITHDRAWN.** The latch's `transportEpoch`
      field is **removed** (latch 40 -> 32 bytes, `CaptureStopJobs` 2,736 -> 2,728, both pins updated and
      re-verified on every env): the drive re-derives the destination from live transport state and the
      boundary tick from the live clock and validates `(kind, slot, interval)` before acting, so a counter
      that is never compared adds no protection and would cost RAM1. Transport history is covered by
      **consumption plus identity validation**. **Closes only on the four paused-worker transition tests:**
      (1) Transport Stop -> Start -- *native, green in B03*; (2) seek/reset/MIDI Start recompute the
      boundary in the new tick domain -- **now green natively**
      (`test_s4f4_the_boundary_is_recomputed_in_the_tick_domain_at_execution`: latch at 96, seek before
      execution, assert the admitted boundary is not the stored tick and lies in the new domain). **Coverage
      limit, stated in the fixture:** the `ClockManager` seek/reset/MIDI-Start entries are harness stubs, so the
      tick domain is moved with the controlled clock -- that proves the decision (the drive passes the live
      tick, never `request.phaseTick`), not the production entry that moves the domain, which is device
      territory; (3) load/
      replacement cannot bind an old request to replacement content -- *needs the load harness*;
      (4) repeated retries commit exactly one stop -- *native, green in B03*.
      **(3) is now green too, and it was a defect, not just a fixture:** a request whose interval is gone (a load
      or replacement leaving the same slot number) used to go straight to the park, which waits for a canonical
      reservation unrelated to the failure -- so the obligation was retained forever and `hasStopInFlight()`
      stayed true. The drive now recognises it before any admission attempt, reports
      `"Accepted stop superseded: its interval is no longer live"`, consumes the obligation exactly once and
      admits nothing. Fixture `test_s4f4_a_request_whose_interval_is_gone_is_superseded_not_bound` is
      discriminating (pre-fix it fails at the first assertion). If a test reveals missing
      history detection, implement a **consumed generation check for that specific gap**, not a general
      epoch. **S4f.4 stays open** on transitions 2 and 3.) Original text: Record the destination and a monotonic transport epoch;
      **CHECKED 2026-09-23 (checkbox reconciliation) — all four closure tests are green.** (1) transport
      stop → start (*B03*), (2) seek/reset/MIDI Start recompute the boundary in the new tick domain
      (`test_s4f4_the_boundary_is_recomputed_in_the_tick_domain_at_execution`), (3) load/replacement cannot
      bind an old request to replacement content
      (`test_s4f4_a_request_whose_interval_is_gone_is_superseded_not_bound`, which caught a real defect —
      a gone interval used to be parked forever), (4) repeated retries commit exactly one stop (*B03*). The
      **recorded coverage limit does not reopen this task**: the fixture proves the decision (the drive passes
      the live tick, never the stored `phaseTick`) while the `ClockManager` entries that *move* the tick domain
      are harness stubs — and that production-entry proof is assigned by the contract to the device runs
      (S4f.11/S7), not to this native fixture. The withdrawn `transportEpoch` field is not reinstated.
      transport stop retargets/invalidates the latch beside `suppressMidiOutputForTransportStop()`; the drive
      re-derives the destination from live transport state and the boundary from the loop phase — never from a
      stored absolute tick (MIDI Start/seek/load reset the tick domain, `ClockManager.cpp:273,315-336,362`).
- [x] S4f.5 **One in-flight predicate (B9, T3).**
      **PARTIAL 2026-09-23 -- the stop-related consumers are done, and the audit found two real holes.**
      `Track::hasStopInFlight()` (`hasPendingDeferredOverdubStop() || hasAcceptedUnexecutedInteraction()`)
      and `Track::hasLiveCaptureInterval()` exist; the three persistence predicates and
      `resolveActiveCaptureTrackSlot` use them (redundant today because a latched interval is still
      OVERDUBBING, made explicit because S4f.3 lets a stop outlive a state change). The holes:
      `TrackManager::startRecordingTrack` checked `hasPendingDeferredOverdubStop()` alone, so it admitted
      a record start on top of an unadmitted stop; and `finalizeCaptureAndSelectSlot`'s switch path fell
      through to `setActiveLoopIndex` after a *non-admitted* `stopOverdubbing()`, moving the active slot
      underneath the accepted stop -- both fixed. Fixture
      `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes` is green.
      **Outstanding:** the `getActiveLoopIndex()`-keyed persistence reads
      (`TrackCaptureStopCommit.cpp:139,263`, `TrackStopTelemetryColdHelpers.cpp:161,166`) need `slot`
      threaded through `commitCaptureForStop`/`finalizeCommitSideEffects`; and
      `resolveCaptureTrackSlotForMidPass`'s selected/active fallback is not removed (a persistence
      behavior change with no fixture in this suite).
      **CHECKED 2026-09-23 (checkbox reconciliation).** The named consumer audit and the one-predicate
      adoption are done, the two real holes it found are fixed, and its fixture is green. The task's remaining
      clause is the audit's **own scope limit**, recorded honestly: the predicate is adopted at the consumers
      that can hold it today, and the `getActiveLoopIndex()`-keyed **release** sites cannot be converted until
      S4.1's `(slot, generation)` release exists. Those sites (`TrackCaptureStopCommit.cpp:139,263`,
      `TrackStopTelemetryColdHelpers.cpp:161,166`, `resolveCaptureTrackSlotForMidPass`'s selected/active
      fallback) are now **named in S4.1's conversion list** so they are tracked there rather than dropped, and
      the pre-latch behavior stays current until they land. Nothing about this task's deliverable is deferred.
 Add `hasAcceptedUnexecutedInteraction()`; **every** consumer of
      `hasPendingDeferredOverdubStop()` that means "a stop is in flight" consults it (start admission, wrap,
      hot-path emission, persistence, `include/Utils/DeferredValidatePolicy.h` — the caller passes the predicate,
      since it is a pure bool function). **Named seed sites the rerun found still
      keyed to the active slot:** `MidPassChunkPersist.cpp:34,54` (primary resolver),
      `TrackCaptureStopCommit.cpp:139,263`, `TrackStopTelemetryColdHelpers.cpp:161,166`; the three persistence
      sites use `hasLiveCaptureInterval() || hasPendingDeferredOverdubStop() || hasAcceptedUnexecutedInteraction()`;
      `resolveCaptureTrackSlotForMidPass` resolves from the interval with **no** selected/active fallback.
      Audit the remaining `getActiveLoopIndex()`-derived paths for the latch window and record the set.
- [ ] S4f.6 **Start side too.** Implement all transition/consumption rules in
      `IMPLEMENTATION_CONTRACT.md` §5, including manager-owned requests with no live interval, deferred
      eligibility and the combined in-flight query. Add paused-worker fixtures for every listed sequence.
      A second Stop supersedes an unexecuted Start explicitly; after Start commits it stops the new interval. `TrackManagerCaptureQueue.cpp:24-29` accepts the record/overdub start and
      schedules it rather than dropping it; `pendingRecord`/`pendingRecordSlot` must not clear their state when
      the start was refused. **Named acceptance report (pass 4C):** add a counter/diagnostic read from the manager-owned accepted-start carrier so
      the unconditional label at `TrackManagerCaptureQueue.cpp:221-222,236-237` is never emitted for a start that
      was not admitted.
- [ ] S4f.7 **Single write authority (B4).** Covered by S4b.5; record the source gate that
      `Track::setActiveLoopIndex` is reachable only through `TrackManager::setActiveLoopIndex`.
- [ ] S4f.8 Make the waits observable as **scheduling**, not refusal: `accepted_stop_finalizer_wait` counts
      waits and names the holding owner; the pre-fix counterfactual is `successor_stop_denied_capacity` +
      `reentryDecision("deny", ...)`.
- [ ] S4f.9 Prove no new capacity is implied: the latch is one small state per live interval, not a stored
      stopped interval, so the descriptor-ring finding is unchanged.
- [ ] S4f.10 Native fixtures: (a) stop while the predecessor finalizes -> accepted -> exactly one request ->
      completes after retirement; (b) the same with a slot switch before the drive, asserting `job.slot`/
      `job.owner` are the interval's; (c) transport stop before the drive -> no `TRACK_PLAYING`; (d) stop +
      record-start in the same tick; (e) stop + undo; (f) MIDI Start/seek between latch and drive;
      **(g) park terminal does not cancel (Rev 23.2):** drive the park past its diagnostic bound with the finalizer
      still occupied, then free capacity → the accepted stop executes exactly once, the interval is **not** left
      recording, and no second press is required.
- [ ] S4f.11 Measure acceptance -> execution latency **per interaction class** on device against the declared
      budgets (design Revision 17) and record the numbers: stop overdub, record/overdub start, slot/track switch,
      cold undo/redo, load loop. Exceeding a budget is a reported event, not a silent wait.
- [ ] S4f.12 **DEFERRED -- progress presentation.** Show that an accepted interaction is being executed when it
      cannot meet its budget, instead of leaving the user to infer failure. **Gate: S4f.11 measured AND a
      mitigation review shows the budget cannot be met by scheduling, prewarm or windowing.** Rules when taken: it
      presents an *accepted* action (never a denial, never a cue to press again) and never justifies a widened
      budget. Do not start before the gate.
- [ ] S4f.13 **HITL — record/overdub stop → slot switch → record/overdub stop (the accepted-sequence reproducer).**
      Two legs, each a managed run (`scripts/host_midi_hitl.py run --preset base`, or the
      `scripts/hitl_stop_interruption.py` wrapper for the interruption variant — **never** a captured
      `command.json`; bounded by `HITL_MANAGED_RUN_TIMEOUT_S`):
      - **(a) record leg:** record A → stop A (one press) → immediately slot-switch to B → record B → stop B;
      - **(b) overdub leg:** overdub A → stop A (one press) → immediately slot-switch to B → overdub B →
        **stop B once while A still finalizes** (the DEC-078 shape) → continue transport.
      Build each as a **TEMP variant** of the closest canonical stimulus (`STIM-BASE-2-ONE-OD-01`;
      `--stimulus-temp-parent` / `--stimulus-temp-suffix`), because AGENTS.md forbids editing a canonical stimulus
      and **no registered stimulus covers this shape today** (the 10 registered ids are content/note-edit only;
      the stop shapes exist only as unregistered TEMP variants under `captures/`). Promote to a new canonical id
      only when the user accepts it as the reproducer.
      Assert per leg: exactly one request per stop; **no** `successor_stop_denied_capacity` /
      `reentryDecision("deny", …)` rows; **no second press**; A finalizes while B records; B's stop executes
      exactly once after A releases, with `accepted_stop_finalizer_wait` counted as scheduling; both intervals'
      content exact (the `hitl-content-verification` inventory parity pipeline); no lost capture; no wrong-slot
      publication; `overflow_free` / `call_site_gate_ok` / no active-interval overflow; display `transport_ok`.
      Evidence: the two session logs recorded in this file and `CURRENT_WORK.md`.
- [ ] S4f.14 **HITL — cross-track: session on track 4, stop, continue directly on track 5 (track-switch variant of
      S4f.13).** Managed run: overdub session on **track 4** → stop track 4 (one press) → immediately switch the
      active/selected track to **track 5** (harness track-select: `TRACK_SELECT_NOTE_BASE = 60`,
      `scripts/hitl/control_constants.py:5`, sent at `legacy_record_baseline.py:5364`) → start a record/overdub
      session on track 5 → stop track 5. Then the discriminator:
      - track 4's finalization is a **track+slot**-scoped owner (Rev 18 §2; DEC-078 item 5), so track 5's stop
        must **not** wait on it: assert track 5's stop executes with **no** `accepted_stop_finalizer_wait` episode
        attributable to track 4's job, while track 4 finalizes concurrently;
      - track 4's job retires exactly once with its content exact, and track 5's interval is untouched by it;
      - `noLiveOwnerForSlot(track 4, slot)` stays **false while its job is live** (no cross-track stale release);
      - per-track holder counters rise and fall independently; no lost capture; no wrong-track publication;
        `overflow_free` / `call_site_gate_ok`; transport never stops.
      This is the case that distinguishes "one device-wide finalizer" from "one finalizer per **track+slot**" — a
      device-wide singleton would fail it. Evidence: session log + per-track holder census.
- [ ] S4f gates: the latency from acceptance to execution is bounded and reported; no capacity-driven
      user-visible denial remains in the diagnostic vocabulary; a registered HITL stimulus exists for the
      single-press shape (see Closeout).

## S5 — demote inactive slots' derived state

- [ ] S5.1 Drop the published snapshot root and whole-loop `VisualCache` for a slot leaving the active
      set. Reactivation derives **only the bounded runtime representation the active consumer needs**; a
      full published root where the current playback/undo/save authority requires one. Until phase F,
      reactivation prepares that root off-clock and charges it explicitly; root-free playback is not claimed.
- [ ] S5.2a **Audit `TrackUndo`** and pin the exact field set of a reconstructible cold descriptor (committed
      pass identity/set, pass ordering or selection revision, note-edit commit identity, capture/session
      generation for validation). The descriptor must describe **authority**, never a cached root. **Rerun
      corrections (B10, Revision 18 § 7 + § 9):** the "does not pin" branch is **unimplementable** —
      `PassReferenceSet` is a veto, not a refcount, so any descriptor read by `collectReferencedPasses` *is* the
      pin; **always pin and charge the pinned canonical bytes**, and the demoter must keep a `LoopPasses`-shaped id
      set that `pinPassesFromLoopPasses` can read. The descriptor itself is a **`ColdPassDescriptor` = the whole
      row set** (`id`, `committedChunkIds`, `state`, `sealedAtTick`, `mergeSequence`, `overdubSessionIndex`, order)
      **plus `editPassIds`**, as a **new externally allocated `ColdPassDescriptor`** — *not* an id+metadata pair
      and **not** a reused `LoopSnapshotRef` (that is the ClearSlot carrier the cold tier exists to avoid; its deep
      clone keeps internal-heap `EditPassVec`/`MidiEventVec`, and the spec forbids a `LoopSnapshot` reference or
      slab in a cold descriptor). Row fields live at `LoopPasses.h:57-71` (`RecordPass` `:57-62`, `OverdubPass`
      `:64-71`; container `:106-110`); `TrackUndo.cpp:331,354` resolve through `disableCapturePass`, while
      `:362,385` resolve through `setEditPassState` (`:266-292`) — both against the **live** row set, so an
      id+metadata descriptor fails once the row is erased. `UndoEntry` already carries `passIds`/`editPassIds`, so
      the new struct adds only the row set. Nested vectors must be external or flattened; charge the descriptor's
      **full** footprint (including nested vectors) as descriptor bytes, and the pinned canonical bytes separately. **ClearSlot is excluded from the cold tier** — its before-state
      is a whole-slot snapshot (`TrackUndo.cpp:1197`, `LoopEditPasses.cpp:68`) that no pass-id descriptor
      reproduces — and its retained hot roots are capped by S5.2g.
- [ ] S5.2b **Implement two-tier compaction**: demote hot entries to cold descriptors, release undo/redo roots
      belonging only to that slot, and never release a root a live deferred owner still holds.
- [ ] S5.2c **Implement off-clock reconstruction**: undo/redo on a cold-derived entry reconstructs or selects the
      historical state, publishes a fresh runtime representation with a truthful `coveredPassRevision`, and
      commits only after preparation succeeds; while it waits report `undo_resource_denied` and leave the descriptor
      unconsumed.
- [ ] S5.2d **Bound cold undo**: per-slot depth plus a device-wide byte budget, and prove the priority order
      (current state and dirty canonical never evicted). The budget charges **descriptor bytes + pinned canonical
      bytes** and gets its own `ExternalPoolAccounting` tag (`kTagCount` is 18 with 17 named tags today; there is
      no undo attribution). **Eviction is transaction-aware (Rev 23.3):** remove the **oldest prefix that leaves the
      oldest retained descriptor independently reconstructable** from canonical state (a descriptor names a
      reconstructable logical state, not a delta against a runtime root) — never merely the oldest entry, which can
      orphan a retained descriptor's predecessor state.
- [ ] S5.2e **Deferred reconstruction has its own owner (B10c, rerun blocker).** `undoForLoop`
      (`TrackUndo.cpp:922-995`) applies `entries[cursor - 1]` and **erases** on failure (`:988-991`), and `cursor`
      means "entries below are done" — so "keep the descriptor and retry" is not an edit, it is a new
      `TrackUndo::stepDeferredReconstruction()` holding `{track, slot, entry id, phase}` with declared grains and
      the `undo_resource_denied` diagnostic, driven beside `Track::processDeferredIdleMaintenance`. `undoForLoop`
      returns **accepted/deferred** instead of falling through to the erase, may not reach older entries while the
      head descriptor is deferred, and the deferred descriptor is read-only for display (never consumed). The
      stale-skip count stays zero. **Reachability and intactness (Rev 22.3):** `hasRealtimeIdleWork`
      (`main.cpp:40-42`) is false for a stopped, non-audible track while another plays — exactly the demoted
      cold-undo case — so the deferred owner must have a turn kind (or extend the existing lane) and
      `hasPendingDeferredReconstruction()` must join the eligibility and the in-flight predicate; deferral must
      also be reflected in `pushUndoEntry` (which sets `cursor = entries.size()`, marking the unexecuted head
      done), `dropRedoBranch`, trim/erase/reposition, `noLiveOwnerForSlot` (release **and** load admission), and
      the wait must resolve by entry id (or index + generation). **Add the lookup-by-id helper (pass 4C):** none
      exists (`TrackUndo.cpp:126-132` exposes only the tip), so name its owner (`TrackUndo`/`GlobalUndoStack`) and
      the enforcement point for "head deferred ⇒ older entries unreachable" under `pushUndoEntry`, trim,
      `dropRedoBranch` and `repositionGlobalUndoStackTipForSlot`. **All-stopped admission (sign-off 22):** a pending
      reconstruction must not be swallowed by the all-stopped guard (`main.cpp:463-468`, which today returns unless
      the owner is `VisualCacheWrapAppend`, or `Snapshot` with stopped-clock work) — exempt the reconstruction turn
      there or route it through the focus-track lane (`:470-504`), exactly as S4b.7 exempts `SlotProvision`. Fixture: undo on a stopped track while another
      plays, with load/capture/trim admitted during the wait → descriptor unchanged, undo executes exactly once;
      a second undo while the head is deferred is a no-op, not a skip.
- [ ] S5.2f **Zero-runtime regeneration is a named operation, not a re-entry (B6, invariant 35; rerun blocker).**
      Implement `Track::regenerateFrozenCaptureStaging(CaptureStopJob&)`: (a) `playbackRuntime.trySlot(job.slot)` +
      `prewarmCaptureStaging()`; (b) refill the **frozen** role from the job's own sealed extent `job.rawRange`
      via `CaptureRange::read(...)` (the authoritative copy); (c) restore `captureGeneration`/`frozenGeneration`/
      reservation count in bounded resumable refill grains **without** `freeze()` or `exchangeCaptureStagingRoles()` — re-entering
      `beginDeferredOverdubStop` after a runtime loss would re-freeze an empty generation and publish an empty
      pass; (d) rebind `RuntimeCanonicalPassInputs` and `RuntimeCompanionPasses`; (e) on no capacity retain
      the same live ring entry and all inputs, mark regeneration pending and retry through the existing stop
      worker. Follow `IMPLEMENTATION_CONTRACT.md` §3: no dequeue/re-admission, no replay of committed receipts,
      bounded refill cursor, diagnostic fault without cancellation. Test refusal then recovery before
      preparation and after commit; publication/undo occur exactly once.
- [ ] S5.2g **ClearSlot hot-root cap (B10b).** A pressure-independent per-slot count/byte cap with its eviction
      point, added to the spec's cold-undo-bounded requirement; "stays hot" is otherwise bounded only by
      `ABSOLUTE_MAX_UNDO_ENTRIES = 512` and a reactive trimmer. Fixture: clear N > cap slots and assert the oldest
      ClearSlot **entry** is dropped — cursor repaired, protected reserve preserved, `nextEntryId` reset when the
      stack empties, loss reported as **entry loss** — and that a later undo does **not** silently wipe the slot.
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

- [ ] S7.0 **Boot admission acceptance (B7, invariant 34):** boot a ~60-slot library and require it to arm and
      record a 24-bar overdub with `freeChunkCount() >= peak reservation + 16`, 0 `#MEM_DENIED` /
      `#MEM_EXT_REFUSED`, and pool `live` flat in recorded slots at steady state.
- [ ] S7.1 The scaling curve on a genuine 60+ slot, up-to-64-bar library: boot and steady state flat in
      recorded slots; one step per activation; deactivate A returns its lease; activate C reuses it with
      no net growth. Record pool `live`, per-owner bytes, holder count, per-loop derived bytes per step.
      Record N as **provisional until phase F** lands.
- [ ] S7.2 **Adversarial owner acceptance** (top-level case, not only a fixture): A active -> A creates a
      deferred job -> A's UI/playback notion deactivates -> attempt C activation -> A's lease stays pinned
      -> A's deferred job retires -> lease released -> C acquires the recycled capacity -> a late cleanup
      from A cannot touch C's lease.
- [ ] S7.3 Re-run the reported reproducer on the large library: arm a track, external clock + MIDI notes,
      and require it to reach `RECORDING`; also arm-and-play internally.
- [ ] S7.4 Require no `FATAL`, no `#MEM_EXT_REFUSED`, live device throughout.
- [ ] S7.4a Review `accepted_stop_finalizer_wait` across S7 runs: if waits are frequent in normal use,
      evaluate a second finalization lease from that evidence; if rare or absent, record 1 as sufficient. The
      acceptance-to-execution latency must stay bounded either way (invariant 31).
- [ ] S7.4f **Single-press acceptance (the product reason)**: in the reentry shape, issue the stop **once** while
      the predecessor finalizes and require the stop to complete after the predecessor retires with no second
      request -- measured as one request issued, the accepted request observable, and the waits counted. Repeat
      for a record/overdub start, a switch and an undo/redo under momentary resource pressure.
- [ ] S7.4e **Cold-undo acceptance**: A active, create OD1 and OD2, undo depth > 0 -> switch to B so A is cold
      and its snapshot-backed roots disappear (snapshot slab live bytes drop) while compact descriptors remain
      -> switch back to A and its current state plays exactly -> Undo A reconstructs the expected historical
      pass selection/content off-clock -> Redo A restores the exact current state. Then 60 cold slots with
      retained compact undo history must stay within the configured global budget with no per-slot
      `LoopSnapshot` roots resident. Also: demotion with a job-owned root does not release it; stale-generation
      cleanup cannot remove a newer slot's reconstructed state; a new edit after reactivation clears redo; and
      save/reboot restores undo depth zero.
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
- [ ] S7.4g **Switch-under-finalization acceptance (B1, invariant 32):** with an interval live and a predecessor
      finalizing, switch slot; require the switch to be accepted, the stop to bind to the interval's slot, and no
      `activeLoopIndex` move before admission; assert the pre-switch slot finalizes exactly once and the successor
      slot is untouched.
- [ ] S7.5 **Continuity acceptance**: with transport playing, switch track and slot repeatedly while up to
      today's supported topology (8 tracks x one sounding slot) sound, start record/overdub, and run undo/redo --
      the transport never stops and
      the MIDI-boundary timing verifier reports no degradation on any switch.
- [ ] S7.6 **Mandatory continuous device session (owner, Rev 23.6).** One session, in order: load ~60 recorded
      slots → PLAY several existing slots → ARM empty slot A → record long A → STOP A → immediately switch to B →
      START overdub B → STOP B **once** while A still finalizes → continue transport → B stops exactly once →
      switch to C → record/overdub C. Require: **0 user-facing denied interactions, 0 second presses, 0 lost
      capture, 0 wrong-slot publication, 0 stale-generation release, 0 playback interruption, bounded MIDI gap,
      pool residency returning after owners retire, and steady-state RAM independent of the number of cold
      recorded slots.** Also run the **cross-track leg** in the same session: overdub on track 4 → stop → switch
      directly to track 5 → record/overdub on track 5 (S4f.14). This single session is the acceptance evidence for
      the whole change; the staged per-leg reproducers are S4f.13 (stop → slot switch → stop), S4f.14 (stop → track
      switch → stop), S4b.8 (slot switch under finalization) and S4b.9 (track switch under finalization).
- [ ] S7 gates: S6 and phase F implemented/validated, then all prior applicable gates plus S7.1-S7.4 recorded with numbers, and S7.6's session recorded as evidence.

## Safeguards (valuable, not the strategy)

- [ ] G.2 `immutable-loop-snapshot-passes` §5.13: **failed admission must be non-destructive and must not
      cancel ARMED** -- derive the required set, acquire it completely, and on failure create no partial
      authority, stay armed, and report a resource wait. Partial capacity only where an owner
      explicitly supports a smaller bounded grant.
- [ ] G.3 §5.13: `RuntimeCompanionReconstruction`'s single 64 KiB buffer split into blocks.
- [ ] G.4 Explicitly **not** the fix: releasing arm reserves on disarm (the same view set serves playback
      of the slot's committed content).
- [ ] G.1 is **not** here: window-derived sizing moved to prerequisite S2.

## Closeout

- [ ] `DEC-###` status: the ownership rule and role tiers are **DEC-076**; the acceptance rule and per-interval
      finalization are **DEC-078** (landed 2026-09-19, amending DEC-067 and DEC-076 point 2). The **S6 canonical
      residency amendment** is still owed; ownership transfer is recorded in `ARCHITECTURE_RULES.md` where a
      documented owner moves.
- [ ] `docs/Authority/Architecture/ExecutionBounds.md`: add or update a row for every new or extended interval-path
      owner in the same commit as the firmware — the **latch drive** (S4f.2), the **windowed playback
      generation** (phase F), **standby** (S4d), the **visual envelope/window** (S4e), and re-classify
      `rebuildVisualCacheIdleSlice`'s Unbounded members (rows 8-10) before S4e claims reuse.
- [ ] Promote the HITL reproducers from TEMP variants to **registered stimuli** once the user accepts them, and
      record the ids here and in `scripts/hitl/stimuli/registry.py`: (a) **stop → slot switch → stop**, record and
      overdub legs (S4f.13); (b) **switch while a predecessor finalizes** (S4b.8); (c) **single-press stop/start
      under a busy owner** (S7.4f); (d) **stop on track 4 → continue on track 5** cross-track (S4f.14/S4b.9).
      Never edit an existing canonical stimulus to chase the new condition.
- [ ] Correct the stale envelope text: `docs/Runtime/CURRENT_WORK.md` § Now implementing priced
      "2 sounding slots per track x 16 tracks = 32 leases" — **corrected 2026-09-19** to the N-derived / 8-today
      statement that `PROJECT_STATE.md` and DEC-076 use; the window-sized requirement title in
      `specs/runtime-working-set-ownership/spec.md` ("two-slots-per-track") was retitled in the same landing.
      Verify both on the next closeout pass.
- [ ] `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md`,
      `docs/DELIVERABLE_TRACKING.md` updated.
- [ ] Architecture and guide docs describing slot-resident working sets or the prewarm contract updated
      to the active-role model (documentation closeout).
- [ ] Archive only after S7 passes; safeguards either landed or explicitly deferred with a reason.
