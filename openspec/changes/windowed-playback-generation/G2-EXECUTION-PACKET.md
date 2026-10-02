# G2 execution packet — `windowed-playback-generation` slice 3.1 (the window authority owner)

Authority: [`IMPLEMENTATION_READINESS.md` §G2 execution packet](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
and its [`EXECUTION_PACKET.md`](../../../docs/Templates/EXECUTION_PACKET.md) template. This file is the change's
change-level packet for the **owner slice** (task 1.5 of `tasks.md`); one section per further slice is added beside
it. It adds no contract: where a field already has an owner it links to it.

**Status: specified (this document). Nothing here is fixture-implemented, native-verified or device-verified.** No
firmware, `src/`/`include/` or test change has been made for this slice. Every "expected" below is an oracle to be
frozen, not a result.

**Predecessor gate:** task 1.2 — the decision review
([`DECISION_REVIEW.md`](DECISION_REVIEW.md)) is complete and its only blocker (**F-1**) does not reach this slice.

---

## Identity and scope

- **Packet/task IDs:** `windowed-playback-generation` task **3.1** (owner first). Approved contract:
  [`DEC-108`](../../../docs/DECISION_LOG.md#dec-108--windowed-playback-generation-one-publication-owner-window-sized-reservation-pinned-source-retention-and-readiness-as-completed-publication)
  item 1 and §13(b); [spec](specs/windowed-playback-generation/spec.md#requirement-one-off-clock-owner-acquires-replaces-and-publishes-windows)
  *One off-clock owner acquires, replaces and publishes windows* and
  [spec](specs/windowed-playback-generation/spec.md#requirement-deferred-playback-service-is-fair-bounded-and-deadline-ordered)
  *Deferred playback service is fair, bounded and deadline-ordered*;
  [`ExecutionBounds.md`](../../../docs/Authority/Architecture/ExecutionBounds.md) rows 27–29 and the
  `CoverageServiceContract` row (line 202). Contract details: [`design.md`](design.md) D2 and §"Recorded lifecycle and
  recovery policies" 2.
- **Owner and production entry → deferred worker → commit/retirement:** the entry is the existing post-MIDI deferred
  enumeration in `src/main.cpp` (`runDeferredIdleMaintenance`, dispatched after the MIDI service boundary); the
  deferred worker is the new turn body `Track::stepPlaybackViewMaintenance`; **this slice has no commit and no
  retirement** — no consumer binds to the owner yet, `TrackManager::servicePendingSlotProvision` keeps requesting and
  reading, and the commit binding in `TrackManager::updateAllTracks` is untouched.
- **Primary files:** `include/Utils/MidiServiceDrain.h` (the eligibility predicate beside `slotProvisionNeeded`, and
  the owner enum), `src/main.cpp` (the turn's own block in `runDeferredIdleMaintenance`), `include/Track.h` +
  `src/Track/TrackPlaybackWindowBuild.cpp` (the turn body, next to `Track::ensurePlaybackMergedEventsForSlot` and
  `Track::noLiveOwnerForSlot`), and — read-only for the contract — `include/RuntimePlaybackViewBuild.h` /
  `src/Track/RuntimePlaybackViewBuild.cpp` (`CoverageServiceContract`), `include/TrackPlaybackRuntime.h:660` /
  `src/Track/TrackPlaybackRuntime.cpp:1577` (`beginCoverageBuild`).
- **Existing test harness:** `test/test_midi_service_drain` (pure selection/policy predicates, including the current
  `slotProvisionNeeded` truth table and the rotation counts), `test/test_playback_prewarm` (the view set,
  `CoverageServiceContract`, `beginCoverageBuild`, and the declared/denial cases at lines 1116–1125 and 6174+),
  `test/test_runtime_working_set_ownership` (the harness that drives a real `TrackManager` turn end to end via
  `test_support/track_harness.inc`), `test/test_continuous_capture`.
- **Preconditions already present:** `CoverageServiceContract` with `isDeclared()` / `requiredGrains()` /
  `canCompleteBeforeHandoff()` (deterministic denial, 64-bit arithmetic, an *undeclared* contract denies);
  `RuntimePlaybackViewBuild`'s separate fill/order grains and its persistent phase cursor; the view set's build role
  and `LoopPlaybackRuntime::beginCoverageBuild`; `DeferredIdleOwner::SlotProvision` as the precedent for a turn
  served **before** the stopped/playing split (`src/main.cpp:449-455`, DEC-103); the loop-length/bar primitives
  (`Config::TICKS_PER_BAR` = 768, `INTERNAL_PPQN` = 192).
- **Prerequisites included in this slice:** the eligibility predicate, the owner turn kind, the dispatch block, the
  turn body, the declared contract values with their derivation recorded, and the fixtures below. **External
  blockers: none.**
- **Mutation, postconditions and explicit non-goals:** the change *adds* an off-clock owner that advances one
  declared grain for a window it is asked to prepare, and *declares* its service contract; it must leave every
  existing consumer unchanged. **Non-goals:** no interval publication consumed by any consumer; no change to
  `Track::isPlaybackMergedMidiEventsReadyForSlot`; no change to `TrackManager::updateAllTracks`; no removal of the
  live merged-window build (`ExecutionBounds` row 2 stays **Unbounded**); no new buffer or storage; no whole-take
  reservation for a demoted slot; no new DTCM state; no `LoopPlaybackRuntime`/`Loop` size-pin re-base; no SD or
  persistence format change; no change to `SlotProvision`; **no behaviour enabled behind the owner** — rows 27–29
  keep active windowed playback denied until the caps are implemented *and proven*, so this slice lands first and
  inert, with task 3.2 as the first slice that consumes what it publishes.
- **Commit boundary:** one commit on the working branch `feature/runtime-working-set-ownership`, independently
  buildable, with the fixtures in the same commit. No PR without an explicit request.

### The one code-shape choice this packet makes

`design.md` §"Open Questions" leaves the wiring open ("a turn kind versus a service point inside an existing turn").
**This packet picks the turn kind**, and records why:

| Option | Verdict |
|---|---|
| **A new `DeferredIdleOwner` value + its own dispatch block** (chosen) | Matches DEC-108 item 1 ("carried by the existing post-MIDI deferred machinery"), §13(b)'s round-robin over holders, and the `SlotProvision` precedent (DEC-103 added enum value 5 and `kDeferredIdleOwnerCount` 6 for exactly this reason). It is the only shape in which *this* owner is selected, denied and cursor-advanced independently of visual-cache or snapshot work. |
| A service point inside an existing turn | Rejected: the `Snapshot` and canonical-prefix blocks run one child and return (`src/main.cpp:542-604`), and the stopped branch routes through `Track::processDeferredIdleMaintenance` — so window preparation would inherit another owner's eligibility, its stopped/playing split and its cursor, which contradicts "the owner that requests a preparation SHALL not also publish in the steady state" and §13(b)'s per-holder deadline order. |

**Consequence to accept:** adding an owner changes the round-robin period (`kDeferredIdleOwnerCount` 6 → 7) and the
existing `test_midi_service_drain` rotation fixture's expected per-cycle counts change by the added turn. That fixture
must be updated **in this slice with the reason recorded at the assertion**, and it is the control that proves the new
turn does not displace or starve the other six.

---

## Oracle fixed before behavior edits

The initial state for every case is a harness-constructed `TrackManager` with one track, a prewarmed view set for the
slot under test, and a window-preparation request established through the **production** requester
(`TrackManager::servicePendingSlotProvision` / `Track::ensurePlaybackMergedEventsForSlot`), never by poking owner
state.

| Case | Initial state | Ordered actions | Expected observation after each step | Completion / fault | Must fail if |
|---|---|---|---|---|---|
| **W-1** eligibility is state-driven | no window owed on any slot | evaluate the predicate for {no request}, {request, prepared}, {request, unprepared} | true **only** for {request, unprepared}; the predicate reads no clock and no queue | — | the predicate answers true with nothing owed, or false while a window is owed |
| **W-2** reachability in the states a pending window sits in | a pending window obligation, one of: PLAYING a demoted slot / RECORDING (first-record bar) / transport STOPPED / playing while a switch is queued | one owner turn per state, with the owner selected by the production selector | the owner's block runs and reports exactly one admitted grain in each of the four states; the other six owners' per-cycle counts are unchanged | no request consumed, no consumer touched | the turn is only reachable when everything is stopped, or a state with an obligation gets no turn |
| **W-3** one declared grain per admitted turn, cursor persists | a multi-grain obligation | two consecutive owner turns, then a third after an interleaved unrelated owner turn | each turn advances ≤ the declared quantum; the persisted cursor moves by that amount and no more; no second grain stacks inside one turn | the build stays incomplete until its required grains have been served | a turn advances more than the quantum, resets the cursor, or stacks two grains |
| **W-4** denied turns are bounded | a contract that cannot complete before its handoff | repeat owner turns until the bound is reached | each denial is non-destructive and named; the consecutive-denial count stops at the declared bound; the request stays pending | at the bound the named refusal is reported with its holder | a denial consumes the turn as if it progressed, or the bound is unbounded/exceeded silently |
| **W-5** handoff margin is the oracle | a window obligation with a known phase distance to its handoff | admit at, just inside, and just outside the declared margin | admitted inside the margin; **denied** outside it; the denial names the contract, not a bar count | — | a bar count or a millisecond constant is used instead of `canCompleteBeforeHandoff`, or an undeclared contract is admitted |
| **W-6** the edge allocates nothing (S4b.6/S4b.7 shape) | a prepared window and a playing track | sample the event-pool chunk count immediately before and after the admitting turn | the pool is identical across the turn; no lease is taken or released by the turn itself | — | the turn takes capacity, opens a block, or takes/releases a working-set lease |
| **W-7** requesting is not publishing | a pending switch with no inbound occupancy, and a pending window | serve a `SlotProvision` turn and an owner turn in both orders | `ServicePendingSlotProvision`'s observable behaviour is byte-for-byte what it is at the baseline revision; the owner turn publishes nothing to any consumer | — | the owner turn is what provisions a switch, or `SlotProvision` starts publishing |
| **W-8** counterexample (the discriminating control) | as W-2 | *mutations of the implementation, one per red-proof*: (a) remove the dispatch block; (b) ignore the declared quantum; (c) count a denial as progress; (d) move the block below the stopped/playing split | each mutation must fail exactly the case that owns it — (a)→W-2, (b)→W-3, (c)→W-4, (d)→W-2's PLAYING case — and the file must be restored byte-identically | — | a mutation leaves all cases green (the RWO-recorded harness tripwire: dropping the window build once left every fixture green) |

**Preserved invariants, asserted in every case:** one content-currency authority (no new revision counter); one
active-slot authority (`activeLoopIndex`); the published root keeps checkpoint authority; no consumer read changes; the
RAM1 line does not grow; the six existing deferred owners keep their own opportunities.

---

## Controls and coverage

- **Seams reused:** the shipped native harnesses — `test_support/track_harness.inc` (real `Track`/`TrackManager`, real
  admission/lease/storage paths) and `test_support/capture_storage_harness.inc` — plus the pure policy suite for the
  predicate. Ticks and owner turns are driven explicitly; **no sleeps, no probabilistic stress, no copied state
  machine**, and no on-path stub that returns success.
- **Production paths exercised:** `Track::stepPlaybackViewMaintenance` itself, the real requester
  (`servicePendingSlotProvision`), the real `CoverageServiceContract` admission, the real view build's fill/order
  grains, and the real allocator/pool accounting for W-6.
- **Known untestable-before-link case, stated rather than worked around:** `src/main.cpp` is **not native-linkable**
  (`build_src_filter = -<src/*>`; the two harnesses that record this are
  `test/test_continuous_capture/test_continuous_capture.cpp:4928` and
  `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp:2659`). So the *dispatch placement*
  (the turn served before the stopped/playing split, like `SlotProvision`) is pinned in two parts: the pure selection
  predicates in `test/test_midi_service_drain`, and the owner-turn body through the harness. The **placement itself**
  is then verified by (a) the build, (b) the existing owner-rotation fixture, and (c) a device run whose row names the
  turn — recorded as a device claim, not a native one. This limitation is recorded here because it is exactly the
  "missing symbol/build error is not red behavioral evidence" case.
- **Baseline failure evidence, honestly:** a *pre-fix* behavioural run is impossible for a method that does not exist
  yet, so the required red evidence is a **post-link mutation proof** (W-8), not a pre-fix failure. W-8's four
  mutations are the discriminating assertions; each is restored byte-identically and the restore is verified.
- **Independent expected values:** the contract's numbers are **derived at the point of declaration** from measured
  grain costs and the worst permitted service gap (DEC-108 §13(b): no microsecond budget is invented here), and the
  derivation is recorded beside the declaration and in the completion record. The fixture's expectations come from the
  declaration and from explicit expected traces (W-3/W-4/W-5), never from the implementation's own output.
- **Bounded drain:** every case steps a fixed maximum number of owner turns and fails with the last owner/phase state
  when it exhausts that limit. No fuzz seed applies.

---

## Verification recipe

Focused:

```bash
pio test -e native -f test_midi_service_drain          # W-1 + the rotation control
pio test -e native -f test_playback_prewarm            # W-3/W-5 against the existing contract fixtures
pio test -e native -f test_runtime_working_set_ownership   # W-2/W-4/W-6/W-7 through the production harness
```

Broad, and required before checkoff:

```bash
pio test -e native                                      # full suite; every case passes, no skipped/errored suite counted as green
pio run -e teensy41-capture-serial                       # build only; RAM1 budget line reported, floor 840 B
python3 scripts/check_ram1_budget.py                     # budget passes on the fresh target build
git diff --check                                         # whitespace
nvm use 20 && openspec validate windowed-playback-generation --strict
```

- **Pass criteria:** the focused suites green with the new cases *present by name*; full native with **0 failures and
  0 errored suites** and the pre-existing suites' counts unchanged except the deliberately updated rotation fixture;
  the firmware build succeeding through the RAM1 guard with the budget line reported and `padding` at or above the
  floor. A missing suite, a compile error, or "0 failures" with skipped suites is **not** a pass.
- **Device claim (this slice):** none mandatory. If the turn's reachability is to be claimed on hardware, it is a
  read-only observation under the existing content-verification entry point with a session capture that emits the
  turn's name; **no upload without asking**, no new stimulus, and no SD operation.
- **If measurement is taken** (grain cost / service gap, for the declaration): state inputs (loop length, density,
  tempo, quantisation), units, attribution to the owner's own row, and name the declaration the numbers consume.
- **Evidence destination:** `openspec/changes/windowed-playback-generation/evidence/<date>-s31-owner/` plus this
  packet's completion record. **Baseline revision and dirty-tree state must be recorded with the run**, because the
  working tree already carries unrelated uncommitted work.

---

## Completion record

Nothing below is done. Each line is the field to fill, and each must be a real command/result, never a planned one.

- Oracle/contract unchanged, or authorized change and supporting evidence: **NOT RUN** — the oracle above is not yet
  frozen in code.
- Fixture implemented and discriminating assertion demonstrated: **NOT RUN** — W-1…W-8 to be established with the
  slice (fixture-first), then behaviour implemented, then the mutations of W-8 red-proved with byte-identical
  restores.
- Native verification: **NOT RUN** — no focused or full native run has been made for this slice.
- Build/RAM1 verification: **NOT RUN** — no `teensy41-capture-serial` build and no budget check for this slice.
- Device verification: **NOT APPLICABLE to checkoff** — this slice enables no behaviour and therefore makes no device
  claim; a device row naming the turn is optional evidence, not a gate.
- Required reviews and durable docs: **NOT RUN** — task 1.2 landed ([`DECISION_REVIEW.md`](DECISION_REVIEW.md), with
  its F-1 blocker); the docs this slice touches (`docs/Runtime/CURRENT_WORK.md`,
  `docs/Authority/Architecture/ExecutionBounds.md` rows 27–29) are updated at checkoff, not before.
- Task IDs eligible for checkoff when the above are green: **3.1 only**; task 3.2 (interval publication) is the first
  consumer and is where F-1 must be resolved. Task 2.4's "nothing allocates or blocks on the edge" fixture and task
  2.8's red-proof are satisfied *by* W-6/W-8 for this slice.

**Carried tightening items, explicitly not claimed here** (DEC-108 §12: "required contents of the first execution
packet that touches their path"): deferred-service **fairness** across tracks and competing owners lands with slice
**3.10**; the **shared-view and cancellation transition table** with slice **3.11**; **second-miss post-breach
behaviour with cross-generation ledger reconciliation** with slice **4.5**. This packet fixes only the owner's own
bounded single-grain turn and its reachability.
