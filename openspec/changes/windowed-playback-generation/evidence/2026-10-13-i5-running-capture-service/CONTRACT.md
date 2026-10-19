# I5 — running-Capture request and composed service contract

**Status: proposed contract, saved with owner permission; not firmware authorization, G1/G2 approval, a new supported envelope, or task completion.** Request protocol is drafted; useful-progress certification, initial readiness and numerical composed service remain BLOCKED. No production/spec requirement change is made here.

## Authority and evidence

- [DEC-116 design contract](../../design.md#dec-116--single-rolling-playback-contract-design-target-no-firmware-checkoff): one Track request/build/adopt/emit driver; Loop owns Capture/checkpoints; existing three views; no new publisher.
- [Normative behavior](../../specs/windowed-playback-generation/spec.md): Record prepares through the ordinary owner; content does not change mid-consumption; accepted late content; retained preparation progress; clock revalidation; source-owner deadline service; non-starving opportunities.
- [Fresh fixture/work evidence](EXECUTION.md): main-order service cancels the live Record frame while bytes remain; live-source fill cost grows with inspected rows, not window output; accepted input after fill invalidates old ordering work.
- [Implementation readiness](../../../../../docs/Authority/IMPLEMENTATION_READINESS.md): architecture-changing UNKNOWNs block the implementation contract. This document does not replace an accepted architecture decision or execution packet.

Branch `feature/unified-rolling-playback-path` @ `26125ceb` plus preserved dirty tree. Default rolling remains OFF; compatibility emitters/tests, old admission constants, Capture acceptance and all parent checkboxes remain unchanged.

## 1. Owner, resources and proposed extension point

**Owner:** Track, using the slot's existing LoopPlaybackRuntime pending/frozen/admitted request records, builder, rolling frame and three-view lifecycle. Loop owns mutable Capture, its content token and sealed source; CaptureStopJob owns the frozen range after stop. TrackManager retains activeLoopIndex and sounding-slot selection. StorageManager and TrackUndo retain persistence/undo ownership.

**Preferred extension:** a live-Capture source recipe in PlaybackViewPendingRequest::SourceKind (provisional implementation name `LiveCapture`), resolved by Track against the identified slot's Loop. Existing kinds are Borrowed/CommittedSnapshot/CommittedLaunch; the proposed kind does not exist today. No raw caller-owned pointer is retained as the request. No new queue, scheduling owner, fourth view, whole-take copy or Track-global current is proposed.

Reuse existing identity/deadline fields where they express the complete contract. Any needed new resident field, source pin, source lifetime, transition or arbitration policy must be priced and explicitly decided before implementation; **no size-neutrality or existing-API sufficiency is asserted**.

## 2. Request identity and position

A request identifies the slot/runtime lease, Track operation generation, Capture interval/content token, origin Capture, projection geometry and coverage. Content identity and positional promise are separate. Source count/extent belongs to the qualified attempt, never inferred from copied output.

The positional promise is an absolute deadline qualified by clock epoch, projection origin, geometry and target traversal. A repeating phase cannot distinguish an expired traversal from a later lap. Epoch zero is unqualified position, not a production readiness exemption. Growing Record length prediction is not final LoopGeometry and must not invalidate a still-correct provisional playback period merely because display/record prediction changed.

**Two times must be derived:**

1. Earliest affected MIDI/closure tick under the chosen projection.
2. Last eligible adoption boundary that can install the replacement before that tick **without changing a traversal in progress**.

The second is the preparation deadline. A later affected event is not permission to adopt mid-traversal. Derivation must account for equal-tick Off-before-On and closure obligations, not just the first NoteOn. No modulo-equality shortcut may invent a full lap of extra lead.

**Existing fixture:** accepted On700/Off740, next first-bar traversal begins768, expected On1468/Off1508. At Off740 the legal adoption deadline768 leaves **28 ticks**; the first changed output1468 leaves **728 ticks**. Both values are recorded, but only28 is the current boundary-based preparation lead. This strengthens the interpretation of the earlier measurement; it does not change its assertions or the spec.

## 3. Request outcomes and coalescing

Use owner-local existing return/state/reason vocabulary; no universal status framework is proposed.

- **Retained/waiting:** request remains generation-qualified when source/resource/build/admission is unavailable; named reason explains the refusal. Content acceptance is not undone.
- **Admitted/preparing:** only the Track owner starts/resumes a qualified private candidate and retains the promised position/cost. A holder is offered because this request owes work, not because the track happens to be RECORDING.
- **Ready:** complete immutable candidate with matching source/coverage/token, fresh clock revalidation and satisfied source/closure requirements. Ready alone does not imply that a passed deadline was met.
- **Adopted:** the clock reader swaps only at the qualified boundary; cursor and ledger effects are exactly once.
- **Superseded:** newest content requirement coalesces while preserving the oldest still-applicable deadline, origin/epoch/geometry stamps and miss history. A newer request cannot refresh the lag allowance or erase an older owed interval.
- **Position invalidated:** epoch/seek/geometry changes prevent adoption under old stamps; accepted content remains owed and explicit revalidation establishes a new valid position without erasing fault history.
- **Stale operation:** old generation completion/release is rejected; it cannot publish or alias a recycled slot/runtime.
- **Miss/fault:** report the responsible source/request/build/adoption transition. First permitted old-version traversal is containment, never readiness PASS; a second consecutive miss uses existing named fault policy.

LoopPlaybackRuntime::retainViewPendingRequest already coalesces newest source/cost with coherent oldest position. Its current test for same operation/length is **not sufficient proof** of same affected coverage or projection. Live coalescing must preserve distinct owed coverage; no oldest interval may disappear behind a newest interval. Whether existing frozen+pending records suffice for every overlap is a design blocker, not permission to add an unbounded queue.

## 4. Useful work and source safety

Served current and complete ready are immutable; accepted input never relabels their old token. The live source factory currently snapshots count/token and invalidates the whole attempt on mutation. Fresh measurements prove stale ordering refusal, not reusable progress.

The final protocol **must preserve useful preparation toward the oldest obligation**, as required by the normative retained-preparation clause. It may reuse bytes/index/frontier/order only with a bounded certificate proving that the reused portion and source dependencies remain valid after the append. Safe provenance is stronger than observing that the old buffer looks unchanged. Stop/closure projection, tick rewrites, source replacement, clear/load and resource reuse must invalidate any certificate that no longer applies.

**Concrete mechanism proposed, not approved for implementation:** [I5 — append-stability/prefix-delta proposal](SOURCE-PROGRESS.md) specifies a Loop-owned stability generation separate from content currency, frozen attempt extents, retained source/copy frontiers and bounded delta-sort/merge/copy-back using existing private arrays. Current APIs do not implement it. Exhaustive mutation invalidation, state/resource pricing, stop/lifetime equivalence and acceptance fixtures remain required; existing frozen CaptureRange callbacks do not certify mutable-prefix reuse. Numerical service, distinct-coverage coalescing and initial anchoring remain open.

For any genuinely invalid work, account for validation, discard/retirement and replacement work. Charge no reuse credit before its certificate/fixtures exist. A full restart is an honest conservative cost characterization, **not an acceptable permanent substitute for preserving useful work** or a certified readiness path. Older deadline/content obligations survive supersession even if a newer candidate cannot satisfy them yet.

## 5. Main ordering and shared-builder ownership

Production path to cover is main's rolling-source service → deadline accounting → per-track offers → across-track round-robin → within-track owing holder selection → one Track maintenance grain → actual TrackManager edge.

A qualified live-Capture reader must not be cancelled solely because Track::isPlaying excludes RECORDING. Live request/reader qualification is source/slot/generation/epoch/coverage-specific; do not broaden isPlaying or grant blanket priority by mode. Calling maintenance directly with arbitrary grants is not production service evidence.

The legacy VisualCache first-bar worker may not mutate the Track-owned private live candidate. Its existing cancellation interaction must become identity-qualified and coherent with the request protocol, not a second publisher or an independent reason to restart useful work. Generic shared-builder occupancy, prepared ready, denied admission and release continuation must have explicit waiting/recovery outcomes.

## 6. Stop, cancellation and lifetime handoff

- **Matching stop:** qualify final geometry, frozen token/content, closure coverage and storage identity. Transfer source responsibility to the job-owned sealed range without copying current or replaying MIDI; preserve cursor/ledger. Complete nonempty matching-stop proof is still owed.
- **Mismatching stop:** keep only valid old traversal coverage, retain the sealed source, and prepare corrected ready through the same Track owner. No token relabel, stop-edge sort/scan/build or inline recovery. Existing mismatching-stop fixtures are reusable evidence, not proof of matching stop.
- **Source supersession:** retain the old audible bytes/source responsibilities until the eligible handoff; private stale completion cannot publish.
- **Clock/source transition/seek:** invalidate position and preserve content/miss obligation. Re-entry cannot revive old generation or mint another first-miss allowance.
- **Transport stop:** pause traversal; it does not forgive a miss or silently clear pending content. Explicit lifecycle cancellation releases only identities it owns.
- **Slot disable/exit, clear/load, undo/redo:** use existing generation-qualified owner transitions; close only affected soundings, cancel borrowed work before source destruction, reject late cleanup, and leave enabled sibling roles independent.
- **Release:** no reader, builder, ready/future-window or ledger closure may still depend on the source being retired. Root publication alone is not a release receipt. Keep the existing frozen job/source-retirement predicates and noLiveOwnerForSlot accounting coherent.

No SD/undo representation or ownership change is proposed. These routes require controlled fixtures before any lifetime claim; this draft does not declare them already GREEN.

## 7. Composed service accounting

Measure **remaining** work at acceptance/supersession and each resumable continuation, not just a final builder total. Include qualification/count/index inspection, useful-progress validation, discarded work/restart, fill, order/rank, closure preparation, begin/binding, promotion/publication, required retirement, same-track owing work and competing owners.

For a conservative serial dependency bound use:

`B_source + (G_window_remaining + K_track) * H_window * Delta_window + M <= D_adoption`

- B_source: remaining prerequisite owner work converted to ticks using those owners' demonstrated bounded service gaps. Live already-readable Capture does not owe a whole Snapshot solely for playback; frozen stop may owe closure/source readiness. Price each actual dependency.
- G_window_remaining: all remaining charged Track grains, including begin/publication if separate opportunities. Count a fused operation once only when its indivisible bound and actual call path prove fusion.
- K_track: other admitted same-track work that can consume this obligation's turns. Count actual work, not a fixed number-of-slots multiplier.
- H_window: across-track contention from supported eligible sounding roles; do not silently reduce today's eight-track workload to one fixture track.
- Delta_window: permitted/enforced tick advance between lane opportunities, already including intervening deferred owners. **Do not multiply outer owner rotation again** if included here. If measuring per-holder gaps instead, charge them directly rather than also multiplying H.
- M: separately justified residual revalidation/adoption margin, not a universal extra bar and not automatically zero.
- D_adoption: absolute eligible-boundary deadline minus current qualified tick; a passed deadline is a miss, not modular future lead.

This sum is a conservative serial upper bound, not an exact latency or assurance that independent owners progress in parallel. Any overlap credit needs a dependency/interleaving proof. Preserve other owners' bounded opportunities; no new scheduler, mode-wide priority or unmeasured urgency rule is assumed.

**Current evidence:** builder calls for0/2/4/66/130 source rows are2/3/3/4/5 (begin + fill + order); output0/2/2/2/2. Publication/source closure/retirement/competitors are excluded. Current declared empty two-grain arithmetic is1792ticks with the transitional768tick margin, or1024 with that margin omitted. Neither meets28ticks. Even the observed two-row builder's three calls, optimistically before any additional publication/dependency/margin, need1536ticks at H8/Delta64. These are incompatible bounds, not a reason to reduce H, delete lead or claim missing work is free.

**Numerical contract: BLOCKED.** No accepted replacement Delta, denied-turn limit, residual margin, incremental-work bound or device-qualified composed envelope exists. The implementation must prove lower remaining work and/or stronger bounded service while preserving accepted content, legal handoffs and all admitted owners—not fit a threshold to this failing trace.

## 8. Initial running-epoch readiness

Device epoch is nonzero even while stopped. Do not substitute epoch-zero for stopped transport or allow epoch reset as a test prerequisite.

An initial silent interval is not a changed successor, but it still needs a qualified source, complete immutable publication and no reader of the raw shell. The initial request must be explicitly retained and wait for real readiness before its new reader is bound; accepted Record input must not depend on that build succeeding. Native epoch1 initial fixture is independently RED. Initial publication timing/anchor and how pre-start/ongoing Record source changes preserve the obligation must be pinned before this becomes an implementation slice. Zero deadline may mean unpositioned waiting only, never sustained-running admission bypass.

## 9. Proposed implementation boundary (not ready to execute)

**Primary files if approved:** TrackPlaybackWindowBuild.cpp (request/holder/owner/rolling service), TrackPlaybackRuntime.h/.cpp (existing request and view lifecycle), RuntimePlaybackViewFill/Build (only the decided source-progress certification), TrackCaptureInput/stop lifecycle where the proven handoff requires it; existing ownership/finalization/TrackManager fixtures. main.cpp only for an explicitly decided and verified existing-owner arbitration change. No firmware edit is authorized here.

**Preconditions:** complete source-progress mechanism and resource/lifetime decision; exact initial request anchor; composed numeric service contract and failure behavior; fixtures control refusal/recovery, clock advancement and competing owners. Current evidence does not satisfy these.

**Required oracles:** all three independent gated failures; On/Off between fill/order/publication; oldest-deadline coalescing with distinct coverage; main/VisualCache shared-worker interleavings; ready blocked/retirement; matching and mismatching stop with root-before-edge; clock/slot/undo/load cancellation and stale reuse; one/four/64-bar identical requested intervals; eight sounding roles plus admitted source/finalization/persistence/display work; no edge build, allocation or full scan; exact MIDI/ledger and source release.

**Verification:** fixture-first pre-fix failures/control restorations; focused and full native; default firmware build/RAM1 with no rebase and priced PSRAM/LoopPool roles; one focused lifecycle/oracle review; registered managed, safely authorized hardware qualification for real epoch/service/content parity. Tests, build and device evidence remain separate. Default rolling stays OFF and old emitters remain until all-route cutover gates pass.

**Commit boundary:** separately reviewable source/request protocol and service enforcement only after their dependency closure is complete; do not ship request wiring as if it solved the28tick oracle. No cyclic gate or absent incremental prerequisite may be assumed implemented.

## Readiness verdict and documentary scope

Owner/resource direction and outcomes: proposed, grounded in existing machinery. G1 — architecture contract: BLOCKED on useful-progress/source-lifetime certification, overlap representation and initial anchor. G2 — execution contract: BLOCKED on these dependencies and composed numeric service. No new implementation tasks, decisions, policy values or checkoffs are generated by saving this draft.

Owner approved saving this file and links in design.md/tasks.md only. No firmware, tests, runtime journals, authority/DEC, project plan, build, hardware or Git delivery changed in this document-only turn. The prior native results are cited as prior evidence, not rerun results. Next is resolving the listed contract dependencies; merely removing epoch/lead guards or treating an affected MIDI tick as a mid-traversal adoption deadline is explicitly rejected.

**2026-10-13 documentary refinement:** owner separately approved saving SOURCE-PROGRESS.md and updating this contract, design/task links and runtime state/scope. The source-progress proposal is not firmware/design-gate approval and does not change requirements, policy, size pins or checkoffs. Prior358/358 focused discovery evidence is linked, not a new implementation test result.

**Host-side closure attempt:** [I5 — candidate pricing/mutation/service evidence](STATE-RESOURCE-SERVICE.md) prices a64B target/72B host runtime extension and8B Loop counter, with no pin change. Lifetime audit maps concrete missing boundaries rather than claiming exhaustive runtime coverage. A production round-robin witness falsifies the28tick deadline under current H8/Delta64 even for one post-append grain, without adding source costs/margin.591focused cases pass; no firmware/HITL action. G1 — architecture closure and G2 — executable service contract remain blocked before hardware on those dependency/policy decisions.
