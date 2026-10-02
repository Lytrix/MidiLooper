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
