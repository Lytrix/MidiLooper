> **Current closure order (2026-09-28, reviewed at `bd7b3bba`):**
> [Shared finalization closure plan](S4F-CLOSURE-PLAN.md) identifies preparation/retry,
> seal-scope and publication-preflight blockers before playback/admission cutover,
> followed by correlated HITL recipes and same-boot qualification. It supersedes
> historical “next slice” pointers, not task acceptance or checkboxes.

<!-- 2026-09-25 S1.3 — residency slope: repaired single-session host fixture and added
seven orchestration tests. No device gate changed. Unfinished !SLOT_PAYLOAD probe withdrawn;
stored-event parity and reboot survival remain open. See takeover packet in ARCHITECTURE-REVIEW.md. -->
# Tasks — runtime-working-set-ownership

**Current continuation:** [NEXT-STEPS.md](NEXT-STEPS.md) supersedes historical
queue/hold statements below. DMA fault repair is parked until AFTER this OpenSpec
by user request, with no stage-gate waiver. S4.1 — persistence identity during
handoff was investigated and closed as verified on 2026-09-26 (DEC-090, no firmware
change); the next recommended native slice is named in that same file.


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
device legs are blocked by the restore-liveness defect that was **archived 2026-09-24** as
[`2026-09-24-deferred-slot-restore-liveness`](../archive/2026-09-24-deferred-slot-restore-liveness/proposal.md)
(reduced scope; the runtime measurements and the two new deferrals S2b.7/S2b.8 were transferred here).

**Checkoff (2026-09-24, eleventh session): 23 → 25 of 147.** **S2b.8** and **S2b.7** are checked with their
own evidence — the first two tasks completed *here* rather than reconciled from earlier work, and the +2 in
the denominator is the transfer of S2b.7 and S2b.8. S2b.8 delivered the external resets; **S2b.7 delivered
the drain-side restore service** (it first *measured* the absence of advancement, then implemented it — both
records are in the task text, so the measurement and the delivery are not confused). No other task changed
state, and **no native task remains open in this change**.

**Transfer in (2026-09-24, tenth session).** The restore-liveness sub-change was archived as
`2026-09-24-deferred-slot-restore-liveness` with a reduced, accurately described scope. Its unfinished runtime
measurements are **transferred here, unchecked**: S1.1, S1.3, S1.4, S1.5, S2b.3, S2b.5. Two clauses it
explicitly did **not** deliver became the named follow-ups **S2b.7** (drain-driven restore advancement, bound
by `resumable-midi-boundary-playback-maintenance` task 3.3) and **S2b.8** (set-change epoch invalidation).
The archive did not complete these; it transferred them, so nothing here is a pass by relocation. `DEC-082`
records the restore-request ruling.

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

## Heap-correctness prerequisite (2026-09-25)

- [x] S1.6a **Physical-pool accounting:** exclude internal fallback pointers, balance original tags on free/address reuse, and reuse a tombstone in a fully visited table. Native production-wrapper regressions plus capture-serial build; document heap-tail semantics and 96 KiB diagnostic footprint.
  **VERIFIED 2026-09-25:** three red native regressions → 5/5 focused; full native 2,389 passed / 1 skipped / 0 failures; capture-serial build and flash, RAM1 unchanged (code 391,304 / padding 1,912 / locals 34,912). Preflight N=56, 403 chunks, zero untracked/unknown. Evidence `captures/heap-accounting-image/`, `captures/heap-accounting-preflight-01/`.
- [ ] S1.6b **Failure attribution:** capture-gated caller PC on wrapper refusal, resolve against pinned flashed ELF in the N=56 canonical 24-bar fixture; distinguish allocation owner, retained memory and fragmentation. Native/build evidence alone does not close this device task. Repeat `captures/heap-rerun-24bar-01/` on stopped-diagnostic image reproduces 8192-byte DisplayNote vector growth failure in RECORD; exact display container and leak/fragmentation remain unresolved.
  **PARTIAL 2026-09-25:** canonical 24-bar run again fails during RECORD: 5,216 bytes, caller `0x6000949b`, resolved via pinned ELF to `DisplayManager::resolveDisplayNotesByDrawSource` (call instruction `0x60009496`). Physical wrapped live 8,142 KB, internal arena tail 4 KB, zero untracked/unknown. This identifies the current allocating owner, not the older 8,192-byte request, exact source expression, or retained-versus-fragmented breakdown. Evidence `captures/heap-attribution-24bar-01/allocation-attribution.json`. Next: distinguish frame temporary/copy capacities and their lifetimes before changing display admission.
- [x] S1.6c.1 **Known owner lifetime fixes:** fully release whole-take origin/rank arrays; reuse pending paint backing for draw-source compose; filter slide window in place; retire obsolete paint before replacement allocation. Native release/readmission/selected-owner and allocation/parity tests, firmware budget and original HITL revalidation. Red→green release and explicit paint-retirement regressions; focused 301/301, full native 2,399 passed / 1 skipped; capture-serial SUCCESS with unchanged RAM budgets. Original HITL `heap-ownerfix-24bar-02` still fails on a 6096-byte display-vector reserve in RECORD; aggregate S1.6c remains open.

- [ ] S1.6c.2 **Snapshot construction reuse:** reuse only candidate-private pitch/page/membership edits across changes; preserve membership tails/counts, immutable parents/siblings and failure semantics. Native node-growth and isolation regressions, full native, target budget and original device workload. See execution packet. **Implementation/native/boot reduction verified (committed `ce2cea24`):** 2,402 native passed/1 skipped; external payload −3,768,320B, 151→36 slabs. **Fixture repaired 2026-09-25 (host-only, committed):** the no-clear arm now selects and proves the target slot before the arm press (`_select_loop_slot_before_record_arm`), because slot selection previously ran only inside the clear step — the recorded run armed track8/slot1 against a configured slot 4. 11 focused host tests green. Intended device acceptance still open: rerun the 24-bar track8/slot4 workload and revalidate the length expectation against the device's internal tempo (the recorded run used the base preset's 2 bars and produced 17 device bars at 85.4 BPM). Late heap-low remains; see review.
      **BLOCKED 2026-09-26 — the device library is empty; the required loaded-library experiment cannot be
      run.** A deliberate clear of the authorized target (track 8 slot 4) admitted a deferred workspace save,
      and the driver then closed the serial port while that save was still writing
      (`PERS,work,...,TrackMeta,track:7,start,ok` with no completion is the last line of that session). The
      next run's upload reset the board mid-write; boot hygiene discarded the incomplete temp; the workspace
      then **failed to load** (`CurrentSet meta missing .../current/temp/runtime.bundle.bin`,
      `BOOT,load,fail`, "Boot recovery chain exhausted; starting empty") and the runtime came up empty —
      all 64 slots, `free_chunks=480/512`, where the same library read 58 committed / ~386-403 free minutes
      earlier. **An empty-library run cannot replace this task's loaded-library experiment**, so the 24-bar
      capacity revalidation is not merely delayed, it is unperformable until the library is restored or
      rebuilt. What is established is the load failure and the empty runtime; what is **not** established is
      that the slot payloads are erased, or the exact failure chain. The SD card has not been touched and no
      further boot was induced (boot hygiene is itself evidence-modifying). Incident record, preserved
      evidence and the read-only imaging procedure awaiting approval:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § "Workspace-load failure" and
      [`evidence/2026-09-26-workspace-loss/`](evidence/2026-09-26-workspace-loss/MANIFEST.md). The host defect
      is fixed (`hitl/device_shutdown.py`, canonical stop -> drain -> stop -> drain with a reset latch); the
      hold is currently **latched**, so any upload/reset refuses until deliberately cleared.
- [x] S1.6c.2a **No-clear target guard:** refuse record arm before any gesture unless latest device-time DISP after track selection matches configured slot. Missing/malformed/mismatched evidence fails closed. Five new tests; focused host total56/56. This prevents recurrence; it does not implement slot selection or close capacity.
- [x] S1.6c.2b **No-clear selection (fixture repair, host-only):** `--no-clear-before-record` skipped the clear *and* the selection, so the arm acted on the previously selected slot. `_select_loop_slot_before_record_arm` stops the transport, selects the Loops row, retires a select-started ghost capture, and the arm path waits on slot identity (`_await_observed_record_slot`) before pressing arm. Selecting under a running transport starts a ghost capture whose later stop leaves the track ARMED, so the transport stop is part of the fix, not cosmetics. `select_loop_slot=False` keeps the edit path from a second tap. 17 focused tests green; host script suite unchanged at its 8 pre-existing failures. No firmware change.
- [x] S1.6c.2c **Loop-edit tempo fader (fixture support, DEC-086):** fader 4 on the loop-edit control channel, ch16 CC102, maps 0–127 to 72–200 BPM; `LoopEditManager::handleTempoInput` applies it through `ClockManager::setBpmFloat`, and session entry sends the current tempo back so the motor parks live. The conversion is the dependency-free `TempoMapping` header. Native `test_tempo_mapping` 8/8, full native **2,421 passed / 1 skipped**; device-verified CC 32 → 104.3, CC 100 → 172.8, CC 64 → 136.5 BPM with feedback (image `870f2948f0676d60…`, `captures/tempo-fader-01/`). RAM1 unchanged, 352 B over the floor. This lets the 24-bar capacity fixture pin the device tempo instead of guessing it.

- [ ] S1.6c **Owner correction and capacity revalidation:** once S1.6b identifies the owner, pin its production-path lifetime/admission oracle, correct the demonstrated defect, and repeat the same workload plus content/recovery checks. Do not lower thresholds or clear content to obtain a pass. Resume S1.5 and S2b.3 only with their own required evidence.

- [x] S1.6d **Stopped heap diagnostics:** add explicit !HEAP_STATS under MemoryMonitor with stopped/quiescent guard, bounded internal free-list and external gap inspection, raw aggregate/largest/top/tail fields, scan completeness and elapsed time. No live scan, probing allocation, routing change or automatic leak claim. Native guard/fragmentation/bounds/clock-quiet tests 5/5; full native 2,394 passed / 1 skipped; capture-serial SUCCESS (padding 1,848). Final stopped/refused device evidence `captures/stopped-heap-03/`, image identity in `captures/stopped-heap-final-image/`. No owner fix or leak verdict claimed.

The (2,56] observations bracket whole-scenario feasibility, not overdub admission. `heap_free=0`
means unused arena tail is exhausted; the failed request is real but reusable malloc capacity
and fragmentation are not established. Legacy live/tag totals include fallback-accounting defects.
See the S1.6 execution packet in ARCHITECTURE-REVIEW.md.

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
<!-- Current S1.3 evidence, 2026-09-25: takeover host suite 23/23. Device -05 refuses
record_start/prewarm_failed (4096 B external allocation; heap_free 4 KB) after arm attempts.
This is warmed-state diagnostic evidence, not an increasing-N slope; 0 added populated slots.
No checkoff. See ARCHITECTURE-REVIEW continuation outcome. Next: native prewarm attribution. -->
<!-- S1.3 native retention result 2026-09-25: denied admission + real cancel retains
16,384 fixed event-capacity units and 8 canonical credits per touched slot; same-slot retry
reuses measured capacity. Full native 2379 passed / 1 skipped. See review packet. Not a device
slope or exact capacity limit; S3.2/S4.1 remain the planned liveness/release owners. -->
<!-- 2026-09-25 controlled fixture: ONE added content slot survives reboot, 9 -> 10 loaded,
36 payload files unchanged. Track 6 slot 3: SD hash and 62 event rows match stopped RAM
before and after reboot. free_chunks 450 -> 449, but focus/initial-playback selection changed;
not a fixed-role slope. Historical zero-added-slot / survival-blocker entries below are superseded
for this fixture. S1.3 and S1.5 remain open. See ARCHITECTURE-REVIEW closeout. -->
<!-- CURRENT 2026-09-25: fixed-role two-bar device slope measured at N=10..15.
Five added slots survive reboot; exact 62-event/1536-tick SD/RAM parity, same boot roles,
no losses in observed loaded sets; free_chunks 449..444. Cold reservation 512->480, credits=0.
All approved empty scratch slots exhausted; no resource refusal reached. Failure-point clause
remains OPEN, as does S1.5. See ARCHITECTURE-REVIEW fixed-role sweep outcome and
captures/s13-slope-evidence/slope.json. Earlier restore-blocker/zero-growth notes are historical. -->
<!-- HISTORICAL checkpoint, superseded by dense completion in the S1.3 row: extended outcome supersedes the N=10..15 stop above:
N=10..56, 46 two-bar additions survive, 45 repeated owner baselines match; free_chunks
449->403. At N=55 during the 56th take: 17 snapshot_reserve/heap_low denials, need
40960 + floor 32768, heap 48->44 KB. Take persists via legacy body and survives.
This is temporary publication backpressure, NOT hard residency/canonical failure; S1.3
stays open. Core extmem_malloc falls back internally; tracked live/high is not physical
PSRAM occupancy and wrapper refusal is not earliest physical-pool refusal. S1.5 24-bar
overdub and S2b.3 workload peak stay open. No S3 start. Final read-only boot N=56,
same roles/content fields, focus 6/3 stopped; host 40/40. See extended sweep closeout
and captures/s13-slope-evidence/extended-slope.json. -->
- [x] S1.3 Device slope to the **current failure point** (not a 60-slot baseline -- the resident event
      store holds only ~28 sixty-four-bar loops today): grow the library until residency or canonical
      capacity fails and record the slope, the failure point and the error. Add a synthetic
      metadata-only 60-slot characterisation for the law itself.
      **COMPLETE 2026-09-25 — current evidence supersedes the historical blockers below.**
      Two-bar device slope N=10..56: 46 additions with exact SD/RAM event/length survival,
      fixed boot roles and 45 repeated owner baselines; free chunks 449->403. Separate dense
      four-bar probe at N=56 (track 8 slot 4, one-clock note cadence, pitches 0–127) reaches
      `#MEM_EXT_REFUSED bytes=8192 tag=0 heap_free=4KB` then allocator FATAL during recording.
      No record stop or take survival claimed. Last sampled chunks_free=393. This records the
      current library/workload's hard allocation failure, not a universal refusal N. Recovery
      confirms original 56 loaded and unchanged other content fields/roles; failed target empty.
      Synthetic 60-descriptor native clause below was already complete. Host 42/42; firmware
      unchanged. See ARCHITECTURE-REVIEW dense-pressure completion and
      `captures/s13-dense-evidence/pressure-result.json`. S1.5/S2b.3 remain separate and open.
      **Historical investigation follows (not current task status):**
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
      pre-restore **480 of 512** (was 400). **The sweep itself is still blocked; the attribution is
      CORRECTED 2026-09-24.** A growth attempt (`STIM-…-TEMP-RWSO-GROWTH`, track 6) observed
      `could not reconcile MidiLooper/sets/index.bin`, `Track 6 loaded state: EMPTY` (and track 7),
      `DIAG,lcr,skip,restore`, and a census whose non-zero-slot set **changed between boots**
      (`{track=1,slot=2, track=4,slot=1}` → `{track=1,slot=2}`). **Those observations stand; the
      conclusion drawn from them did not.** `MidiLooper/sets/index.bin` is
      `SavedSetCatalog::SetIndex{ uint32_t nextSequence; }` — a saved-set *folder counter*, not a slot
      catalog — so a reconcile failure cannot by itself omit a slot; the live library is
      `/MidiLooper/current/loops/loop_TT_SS.bin` plus the `loopSlotPayloadOnSdInRam_` mirror
      (`src/StorageManager/LoopSlotRestoreQueue.cpp`). The boots that appeared to shrink report
      `reconcile_warnings=0` (`captures/s13-slope-02/sweep-result.json`), `DIAG,lcr,skip,restore` is a
      **content-resolution** line (`TrackDeferredMaintenance`/`LoopContentResolution`, `kind=skip`,
      `reason=restore`), and `Track 6 loaded state: EMPTY` appears on the stable `N=29` boots
      (`captures/s13-slope-final/`) too, so none of them tracks the shrink. The obstacle is therefore an
      **unattributed** restored-library instability, owned since 2026-09-24 by
      [`restored-library-stability`](../archive/2026-09-24-restored-library-stability/proposal.md) (Packet A,
      observation-only firmware plus read-only analysis). An earlier claim of mine — that the library
      "can grow" because a reset-then-dump once returned `length=18432` — is **withdrawn as
      over-generalised**; Leg 3 of the device session records the falsification.
      **Handoff 2026-09-24 — the stability obstacle is removed:** `restored-library-stability` **2.2** proves
      the observed per-slot manifest reproduces across three consecutive boots
      (`captures/s13-libobs-stability-3boot.json`; five complete-manifest observations agree). The slope
      fixture above **must vary the loaded population** (select/enable each scratch slot so boot
      loads it), not merely create an SD payload, and must explain the 28 payload-present slots that read as
      unloaded; see
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § *Handoff from `restored-library-stability`* for the
      evidence link and the 36/8/8 disposition. The *slope and failure point remain unmeasured here.*
      **READ-ONLY DEFINITION DONE 2026-09-24 (no sweep, no firmware): the "28 unloaded" premise is
      falsified.** All 36 payload slots are offered and restored to `terminal/committed`; 8 are pass-backed
      (legacy payloads → `adoptPersistedSnapshot` populates `passes`) and **28 are published-root-backed**
      (LSV1 payloads → `storePublishedSnapshot`, `passes` left empty) and are resident — boot restore costs
      **27 pool chunks (480 → 453)** and **2,155 KB / 78 slabs** of snapshot arena, flat after settle, and
      playback reads the root (`tryGatherPublishedSnapshotEventsInWindow`). The observation's
      `committed_at_checkpoint` was therefore a **pass-backed subset**, not the loaded population, and was
      not spec-conformant for root-backed slots. The four required-but-not-pass-backed slots are `(2,1)`/`(4,0)`
      (root-backed) and `(6,0)`/`(7,0)` (payload-absent enabled slots; `Track 6/7 loaded state: EMPTY`).
      **Controlled fixture defined:** fix one active role, add one payload per boot inside the approved
      scratch range, and report `N_payload` / `N_pass` / `N_root` plus reservation, payload, root and
      directory residency separately. **The slope run and the failure point remain unmeasured.** Full trace
      and definition: [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § *S1.3 — slope-fixture definition*.
      **OBSERVABILITY CORRECTION LANDED 2026-09-24 (seventeenth session) — and the corrected telemetry
      CONTRADICTS the per-slot attribution above.** The observation now reports `hasPassContent` and
      `hasRootContent` **separately** with `hasCommittedContent` as their **union** (`kp`/`kr`/`kc` on the
      slot record; `P`/`R` beside `m` on the end frame), where `hasRootContent` means a root **is installed**
      (`Loop::publishedSnapshot()`) and `kc` reports whether it is current; invalidation includes the
      **published-root publication identity**, so a root replaced between slices can no longer be missed.
      Tests: classifier pass-only/root-only/both/empty, session O09 (root content + root replacement
      invalidates) and O10 (union with both subsets beside it), three host parser cases and one comparator
      case; full native **2,378 / 1 skipped / 0 failures**, firmware build **SUCCESS and flashed**.
      **Two independent read-only revalidation boots agree exactly: payload 36, initial-playback 8,
      `committed_at_checkpoint` 9 (`pass_backed` 9, `root_backed` 7); the content set is `(0,0) (1,0) (1,1)
      (1,2) (2,0) (3,0) (4,0) (5,0) (5,1)` and every other payload slot has 0 passes, 0 length, 0 events and
      NO published root at all.** The publish path installs a root whenever it adopts
      (`applyLoadedLoopSlotBodyCommit`), so **the "8 pass-backed / 28 published-root-backed" reading is
      contradicted: 27 of the 36 token-verified payload slots hydrate nothing**, and the corrected loaded
      count is **9, not 36**. The three-boot repeatability result is repeatability of the fields it measured
      only — never of a *new* payload surviving a reboot. `#MEM_OWNERS` on the same boot reproduces the
      trace's owner totals exactly (78 slabs / 2,455 KB used / 2,155 KB snap / `chunks_free` 450), so the
      owner total holds while the per-slot attribution does not. The pinned fixture parameters (authorized
      slots, fixed roles, equal content, one payload format per series, separate cost classes,
      refusal-inside-the-authorized-population) are in
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § *Pinned fixture parameters*. **The scratch-range
      limit is not a capacity failure:** if no refusal occurs inside the authorized population, report the
      measured range and leave **S1.5 — refusal point** open.
      **SLOPE ATTEMPTED, BLOCKED AT THE FIRST STEP (0 added slots, no refusal).** Recording (6,1) saved (×4)
      and rebooted; the boot reports `Track 6 loaded state: EMPTY`, N=36 (no new payload) and an unchanged
      owner census, so the saved scratch payload did not survive the reboot. **No `#MEM_EXT_REFUSED`, no
      `refused` terminal** — a fixture-path blocker, **not** a capacity failure, and **not** an S1.5 result.
      The slope is therefore **still unmeasured**, S1.1's owner census and S2b.3's post-restore budget are
      measured on the revalidation boot (`free_chunks=450` vs required 16 → met; reservation 512→480 then
      480→480, `credits=0` on 16 rows), and the next step is to resolve the save/restore survival of a new
      scratch payload — a state-changing investigation, not another read-only trace.
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
      **DEVICE REFUSAL-POINT ATTEMPTED 2026-09-25 on the S1.3 library (N = 56) — REFUSED IN THE RECORD
      PHASE; clause stays OPEN.** Pinned image `b289af53…`, git `1ccff16a`; target user track 8 slot 4
      (verified empty, authorized this session); the RWSO-24CAP canonical shape with
      `--record-bars 24 --overdub-bars 24` and `--no-clear-before-record`. Pre-flight: complete 64/64
      observation, payload 59, initial playback 8, committed 56 (pass 56 / root 53), `chunks_free 403`,
      `live 6846 KB`, `heap_free 84 KB`, target empty. The device entered RECORDING at 89.169 s; from
      91.961 s `snapshot_reserve` denied every ~1 s (`need=40960B`, `heap_free` 68 → 0 KB over 39
      denials); at 130.538 s `MEM_EXT_REFUSED bytes=8192 tag=0 heap_free=0KB live=8527KB chunks_free=391`
      plus the fatal exhaustion line, then no response (no crash report, no reboot). `Overdubbing started`
      = **0** and `op=overdub_start` denials = **0**, so the **overdub reserve admission was never
      reached** — this is the **S1.3 record-phase hard-failure class**, not the S1.5 overdub refusal.
      Bracket **(2, 56]**: admitted at the 2-slot RWSO-24CAP library, refused here; S1.3's 2-bar slope
      first named denial at N = 55. Recovery boot: identical observation, 0 content-field differences
      over 64 slots, target still empty, `chunks_free 403`. Evidence
      `captures/s15-24bar-t8s4-01/refusal-analysis.json`, `captures/s15-preflight-01/`,
      `captures/s15-recovery-02/`; [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S1.5 — device
      refusal-point result. No firmware change; no `DEC-###` owed.
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
      admitted with 0 denials). **Not satisfied: S1.3's slope *to the failure point*** — still blocked,
      **and the recorded attribution is corrected 2026-09-24**: the obstacle is an **unattributed
      restored-library instability**, not a demonstrated SD-catalog-reconcile defect. `sets/index.bin` is a
      saved-set folder counter (`SavedSetCatalog::SetIndex{ uint32_t nextSequence; }`), the boots that
      appeared to shrink report `reconcile_warnings=0`, `DIAG,lcr,skip,restore` is a content-resolution
      line, and `Track 6 loaded state: EMPTY` appears on stable boots too; the four corrected measurement
      definitions and their evidence paths are recorded in
      [`restored-library-stability/ARCHITECTURE-REVIEW.md`](../archive/2026-09-24-restored-library-stability/ARCHITECTURE-REVIEW.md)
      § Task 1.1. Also open: a **content-verified PASS**, which the pipeline run does not meet (2
      unexplained missing rows in bar 0, Leg 4). S1.5's refusal-N and S1.4's pass→chunk conversion also remain.
      **The named blocker was the change now archived as**
      [`2026-09-24-deferred-slot-restore-liveness`](../archive/2026-09-24-deferred-slot-restore-liveness/proposal.md),
      which unblocked preparation under transport and the CLEAR completion branch but **did not** remove the
      restored-library confound (its drain-advancement clause became S2b.7 here); this gate closes after
      the sweep is re-run on a **stable, manifest-verified** library and this list is re-measured — a
      confounded gate is retried, never reinterpreted.
      **Consolidated update 2026-09-25.** S1.3 is **checked**: the controlled two-bar slope N = 10..56
      (46 verified additions) plus the separate dense four-bar probe that reached the hard allocator
      refusal. **Still open:** S1.5's **overdub** refusal-N — the 2026-09-25 device attempt on the N = 56
      library was refused in the **record** phase (`Overdubbing started` = 0), so the overdub reserve
      admission was never reached; the bracket is **(2, 56]** (see the S1.5 row) — and S1.4's
      pass→chunk conversion. The "stable, manifest-verified library" condition this paragraph waits on
      is **satisfied** (three-boot manifest PASS 2026-09-24), so the sweep was re-run and S1.3 closed;
      S1.5's clause needs a lower-N target or an explicit boundary decision, not another library-stability
      run.

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
      post-restore value is not measured. Also observed: the restored slot reports `Track 6 loaded state: EMPTY`
      and `index.bin` does not reconcile. **Attribution CORRECTED 2026-09-24:** `sets/index.bin` is
      `SavedSetCatalog::SetIndex{ uint32_t nextSequence; }`, a saved-set **folder counter**, so the reconcile
      line cannot by itself explain a missing slot, and `Track 6 loaded state: EMPTY` also appears on the
      **stable** boots (`captures/s13-slope-final/`). Recording into the scratch slot therefore does not
      demonstrably grow a stable library, but the cause is unattributed — owned by
      [`restored-library-stability`](../archive/2026-09-24-restored-library-stability/proposal.md) Packet A (observation-only
      firmware plus read-only analysis). See [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S2b.3 and
      [`restored-library-stability/ARCHITECTURE-REVIEW.md`](../archive/2026-09-24-restored-library-stability/ARCHITECTURE-REVIEW.md)
      § Task 1.1.
      **Handoff 2026-09-24:** the "baseline's post-restore value is not measured" gap is unchanged, and the
      stable library from `restored-library-stability` **2.2** is the fixture that can hold the measurement
      (`captures/s13-libobs-stability-3boot.json`; disposition and evidence link in
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § *Handoff from `restored-library-stability`*).
      Nothing is measured here.
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
      proposed as `deferred-slot-restore-liveness` (since archived 2026-09-24); this task is done, the device leg is not.
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
      track 6) was stopped by the **unattributed restored-library instability** (attribution corrected
      2026-09-24; the earlier "SD catalog reconcile defect" reading is withdrawn): the run observed
      `could not reconcile MidiLooper/sets/index.bin`, `Track 6 loaded state: EMPTY` (and track 7),
      `DIAG,lcr,skip,restore`, and a census whose non-zero-slot set **changed between boots**
      (`{track=1,slot=2, track=4,slot=1}` → `{track=1,slot=2}`). `sets/index.bin` is a saved-set folder
      counter, so the reconcile line is not a demonstrated cause; the instability is owned by
      [`restored-library-stability`](../archive/2026-09-24-restored-library-stability/proposal.md) Packet A. **An earlier claim of
      mine — that the library "can grow" because Leg 3 dumped `length=18432` after a reset — is withdrawn as
      over-generalised:** a slot can come back byte-exact right after its own save+reset, but successive boots
      do not accumulate a stable library.
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
      restore-liveness defect (`Track 6 loaded state: EMPTY`, a census that changes between boots) whose change
      was archived as
      [`2026-09-24-deferred-slot-restore-liveness`](../archive/2026-09-24-deferred-slot-restore-liveness/proposal.md).
      Per the repo rule
      that a missing or confounded gate is not a pass and a threshold is never weakened, this gate closes only
      after the sweep is re-run on a stable library.
      **Handoff 2026-09-24:** that stable library now exists — `restored-library-stability` **2.2** proves the
      observed per-slot manifest reproduces across three consecutive boots
      (`captures/s13-libobs-stability-3boot.json`; five complete-manifest observations agree). The device
      half's blocker is therefore now only the slope fixture (S1.3), not library stability. Evidence link and
      the 36/8/8 disposition:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § *Handoff from `restored-library-stability`*. **No
      gate is checked here.**

- [x] S2b.7 **Drain-driven restore advancement — DELIVERED 2026-09-24 (native).** The blocking drain's
      *decisions* were already verified natively (A3.2 wait-reason classification, A3.3 budget/progress
      accounting, A3.4 caller contract in the archived `2026-09-24-deferred-slot-restore-liveness`), and its
      **completes** branch is
      device-verified with an owed save, an in-flight commit-gated restore and another track playing
      (that change's A3.7 shape (b), `captures/a37fix-04/`). **Deferred here:** that the drain **advances** a
      restore it waits on — i.e. that a restore holding the SD session is progressed by the drain rather than
      only reported. The observed completion came from
      `shouldPrioritizeWorkspaceSaveDrain()` bypassing the slot-load gate for an urgent owed save, so the
      advancement clause needed its own fixture and must not be read into that run.
      **Bound by** `resumable-midi-boundary-playback-maintenance` **task 3.3** (bounded commit/cleanup); do not
      relax the commit gate here.
      **Verify:** a fixture where a save is owed, another track plays, and a restore **holds the SD session**
      at the drain edge, showing either that the drain advances it or that it names the wait — with no claim
      of advancement absent that evidence.
      **IMPLEMENTED — the drain now SERVICES the restore it waits on.** `StorageManager::serviceDeferredRestoreForDrain()`
      (`src/StorageManager/LoopSlotRestoreQueue.cpp`, one `StorageManager`-owned entry, no new owner) runs the
      **same** selection/admission + bounded step the deferred-load frame uses
      (`DeferredJobScheduler::runFrame` → `selectSubmittedLoadJobs` + `stepSubmittedLoadJobs`), and the drain
      loop calls it once per iteration whenever `deferredSaveBlockedByActiveSlotLoadSd()` is true. No second
      loader, queue or admission policy: commit/cleanup still need the global idle gate and record
      `waiting_commit_gate`. A **termination clause** (D4 point 6) ends the episode as soon as the restore
      reaches a wait the drain cannot clear, so it reports that named wait instead of spending its budget
      re-servicing a gated request. Restore grains already count as drain progress (`restoreProgressCounter`,
      A3.3), so the new work cannot be misreported as stuck.
      **D4 point 1 (active **and** parked instances) needed no new promotion.** The existing selection step
      already promotes a parked instance (`ensureActiveLoadLoopJobSelected` → `LoadLoopSelectionPolicy::PromoteParkedLow`).
      An explicit `promoteParkedLoadLoopJobForDrainService()` was written, **proven redundant by removing it**
      (the parked case stays green without it), and deleted — recorded here rather than left as surface.
      **Fixtures (default gate, `test_storage_drain_restore`, real drain):**
      `test_s2b7_the_drain_advances_a_session_holding_restore_and_names_the_commit_gate` (another track
      audibly playing: progress **1 → 8**, `TemporarilyBlocked` with `waiting_commit_gate`, the loop does
      **not** publish, the request stays `waiting` with no terminal, the save stays owed);
      `test_s2b7_the_drain_advances_the_restore_to_completion_where_the_gate_admits` (focused slot, nothing
      timing-critical, **no external advancement** between assertions: the drain commits the restore and
      discharges the owed save);
      `test_s2b7_the_drain_services_a_parked_instance_holding_the_session` (session held by the parked
      instance alone: serviced, progress advances, still publishes nothing while the gate denies);
      and the control `test_s2b7_control_the_drain_completes_once_the_restore_releases_the_session`.
      **Discriminating by removal:** disabling the drain's service step reverts to the measured pre-slice
      behavior and fails exactly the three advancement assertions (`Expected 1 Was 1`, `progress=2->2`,
      `Expected 3 Was 3`); the mutation was reverted, `src/` byte-identical.
      **Recorded nuance, not a defect:** with the transport idle and the restore on a **non-focus** slot,
      focus policy parks it before it can be stepped, and the drain's urgent-save priority discharges the save
      without advancing the restore — so the gate-admits fixture uses the focused slot, and the parked/shape
      cases assert the commit gate rather than the save's discharge.
      **Evidence:** `test_storage_drain_restore` **10/10**; `test_drain_reentry_callback_safety` 8/8; full
      native **2,352 / 1 skipped / 0 failures** (2,350 before these two new cases;
      `test_drain_reentry_callback_safety` had to link `DeferredJobScheduler.cpp` because the real drain now
      reaches it — a build fix, recorded); `teensy41-capture-serial` **SUCCESS** `variables 96,192 /
      code 391,080 + padding 2,136` (1,296 B above the 840 floor). **Device: NOT RUN** — every claim here is
      native; a hardware re-run of the archived A3.7 shape (b) would need the clear-then-reload route the
      owner deliberately kept disabled, so no device class is claimed. Packet and full record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S2b.7.

- [ ] **S2b.7 (TRANSFERRED 2026-09-24 from `restored-library-stability` task 2.3) — drain fixture: hold the
      condition on device.** Show the fixture yields a save owed + a restore holding the SD session + another
      track playing, with `deferredSaveBlockedByActiveSlotLoadSd()` true at the drain edge. **Verify:** the
      condition is observed, not assumed; the exact observables are recorded; no external advancement is
      performed inside the run. *Unchecked on transfer; the library change no longer carries it.*
- [ ] **S2b.7 (TRANSFERRED 2026-09-24 from `restored-library-stability` task 3.1) — drain device leg: report
      which permitted outcome occurs.** Use the authoritative HITL entry point with a registered or reused
      stimulus (list before adding; never edit a canonical stimulus). Report **which** outcome occurred,
      with evidence, rather than asserting one: (i) the drain returns `TemporarilyBlocked` with the named
      **`waiting_commit_gate`**; **or** (ii) the owed save is discharged by the documented **urgent-save
      priority** while the restore remains commit-gated. Both require **no unintended publication** (no
      committed pass appears while the gate denies) and the request preserved. **Progress is defined inside
      the drain call:** sample `restoreProgressCounter()` immediately before the drain entry and immediately
      after it returns; an urgent-save discharge with **no movement of the counter within the call** SHALL NOT
      check off S2b.7's advancement clause and SHALL record `advancement NOT observed inside the drain`.
      *Unchecked on transfer; the library change no longer carries it.*
- [ ] **S2b.7 (TRANSFERRED 2026-09-24 from `restored-library-stability` task 3.2) — the device half is
      independently reportable.** S2b.7's device confirmation SHALL be checkable from its own evidence alone
      and SHALL NOT depend on `restored-library-stability`'s closeout (or any other change's closeout).
      *Unchecked on transfer; the library change no longer carries it.*

- [x] S2b.8 **Set-change epoch invalidation for the restore request record — external resets.**
      Transferred from the archived `2026-09-24-deferred-slot-restore-liveness` A2.1, whose
      delivered scope covers the
      request identity, the named waits, exactly-one-terminal, the external **CLEAR** generation advance, and
      pre-publication identity validation. **Not delivered there:** a production **set-change** wire (the
      record's `noteRestoreSetEpoch` had no production call site) and any undo/redo-clear external reset.
      **Verify:** a set-change entry point feeds the epoch (or an equivalent external reset) and native cases
      assert that outstanding requests reach the `superseded` terminal under the identity they were created
      with; the undo/redo-clear routes decide explicitly whether they are external resets.
      **DONE 2026-09-24 — native only; device gate NOT APPLICABLE (no hardware-observable behavior).**
      *Decision, explicit as the task requires:* **undo-clear and redo-clear ARE external resets.** They
      replace the slot's content by an external owner (the user), which is the class the
      `lazy-slot-hydration` spec lists for both `superseded` ("the slot was cleared by an external owner")
      and the `requestGeneration` advance ("a user clear"). Both routes now call
      `advanceRestoreRequestGeneration` when the undo/redo actually applies, so an in-flight job cannot
      publish a parsed payload over the content just restored or re-cleared. This is an application of
      **DEC-082** clause 1, so no new `DEC-###` is introduced.
      *Two defects fixed in `noteRestoreSetEpoch` itself*, both found by the audit: it recorded the
      `superseded` terminal with the **new** epoch stamped into the record (so the outcome described an
      identity the request never had, and the record read as *current*-but-terminal), and it emitted **no**
      `RRQ` transition, so the reason was not observable. It now marks **before** advancing the epoch --
      the order `advanceRestoreRequestGeneration` uses -- keeps the records readable under their own
      identity, and emits one transition per decided request. The dead, non-emitting
      `invalidateAllRestoreRequestsAsSuperseded` was removed (grep: declaration + definition only).
      *Set change:* **no runtime set-change entry point exists**, so none was invented. The loaded current
      set is established at boot (`loadCurrentSetBundleAndActiveLoopSlots`), which calls
      `resetAllLoopSlotRestoreAttempted()` → `resetAllRestoreRequestState()`: a **full** record reset with
      `setEpoch = 0`, stronger than `superseded`, and it precedes any request because none is queued yet in
      that session. `loadCurrentWorkspaceFromSd` (same directory load) has zero firmware callers.
      `noteRestoreSetEpoch` therefore stays the record-level API for the boundary and is now corrected and
      native-tested; the *equivalent external reset* the task allows is the undo/redo-clear class.
      *Audit finding, recorded not re-wired:* "a different owner committing that slot" is already
      terminated -- `markLoopSlotRestoreAttempted` records `unnecessary` and `commitLoadLoopJobPublish`
      revalidates the captured identity before the token check and publishes nothing when stale. Converting
      it to a generation advance would change the B04/S2b.5 `unnecessary` oracle for no safety gain, so it is
      recorded as a spec-wording gap, not a defect.
      **Evidence.** New read-only accessor `restoreRequestEpochFor(track, slot)` makes "under the identity it
      was created with" checkable rather than assumed. `test_storage_manager_load` **35/35** (strengthened
      set-change case + `test_s2b8_set_change_decides_a_request_exactly_once` +
      `test_s2b8_set_change_is_rejected_immediately_before_publication`);
      `test_drain_reentry_callback_safety` **8/8** (`test_s2b8_undo_clear_supersedes_the_restore_request`,
      `test_s2b8_redo_clear_supersedes_the_restore_request`, through the real button routes); full native
      **2,348 / 1 skipped / 2,347 succeeded / 0 failures**; `teensy41-capture-serial` **SUCCESS**
      `variables 96,192 / code 391,080 + padding 2,136` (1,296 B above the 840 floor; `code + padding`
      constant at 393,216, so no bank cross). **Discriminating by removal, both directions observed:**
      restoring the epoch-first/re-stamp behavior fails exactly the original-identity assertions (3 failed,
      32 succeeded), and removing the two handler calls fails exactly the two new reentry cases on the
      generation assertion (2 failed, 6 succeeded); both mutations reverted byte-identical. Packet:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S2b.8.

## S3 — introduce the reusable lease pool (no release yet)

> **Handoff 2026-09-24 (recorded, not started):** the restored library is stable enough to hold a
> lease-acceptance run — `restored-library-stability` **2.2** proves the observed per-slot manifest
> reproduces across three consecutive boots (`captures/s13-libobs-stability-3boot.json`). S3 is **not
> started here** and the stage order is not bypassed; evidence link and the 36/8/8 disposition:
> [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § *Handoff from `restored-library-stability`*.
>
> **Started 2026-09-25.** G2 execution packet and completion record:
> [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S3.1.

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
      `LoopPlaybackRuntime` (6,352 B extmem; owner receipt remeasured 2026-09-28) or the `LoopPool` `Loop`; **per-track state — including the latch —
      externally held (`std::unique_ptr`) or with out-of-line `TRACK_COLD_MEM` ctors/dtors**, because `.bss`
      routes to DTCM and a by-value scalar latch would land there. Re-measure the padding baseline at S3.0 (the
      cursor rule still cites 1,256; the recorded line is 872). Evidence: the RAM1 line stays `padding >= 840` (re-based from 872 on 2026-09-23 by the reviewed
      32-byte allowance; enforced post-link),
      12 banks, `locals free` unchanged (a bank cross costs 32,768 B and the documented silent boot hang).
- [x] S3.1 `WorkingSetLease` with owner = slot + generation + role, and a pool provisioned for the
      measured active envelope. Route today's prewarm through it so behaviour is unchanged but ownership
      is expressible.
      **PARTIAL 2026-09-25 — vocabulary landed and routed; release is S4.1's.** `WorkingSetLeaseSlot`
      (per-slot, on the externally allocated `LoopPlaybackRuntime`) and `WorkingSetLeaseTable<MaxSlots>`
      (multi-slot census) carry one identity vocabulary over a declared cap: O(1), allocation-free, and
      reporting `CapReached` / `InvalidRole` / `NotHeld` / `StaleGeneration` instead of growing or
      asserting. Both cold admission points record `PlaybackWindow | LiveCapture` — the record arm's
      `prewarmPlaybackForSlot` and `admitOverdubStartWorkingSet` — and the boot pass moved into
      `Track::prewarmBootPlaybackRuntimeSlots()` so the slot's owner holds the admission predicate. No
      resource call moved and capture admission is not newly gated, so behavior is unchanged
      (`test_playback_prewarm` and full native 2,434 passed / 1 skipped). RAM1: `variables` flat,
      `code 391,960 → 391,912`, `padding 1,256 → 1,304`; `sizeof(Track) 384` and
      `sizeof(LoopPlaybackRuntime) 6,344` unchanged (the per-track `TrackPlaybackRuntime` placement was
      rejected by its pin — 16 B there costs 128 B of DTCM). **Still open:** nothing releases yet, so the
      lease is write-only and the generation never advances. S4.1's release calls
      `releaseAllWorkingSetLeases()`; S4.2 reads `workingSetGeneration()` for `captureStopOwner`. S3.2's
      liveness predicates are the other half of this task.
      **CHECKED 2026-09-26 (checkbox reconciliation).** The remainder named above is discharged or assigned
      elsewhere, which is the rule this file already applies (§ Checkbox reconciliation): **S3.2**, named here
      as "the other half of this task", is **done** (its own entry, `CHECKED` 2026-09-25), and the **release**
      is built and *called* — `TrackPlaybackRuntime::releaseSlot(slot, generation, preserveLedger)` checks
      `rt->workingSetGeneration() != generation` (S4.2's discrimination) and then calls
      `rt->releaseAllWorkingSetLeases()`, which is what advances the generation; `slotGeneration(slot)` exposes
      the identity a caller must present. So the lease is no longer write-only, which was this task's stated
      blocker. Verified in the tree, not inferred. The task's own deliverable — the identity vocabulary, the
      declared-cap pool that reports instead of allocating, and routing today's prewarm through it with
      unchanged behaviour — landed with 11 lease fixtures and `test_playback_prewarm` green. Nothing was checked
      because a nearby task finished; the blocker this entry names is gone.
- [x] S3.2 Explicit liveness predicates per owner kind: capture stop job, deferred stop commit, undo/redo
      root, prepared playback handover. Native fixture per kind.
      **DONE 2026-09-25 (naming + discriminating matrix).** `Track::noLiveOwnerForSlot` is now a list of
      named terms — `hasLiveCaptureStopJobForSlot` (ring live window only, B6), `hasAcceptedInteractionForSlot`
      (S4f.1 latch), `ownsLiveFinalization` (`captureStopOwner`) and `ownsInFlightPreparedHandover`
      (`preparedPairRequestPending`) — with identical meaning, order and callers, so nothing widened or
      narrowed. The two terms that **cannot be expressed yet** are recorded in place with their owning
      stages instead of being dropped: the job-owned undo root arrives with **S5.2a** and pending deferred
      reconstruction with **S5.2e**. The published prepared pair stays deliberately **not** a term (it is the
      slot's playback authority while it sounds, so counting it would make release unreachable).
      Fixture `test_s3_2_liveness_predicates_are_discriminating_per_owner_kind` in `test_continuous_capture`
      holds each kind alone and asserts the composite refuses only for it, plus two inverse controls (an
      unowned slot allows release; a slot holding only a published pair and an admitted working set allows
      release). Suite 101/101; full native `2,436 passed / 1 skipped` (clean build, 97 suites); RAM1 flat; capture-serial builds.
      Packet and record: [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S3.2.
- [x] S3.3 Selection acquires nothing: assert the SELECTED tier and browsing cause no lease churn.
      **DONE 2026-09-25 (fixture only; no production line changed).**
      `test_s3_3_selection_and_browsing_acquire_no_working_set` browses three committed slots three
      times through `SlotStateMachine::setSelectedSlotIndex` — the mechanism the UI gesture ends in —
      and asserts every slot still holds `WorkingSetRole::None`, generation 0, and **no slot runtime
      at all** (selection cannot depend on working-set availability). The positive control is the
      record arm, which sets `PlaybackWindow | LiveCapture`, plus a repeated admission that must leave
      the held set unchanged, so a witness that can never move cannot pass. The observation needs **no
      new state**: the first cut added acquire/release churn counters and the
      `sizeof(LoopPlaybackRuntime) == 6,344` pin rejected it, so the counters were removed rather than
      re-pinning the per-slot runtime for test observation. Suite 101/101; full native
      `2,436 passed / 1 skipped` (clean build); firmware RAM1 byte-identical to S3.1. Packet and record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S3.3.
- [ ] S3 gates: S2b gates plus S2.2/S2.3 current-root budget (no phase-F behavior prerequisite), plus `test_runtime_working_set_ownership`; `test_playback_prewarm` unchanged.
      **NATIVE HALF DONE 2026-09-25; the gate stays OPEN on its device items.**
      `test_runtime_working_set_ownership` now exists and is linked as a firmware-boundary suite: boot/load
      is an admission boundary (a committed slot leaves boot with no working set; the record arm on the same
      slot establishes `PlaybackWindow | LiveCapture`), a repeated admission does not churn the held set or
      the generation, release is refused by each owner kind and allowed once it retires (latch, in-flight
      handover, and a stop admitted through the production entry on an interval opened by
      `startOverdubbing`), and selection acquires nothing. 4 cases green; full native `2,440 passed /
      1 skipped` (98 suites); `test_playback_prewarm` unchanged; firmware RAM1 byte-identical (test-only).
      **Still open, and not native work:** the S2b device gates (S2b.3 post-restore half, S2b.5, the
      transferred S2b.7 legs) and the S2.2/S2.3 current-root per-class budget. Record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S3 gate.

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
- [x] S4.1 Release when the slot leaves the active set and the guard reports no live owner; release is
      total. **CHECKED 2026-09-26.** The release behavior is implemented and verified at the level the task
      names: the `(slot, generation)` entry, the per-slot liveness guard, and — closed in two slices this
      day — the persistence-identity read during a handoff and the **working-set lease release on a
      same-track slot switch**. The lease leg now has three fixtures in `test_runtime_working_set_ownership`
      (23/23) driving the production switch commit with a live owner latched on the *incoming* slot (retired
      slot released + generation advanced + identity consumed, owned sibling untouched; control: retire and
      switch back), the refusal under a live finalization (`noLiveOwnerForSlot` false, `releaseSlot` →
      `RefusedBusy`, lease and generation unchanged, switch still commits), and the release **after**
      retirement through the next same-track switch. **Red-proved** by removing the guards in stages: the
      predicate alone (case A fails; B/C survive on `reset()`'s own refusal), predicate + refusal (case B
      fails, *"Expected 6 Was 1 … RefusedBusy"*, plus four pre-existing fixtures), and predicate + refusal +
      the track-level early return (case C fails) — so the live-finalization protection is
      **triple-layered** and each layer is independently observable. No production code changed; full native
      **2,468 passed / 7 skipped**; capture-serial SUCCESS, `padding 856` (floor 840). **Remaining, and it is
      an evidence obligation rather than a behavior gap:** the device row for the lease release has not been
      *observed* on a run — the emission `DIAG,ws_release,<slot>,<generation>,<outcome>` already exists and
      is **Tier-A** (added in `bbedfeac`), so what is owed is a run in which the path fires for the slot
      under test plus an oracle that reads the row. Record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § "S4.1 — working-set lease release on a same-track
      slot switch". **CAMPAIGN RUN 2026-09-27 — the row is still owed, and the shape that would exercise it
      is now known rather than hoped for.** A full authorized A/B/C run (`git_commit 5849d620ebc5`) completed
      the whole chain on track 8 slots 4/5/8 and produced **zero** `DIAG,ws_release` rows. That is measured
      behaviour consistent with the G0 trace, not a defect: the same-track handoff **excludes its own track**
      from the take-start sweep, and the cross-track sweep only reaches another track's slot, so a start on
      another track **while a lease is still held** is what the row needs. The three candidate routes are
      therefore (a) a cross-track take start after a predecessor's lease exists, (b) the memory-pressure lane,
      and (c) a same-track switch **after** retirement — the run above exercised none of them. Also recorded:
      the pre-identity firmware gives no lease telemetry at all on this path, and the campaign's L2 blocker
      (`Track::stopOverdubbing`'s `pending` branch logging no slot/interval) is independent of this row.
      **ROUTE (a) RETIRED 2026-09-27 — it cannot fire the row at all.** The re-run completed the full chain
      again with **zero** `DIAG,ws_release` rows, and the source explains it: `Track::prewarmPlaybackForSlot`
      acquires a lease **only** when `prepareCapture` is true, and the only callers that pass `true` are the
      record arm and the boot-cold overdub start — boot's `prewarmBootPlaybackRuntimeSlots` passes **false**
      ("Boot reserves staging, not eight full view/bridge/companion bundles before SD load"). Since the
      cross-track sweep (`releaseBackgroundPlaybackMergedMidiEventsMemory`) **excludes the capturing track**, a
      lease can only ever exist on the track that armed the record, so a take start can never sweep a slot that
      holds one. **What remains is route (c), the one the native fixtures already pin:** a same-track switch
      **after** retirement, where `resetPlaybackStateForSlot` → `releaseEligiblePlaybackRuntimeSlots` runs over
      the retired slot. The device leg is therefore owed as a **reachable protocol** (arm and stop a take, let
      the finalizer retire, then switch away on that same track) rather than a hoped-for one; the arm-then-start
      sequence in this run does not produce it, because the successor's `startRecordingTrack` sweeps *other*
      tracks while A's retired lease lives on the **same** track. Record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § "S4.1's lease-release device row: the cross-track
      route is a structural dead end".
      **4→5→8 SWITCH SEQUENCE CONFIRMED ON DEVICE 2026-09-27, and the slot accounting is now pinned.**
      The existing retention driver *is* the correct 4→5→8 machine: its `b_switch` presses slot 5's loop
      button **while A is overdubbing**, which is the capture-switch that admits A's stop as a deferred job,
      and its `c_switch_request` presses slot 8 while B still captures, so that request is **retained** rather
      than committed. One run (`captures/s41-switch458-20260927_014248`, `--no-upload --no-clear`) shows the
      order verbatim: `Loop 5: Switch gesture from slot 4` then `Loop 8: Switch gesture from slot 5` followed
      by `Track 7 loop 8: switch and record start retained (stop not yet admitted)`, with the successor
      starting at tick 2304 and the shutdown `complete`.
      **Base convention, pinned because it cost two wrong readings:** the human `Loop N:` / `Switch gesture
      from slot N` rows are **1-based** (`Loop 4` = the fourth slot = the driver's `--slot-a 4`), while
      `RECA`/`RECS` and `DIAG,ws_release` carry **0-based** slot fields (`RECA,3` = the same slot as
      `Loop 4`). So the seed rows `RECA,4` then `RECA,3` are the driver seeding B (slot 5) first and A
      (slot 4) second, exactly as its own comment says.
      **FIVE RUNS, NO ROW — the release is not reachable by any same-track sequence tried, and the reason is
      now definite rather than suspected.** Across `captures/s41-lease-*`, `s41-switch458-*`, `s41-commit-*`
      and a final **clean-upload, fresh-boot** run (`captures/s41-commit-upl-20260927_014715`, boot banner
      `BOOT,usb_host,deferred`, shutdown `complete`), the take records and commits (`RECA,3,0` then
      `RECS,stopToStopped,3,2089,0,2089,2304,0`), the switch commits, and **zero `ws_release` rows appear**.
      Every run also reports `slot_a_finalizer_retired: False` — there is no `reentry_window_open` and no
      `canonical_release` for the predecessor at all. Two protocol shapes were tried (transport-stop then
      switch; capture-switch while recording) and both stop shapes produce the same signature, so the
      missing link is the same in each: **the predecessor's stop completes without leaving a deferred
      finalization**, and the release sweep is therefore never called on a retired slot in this shape.
      **What a future attempt needs, stated precisely:** a sequence in which the predecessor's stop is
      *accepted and latched* (the DEC-078 shape the retention campaign produced when a live canonical owner
      forced the stop to be admitted as a job) **and** a same-track switch that commits *after* that job
      retires. Neither the simple record/stop sequence nor the 4→5→8 acceptance has both halves, which is why
      five runs found nothing. This is recorded as a **reachability result for the shapes tried**, not as
      evidence that the release is broken — the release remains exercised natively (S4.3's reuse fixture and
      the three S4.1 fixtures), and the device row stays owed.
      **Also recorded:** a stale shutdown latch blocked the first upload attempt. It had been set by this
      probe's own first cut (a `TypeError` in `_ensure_transport`, so the transport stop never ran), survived
      three verified-clean shutdowns, and was cleared only after an independent read-only check
      (`deferred_save_idle_in_suffix` true, library observation complete at 59 committed). The latch behaved
      correctly; its message pointing at the 2026-09-26 workspace-loss procedure is the designed response.
      **Consequence for the release row, stated so the next attempt does not chase a phantom:** no
      `ws_release` appears in a 4→5→8 retention sequence, and that is expected rather than a defect. The
      release runs from a playback reset on the track, and the final switch to slot 8 is *retained* — it never
      commits inside the window, so no reset runs after A retires. Exercising the row needs a switch that
      **commits** after retirement, which is a different sequence from the retention acceptance. **PARTIAL 2026-09-25 — the per-interval stop scoping is fixed and red-proven; the
      memory-pressure and `(slot, generation)` conversions remain.** `Track::clearStoppingIntervalLedger()`
      now clears only the stopping interval's slot ledger, and both converted stop routes call it
      (`handleNoteEditFold`'s `endInPlaying` branch, `stopOverdubbing`'s fallback);
      `Track::sendAllNotesOff` keeps the deliberate track-global sweep (now looping the per-slot
      `LoopPlaybackRuntime::clearLedger()`). Fixture `test_a_stop_clears_only_its_own_slots_ledger` is
      **red on the pre-fix behavior** (the first version drove the ordinary stop route, which takes the
      deferred admission path and never reached a converted site — a false green, recorded). Full native
      `2,441 passed / 1 skipped`; capture-serial `variables` unchanged (`code 391,912 → 391,960`).
      **SECOND SLICE 2026-09-25 — the `(slot, generation)` release entry exists and the two
      memory-pressure sites are converted.** `TrackPlaybackRuntime::releaseSlot(slot, generation,
      preserveLedger)` reports `NotHeld` / `StaleGeneration` / `RefusedBusy` / `Released`, with
      `slotGeneration(slot)` as the identity a caller presents; `Track::releaseEligiblePlaybackRuntimeSlots`
      applies it per slot behind `noLiveOwnerForSlot`; `releasePlaybackMergedMidiEventsMemory` and
      `invalidatePlaybackMergedMidiEvents` call it. The predicate-free `TrackPlaybackRuntime::resetAll`
      was **deleted** (no remaining callers). No new state: the identity is the S3.1 working-set lease
      generation, so `sizeof(TrackPlaybackRuntime)` stays 32 and RAM1 is byte-identical. Both fixtures are
      **red-proven** (identity check removed → stale release accepted; liveness gate removed → the sweep
      releases a live-owned slot). Full native `2,444 passed / 1 skipped`.
      **Still open:** `DeferredSaveJobStages`' footer read (`getActiveLoopIndex()`), and the
      activation/deactivation sweep callers S4.3/S4.4 describe — the entry is callable and called, but
      nothing yet calls it on a role transition. Packet and record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S4.1. **The existing release APIs must be converted, not supplemented (rerun blocker):**
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
      **RECONCILED 2026-09-26 — the "must be converted" text above is superseded by the tree, and the
      A/B/C device run is this task's release evidence.** Verified: `clearStoppingIntervalLedger()` clears
      **its own slot** through `LoopPlaybackRuntime::clearLedger()` (the comment names the old global sweep
      as the S4.1 rerun blocker), `TrackPlaybackRuntime::resetAll` is **deleted** and replaced by
      `releaseEligiblePlaybackRuntimeSlots()` -> `releaseSlot(slot, generation)` behind
      `noLiveOwnerForSlot`, the one remaining `clearAllLedgers()` caller is the deliberate whole-track
      `sendAllNotesOff` sweep, and the slot-threaded persistence reads are converted
      (`TrackStopTelemetryColdHelpers` documents the interval-slot rule; `TrackCaptureStopCommit` uses
      `persistSlotIndex = slot`). **Device evidence:** the accepted-switch retention run
      (`captures/s4f3-retention-20260926_181732`) exercises the release path end-to-end — the release
      marker fires only with `captureStopOwner == nullptr` (the `ownsLiveFinalization` term), names the
      retiring owner's slot **and generation** (`canonical_release,4,40,6144,8`), and C's start follows it.
      **Correction (same day):** an earlier draft of this note said C's start drives the guarded
      working-set release. It does not, for this run: `releaseBackgroundPlaybackMergedMidiEventsMemory`
      sweeps `if (i != captureTrackIndex)`, so a same-track sequence on track 8 excludes track 8 and A's own
      working set is not released by it. The lease release for a same-track slot is therefore **not**
      evidenced by this run — only the occupancy release (L4) is. **Fixed here:** the mid-pass journal resolver still read `getActiveLoopIndex()` for a stop in
      flight, journalling an interval pinned to slot 5 under whichever slot was active; the rule now lives
      on the owner as `Track::captureWorkSlot()` (with `Track::pinnedStopSlot()`), returns false rather than
      guessing, and the selected-track fallback is removed — fixture
      `test_s4_1_capture_work_slot_uses_the_pinned_interval_slot`, red-proved (*Expected 1 Was 3*). Full
      native 2,459 passed / 1 skipped; `padding 840` (floor). **Still open:** the working-set lease release
      is *inferred* from the sweep mechanism rather than asserted by name (the oracle asserts L4's
      finalization-capacity release); and the activation/deactivation sweep caller on a same-track slot switch.
      **CLOSED AS VERIFIED 2026-09-26 — `DeferredSaveJobStages`' footer read (persistence identity during
      handoff).** The read is **not** a defect: the footer records the workspace focus at **footer-write
      time**, which is the contract, and the interval's slot stays the **content** identity (its body is a
      `LoopPersist` item keyed by `(track, slot)`, cleared only by its own token-verified write). Four cases
      in `test_storage_drain_restore` drive the real worker paused with the footer due and unwritten, an A→B
      handoff through `TrackManager::setSelectedSlotIndex`, then a reload: content, loop identities and the
      **restored focus** are asserted per slot, with a no-switch control (restores A), a handoff *after* the
      footer was written (the selection route's requeue lands it), and an SD write fault at the footer
      (nothing published; the retry lands the handoff). **Red-proved** by pinning the footer's two reads to
      the interval's slot — the three handoff cases fail on the restored focus while the control passes; the
      first red-proof attempt was a **false green** (pinning only `Slot`-keyed writes left the handoff's own
      requeued `WorkspaceFooter` write reading live state, which repaired it) and is recorded as the
      robustness property it is. No production code changed: `src/` is untouched. Recorded as **DEC-090**;
      packet and record: [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § "S4.1 — persistence identity
      during handoff".
- [x] S4.1a Deferred-save completion failure policy: a persistently failing workspace completion must not
      hold the persistence queue. **DONE 2026-09-26 (raised by this task's fixture work; decided as
      DEC-091).** The completion failure branch used to requeue the item **at the queue head**, so a
      deterministic failing step retried forever: every other item waited behind it and
      `hasDeferredSaveWork()` stayed true — refusing the save-new-set pre-flight and making the read-only
      slot-payload observation answer `busy` for the rest of the boot. It now reports the failure once
      (`PERS,result,...,failed`, `failedAtMs`) and discharges the item into the queue's terminal tombstone,
      leaving the per-slot dirty set for the next save trigger. Fixture
      `test_f4_a_failed_completion_is_discharged_and_releases_the_queue` in `test_storage_drain_restore`
      (fault held through the assertion window) asserts the discharge, the reported failure, the guard
      clearing and staying clear, the preserved dirty set and the completing retry; **red-proved** by
      restoring the front-requeue (*"Expected 0 Was 1. the failed completion must be discharged … queue=1"*),
      and a **false green in the fixture's first cut is recorded** (a single-turn fault self-heals on the
      retry, which is what the requeue is for). The S2b.7 parked-instance drain fixture was **re-premised**
      on the drain's ownership transfer (its progress-counter oracle needed the removed spin) and pins the
      corrected `Failed` verdict for the mixed failed-save/gated-restore shape. RAM1: `code 392,360 /
      padding 856`, byte-identical — the withdrawn named queue API cost 256 B of ITCM and was dropped rather
      than widening the floor. **Device evidence owed:** this is an SD fault path and no device fault
      injection exists for it; the behavior is host-verified only. Record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § "F4 — a failed workspace completion is discharged".
- [ ] S4.2 Generation-mismatched release rejected and reported (native fixture reproducing the
      A-releases / C-reuses / late-A-cleanup sequence).
      **PARTIAL 2026-09-25 — the finalization identity is generation-qualified; the lease's own
      release is not wired yet.** `LoopPlaybackRuntime::captureStopOwnerGeneration` now records the
      owner's identity beside the pointer, and `setCaptureStopOwner(admission, generation)` /
      `clearCaptureStopOwner(admission, generation)` are the only writers: the clear is address- **and**
      generation-qualified, so a predecessor's late cleanup on a reused runtime object is a rejected
      no-op instead of nulling a successor's marker. Set site passes the admission's
      `frozenGeneration`; the clear site is `TrackPreparedStopCommit::completeDeferredCleanup`.
      Fixture `test_a_late_cleanup_cannot_clear_a_successor_finalization_marker` covers the
      A-releases / C-reuses / late-A sequence with the matching-identity control, and is **red-proven**
      (with the generation check removed it fails). Cost is **zero**: the field fit existing padding, so
      `sizeof(LoopPlaybackRuntime)` stays 6,344 and RAM1 is byte-identical. Full native
      `2,442 passed / 1 skipped`.
      **Also landed 2026-09-25 (second slice):** the per-slot runtime generation now *advances on
      release* — `TrackPlaybackRuntime::releaseSlot` calls `releaseAllWorkingSetLeases()`, which bumps the
      lease generation, and `slotGeneration(slot)` exposes it as the identity a release must present. The
      release path itself is `(slot, generation)`-qualified and its refusal is reported
      (`RefusedBusy`), so S4.2's "A releases / C reuses / late-A cleanup" sequence is now expressible on
      both the finalization identity (`captureStopOwnerGeneration`) and the slot's working-set lease.
      **Still open:** the two identities are not aliased — `captureStopOwnerGeneration` is the interval's
      `frozenGeneration`, while the lease generation is the slot-runtime lifecycle counter. No current
      caller needs them unified, and unifying them would have to respect that a finalizing interval keeps
      its lease until its slot is actually released. Record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S4.2. **`captureStopOwner` needs a generation:** it is a raw
      `PendingStopAdmission*` cleared by address identity (`TrackPreparedStopCommit.cpp:205`) while ring entries
      are reused at stable addresses, so a late cleanup on the reused runtime object can null a successor's
      marker. Carry `(slot, runtime generation)` and check it at set **and** clear; bump the slot runtime
      generation on every release.
- [x] S4.3 Reuse: activating C after A releases uses A's capacity with no net pool growth.
      **DONE 2026-09-25 (native; the device byte delta remains a device measurement).** Reuse here is
      **capacity retained at a stable address**, not a free/realloc cycle: `releaseSlot` retires the
      slot's working set while `runtimeBySlot_` keeps the `LoopPlaybackRuntime`, so the next activation
      re-admits into the same object and the generation/role state advances instead of accumulating.
      Fixture `test_reactivating_a_released_slot_reuses_its_capacity` runs four release/re-activate
      cycles and asserts, against a baseline activation control: lease unheld after the sweep, generation
      advanced, **same runtime address** on re-admission, same role set, and the same
      `LoopEventStore::freeChunkCount()`. **Red-proven** (with the sweep disabled it fails on "the sweep
      must release the slot's working set"). The entry already has a production caller —
      `TrackManager::startRecordingOnTrack` → `releaseBackgroundPlaybackMergedMidiEventsMemory` sweeps
      every other track — and the memory-pressure path drives it too. **Recorded limit:** the native build
      has no PSRAM, so `ExternalPoolAccounting` cannot report external bytes; the fixture observes reuse
      structurally and the byte-level device claim stays open with the S2.2/S2.3 budget items. **Still
      open:** an explicit sweep on a same-track slot switch. Full native `2,445 passed / 1 skipped`; RAM1
      byte-identical. Record: [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S4.3.
- [ ] S4.4 Declared cap policy when activation is requested beyond the envelope (refuse vs queue),
      reported rather than resolved by allocating.
      **PARTIAL 2026-09-25 — the breach is reported; refuse-vs-queue is deliberately NOT settled.**
      `Track::reportEnvelopeBreachIfAny(slot, role)` reads the per-slot lease census (no storage) and, when
      a class is already held at its declared cap, reports `#MEM_DENIED,reason=cap_reached,class=N` via the
      new `MemoryMonitor::reportDeclaredCapDenied` — a reason deliberately distinct from an allocation
      failure (`prewarm_failed`), so "declared cardinality exceeded" is never read as "the pool ran out".
      It does **not** refuse. The first cut *enforced* the caps and broke fixtures that arm a second slot;
      **the original justification for that was wrong and is retracted (2026-09-25, recorded in § S4b.1):**
      the census counted `PlaybackWindow | LiveCapture` for a slot that was merely *prepared*, so a slot
      holding an **outgoing finalizer** was labelled `LiveCapture` and counted against a live-capture cap.
      The mislabel was in the check, not in the envelope: the outgoing role is `DeferredFinalization`
      (per admitted interval), so two prepared slots never meant two live captures. The cap stays reported
      rather than enforced because the census cannot yet tell the outgoing role from the incoming one, and
      correcting that is S4b's role-transfer work. The enforcing caps remain the lease's own (`WorkingSetLeaseTable`,
      covered by `test_working_set_lease`), which refuse at the point of custody. Fixture
      `test_a_declared_envelope_breach_is_reported_and_does_not_block_activation`. **Open:** nothing is
      refused, so the refuse-vs-queue decision is unanswered and needs either a cap the resource model also
      enforces or an architecture decision on per-track cardinality; the emitted report line is a device
      observation, not host-assertable. Full native `2,446 passed / 1 skipped`; RAM1 byte-identical.
      Record: [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S4.4.
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
      **PARTIAL (2026-09-25): the acceptance sequence now passes end-to-end on S4f.6's carrier.** The user's
      acceptance sequence (A overdubbing → A's stop admitted, finalizer **paused** → switch to B → B starts
      and records MIDI while A still finalizes → A completes without damaging B) is
      `test_switching_starts_the_new_take_while_the_previous_take_still_finalizes` in
      `test_runtime_working_set_ownership`, driven through `TrackManager::finalizeCaptureAndSelectSlot` and
      the real MIDI input route, asserting B's recorded events rather than a prewarm result. B now switches
      (S4f.3's first half) **and starts** (S4f.6), through production routes. Both characterization branches
      were **deleted** — not left conditional — when S4f.6 landed, and the fallback press was removed from the
      dispatch loop, so a regression cannot pass by returning early or by pressing again. Red-proved by
      restoring the pre-S4f.6 `hasStopInFlight()` guard (`2 failed, 10 succeeded`).
      **Still open, and named rather than implied:**
      (a) **check 4's start half** — **now met for the committed switch** (S4f.3 PARTIAL 2: the switch's own
      gesture carries the record start; `test_the_switch_gesture_carries_the_start_without_a_second_press`
      asserts it and is red-proved). The **uncommitted** case is the remainder: when
      `Track::stopOverdubbing` denies admission on capacity, the switch returns `false` and the press is
      dropped rather than retained — that needs the switch's own retained carrier and depends on the
      finalizer-capacity question;
      (b) **new finding to fix with it:** `finalizeCaptureAndSelectSlot` clears all pending-record state at
      its top *before* it knows the switch will commit, so a switch discards a retained start request for any
      slot — the landed fixture cannot see this because it commits the switch before queuing the start;
      (c) **check 7 coverage** — cross-track targets and cleared / populated targets have no fixture yet;
      (d) **check 6's** "a successful start cannot execute twice" has no dedicated fixture;
      (e) the cross-track switch and repeated B → C switches while A finalizes depend on **finalizer
      capacity**, not only on the inbound overlap policy — `Omax` alone does not solve repeated switches
      (recorded per the review). **Retention itself is now proven** (S4f.3 PARTIAL 3): a switch/start
      requested while the predecessor's stop cannot be admitted survives repeated dispatch and executes once
      the obligation binds, without another gesture. What that does **not** prove is **immediate** execution,
      which is the capacity question and stays open by instruction. Record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S4b.1, § S4f.6.
      **Telemetry blocker cleared (2026-09-26): the identity-grade acceptance evidence was in the
      timing-critical *shed* class, which drops non-Tier-A diagnostics silently and does not report a loss —
      that, not a deferral or flush problem, is why the 17:56 rerun ended `INSUFFICIENT_EVIDENCE` with zero
      `#CAP` edge rows while `capture_integrity` reported no eviction.** `DIAG,reentry,`,
      `DIAG,reentry_window_open,` and `DIAG,canonical_release,` are now Tier-A; the denial names
      `slot=`/`interval=` on its timely human row; the clear path names its slot, so `_clear_slot` can prove it
      cleared the *intended* target instead of accepting any marker. No RAM1 cost (`padding 840`, at floor).
      Guards added and red-proved. Record: [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § "The evidence
      edges are Tier-A now". **The zero-concession device rerun then PASSED (2026-09-26):**
      `captures/s4f3-retention-20260926_181732` — `RETENTION_PASS`, exit 0, **20/20 checks ok, zero
      concessions**, identity `5629e7e4f96d` / `da59a7eeb3e0`. The previously-absent identity rows are
      present (`reentry_window_open`, `canonical_release,4,40,6144,8`, `reentry,deny,1311,2,5,1`,
      `reentry,bound,1706,2,5,0`) with no ring eviction reported — confirming the shed, not eviction,
      diagnosis. The bind precedes the release by 24.7 ms under the corrected L5 reading, and the denied
      request was **staged** inside the 1550.3 ms window with C starting afterwards. **Retention plus
      successor capture is closed at this precise scope.** Still open and not claimed: A/B content
      preservation, the raw-only companion fallback, and **MIDI-output playback parity** (the whole-playback
      review reports `inventory_equal=False` because track 8 slots 1–4 sound alongside the loop under test;
      L7's deciding evidence is the loop-scoped dump). Record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § "The rerun after the Tier-A fix".
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
      **NAME CORRECTION 2026-09-27:** the type is `Track::PendingInteractionLatch` (in `include/Track.h`), not
      `PendingInteractionRequest`; the earlier name appears only in this task text. **`switchTarget` is
      declared and compared but never written** — `grep` finds exactly two occurrences (the declaration and
      `matches()`), and the only `Kind` ever assigned is `Stop` (`latchAcceptedStop`). That is consistent with
      the recorded scope rather than a gap: the start/switch carriers live in the **manager's**
      pending-start/switches owners (S4f.3/S4f.6), and this latch carries the **stop** obligation alone, so
      the enum's `Switch`/`Start` variants are unexercised until a carrier needs the latch instead. Recorded so
      the declaration is not misread as implemented switch carrying.
 `PendingInteractionLatch {kind, destination, slot, switchTarget, interval,
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
- [x] S4f.3 **The switch is an accepted interaction (B1, invariant 32).** `TrackManager::finalizeCaptureAndSelectSlot`
      accepts the switch, latches the stop against the interval's slot, and does **not** move `activeLoopIndex`
      while an interval is live unless the stop was admitted and bound to that slot; remove the refusal at
      `TrackManagerCaptureQueue.cpp:329-332`. The interactive/display role may transfer immediately.
      **The target needs a carrier and a commit point:** the existing SlotStateMachine pending switch is
      authoritative (handoff §5); a Switch latch identifies its prerequisite stop, not a second target owner.
      Dispatch the switch after the interval's stop binds. Replace the unconditional pending-switch clear in
      `TrackManagerTransportTick` with consumption at successful switch commit. Fixture: queued
      switch + undo + stop admission in one window → the target is never lost.
      **PARTIAL 2026-09-25 — the admitted-stop half landed; the carrier's dispatch half is open.**
      `finalizeCaptureAndSelectSlot` no longer refuses the move when the stop is **admitted**: the
      `hasStopInFlight()` early return is gone, while the `hasAcceptedUnexecutedInteraction()` guard stays
      (S4f.5 requires the active slot not to move under an accepted-but-unexecuted stop). Safe because the
      deferred job already finalizes from `*job.owner` / `slotIfAllocated(job.slot)` with an explicit identity
      guard, not from the active slot. `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes`
      asserted the *old* rule and now asserts the corrected one (unadmitted still refuses; admitted moves and
      finalizes from the job's own identity) — 101/101. **Still open, exactly as specified above:** the
      `SlotStateMachine` pending switch as the authoritative carrier, dispatch **at the switch's commit
      point**, and replacing the unconditional `clearPendingSlotSwitch` with consumption at successful
      commit; plus the "queued switch + undo + stop admission in one window" fixture. Consequence today: the
      per-slot gesture returns after committing the switch, so the requested **start is not dispatched to the
      target** — S4f.6 lets a *later* start execute, but the switch's own gesture still does not carry one, so
      S4b.1's contract check 4 ("... without another gesture") holds only for the **switch** half.
      **Sharpened by S4f.6 (2026-09-25, new finding):** `finalizeCaptureAndSelectSlot` clears
      `pendingRecord` / `pendingRecordQueuedAtTick` / `pendingRecordRefSlot` / every `pendingRecordSlot` at its
      **top**, before it knows the switch will commit (it can still return early on an unadmitted stop), so a
      switch currently **discards** a retained start request for any slot. The carrier must queue its start
      before that commit and scope the clear, or the contract's "no request is discarded because a predecessor
      is finalizing" fails on the switch path itself.
      **PARTIAL 2 (2026-09-25) — the carrier's commit case landed.** `finalizeCaptureAndSelectSlot` now
      **returns whether it committed**, and `handleToggleRecordForSlot`'s capture-switch branch uses that
      commit point to carry the requested record start for a **record target**:
      `queueRecordingTrack(trackIdx, slotIndex, Config::INVALID_LOOP_SLOT)`. The phase reference is
      deliberately none, because `refSlotPhaseForQueue(track, previousSlot)` would return the predecessor's
      slot (it always has a length here) and so make A's mutable loop timing an accidental dependency of B's
      start — forbidden by name in the owner-provided contract; with no reference the dispatcher uses the
      transport's bar grid, as required for an empty/cleared target. Fixture
      `test_the_switch_gesture_carries_the_start_without_a_second_press` (renamed; the second press is
      gone) asserts the request exists after **one** gesture, drives the dispatcher with no further press,
      and asserts the target records while A still finalizes. **Red-proved** by disabling the carrier
      (`1 failed, 11 succeeded`), and the two-gesture acceptance fixture still passes in that run, so the
      fixture **discriminates** the carrier. Green in both gate modes; full native `2,448 passed /
      1 skipped`; firmware SUCCESS with `code 392,040 → 392,104` and `padding 1,176 → 1,112` (above the
      840-byte floor). **Still open after it:** (a) the **uncommitted** switch — the reachable window where
      `Track::stopOverdubbing` **denies** admission on capacity
      (`hasPendingDeferredOverdubStop() || finalizationResourcesBusy`, DEC-067), so the switch returns
      `false` and the press is **dropped** rather than retained; retaining it needs the switch's own carrier
      with its prerequisite-stop identity, and it lands on the finalizer-capacity question left open;
      (b) the unconditional pending-record clear at the top of `finalizeCaptureAndSelectSlot`, which the
      carrier now survives only because it queues *after* the call — scope it to the finalized slot when the
      retained switch lands. Record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S4f.3, § S4f.6.
      **PARTIAL 3 (2026-09-25) — accepted-switch retention under unavailable stop admission LANDED.**
      Both (a) and (b) above are now closed. (a) `TrackManager::retainRecordStartForSlot` retains the start
      in the **existing** manager pending-start owner without the enabled-set change (the predecessor is
      still the active audible interval, so the set must not move); `handleToggleRecordForSlot` calls it on
      the uncommitted path instead of dropping the press. (b) `finalizeCaptureAndSelectSlot` now performs
      **explicit supersession scoped to the finalized slot** via `clearQueuedRecordingTrack(trackIndex,
      captureSlot)` (and drops the retained request's phase reference when it is that slot), instead of
      clearing the whole track's pending-start state before it knows whether the switch will commit.
      **Three further corrections landed with it.** (i) The overdub guard is **slot-scoped**:
      `hasLiveCaptureStopJobForSlot(captureSlot)` = this interval's stop is bound, commit;
      `hasAcceptedInteractionForSlot(captureSlot)` = accepted-unadmitted, wait. `hasPendingDeferredOverdubStop()`
      is **track-wide** and would let another slot's admitted job stand in for this interval's unadmitted stop.
      (ii) Re-entering `stopOverdubbing()` on an already-admitted stop is avoided, because
      `beginDeferredOverdubStopForSlot` returns false for a pending job and the capacity guard then
      **re-latches**, blocking the very switch being made. (iii) **The activation hazard:**
      `handlePendingRecordStart` was activating with `Track::setActiveLoopIndex` (unconditional), bypassing the
      switch path and moving the active slot underneath an unadmitted predecessor stop; it now uses
      `TrackManager::setActiveLoopIndex` and **explicitly asserts** that a failed activation leaves the active
      slot unchanged with the predecessor's stop in flight, refusing otherwise.
      **Evidence:** `test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds` (A's stop held,
      B overdubbing with its stop capacity-denied, C's switch/start retained, repeated dispatch, capacity
      released, B's stop binds, C starts and records, no double execution), **red-proved three ways** with
      distinct failure lines (retention disabled; activation reverted; the old blanket clear restored); a
      **false green found and removed** (the replaced fixture returned early from a characterization branch in
      the default gate and held nothing). Full native `2,448 passed / 1 skipped`; firmware SUCCESS.
      **RAM1 is now exactly on its approved floor** (`code 392,376 / padding 840`); this slice spent 272 bytes
      and gating the diagnostic recovered none (the strings are FLASH-resident) — **the next slice here must
      free RAM1 or revisit the floor first**.
      **Deliberate deviation, recorded:** the review suggested using the `SlotStateMachine` pending-switch owner
      as well. It is the **playback** switch owner and its consumer cancels a target with no loop data — i.e. a
      record target — so a record-carrying request parked there would be cancelled by the playback lane; and
      carrying the target in two owners contradicts "avoid another target owner". The pending-start owner is
      the single home; the switch is still executed by the existing switch path. Record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S4f.3 (retention).
      **CONCESSION LOOPHOLE CLOSED + EVIDENCE GAPS (2026-09-26).** Conceded links no longer produce a pass:
      **`RETENTION_PARTIAL` (exit 5)** is its own verdict and `RETENTION_PASS` (**exit 0**) is reserved for the
      complete, unconceded chain -- `--allow-setup-issue` now also counts as a concession, because allowlisting
      a report issue is an operator claim, not evidence. **L5 closed:** `beginDeferredOverdubStopForSlot` emits
      `reentryDecision("bound", tick, interval, slot, depth)` on the admission-committed edge, reusing the
      existing emitter and wire format, so a denial and its later binding correlate on the **same interval
      identity**; a `bound` record for the denied interval *before* the release is a `RETENTION_FAIL`, and
      slot-scoped evidence alone still only earns a concession. **RAM1 floor preserved, measurably:** `padding`
      byte-identical at **840**, FLASH `+104 bytes`, because the function is `TRACK_COLD_MEM` (FLASH-resident)
      and never enters the ITCM segment the floor measures -- no space needed recovering. **L3:** the retained
      row now names the track *and* the slot (`Track %d loop %d: switch and record start retained ...`); the
      oracle requires both plus positional ordering, and records that the ordering is positional because a
      human log row carries no timestamp. **L7:** now requires the **existing content pipeline** -- the
      `!CAPTURE_CONTENT` dump for the successor slot's real **pitches** (`--content-dump`,
      `--expect-successor-note`) **and** `content-review.json` parity (`--content-review`); a review alone is
      parity-only and insufficient, a wrong-slot dump or a missing expected note fails. Evidence: **36 host
      tests** green, including the complete unconceded chain reaching `RETENTION_PASS`/exit 0 with zero
      concessions; firmware SUCCESS with the floor untouched.
      **STEP 3 (the first authorized no-concession device run) NOT PERFORMED:** it needs a host sequence that
      performs the real gestures, and that flow does not exist -- `scripts/hitl/flows/queued_switch.py` is a
      Phase-1 skeleton and the action layer has no per-slot record press or slot/track select. Building it is
      host work with device round-trips; no device command was run, and the run is recorded as the next step
      rather than rushed into non-evidence.
      **ORACLE TIGHTENED (2026-09-26) — the first oracle could falsely report `RETENTION_PASS`.** The owner
      reproduced two false passes and both are now refused: C's start **before** capacity release (was a pass;
      now `RETENTION_FAIL`, exit 2, failing L3 and L6), and **removal of B's identity-specific denial** with the
      generic `successor_stop_denied_capacity` row left in place (was a pass; now `INSUFFICIENT_EVIDENCE`, exit
      4). The oracle now requires an **ordered chain** — L1 A's finalizer outstanding → L2 B's stop denied
      **with slot and interval** → L3 C's request retained → L4 capacity released → L5 that same B stop bound →
      L6 C starts exactly once **after the release** → L7 C recorded the expected MIDI → L8 bounded follow-up
      with no duplicates — and a missing or reordered link is never a pass. New verdict
      `INSUFFICIENT_EVIDENCE` (exit 4) distinguishes "the run was staged but a link has no telemetry" from
      `SETUP_INCOMPLETE` (exit 3, "this run is not evidence"). Integrity gates added: `identity_pinned`
      (metadata must carry `git_commit` **and** `firmware_hex_sha256`), `capture_integrity` (no ring eviction;
      base-report `capture_integrity == COMPLETE`), `observation_tail_complete` (log not cut mid-line;
      `capture_complete`), `setup_transitions_confirmed`, and `reported_issues_resolved`. **Telemetry gaps
      reported, not inferred:** L5's identity link is **unavailable** (`captureInterval_` is emitted only by
      `reentryDecision`; stop-tier records carry `frozenGeneration` = `staging.captureGeneration`, a different
      identity space, so only slot-scoped bind evidence exists — closing it needs one emission on the binding
      edge carrying the request's interval); L7 is **count-level** because `storedNoteEvent`/`storedNotes` have
      no callers, so pitch-level needs the content pipeline; L3 rests on a **human log line** with no `#CAP`
      record. Weaker links pass only via explicit `--accept-slot-scoped-bind` / `--accept-event-count-only`
      **concessions**, recorded as concessions rather than proofs. **Correction:** exit-code separation does
      **not** establish that out-of-range notes were generator artefacts; any base-report issue now blocks a
      pass unless explicitly allowlisted, and an allowlisting is recorded as an **operator claim**.
      **Withdrawn:** the earlier "no finalizer-pause control is required" — the 1.335 s measurement is a timing
      observation from a session with no boot banner, so the hook stays a **contingency**. Evidence: 29 host
      tests in `scripts/test_retention_probe.py` (one negative per way to lose a link; complete chain with
      concessions; the same chain without them lands on `INSUFFICIENT_EVIDENCE`), CLI reproductions giving exit
      2 and 4, and the real device log returning `SETUP_INCOMPLETE` (exit 3) while still measuring the window.
      Three oracle bugs were found by its own tests and fixed: an unknown link masking an affirmative
      violation, an early return masking setup failures as `RETENTION_NOT_EXERCISED`, and a follow-up tail
      computed from file order instead of the maximum timestamp. Record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S4f.3 (the retention oracle tightened).
      **DEVICE STIMULUS PREPARED, NOT RUN (2026-09-26).** Per the owner's direction the temporary retention
      stimulus and its evidence oracle are prepared and the device run is **not** authorized, so nothing was
      executed and no target slot is assumed authorized. Assessment: the requirement is decidable from
      **existing** telemetry keyed on slot and interval identity — `DIAG,reentry_window_open,<slot>,…`
      (DEC-067 `successor_start_window_open`, = the predecessor's finalizer admitted and outstanding, named by
      slot), `DIAG,canonical_release,<slot>,…` `canonical_finalization_capacity_released` (= the denied window
      closes), `DIAG,reentry,deny/deny_resource,<tick>,<interval>,<slot>,<depth>` (the denial, from the decision
      owner) plus `ODUB,stop,admission,…,successor_stop_denied_capacity`, and `RECA,<slot>,<tick>` (the start,
      so exactly-once is a count) — never on the 21 µs `OVERDUB_STOPPING` state. Exposure is **measured** from
      real device records: `reentry_window_open,0,16387,1536` at 136,629,769 µs to
      `canonical_release,0,1536,8` at 137,965,158 µs = **1,335,389 µs (1.335 s)** for a 2-bar loop, so a
      natural workload suffices and **no finalizer-pause control is needed** (its bounded, compile-gated
      design is recorded as contingency only, not implemented, so the RAM1 floor is untouched). That session
      has **no boot banner** (108 s uptime), so the figure is a timing observation to reconfirm under a fresh
      boot, not a verdict. Stimulus authored in the sanctioned form
      (`STIM-BASE-2-ONE-OD-01-TEMP-RETENTION-A`, forked, **not registered**, sidecar under the change
      directory); pass criteria live in the oracle, not the sidecar. Oracle `scripts/hitl/retention_probe.py`
      → `retention-review.json` with setup **distinct** from retention by verdict and exit code
      (`SETUP_INCOMPLETE` 3, `RETENTION_NOT_EXERCISED` 2, `RETENTION_FAIL` 2, `RETENTION_PASS` 0); setup
      checks `fresh_boot`, `tempo_verified` (read-back required), `seed_content_on_a`,
      `finalizer_admitted_on_a`, `capacity_released_after_a`, `capacity_denied_window_wide_enough` (150 ms
      floor); retention checks `successor_stop_admission_denied` (must fall *inside* A's window),
      `successor_started_exactly_once`, `activation_invariant_held`, `no_stop_obligation_replay`. Validated
      **without a device**: 11 host tests green (`scripts/test_retention_probe.py`) and a run against the real
      log returns `SETUP_INCOMPLETE` while still measuring the window — a non-staged run is never reported as a
      retention result. **Setup determinism:** the three targets are **not yet authorized** (the documented
      scratch slot is one slot, track 5 / slot 1); tempo is applied and read back after the final serial open;
      freshness is enforced by the oracle. Record: [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S4f.3
      (device-retention stimulus and oracle). **Adjacent defect found, reported not fixed:**
      `scripts/hitl/stimuli/yaml_view.py` `dump_yaml` emits invalid YAML because the dict branch tests
      `isinstance(item, list)` while `StimulusSpec`'s list-ish fields are tuples.
      **HITL (2026-09-26) — the registered entry points were run and produced no pass; the new path is
      uncovered.** Firmware flashed from the verified tree (`firmware_hex_sha256 14a1aea7…`,
      `git_commit e743b97c…`), device prep per the hitl-content-verification skill (fresh upload, documented
      scratch slot **track 5 / slot 1**). Stop-interruption wrapper: reached the fixture properly
      (`PLAYING->OVERDUBBING: 1`, record on/off `32/32`, persist `ok=True`, `OVERDUB_STOPPING: 1`,
      intervention sent) but the focused verdict is `INCONCLUSIVE_CAPTURE` /
      `no_device_stop_inside_stopping`, and the session log shows why it **cannot** be hit: the
      `OVERDUBBING -> OVERDUB_STOPPING -> PLAYING` window is **21 µs** because the admitted stop defers
      finalization (DEC-078), so the fixture's premise is architecturally mismatched, not flaky. Base leg
      `FAIL (FIRMWARE_CONTRADICTION)` on `overdub_unmatched_open_notes` / `overdub_out_of_range_notes` — the
      skill's documented **setup** verdicts. Baseline preset (`STIM-BASE-2-2-01`):
      `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`, `RECORDING: 0`, the known **base-preset setup race**
      already attributed to the still-open selection fixture, with tempo correct
      (`record loop length 1536/1536`). **Tempo must be re-pinned per run** (found at 160.0 BPM; pinned to
      120.0 via DEC-086) because the pin does not survive the serial-open reset. **No registered stimulus
      exercises this slice's behaviour**, so these runs are regression context only; the owner declined a
      temporary variant this session, so the retention path is **host-verified only**. Full record:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S4f.3 (HITL attempt).
      **FIRST AUTHORIZED CAMPAIGN RUN (2026-09-27) — executed on the authorized targets, verdict
      `SETUP_INCOMPLETE` (exit 3), no acceptance claimed.** The block recorded above ("that flow does not
      exist") is **stale as of `260a8c14`**: the host gesture layer exists
      (`scripts/hitl/actions/slots.py` — per-slot record/overdub/clear and track select),
      `scripts/hitl_retention_handoff.py` drives the sequence and enforces its preconditions, and
      `scripts/test_retention_handoff_plan.py` pins the gesture order. The device's own read-only census
      (`hitl_boot_census.py --library-observe`) then established that **60/64 slots hold committed content
      and no track has three empty slots** — track 5 in particular has none — so the user authorized
      clearing exactly one slot and the run used **track 8, A=4, B=5, C=8**, every clear identity-grade and
      every target re-verified empty by census before any gesture. Identity
      `git_commit 5849d620ebc5` / `firmware_hex_sha256 adbb079fd552`; fresh boot, no ring eviction, complete
      tail, tempo verified. The device drove the whole chain (A's overdub → switch to B → B's overdub → B's
      stop **denied on capacity** → C's switch/start **retained** → capacity released → C started once and
      recorded) and all eight links are present in order. **Three findings, all recorded rather than worked
      around:** (1) `capacity_denied_window_wide_enough` reports **34.8 ms** against the 150 ms floor, but it
      is the last check still computing its edges from the **deferred `#CAP` rows** in a file whose own
      documented rule is to prefer the timely human rows — both releases flushed in one burst 48 µs apart, so
      that timestamp is the flush, not the edge, while the human rows put the window opens **1.56 s** apart
      (the order of the earlier 1.335 s/1.55 s passes). The check cannot currently distinguish "too narrow"
      from "flushed late", so the run is not usable evidence for the width either way. (2) L2's identity gap
      is a **half-converted emission, verified in the source**: `Track::stopOverdubbing`'s
      `headJob->admission.pending` branch logs no slot/interval while its `else` branch does, and the
      no-identity branch is exactly the reason this scenario produces. (3) **`DIAG,ws_release` appears zero
      times** in a completed A/B/C chain — measured, and consistent with the G0 trace: the same-track
      handoff excludes its own track from the take-start sweep and the cross-track sweep needs a start on
      another track while a lease is still held, so this shape exercises **none** of the three routes S4.1's
      packet names. Full record, timeline, reproduction commands and the two-track-number-base trap:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § "S4.1 device leg + S4f.3 no-concession run".
      **NO-CONCESSION DEVICE GATE CLOSED 2026-09-27 — `RETENTION_PASS`, exit 0, 20/20 `ok`, zero
      concessions.** Re-run on the same authorized targets (track 8, A=4, B=5, C=8) with the two fixes in the
      flashed image (`675a115a`), identity `git_commit 675a115a8225` / `firmware_hex_sha256 e76e1818e069`.
      Every link is on its own evidence: L1 `DIAG,reentry_window_open,3,1117,2304`; the exposure gate now
      **bounded at 531.1 ms** across A's window opening to the successor's denial (the old formula would have
      said 34.8 ms from the release turn's delta, which is a servicing latency — the two are now separate
      report fields and only the bounded one gates); L2 reads the **device-timestamped**
      `DIAG,reentry,deny,1321,2,4,1` (slot 4, interval 2) rather than falling back to `UNKNOWN`, which is what
      the `stop_job_pending` identity emission bought; L3 the retained row for slot 7 between denial and
      release; L4 `canonical_release,3,40,2304,8`; L5 `DIAG,reentry,bound,1717,2,4,0` on the **denied**
      interval; L6 exactly one `RECA,7` after the release; L7 note 67 in storage **and** effective with the
      fields agreeing; L8 a clean bounded follow-up. Content pipeline: declared counts
      `{'effective#0': 2, 'record#0': 2}` match parsed, `storage_matches_effective: true`. **Three host
      defects were found by running and fixed, not worked around:** `exit_code` was a computed property that
      `report_dict` never serialized (so every review file omitted the authoritative code); the oracle's CLI is
      **0-based** for slots *and* tracks while the driver's is 1-based, and passing the driver's numbers makes a
      correct run look like it had no denial at all (now documented in the docstring and per-argument help, with
      the denial row's 0-based `slot=` converted explicitly); and the first cut of the exposure fix subtracted
      host seconds from device micros, producing a *negative* window — it now reports the width ungated instead.
      Record: [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § "S4f.3 no-concession device gate — CLOSED".
      **CHECKED 2026-09-27 — the device gate is green and the carrier is proven end-to-end.** The
      no-concession retention run returns `RETENTION_PASS` (exit 0, 20/20 `ok`, zero concessions) on track 8
      slots 4/5/8, and every clause this task names is on its own evidence in that run: the switch **commits**
      while the predecessor finalizes (L1 window, L4 release), an unadmitted stop **does not** move the active
      slot (the denial is staged inside the window and the successor's start is retained, L2/L3), and the
      target is **never lost** — L5 binds the *same denied interval*, L6 shows exactly one `RECA` **after** the
      release, L7 records the expected MIDI, L8 shows no duplicate execution. Remaining non-blocking scope is
      named in S4f.6's note (a positive acceptance record) and S4f.10 (fixture cases (c)/(d)/(e)); neither
      reopens this behaviour.
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
- [x] S4f.6 **Start side too.** Implement all transition/consumption rules in
      **PARTIAL (2026-09-25) — the accepted-start carrier landed and unblocked S4b.1's acceptance.**
      Landed: `TrackManager::startRecordingTrack` returns `bool` and every refusal returns `false`;
      the refusal is **slot-scoped** via `Track::hasStopInFlightForSlot(uint8_t)` (reads
      `captureStopJobHead()->slot` and `interactionLatch()->slot`), so a stop bound to A no longer blocks a
      start on B; `handlePendingRecordStart` sets the target active **first** and consumes the request
      **only on success**, so a refusal retains it instead of discarding it.
      **Retraction:** this task's earlier text (and the § S4b.1 path review) said the refusal "strands the
      request: retained but never executed". The committed dispatcher consumed **unconditionally** —
      `startRecordingTrack` returned `void` and the four consumption lines followed it — so a refusal
      **discarded** the request. Symptom unchanged, mechanism corrected.
      Evidence: the S4b.1 acceptance fixture now asserts the requirement **unconditionally** (both
      characterization branches deleted, fallback press removed) and is green in default and
      `S4B_ACCEPTANCE=1` modes; red-proved by restoring the pre-S4f.6 guard (`2 failed, 10 succeeded`);
      native regression 2,448 passed / 1 skipped; firmware SUCCESS with RAM1 byte-identical
      (`padding 1,176`).
      **Still open:** the combined in-flight query and the manager-owned accepted-**start** carrier with a
      **named acceptance report** (pass 4C) so the unconditional labels at
      `TrackManagerCaptureQueue.cpp:221-222,236-237` are never emitted for a start that was not admitted;
      the former whole-track switch-clear remainder is **resolved** by the slot-scoped
      `clearQueuedRecordingTrack(trackIndex, captureSlot)` call. Arm-and-wait and
      failed-then-retried exactly-once fixtures are present and registered; those are
      not future work. See NEXT-STEPS.md for the current source-backed audit.
      `IMPLEMENTATION_CONTRACT.md` §5, including manager-owned requests with no live interval, deferred
      eligibility. Add paused-worker fixtures for every listed sequence.
      A second Stop supersedes an unexecuted Start explicitly; after Start commits it stops the new interval. `TrackManagerCaptureQueue.cpp:24-29` accepts the record/overdub start and
      schedules it rather than dropping it; `pendingRecord`/`pendingRecordSlot` must not clear their state when
      the start was refused.
      Contract and path review:
      [`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § S4b.1 behavioral contract, § S4f.6.
      **CHECKED 2026-09-27 — the carrier is device-proven and the acceptance-report remainder is RE-SCOPED,
      not open behaviour.** The transition/consumption rules this task specifies are implemented and green end
      to end: `startRecordingTrack` returns a slot-scoped verdict, `handlePendingRecordStart` activates the
      target first and consumes **only** on success, and the device run shows a retained start executing
      exactly once after the predecessor binds (L3 retained row between denial and release, L6 exactly one
      `RECA` after the release, L8 no duplicate). **The "unconditional labels" clause is stale:** the sites it
      cited no longer emit an acceptance label — the invariant-violation path logs a refusal and `continue`s
      (`TrackManagerCaptureQueue.cpp`, the `activeAfterActivation != targetSlot` branch), and the start is
      recorded only after `startRecordingTrack` returns true. What genuinely remains is a **positive** record
      that an accepted start was *admitted*, which is instrumentation rather than a correctness gap and is
      gated behind `SESSION_CAPTURE` per the repo rule; it is carried as the next slice, not as an open
      behaviour. Fixture-case gaps: S4f.10.
- [ ] S4f.7 **Single write authority (B4).** Covered by S4b.5; record the source gate that
      `Track::setActiveLoopIndex` is reachable only through `TrackManager::setActiveLoopIndex`.
- [ ] S4f.8 Make the waits observable as **scheduling**, not refusal: `accepted_stop_finalizer_wait` counts
      waits and names the holding owner; the pre-fix counterfactual is `successor_stop_denied_capacity` +
      `reentryDecision("deny", ...)`.
- [ ] S4f.9 Prove no new capacity is implied: the latch is one small state per live interval, not a stored
      stopped interval, so the descriptor-ring finding is unchanged.
- [x] S4f.10 Native fixtures: (a) stop while the predecessor finalizes -> accepted -> exactly one request ->
      completes after retirement; (b) the same with a slot switch before the drive, asserting `job.slot`/
      `job.owner` are the interval's; (c) transport stop before the drive -> no `TRACK_PLAYING`; (d) stop +
      record-start in the same tick; (e) stop + undo; (f) MIDI Start/seek between latch and drive;
      **(g) park terminal does not cancel (Rev 23.2):** drive the park past its diagnostic bound with the finalizer
      still occupied, then free capacity → the accepted stop executes exactly once, the interval is **not** left
      recording, and no second press is required.
      **COVERAGE AUDIT 2026-09-27 — (a), (b), (f) and (g) exist and are registered; (c), (d) and (e) do not.**
      No fixture in the tree carries an `S4f.10` marker, so each case was mapped by its assertion instead:
      (a) `test_b02_accepted_stop_waits_for_canonical_credit_then_executes_once` (accepted, waits, executes
      once after the credit frees); (b) `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes`
      plus the S4f.3 carrier fixtures, which drive a switch before the drive and assert the interval's own slot
      (`DIAG,reentry,bound` names the **denied interval**, device-confirmed); (f)
      `test_s4f4_the_boundary_is_recomputed_in_the_tick_domain_at_execution` (with its recorded coverage limit:
      the `ClockManager` entries that *move* the domain are harness stubs, so the decision is proven and the
      production entry is device territory); (g) `test_s2b6_park_terminal_never_cancels_an_accepted_stop`.
      **Missing, and this is the next bounded slice:** (c) transport stop before the drive, asserting no
      `TRACK_PLAYING`; (d) stop + record-start **in the same tick**; (e) stop + undo. (c) and (e) both test the
      same contract clause — a latch that is superseded, not executed, when the world moves under it — and (d)
      tests the boundary the drive's `currentTick == pendingRecordQueuedAtTick` guard exists for.
      **ATTEMPTED AND WITHDRAWN 2026-09-27 — (c) needs the real commit TU linked first, which is not a fixture
      change.** An experiment in `test_continuous_capture` drove the production transport-stop entry
      (`Track::stopOverdubbingToStopped`) against a live interval holding an unadmitted latched obligation. It
      cannot pass in this suite as it stands, and the reason is **structural, not a bug**: with the canonical
      owner released, that entry does not defer — it consumes the obligation and completes the stop
      **synchronously** through `Track::commitCaptureForStop`, which this suite's `off_path_deps.h` stubs as
      `OFF_PATH()` ("fail closed when the fixture accidentally enters an unrelated route"). The preconditions
      are now known exactly: the latched state needs a **live canonical owner** (`prewarmCanonicalReservation`
      + `appendCanonicalEvent`) to make `beginDeferredOverdubStop` refuse and take the DEC-078 accept-and-latch
      branch; the obligation then has to be checked *before* that owner is released, and the entry's
      synchronous completion is what the harness refuses. Closing it therefore means linking the real
      `src/Track/TrackCaptureStopCommit.cpp` and checking its closure — the pattern already exists
      (`test_drain_reentry_callback_safety` defines `A34A_LINKS_REAL_TRACK_TRANSPORT`, and `off_path_deps.h`
      gates on it) — so it is a **harness-coverage slice**, not a native-fixture slice, and it must not be
      rushed into a passing-but-non-discriminating test. The experiment was **reverted**, leaving the suite
      green (101/101), rather than left failing. (d) and (e) remain unassessed for the same reason.
      **HARNESS ATTEMPTED AND MEASURED 2026-09-27 — the closure does not fit this suite, and the reason is a
      three-way coupling in one macro.** Enabling `A34A_LINKS_REAL_TRACK_TRANSPORT` in this suite and linking
      the commit TU was tried end to end. The commit TU's own closure is small and is now known exactly
      (measured by linking, not guessed): `TrackCaptureStopCommit.cpp` plus `TrackManagerMidiLedFeedback.cpp`,
      `TrackManagerPlaybackPolicy.cpp` and `TrackManagerMemoryPressure.cpp` (the owners whose stubs that macro
      removes), and three telemetry symbols — of which `resetActiveLoopAfterEmptyCapture` is a genuine state
      owner (it discards the empty capture and clears the loop length), so it cannot be stubbed away. **But**
      that same macro also gates nine stubs spanning **three independent boundary families**: track transport
      (the target), **storage-persist** (`markLoopSlotMaterialDirty`, `admitLoopSlotPersist`,
      `requestDeferredSaveState`) and **edit-session** (`foldLiveCaptureIntoNoteEditSession`, plus
      `onSelectedSlotChanged`/`beforeSelectedSlotChange`/`emitEditEvent`/`reopenNoteEditSession`/
      `persistActiveNoteEditSession`). Enabling it for the transport family therefore disables the other two,
      and the re-link then demands the full drain closure (~30 storage TUs) plus `EditManager.cpp` and
      `NoteEditSessionLifecycle.cpp` — machinery the continuous-capture suite deliberately does not carry.
      **Conclusion: this needs a harness-architecture decision, not a fixture.** The options, recorded rather
      than chosen unilaterally: (1) **split `A34A_LINKS_REAL_TRACK_TRANSPORT` by owner family** so a suite can
      enable the transport family alone — the clean fix, but it edits the shared harness used by
      `test_drain_reentry_callback_safety` and needs a full native re-run; (2) place the (c)/(e) fixtures in a
      suite that already links the storage+drain closure, noting that the commit TU additionally needs
      `TrackManagerCaptureQueue`'s slot-owner TU; or (3) accept them as device-observable claims. The
      simulation was reverted in full (both files restored, suite green at 101/101, no test or harness change
      committed).
      **HARNESS SPLIT LANDED 2026-09-27 (option 1) — (c) is now a passing fixture; (d) and (e) remain open.**
      `test_continuous_capture/off_path_deps.h`'s single macro was split **by owner family**, each family
      carrying its own gate while the historical umbrella keeps every existing suite's behaviour byte-for-byte:
      `A34A_LINKS_REAL_TRACK_TRANSPORT` (Track playback/queued-start owners + the LED update),
      `A34A_LINKS_REAL_TRACK_STOP_COMMIT` (**new**: the synchronous stop-commit owners alone —
      `stopRecording` / `stopRecordingToStopped` / `commitCaptureForStop`),
      `A34A_LINKS_REAL_STORAGE_PERSIST`, `A34A_LINKS_REAL_EDIT_SESSION`, and
      `A34A_LINKS_REAL_TRACK_STOP_TELEMETRY` (**new**: the stop-path telemetry helpers).
      `A34A_LINKS_REAL_TRACK_TRANSPORT_ALL` still means "all of them", so
      `test_drain_reentry_callback_safety` is unchanged (8/8). Enabling the stop-commit family links the real
      `TrackCaptureStopCommit.cpp`, `TrackManagerMidiLedFeedback.cpp`, `TrackManagerPlaybackPolicy.cpp`,
      `TrackManagerMemoryPressure.cpp` and `RecordStopLength.cpp`; `resetActiveLoopAfterEmptyCapture` is
      supplied with its **real body** because it is a genuine state owner, while the two diagnostics
      (`collectStopPathStorageStats`, `logRecordStopStage`) are inert stand-ins — the real pair computes a
      census and emits a log line, and neither feeds a decision.
      **Finding recorded from the split:** two boundaries that look like transport are actually the playback
      TU's — `sendAllNotesOff` and `silenceSlotMidiOutput` live in `TrackPlaybackHotPath.cpp` beside the MIDI
      silence owners **B03 counts** (asserting `g_nativeSilenceTrackCalls == 1`), so linking the broader
      transport family silently replaces that instrumentation. The stop-commit family therefore leaves them as
      inert stand-ins; what they omit is the per-slot LED/ledger clearing, and a future fixture that depends on
      *that* must link the playback TU and rework B03's observation. The same is true of the three
      persistence requests the stop route reaches: they are **inert by configuration, not by stubbing**
      (`PersistenceAdmit.cpp` compiles each to an immediate `return` under `BYPASS_STOP_UNDO_SAVE`, which the
      native env sets), so the stop-commit family stands them in without making this suite exercise the
      persistence path.
      **Fixture (c)** `test_s4f10c_accept_and_latch_then_transport_stop_supersedes_before_the_drive` asserts,
      **while the canonical owner is still live**, that the stop is *accepted and latched* (obligation
      present, no stop descriptor consumed, the interval still overdubbing, the obligation latched against the
      interval's own slot **and** identity, the capture store unchanged) and then, after the transport stop,
      that the obligation is **consumed** and never re-created by eight further drive turns, with the interval
      resolving to STOPPED and never PLAYING. **Red-proved:** commenting out the production
      `consumeAcceptedStopObligation()` in `Track::stopOverdubbingToStopped` fails it exactly on
      *"transport stop must consume the accepted obligation…"* (1 failed / 101 succeeded); `src/` was restored
      and is clean. **Still open:** **(d)** stop + record-start in the same tick needs the manager's
      pending-start owner linked (`TrackManagerCaptureQueue.cpp`), and **(e)** stop + undo needs the real undo
      path and the `TrackUndo` closure. Both are bounded and now have a working family to build on.
      Verification: `test_continuous_capture` **102/102**, `test_drain_reentry_callback_safety` 8/8, full
      native **2,470 passed / 7 skipped** (baseline 2,469 + the new fixture), OpenSpec validates.
      **CHECKED 2026-09-27 — all seven cases (a)-(g) are covered, and (c)/(d)/(e) landed this session.**
      Each of the three new fixtures was **red-proved** by disabling the exact production guard it asserts on,
      and `src/` is clean afterwards — this was a harness-and-test slice, no production defect was exposed:

      | case | fixture | red proof |
      |---|---|---|
      | (a) | `test_b02_accepted_stop_waits_for_canonical_credit_then_executes_once` (pre-existing) | — |
      | (b) | `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes` + the S4f.3 carriers | — |
      | **(c)** | `test_s4f10c_accept_and_latch_then_transport_stop_supersedes_before_the_drive` | commenting out `consumeAcceptedStopObligation()` in `Track::stopOverdubbingToStopped` → fails on *"transport stop must consume the accepted obligation…"* |
      | **(d)** | `test_s4f10d_a_request_queued_in_this_tick_is_not_dispatched_in_this_tick` | removing the `currentTick == pendingRecordQueuedAtTick` guard in `handlePendingRecordStart` → fails on *"a request queued in this tick must survive this tick's dispatch pass"* |
      | **(e)** | `test_s4f10e_an_undo_replacing_the_interval_supersedes_the_accepted_stop` | neutralising `request.interval != captureInterval_` in `driveAcceptedInteraction` → fails this fixture **and** the pre-existing S4f.4 supersession fixture |
      | (f) | `test_s4f4_the_boundary_is_recomputed_in_the_tick_domain_at_execution` (with its recorded coverage limit) | — |
      | (g) | `test_s2b6_park_terminal_never_cancels_an_accepted_stop` (pre-existing) | — |

      **(c)** asserts DEC-078's accept-and-latch obligation **while the canonical owner is still live**
      (obligation present, no descriptor consumed, the interval still overdubbing, the obligation keyed to the
      interval's own slot *and* identity, the capture store unchanged), then that the transport stop consumes
      it and eight further drive turns never re-create it, with the interval resolving to STOPPED and never
      PLAYING. **(d)** asserts the same-tick guard defers rather than drops: the request survives its own
      tick's pass with nothing started, and one boundary later it starts exactly once. **(e)** asserts an undo
      that replaces the interval leaves the obligation **superseded, not bound** — the identity is mutated
      directly rather than driving `TrackUndo.cpp`, which is a deliberate boundary of this suite, because
      identity invalidation is the whole of what undo contributes to that decision while the supersession
      decision itself is the real drive.
      **Coverage notes recorded rather than hidden:** (e) is `TrackUndo`-equivalent, not a full undo drive;
      (f)'s production-entry half stays device territory as its own note says. Verification:
      `test_continuous_capture` **104/104**, `test_drain_reentry_callback_safety` 8/8, full native
      **2,472 passed / 7 skipped** (baseline 2,469 + the three new fixtures); OpenSpec validates.
- [ ] S4f.11 Measure acceptance -> execution latency **per interaction class** on device against the declared
      budgets (design Revision 17) and record the numbers: stop overdub, record/overdub start, slot/track switch,
      cold undo/redo, load loop. Exceeding a budget is a reported event, not a silent wait.
- [ ] S4f.12 **DEFERRED -- progress presentation.** Show that an accepted interaction is being executed when it
      cannot meet its budget, instead of leaving the user to infer failure. **Gate: S4f.11 measured AND a
      mitigation review shows the budget cannot be met by scheduling, prewarm or windowing.** Rules when taken: it
      presents an *accepted* action (never a denial, never a cue to press again) and never justifies a widened
      budget. Do not start before the gate.
### Shared record/overdub finalization prerequisite (2026-09-27)

[Implementation contract](SHARED-CAPTURE-FINALIZATION.md), design Revision 25,
DEC-093 — shared capture finalization. These tasks close an implementation omission in the
existing requirement; they precede accepted-sequence device closure. Each slice requires its
own execution packet and records fixture/native/build/device results separately.

- [x] S4f.15 **Shared capture finalization contract.** Trace existing intent versus record's inline route;
      specify owner/identity, ingress, first-bar/onward playback, undo, persistence, caller migration and
      bounded verification matrix. Verification: contract + adversarial review recorded; strict OpenSpec
      valid. This checkbox represents documentation only, not firmware readiness or acceptance.
- [x] S4f.16 **Production finalization fixtures.** Establish controllable real record-stop/worker/undo/storage
      linkage for the contract's COMMON-IDENTITY and MODE-ROTATION cases. No stop-owner stand-in. If shared
      harness gates need splitting, separate transport/storage/edit families and rerun dependent suites.
      Verify inline record stop fails the deferred-ownership oracle; pin actual closure and per-slice bounds.
      **DONE 2026-09-27:** real stop/cold-helper/worker/undo/storage closure shared with drain fixtures;
      four rotation baselines pass, strict contract mode produces four expected failures (record deferred
      ownership, record interval stamp, deferred persistence). Full native: 2,490 passed / 7 skipped;
      final focused suite 4/4. Closure, bounds, retained-state measurements and limitations:
      [S4F16-EXECUTION.md](S4F16-EXECUTION.md). Fixture completion only; shared firmware behavior remains open.
- [x] S4f.17 **Common interval and stop admission.** Extend Track's existing latch/job/drive to both modes;
      stamp every actual start, carry mode/geometry/raw/held-note receipts, preserve failed-preflight ownership
      and accept busy-owner stops. Verify R→R, R→OD, OD→R, OD→OD at controlled grains, repeated drive,
      stale identity, empty capture, terminal wait and runtime regeneration; no new ring/capacity owner.
      **2026-09-27 preflight:** production enablement depends on record preparation, source handoff,
      publication and caller conversion in S4f.18–S4f.21. A standalone record-admission switch would
      feed an overdub-only worker or expose premature manager continuations. Connected S4f.17–S4f.21 scope approved by the user;
      implementation in progress, with individual checkoffs retained. See [S4F17-EXECUTION.md](S4F17-EXECUTION.md).
      **Prerequisites implemented:** successful record/overdub starts share interval stamping;
      rejected record transitions preserve content; accepted stops validate LoopId/mode; jobs carry
      Capture mode without layout growth. Record binding/geometry/held-note receipts and common
      worker execution remain open. S4f.18's maximum-tick prerequisite is implemented; normalization
      and closure still await the bounded Record source path. No admission/publication gate checked.
      **2026-09-29 S17.1 + S17.2 — production Record admission and stop routing.** `stopRecording` and
      `stopRecordingToStopped` now admit through `beginDeferredRecordStopForSlot` and keep their
      destination behaviour at the stop edge (`Track::completeAdmittedRecordStop`: origin alignment,
      truncation rewind, loop phase, PLAYING/STOPPED, the record-stop playback handoff); a denied stop
      is retained as an accepted obligation and admitted later by `driveAcceptedInteraction`, which now
      dispatches on the request's capture mode. The inline `prepareRecordStop`/`commitCaptureForStop`
      Record fallback is gone from both entry points, and the pass, its `RecordPassAdded` undo entry and
      its persistence admission come from the worker's publication. The strict R→R/R→OD/OD→R/OD→OD
      matrix is green (45/45, `S4F16_REQUIRE_CONTRACT=1`), full native is 2 612 cases / 2 605 succeeded /
      7 skipped, and `teensy41-capture-serial` builds with RAM1 padding 2 040 (floor 840). Three defects
      fell out of the migrated fixtures and are fixed in the same slice: (1) the deferred Record
      normalization closed open tails at loop end (the *overdub* policy) instead of the stop playhead's
      loop phase, changing the published row set; (2) the deferred-grain prologue required the live
      `loopLengthTicks` to equal the job's, which parks a completed Record job forever once the take is
      filtered out or a successor resets the slot; (3) the S4f.19 continuity gate waited on a frozen
      window build that only the playback owner can advance, deadlocking retirement on an empty take.
      The Record admission also declares its bounded preparation estimate so the job's declared work is
      honest. Caller conversion (S4f.17 remainder) and S4f.18's task checkbox stay open.
- [x] S4f.18 **Bounded record preparation.** Add the record source mode to existing preparation, using
      frozen canonical raw ranges and maintained length summaries; defer normalization/closure/order/pass
      construction without a full-take staging copy or changed input capacity. Verify exact short/dense/long
      content, held notes, stop-length edges, resource failure and bounded units; mutation restoring an
      input-edge full walk or wrong capture mode must fail the production fixtures.
      **2026-09-27 source-consumer slice:** the existing playback fill accepts a fallible,
      generation-qualified frozen CaptureRange reader and RecordCoverage tick projection.
      Every examined row consumes quantum; only matching rows consume window capacity.
      Source reuse, pool expiry and read failure deny. This prerequisite does not enable record
      stop, synthesize closures, build passes, or prove onward-window deadlines. Parent remains
      open; see [source execution evidence](S4F18-SOURCE-EXECUTION.md).
      **Held-note identity prerequisite:** accepted Record input retains its NoteId in the
      existing pending-note map; physical and stop Offs use that receipt after raw freezing.
      Overdub wrap stays reservation-owned. Frozen-source, stop-content, pre-roll and rejected
      input fixtures cover this prerequisite. Deferred closure output and pass preparation
      remain open; [closure execution evidence](S4F18-CLOSURE-EXECUTION.md).
      **2026-09-28 closure-output consumer:** a job-qualified bounded helper writes one
      reserved canonical closure row per grain, preserving successor capture and retrying
      denied output without consuming obligations. Raw/output/binding generations are checked.
      Controlled consumer fixtures include 257 held notes without overdub staging. Production
      Record dispatch/admission, normalization and pass construction remain open; see
      [output execution evidence](S4F18-OUTPUT-EXECUTION.md).
      **2026-09-28 normalization oracle:** real stopped/playing Record stop fixtures pin
      overflow projection, tail repair, minimum-length enabled/disabled, filtered-empty
      geometry and the full 2,056-row inventory beyond overdub staging. The playing case
      links real playback reanchoring/ledger-clear ownership via an independent harness
      opt-in. Bounded normalization and pass construction remain open; see
      [normalization execution evidence](S4F18-NORMALIZATION-EXECUTION.md).
      **2026-09-28 bounded pass-directory transfer:** the existing store transition now
      supports one prewarmed reference move or order-restoring swap per grain, with
      owner/pool/destination receipts and partial-cancellation ownership. Fully prewarmed
      synchronous callers share the primitive; Record worker phase integration and full
      normalization remain open. [Transfer execution evidence](S4F18-TRANSFER-EXECUTION.md).
      **2026-09-28 worker pass staging:** PrepareTransfer now builds directly in the
      job receipt, one reference move/order swap per turn. Partial receipts reject
      publication and companion borrowing. A 282-row production fixture retains a
      live successor through transfer/publication; completed unused undo estimates
      are retired to prevent a discovered atomic-transfer stall. Verification and
      measured ring growth: [staging execution evidence](S4F18-STAGING-EXECUTION.md).
      Record normalization/admission and connected playback/caller work remain open.
      **2026-09-28 bounded tail-repair prerequisite:** the existing stop-finalize
      policy now has a generation-checked streaming consumer with bounded reset/
      enumeration and acknowledged output retry. Fixed 24,576-byte MIDI-key scratch
      replaces per-event allocation/repeated scans in this consumer; no resident
      role or production Record dispatch is added. Handwritten inventories and real
      helper differential fixtures preserve policy, including >2,048 input rows.
      Sort/minimum-length filtering and integration remain open. See
      [tail-repair execution evidence](S4F18-TAIL-EXECUTION.md).
      **2026-09-28 minimum-length decision prerequisite:** the existing sanity
      policy has a bounded ordered-row cursor retaining exact short-pair source
      ordinals until acknowledged. Retrigger/channel/strict-threshold semantics
      match the existing helper; no source mutation or per-event removal bitmap
      is added to firmware. ARM scratch 16,384 bytes / cursor 48 bytes, not resident.
      Ordering, canonical compaction and production integration remain open;
      [minimum-length execution evidence](S4F18-MINIMUM-EXECUTION.md).
      **2026-09-28 compaction retirement correction:** the existing canonical
      rewrite now pops surplus tail references instead of erasing and shifting
      its directory. Per-grain prefix/reference fixtures expose the old bound
      violation; exact final rows and stale-owner denial remain pinned. Record
      ordering/filter-writer integration remains open. See
      [compaction execution evidence](S4F18-COMPACTION-EXECUTION.md).
      **2026-09-28 bounded stable tick ordering:** private prepared dense chunks
      can now be sorted in place, one bounded comparison/swap/control unit at a
      time. Fixed 864-byte ARM cursor; no whole-take mirror/index or store growth.
      Generation/pool and persistence/share checks protect source ownership;
      equal-tick identities remain stable. Filter compaction and private-output
      publication/persistence integration remain open, as does Record dispatch.
      [Ordering execution evidence](S4F18-ORDER-EXECUTION.md).
      **2026-09-28 private filter compaction:** the store applies rejected ordinal
      pairs to exclusive prepared output, then compacts rows and retires surplus
      chunks in bounded units. Real minimum-policy differential cases preserve
      retained inventory; all 2,048 keys / 4,096 rows and frozen-source isolation
      pass. ARM cursor 56 bytes, no store growth or take-sized scratch. Ordering/
      policy composition, preparation-role admission and production publication
      remain open. [Filter execution evidence](S4F18-FILTER-EXECUTION.md).
      **2026-09-28 preparation composition:** `RecordPreparationCursor` now composes the
      existing owners over one private prepared store: canonical (tick, Off < On < other)
      order, wrap repair with reserved synthetic Offs, canonical re-order, then
      minimum-length decisions and private compaction. Work is bounded per grain; identity
      gaps, stale generations and missing reservations deny by name. 12/12 focused cases
      and an eight-round differential against the legacy store helpers pass; four mutations
      fail assertions. Full native **2,571 passed / 7 skipped**; capture-serial build passes
      with RAM1 padding **840** and no linked ITCM growth (the role is not dispatched yet).
      ARM `Workspace` 40,960 B / cursor 1,104 B, caller-owned. Bounded missing-identity
      completion, resource admission and the shared worker/publication handoff remain open.
      [Composition execution evidence](S4F18-COMPOSITION-EXECUTION.md).
      **2026-09-28 bounded identity completion:** the private prepared store now has one
      fail-closed NoteId write, and the composition completes missing NoteOn identities in
      canonical order between the sort and wrap repair, matching `sealCapture`. A caller
      `NoteIdSource` supplies them; without one, or on an invalid/refused id, the cursor
      denies `IdentityIncomplete`. 17/17 composition cases (store-level overwrite/row
      matrix plus the canonical-order and differential cases) pass; three mutations fail
      assertions. Full native **2,576 passed / 7 skipped**; capture-serial build passes with
      RAM1 padding **840** and no linked ITCM growth. ARM cursor 1,104 -> 1,120 B. Resource
      admission and the shared worker/publication handoff remain open.
      [Identity execution evidence](S4F18-IDENTITY-EXECUTION.md).
      **2026-09-28 preparation-role admission packet + workspace union:** wrap repair and
      minimum-length filtering run in disjoint phases, so the composition `Workspace` is a
      union scratch role of **24,576 B** (was 40,960) pinned by a `static_assert`, and
      `FilterReset` re-discards the tail cursor. The admission slice is pinned in the
      [role-admission packet](S4F18-ROLE-ADMISSION-PACKET.md): one externally allocated
      25,696 B preparation role per track (8 total, 205,568 B), bound by job identity,
      driving one bounded grain and advancing only to the (future) publication phase.
      Packet only — no behavior edit; resource admission/publication remain open.
      **2026-09-28 bounded source staging (G0 packet correction):** reconnaissance falsified
      the packet's premise that the closure output store already holds the take: the closure
      consumer requires an empty store and writes only terminal Offs. The corrected sequence
      is (1) stage the frozen raw range into a private output, (2) merge the closure Offs,
      (3) the per-track role and normalization, (4) publication. Step 1 landed as
      `RecordSourceStaging::Cursor`: one row per bounded unit through the existing
      `projectRecordStopTick` policy (remap an overflowing NoteOn, drop other overflowing
      rows) under an ephemeral seal, never allocating and never growing the directory.
      7/7 focused cases including stale-source, credit-exhaustion and privacy-on-seal;
      two mutations fail assertions. Full native **2,583 passed / 7 skipped**; capture-serial
      build passes with RAM1 padding **840**. [Source-staging evidence](S4F18-SOURCE-STAGING-EXECUTION.md).
      **2026-09-28 closure merge and staging phase:** the frozen take is staged into the same
      private output *after* the terminal closure Offs (the composition orders the store, so the
      append order is not semantic), so the closure consumer's contract is unchanged.
      `PendingStopPhase::RecordSourceStaging` is appended to the enum; the Record closure step
      sets its own next phase in both completion places; `stepDeferredRecordSourceStaging`
      validates owner/generation/closure/raw-range, tops up credits, binds the per-track cursor
      under a receipt and drives one bounded grain to `CanonicalOrder`. The cursor and receipt
      live in the externally allocated ring, whose reviewed size pin is re-based 2984 -> 3072 B
      (+88 B per track; RAM1 padding stays 840). Focused shared-finalization **32/32** and the
      affected suites **166/166**; two mutations fail assertions. Full native **2,584 passed /
      7 skipped**; the full-suite gate caught a missing harness include in three
      firmware-boundary suites, which was fixed. [Closure-merge evidence](S4F18-CLOSURE-MERGE-EXECUTION.md).
      **2026-09-28 per-track preparation role and normalization step:** the admission slice
      landed. `RecordPreparation::Role` (composition cursor + 24,576 B union scratch, bound by
      job slot/LoopId/interval/generation) lives in the externally allocated per-track ring;
      `sizeof(CaptureStopJobs)` is re-based 3072 -> 28,808 B (8 x 25,736 B role = 205,888 B
      PSRAM, RAM1 padding unchanged at 840). `stepDeferredRecordNormalization` validates
      mode/phase/owner/geometry/runtime generation/closure, tops up credits, binds once and
      drives one bounded grain over the merged store, advancing to the new terminal
      `RecordPrepared`. The Record worker branch dispatches closure -> staging -> normalization
      -> prepared. Integrated fixture proves the pre-normalization order (Off, Off, On, On)
      becomes canonical (On, On, Off, Off) with identities preserved; mismatched-binding and
      terminal-phase denials leave the store untouched. Focused shared-finalization **33/33**,
      with preparation and staging **57/57**; two mutations fail assertions. Full native
      **2,585 passed / 7 skipped**; capture-serial build passes with RAM1 padding **840**.
      [Preparation-role evidence](S4F18-PREPARATION-ROLE-EXECUTION.md).
      **2026-09-28 bounded Record pass transfer (S4f.20 prerequisite):** the normalized private
      output is now staged into the job-owned transfer receipt through the same mode-neutral
      mechanism the overdub route uses (`beginPreparedCapturePassFromActiveAdmission` +
      `stepPreparedCapturePass` over `runtime->canonicalCaptureOutput`), with a new terminal
      `RecordPassReady`. The chain is
      `ReservedClosure -> RecordSourceStaging -> CanonicalOrder -> RecordPrepared -> RecordPassReady`.
      Nothing is published: the atomic publication transaction (pass + `RecordPassAdded`
      undo/geometry + audible-source handoff + once-only persistence + retirement) consumes that
      terminal and is S4f.20 completion. Focused shared-finalization **33/33**; mutations M1
      (ready phase) and M3 (receipt re-begun) fail assertions, while M2 (phase guard) is recorded
      as individually unobservable. Full native **2,585 passed / 7 skipped**; capture-serial
      build passes with RAM1 padding **840** and the ring pin unchanged.
      [Pass-transfer evidence](S4F18-RECORD-PASS-TRANSFER-EXECUTION.md).
      **2026-09-28 mode-correct Record atomic publication and retirement:** the deferred Record
      route now publishes end to end. `stepDeferredRecordPublication` performs the bounded
      canonical handoff with a local receipt and then the job-slot-keyed effects
      (`RecordPassAdded` undo entry, display staleness, once-only persistence) at a new
      `RecordPublished` phase; `stepDeferredRecordRetirement` releases the frozen extent in bounded
      grains and returns the ring entry. The chain is now
      `ReservedClosure -> RecordSourceStaging -> CanonicalOrder -> RecordPrepared -> RecordPassReady
      -> RecordPublished -> (retired)`. The inline side-effect owner was evaluated and rejected: it
      drags the note-edit surface in and two of its Record effects are not job-correct. Named
      residuals keep S4f.20 unchecked: (1) job-owned Record geometry, captured at the production
      Record bind site once S4f.17 record admission exists; (2) the deferred Record revts validation,
      which reads the active loop; (3) `invalidateCaches()` parity. Focused **68/68** (load 35 +
      shared-finalization 33); mutations M1 (undo entry), M2 (atomic handoff) and M3 (ring entry)
      fail assertions. Full native **2,585 passed / 7 skipped**; capture-serial build passes with
      RAM1 padding **840**. [Publication evidence](S4F20-RECORD-PUBLICATION-EXECUTION.md).
      **2026-09-28 job-owned Record geometry receipt:** `CaptureStopJob` now owns the interval's
      before/after `UndoLoopGeometry` and its LoopId, captured at the Record bind by
      `Track::captureRecordGeometryReceipt` (which also consumes the Track's inline baseline so a
      successor cannot rewrite it). `TrackUndo::pushRecordPassAdded` gained a job-owned overload,
      and the deferred publication now fails closed without a matching receipt and stamps the
      after geometry from the job's own loop. The reviewed ring pin is re-based 28808 -> 28936 B
      (+32 B per job). A fixture seeds a successor record baseline after the bind and proves the
      published entry still carries the job's geometry; two mutations fail assertions (reading the
      live baseline publishes the seeded value; a missing receipt denies). Focused **33/33**; full
      native **2,585 passed / 7 skipped**; capture-serial build passes with RAM1 padding **840**.
      [Geometry evidence](S4F20-RECORD-GEOMETRY-EXECUTION.md).
      S4f.20 remains unchecked for the deferred revts validation, `invalidateCaches()` parity and
      the production Record bind site (S4f.17). Next: S4f.21 caller migration.
      **2026-09-28 closure-plan Step 1 — preparation/publication boundaries repaired.** Six
      source-verified boundary defects from [S4F-CLOSURE-PLAN.md](S4F-CLOSURE-PLAN.md) §1 are
      closed with production-worker fixtures:
      (1) *seal scope* — `RecordSourceStaging` and `RecordPreparation` no longer hold the global
      ephemeral seal across pending turns; each private append enters and leaves it, so a pending
      cursor can never make another track's normal capture miss persistence admission. Fixture
      `test_private_grains_never_hold_the_global_ephemeral_seal` interleaves a second track's
      256-row capture between private grains: its sealed chunk is `Queued` (queue +1) while the
      private output chunks stay `NotScheduled` and `isEphemeralSeal()` is false after every grain.
      (2) *staging retry* — `RecordSourceStaging::Step::Pressure` makes credit exhaustion resumable
      with the source position and un-consumed row intact; the Track wrapper tops credits back up
      each grain and a fatal deny is sticky (no discard/re-bind that would duplicate the prefix).
      A 300-row/1-credit fixture pauses at exactly 256 copied rows and resumes to 300 exact rows.
      (3) *partial normalization retry* — `RecordPreparation::Step::Pressure` covers the synthetic
      Off append (the tail cursor repeats the same `EventReady`); a fatal deny no longer discards
      the role, so the private output can never be re-read from temporary filter markers. Fixtures
      prove exactly-once synthetic Offs under pressure and an unchanged marked store across
      repeated sticky denials.
      (4) *empty results* — new terminal `PendingStopPhase::RecordEmpty` and
      `stepDeferredRecordEmptyResult`: an all-dropped (projection) or all-filtered (minimum-length)
      Record take keeps the inline `CommitResult::Skipped` no-content semantics — no pass, no
      `RecordPassAdded` undo, no persistence, geometry reset only when the slot has no active pass —
      then retires terminally. Fixtures cover both shapes, assert a live successor on another slot
      is untouched, and prove the stop never stays pending.
      (5) *transfer continuation* — an owned pass transfer may continue after
      `stepCommittedChunkTransfer` empties the source store mid-reversal. `Loop::prewarmPreparedPassTransferChunks`
      sizes the prewarmed ownership row from the prepared take through the flash-resident
      `push_back` path (a runtime `reserve` instantiation cost 320 B of ITCM), so an accepted Record
      take is no longer capped at the fixed 8-chunk overdub row; a 24-chunk fixture observes the
      empty-store `Pending` state, completes the reversal and pins exact directory order.
      (6) *publication preflight* — `stepDeferredRecordPublication` validates the job-owned geometry
      receipt and `canTransferCapturePassWithoutAllocation` **before** the irreversible commit; a
      missing receipt denies with `ownsPendingPass` intact, no published pass, no undo and no
      persistence, then retries exactly once when restored.
      Focused shared-finalization **38/38**, preparation **18/18**, staging **7/7**; full native and
      capture-serial build re-run. Still open (S4f.17/S4f.21, unchanged): production Record
      admission/bind so the geometry receipt and the transfer-row capacity are established at the
      production bind rather than the first transfer grain.
      [Closure-plan Step 1 evidence](S4F18-CLOSURE-BOUNDARIES-EXECUTION.md).
      **2026-09-28 review follow-up — allocation refusal fails closed:** the transfer-row capacity
      admission (`Loop::prewarmPreparedPassTransferChunks`) grew the prewarmed ownership row with
      `std::vector::push_back` and checked its size afterwards; `std::vector` cannot report a null
      allocate, so `_M_realloc_insert` wrote through the refused block (ASan null-write). Growth now
      goes through the new flash-resident `LoopEventStore::prewarmCommittedChunkIdCapacity`, which
      probes the exact block the next growth will request immediately before the `push_back` that
      needs it and returns false with the row unmodified. A native-only refusal hook plus
      `test_record_pass_transfer_capacity_refusal_fails_closed` prove the denial leaves the
      job-owned state intact and that the same binding retries to `RecordPassReady`; red-proved by
      removing the probe guard. RAM1 padding unchanged at **840**. See the same
      [closure-boundaries evidence](S4F18-CLOSURE-BOUNDARIES-EXECUTION.md) § review follow-ups.
- [x] S4f.19 **First-bar and onward playback handoff.** Bind generation/geometry-correct retained coverage,
      preserve source borrowers through successor start, and prepare later windows from the owned frozen
      source until canonical publication. Verify incomplete readiness, sub-bar stop, remapped/held notes,
      playback beyond bar one and wrap before finalization completes, plus multiple sounding tracks.
      **2026-09-28 G0 complete — execution packet pinned, no behavior changed.** Read-only trace
      verdict: **PASS with named prerequisites**, not a small wiring change. The MIDI entry
      returns before `playCommittedLoopMidi` when `!hasCommittedPasses()`
      (`TrackPlaybackHotPath.cpp:478,495`), so a deferred-only interval emits nothing; the
      retained bar-1 window is unreachable (its adopt branch needs
      `isPlaybackSessionExitPublicationGap()`, false while unpublished) and nothing binds it on
      the deferred route (`bindFirstRecordingBar`'s only caller is the inline stop). The
      projected frozen-source fill (`RecordCoverage` + `fromCaptureRange`) has **no production
      caller**, no later-window builder exists for the frozen take, playback holds no borrow
      that would block `retireJobCaptureSourceGrain`, and Record-mode jobs are still
      test-only (S4f.17). Slice sequence S19.1–S19.5 (admission → bounded return-window bind in
      the worker → onward windows from the owned frozen source → ownership through
      successor/publication/retirement → acceptance evidence), resource accounting to record
      before the first production edit, and the open decisions with preferred paths are pinned in
      the [playback-handoff packet](S4F19-PLAYBACK-HANDOFF-PACKET.md). S4f.19 stays unchecked.
      **2026-09-28 S19.2 landed — job-qualified deferred return-window bind, transformed content.**
      `LoopPlaybackRuntime::bindDeferredRecordReturnWindow` binds the retained bar-1 window to a
      deferred job's sealed source generation and final geometry, and
      `RuntimePlaybackView::projectRawCoverageToLoopLength` **transforms** the raw bar first:
      an overflowing NoteOn is remapped into the loop, every other overflowing row is dropped, a
      note left open at the loop end gets one synthetic Off at `loopLengthTicks - 1` carrying its
      identity, the coverage is clamped to the prepared-bar/final-loop intersection, and the rows
      are re-ordered canonically (phase, NoteOff < NoteOn < other). `Track::stepDeferredRecordClosure`
      calls it once per grain; a refusal never fails the job. `hasBoundRecordReturnWindowForPendingJob`
      is the eligibility predicate the MIDI entry will use (pending owner **and** bound window).
      Fixtures verify transformed content (sub-bar take: NoteOn 600 → 100, CC 700 dropped, two notes
      closed at 499, exact canonical sequence), stale-generation and unsealed-range refusals, and
      the eligibility predicate; two mutations (remove projection, remove closure) fail assertions.
      No new view role, job byte or stored cursor: one FLASH cold function plus a 256-byte stack
      lane bitmap, work bounded by the view role capacity. **S19.1 (MIDI-entry admission) is not yet
      wired** — it is held until a native fixture can observe emission (the harness has no MIDI send
      capture). Onward windows and the continuity-gated retirement (matching committed replacement
      handed off, all builders detached, playback no longer needs the range) remain open, and
      production Record admission must not be enabled before they land.
      **2026-09-28 round 2 — refusal atomicity, send-capture boundary, admission predicate.**
      (a) `projectRawCoverageToLoopLength` now preflights the held-note closure count with a
      non-mutating pass (same drop rule and lane logic) and returns `false` with the window
      byte-identical when the appends cannot fit; fixture
      `test_deferred_record_projection_refusal_leaves_the_window_untouched` fills the view role to
      capacity with held notes and compares every row plus the coverage before/after, twice;
      mutation (remove the preflight) fails it. (b) `MidiSendCapture` is a native-only boundary
      that records every event reaching `MidiHandler::sendMidiEvent`. (c)
      `Track::hasAudiblePlaybackSource(slot)` is the shared admission predicate for both playback
      entry points (committed passes, or a pending job's bound bar-1 window); fixture
      `test_deferred_record_interval_is_audible_only_with_a_bound_window` drives the real record
      start and the deferred closure grain and asserts not-audible (raw window) → audible (bound)
      → not-audible (job retired), plus another slot. (d) A hot-path prerequisite was found:
      `playCommittedLoopMidi`'s stale-runtime branch calls `reset(true)`, which refuses while a
      pending job owns the window, and returns — so the interval cannot be served; recorded for
      the emission slice, not changed here because its assertions are not yet green. The
      **emission assertions remain the immediate next step** (expected NoteOn/NoteOff sequence,
      stale/unready emits nothing, publication/replacement boundary, note crossing bar one). The
      new admission predicate call site needed 16 B of ITCM, reclaimed by moving the idle
      diagnostic `Track::notePlayingMidiDrainAfterOverdubStopIdle` to FLASH; RAM1 padding is
      **888** (floor 840). Capture-serial build PASS.
      **2026-09-28 round 3 — emission through the production entry point.** Fixed the hot-path
      prerequisite: `playCommittedLoopMidi`'s stale-runtime branch called `reset(true)`, which
      refuses while a pending job owns the bound window, and returned — the interval could never
      be served. The reset is now skipped for that owner and the revision synced. With the native
      `MidiSendCapture` boundary, `test_shared_capture_finalization` now proves: a direct
      `sendMidiEvent` records; a pending job with a **raw** window emits nothing; the **active**
      entry point emits exactly one NoteOn and one NoteOff from the bound window (trace in the
      failure message); the **layered** entry emits nothing (its `isActive` is target-based, so an
      unpublished interval has no fixed view there); retiring the job ends emission and another
      slot emits nothing. Still owed: publication/replacement handoff must neither repeat nor omit
      boundary events, and a note crossing the first-bar boundary must close at its actual
      endpoint — currently the window is truncated at one bar, so the projection closes such a
      note at `loopLengthTicks - 1`; the actual Off lives in the owned frozen source, so that check
      depends on the onward-window slice and the closure is a known approximation until then.
      Focused **241/241**; full native **2,598 passed / 7 skipped**; capture-serial PASS, padding
      **888**.
      **2026-09-28 round 4 — publication/replacement parity.** `test_published_record_interval_does_not_replay_the_return_window`
      drives the whole deferred chain (closure → staging → normalization → transfer → publication),
      asserts the pass is committed, then emits through the active entry point and asserts exactly
      one NoteOn and one NoteOff: the already-served window events are not replayed and the
      committed boundary is not omitted. The bound window survives publication and remains the
      served source because no matching replacement *view* has been handed off yet, which is the
      contract's retain-until-replaced rule. Focused **242/242**; full native **2,599 passed /
      7 skipped**; capture-serial PASS. Remaining: the crossing-note actual endpoint (needs the
      owned frozen source — the window is one bar, so the projection currently closes such a note
      at `loopLengthTicks - 1`), onward windows from the owned frozen source, and the
      continuity-gated retirement plus ownership through successor start.
      **2026-09-28 round 5 — onward window primitive from the owned frozen source.**
      `LoopPlaybackRuntime::serviceFrozenRecordWindow` builds the window containing the playhead
      from the job's frozen `CaptureRange` through the existing fill/order owners and the
      `RecordCoverage` projection: one bounded grain per call, reusing the `build` role and
      publishing into `ready` for the normal handoff, refusing a stale/unready range or an
      out-of-loop phase. Fixtures pin the bar containing phase 800 as window `startPhase 768 /
      length 768` over a 1536-tick loop with exact projected content (only the two bar-2 rows,
      canonically ordered) and all three refusals; the `RecordCoverage`→`AllSource` mutation
      retains all four rows and fails. Focused **244/244**; full native **2,601 passed /
      7 skipped**; capture-serial PASS, padding **888** (the service is FLASH-resident). Remaining:
      the hot path must pass the job's `rawRange` to this service and promote the ready window
      through the handoff; then the crossing-note endpoint, continuity-gated retirement and
      ownership through successor start.
      **2026-09-28 round 6 — onward window served through the hot path.** `adoptFrozenRecordWindow`
      promotes a completed `ready` build through the audible handoff and reuses an already-serving
      onward window across turns (excluding the bar-1 return window) so a steady playhead does not
      rebuild. `Track::playCommittedLoopMidi` now, when the bar-1 window no longer covers the
      phase and a pending Record job owns the frozen range, adopts the onward window or advances
      one build grain from `job->rawRange` with `RecordCoverage` — the whole service is a
      FLASH-resident `Track` helper so the MIDI edge keeps only the call. Fixture
      `test_deferred_record_interval_serves_beyond_bar_one_from_the_frozen_source`: a two-bar take
      with a note in each bar; phase 90 is served by the bound bar-1 window, and past bar one the
      onward window from the frozen source serves the bar-2 note exactly once while the pass is
      still unpublished. Focused **245/245**; full native **2,602 passed / 7 skipped**;
      capture-serial PASS, padding **888** (one legacy note-edit accessor moved to FLASH to keep
      the new call site inside the floor). Remaining: the crossing-note actual endpoint,
      continuity-gated retirement and ownership through successor start.
      **2026-09-28 round 7 — crossing-note endpoint.** `projectRawCoverageToLoopLength` and
      `bindDeferredRecordReturnWindow` take an optional `openAtStopLanes` bitmap (16 x 128 bits):
      the projection closes a lane left open at the window end **only** when it was open at the
      stop, so a note whose real Off lies further inside the take stays open for the onward window
      to end at its actual tick instead of being cut at `loopLengthTicks - 1`.
      `Track::stepDeferredRecordClosure` builds the bitmap from `job.pendingNotes` (the
      authoritative open-at-stop set at that grain). Fixture
      `test_projection_closes_only_lanes_open_at_the_stop`: with the bitmap only the open-at-stop
      lane closes at 767 and the crossing lane stays open; with no bitmap both close, so the gate
      is discriminated by construction. Focused **269/269**; full native **2,603 passed /
      7 skipped**; capture-serial PASS, padding **888**. Remaining: continuity-gated retirement
      and ownership through successor start.
      **2026-09-28 round 8 — continuity-gated retirement.** `stepDeferredRecordRetirement` now
      refuses (`Pending`) while a frozen-window build is in flight, because that build still
      borrows the job's raw range, and then calls `LoopPlaybackRuntime::releaseFrozenRecordWindow`
      to drop the range-derived onward window (and any active build) before the first release
      grain. The bound bar-1 return window is deliberately kept: it owns copies and is served
      until a replacement *view* is handed off. Fixture
      `test_published_record_interval_does_not_replay_the_return_window` now starts an onward
      build, asserts retirement returns `Pending` with the ring depth unchanged and the phase still
      `RecordPublished`, then drains the build and completes retirement with the ring empty.
      Mutation (remove the build-active gate) retires while the build is in flight and fails the
      ring-depth assertion. Focused **269/269**; full native **2,603 passed / 7 skipped**;
      capture-serial PASS, padding **888**. Remaining: ownership through successor start, plus the
      wrap-while-finalizing, multiple-sounding-tracks and stale-source-replacement fixtures.
      **2026-09-28 round 9 — wrap while finalizing.** The eligibility predicate
      `hasBoundRecordReturnWindowForPendingJob` now reports **ownership** (a pending job owns the
      interval) rather than "the bar-1 window is current". Once an onward window is handed off,
      the bar-1 window is no longer current, so the old predicate went false and a later bar --
      including a wrap back to bar one -- was never built: the interval fell silent. Because
      ownership alone is not audibility, a new `hasServableDeferredRecordView` (bound bar-1 window
      or a range-derived onward window) is what `Track::hasAudiblePlaybackSource` uses. Fixture:
      the beyond-bar-one case now also drives past the loop end and asserts the bar-1 note is
      served again after the wrap while the pass is still unpublished. Focused **269/269**; full
      native **2,603 passed / 7 skipped**; capture-serial PASS, padding **888**. Remaining:
      **2026-09-28 round 10 — successor start: ownership holds, audibility does not (recorded).**
      A fixture starting a successor record on slot 1 while slot 0's deferred interval is served
      shows the predecessor is not audible during the successor's recording: the active entry's
      `fixedViewEligible` requires `isPlaying()`, and a successor record puts the track in
      `TRACK_RECORDING`; the layered entry is target-based and needs a committed bridge. Ownership
      is unaffected (per-slot runtimes keep the window and frozen range independent), but closing
      the audibility gap needs the layered path to serve a range-derived window for a non-active
      slot, or an explicit decision that the predecessor sounds only after the successor's capture
      ends. The failing fixture was withdrawn, not committed; the finding is recorded in the
      [playback-handoff packet](S4F19-PLAYBACK-HANDOFF-PACKET.md) § successor start. Remaining:
      **2026-09-28 round 11 — stale source replacement verified; multiple-tracks boundary named.**
      The frozen-window refusal fixture now also proves **stale source replacement**: after a
      stale-generation refusal the same service builds the window from the current source, so a
      stale attempt never blocks the replacement that follows. A **multiple sounding tracks**
      fixture (two tracks, each with an inline-committed take, emitting through their active
      entries) emitted nothing for committed content: the committed-bridge/fixed-view emission
      path is task 2.4's subsystem, not S4f.19's. The failing fixture was withdrawn rather than
      committed. S4f.19's own multi-track property — per-track runtimes keeping the bound window
      and frozen range independent — is already pinned by the eligibility and retirement
      fixtures. Remaining: the successor-audibility decision, and multi-track audibility once the
      **2026-09-28 round 13 — successor source preservation pinned (ownership, not audibility).**
      Re-reading the contract, "preserve source borrowers through successor start" is an
      **ownership** requirement: B's start must freeze/rebind to the job-owned source rather than
      invalidate it. The design deliberately suppresses playback emission while the track is
      RECORDING (`isAudiblePlaybackActive` excludes it), so the predecessor is not expected to
      sound during B's capture — only its source must survive. Fixture: while a successor record
      runs on slot 1, slot 0's `ownsRawRange` and `rawRange.ready()` still hold, the job mode is
      still Record, the servable view survives, no frozen build is in flight, and the stop ring
      still holds the predecessor. This supersedes the round-10 "audibility blocker" reading: the
      remaining gap is multi-track *audibility* only, which is task 2.4's committed-bridge path.
      **2026-09-28 round 13b — multiple sounding tracks (S4f.19 case) verified.** Fixture
      `test_two_tracks_each_serve_their_own_deferred_window`: two tracks each own an unpublished
      deferred interval and each serve their own bound window through their own active entry,
      emitting their own note exactly once. That is S4f.19's multi-track property; the
      committed-bridge multi-track case belongs to task 2.4. With this, every S4f.19 exit fixture
      is landed: incomplete preparation, sub-bar stop, remapped notes, held notes, traversal
      beyond bar one, wrap while finalizing, multiple sounding tracks and stale source
      replacement, plus bind-to-job-geometry, onward windows from the owned frozen source,
      ownership through successor/publication/retirement, and discriminating mutations.
      Focused **234/234** (playback-prewarm + shared-finalization); full native **2,604 passed /
      7 skipped**; capture-serial PASS, padding **888**.
- [x] S4f.20 **Mode-correct atomic publication and retirement.** Prepare record undo/geometry and slot-history
      pruning in the existing protected transaction; publish pass/undo/source consistently, attach snapshots
      to the correct entry and schedule persistence once by job identity. Verify pre/post-commit undo/redo,
      note edit, failed preparation, stale cleanup, replacement/load/clear/save, exact persisted content and
      bounded retirement. Preserve overdub session undo; do not add a new undo kind.
      **2026-09-27 handoff slice:** successful deferred atomic transfers request dirty/persist/save
      for their validated job slot once; stale Loop identity cannot advance publication. Production
      fixtures cover Playing, Stopped, superseded source and stale/recovered identity with a live
      successor and different selected track. Record undo/publication and device/durability gates
      remain open. Full native **2,501 passed / 7 skipped**, focused **15/15**; capture-serial
      passes with padding **840 bytes**. Strict OD→OD now passes; three Record cases remain red.
      [Execution packet](S4F20-PERSISTENCE-EXECUTION.md).
      **2026-09-28 preparation completion:** completed undo estimates now retire after
      consumed collection and prepared protected publication. Denied collection keeps
      its obligation; refused publication retains/retries the entry. Dense production
      and denial/recovery assertions pass; [staging execution evidence](S4F18-STAGING-EXECUTION.md).
      **2026-09-29 parity gap — the deferred route must queue its own session-exit publication.** The
      inline Record stop queued the interval's session-exit snapshot publication at the end of
      `Loop::commitPendingCapturePass`, and that queue is the only enumeration of the *committed Record
      pass* into the pending box. The caller migration left it behind: the deferred publication mirrored
      undo/display/scratch/persistence but never queued it, so a mid-bar take's box published the retained
      live-record candidate (content stops at the completed-bar frontier) with its declared length handed
      off to the sealed length, and a successor's exit stamped the root current — the "issue 2" device
      symptom (Record-pass last bar missing from the display, restored by an overdub undo). Fixed in
      `Track::stepDeferredRecordPublication` with the mode-correct reason; verified by
      `test_deferred_record_publication_queues_the_record_session_exit_snapshot` (red-proved by removing
      the call) and the Loop-level `test_record_session_exit_with_a_truncated_candidate_keeps_the_take_tail`
      and `test_record_session_exit_ignores_a_pending_overdub_receipt` (a session-exit receipt belongs to
      an *overdub* session, so `queueSessionExitSnapshot` now reads it only for an overdub exit -- a Record
      exit always enumerates its own just-committed pass; that selection is red-proved by restoring the
      unfiltered read).
      Focused **198/198**, full native **2 636 cases / 2 629 succeeded / 7 skipped**, `teensy41-capture-serial`
      **SUCCESS** with RAM1 padding **1,928** (floor 840). Decision **DEC-094**; residual (the DEC-073
      coverage watermark is written without a completeness check) is recorded, not fixed.
      [Plan](../../../docs/Plans/record_session_exit_snapshot_queue_deferred_publication_bugfix.md),
      [packet](S4F17-CUTOVER-PACKET.md).

- [x] S4f.21 **Migrate all record-stop continuations.** Convert stopRecordingTrack, finalizeCaptureAndSelectSlot,
      stopAllTracks and direct stop entry points to identity-qualified execution/geometry results; remove
      synchronous full-record fallback and caller-side duplicate save. Guard same-slot destructive start;
      retain different-slot successors. Verify master length/auto-align/rewind once, both destinations,
      transport Stop in each phase, internal/external clock, mixed starts and common in-flight guards.
      **2026-09-29 S17.3a — same-slot/different-slot switch guard: done.** `finalizeCaptureAndSelectSlot`'s
      Record branch now mirrors the overdub branch's slot-scoped rule: an **admitted** Record job carries its
      own identity and does not hold the active slot, so the switch commits and the successor is not
      serialized behind predecessor cleanup; an **accepted-but-unbound** Record stop refuses the switch and
      leaves the active slot unchanged. Without this, S17.2's deferred stop let the switch advance
      `captureInterval_` past the retained interval, after which `driveAcceptedInteraction` judged the
      obligation superseded and dropped the take — silent content loss on a switch-while-finalizing
      handoff. Fixture `test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one`
      (refusal + unchanged active slot + retained obligation + post-bind commit + predecessor still
      finalizing + its pass still committed); the guard-removal mutation fails it. Full native 2 613 cases /
      2 606 succeeded / 7 skipped; capture-serial build PASS, RAM1 padding 1 976.
      **2026-09-29 S17.3b — job-qualified geometry result: done.** `Track::recordStopGeometryForSlot`
      reports `Final` (no stop in flight for that slot; the length is authoritative) or `PendingStop`
      (an admitted job or a retained obligation still owns it). It is mode-neutral and slot-scoped.
      `stopRecordingTrack`, `finalizeCaptureAndSelectSlot`'s Record branch and `handleTransportStop`
      (there is no separate `stopAllTracks` symbol) adopt the geometry once, only when final; until
      then they retain a per-track obligation consumed by `servicePendingRecordStopGeometry` at the
      next dispatch boundary, and auto-align uses the new slot-qualified `Track::setLoopLengthForSlot`
      because the obligation's slot may no longer be active. This is a real fix, not tidying: the stop
      edge stamps the *predicted* length, so the old read adopted a `masterLoopLength` from a take the
      minimum-length policy then deleted. Fixtures
      `test_record_stop_adopts_published_geometry_not_the_stop_edge_prediction` and
      `test_filtered_record_take_publishes_no_master_length`; reverting the transport continuation to
      the stop-edge read fails both. Full native 2 613 cases / 2 606 succeeded / 7 skipped;
      capture-serial build PASS. `scripts/ram1_budget.json` re-based deliberately (+32 B DTCM for the
      obligation, offset by moving the continuation to FLASH: padding 1,976 -> 1,992); reason recorded
      in the packet. No admission/publication behaviour changed by this sub-slice.
      **2026-09-29 S17.4 — S4f.20 residuals: done.** The deferred Record publication now queues the
      stored note-on (REVT) scan the inline `finalizeCommitSideEffects` queued on commit, and the
      queue is **slot-qualified**: `deferredRecordRevtsPending` (bool) became
      `deferredRecordRevtsSlot` (same byte, the interval's slot or `INVALID_LOOP_SLOT`),
      `queueDeferredRecordRevts(slot)` names the publishing interval, and `processDeferredRecordRevts`
      scans that loop instead of `getActiveLoop()` -- previously the side effect was missing on the
      deferred route entirely, and an active-slot read would have scanned a successor take.
      `invalidateCaches()` parity is proven rather than assumed: `notifyCommittedContentChanged`
      (`markPassDerivedStale`) already covers the Loop's playbackOrder/visual/noteCache/eventIndex
      invalidation, so the only gap was the Track-level work -- `CommittedMidiScratch::invalidate`,
      the note-edit selected-slot invalidation and the REVT queue -- now applied by
      `Track::invalidateCommittedContentForSlot(slot, queueRecordRevts)` against the job's slot.
      Fixtures `test_deferred_record_publication_queues_revts_for_its_own_slot` and
      `test_deferred_record_publication_invalidates_the_track_committed_scratch`; removing either
      wiring fails its fixture. Strict matrix 47/47; full native 2 617 cases / 2 610 succeeded /
      7 skipped; capture-serial PASS, RAM1 unchanged at padding 1,992.
      **2026-09-29 S17.5 — gates, mutations and accounting (native leg): done.** The finalization
      suite is an enforced gate (it no longer reads `S4F16_REQUIRE_CONTRACT`, so ordinary suite
      success is the strict result). Nine mutations are red-proven, including the three the packet
      names (inline Record commit reintroduced; stop-edge prediction read instead of the published
      geometry; retirement while a frozen-window build still borrows the raw range). Structural
      accounting is compile-time and active (`sizeof(Track) == 384`, `sizeof(CaptureStopJobs) ==
      28936` under SESSION_CAPTURE+PERF_TELEMETRY) plus the post-link RAM1 gate. Strict 47/47; full
      native 2 617 cases / 2 610 succeeded / 7 skipped; capture-serial SUCCESS.
      **2026-09-29 S17.5a — transport stop during a Record finalization.** The retarget is now
      mode-aware (`requestDeferredOverdubStopToStopped` returns after the destination change for a
      Record job rather than running the overdub recovery against roles a Record job never
      populated), and "transport stop wins" is resolved at that edge (`TRACK_STOPPED` + silence +
      playback-state reset); previously the track stayed TRACK_PLAYING while the job's destination
      said STOPPED. The playback-runtime reset path deliberately does not force the transport state,
      pinned by the playing-inventory fixture. Fixture
      `test_transport_retarget_of_a_record_job_resolves_stopped`; removing the resolution fails it.
      **§3 exit breadth.** Transport Stop is covered across the deferred stop's phases by
      `test_continuous_capture` (canonical-copy, mid-flight playing retarget, live successor, B03
      retarget, accept-then-transport-stop) plus this change's Record retarget fixture, and
      clock-source classification by `test_clock_transport_master`.
      **2026-09-29 S17.5b — load/clear underneath a finalizing stop: done (two real defects found).**
      An admitted job whose interval identity was gone used to park forever on the deferred-grain
      prologue's LoopId guard, holding its raw range, its capture-stop owner and its ring entry so no
      later stop on that track could be admitted; `supersedeDeferredStopJob` now applies the S4f.4
      transition-3 supersede rule to a job that *did* bind, releasing everything it owns (frozen
      window, staging role, note-change and preview borrows, owned pass chunks, private canonical
      output and the sealed extent one grain per turn) without touching the live interval. And
      `Track::clear()` now supersedes a job that owns an unpublished take for the slot instead of
      letting the worker publish the cleared take back into the slot the user emptied — an explicit
      supersede, not a LoopId bump, because clear preserves identity by design. Fixtures
      `test_replaced_loop_during_a_record_finalization_retires_the_job` (the load / re-record shape)
      and `test_slot_clear_during_a_record_finalization_does_not_republish`, plus the overdub
      counterpart renamed to `test_stale_publication_is_superseded_without_persistence` (it previously
      encoded parking as the expected behaviour).
      **2026-09-29 S17.5c — the transport-not-running leg: done.** The clock source reaches this path
      through `isTransportRunning()` (false both for a stopped internal transport and a quiet external
      clock). `test_retained_record_stop_admits_stopped_once_the_transport_is_not_running` retains a
      PLAYING-destination Record stop behind another interval's job, stops the transport before the
      drive, and pins DEC-078 item 3: the stop admits STOPPED, the take still publishes, and the
      transport resolves `TRACK_STOPPED`; replacing the re-derivation with the stored destination
      fails it. §3's exit list now has no unexercised item this environment can reach (the clock
      *classification* is covered by `test_clock_transport_master`). No device acceptance is claimed
      from host results.
<!-- 2026-09-29: Record-evidence accounting slice is under verification in
S4F17-CUTOVER-PACKET.md. The previous attribution to critical reentry/release
traffic was disproven by the real-ring fixture: those diagnostics are Tier-A,
not critical. Device loss attribution and integration acceptance remain open. -->
<!-- Same-session drain follow-up: managed baseline now retrieves queued Record
rows before MIDI/reader close, using stopped-only fences and counter/log deltas.
Device-exercised read-only 2026-09-29 (captures/s4f22-base-device-17:
record_evidence_drain.complete=true, RECA 1 / RECS 7 / RID 1 reconciled). -->
<!-- 2026-09-29: run-08 Record retarget defect reproduced and fixed: cache
invalidation preserves a Record job's Playing destination/output barrier.
Focused 156/156; native 2,619 passed / 7 skipped; capture-serial build PASS.
Hardware confirmation awaits a recorded run of this candidate image; clear
authorization for every slot on every track was granted 2026-09-29. -->
<!-- 2026-09-29 evidence reliability (step 2, host-verified, no device claim):
REVT drains during playback via the bounded chunk walk (32/turn; merge and
stored-MIDI verification stay idle-only); the ring reserves 32 KiB of Record
rows against displacement by ordinary traffic or another take; a missing
telemetry row is INCONCLUSIVE/EVIDENCE_MISSING instead of a firmware
contradiction, while a missing note event, a length mismatch and short
span/count/bar sets stay contradictions. Native 2,628 / 2,621 passed /
7 skipped; capture-serial SUCCESS, padding 2,072; both new fixtures
mutation-red-proved. Bind evidence and the setup-precondition receipt are the
remaining host-side items before the device rerun. -->
<!-- 2026-09-29 device evidence for S4f.22/S4f.13 (no device checkoff claimed).
`s4f22-base-device-17`, `s4f22-step3-base-02`, `s4f22-step5-b1`, `s4f22-step6-c1`: **PASS** with
complete capture integrity, all five transition checks green, drain reconciled (RECA 1 / RECS 7 /
RID 1) and 32 `REVT` rows once the queued scan's chunk directory was pinned (it borrowed ids the
producer then freed, so it read nothing: `Queued REVT note-ons (deferred): 0`). The playing REVT
slice drains through the bounded chunk walk; merge and stored-MIDI verification stay idle-only.
Record-family rows are unevictable by ordinary traffic (32 KiB reserved slice) and the ring reports
its own losses (`RING,head_bad`, `RING,wiped`, `RING,stats`, `RING,overlap`).
Not accepted: repeat attempts on one boot still fail, and the failures are now attributed. Two
harness defects are fixed (transport toggle in the clock helper; a successor press without its stop
receipt that created a phantom take scored as the record leg), and the record-leg pairing/selection
is fixed so a later take cannot be scored. **Open:** the ring lost 384 571 bytes to zeros with
self-consistent bookkeeping (`s4f22-step8-e3`) -- pool-over-ring overlap is armed for the next
campaign; and the same-boot qualification campaign (six bundles, three consecutive passes per leg)
has not run to completion.
[Cutover packet](S4F17-CUTOVER-PACKET.md). -->
- [ ] S4f.22 **Shared finalization integration gates.** Execute the contract matrix and mutation proofs,
      full native, capture-serial build and exact role/RAM accounting (RAM1 padding >=840 bytes); record
      all findings. Run response-driven identified device R→R, OD→OD and mixed record-A → overdub-B with
      natural overlap, continuous transport, one press, content/undo/persistence parity and existing timing
      gates. Missing overlap is NOT_EXERCISED. Link evidence to S4f.13; do not close from host results.

- [~] S4f.13 **HITL — record/overdub stop → slot switch → record/overdub stop (the accepted-sequence reproducer).**
      **Resolved direction (2026-09-27):** the record-overlap requirement already exists and is retained.
      Implement S4f.16–S4f.22 under the shared-finalization contract before expecting this gate to pass.
      The separate weaker record oracle proposed during HITL work is not adopted. Response-driven HITL
      remains required for both modes and the mixed record-A → overdub-B acceptance.
      **2026-09-27 implementation finding:** response-driven fixture tasks 1.2/1.3 verified (full native
      2,486 passed / 7 skipped; Python 33). Its shared overlap oracle is blocked: record-stop publishes
      inline, while the specified deferred capture-stop window belongs to overdub. User contract decision
      pending; no acceptance criterion waived. See the sibling change's `EXECUTION-REVIEW.md`.
      **2026-09-27 next-step contract:** [response-driven-hitl-acceptance](../response-driven-hitl-acceptance/tasks.md)
      owns the harness prerequisite. Use DROID-equivalent capture-switch gestures with transport continuously
      playing, distinct record/overdub fixtures, response-correlated identities, and same-boot scoped preparation.
      No manual reference pass. Historical fresh-boot and literal zero-denial-log checks below are replaced by
      fresh run/boot identity and proof of no rejected/lost interaction: DEC-078 — accept-and-latch requires
      distinguishing a matched accepted obligation from compatibility denial vocabulary. A real rejection
      still fails. Existing content/timing obligations remain; no task checkoff from host-only tests.
      **2026-09-27 host correction (supersedes the carrier/SD-only diagnosis below):** the capture-switch
      branch runs BEFORE the SD-aware playback branch, but only while capturing. Reuse the proven retention
      prefix: B’s loop press is A’s stop plus switch; B is seeded for overdub, and gets a separate start.
      No empty C record is substituted for B’s overdub. Driver aborts on unexpected playback queue without
      another press, preserves report/shutdown and nonzero exit; confirms starts and seed commits and sends
      notes into both overdubs. Accepted-sequence tests 30, retention plan 23, shutdown 15; old guard
      red-proved; OpenSpec strict valid. Oracle rejects wrong-slot/late starts and window-as-release;
      overdub identity and input-press counts are explicitly unknown with record-only evidence. No device
      run or task checkoff. See ARCHITECTURE-REVIEW.md § “S4f.13 — accepted-sequence driver correction”.
      **REPRODUCER BUILT 2026-09-27; the first device leg ran and did NOT pass — the oracle found two real
      gaps rather than a staging problem.** The shape is now executable and its assertions are testable
      without a device:
      * `scripts/hitl_accepted_sequence.py` drives one leg (a = record, b = overdub); the gesture plan is
        **data** (`leg_gestures`) so the order — which is the requirement — is unit-tested, and the presses go
        through the real per-slot action layer;
      * `scripts/hitl/accepted_sequence_probe.py` is the oracle: an ordered chain L1 A's stop accepted once →
        L2 no capacity denial → L3 A's finalization window → L4 A's release → L5 a successor start **after**
        A's stop → L6 B's stop executes once → L7 one press per stop, plus `overflow_free`/fresh-boot/identity
        setup gates, with `SETUP_INCOMPLETE` and `INSUFFICIENT_EVIDENCE` kept apart from `ACCEPTED_SEQUENCE_FAIL`;
      * `scripts/test_accepted_sequence.py` — **17/17 green**, pinning both leg orders, the one-press-per-stop
        property, the wire notes against the firmware's own mapping, the base conversion, and **one negative per
        way to lose a link** (missing window, a capacity denial with every other link intact, a second stop,
        ring loss, missing boot banner, missing successor start, missing metadata).
      Stimulus provenance per AGENTS.md: both legs are **TEMP variants** forked with the sanctioned
      `stimulus temp` mechanism — `STIM-BASE-2-ONE-OD-01-TEMP-ACCEPTED-SEQ-REC` and `…-OD` under
      `captures/stimuli/`, deliberately **not registered**.
      **First device run** (`captures/s4f13-od-20260927_015145`, leg b, track 8, A=4, B=5, fresh boot, shutdown
      `complete`): `SETUP_INCOMPLETE`, and the oracle's two failures are the useful part —
      1. **`overflow_free` FAIL: a `RING,shed,1` row at boot.** The identity/telemetry shed is present even on a
         fresh upload, so a shed row must be distinguished from an *eviction* before this gate can be a pass
         condition; recorded rather than relaxed.
      2. **`L5_successor_started` FAIL: nothing started after A's stop.** The leg's own log shows why — A's
         overdub opened, `Loop 4: Stop Overdub` opened A's finalization window, and the switch logged
         `Loop 5: Queued playback switch` and never executed: no `RECA` for B, no B stop. So the **leg plan is
         wrong, not the firmware**: the `od` leg never gets B started (it presses the record button while the
         queued switch has not run, so the gesture acts on A), and its B target is empty, so an *overdub* there
         has no loop to open onto. The next attempt must (a) settle B before the leg, and (b) drive B's start
         through the switch-carried start rather than a bare record press.
      **Not checked off:** the acceptance this task exists to prove has not been observed on device, and the
      leg needs the two corrections above before it can be.
      **BUG FOUND AND FIXED IN THE DRIVER 2026-09-27 (reported by the owner, "it counts down, then counts down
      again"):** the leg pressed slot 5 **twice inside the firmware's ~300 ms tap window** — `b_switch` at
      26.158 s and `b_stop` at 26.503 s, with the record press interleaved — and the device logged
      `Loop 5: Queued playback switch` → `Loop 5: Reaffirmed playback switch` → `Loop 5: Reaffirmed playback
      switch`, i.e. **two countdowns for one switch**. Two causes, both now pinned:
      * **driver (fixed):** `hitl_accepted_sequence.py` now spaces loop-button presses by at least the firmware
        tap window and **waits for a queued switch to execute** (a `Playback started` row) before pressing
        again. `_switch_pending` is the predicate; the double-press was a timing artifact of the driver, not a
        user gesture, and a test that pressed again inside the window was testing the merge, not the sequence.
      * **firmware (recorded, not fixed):** `queuePlayingSlotSwitch` calls `clearPendingSlotSwitch()` and then
        `requestPendingSlotSwitch()`, which overwrites `pendingQueuedAtTick` **unconditionally**. A repeat on
        the *same* target while one is already pending therefore **restarts the quantized countdown** instead
        of being a no-op, and the `Reaffirmed playback switch` row is emitted for it. The comment above the
        call says the short-press queue is "always a replace", which is right for a *different* target; nothing
        distinguishes that from re-affirming the same one. Whether that should be idempotent is a behaviour
        decision owing its own change, so it is recorded here rather than changed under a fixture task.
      **Regression coverage:** `test_accepted_sequence.py` now has a `SwitchRepeatGuardTests` class (5 cases:
      a queued-but-unexecuted switch reads as pending, the *reaffirm* row also reads as pending, an executed
      switch frees the buttons, another slot's switch does not hold this one, and the tap window is at least
      the firmware's). Suite 22/22.
      **SECOND AND THIRD RETESTS 2026-09-27 — two defects fixed, one acceptance link still open.** Suite now
      **23/23.**
      * **The upload was not retried, and its failure was raised instead of reported (fixed).** `teensy-gui`
        intermittently fails with *"Teensy Loader is unable to read your compiled sketch (r)"* and then
        succeeds on the very next attempt (measured: a retest aborted on the first try, and a direct
        `pio run -t upload` immediately afterwards succeeded). The driver now retries the upload
        (`--upload-attempts`, default 3), records each attempt's return code, and returns
        **`UPLOAD_FAILED` (exit 5)** with a written report rather than raising a traceback.
      * **B must be seeded before the leg — and that is structural, not symmetry (fixed).** With an **empty
        selected slot** behind a pending switch, a record press never reaches the overdub stop:
        `handleToggleRecord` sees `!hasCommittedPassesInSlot(selectedSlot)` and routes to the per-slot handler,
        which **reaffirms the pending switch and emits another `Reaffirmed playback switch`** — a second
        countdown. The driver now seeds **both** slots (B first, then A, leaving A active/selected), and the
        double countdown is **gone: `Reaffirmed` count 0** in the latest run, against 2 before.
      * **The one open link is L5 — nothing succeeds A.** In the latest run (`captures/s4f13-od4-…`) A's
        overdub opens (`Overdub session opened`), `Loop 4: Stop Overdub` opens A's window, A retires
        (`Reentry released slot=3`), then `Loop 5: Queued playback switch` — and the switch **still does not
        execute**, so the subsequent record press opens a *second overdub session on the same active slot*
        rather than starting B. L1, L2, L3, L4, L6 and L7 all pass; there is no `RECA,4` at all. **The next
        step is to drive B's start through the switch-carried start** (the mechanism S4f.3/S4f.6 built for
        exactly this) instead of expecting a queued switch plus a record press to compose.
      * **Setup-gate finding, recorded rather than relaxed:** `overflow_free` FAILs on **every** fresh-boot run
        with a `RING,shed,1` row at ~22-27 s uptime, while the identity-grade rows are all present. A *shed* is
        not an *eviction*, so this gate needs to distinguish them before it can be a pass condition; it is
        currently over-strict rather than the run being bad.
      * **Telemetry note:** L3/L4 passed on the **human** rows only (`Reentry window open slot=3`,
        `Reentry released slot=3`) — the `#CAP,DIAG,reentry_window_open,3,…` and
        `DIAG,canonical_release,3,…` rows are absent in these runs, which is itself worth a look before the
        ordering can be claimed in device time.
      **THE DOUBLE COUNTDOWN CAME BACK, AND THE CAUSE IS THE SD-AWARE FILLED PREDICATE (2026-09-27).** Routed
      B's start through the switch-carried start as directed, and the reassessment found why it had worked in
      the retention run but not here: **`slotHasDataForPlaybackSwitch` is SD-aware**
      (`TrackManager::slotHasLoopContent(..., true)`), and **every slot on this device retains an SD payload** —
      the library observation reports `payload_inventory=64` while only 59 slots are committed. So an *empty*
      slot is **not** automatically a record target: the switch to it takes the **playback-switch branch** and
      waits on a quantized countdown instead of carrying a start. The carrier's own predicate
      (`if (!track.hasCommittedPassesInSlot(slotIndex))`) is satisfied, but it is never reached, because the
      playing/filled branch is entered first.
      **That is exactly how the second countdown is produced**, and it is now understood end to end: the
      switch is pending → the record press lands → `handleToggleRecord` sees
      `!hasCommittedPassesInSlot(selectedSlot)` (the *selected* slot is the empty one) → routes to
      `handleToggleRecordForSlot` → `slotIndex == selectedSlot` and `playingSlot != slotIndex` →
      `queuePlayingSlotSwitch` → **`Reaffirmed playback switch`**. A second countdown for one switch.
      **Driver fix, and it is a safety property rather than a timing tweak:** the driver now waits
      **unconditionally** for a pending switch to execute before any further press, and when the switch never
      runs it **aborts the leg** (`PENDING_SWITCH_NEVER_RAN`) instead of pressing into it. A leg that presses
      there is not testing the acceptance; it is manufacturing the reported bug. Pinned by two tests
      (`_wait_switch_executed` detects a never-running switch, and detects one that runs) — suite **26/26**.
      **Consequence for the leg's design, recorded so the next attempt starts from the right shape:** the
      carrier needs a successor that is a **record** target in the SD-aware sense. The retention run satisfied
      that for **slot 8** while A=4 and B=5 were filled; the `od` leg cannot use a merely-cleared slot 5. Either
      the leg's successor must be a slot with no playback content at all, or the leg must accept the
      playback-switch path and prove the acceptance through a different shape.
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
- **UNRESOLVED OBSERVATION (2026-09-24, user capture `captures/session_20260924_183229.log`) — the ordinary
  Playing stop route under repeated press; acceptance and progress UNPROVEN.** Recorded, not diagnosed: no
  firmware edit, no device run, no solution adopted.
  - **What the capture shows.** Five `Overdub stop refused: stop_job_pending` rows across 1215.093–1228.231 s:
    `phase=9 dest=0` ×3 (**CompanionBridge**, **Playing**), `phase=5 dest=0` ×1 (**CanonicalTail**),
    `phase=1 dest=0` ×1 (**CanonicalPrefix**). Three overdub sessions opened; `Reentry window open` ×3 and
    `Reentry released` ×2. Separately 16 `Capture admission rejected: unmatched NoteOff` rows — a held-note
    reservation signal, **not** a stop-resource denial.
  - **The row is not a denial verdict.** `Track::stopOverdubbing` calls `Track::latchAcceptedStop` in the same
    branch that emits `Overdub stop refused` (`src/Track/TrackOverdubLifecycle.cpp:1721-1748`), so the wording
    survives the acceptance implementation (DEC-078 item 1) and this row alone cannot establish that a second
    press was required.
  - **No job identity, no worker progress.** Two `phase=9` rows 2.312 s apart (1216.836 / 1219.148) are repeated
    observation of the *phase*, not proof that worker progress was unchanged; a `Denied` return, a restart and
    insufficient service all remain consistent. The flashed revision is unidentified — the log opens mid-session
    on a `[Memory]` row and carries no banner or `firmware_hex_sha256`.
  - **No terminal wedge.** `MIDI clock lost` is at 1229.981 s, followed by `CurrentSet saved successfully (v6
    deferred slices)` and then **1,023 `[Memory]` + 256 `#MEM_OWNERS` rows and nothing else** to ~2255 s. That is
    continued execution, not a freeze — and it does not establish stop completion either.
  - **Probe limitation to resolve before the follow-up run.** `!STOP_STATUS` cannot serve this window as it
    stands: it is transport-gated twice (`src/StorageManager/StorageManagerHitlSerial.cpp:672`, re-checked in
    `Track::printStoppedServiceStatusForHitl`, `src/Track/TrackOverdubLifecycle.cpp:385`) and reports only the
    **selected track's ring head** (`captureStopJob()`) — it prints **no latch field**, and `Track::acceptedStopWaitCount()`
    is emitted by no probe. The window under test is transport-running with `dest=0` (**Playing**), so the probe
    would have refused; even stopped it could not show acceptance.
  - **Code-gap candidate — hypothesis only, causal role NOT established.** The bounded per-phase stall fallback is
    restricted to `retargetedFromPlaying` (DEC-070, `src/Track/TrackOverdubLifecycle.cpp:1013`), and the
    non-retargeted **Playing** branches for `CanonicalTail` / `CompanionHistory` / **CompanionBridge** can return
    on `Denied` with no branch-specific diagnostic. **Do not generalize that fallback on this evidence**: its
    raw-only recovery drops the display representation and the protected undo (DEC-070), so generalizing it is a
    behaviour decision, not a timeout fix.
  - **Resolved by:** S4f.11 (acceptance → execution latency per interaction class) and S4f.13 (accepted-sequence
    device verification), once a run on identified firmware supplies stop acceptance, job identity, worker
    progress and scheduler-service evidence.
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

- [ ] **Correct the stale drain clause in the main `lazy-slot-hydration` spec (TRANSFERRED 2026-09-24 from
      `restored-library-stability` task 4.2).** `openspec/specs/lazy-slot-hydration/spec.md` § *The blocking
      persistence drain preserves and reports the restore it waits on* still reads "**Not delivered by this
      requirement:** that the drain *advances* a restore that holds the SD session … **deferred** to
      `runtime-working-set-ownership` S2b.7 … no advancement is claimed here." **S2b.7 landed 2026-09-24**
      (native; `ARCHITECTURE-REVIEW.md` § S2b.7, with the device half now **transferred here** as the three
      unchecked S2b.7 "TRANSFERRED" entries above and **no device class claimed**). This change owns the
      correction and will apply it with its own spec delta at archive/sync — `restored-library-stability`
      deliberately did **not** rewrite it. The corrected wording must say: advancement is **delivered
      natively and bounded by task 3.3**, and the *device* confirmation is the transferred S2b.7 device leg
      (unchecked here, independently reportable).
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

## Incident-support USB folder copy (2026-09-26; separate from stage acceptance)

Local reader preparation is complete (7 host tests, standalone firmware build); see
`tools/midilooper-reader/README.md`. No production code or stage checkbox changed.
- [x] With explicit user approval, installed the isolated reader and verified a folder-only
      `/MidiLooper` copy: 1,007 entries / 6,837,184 bytes; CRC/SHA256/length checks pass.
      Duplicate file preserved separately. Reader remains installed; normal-looper hold remains.
      Evidence: `captures/midilooper-folder-copy-20260926-195228/verification.json`.
      Recovery and content validity remain unclaimed.

- [x] Offline recovery audit of the verified folder copy: verify stored checksums,
      inventory current slots and saved revision candidates; no device actions or
      stage acceptance implied.
      Evidence: `evidence/2026-09-26-workspace-loss/OFFLINE-RECOVERY-AUDIT.md`;
      16 host tests pass. Full content validation and workspace reconstruction remain open.

- [x] Decode the 64 copied current slots with production parsers on the host and
      identify runtime metadata requirements; no restore-readiness claim.
      Evidence: `evidence/2026-09-26-workspace-loss/PRODUCTION-DECODE-AUDIT.md`.
      Three host native cases pass with explicit target-layout adapter; all slots decode.

- [x] Reconstruct a separate host workspace candidate with unchanged slot bytes;
      test missing/corrupt bundle refusal, real bundle load and slot restore.
      Document replacement settings and on-device C++ recovery feasibility.
      Evidence: `evidence/2026-09-26-workspace-loss/WORKSPACE-RECONSTRUCTION.md`.
      Host acceptance only; no device install or capacity gate checkoff.

- [x] Test and reconstruct undo history from the copied passes where possible;
      exercise target load hooks and hydration, verify undo/redo content and reload,
      and state any unrecoverable history or firmware dependency. Host only.

      Outcome: 122 actions reconstructed; all 60 nonempty slot roundtrips pass.
      Reload retains only 9, so no installation acceptance. See UNDO-RECONSTRUCTION.md.
- [x] Establish restart-safe recovered undo history through the production deferred
      load/hydration boundary; preserve content and validate all recovered entries
      after reload before considering device installation.

      **Policy review correction (2026-09-26):** this pending step changes the explicit
      zero-undo-after-load contract; do not treat it as an approved bug fix. See
      `evidence/2026-09-26-workspace-loss/UNDO-LOAD-POLICY-REVIEW.md`. Approval pending;
      existing 122-action host proof remains valid, device hold unchanged.

      **Approved and host-verified 2026-09-26:** DEC-088 — load-time undo reconstruction
      resolves the prior policy block. 122 units survive load/reload with empty
      bundle undo headers; 60 nonempty slot Undo/Redo roundtrips; hydration ordering
      and unrelated background-load Redo preservation pass. Firmware builds at
      padding 840; no device installation or hardware acceptance implied.
      Evidence: `evidence/2026-09-26-workspace-loss/UNDO-LOAD-FIX.md`.
- [ ] Device acceptance of reconstructed workspace and load-time undo after the
      incident hold is deliberately resolved; preserve backup, verify recovered
      content and Undo/Redo after boot. Not run or authorized by the host-only slice.

Host implementation verification: 2,461 native passed / 5 skipped; isolated recovery
acceptance passes; capture-serial firmware SUCCESS at padding 840. Device gate above
remains open.

- [x] Automatic boot metadata reconstruction (DEC-089): inspect before cleanup,
      validate complete slot matrix, preserve originals, publish checked candidate,
      block unsafe fallback saves, prove interruption/retry and content preservation
      on host and build firmware. Actual device acceptance remains separate.

      Host evidence: `evidence/2026-09-26-workspace-loss/BOOT-REPAIR.md`.
      276 interruption cuts; 64-slot load/Undo/Redo; 122-action healthy reboot;
      native 2,461 passed / 7 skipped; capture-serial padding 856. Device gate open.

**2026-09-26 — recovery firmware uploaded with explicit user authorization.**
`teensy41-capture-serial` upload SUCCESS; SHA256
`4b13749ab9d97b0699fb02f813a3a9a26b7475a60391e19ffa784c21cec93255`
matches the host-verified image. Confirmed standalone read-only reader before upload;
archived and cleared the incident hold for this approved recovery installation.
Device reports runtime metadata repaired, `BOOT,load,ok`, and a settled checkpoint:
`admission_complete=1`, `interactive_ready=1`, `undo_hydrated=1`,
`transport_stopped=1`, `deferred_prewarm_idle=1`, `free_chunks=391`.
Serial held open through verified persistence idle, then closed. No content/clear
stimuli. This establishes successful recovery boot, not per-loop content parity,
hardware Undo/Redo roundtrips or second-boot acceptance; those remain open.
Evidence: active-change workspace-loss `authorized-recovery-upload/`.

**2026-09-26 — blocking-SPI visual comparison PASS (user observed).**
User confirms “Yes, the screen updates now.” Same recovery/renderer source, existing
`MIDILOOPER_DISPLAY_DMA=0` switch only. Device reached load=ok, restore admission
complete, undo hydrated and transport stopped. This isolates the frozen-screen
behavior to the DMA display route; it does not establish the precise DMA defect.
Blocking SPI remains installed as a diagnostic workaround; no real-time timing
acceptance or permanent DMA fix claimed. Serial capture remains open.
Evidence: `captures/display-blocking-diagnostic-20260926/metadata.json` and session log.

**2026-09-30 — S4f.17 sub-slice: the deferred overdub stop publishes its interval's undo entry.**
The sidebar `U:` level (`TrackUndo::undoDepthForLoop`) counted only the record pass while a loop was live
(`U:01` after recording plus overdubs against `U:03` after a reboot), and the base check
`undo_redo_after_overdub_stop` logged `0 counts` against `expected_min: 1`. The deferred overdub stop drive
prepared the interval's `OverdubPassAdded` entry only in the `CompanionHistory`/`UndoCollection` phases, so three
correct production routes committed the pass with no entry: the **clean-stop jump** (`PrepareTransfer` with
`SealOutcome::SkippedEmpty`; `stop_phase` trail `1,6,10,11`, the dominant device route), the **STOPPED
destination without verified companion preparation** (the transport-Stop fallback published no entry by design),
and the **superseded causal base** (its variant runs before what was then the audible-bridge handoff). The
reported "3-bar record loses the levels" is the same defect reached through timing: a late record start is
quantized up to 3 bars, shifting the later steps so the overdub stop lands as a clean stop. Two further defects
kept the handoff from working once it existed: `job.undoPublicationReady` held `begin`'s *return* (the
transaction only opens into `DetachRedo`), and `TrackUndoPublication::canPublish` required a *staged* pass
identity a clean stop never has.
**Fix** (user decision: deferred overdub route only; publish at pass publication, once per job; full-route
collection machinery intact): new `Loop::gatherSessionExitReceiptUndoIdentities` reads the interval's **own**
session-exit receipt (DEC-068) in `collectOverdubSessionUndoPasses` shape so the receipt type stays Loop-private;
`Track::prepareDeferredOverdubUndoEntryFromReceipt` builds the canonical `OverdubPassAdded` entry from that
receipt plus any staged final pass; the handoff sits at the **top of the `AtomicTransfer` phase**, once per job
(`undoPublicationStarted`), and drives the protected transaction to `Ready` before any variant runs, so the
audible bridge, the stopped recovery, the **no-staged** case and the superseded case each publish what was
prepared (`commitSupersededCausalBase` now takes the stack as its publishability witness);
`canPublish(…, kInvalidPassId)` means "this turn commits no pass of its own". Fail-open when the interval names
no identity (publish nothing, always retire), fail-closed on a refused `begin` (retain and retry).
Fixture `test_stopped_overdub_stop_publishes_one_undo_entry` (`test_shared_capture_finalization`, four legs
including the clean stop); seven mutations red-proved and restored byte-identically (removing the prepare call
site also stalls `test_stopped_publication_requests_own_slot_once`, proving the fail-open guard is load-bearing).
Strict focused 56/56; full native 2 637 cases / 2 630 succeeded / 7 skipped / 0 failed; capture-serial SUCCESS,
RAM1 padding 1 928 (floor 840). Device A/B on the clean route with an identical command:
`captures/s4f20-odundo-07` (pre-fix) trail `1,6,10,11` and `undo_entries` 5→5, no short-route row;
`captures/s4f20-odundo-10-1` (fixed) same trail, 5→6 with one `src=deferred_short_route` row; full-route control
`captures/s4f20-odundo-09` 5→6 and `MIDI: Undo (entries=3)` (the reboot's `U:03`). Decision DEC-095; packet
section and plan `overdub_stop_undo_level_short_route_bugfix.md`. No task checkoff: S4f.17/S4f.18/S4f.20/S4f.21/
S4f.22 stay open.

**2026-09-30 — S4f.22 device attempt against the committed build (`3307f33d`): the R→R leg drives, overlap
NOT_EXERCISED; the OD→OD leg is blocked at prepare. S4f.22 stays open.**
Fresh response-driven legs on the committed clean tree (identity pinned: `git_commit=3307f33daec1ce1e
9924df191588d905b36f2aef`, `firmware_hex_sha256=351b2e99…`, no `git_dirty`). Authorized scratch slots:
track 8 slots 4 and 5 (both cleared this session).
**R→R** (`captures/s4f22-rec-rec-01`, `hitl_accepted_sequence.py --leg rec`, track 8 A=4 B=5): the leg drove
end-to-end — A seeded (`prepare_slot_a: ALREADY_EMPTY`, `seed_a_recording confirmed`), then
`select_track → a_record_start → a_stop → b_switch_start → b_stop` with one press per stop and a clean
persistence-aware shutdown (2 stop+drain cycles). Oracle: `telemetry_present`, `identity_pinned`,
`L1_a_stop_accepted_once` and `L2_no_capacity_denial` OK; **`L3_a_finalization_window`** fails ("A never froze
into a deferred finalization"), so `L4`/`L5` are NOT_EXERCISED — the required classification when the overlap
window is missing. Setup gates: `fresh_boot` FAIL (capture attached without a reset, so no `BOOT,usb` banner)
and `overflow_free` FAIL (one `#CAP,62641732,RING,shed,1`). The reason the record leg cannot show the window is
the recorded design fact that the production record stop publishes **inline**, while the deferred capture-stop
window belongs to the overdub path (2026-09-27 finding, re-confirmed).
**OD→OD** (`captures/s4f22-od-od-03`, `--leg od`, same pair): `PREPARATION_FAILED` at the second seed — B's seed
records and stops (`length=1536`), then the A-slot press is answered `Loop 4: Overdub finalization pending` and
the following stop press is consumed as a session undo (`Record pass undo reset for slot 4: removed=1`), so A
never records. The same signature appears in `captures/s4f22-od-od-01` and in this file's 2026-09-29
classification ("the next gesture is consumed as `Overdub session undo`"). **This is the leg's blocker: a
decision on the gesture semantics (firmware) or the driver's gesture order, not a timeout or a retry.**
**Not attempted:** the mixed record-A → overdub-B leg — the retention-handoff shape
(`scripts/hitl_retention_handoff.py`) needs three authorized slots (A/B/C) and this session authorized two.
**Content parity** (`inventory_equal`) was not re-reached, since no leg produced the overlap the pipeline is
meant to verify. No task checkoff; the packet records the same evidence and the four remaining closure items.

**2026-09-30 — DEC-096 host/build pass; S4f.22 device gate attempted, not accepted.** The user's pending-finalization rule is implemented in `MidiButtonActions`; both production-owner native fixtures pass. Focused suite 28/28; full native passed; `teensy41-capture-serial` SUCCESS, RAM1 padding 1,928 (floor 840); strict OpenSpec validation passed. User authorized track 8 slots 3–5, and only those targets were identity-grade cleared. Fresh read-only census found no committed events/passes/length in them; verified payload files remain. Device evidence: `captures/s4f22-dec096-od-01` OD→OD drove but oracle `SETUP_INCOMPLETE` (3 `RING,shed` rows; missing overdub identity links); `captures/s4f22-dec096-rec-01` stopped before recording after LOOP_EDIT/double-press setup; `captures/s4f22-dec096-mixed-01` seeded slot 4 but stopped before slot 3's seed because startup LOOP_EDIT routed the slot gesture to geometry editing. The partial mixed attempt leaves test content on authorized scratch slot 4. No content-parity run; no S4f.22 checkoff. Stop device retries here; a corrected, specific device path is required before further upload/run.

      **2026-09-30 — the oracle's `overflow_free` gate no longer counts a timing shed as ring loss.**
      `DebugSessionCapture` sheds only non-Tier-A diagnostics in the playback flush instead of blocking the
      transport on USB I/O and reports it as neither loss kind; `scripts/hitl/serial_collector.py` already
      marked only `overflow`/`critical_rejected`/`wiped` incomplete, and `midi_boundary_timing.py` never
      counted a shed. `scripts/hitl/accepted_sequence_probe.py` grouped `RING,shed` with
      `RING,tier_a_evicted`, so **every** fresh-boot leg was `SETUP_INCOMPLETE` on a shed row alone — the
      over-strict gate recorded here on 2026-09-27 and again for `captures/s4f22-dec096-od-01`. The gate now
      fails only on `tier_a_evicted`/`overflow`/`critical_rejected`/`wiped`, reports shed as a non-fatal
      `ring_shed_diagnostics_only` check plus `evidence.ring_shed`, and (second fix in the same pass) treats
      an **absent L3 window / L4 release as a staging outcome**, i.e. "the acceptance was not exercised" in
      the oracle's own L3 wording, matching the packet's NOT_EXERCISED classification instead of labelling a
      NOT_EXERCISED run a chain violation. Re-graded with **no device action**: `s4f22-dec096-od-01`
      `SETUP_INCOMPLETE`(3) → `INSUFFICIENT_EVIDENCE`(4); `s4f13-od4` keeps `SETUP_INCOMPLETE`;
      `s4f13-od*`/`carrier` move to `INSUFFICIENT_EVIDENCE` (all remain non-passes). Host:
      `scripts/test_accepted_sequence.py` **54/54** and `scripts/test_accepted_sequence_corpus.py` **3/3**
      (new coverage: each session-invalidating ring row is a setup failure, a shed alone is not), plus
      integrity 17/17, command-forwarding 26/26, drain 20/20, transitions 61/61, shutdown 15/15;
      `git diff --check` clean. No firmware change: the defect was host-side classification.

      **2026-09-30 — clear-first device rerun attempted; blocked in the driver's census precondition.**
      `captures/s4f22-dec096-od-02` (`scripts/hitl_accepted_sequence.py --leg od --track 8 --slot-a 4
      --slot-b 5`, the driver's default clear-first path) uploaded the unchanged image `8d07faec…e05f`
      (`git_commit=f0c4b26d`, dirty tree) and failed at the clear, before any leg gesture: the initial
      read-only census was lost, so `known_occupied` was `None`, the filled non-selected target was not
      pre-selected, and its long press was answered `Loop 4: Queued playback switch (filled slot)`
      (`#CAP,68054720,ST,Track,STOPPED,PLAYING` — the switch started the transport instead of clearing);
      the follow-up census also went unanswered (`census_lost`), so `stopped_preparation.clear_slot`
      returned `UNCONFIRMED` and the driver failed closed (`PREPARATION_FAILED`, no seed, no take). The
      shutdown reached only `incomplete_timeout` (persistence not verified idle within 30 s), which latched
      `captures/.device-shutdown-incomplete.json` (`context: accepted-sequence-od`) and correctly refused
      the next upload/reset. A read-only canonical check afterwards (`captures/dec096-recovery-check-01`)
      reached **verified persistence idle** (`2 stop+drain cycle(s)`, tail `SAVE,idle,0`,
      `FinalizeWorkspace … done,ok`), so the device is known good and the latch is the only gate before
      another reset. The proven identity-grade clear for a filled target is
      `scripts/hitl_retention_handoff.py --prepare-only` (`captures/s4f22-dec096-prepare-02` cleared track 8
      slots 3/4/5 with no census precondition). No S4f.22 acceptance, no content parity, no checkoff;
      driver-numbered slot 4 still holds its `f0c4b26d`-era seed.

      **2026-09-30 — the driver's device path is corrected (host tooling only; no firmware, no device action).** The
      three failed legs above are each a **named** failure now, not a silent press or a traceback:
      `scripts/hitl_accepted_sequence.py` retries the read-only census on a bounded set (`--census-attempts`) and
      **refuses before any gesture** while a target's occupancy is unknown (the old fallback long-pressed a filled,
      non-selected target, which `CLEAR_TRACK_FOR_SLOT`'s not-selected branch answers with
      `Loop N: long-press start (not playing)` — playback, never a clear); a filled target's **selection** must be
      named by its own `#CAP,<us>,DISP,<slot0>,…` row before the clear; the seed press must produce a slot-scoped
      `DISP,<slot0>,ARMED` receipt over a bounded press set (`DEFAULT_SEED_PRESS_ATTEMPTS = 3`, because
      `captures/s4f22-dec096-mixed-01` shows press 1 silent and press 2 only moving focus) with **no**
      transport-start fallback; repeated presses of one button stay outside the firmware's tap window
      (`captures/s4f22-dec096-rec-01`: two taps 0 ms apart became `Loop 3 (double)` = `OVERDUB_FOR_SLOT`); and
      every seed abort is a reported `PreparationFailure`, so the run writes its `accepted-sequence.json` at all

      **2026-09-30 — device rerun on the corrected driver: the OD leg drives (`captures/s4f22-dec096-od-13`,
      `DROVE_LEG`, commit `cf6c1853`, firmware `8d07faec…` = the DEC-096 image).** Ten attempts, one fix each, all
      attributed from the device's own rows: the census retry sized for the boot restore and refusal before any
      gesture (`od-02`, `od-06`, `od-07`); a decode failure and a crash both named and reported, with the report
      written at every milestone (`od-08`, whose `LIBOBS,E` row read mid-write raised `int('')`); receipts
      qualified by the gesture's own device clock instead of file position or a text match (`od-03`, `od-04`);
      receipt waits flushing the capture ring (`od-05`); the commit mark before the commit press (`od-09`, the
      `RECS,stop,<slot>` row lands at the stop edge); seed readiness from the live human rows with no press on an
      already-armed target (`od-10`, where the extra press was read as `Stop Recording`); and the leg waiting for
      the predecessor's publication while reporting the device's own
      `record admission deferred while a stop obligation is live for slot N` (`od-12`, publication 40 s after the
      stop; `--predecessor-publication-timeout-s`). **`od-13`:** both clears (B through the documented
      `post_load_holdoff` refusal retries, A through an `ABORTED` retry), both seeds armed/recorded/committed/
      settled, the full plan `select_track → a_overdub_start (16 notes) → b_switch → b_overdub_start (1 note) →
      b_stop_request` driven, shutdown `complete`, no latch. Oracle: every setup gate **ok** (fresh boot, identity
      pinned to this commit, overflow-free with the single `RING,shed` reported as the non-fatal diagnostic row it
      is), `L2_no_capacity_denial` **ok**, `L3_a_finalization_window` **ok**
      (`DIAG,reentry_window_open,3,1049,1536`), `L4_a_capacity_released` **ok**
      (`DIAG,canonical_release,3,32,1536,8`), and `L1`/`L5`/`L6`/`L7` + `overdub_interval_identity` **unknown** in
      the oracle's own words: *"RECA/RECS do not identify overdub intervals; correlated overdub start,
      accepted-stop and retirement evidence for A and B is still required"* — the sibling change's identity work,
      not this driver's gates. Evidence: `test_accepted_sequence` **66/66**, 15 neighbouring host suites
      **372/372** together, four mutations red-proved and restored byte-identically, `git diff --check` clean.
      **No checkoff:** S4f.22's acceptance still needs that overdub-identity evidence and a content-parity run;
      further device iteration needs approval.
      (`rec-01` and `mixed-01` wrote none). The seed's post-commit finalization receipt is recorded, not gated.
      Evidence: `test_accepted_sequence` **63/63** (9 new), four mutations red-proved and restored
      byte-identically, 15 neighbouring host suites **369/369** together, the new gates replayed against the real
      logs, `git diff --check` clean. One unrelated pre-existing flake recorded:
      `test_midi_clock`'s `test_pumped_host_phase_waits_on_the_pump_instead_of_sending` failed 1/8 isolated runs
      (0/8 with these suites first). **No checkoff:** S4f.22 stays open on its device half, the leg has not been
      driven on this fix, and `captures/.device-shutdown-incomplete.json` is still latched (`context:
      accepted-sequence-od`), so an upload/`--boot-reset` needs approval.

      **Superseded by the device-rerun entry above (2026-09-30):** a leg *has* since been driven on this fix
      (`captures/s4f22-dec096-od-13`, `DROVE_LEG`) and `captures/.device-shutdown-incomplete.json` is **clear**;
      S4f.22 stays unchecked for the overdub-identity evidence and a content-parity run, not for the driver.
