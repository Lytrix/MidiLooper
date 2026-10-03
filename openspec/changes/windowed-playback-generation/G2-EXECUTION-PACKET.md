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

**Slice 3.1's record (2026-10-02).** The owner, its declared contract and its fixtures **landed and are green on
the host and through the firmware gate**. Full detail and the raw numbers: [`evidence/2026-10-02-s31-window-owner/EVIDENCE.md`](evidence/2026-10-02-s31-window-owner/EVIDENCE.md).

- Oracle/contract unchanged: **YES** — no case was weakened; the declaration is pinned value by value by W-5.
- Fixture implemented and discriminating assertion demonstrated: **YES** — W-1…W-8 landed as
  `test_playback_view_maintenance_turn_work_is_an_unfinished_build` plus the owner-rotation control in
  `test_midi_service_drain`, and three owner fixtures in `test_runtime_working_set_ownership`. Four mutations
  red-proved with byte-identical restores (sha256 verified): a served grain reported `Idle`; the declared quantum
  ignored; the handoff-margin gate removed; the obligation claimed unconditionally.
- Native verification: **GREEN** — `test_midi_service_drain` **38/38**, `test_runtime_working_set_ownership`
  **48/48**, `test_playback_prewarm` **190/190** (unchanged), full native **2679 cases / 2672 succeeded /
  7 skipped / 0 failed** (baseline 2675 → +4 cases).
- Build/RAM1 verification: **GREEN** — `teensy41-capture-serial` **SUCCESS** with the approved line unchanged:
  `variables 96,192 / ITCM 393,216 (code 392,008 + padding 1,208) / locals 34,880`, floor 840, 368 B of headroom.
  **No re-base** of `scripts/ram1_budget.json` was needed or made. The first build of this slice *did* fail the guard
  (`+4,096 B`) and the cause is now named: three `switch` statements in the owner emitted 16–24 B jump tables into
  `.data`, and a few added `.data` bytes snapped the following `ALIGN(1024)`/`ALIGN(4096)` — the RAM1 rule's own
  capture-tag finding. The fix was to add **zero `.data`**: no name table, an if/else chain instead of a `switch`, and
  the refusal contract carried as the typed outcome enum (rendered as a name where the row is reported).
- Device verification: **NOT APPLICABLE** — this slice enables no behaviour and makes no device claim.
- Required reviews and durable docs: **task 1.2 landed** ([`DECISION_REVIEW.md`](DECISION_REVIEW.md)); the durable
  docs (`docs/Runtime/CURRENT_WORK.md`, `ExecutionBounds.md` rows 27–29) are updated at checkoff, not before.
- Task IDs eligible for checkoff: **3.1** (all four of its listed clauses have a green check: quantum, persistent
  build cursor, the declared denial allowance consumed by the admission, and the demonstrated handoff margin; it is
  reachable in the states a pending window sits in; and no new scheduling owner was introduced). Task 2.4's "nothing
  allocates or blocks on the edge" and task 2.8's red-proof are satisfied *for this slice* by W-6/W-8.

## Slice 3.2 (part) — the owner's atomic interval-window publication (2026-10-02)

Landed and verified: `RuntimePlaybackViewLifecycle::publishCompletedBuildAsCurrentWindow` (a demoted slot's first
window had **no** publication path — `publishCompletedBuildAsReady` needs a retained/idle successor it cannot
have), the owner publishing inside the completing turn, `Track::publishedPlaybackWindowForSlot`, and the named
`PlaybackViewRequestResult::WindowAlreadyPublished` refusal for a second window. **Readiness, identity on
consumers and the consumer switch are untouched** (3.5/3.4/3.7).

- Fixture: `test_window_owner_publishes_the_interval_window_atomically` — nothing published before a request; an
  incomplete build is never published (checked per turn); publication lands in the completing turn; the window
  carries its own identity and coverage; the slot is demoted; the second window is refused by name and the
  published window is unchanged by that refusal.
- Red-proofs: removing the publication call → *"the completed build must be published in that same turn"*;
  removing the `WindowAlreadyPublished` guard → *Expected 3 Was 0*. Both restored byte-identically.
- Green: this suite **49/49**, `test_midi_service_drain` 38/38, `test_playback_prewarm` 190/190, full native
  **2680 / 2673 succeeded / 7 skipped / 0 failed**, and `teensy41-capture-serial` **SUCCESS** with `variables
  96,192` (the approved line) and padding 1,080 ≥ the 840 floor.
- **Second increment (same day) — the enumeration clause and fixture 2.10.** `Track::playbackWindowInterval` is
  the **interval policy as one pure function** (whole-loop coverage where the whole-take view is admissible, else
  the two-bar window at the playhead, clamped into the loop — the shape the live build already uses), and
  `Track::requestPlaybackWindow(slot, phase, scratch)` lets the caller name a **slot and a phase, never events**:
  it enumerates through `Loop::tryGatherPublishedSnapshotEventsInWindow` over the selected `LoopSnapshot`,
  refuses a rootless slot (`SourceUnavailable`) or a non-current root (`SourceNotCurrent`) by name, and stamps
  `{playbackRevision, playbackGeneration, publishedSnapshotIdentity}`. `Loop`'s gather gained an optional
  **inspected-work** out-parameter (every membership reference it looks at, rejected ones included) and fixture
  2.10 asserts it: the same window costs identical inspected work on a 4-bar and a 64-bar loop, and 0 when its
  bars carry no membership. Red-proofs: the policy made to ignore demotion (*Expected 4232 Was 0*); the counter
  removed (*"the short loop's window has content"*). Full native **2681 / 2674 succeeded / 7 skipped / 0 failed**,
  build `variables 96,192` unchanged. **Task 3.2 is checked.**
- **Still open, named rather than claimed:** an ordinary loop's whole-loop interval is refused by the declared
  contract's **unmeasured** per-turn phase advance (task 6.3's measurement), and *replacement* sequencing is
  3.4/3.5's successor lane; and the measured finding that a second `begin` on a slot with
  a published window surfaced the new identity before its build completed — **unattributed**, and made moot for
  this slice by the refusal; the successor lane (3.4/3.5) must sequence replacement through
  `handoffReadyCoverage`. Detail: [`evidence/2026-10-02-s32-interval-publication/EVIDENCE.md`](evidence/2026-10-02-s32-interval-publication/EVIDENCE.md).

## Slice 3.3 — the representation is budgeted (2026-10-02)

Measurement/accounting only: **no firmware, test or build change**, so the verification below is *reused* from the
identical code state (the only edits are the artifact and the docs).

- **Five role names, three buffers:** `current`, `build`, `ready`, `retained`, `idle` — and `retained` always
  aliases `current` or `build` (`retainCurrent`, `handoff`, the set's `resetMetadataPreserveRetained`), while
  `idle` is a sentinel. The publisher this change added uses `current + build` only, and refuses to recycle the
  retained view, which is that invariant enforced where a buffer changes hands.
- **No buffer added, and no retention buffer needed:** DEC-108 item 4 makes the retained form a **pinned source**
  (3.6's term), so the one-pass-late content is not a fourth window; the windowed lane reserves no bridge/base and
  therefore has no transition overlap to budget; staging is the holder's existing two capture stagings. The pins
  prove the "no new state" claim rather than asserting it: `sizeof(LoopPlaybackRuntime) == 6368`,
  `sizeof(Track) == 384` and RAM1 `variables 96,192` all unchanged.
- **The account:** the windowed holder is "three views + two stagings" = 215,040 + 118,784 = **333,824 B (326.0
  KiB)** on the target ABI, from RWO S2C1's already-verified 35/29 B-per-event (cited, not re-derived).
- **`test_playback_prewarm` unchanged (190/190)** and cited rather than restated — it already pins the per-view
  capacity, the whole-take reservation's presence/absence and `fixedExternalBytes()` shrinking without it. **No new
  fixture**, deliberately.
- Detail: [`S33-REPRESENTATION-BUDGET.md`](S33-REPRESENTATION-BUDGET.md).

**Carried tightening items, explicitly not claimed here** (DEC-108 §12: "required contents of the first execution
packet that touches their path"): deferred-service **fairness** across tracks and competing owners lands with slice
**3.10**; the **shared-view and cancellation transition table** with slice **3.11**; **second-miss post-breach
behaviour with cross-generation ledger reconciliation** with slice **4.5**. This packet fixes only the owner's own
bounded single-grain turn and its reachability.

## Slice 3.4 — the window identity is carried, and a replacement cannot be straddled (2026-10-02)

F-1's three clauses are **asserted rather than asserted-about**, and the successor lane the 3.2 refusal stood in
for now exists. Detail and raw numbers: [`evidence/2026-10-02-s34-window-identity/EVIDENCE.md`](evidence/2026-10-02-s34-window-identity/EVIDENCE.md).

- **What landed:** `PlaybackWindowIdentity` `{slot, playbackGeneration, playbackRevision, interval}` (equality over
  exactly those fields; the snapshot identity stays provenance *inside* `PlaybackViewSourceIdentity`);
  `Track::publishedPlaybackWindowIdentityForSlot` (const, returns a **copy**);
  `RuntimePlaybackViewLifecycle::publishCompletedBuildAsReadySuccessor` (the successor lane — shell-tolerant,
  keeping "one **real** successor at a time" and "only a complete view is ever installed", touching nothing else);
  `Track::adoptPreparedPlaybackWindow` (one atomic `handoffReadyCoverage(Preparation)`); and the owner's completed
  build now **routes** — no window → `current`, a window being served → the `ready` successor.
- **The refusal changed with the routing:** `WindowAlreadyPublished` → **`SuccessorAlreadyPrepared`**. A window
  merely *being served* is not a bound; a second *prepared* successor is. This **resolves** the 3.2 measured
  finding (a second `begin` surfacing the new identity before its build completed) instead of working around it.
- Oracle/contract unchanged: **YES** — DEC-108 items 1/4/5 and F-1's three clauses; no case weakened.
- Fixture + discriminating assertions: **YES** — both owner fixtures. Identity is the real resolver's, and a second
  request at the **same revision and the same ownership but a different interval** leaves generation **and**
  revision unchanged while the interval moves (DEC-108 item 5's separation). The served window is asserted
  identical field-for-field **before every preparation turn**, unchanged once the successor is ready, and swapped
  only by the adoption.
- Native verification: **GREEN** — focused **50/50**, `test_midi_service_drain` **38/38**, `test_playback_prewarm`
  **190/190** (unchanged, cited), full native **2681 cases / 7 skipped / 2674 succeeded / 0 failed**.
- Build/RAM1 verification: **GREEN** on a **clean rebuild** (all TUs recompiled, 70.8 s — not a cache hit):
  `teensy41-capture-serial` **SUCCESS**, `variables 96,192 / ITCM 393,216 (code 392,200 + padding 1,016) / locals
  34,880`, floor 840, 176 B of headroom. **No re-base**; the new code is `TRACK_COLD_MEM` (FLASH), so no `.data`
  and no ITCM code moved.
- Device verification: **NOT APPLICABLE** — no consumer-visible behaviour is enabled in this slice.
- Red-proofs: **two**, each restored byte-identically with the sha256 verified: the successor routing reverted to
  "always replace" fails *"the served window must not change while its successor is prepared"* and *"preparing a
  successor must leave the served identity untouched"* (the 3.2 symptom, reproduced by mutation); the shell
  tolerance reverted fails the successor publication's own expectations (*Expected TRUE Was FALSE*, twice).
- Task IDs eligible for checkoff: **3.4** (identity carried, F-1 passes, the generation guards ownership rather
  than becoming a second currency) and **2.1** (F-1's three clauses). **2.10** was checked off on 3.2's record.
- **Still open, named rather than claimed:** the adoption edge is **not wired into production** (3.7 owns when a
  boundary is eligible) and **readiness is still 3.5**'s predicate — nothing a consumer reads changed here.

## Slice 3.5 — readiness is the completed publication, and its failure names its holder (2026-10-02)

**Scope.** AC-3's **readiness predicate** over the window this change publishes (3.2/3.4), its **named holder**, and
the removal of "empty ⇒ not ready" from readiness. Readiness answers *does the window for this slot exist, is it
complete, and is it current?* — the published window, never the live merged buffer.

**Non-goals, named rather than implied.** (1) The **consumer switch** does not happen here: the switch commit's
hold term (`TrackManagerTransportTick` → `isPlaybackMergedMidiEventsReadyForSlot`) keeps the legacy merged-buffer
term, because **nothing in production requests a window yet** — `requestPlaybackWindow` has no production caller —
so flipping the consumer now would leave every demoted target's switch pending forever. That request and the flip
are 3.7's, and the measurement that establishes it is red-proof (b) below. (2) The **one-pass-late** allowance
(AC-5) is 3.6's: 3.5's currency is exact, and an older published revision is `OwesCurrency`. (3) No retention, no
interval index, no adoption wiring.

**Transition oracle** — W-9…W-12 are the predicate's own cases (AC-3's "the predicate's own fixtures, including the
'missing ⇒ named owner' case"), W-13 is the commit path. Each names the mutation that must fail it.

| # | Transition | Discriminating assertion | Mutation that must fail it |
|---|---|---|---|
| W-9 | A current published root, a prewarmed runtime, **no window published** | `OwesBuild` + holder `window_build_owner` — the window-build owner owes it | answering from the live merged buffer (red-proof (a)) |
| W-10 | The same slot with **no published root** (rootless), and a not-current root | `OwesSource` + holder `published_source` — the *source* owes the root, and that is not the build owner's debt | folding `OwesSource` into `OwesBuild` |
| W-11 | A window published for an interval that **holds no events at all**, at the loop's current revision | `Ready` — and the same slot read through the legacy merged term is *not* ready, so "empty ⇒ not ready" is gone from readiness | answering from the live merged buffer (red-proof (a)); a readiness clause counting events |
| W-12 | A window published, then the loop's revision advances (a later pass commits) | `OwesCurrency` + holder `window_rebuild` — currency is `playbackRevision`, and exactly it | comparing the generation instead of the revision; accepting any published window as current |
| W-13 | The demoted target through the production switch path (RWO's two fixtures, re-pointed) | the switch's commit is still served by the **legacy** term, while the new predicate names the holder for both shapes: `OwesBuild` with a root (no window was ever published — the requester is 3.7's), `OwesSource` without one | **red-proof (b):** flipping the commit's hold term to the new predicate leaves the root-backed demoted target's switch pending (the measurement behind the sequencing above) |

**What lands.** The predicate, its typed outcomes and the holder names (a FLASH if/else chain — no `switch`, no
name table, per this owner's RAM1 rule), the empty-interval and named-holder fixtures, and RWO's two demoted-target
fixtures re-pointed to ask the new predicate instead of `empty ⇒ not-ready`. The first body of the predicate is
deliberately **today's** answer (the legacy merged buffer) so the fixtures can compile against the API and be shown
to fail for the right reason; that body *is* red-proof (a), and it is replaced by the publication answer in this
same slice.

**Verification recipe.** `pio test -e native` focused on `test_runtime_working_set_ownership` (the two re-pointed
RWO fixtures included) + `test_midi_service_drain` + `test_playback_prewarm` (cited, unchanged) + the full native
suite + the `teensy41-capture-serial` build with the RAM1 guard, then the record and the durable docs. Detail and
raw numbers: [`evidence/2026-10-02-s35-readiness-holder/EVIDENCE.md`](evidence/2026-10-02-s35-readiness-holder/EVIDENCE.md).

### Completion record (2026-10-02)

- Oracle/contract unchanged: **YES** — AC-3 and AC-4, with every W-case above; nothing was weakened to make a fixture pass.
- Fixture implemented and discriminating assertion demonstrated: **YES** — `test_window_readiness_names_its_holder_and_an_empty_interval_is_ready`
  (W-9…W-12 plus the `NotServable` boundary) and **RWO's two demoted-target fixtures re-pointed** (W-13).
- Native verification: **GREEN** — focused **51/51** (was 50), `test_midi_service_drain` **38/38**,
  `test_playback_prewarm` **190/190** (unchanged, cited), full native **2682 / 2675 succeeded / 7 skipped / 0 failed**.
- Build/RAM1 verification: **GREEN** on a **clean rebuild** with the approved line **byte-identical**
  (`variables 96,192` / `code 392,200 + padding 1,016` / locals 34,880, floor 840). The first build of the slice
  **failed** the guard (`variables 100,288`); the cause (this script places `.rodata` in DTCM, and the holder names
  crossed the `_VectorsRam`/periodictable `ALIGN(1024)` edge) and the fix (`.text.progmem` carve-out, after
  `TRACK_COLD_MEM` was rejected by a section-type conflict) are recorded in the slice evidence and in
  [`Teensy-RAM1-Bank-Overflow.mdc`](../../../.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc). **No re-base** was made.
- Red-proofs: **two**, each restored byte-identically (sha256 verified): readiness answered from the legacy merged
  buffer (**5 failures**, one per designed reason); the commit's hold term flipped to the new predicate (the
  root-backed demoted switch stops committing — the measurement behind keeping the flip in 3.7).
- Device verification: **NOT APPLICABLE** — no consumer-visible behaviour changes in this slice.
- Required reviews and durable docs: **task 1.2 landed** (`DECISION_REVIEW.md`, F-1 resolved as path A); `CURRENT_WORK.md`,
  `PROJECT_STATE.md` and `ExecutionBounds.md` row 27 are updated at checkoff.
- Task IDs eligible for checkoff: **3.5**. Task **2.2** (F-2's per-affected-tick deadline) is **not** claimed: this slice
  makes readiness the completed publication and names its holder; the deadline stays its own requirement.

## Slice 3.6 — pinned-source retention, the interval index, and the lag rule (2026-10-02, in increments)

**Why this slice is staged.** 3.6's own text has four clauses with different owners and different costs: the
**retained source** (a new persistent holder, and therefore a deliberate `sizeof(LoopPlaybackRuntime)` re-base under
the S3.0 pin), its **protection as read by the reclamation consumer and on the direct reset path**, the **interval
index over pinned bodies**, and the **lag rule** (F-3). The climb is the ladder's, one increment at a time; 3.6 is
checked only when every clause has its own green fixture.

**What the discovery establishes** (read-only, cited, before any edit):

- The pin mechanism exists and is **reclamation-side already**: `PassReclaim::pinPassesFromLoopPasses(passes, refs)`
  fills a `SlotPassReferences` (pinned `PassId`/`EditPassId` lists), and `Loop::reclaimUnreferencedDisabledCapturePasses`
  / `…DisabledEditPasses` free only what the passed set does not reference.
- **The consumer reads pins from the undo stack alone**: `TrackManager::reclaimUnreferencedDisabledPasses` builds one
  transient `PassReferenceSet` per track from `collectReferencedPasses(track.getGlobalUndoStack(), refs)`. That is
  exactly what the spec forbids as *sufficient* ("a pin registered only alongside undo references is not sufficient"),
  so a playback-owned retained source must be **merged into that same read**.
- **The direct reset/replacement path is `Loop::resetPassTimeline()`** — it bumps `canonicalStorageGeneration` and then
  releases every pass's chunk refs through `LoopEventStore::releaseChunkRefs`, which consults **no pin set at all**. It
  has **eleven call sites** (record start, slot clear, boot load, undo, jam geometry, capture-stop commit, stop
  telemetry), so "honoured on the direct reset/replacement paths" is a real decision about where the veto lives.
- **The holder's cost is a pinned ABI**: `static_assert(sizeof(LoopPlaybackRuntime) == 6368)` (`static_assert` in
  `src/Track/TrackPlaybackRuntime.cpp`), and `sizeof(Track) == 384`. Any per-slot holder re-bases that pin deliberately
  and is recorded, exactly as DEC-107 recorded `6,352 -> 6,360`.
- The published window's identity is **provenance, not ownership** (`PlaybackViewSourceIdentity::snapshotIdentity` is a
  `const void*`), which is why DEC-108 item 5 says a null snapshot identity is not sufficient protection and item 4
  makes retention **pinned bodies plus reconstruction through the effective-content resolver**.

**Open implementation choices this packet records rather than assumes** (both are the owner's own recorded decisions,
and they differ in cost, so the first increment waits on the owner's call):

1. **Which retained-source shape the first increment builds.** DEC-108 item 12(a) decided "**one pinned revision per
   slot**"; the second-pass resolution then **explicitly replaced that clause** with item 13(a)'s *serving / candidate /
   retiring* source roles (bounded by role admission, unchanged bodies shared, a still-needed source never discarded to
   satisfy a count). 13(a) is later and self-describing as a replacement, so it governs — but the minimum honest role
   set (how many entries per slot, and whether `Retiring` is a role or a release path) is a cost decision: two entries
   per slot costs roughly a `SlotPassReferences` each (two `std::vector`s, 48 B) plus the identity, on the pinned ABI.
2. **Where the reset-path veto lives.** Either (a) `Loop::resetPassTimeline` consults a retention term and
   **refuses/defers** while a retained source still needs the bodies, or (b) the reset calls the **one release
   predicate** for its slot first and refuses when that predicate says the source is still referenced. (b) keeps the
   single-release rule in one place and touches no `Loop` state; (a) is stricter but spreads the term to eleven call
   sites and to `Loop`'s own size pin.

**Increment 3.6a — the retained source and its one release predicate** (the enabling half; the index and the lag rule
are 3.6b). Oracle cases, each with the mutation that must fail it:

| # | Case | Discriminating assertion | Mutation that must fail it |
|---|---|---|---|
| W-14 | A revision is retained for a slot | the retained source names that revision and holds a **pinned pass set** — and the pins appear in what the **reclamation consumer** reads (the counts the reclaim pass already accumulates) | building the reclaim set from the undo stack alone (today's shape) |
| W-15 | A reclaim pass runs while the source is retained | the retained revision's passes are **not** reclaimed | the merge removed from the reclaim consumer |
| W-16 | The loop's revision advances past the retained one with no window/builder/obligation/reader referencing it | the **one release predicate** releases it, and only then are its passes reclaimable | releasing without the all-clear conditions; keeping the pins forever |
| W-17 | A retained source exists | `Track::noLiveOwnerForSlot` is **false** by name (the retention term), and true once released | the term missing from the composite |

**Verification recipe.** Focused `test_runtime_working_set_ownership` + `test_midi_service_drain` + the reclamation
suite(s) that already pin the reclaim consumer, `test_playback_prewarm` (cited, unchanged), the full native suite, and
the `teensy41-capture-serial` build with the RAM1 guard — plus, because this slice re-bases a pinned `sizeof`, the
**size pin itself** is asserted in the build and in its fixture.

### Increment 3.6a — the retained source and its one release predicate (2026-10-02)

**What landed:** `RetainedSourceRole { None, Serving, Candidate, Retiring }` and `RetainedPlaybackSource` (identity +
descriptor pins + **owned** bodies) on the per-slot runtime; `Track::retainPlaybackSourceForSlot` with typed
outcomes; the reads; `tryReleaseRetainedPlaybackSource` (**the one release predicate**); `mergeRetainedPlaybackPinsInto`
and the reclamation consumer's merge; and the retention term in `Track::noLiveOwnerForSlot`. Detail and raw numbers:
[`evidence/2026-10-02-s36a-retained-source/EVIDENCE.md`](evidence/2026-10-02-s36a-retained-source/EVIDENCE.md).

- Oracle/contract unchanged: **YES** — DEC-108 items 4, 12(a), 13(a), 14(c), with W-14…W-19 (W-18 and W-19 joined the
  table above as the cases appeared); nothing was weakened to make a fixture pass.
- Fixture implemented and discriminating assertion demonstrated: **YES** —
  `test_retained_source_is_admitted_with_roles_and_survives_both_destruction_paths`.
- Native verification: **GREEN** — focused **52/52** (was 51), full native **2683 / 2676 succeeded / 7 skipped /
  0 failed**.
- Build/RAM1 verification: **GREEN** on a **clean rebuild** — `variables 96,192` **unchanged** / `code 391,864 +
  padding 1,352` / locals 34,880, floor 840, **512 B headroom** (336 B *more* than the 3.5 baseline). **No re-base.**
  Two findings had to be resolved to get there, both recorded in the evidence: the S3.0 size pin is now **per ABI**
  (6,368 → 6,464 on the target, 6,544 on the host — the holder contains pointer-sized pin lists, and one literal
  cannot pin both), and the enlarged cold reclamation pass moved to FLASH with the TU's existing macro.
- Red-proofs: **three**, each restored byte-identically (sha256 verified): the descriptors not pinned; the bodies named
  but **not owned** (the reset then frees them); the "a window still serves it" clause dropped from the release predicate.
- Device verification: **NOT APPLICABLE** — the lane has no production caller yet.
- **One approved decision was replaced by a better mechanism, stated rather than buried:** the plan for this slice was
  "the reset calls the one release predicate and **refuses** while still referenced". Implementing the holder exposed
  `LoopEventStore::retainChunkRefs` ("pins borrowed ids for a deferred reader"), so the retained source **owns** its
  bodies and the eleven `resetPassTimeline` call sites are safe **without any veto** — the gesture keeps working
  (W-18 asserts exactly that, plus that the bodies survive and are returned only by the release predicate). The veto
  was superseded by ownership, which is the spec's own "actual body ownership" clause, not a relaxation.
- **Task 3.6 stays OPEN.** The **interval index over pinned bodies** (later intervals constructible from the retained
  revision with no resident whole-loop root; both affected ranges; atomic publication with the source revision;
  inspected-work budgeting) and the **lag rule / F-3** (one pass late and only that, the named second-miss breach, the
  allowance not reset by a further edit, the consumer never blocked) are the next increment. This one builds their
  precondition: a revision can be held, protected against both destruction paths, and released by one predicate.

### Increment 3.6b — the lag rule (F-3, AC-5): one pass late, and only that (2026-10-02)

**Scope.** The **lag rule** only: a missed deadline keeps the **previous** revision of the affected interval
authoritative for **that traversal**; a **second** consecutive miss is a **named** contract breach; the consumer is
never blocked; and a further edit does **not** reset the allowance. It is the decision core of F-3/AC-5, riding on
3.5's readiness predicate (a miss *is* readiness that is not `Ready` when the playhead returns to the interval) and on
3.6a's retained source (the previous revision is *held*, which is what makes "keeps the previous version
authoritative" true rather than aspirational).

**Non-goals, named rather than implied.** (1) The **interval index over pinned bodies** remains the next increment of
3.6 — nothing here claims interval-directed lookup. (2) The **consumer switch** stays 3.7's: the traversal boundary
has no production caller yet, exactly as the window lane and the adoption edge do not. (3) This slice does not adopt
a replacement at the boundary; it decides what the boundary may serve, and `Current` means "the boundary may serve the
current revision" (either it already is, or a complete current window is published and the boundary adopts it).

**The rule, as one resolution plus per-interval accounting.** A `PlaybackLagState` per slot records the interval
being traversed (its coverage geometry), the served and desired revisions, the consecutive missed traversals, and
whether the breach has been reported. The resolution is total — **there is no waiting outcome**, so the consumer can
never be blocked — and reports the breach **once per episode**: the episode ends when a traversal is served at the
current revision or the interval changes.

| # | Case | Discriminating assertion | Mutation that must fail it |
|---|---|---|---|
| W-20 | the served window already carries the desired revision | `Current`; nothing is owed and no miss is counted | counting a miss when served is desired |
| W-21 | a complete current window is published for the boundary (the successor the boundary adopts) | `Current` — the replacement is published *before* the following eligible traversal | treating "not yet the served window" as a miss |
| W-22 | the first traversal with no replacement ready | `PreviousOnePass`: the previous revision stays authoritative for **that traversal**, and the published window is **byte-for-byte unchanged** (no tear, no hold, no defer) | replacing or clearing the served window on a miss; a fourth "waiting" outcome |
| W-23 | a **second** consecutive miss, and a further edit in between | `BreachSecondMiss`, reported **once per episode** (a third miss keeps breaching without re-reporting), and the further edit does **not** reset the miss count | resetting the allowance when the desired revision changes (the AC-5 rule) |
| W-24 | a replacement becomes ready, or the interval changes | the next boundary is `Current` and the episode resets (misses and the breach flag cleared) | never resetting, so one breach poisons the slot forever |

**Verification recipe.** Focused `test_runtime_working_set_ownership` + the full native suite + the
`teensy41-capture-serial` build with the RAM1 guard (this increment adds per-slot state, so the S3.0 pin is re-based
again **per ABI**, with the target figure charged). Detail:
[`evidence/2026-10-02-s36b-lag-rule/EVIDENCE.md`](evidence/2026-10-02-s36b-lag-rule/EVIDENCE.md).

### Increment 3.6b — the lag rule (F-3, AC-5): one pass late, and only that (2026-10-02)

**What landed:** `PlaybackLagDecision { Current, PreviousOnePass, BreachSecondMiss }` — **total by construction**, with
no waiting outcome, so a consumer can never be blocked — `PlaybackLagState` (the traversed interval, served and desired
revisions, consecutive misses, the once-per-episode breach) on the per-slot runtime, and
`Track::resolvePlaybackLag` / `advancePlaybackLagTraversal` / `playbackLagDecisionForSlot` with the miss and breach
reads. Detail and raw numbers:
[`evidence/2026-10-02-s36b-lag-rule/EVIDENCE.md`](evidence/2026-10-02-s36b-lag-rule/EVIDENCE.md).

- Oracle/contract unchanged: **YES** — AC-5 / F-3 and DEC-108 item 13(e), with W-20…W-24 as tabulated above.
- Fixture implemented and discriminating assertion demonstrated: **YES** —
  `test_playback_lag_rule_serves_the_previous_revision_once_and_names_a_second_miss`.
- Native verification: **GREEN** — focused **53/53** (was 52), full native **2684 / 2677 succeeded / 7 skipped /
  0 failed**.
- Build/RAM1 verification: **GREEN** on a **clean rebuild** — `variables 96,192` **unchanged** / `code 391,928 +
  padding 1,288` / locals 34,880, floor 840, **448 B headroom**. **No re-base.** The S3.0 pin moved per ABI again
  (target 6,464 → 6,488; the host stayed 6,544 because the state fitted its existing padding), measured by a
  temporary compile-time probe after the naive guess failed — the probe is recorded in the slice evidence.
- Red-proofs: **three**, each restored byte-identically (sha256 verified): the AC-5 allowance reset by a further edit;
  a published replacement ignored; the once-per-episode breach guard dropped. The first attempt at the AC-5 mutation
  was a **no-op** (placed after the assignment it compared) and was re-placed before red-proving — recorded, because a
  mutation that cannot trigger is not evidence.
- Device verification: **NOT APPLICABLE** — the traversal boundary is a seam; the consumer switch is 3.7's.
- **Task 3.6 still stays OPEN:** the **interval index over pinned bodies** (later intervals constructible from the
  retained revision with no resident whole-loop root; both affected ranges; atomic publication with the source
  revision; inspected-work budgeting) is the remaining clause, and nothing here claims interval-directed lookup.

### Increment 3.6c — the interval index over pinned bodies (2026-10-02)

**Scope.** The last clause of 3.6: later intervals must stay constructible from the retained revision **through an
index over pinned bodies, with no resident whole-loop root**, whose lookup is **interval-directed** (total work does not
grow with unrelated loop content), **includes edit-moved events at their effective destinations**, publishes **atomically
with the source revision** and never modifies an index a reader holds, and whose representation change, ownership,
**resident-memory bound** and construction/update lifetime are stated explicitly.

**The representation, stated rather than implied.** One **bit per bar** says whether any pinned body carries content in
that bar (`RetainedIntervalIndex`). It is a **descriptor**, not an event copy — `TickIndex`'s whole-history copy storage
is deliberately not inherited, and `CommittedEventRange` stays the body-reading primitive. Resident bound: the bar
ceiling `kRetainedIndexMaxBars` (512 bars = 16 words per bitmap), **refused by name** beyond it, because an index that
answered only part of a loop would present the complete scan as window-bounded. Ownership: the slot's retained source.
Lifetime: the revision the index was built for. Update strategy: a **full rebuild per admitted revision**, published
with a single assignment, so both the old and the new affected ranges are covered by construction and no reader ever
holds a mutated index.

| # | Case | Discriminating assertion | Mutation that must fail it |
|---|---|---|---|
| W-25 | an interval over a bar whose pinned bodies carry content | `Indexed` with every bar of the interval covered and none skipped | counting a covered bar as skipped |
| W-26 | an interval whose bars carry no pinned bodies | `Indexed` with **0 covered and every bar skipped** — the interval-directed answer, at no chunk-inspection cost | marking every bar of the interval covered (the scan the index exists to avoid) |
| W-27 | an **active edit** whose destination is an otherwise-empty bar | that bar is **covered, not skipped** — the index covers the edited destination, while applying the edit stays the resolver's | not folding the edit destinations (the "lookup includes edit-moved events" clause) |
| W-28 | a query for a revision the index was not built for; a loop beyond the bar ceiling | `RevisionMismatch` (named, never a stale answer) and `BarsUnsupported` for the build **and** the query, with **no index left behind** to answer partially | answering from the index regardless of its revision; indexing part of an over-ceiling loop |

**Verification recipe.** Focused `test_runtime_working_set_ownership` + the full native suite + the
`teensy41-capture-serial` build with the RAM1 guard (the index adds per-slot state again, so the S3.0 pin is re-based
per ABI with the **target** figure charged). Detail:
[`evidence/2026-10-02-s36c-interval-index/EVIDENCE.md`](evidence/2026-10-02-s36c-interval-index/EVIDENCE.md).

### Increment 3.6c — the interval index, and **task 3.6 checked** (2026-10-02)

**What landed:** `RetainedIntervalIndex` (one **bit per bar**, revision-stamped, bounded by `kRetainedIndexMaxBars`),
`buildRetainedIntervalIndexForSlot` and `queryRetainedIntervalForSlot`, and the build wired into
`retainPlaybackSourceForSlot` so the index is **published with the source revision**. Detail and raw numbers:
[`evidence/2026-10-02-s36c-interval-index/EVIDENCE.md`](evidence/2026-10-02-s36c-interval-index/EVIDENCE.md).

- Oracle/contract unchanged: **YES** — DEC-108 item 14(c) and the spec's index paragraph, with W-25…W-28 as tabulated
  above. The representation decisions the spec demands (descriptor not a copy; ownership; resident bound; lifetime;
  update strategy) are stated in the evidence rather than left implicit.
- Fixture implemented and discriminating assertion demonstrated: **YES** —
  `test_retained_interval_index_is_interval_directed_and_refuses_stale_revisions`, including an interval built from the
  **pinned chunk lists alone** (no published root), which is the "no resident whole-loop root" clause demonstrated.
- Native verification: **GREEN** — focused **54/54** (was 53), full native **2685 / 2678 succeeded / 7 skipped /
  0 failed**.
- Build/RAM1 verification: **GREEN** on a **clean rebuild** — `variables 96,192` **unchanged** / `code 391,928 +
  padding 1,288` / locals 34,880, floor 840, **448 B headroom**. **No re-base.** The S3.0 pin moved per ABI again
  (target 6,488 → 6,512), measured by the same temporary probe discipline as 3.6b, removed before the commit.
- Red-proofs: **three**, each restored byte-identically (sha256 verified): the edit destinations not folded; the
  revision guard dropped; every bar reported covered.
- Device verification: **NOT APPLICABLE** — no consumer-visible behaviour changes; the lane still has no requester.
- **Task 3.6 is checked.** Its four clauses are green — retained source with one release predicate (3.6a), protection at
  both destruction paths, the interval index over pinned bodies (3.6c) and the lag rule with the named second miss
  (3.6b) — with two limits recorded in the evidence and in `tasks.md`: the **consumer path stays 3.7's** (no production
  requester yet, so the fixture drives the halves directly) and the **envelope roll-up** of the per-slot figures into
  RWO's budget artifact belongs to that change's own terms.

### Slice 3.8 — reuse before invention (2026-10-02, documentation only)

`REUSE-BEFORE-INVENTION.md` answers AC-7: it names the four entry points the contract names — the declared build
contract (`CoverageServiceContract` + `Track::playbackViewCoverageContract`), the body format's bounded write/parse
(`StorageIo`'s `write`/`read`/`peek` + `getLastPersistedCapturePassWriteMaxBatchEvents()`), the **resume-arming** cursor
pattern (`PersistedLoopSnapshotReleaseCursor::begin`/`step(maximumUnits)`/`complete`) and the single persistence drain
budget (`SyncDrainBudget` + `buildSyncDrainBudget(input, sliceBudgetUs)`) — marking its two **INTERPRETATION** rows as
mappings a reader may dispute, and records the audit the task asks for: **one** deferred-service enumeration
(`DeferredIdleOwner`), to which this change added a *value* (task 3.1, on the `SlotProvision` precedent), with dispatch
on the existing frame and state-driven eligibility — **no second publication scheduler**.

- Oracle/contract unchanged: **YES** — AC-7 as written; nothing weakened.
- Fixture: **NOT APPLICABLE** — a review note is the artifact AC-7 asks for; the behavioural half (fair service among
  holders, no starvation) is task 3.10's and is not claimed here.
- Verification: **no code changed**, so the record is the last full-suite/build state (focused 54/54, full native
  2685 / 2678 succeeded / 7 skipped / 0 failed, `teensy41-capture-serial` SUCCESS on a clean rebuild with
  `variables 96,192` / `code 391,928 + padding 1,288`), exactly as slice 3.3's documentation-only record did.
- Red-proof: **NOT APPLICABLE** to a note.
- Task IDs eligible for checkoff: **3.8**.

### Slice 3.7 — not started, and its own gate is the reason (2026-10-02)

3.7 requires **3.11** (shared-view ownership and the cancellation transition table), **3.12** (the bounded legacy
compatibility path), **3.13** (preview publication), **3.14** (undo/redo audible handoff) and **4.5** (fault recovery)
to be implemented and verified **before** it removes the live mutable merged-window build, and none of them exists
yet. The ladder's stage (4) is therefore not reachable, and the next slices are those dependencies — **3.11 first**,
since the shared-view transition table is what the removal's correctness rests on. Recorded here and in `tasks.md`
under 3.7 so the gate is visible where the task is read.

### Slice 3.11 — shared-view ownership and the transition table (DEC-108 §13d) (2026-10-02)

**Scope.** The ownership rules the spec states (*Published views are read-only and ownership survives transitions*) as
**one table plus the operations it names**: which element of the shared view state each lifecycle transition keeps,
releases or invalidates, the publication guard that makes a **late completion publish nothing**, and the
generation-qualified identity that stops a returning slot reviving an earlier operation. This is also **3.7's first
declared dependency** (`tasks.md` 3.7 requires 3.11 before the live mutable merged-window build is removed).

**The table, stated as the spec's own transition list.** `PlaybackViewTransitionEffect` has one field per element the
spec names, and `resolvePlaybackViewTransition` is the **pure** resolution so the table has one implementation and a
fixture can pin every row:

| Transition | operation continues | candidate released | prepared successor retired | identity invalidated | served window readable | frozen source held |
|---|---|---|---|---|---|---|
| `NewEdit` | **yes** (the frozen source is retained and the successor coalesced) | no | no | no | yes | yes |
| `SlotSwitch` | **yes** (inbound coverage is prepared before binding; outbound is retained until readers and closures finish) | no | no | no | yes | yes |
| `SelectedTrackChange` | **yes** (an existing worker is never retargeted) | no | no | no | yes | yes |
| `StopWithSuccessorStart` | **yes** (the predecessor's frozen ownership is preserved; the successor uses its own buffers) | no | no | no | yes | yes |
| `TransportStop` | no — obsolete **lookahead** is cancelled | **yes** | **yes** | no (accepted content and finalization survive) | yes | yes |
| `ClearOrLoad` | no | **yes** | **yes** | **yes** (the old operation's identity is invalidated) | yes | yes (until cancellation and readers finish) |
| `LateCompletion` | no — it is rejected for publication | **yes** (it releases only what it owns) | no | yes (it is late *because* the identity moved) | yes | no (it holds nothing new) |

**What the slice adds beyond the table:** the application (`Track::applyPlaybackViewTransition`), the **publication
guard** (a completing build whose generation is no longer current is refused, and only its own candidate is released),
and a read the fixture — and later slices — can assert role state with.

| # | Case | Discriminating assertion | Mutation that must fail it |
|---|---|---|---|
| W-29 | every row of the table | the pure resolution's six fields per transition, including that no lifecycle transition can make the **served window unreadable** | flipping "served window readable" for any row |
| W-30 | a new edit / selected-track change / stop-with-successor / slot switch on an in-flight build | the operation **continues**: the build is still owed, its identity (generation and revision) is unchanged, and the served window is untouched — "an existing worker is never retargeted" | cancelling the build (or bumping its identity) on those rows |
| W-31 | transport stop, then clear/load | transport stop cancels the **lookahead** (nothing owed, and the retired successor cannot be adopted) while the served window stays byte-for-byte readable; clear/load additionally **invalidates the generation**, so a completion carrying the old generation cannot publish | not invalidating the identity (measured: *"clear/load invalidates the old operation's identity"* fails). **Corrected 2026-10-02:** the mutation that empties the explicit *prepared-successor retirement* branch does **not** discriminate — `RuntimePlaybackViewBuild::discard()` clears its own destination's `complete`, and after a successor publication that destination is the very view the `ready` role holds, so the cancel already retires the lookahead. That branch covers a `ready` view the current builder is *not* pointing at, which this fixture does not construct: a named gap, not a covered case |
| W-32 | a completion that arrives **after** the identity moved | it **publishes nothing** (the served window is byte-for-byte what it was, and a later request still sees the current revision), and only its own candidate is released | removing the publication guard |
| W-33 | storage reuse | a view whose storage a reader still needs is **not** reused, and the roles become available again only after the served window is replaced and the retained source is released (the next request is admitted) | recycling the view that a reader holds |

**Verification recipe.** Focused `test_runtime_working_set_ownership` + `test_midi_service_drain` + the full native
suite + the `teensy41-capture-serial` build with the RAM1 guard. Detail:
[`evidence/2026-10-02-s311-shared-view-transitions/EVIDENCE.md`](evidence/2026-10-02-s311-shared-view-transitions/EVIDENCE.md).

### Completion record — slice 3.11 (2026-10-02)

- Oracle/contract unchanged: **YES** — DEC-108 §13d and the spec's four scenarios, with W-29…W-33 as tabulated above
  (and one mutation cell **corrected** rather than left overstated).
- Fixture implemented and discriminating assertion demonstrated: **YES** —
  `test_shared_view_transitions_state_the_ownership_rules_and_a_late_completion_publishes_nothing`.
- Native verification: **GREEN** — focused **55/55** (was 54), full native **2686 / 2679 succeeded / 7 skipped /
  0 failed**.
- Build/RAM1 verification: **GREEN** on a **clean rebuild** — `variables 96,192` **unchanged** / `code 391,928 +
  padding 1,288` / locals 34,880, floor 840, **448 B headroom**. **No re-base**, and the S3.0 pin is unchanged: the
  4-byte operation-generation field fitted existing padding on both ABIs.
- Red-proofs: **three**, each restored byte-identically (sha256 verified): the publication guard dropped; the identity
  not invalidated on clear/load; the table's continuing rows made to cancel the candidate. **One attempted mutation
  did not discriminate and is recorded as such** (the explicit prepared-successor retirement branch, because
  `discard()` already clears the view that the successor publication leaves in `ready`) — the cell above says so, and
  the uncovered case (a `ready` view the current builder is not pointing at) is named as a gap deferred to 3.7.
- Device verification: **NOT APPLICABLE** — the lane still has no production requester.
- Task IDs eligible for checkoff: **3.11** — the shared-view ownership rules, the seven-transition table, the late
  completion's refusal and the generation-qualified identity, each with its own assertion.
- **Consequence for 3.7:** its first dependency is now green. Remaining before the live build may be removed: **3.12**
  (admission limit + bounded legacy path), **3.13** (preview), **3.14** (undo/redo audible handoff) and **4.5** (fault
  recovery).

### Increment 3.12a — the admission limit on the decided accounting unit (2026-10-02; task 3.12 stays open)

- What landed: `WindowAdmissionAccounting` in `RuntimePlaybackViewConfig` — one capture interval's **effective** events plus its open reservations plus the closure reserve **inside** L, with `fits(...)` as the one predicate, `admit(...)` carrying a **named** `Outcome` and never half-accepting, and per-bar/span bounds conservative and **clamped by L**.
- Pinned invariants (`static_assert`): `kWindowAdmissionLimit == kEventsPerView` (L **is** the storage capacity, no partition multiplier) and `kWindowClosureReserve < L` — 128 being a declared **provisional** value, not a measurement.
- Fixture: `test_window_admission_limit_accounts_effective_events_and_the_closure_reserve_inside_l` (one below / at / above the limit; same-tick events across four passes; a reservation occupying the window until it closes; the clamped span bound, a wrap-crossing interval included).
- Native verification: **GREEN** — focused **56/56** (was 55), full native **2687 / 2680 succeeded / 7 skipped / 0 failed**.
- Build/RAM1 verification: **GREEN** on a clean rebuild — `variables 96,192` unchanged / `code 391,928 + padding 1,288` (448 B headroom); the S3.0 pin unchanged, the type being header-only with no per-slot state.
- Red-proofs: **two**, each restored byte-identically — the reserve moved outside L (`Expected 128 Was 0`); the per-bar bound not clamped (`Expected 2048 Was 8192`).
- **Task 3.12 stays OPEN**: (b) the bounded legacy paged path (fixture 5.5) and (c) the MIDI-edge wiring of the limit into capture acceptance are not done.
- Detail: [`evidence/2026-10-02-s312a-admission-limit/EVIDENCE.md`](evidence/2026-10-02-s312a-admission-limit/EVIDENCE.md).

### Increment 3.12b — the bounded legacy paged path, pages plus a resumable cursor (2026-10-02; task 3.12 stays open)

- What landed: `include/WindowPagedStream.h` (new) — `WindowEventPage` (a preallocated fixed-capacity buffer + revision + sequence), `WindowPageCursor` (the consumer's `(sourceRevision, nextSequence, offsetInPage)`), and `WindowPagedStream`, a **ring of three 128-event pages** whose `produce(...)` takes one bounded slice per call through the **existing** `CommittedEventRange::appendWindowBudgeted` + `WindowTraversalCursor` (no second enumerator, no inline rebuild) and whose `consume(...)` continues exactly where it left off. Typed outcomes `Produced`/`SourceComplete`/`RingFull`/`StaleRevision` and `Emitted`/`NeedsPage`/`StaleRevision`/`Exhausted`.
- Clause (b) discharged: content beyond `L` — and any interval that outgrows one page — is served through fixed-capacity immutable pages, prepared off-clock from bounded preallocated storage, with no truncation, no tick delay and **no blocking state** (a consumer whose next sequence is unpublished is told `NeedsPage`, never left waiting).
- Fixture: `test_paged_window_stream_serves_more_than_one_page_exactly_once_across_the_ring` — **700** events (six pages at 128) emitted in source order across page boundaries with the ring high-water mark ≤ 3 and every event exactly once; `StaleRevision` from **both** `consume` and `produce` for a foreign revision.
- Native verification: **GREEN** — focused **57/57** (was 56), full native **2688 / 2681 succeeded / 7 skipped / 0 failed**.
- Build/RAM1 verification: **GREEN** on a clean rebuild — `variables 96,192` unchanged / `code 391,928 + padding 1,288` (448 B headroom); the header has **no production caller**, so neither ITCM nor DTCM moved and the S3.0 pin is untouched.
- Red-proofs: **two**, each restored byte-identically (sha256 verified) — M15 the drained page never returning to the ring (production reaches `RingFull` and the consumer stalls); M16 the cursor not advancing on drain (the same page would be re-emitted). The **first M15 attempt did not discriminate** — a 300-event fixture filled exactly the ring, so no page was ever reused, and a mutation that cannot trigger is not evidence; the fixture now commits 700 events and only then did both red-prove.
- **Task 3.12 stays OPEN**: (c) the MIDI-edge wiring of the admission limit into capture acceptance remains, and fixture 5.5's timing envelope plus the over-capacity same-tick/wrap/cross-page-closure cases are owed. The ring size (3×128) and the closure reserve (128) are **declared, unmeasured** choices for task 6.3's sweeps.
- Detail: [`evidence/2026-10-02-s312b-paged-path/EVIDENCE.md`](evidence/2026-10-02-s312b-paged-path/EVIDENCE.md).

### Increment 3.12c — the MIDI-edge wiring of the admission limit (2026-10-02; task 3.12 stays open)

- What landed: the gate in `Track::recordMidiEvents` (the edge `MidiHandler` calls for CC/PC/AT/PB and which `noteOn`/`noteOff` call for notes), consulted **before** the canonical append; the **derived** accounting `Track::captureIntervalAdmissionForSlot` (the serving window's content + this interval's accepted rows + the closures they owe, read from `pendingNotes` + the reserve) with `Track::playbackWindowServesSlot` / `playbackWindowEventCountForSlot` as its O(1) window reads; `WindowAdmissionAccounting::admitsNewEvent` / `kUnitsForNoteOn` / `closeReservation` as the type's edge vocabulary; and `CaptureAppendDenyReason::WindowAdmission` (`"window_admission"`) so the refusal is named like every other capture denial.
- **No new per-slot state**: the accounting is computed from the capture it describes, so there is no counter to drift and no `sizeof` pin to move.
- **A closure is never refused** — its closing row is already inside `L`, so it is a move, not a growth. A refused note-on gives its staged reservation back and leaves no row and nothing owed, and the refusal is deliberately not reported as a memory fault.
- **Scope, decided by the first full-suite run rather than by preference:** the gate bounds **what a window holds**, so it is consulted only where a window serves the slot. The first wiring bounded every interval and broke `test_shared_capture_finalization::test_record_normalization_preserves_take_larger_than_overdub_staging`: a whole-take record take is not served by the 2048-event window and is therefore not window-bounded.
- Fixtures: `test_capture_admission_refuses_at_the_edge_and_never_a_closure_or_a_stop`, `test_capture_admission_refusal_on_the_overdub_path_gives_back_its_staging_reservation`.
- Native verification: **GREEN** — focused **59/59** (was 57), `test_shared_capture_finalization` **60/60** (the over-reach oracle), full native **2690 / 2683 succeeded / 7 skipped / 0 failed**.
- Build/RAM1 verification: **GREEN** on a clean rebuild — `variables 96,192` unchanged / `code 392,072 + padding 1,144` (304 B headroom, floor 840), **no re-base**.
- Red-proofs: **five**, each restored byte-identically — the limit bounding note-ons only; closures gated too; no window ever seen as serving; the refusal keeping its staging reservation; the live accepted rows not counted.
- **Task 3.12 stays OPEN**: fixture 5.5's timing envelope with the over-capacity same-tick, wrap and cross-page-closure cases, and the paged path's production consumer, remain.
- Detail: [`evidence/2026-10-02-s312c-midi-edge-admission/EVIDENCE.md`](evidence/2026-10-02-s312c-midi-edge-admission/EVIDENCE.md).

### Increment 3.13 — the preview source contract (2026-10-02; task 3.13 stays open)

- What landed: `PlaybackWindowOrigin { Committed, Preview }` carried in **both** `PlaybackViewSourceIdentity` (the source contract) and `PlaybackWindowIdentity` (the consumer-facing tuple), each **part of equality**; `Track::requestPreviewWindow(slot, previewRevision, source, sourceCount, currentPhase)` publishing preview through the **same** deferred lane as committed content (whole-loop coverage, the session's preview revision as the freeze token and currency, `origin == Preview`); `Track::cancelPreviewWindowForSlot(slot, reason)` for `SourceMutated` / `SelectedTrackChanged` / `SessionExited` with a **named** `PreviewCancelOutcome`; `servedPlaybackWindowIsPreviewForSlot` / `previewWindowHandoffOwedForSlot`; and the preview lane's three fields on the per-slot runtime.
- **Source freezing** is a revision, not a copy: the lane freezes the session's preview revision and the projection pointer, so a mutation makes the frozen revision stale and a stale completion cannot pass as current. No content-sized second buffer.
- **Cancellation** reuses the shared-view row (`PlaybackViewTransition::ClearOrLoad`: candidate released, successor retired, identity invalidated) and touches nothing else — the served window stays readable, so a mutated source leaves a served preview stale **by its own revision** rather than torn.
- **The exit handoff is a boundary, not a gap:** the preview window keeps serving, the committed successor is owed, and the obligation is discharged at the **adoption** boundary; a repeated exit answers `AlreadyHandedOff`.
- Fixtures: `test_preview_window_is_a_distinct_source_and_never_committed_currency`, `test_preview_window_is_cancelled_by_its_lifecycle_and_hands_off_to_committed`.
- Native verification: **GREEN** — focused **61/61** (was 59), full native **2692 / 2685 succeeded / 7 skipped / 0 failed**.
- Build/RAM1 verification: **GREEN** on a clean rebuild — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B above the floor). **The target size pin was re-based, measured and recorded:** `sizeof(LoopPlaybackRuntime)` 6,512 → **6,624** (+112 B; 8 B of payload, the rest alignment), host unchanged at 6,544. Named, not smoothed over: the lane's ~256 B of ITCM code took the RAM1 padding from 1,144 to 888 — it passes, with 48 B to spare.
- Red-proofs: **five**, each restored byte-identically — the origin not carried into the identity; the origin not part of equality; the cancellation not releasing the in-flight build; the exit not owing the handoff; the lane not marking its own in-flight build.
- **Task 3.13 stays OPEN**: fixture 2.12's **integration** half (edit while playing on a long loop, cross window boundaries, in-session undo/redo, switch the selected track, mutate/exit between build grains, then save) needs a suite that links **both** the window lane and the real edit stack, and the production **callers** (the preview requester and the `EditManager` lifecycle hooks) are owed with the committed requester to 3.7's stage.
- Detail: [`evidence/2026-10-02-s313-preview-contract/EVIDENCE.md`](evidence/2026-10-02-s313-preview-contract/EVIDENCE.md).

### Increment 4.5 — second-miss fault containment and the approved last resort (2026-10-02; task 4.5 stays open)

- What landed: `ContinuityContainment { Nominal, ServeLastCoherentRevision, LastResortTargetedSilence }` with the **pure** `resolveContinuityContainment(faultLatched, validCoverageRemains)` (total; no waiting and no rebuilding outcome exist to be chosen, so "block the clock" and "unbounded rebuild" are unrepresentable rather than avoided); `deadlineContractFaultLatched` on the per-slot runtime; the **latch** and its single clear in `Track::advancePlaybackLagTraversal` (3.6b's once-per-episode breach *report* keeps its shape; the latch is what containment reads); and `Track::containmentForSlot(slot, phase)` / `deadlineContractFaultLatchedForSlot` / `applyLastResortForSlot(slot, phase)` with a **named** `LastResortOutcome { NotNeeded, SlotSilenced }`.
- **Containment is not a successful fallback:** the fault is latched in addition to the report and cleared only at the **eligible boundary**; a further edit never forgives it (AC-5). The served window stays field-for-field untouched through the episode, and preparation keeps progressing (the committed lane still admits a replacement while the fault is latched).
- **"Valid coverage remains" is a coverage question**, not a content question — an event-empty window still covers its interval, which is the structural half of "an event-empty window means *no new events*".
- **The last resort is targeted by construction:** the existing **slot-scoped** `Track::silenceSlotMidiOutput` for the closures plus that slot's ledger; no transport state, no rebuild. With coverage intact it answers `NotNeeded` and emits **nothing**.
- Fixture: `test_second_miss_latches_a_fault_and_the_last_resort_is_targeted_not_global` (forced two-miss run, a further edit, continuity, progressing preparation, recovery at the boundary, and the last resort asserted through the real **MIDI output** (`MidiSendCapture`) and a sibling slot's untouched ledger).
- Native verification: **GREEN** — focused **62/62** (was 61), full native **2693 / 2686 succeeded / 7 skipped / 0 failed**.
- Build/RAM1 verification: **GREEN** on a clean rebuild — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom), the line **byte-identical to the previous slice's** (the new functions follow the TU's FLASH placement, so containment costs no ITCM; the new `bool` fitted padding, so **no size pin moved**).
- Red-proofs: **six**, each restored byte-identically — the second miss not latching; containment ignoring coverage; the last resort closing nothing (track-wide all-notes-off); the last resort sweeping globally; an eligible boundary not clearing the fault; the last resort applied opportunistically. **Two corrections recorded:** M29 first did *not* discriminate (the fixture observed only the ledger, while the track-wide owner emits CC 123 and touches no ledger), so the fixture was sharpened to assert the **MIDI output** and M29 then failed; and an assertion I had written (`g_nativeSilenceTrackCalls == 0`) is **vacuous in this suite** and was **removed** rather than kept as fake evidence, with a genuinely global mutation (M29b) added to show the sibling-slot assertion has teeth.
- **Task 4.5 stays OPEN**: the **cross-generation closure** half (§13g) is emission at a version handoff — the consumption path (3.7 / 3.14) — and its verification (a closure whose note-off moves across the handoff, a window empty while a note is sustained) is owed there.
- Detail: [`evidence/2026-10-02-s45-containment/EVIDENCE.md`](evidence/2026-10-02-s45-containment/EVIDENCE.md).

### Increment 4.5b — the cross-generation closure reconciliation (2026-10-02; task 4.5 stays open)

- What landed: `ClosureSettlement` (the handoff's plan as a **value**: `closures`, `suppressedRetriggers`, `preservedCount`, `eventEmptyWindow`, all bounded by `ActiveNoteLedger::kMaxOpenNotes`, nothing allocating); the one **interpretive** rule `handoffAbsenceMeansRemoval(incomingIsWholeLoop, incomingIsEmpty)`; `Track::planClosureSettlementForSlot(slot, events, count, incomingIsWholeLoop)` (one bounded pass over the incoming note-ons into a 256-byte presence set, then a ≤128-entry ledger walk, no search); and `Track::applyClosureSettlementForSlot(slot, settlement)` (closures **first** and only closures, discharging exactly those ledger obligations through the ledger's own event path).
- **The three fates are separated by construction** — closed (removed), suppressed (re-issued → not retriggered and not closed), or preserved (untouched) — so "unchanged, without retriggering" has a mechanical meaning.
- **Absence is evidence of removal only when the incoming content is authoritative for the whole loop**, and never for an empty window: that one rule makes "an event-empty window means *no new events*" a consequence rather than a special case (marked as an interpretation).
- **Ordering is a calling order:** the applier emits closures and nothing else; the replacement's note-ons are the caller's, which is the spec's note-offs-before-replacements as structure.
- Fixture: `test_cross_generation_closures_are_reconciled_and_an_empty_window_silences_nothing` (whole-loop removals closed with real MIDI output; a re-issued note neither retriggered nor closed; a slice's absence not treated as removal; an empty window silencing nothing).
- Native verification: **GREEN** — focused **63/63** (was 62), full native **2694 / 2687 succeeded / 7 skipped / 0 failed**.
- Build/RAM1 verification: **GREEN** on a clean rebuild — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom), the line **byte-identical to the two previous slices** (FLASH-placed additions, no ITCM growth, no size pin move).
- Red-proofs: **four**, each restored byte-identically — absence always meaning removal; a re-issued note also closed; the applier emitting no closure; the applier not discharging the ledger. **One harness repair recorded:** M32's inverse anchor was not unique, so the restore aborted and the mutation stayed applied; the `shasum -c` check caught it and the context-qualified repair was verified byte-identical before the gates ran.
- **Task 4.5 stays OPEN**: the emitted order at the real handoff (the clock-path caller that emits the replacement's note-ons), the moving-closure case (a note-off moved across the boundary) and displayed provenance are the serving path's (3.7).
- Detail: [`evidence/2026-10-02-s45-closure-reconciliation/EVIDENCE.md`](evidence/2026-10-02-s45-closure-reconciliation/EVIDENCE.md).

### Increment 3.14 — the undo/redo audible handoff (2026-10-02; task 3.14 stays open)

- What landed: the **blanket silence is gone** from the undo/redo playback refresh (`refreshPlaybackAfterCapturePassStateChange` no longer calls `silenceSlotMidiOutput`); a new `refreshPlaybackAfterEditPassStateChange(track, slot, passIds, identitiesAreRemoved)` reconciles first and both refreshes now invalidate with the **ledger preserved**; `Track::reconcileAfterEditPassStateChange(slot, passIds, identitiesAreRemoved)` settles the affected edit batch's **own identities** against the ledger (closed / suppressed / preserved) with `NotePresenceSet` as its bounded, allocation-free input; and `LoopPlaybackRuntime::reset` now clears the revision/generation caches unconditionally while `preserveLedger` governs **only** the ledger.
- **The blanket invalidation had two halves and both are removed:** the silence emitted a NoteOff for *every* sounding note, and the refresh on the same path cleared the ledger. The second half was **found by the fixture**, not by inspection (its "a sounding note the batch never named is untouched" assertion failed first).
- Fixture: `test_undo_edit_batch_settles_its_own_soundings_and_never_blankets_the_slot` — driven through the production `TrackUndo::undoForLoop`: an unrelated sounding survives with **no** early NoteOff (the blanket emitted three), the batch's own identity closes exactly once, the re-establishing direction suppresses rather than retriggers, the generic refresh emits nothing, and a slot with no window lane is silenced by nothing.
- Native verification: **GREEN** — focused **64/64** (was 63), full native **2695 / 2688 succeeded / 7 skipped / 0 failed**.
- Build/RAM1 verification: **GREEN** on a clean rebuild — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom), the line **byte-identical to the three previous slices** (FLASH-placed, no ITCM growth, no size pin move).
- Red-proofs: **six**, each restored byte-identically — the task's own "restore the blanket silence" on **both** refresh paths (1 closure became 3; 0 became 2), the ledger-clearing refresh restored, the reconciliation closing nothing, and the re-issuing direction closing instead of suppressing. **One recorded correction:** the first blanket-silence attempt did not discriminate (it mutated the generic refresh while the fixture drives the edit-batch one), which is why the fixture now also asserts the generic path **directly**.
- **Task 3.14 stays OPEN** on two named items: the two **overdub-session** sites still do not close the session's removed soundings (they need a committed-pass identity source; the stuck-note risk is stated), and the refresh is not yet a **request through the immutable publication owner** — the request needs a bounded content source for the affected interval, a design decision the packet must state first. Plus the between-grains/repeated undo-redo matrix (3.7's serving path), retained-source lifetime (3.6a, cited) and preview-session undo/redo (3.13/2.12, cited).
- Detail: [`evidence/2026-10-02-s314-undo-audible-handoff/EVIDENCE.md`](evidence/2026-10-02-s314-undo-audible-handoff/EVIDENCE.md).

### Increment 3.14b — the committed-pass identity source (2026-10-02; task 3.14 stays open)

- What landed: `Loop::overdubSessionPassIdAt` / `overdubSessionCompanionIdsAt` (the pass a session step just changed, and the companion edit batches it changed with it — out-of-range answers **named**, never clamped to a neighbouring step); one shared settlement core `settleLedgerAgainstPresence` (a single O(open-notes) ledger walk) used by **both** reconcilers; `collectCommittedPassIdentities` (a pass's own bodies, a row at a time via `chunkUsedCount` + `readSealedChunkEvent`, bounded and allocation-free); `Track::reconcileAfterCommittedPassStateChange(slot, passIds, passCount, identitiesAreRemoved)`; and `refreshPlaybackAfterCommittedPassStateChange`, which **replaces the generic refresh** at the two overdub-session sites — each now settling the step's committed pass **and** its companion edit batches, with the direction's own flag.
- **The session case's stuck-note risk is closed:** a note the undo removed now closes, because the identities come from the pass that actually carried them. (A disabled pass keeps its chunk references — "keep disabled capture passes on the timeline so redo can re-enable them" — which is what makes the removal settleable at all.)
- **One core, two sources:** an edit batch's added events and a committed pass's bodies now feed the *same* classification, so a divergence between the cases would be a divergence of input, not of rule.
- Fixture (extended, not added): `test_undo_edit_batch_settles_its_own_soundings_and_never_blankets_the_slot` gained the committed-pass case — the pass's own sounding closes (one NoteOff on the wire, obligation discharged), an unrelated sounding is preserved, the re-establishing direction suppresses with **no** output, and a refresh with no affected identity emits nothing.
- Native verification: **GREEN** — focused **64/64**, full native **2695 / 2688 succeeded / 7 skipped / 0 failed**.
- Build/RAM1 verification: **GREEN** on a clean rebuild — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom), byte-identical to the four previous slices; **no size pin moved** (the `Loop` accessors add no state, and the build's own `static_assert(sizeof(Loop) == 984)` proves it).
- Red-proofs: **six**, each restored byte-identically — the blanket silence restored on the edit-batch path; both refreshes clearing the ledger; the committed-pass identity source empty; the re-issuing direction closing instead of suppressing; the presence filter inverted; the committed-pass refresh clearing the ledger. Bookkeeping recorded: the previous slice's M38/M39 anchors moved into the shared core and M36b's target was deleted, so their successors here are M42/M43/M44 — and M44's first attempt "passed" only because its anchor was in the wrong **file** (an unmatched anchor is not evidence).
- **Task 3.14 stays OPEN** on: the refresh as a **request through the immutable publication owner** (3.14c — the two candidate sources are now both visible: the owner's companion passes, *unverified* as authoritative at undo time, versus the existing request path's caller-owned enumerated scratch; the choice is a design decision to state first), the between-grains matrix (3.7's serving path), retained-source lifetime and discharge (3.6a), displayed provenance, and preview-session undo/redo (3.13/2.12).
- Detail: [`evidence/2026-10-02-s314b-committed-pass-source/EVIDENCE.md`](evidence/2026-10-02-s314b-committed-pass-source/EVIDENCE.md).

### Increment 3.14c — the refresh's source question, verified (2026-10-02; task 3.14 stays open)

- **No production code changed in this increment.** It answers the source question both previous increments ended on, using the code's own terms, and pins the measured consequence in the fixture.
- **Both no-caller-buffer candidates are rejected on their own documentation:** `RuntimeCompanionPasses` is an *operation-scoped preparation* ("caller freezes active passes … and keeps their descriptors/events and rows immutable **until done**"; `ready(source)` requires `phase_ == Complete && source == expected_`), and `RuntimeCanonicalPassInputs` documents *"invalidate before pass/history mutation or release"*. The undo edge's act **is** a pass/history mutation — the very thing those structures say invalidates them — so sourcing a refresh from either would read content the same gesture has just invalidated.
- **The enumerated-scratch candidate is gated, measurably:** with a served window and a real `TrackUndo::undoForLoop`, readiness stops being `Ready` (the lane owes a rebuild, named in the owner's vocabulary) and `Track::requestPlaybackWindow` answers **`SourceNotCurrent`** — the lagging root is **named**, never served as current (DEC-073's protection, i.e. task 4.1's lagging-checkpoint outcome).
- **Therefore the refresh's requester belongs to 4.1 (a current root) and then 3.7 (the consumer)** — an ordering statement backed by two measurements rather than a deferral. What the gesture edge owes it already does: advance the currency (revision + generation), settle the closures from the change's own identities, keep the ledger (the closure-obligation set), silence nothing and enumerate nothing.
- Fixture (extended again): the served-window → undo → `not Ready` → `SourceNotCurrent` chain, plus the refusal leaving nothing in flight.
- Native verification: **GREEN** — focused **64/64**, full native **2695 / 2688 succeeded / 7 skipped / 0 failed**.
- Build/RAM1 verification: **GREEN** on a clean rebuild — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom), byte-identical to the five previous slices (no production code added, no size pin moved).
- Red-proofs: **three**, each restored byte-identically — a lagging root served as current (the not-current guard dropped); the undo no longer advancing the revision; the edit-batch site not settling. **M45/M46 deliberately mutate other owners' code** (`requestPlaybackWindow`'s guard, `setEditPassState`'s revision bump), because those are the facts the handoff rests on — a fixture that only fails when its own slice is undone would not test the claim it states.
- **Task 3.14 stays OPEN**: the refresh's requester (4.1 → 3.7), the between-grains matrix (3.7's serving path), retained-source lifetime and discharge (3.6a), displayed provenance, preview-session undo/redo (3.13/2.12). Its **audible** requirement and the red-proof the task demands are complete.
- Detail: [`evidence/2026-10-02-s314c-refresh-source-verification/EVIDENCE.md`](evidence/2026-10-02-s314c-refresh-source-verification/EVIDENCE.md).

### Increment 4.1 — checkpoint parity, named (F-5) (2026-10-02; task 4.1 stays open)

- **What landed:** `CheckpointParity { NoCheckpoint, Current, OwesReconciliation, CoverageUnrecorded }` and the **pure** `resolveCheckpointParity(hasRoot, coveredPassRevision, currentCommittedPassRevision)` in `include/LoopSnapshot.h`, placed beside the coverage constants they consume, plus `Loop::checkpointParity()` in `src/Loop/LoopSnapshotLifetime.cpp` — the same rule `publishedSnapshotIsCurrent()` applies, **named**, read from the three figures the loop already keeps. **No new state** (`sizeof(Loop) == 984` still holds, which the build's own assert proves) and **no size pin moved**.
- **Why a *name* and not a second rule:** the parity rule already existed as a **bool**, and the requirement's own words are that a checkpoint lagging the committed passes "SHALL be **named** in the readiness/lag vocabulary and SHALL NOT be presented as current" — nothing named it. The name cannot drift from the predicate, because `storePublishedSnapshot` resolves the `kSnapshotCoverageCurrent` sentinel into a real revision **as it stores it**: the stored figure is always a real revision or `kSnapshotCoverageUnset`, so for every reachable state `parity == Current` iff `publishedSnapshotIsCurrent()`. The fixture asserts that agreement directly, which pins the naming to the rule rather than to a paraphrase of it.
- **Neither undo nor save consults the window's lag:** `checkpointParity()` reads the loop's own figures only, and the fixture asserts that a slot whose **window lane is not ready** still reports a `Current` checkpoint — the window's lag is the window's business. This is also what makes it consistent with 3.14c's measured refusal (`SourceNotCurrent` after an undo): both describe the *checkpoint's* own coverage, never the window's.
- Fixture (**added**, one case in five sections): the pure rule over its four input shapes; the agreement with `publishedSnapshotIsCurrent()`; **through the production undo path** (`TrackUndo::undoForLoop` over a pushed entry whose root carried the pre-undo coverage) the answer `OwesReconciliation` **with `publishedSnapshotIdentity()` null** — selected, never presented as current, and the revision-advance precondition asserted so the case cannot pass vacuously; DEC-073's demotion named (`CoverageUnrecorded`); and the window's lag not consulted.
- Native verification: **GREEN**, re-run in this session — focused **65/65** (was 64; +1 fixture), full native **2696 / 2689 succeeded / 7 skipped / 0 failed** (baseline 2695 → +1).
- Build/RAM1 verification: **GREEN** on a **true clean rebuild** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B above the floor, byte-identical to the six previous slices): a header-inline rule plus one cold `Loop` function, no ITCM growth. **Caught in the act, recorded:** the first attempt was a **2.8 s cached link** rather than a rebuild, because `build_dir` points at `~/Library/Caches/pio-build-250513-215524-teensy41` and deleting `.pio/build/…` deletes nothing; the harness was corrected (the cache tree removed, rebuild 76.9 s) before the line was trusted.
- Red-proofs: **four**, each restored byte-identically — M48 the rule always answering `Current` (the lagging case reported as current); M49 the unrecorded sentinel not distinguished from a revision; M50 DEC-073's demotion dropped (a stamp that is not the live revision accepted as current); M51 the undo selecting the root **without** the coverage it carried. **A lesson repeated, recorded rather than glossed:** M50's first attempt reported "green" because its anchor sat in the wrong **file** (`storePublishedSnapshotForTest` lives in `Loop.h`), the same class of error as 3.14b's M44 — so the recommended harness improvement is that the anchor check assert the expected **path** alongside the occurrence count.
- **Task 4.1 stays OPEN** on one named item: the **content-level** comparison. "An undo restores exactly what its entry named" is verified here as the entry's restored **geometry** (S4b.4's existing fixtures) plus the **checkpoint authority** it names — this root, with this coverage; a byte-level comparison of restored content against the entry's named state is the content-verification pipeline's instrument, which is **4.3**'s own subject. **Save parity is 4.2**: its RC2 predicate already exists (`Loop::hasSettledPublishedSnapshotForSave()`, "coverage-current and no snapshot request still pending"), but 4.2's falsification of the `storePublishedSnapshot(…, kSnapshotCoverageCurrent)`-without-reconcile path is its own slice.
- **No production caller yet, stated plainly:** `Loop::checkpointParity()` is **named and asserted**, and its consumers are the slices that need it — save parity (4.2) and the readiness/lag vocabulary's reporting; nothing in `src/` calls it yet, so this increment adds an outcome that can be **named** rather than one already **displayed**.
- Detail: [`evidence/2026-10-02-s41-checkpoint-parity/EVIDENCE.md`](evidence/2026-10-02-s41-checkpoint-parity/EVIDENCE.md).

### Increment 4.2 — save parity is judged on the serialized root (F-6) (2026-10-02; task 4.2 stays open)

- **What landed:** `test_lsv1_save_parity_is_judged_on_the_serialized_root` in `test_storage_loop_io` — the spec's *Save parity is judged on the serialized root* scenario, driven through the **production writer** (`writeLoopPersisted`) and the **production restore edge** (`stepLoopSnapshotBodyFormatV1LoadParse` → `applyLoadedLoopSlotBodyCommit`): the installed root is **equivalent** to the root that was serialized (content, not address — the round trip rebuilds the object, which my first version got wrong by asserting pointer identity), the body's committed state arrives with it, and the restore's coverage outcome is **named** (`CheckpointParity::Current`, `publishedSnapshotIdentity()` non-null). Plus a **comment-only** production edit: the restore edge's over-claiming DEC-073 comment is replaced by the boundary statement — the claim rests on the **writer's** admission (RC2), not on a derivation — so **no behaviour change**.
- **A verified gap, not an assumed one:** the LSV1 root section persists **no provenance figure**; the reader validates section presence and each section's internal structure and **never** the root's correspondence to the persisted passes; and the pre-existing restore-edge fixture asserted only presence. So the spec's equivalence clause was asserted **nowhere** at the restore edge, and nothing judged its coverage at all.
- **The falsification is measured, not asserted:** a temporary probe (removed before commit — it would pin wrong behaviour) built a body the reader accepts whose two sections cannot describe one committed state (metadata `nextNoteId` = the root's + 7). Today's path installs a root that does **not** come from the restored state and reports it `CheckpointParity::Current` with a non-null identity — exactly the "silently current" outcome the requirement forbids, and D7's target. **Side-finding recorded because it changed the measurement:** the reader merges the metadata section **into** the oracle, so a divergence placed only on the oracle is repaired before the restore edge sees it; the incoherence has to be metadata-vs-root to survive.
- **The repairs, one refuted and two owed:** (i) persist the root's coverage in the body — a **format decision** (LSV1 v1 is pinned, the single flag bit is strictly validated, and the root payload has no provenance field to extend in place); (ii) a cheap cross-check of the root's own figures against the committed state's — **refuted by reading the code** (`EditStates/EditSelectNoteState.cpp:188` → `Loop::allocateNoteId()`, so `nextNoteId_` can advance while the committed state — and a settled root — is unchanged, and the guard would name a *legitimate* body stale); (iii) reconcile the root against the body's own passes at load — owed, a design task across two representations.
- Native verification: **GREEN** — focused `test_storage_loop_io` **52/52**, `test_runtime_working_set_ownership` **65/65**, `test_playback_prewarm` **190/190** (the cited pin, unchanged), `test_midi_service_drain` **38/38**; full native **2697 / 2690 succeeded / 7 skipped / 0 failed** (baseline 2696 → +1).
- Build/RAM1 verification: **GREEN** on a true clean rebuild (71.5 s) — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B above the floor), byte-identical to the seven previous slices.
- Red-proofs: **four**, each applied to a **named** file at an anchor whose **occurrence count** was asserted first (the 4.1 lesson), each restored byte-identically — the coverage claim degraded to `CoverageUnrecorded`; the install skipped; the oracle not applied; the writer's root section omitted. **And their bound, stated rather than glossed:** every outcome assertion in this fixture is the *positive* case, so it cannot falsify an **over-claiming** parity — which is precisely why the over-claiming case had to be a measurement, and why the repair is owed.
- **Task 4.2 stays OPEN** on that repair (candidate (i) or (iii)). Its **verification** half — equivalence at the restore edge and a *named* coverage outcome — is landed, with the falsifying case recorded as a reproducer rather than a description.
- Detail: [`evidence/2026-10-02-s42-save-parity/EVIDENCE.md`](evidence/2026-10-02-s42-save-parity/EVIDENCE.md).

### Increment 4.3 — content parity across a window boundary (F-7) (2026-10-02; task 4.3 stays open)

- **What landed:** `Track::playbackWindowEventAtForSlot(slot, cursor, out)` — **the served window's ordered event stream**, rendered exactly as the emission path renders it (`storage.events[storage.order[cursor]]`), read-only, O(1), no new state, and it reads the window that is **served** (complete, published, valid interval), never a prepared successor. Plus `test_windowed_stream_concatenates_to_the_whole_loop_stream_across_a_boundary`: **32 successive windows** over a 64-bar demoted loop, concatenated and compared **row for row** with the whole-loop stream, including the note whose closure crosses a boundary.
- **The verified gap this closes:** the lane exposed identity, coverage, a count and a presence predicate — and **no way to read what a window holds**. Nothing in production consumes a window's events (the emission path still reads bridge/pair/oracle/merged, and `stepPlaybackViewMaintenance` returns `Idle` in production because nothing requests a window), and no native fixture read a built window's content. So the spec's parity clause had **no observable** on this side, which is why the emission switch (3.7) needed this door first.
- **The reference is the existing instrument, not a second one:** the whole-loop stream comes from `LoopContentResolution::resolveWindow(snapshot, 0, loopLengthTicks, out)` — the project's content resolver at whole-loop width, the same primitive the content-verification pipeline's `effective` inventory dump is built on. The **device** leg of that pipeline (with the windowed stimulus profile) is 6.4; the native/hardware split is 6.5.
- **Parity holds, measured:** the concatenation equals the whole-loop stream row for row and in order (tick, type, channel, note) across all 31 boundaries — nothing lost, duplicated or reordered; the boundary note's **ON is served by tile 0 and its OFF by tile 1** (not synthesized at the edge); and the concatenation **balances**, so no note-on is left unclosed by the transition, read where that is a property of the lane rather than of a consumer. The two publication paths are also pinned as measured: the **first** window is installed as the served one, every **replacement** publishes as the ready successor and the consumer-side edge adopts it.
- **The obligation the measurement names:** because a window's stream is a half-open tick filter, a note sounding across a boundary legitimately ends its window unclosed and begins the next unmatched — the **sequence** is balanced while either window alone is not, so the transition's closure is the **consumer's** obligation (the ledger's set: 4.5's `ClosureSettlement`, 3.14's `NotePresenceSet`), owed to **3.7**'s emission switch. Likewise the interval policy is *centred*, so consecutive requests at arbitrary phases **overlap**: a consumer must not double-serve the overlap (3.7), which is why this slice's parity claim is stated for the tiling sample.
- Native verification: **GREEN** — focused `test_runtime_working_set_ownership` **66/66**, `test_storage_loop_io` **52/52**, `test_playback_prewarm` **190/190** (the cited pin), `test_midi_service_drain` **38/38**; full native **2698 / 2691 succeeded / 7 skipped / 0 failed** (baseline 2697 → +1).
- Build/RAM1 verification: **GREEN** on a true clean rebuild (82.4 s) — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B above the floor), byte-identical to the eight previous slices.
- Red-proofs: **four**, each applied to a named file at an anchor whose occurrence count was asserted first, each restored byte-identically — the interval policy losing its centring; the window span; the accessor reading the **ready successor** instead of the served window; the accessor's bounds check dropped. **And their bound:** mutations that break the lane's output are caught by the fixture's interval/coverage pins *before* the row comparison runs, so the row comparison is the claim those pins protect rather than a separately red-proved assertion — recorded as the fixture's reach.
- **Task 4.3 stays OPEN** on three named items: its **device leg** (6.4's windowed profile with 6.5's split — and `ExecutionBounds.md` row 27 still denies active windowed playback until its bounds are measured), the **closure obligation at the transition** (3.7 with 4.5), and the **overlap rule** for consecutive arbitrary-phase windows (3.7). The **loop-wrap** boundary is deliberately not in this fixture and is recorded as such.
- Detail: [`evidence/2026-10-02-s43-window-boundary-parity/EVIDENCE.md`](evidence/2026-10-02-s43-window-boundary-parity/EVIDENCE.md).

### Increment 4.4 — length-independent residency, swept over loop lengths (F-8) (2026-10-02; task 4.4 stays open)

- **What landed — test-only, no production change:** `test_demoted_slot_residency_is_length_independent_swept_over_lengths`, a sweep over **16, 17, 24, 30, 32 and 64 bars** (one slot per length), each set up through production entries only — `Loop::fullTakePlaybackViewAdmissible()` as the policy and `Track::prewarmPlaybackForSlot()` as the admission, **which decides the representation itself** so the fixture never states the policy to the code — and read back from the slot's own accounting via the existing seam (`wholeTakeViewsReserved()`, `fixedExternalBytes()`, the views' capacities, `captureStagingsPrewarmed()`).
- **The F-8 claims, verified:** the policy boundary is the declared bar cap (16 ✓ / 17 ✗); the whole-take representation is reserved **exactly** while admissible and **absent at every demoted length** ("no whole-take reservation SHALL be restored for demoted slots"); and **every demoted length reports the same windowed reservation** — length-independent by construction, and strictly smaller than the admissible figure, so the bundle is absent rather than merely unused. Every view's storage is the **declared capacity** at every length (the requested companion-row horizon does not leak into it), and the stagings stay. At and above the declaration's floor the slot is **served from a window while its root is demoted**.
- **The floor, turned into a number:** the request's answer equals the declaration's own `canCompleteBeforeHandoff` at every length, and the fixture **re-derives** the lead from the contract's declared fields and requires the sweep to match that arithmetic exactly. With the declaration's conservative `maximumPhaseAdvancePerTurn` (the *unmeasured* term the code itself flags), the lead is **22,272 ticks ≈ 29 bars**, so **the floor is exactly 30 bars** and loops between the bar cap and the floor have neither representation admissible. The declaration's own comment already says this in prose ("admits only a loop long enough to hold the lead below"); this slice supplies the number task 6.3's sweeps and task 3.10's envelope need. Below the floor the refusal is **named**, nothing is half-prepared and no window is served.
- Native verification: **GREEN** — focused `test_runtime_working_set_ownership` **67/67**, `test_storage_loop_io` **52/52**, `test_playback_prewarm` **190/190** (the cited pin), `test_midi_service_drain` **38/38**; full native **2699 / 2692 succeeded / 7 skipped / 0 failed** (baseline 2698 → +1).
- Build/RAM1: **SUCCESS** and a **no-op** (2.7 s, no source changed) — `variables 96,192` / `code 392,328 + padding 888` (48 B above the floor): a test-only slice cannot move the image.
- Red-proofs: **three**, each at a named file with its anchor count asserted and each restored byte-identically — the admission reserving the whole take for a demoted loop; the admissibility boundary moving one bar; the window storage no longer being the declared capacity. **Two limits stated rather than glossed:** the admissibility oracle is the same policy function production consults, so mutating the contract's *declared numbers* moves the fixture with the code and is deliberately not detected here (it pins declaration-vs-consequence consistency, and 3.10/6.3 own whether the numbers are right — which is why the floor is also derived independently); and the sweep pins the reservation's **invariance**, not its absolute figure, which `test_windowed_only_slot_reserves_no_whole_take_representation` already owns and is cited for.
- **Task 4.4 stays OPEN** on: the **parity half** with the content-verification pipeline (6.4's windowed profile, 6.5's split), the **retained-source figure** as a recorded term (3.6a's accounting), and the sweep's widening (6.3's receipt/timing sweeps). No device evidence; row 27 still denies active windowed playback.
- Detail: [`evidence/2026-10-02-s44-length-independent-residency/EVIDENCE.md`](evidence/2026-10-02-s44-length-independent-residency/EVIDENCE.md).

### Increment 3.10 — fair, bounded, deadline-ordered service at the holder level (DEC-108 §13b) (2026-10-02; task 3.10 stays open)

- **What landed:** the **holder-level** selection policy, pure and header-only in `include/Utils/MidiServiceDrain.h` — `PlaybackHolder` (eligible / can progress / outstanding deadline), `PlaybackHolderCursor`, the named `PlaybackHolderSelection { None, Service, AllBlocked }` and `selectPlaybackHolder(cursor, holders, count, out)`. No allocation, no new state beyond the caller's cursor. Fixture: `test_playback_holder_service_is_fair_deadline_ordered_and_never_held_by_a_blocked_holder`.
- **Why the holder level is the gap, verified rather than assumed:** the **owner** rotation already exists *and is already proved* (`nextDeferredIdleOwner` cycles unconditionally before eligibility is known; the existing `test_select_deferred_idle_owner_backlog_snapshot_two_of_eight` asserts one turn per owner per cycle). Below it, which *holder* an admitted `PlaybackViewMaintenance` turn serves was ascending first-match with **no cursor**, and **no deadline was stored at all** — `beginCoverageBuild` takes `handoffPhase` and drops it, so "earliest outstanding playback deadline" had no value to order by.
- **The deadline is not new state:** it is the phase at which the playhead next enters the interval the build serves — the figure `Track::requestPlaybackWindow` already computes and passes as `handoffPhase`, so the policy reads an answer the caller already has rather than adding a second currency.
- **The oracle, as five bounds:** (A) with every holder eligible and able, `count` selections serve each holder **exactly once** and the next rotation continues from the cursor; (B) earliest outstanding deadline first, with deadlines deliberately in the reverse of the holder order; (C) a holder with a deadline outranks every holder without one, and deadline-less holders still rotate **evenly**; (D) a **blocked holder is never chosen**, the others keep the one-rotation bound, and it is served within one rotation of becoming able — skipped over, never lost; (E) "nothing owed" (`None`) is distinct from "all owed and blocked" (`AllBlocked`), `holderOut` is `kNoPlaybackHolder` in every non-serving outcome, and the rotation **advances** in both (never a latch).
- **Reading chosen, stated:** earliest-deadline-first is applied **globally** over the eligible set — the tighter of the two readings of "within a holder", since a global order cannot serve a later deadline first inside any one holder; the per-holder reading follows a fortiori and the property is stated once.
- Native verification: **GREEN** — focused `test_midi_service_drain` **39/39**, `test_runtime_working_set_ownership` **67/67**, `test_storage_loop_io` **52/52**, `test_playback_prewarm` **190/190** (the cited pin); full native **2700 / 2693 succeeded / 7 skipped / 0 failed** (baseline 2699 → +1).
- Build/RAM1: **SUCCESS** (36.7 s, the header's dependents recompiled) — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B above the floor): the policy is inline and unconsumed, so it emits no code.
- Red-proofs: **four**, each at the named header with its anchor count asserted and each restored byte-identically — the scan ignoring the cursor; a blocked holder served anyway; the deadline order inverted; the cursor resuming on the served holder instead of after it. **The first attempt at the fixture's outcome block was wrong, not the code** (no eligible holder is `None`, and `canProgress` defaults true, so the block was corrected to distinguish both outcomes) — recorded rather than quietly fixed.
- **Task 3.10 stays OPEN** on four named items: the **wiring** (no production caller yet — the holder scan in `Track::stepPlaybackViewMaintenance` and the `src/main.cpp` track scan are still ascending first-match, and nothing carries the outstanding handoff phase per holder); the **starvation half** (capture finalization / persistence keep only *structural* opportunities today, and their owner is `src/main.cpp` + the track-level body); the **envelope derivation** from grain costs and the worst permitted *gap between service opportunities* (this slice bounds turns and names the gap measurement as owed — the same unmeasured `maximumPhaseAdvancePerTurn` the coverage contract flags; no microsecond budget is invented); and **"one declared grain per turn"** as a uniform invariant, which is not there to assert yet (`Snapshot` declares two units and `processDeferredIdleMaintenance` runs several children per owner call).
- Detail: [`evidence/2026-10-02-s310-holder-service-fairness/EVIDENCE.md`](evidence/2026-10-02-s310-holder-service-fairness/EVIDENCE.md).

### Increment 3.10 (increment 2) — the urgency figure, named once and shared (2026-10-02; task 3.10 stays open)

- **What landed:** `handoffLeadTicks(currentPhase, handoffPhase, loopLengthTicks)` in `include/RuntimePlaybackViewBuild.h` — the tick distance along the loop to a handoff, **named once** beside the contract whose `available` sub-term it is — and `canCompleteBeforeHandoff` now **calls it** instead of computing the same expression inline, so the admission and any scheduling reader cannot diverge. The holder policy orders by that tick distance rather than by a raw phase.
- **The fidelity fix, stated plainly:** increment 1 ordered holders by a raw *phase*, which is only meaningful within one loop. The fixture now pins the pair that shows why — a holder 1,400 ticks out on a 2,000-tick loop versus one 18,200 ticks out (a whole pass) on a 20,000-tick loop, where the **more urgent holder carries the larger phase**. The tick distance is also the quantity the admission already judges, so naming it once makes the scheduler's urgency and the admission's margin the same number.
- Fixture: block **(F)** added to the holder-service fixture (the derivation, the wrap case, and the refusals for an undeclared loop and an out-of-range phase, each answering `0` rather than wrapping); blocks (B)/(C) restated in ticks.
- Native verification: **GREEN** — `test_midi_service_drain` **39/39**, `test_runtime_working_set_ownership` **67/67** (the admission side of the shared definition, unchanged by the refactor), full native **2700 / 2693 succeeded / 7 skipped / 0 failed**.
- Build/RAM1: **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B above the floor): the shared inline helper compiles to the same code the inline expression did.
- Red-proofs: **three**, each at a named file with its anchor count asserted and each restored byte-identically — the wrap branch dropped (fails the new fixture **and 11 cases in the sibling suite**, the admission side of the same definition; the *named* margin fixture happened to still pass, which is not claimed), the out-of-range refusal dropped, and "a deadline beats none" inverted.
- **Two of this task's own fixture attempts were wrong, not the code** — increment 1's outcome block, and here an assertion that claimed equal phase distances differ with loop length (they do not; the difference is the wrap) — both recorded rather than quietly corrected.
- **Task 3.10 stays OPEN** on the same four items: the **wiring** (no production caller; nothing yet computes the lead for a live holder), the **starvation half**, the **envelope derivation** from grain costs and the worst permitted service gap, and **one declared grain per turn** as a uniform invariant.
- Detail: [`evidence/2026-10-02-s310b-shared-handoff-lead/EVIDENCE.md`](evidence/2026-10-02-s310b-shared-handoff-lead/EVIDENCE.md).
