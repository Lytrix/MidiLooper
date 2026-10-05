# Reconciliation — the change's remaining tasks against the code and the evidence

**Current closeout — the activation audit, atop `25542299`, the gated rolling slice, the `H x K` contention sweep and the
preserved cumulative uncommitted work, and the playing-chain boundary slice.** Full native: **2,794 cases / 2,787 passed / 7 skipped / 0 failed**. Default
capture build passes the unchanged RAM1 floor 840 (padding **1,640**; the explicit rolling qualification build's 984 is
from the rolling slice). No upload; the first two slices are committed (`2620168e`), the audit is not. Earlier counts below
and in dated runtime history are historical, not final-tree verification. Three slices sit in this tree's history: the
gated rolling handoff ([ROLLING_HANDOFF.md](evidence/requester-retry-integration/ROLLING_HANDOFF.md)), the test-only
contention sweep ([EVIDENCE.md](evidence/2026-10-05-s310x-hk-contention-sweep/EVIDENCE.md)) the activation audit
([EVIDENCE.md](evidence/2026-10-05-s310y-activation-audit/EVIDENCE.md)) and the playing-chain admission boundary
([EVIDENCE.md](evidence/2026-10-05-s310z-rolling-admission-boundary/EVIDENCE.md)), each with its own durable verification
manifest for the exact scope and results.
**Method / corrected dependency reading.** Task completion requires code, production-path
fixtures and the applicable evidence. Implementation, controlled enablement, qualification and
fallback removal are separate stages. `requestCommittedPlaybackWindow` and
`adoptPreparedPlaybackWindow` now have gated rolling production callers through
`serviceRollingPlaybackWindow` and `resolveRollingPlaybackWindow`. The gate defaults
OFF; native exercises the Track chain and an explicit qualification build compiles
the main dispatcher. This is not continuous service qualification. Initial launch is already
wired through `playbackLaunchReadyForSlot` / `bindPlaybackLaunchForSlot` and `boundLaunchView`;
legacy readiness remains the explicit dense fallback case, not the general launch boundary.
The completed-publication predicate `playbackWindowReadinessForSlot` is not by itself an
audible handoff contract. The corrections below do not check off unfinished acceptance.

---

## 1. The reconciled state

### §1 Decision gate — **closed** (1.1–1.7 checked; DEC-108, the reassessment, the G2 packet, F-1's naming ruling)

### §2 Fixtures

| Task | Status | Fixture / implementation | Evidence | Named remainder |
|---|---|---|---|---|
| 2.2 F-2 derived deadline | **open** — partial | nearest only: `test_an_admitted_deadline_miss_is_serviced_per_turn_once_per_traversal_and_never_re_granted` (`test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp:6402`) | none claims 2.2; open by the docs' own admission (`tasks.md:226`, `evidence/2026-10-02-s35-readiness-holder/EVIDENCE.md:105`) | no fixture derives the deadline from tempo/quantisation/phase-advance/service-availability over a short **and** a long loop; gated on **3.10**'s envelope and **6.3**'s sweeps |
| 2.3 F-3 one pass late | **checked now** | `test_playback_lag_rule_serves_the_previous_revision_once_and_names_a_second_miss` (`:4718`, run at `:9043`) | `evidence/2026-10-02-s36b-lag-rule/EVIDENCE.md:23,44-53` (M4 allowance-not-reset, M5 replacement, M6 once-per-episode; byte-identical restores) | — |
| 2.4 F-4 nothing allocates/blocks on the edge | **open** — partial | `test_window_owner_serves_one_grain_per_turn_and_publishes_nothing` (`:4152`) | s31 packet (`EVIDENCE.md:112-113`) | no recorded mutation where *the commit edge itself* takes capacity or builds inline; the nearest red-proof removes the publication call (`evidence/2026-10-02-s32-interval-publication/EVIDENCE.md:68-70`) |
| 2.5 empty-interval readiness | **checked now** | `test_window_readiness_names_its_holder_and_an_empty_interval_is_ready` (`:4427`, run at `:9038`); silence half in `test_cross_generation_closures_are_reconciled_and_an_empty_window_silences_nothing` (`:5619`) | `evidence/2026-10-02-s35-readiness-holder/EVIDENCE.md:21,82-91` (two mutations; the pre-change non-empty term red-proved) | — |
| 2.6 stale deferred completion | **checked now** | `test_shared_view_transitions_state_the_ownership_rules_and_a_late_completion_publishes_nothing` (`:4927`, run at `:9059`) | `evidence/2026-10-02-s311-shared-view-transitions/EVIDENCE.md:45-53` (M10 guard dropped, M11 identity not invalidated, M12 wrong row) | — |
| 2.7 demoted-shape coverage gap | **open** — partial | demoted-slot window fixture `:4340`, demoted target committing through the turn `:3229` | the tripwire is *stated* (`design.md:78`) but no mutation removing the window build on a demoted slot is recorded | run that mutation; it is the RWO-recorded tripwire, so it is cheap and load-bearing |
| 2.8 red-prove inside the slice | **open** — partial | process task; mutation tables exist per slice (s36a `:51-58`, s36b `:44-53`, s311 `:47-51`, s312a/b/c, s313 `:85-88`) | — | untestable for the fixtures that do not exist yet (2.2, 2.9, 2.11, 2.12) |
| 2.9 retained-source hold/release | **open** — partial | `test_retained_source_is_admitted_with_roles_and_survives_both_destruction_paths` (`:4553`) | `evidence/2026-10-02-s36a-retained-source/EVIDENCE.md:28,40-58` (M1–M3) | the task's full matrix (pause a build → replace the published root → undo/reclaim → reset path with IDs reused → release + load → resume → exact old-version content) is not one fixture; requires **3.6** retention integration, **3.10** serving-path integration and **4.1** parity verification; not removal in **3.7** |
| 2.11 acceptance accounting | **open** — partial | `test_window_admission_limit_accounts_effective_events_and_the_closure_reserve_inside_l` (`:5068`), paged stream `:5125`, capture-edge refusals `:5203`, `:5304` | `s312a:24`, `s312b:6,:77`, `s312c:108` | no real wrap-crossing interval, no edit that moves events into a full interval, no SD-restored pre-limit loop through pages with cross-page closures — that matrix is fixture **5.5**'s |
| 2.12 preview source | **open** — partial | `test_preview_window_is_a_distinct_source_and_never_committed_currency` (`:5367`), `test_preview_window_is_cancelled_by_its_lifecycle_and_hands_off_to_committed` (`:5415`) | `evidence/2026-10-02-s313-preview-contract/EVIDENCE.md:17-21,94-101` | the integration half needs a suite linking the window lane **and** the real edit stack; requires 3.13's production callers and 3.10's serving-path integration, before 3.7 removal |

### §3 The window authority

| Task | Status | Implementation | Evidence | Named remainder |
|---|---|---|---|---|
| 3.1–3.6, 3.8, 3.11 | **checked** | `stepPlaybackViewMaintenance`, `publishCompletedBuildAsCurrentWindow`, `PlaybackWindowIdentity`, `playbackWindowReadinessForSlot`, `RetainedIntervalIndex`, `resolvePlaybackLag`, `RetainedPlaybackSource`, `resolvePlaybackViewTransition` | `s31`, `s32`, `s34`, `s35`, `s36a/b/c`, `s311`, `REUSE-BEFORE-INVENTION.md` | 3.11 carries one **named gap**: the prepared-successor retirement branch is unfixtured, deferred to the slice that gives the successor lane a production caller (this document's §3) |
| 3.7 remove the live mutable merged-window build | **open — gate still closed, nothing implemented** | target intact: `Track::playMidiEventsForSlot` (`src/Track/TrackPlaybackHotPath.cpp:505`), `useMergedFallback` → `buildMergedPlaybackFallbackCold` (`:308-311`), live buffer `mergedMidiEvents` (`include/TrackPlaybackRuntime.h:474`) | no evidence dir exists | gate items 3.12, 3.13, 3.14, 4.5 are **not** at full verification and the gated requester/handoff does not establish continuous service or recovery — see §3 |
| 3.9 readiness receipts | **open** — landed 4/4 increments | `readinessReceiptImmediate` (`src/Utils/DebugSessionCapture.cpp:1562`), `reportPlaybackWindowReadinessReceipt` (`src/Track/TrackPlaybackWindowBuild.cpp:3175`), `playbackBuildRow`, Tier-A row `DIAG,win_readiness,` (`include/Utils/CaptureLineTier.h:179`) | `s39`, `s39b`, `s39c`, `s39d` | the declared cadence interval (16) and **hardware**; the refusal rows were re-assigned to the caller (3.12c's admission) |
| 3.10 fair, bounded, deadline-ordered service | **open** — the change's front line | `selectPlaybackHolder` (`include/Utils/MidiServiceDrain.h:298`), `handoffLeadTicks`, `playbackWindowServiceOffer`, the offer+sweep dispatch (`src/main.cpp:477-516`), `preparePlaybackLaunchForSlot` ← `servicePendingSlotProvision` (`src/TrackManager/TrackManagerSlotMatrix.cpp:399`) | `s310`…`s310p`, `timing-envelope-qualification`, the gated `requester-retry-integration/*`, the `H x K` sweep and the activation audit | see §3 below; **wiring**, the **starvation half**, the **envelope derivation** (measured: the composed cost law, the density/span axes and the boundary pair) and the **contention** half (`H x K`) are closed; **one grain per turn** as a uniform invariant, **continuous refill**, the **clock/tempo** sweep beyond the declared advance and the **device leg** remain open, and activation itself is now an owner **enablement decision** (the demand that exists carries its stamp; the launch and the capture edge are exempt by design) |
| 3.12 admission limit + bounded legacy path | **open** — 3.12a/b/c landed | `kWindowAdmissionLimit = kEventsPerView` with `WindowAdmissionAccounting` (`include/RuntimePlaybackView.h:28,44-92`), `captureIntervalAdmissionForSlot` + the MIDI-edge gate (`src/Track/TrackCaptureInput.cpp:768-769,940-962`), `WindowPagedStream`/`WindowPageCursor` (`include/WindowPagedStream.h:58`) | `s312a`, `s312b`, `s312c` | fixture **5.5**'s timing envelope (over-capacity same-tick, wrap, cross-page closures) and the paged path's **production consumer**; the ring size (3×128) and closure reserve (128) stay declared-but-unmeasured choices |
| 3.13 preview contract | **open** — landed | `PlaybackWindowOrigin` in both identities (`include/RuntimePlaybackView.h:147,160,194`), `requestPreviewWindow` / `cancelPreviewWindowForSlot` (`src/Track/TrackPlaybackWindowBuild.cpp:2632,2676`) | `s313` | fixture 2.12's integration half + the production callers (preview requester, `EditManager` lifecycle hooks) |
| 3.14 undo/redo audible handoff | **open** — 3.14a/b/c landed; production path exists | `refreshPlaybackAfterCommittedPassStateChange` / `refreshPlaybackAfterEditPassStateChange` (`src/TrackUndo.cpp:263,278`), `reconcileAfterCommittedPassStateChange` / `reconcileAfterEditPassStateChange` (`src/Track/TrackPlaybackWindowBuild.cpp:2992,3016`) | `s314`, `s314b`, `s314c` | the refresh is still not a **request through the immutable publication owner**; no between-grains matrix (3.10/3.14 serving-path integration, before 3.7 removal); `UndoEntryKind::RecordPassAdded`'s undo never calls the refresh (recorded finding) |

### §4 Parity

| Task | Status | Implementation | Evidence | Remainder |
|---|---|---|---|---|
| 4.1 F-5 undo parity | open — landed | `CheckpointParity` + `resolveCheckpointParity` (`include/LoopSnapshot.h:37,54`), `Loop::checkpointParity()` (`src/Loop/LoopSnapshotLifetime.cpp:69`) | `s41` | content-level comparison (4.3's pipeline); no production consumer of the name |
| 4.2 F-6 save parity | open — landed | `writeLoopPersisted` / `applyLoadedLoopSlotBodyCommit` (`src/StorageLoopIo.cpp:1314,1353`) | `s42` | a content-divergent oracle and remedy are still owed; persisting provenance is one possible format-changing remedy, not a prerequisite for all parity work |
| 4.3 F-7 boundary parity | open — landed | `Track::playbackWindowEventAtForSlot` (`src/Track/TrackPlaybackWindowBuild.cpp:3076`) | `s43` (32 windows over a 64-bar loop, row-for-row) | the **device leg** (6.4 + 6.5), the transition closure obligation (3.10/4.5, before 3.7 removal), the overlap rule |
| 4.4 F-8 length-independent residency | open — landed | `fullTakePlaybackViewAdmissible` (`include/Loop.h:290`), `prewarmPlaybackForSlot` (`include/Track.h:211`) | `s44` (sweep 16/17/24/30/32/64 bars, floor 30 bars) | the parity half with the content-verification pipeline, the retained-source figure, 6.3's wider sweeps |
| 4.5 containment, last resort, closures | open — landed, no production caller | `resolveContinuityContainment` (`include/RuntimePlaybackView.h:239`), `containmentForSlot` / `applyLastResortForSlot` (`src/Track/TrackPlaybackWindowBuild.cpp:1351,1333`), closure plan/apply `:2867,2908` | `s45-containment`, `s45-closure-reconciliation` | positioned promises now have a gated rolling requester, but containment/recovery is not integrated into that serving slice; the emitted order at the real handoff, the moving-closure case and displayed provenance are owed on the serving path |

### §5 Envelope and placement; §6 gates

`5.1` capacity/span provisional and `5.4` honest envelope have their content in `RuntimePlaybackView.h:28,119-121`,
`proposal.md:120`, `design.md:222,229`, but **no evidence file of their own** and no running "memory ∧ deadline"
gate. `5.2` is landed (`WindowAdmissionAccounting::fits` + `Outcome::OverCapacity`, fixtures `:5068`, `:8033`) yet
unchecked. `5.3`'s size pins hold (`src/Track/TrackPlaybackRuntime.cpp:2501,2517` target 7,120 / host 9,144) but
the task's own stated baselines (6,368 B `LoopPlaybackRuntime`) are **superseded** and still cited in
`design.md:22` and `ExecutionBounds.md:1468` — doc drift to fix when 5.3 is worked. `5.5`'s timing half is
fixture 5.5's (see 2.11/3.12).

`6.1`/`6.2` are repeatable regression/build gates, not prerequisites that prevent
implementation: current-tree runs and durable manifests exist, including
`evidence/requester-retry-integration/single-pass-verification.json`. Final acceptance
still requires checks on the final implementation; skipped cases are not passes.
`6.3`'s **contention, span and density axes are now measured** (the `H x K` sweep: the composed cost law, the grid, the
density and span tables, and the boundary pair) — see
[`hk-contention-sweep`](evidence/2026-10-05-s310x-hk-contention-sweep/EVIDENCE.md); what the task still owes is the
**clock/tempo** axis beyond the declared advance, chords and same-tick density, and the **device** leg. `6.4` separates
profile preparation/registration, authorization, actual exercise and qualification;
profile work can precede rolling enablement. `6.5`'s **native** half has since forced its two gaps on the playing chain
— A→B→A view-slot reuse with the first completion arriving last, and cancellation at every preparation grain with
recovery (`evidence/2026-10-05-s65-native-verification-split/`; mutations MR-B, MR-C1 and MR-C2 red-prove them, MR-A
recorded as a non-red-proof) — while the **interval A/B revision trace** stays partial (shared-view fixtures only) and the
**hardware** half stays unqualified: initial-launch device observations exist (`DEVICE_ROOTLESS_HOLD.md`), but sustained
rolling timing, the contention legs and full content parity are **NOT RUN** (they need 6.4's registered profile and an
authorized upload). `6.6` and `6.7` documentation reconciliation can proceed
without claiming shipped capability. For `6.8`, RWO **S7.5a** provides the upstream
numeric oracle and shared budget before qualification/affected enablement. The
window evidence is an INPUT to **S7.5b** dependency closure and **S7.5** combined
acceptance, not work that must wait for those downstream gates to pass.

---

## 2. Cross-cutting findings

1. **A gated service slice now exists, not general service enablement.** Off-clock
   `serviceRollingPlaybackWindow` requests a later current-root interval; the bounded
   worker prepares it; `resolveRollingPlaybackWindow` validates the absolute boundary
   and adopts it before the existing emitter reads it. Native exercises these real
   Track entries; `main.cpp` wiring is target-build verified, not native linked.
   Preview, paging, retained-old-source reconstruction and full containment remain
   incomplete; the production rolling gate is OFF.
2. **Initial launch remains production-wired.** Slot provisioning prepares the
   initial immutable interval and the actual switch binds it. Expiry is sticky.
   The gated rolling selector prepares ahead, never only at expiry: from phase0
   it targets [4608,6144), with fallback after initial [0,1536) and through the
   intervening gap. Native proves successive handoffs, not contiguous coverage.
3. **Capacity and the declared timing constants are held, by the owner's ruling of 2026-10-04.** The
   2,048-event limit stays the window storage capacity and must **not** be shrunk, nor made a function of loop
   length, to make timing pass; the declared grain/gap/lead constants must not be bent either. The open
   contradiction is therefore to be resolved on the **service side** (what work a request costs, what lead that
   implies, and a *named* refusal when a shape cannot be served) rather than on the capacity side.
   **The service side is now quantified (2026-10-05, the `H x K` sweep):** a window's cost is its own
   `serviceTurns` (measured: 2/7/33 turns for 5/20/80 notes), the composition needs `H x (1 + K)` of them, the
   declared charge is a *sufficient* bound on those turns (`serviceTurns + 1` at these shapes, because the
   promotion grain and the order bound are charged while the work lands inside the completing turn), and the
   declaration's `Delta-max x H x lead` predicts **where** a promise stops being keepable -- a shape just outside
   it still admits (the unenforced admission judges only the request's own cost) and is then named
   `one_pass_late`. **The callers' clock wiring is audited (2026-10-05) and the demand that exists is already stamped:**
   the only production window request is the lane turn's `serviceRollingPlaybackWindow` (the launch is unpositioned by
   construction and never judged, and the capture edge takes no clock because it is accounting, not a promise). What
   remains on the service side is therefore the owner's **enablement decision plus the device leg**, and the wider sweep
   axes -- not a missing stamp and not a missing constant.
4. **The cumulative work remains uncommitted.** Final native and both build results
   are recorded above and in the slice manifest. No commit or upload was requested
   for this slice; preserved earlier device observations are not this image's proof.
5. **Doc drift found while reconciling** (fix with the slices that own them): task 3.14 names
   `refreshPlaybackAfterCapturePassStateChange`, which **no longer exists** in `src/` (today:
   `refreshPlaybackAfterCommittedPassStateChange` / `…AfterEditPassStateChange`); 5.3's baselines are superseded
   by today's pins; 6.6's RWO records still say "nothing implemented"; `DELIVERABLE_TRACKING.md` has no entry
   for this change.
6. **Three fixtures were honestly checkable and are checked now** — 2.3, 2.5, 2.6 (§1) — because each has its
   fixture, a recorded green and a recorded mutation with a byte-identical restore. Nothing else in §2–§6 meets
   that bar today.

---

## 3. Implemented bounded slice — gated current-root lookahead and audible handoff

The bounded implementation and native verification are complete; continuous
qualification remains open. The retained execution contract is
one end-to-end request -> preparation -> validated handoff -> emission slice,
NOT task3.7 removal and NOT a supported contiguous two-bar envelope.

- Keep window span <=two bars, capacity2048 and every declared service constant.
  Prepare a later two-bar interval with legitimate absolute lead; fallback explicitly
  covers intervening intervals. An adjacent two-bar request remains a negative
  admission case. Do not lengthen the window to manufacture preparation time.
- The source is the current selected `LoopSnapshot`. The retained-body roles/index
  are not yet an older-revision reconstruction producer; that integration remains
  open. Do not claim that all remaining work is merely wiring.
- Request off-clock through the existing Track owner, before the target interval.
  Keep existing refused-demand retention and original deadlines; never overwrite
  a positioned debt to obtain a later opportunity or synthesize ready views in tests.
- Qualify the real consumer edge: candidate identity, geometry, clock epoch and
  absolute traversal, completion/publication state, emission-cursor transfer and
  unchanged-note continuity. `adoptPreparedPlaybackWindow` is a preparation swap,
  not this entire audible contract. Unsupported transitions retain fallback.
- Production enablement stays OFF by default. Native controlled tests exercise
  the same Track-level request/worker/consumer symbols; `main.cpp` is not native
  linkable, so test its wiring separately and state that coverage limit. Any future
  device experiment needs its own explicit qualification build/profile/authorization.
- Correctness fixtures, preview3.13, undo3.14 and recovery/closures4.5 integration
  precede 3.7 removal; they do not wait for removal. Paged3.12 implementation/tests
  may proceed, but its explicit timing-before-enablement requirement still applies.
- 4.1 verifies undo parity; source currency comes from the existing Loop publication
  owner and its wakeup/retry path, not from checking off the parity task.
- 4.2 needs an actual content-divergence oracle. A nextNoteId mismatch alone may be
  legitimate. Format provenance and load reconciliation are unresolved alternatives;
  neither is silently selected or treated as a prerequisite for unrelated work.
- 6.4's profile/oracle preparation is now done: `STIM-WINDOW-CONTENT-01@v1`
  (`windowed_content_parity`) is registered with its own recipe shape (a 16-bar loop
  against the two-bar span, transport running across boundaries) and its declared
  target authorization, and its `must_prove` names the F-7 / F-8 / AC-8 outcomes
  (`evidence/2026-10-05-s64-windowed-hitl-profile/`), and the profile is now **selectable**:
  its driver is the existing long-loop content-verification leg, not a second
  pipeline, paired with the windowed lane's own checks
  (`evidence/2026-10-05-s64-windowed-leg-driver/`). Qualification still waits for an
  **exercised** path: the leg has not run on a device, two declared terms are
  deferred to other owners (the parity verdict to the content pipeline; the
  residency term to an unidentified row set), and every verdict carries
  `oracle_complete: False` so a green run is not read as F-7/F-8 qualification.
  6.8 supplies evidence downstream to RWO acceptance.

The pre-edit [slice packet](evidence/requester-retry-integration/ROLLING_HANDOFF.md) names its exact selector, frame/state
lifetime, caller order, fallback intervals, cancellation and positive/negative traces.
It does not equate a single-track test with a smaller declared H factor: admission
continues to charge the existing H8 contract. Broader old-source reconstruction,
preview/undo enablement, cold-root latency and continuous timing stay explicitly open.

Verification: real request -> bounded worker -> ready candidate -> validated timing
boundary -> exact MIDI emission, with controlled clocks, no fabricated publication,
retained original demand under refusal, stale/late candidate rejection and fallback
continuity. Focused suites, full native, capture/RAM1, strict OpenSpec and diff checks.
No full-task checkoff or fallback removal from this slice alone.
