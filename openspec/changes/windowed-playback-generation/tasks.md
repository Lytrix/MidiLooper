# Tasks — windowed-playback-generation

Requirements: `specs/windowed-playback-generation/spec.md`. How: `design.md`. The contract: **DEC-108**; the trigger
record: `ARCHITECTURE-REASSESSMENT.md`. Sequencing and inherited criteria: `proposal.md` §Inherited contract (`S4C1`
AC-1…AC-9, `S4C45` F-1…F-8).

**Gate on the whole change:** DEC-108 and the architecture reassessment **landed 2026-10-02**; what remains before a
firmware edit is the `DECISION_REVIEW.md` pass (§1.2), the **five G2 blockers** (§1.6) and the G2 execution packet for
the owner slice (§1.5). Every
implementation slice additionally needs its own packet ([`docs/Templates/EXECUTION_PACKET.md`](../../../docs/Templates/EXECUTION_PACKET.md))
with its scope, transition oracle, controllable production-path fixtures and evidence recorded before checkoff.
Nothing in this file may be claimed as proven: DEC-108's bounds are unproven until the checks below pass.

## 1. Decision gate (before any firmware edit)

- [x] 1.1 Record the change's contract as **DEC-108** in `docs/DECISION_LOG.md` — the window authority, its owner,
  the reservation, retention, identity, readiness, deadline, limits, envelope and the demoted hold. *Recorded
  2026-10-02; it states "not implemented" and lists the evidence owed.*
- [x] 1.2 Complete [`docs/Templates/DECISION_REVIEW.md`](../../../docs/Templates/DECISION_REVIEW.md) for this change —
  verify the completed review is on record and its open items are either closed or carried as named tasks here.
  *Landed 2026-10-02 as [`DECISION_REVIEW.md`](DECISION_REVIEW.md): the prior decisions this change inherits are
  recorded with their extension points, **reuse decision YES**, **migration NO**, and two findings — **F-1** (authority
  conflict, now carried as task 1.7) and **F-2** (three dangling fragments in this change's own spec, repaired in
  place; no requirement changed). No bound was measured and no claim was made.*
- [x] 1.3 Record the architecture reassessment in `ARCHITECTURE-REASSESSMENT.md` per
  [`docs/ARCHITECTURE_REASSESSMENT.md`](../../../docs/ARCHITECTURE_REASSESSMENT.md) — the new-owner, ownership-transfer
  and timing-model triggers, the current architecture, the proposed evolution, the migration impact and a single
  recommended path. *Recorded 2026-10-02.*
- [x] 1.4 Record the four previously open decisions — the per-window capacity and comparison span, the window-size
  tunable, the envelope targets and the demoted-target hold — with their rationale in `design.md` and DEC-108, leaving
  no TBD entry behind. *Recorded 2026-10-02; verify by re-reading `design.md` §"Recorded decisions".*
- [x] 1.5 Author the G2 execution packet for the **owner slice** (§3.1) — verify the packet names scope, the transition
  oracle, the fixture established before the behaviour change, and the evidence to be recorded.
  *Landed 2026-10-02 as [`G2-EXECUTION-PACKET.md`](G2-EXECUTION-PACKET.md): scope and non-goals, the eight-case
  transition oracle (W-1…W-8, with the mutation that must fail each), the reused seams and the one recorded
  untestable-before-link case (`src/main.cpp` is not native-linkable), the focused/broad verification recipe, and a
  completion record whose every field reads **NOT RUN**. It also picks the one code-shape choice `design.md` left open
  (a new `DeferredIdleOwner` turn kind, on the `SlotProvision` precedent) and records its consequence for the rotation
  fixture. No fixture was implemented and no run was made.*
- [x] 1.6 **Record the five G2 blockers' contract** — decided 2026-10-02 as **DEC-108 §12**, then **corrected by the
  second review pass**: the retained-source **hold** (not an ID list), the **inspected-work** bound, the admission
  accounting unit, the clock basis and the preview contract. *Recorded 2026-10-02 in `design.md` §"Recorded
  resolutions" (second pass): the retained-source **hold** — **pinned bodies plus the effective-content resolver**, not an
  ID list; the **inspected-work** bound; the admission limit accounted on the **capture interval at acceptance** in
  effective events plus reserved closure capacity; the corrected **20–300 BPM** clock basis; and preview moved to
  **immutable deferred publication**. Each fixture sits with the slice that delivers it (§2.9→§3.6, §2.10→§3.2,
  §2.11→§3.12, §2.12→§3.13) and gates only that slice, not the owner slice.*

- [x] 1.7 Resolve **F-1** of [`DECISION_REVIEW.md`](DECISION_REVIEW.md) — the change named `LoopContentResolution` as the
  effective-content owner it extends (spec *One pass late is the only stale authority*, `design.md`
  §"Retained-source resolver and interval index", DEC-108 §14(c), `docs/Runtime/CURRENT_WORK.md`), while
  [`NAMING.md`](../../../docs/Authority/NAMING.md) forbids that name in new docs and gives committed effective state to
  the selected **`LoopSnapshot`**, and DEC-037 keeps it a parallel prototype whose caller gates never passed.
  *Resolved 2026-10-02 by the owner as **path A** — restate the owner in NAMING vocabulary and keep every behavioural
  clause.* Applied to the change's `spec.md`, `design.md` (the G2 row 3 resolution, §"Recorded resolutions" 1 and
  §"Retained-source resolver and interval index"), `proposal.md`, `tasks.md` §3.2/§3.6, **DEC-108 §14(c)** (in-place
  correction with its reason recorded; **no new `DEC-###`**, since no decision changed) and
  `docs/Runtime/CURRENT_WORK.md`. **`NAMING.md` and DEC-037 are untouched** (nothing superseded). The naming gate is
  discharged for tasks 3.2/3.6/3.12; task 3.1 was never gated.*

## 2. Fixtures — each placed with the slice that implements it

Every fixture is written **before** the behaviour it pins and each is red-proved (named mutation, byte-identical
restore). **The four-stage ladder is the rule, per slice: (1) fixture established and shown to fail for the right reason
→ (2) behaviour implemented and verified against it → (3) production path enabled → (4) the old path removed.** A fixture
is therefore *not* a blanket pre-gate on unrelated work and is **not** required to be green before the slice that
implements it exists: 2.1–2.8 are established with their slices, while 2.9–2.12 each sit with the slice named beside
them. Nothing in this section may stand in for production behaviour with a stub that returns success.

- [x] 2.1 Land **F-1** (window identity and immutability) — verify the fixture asserts
  `(slot, playbackGeneration, playbackRevision, interval)` identity, that a consumer read cannot straddle a
  replacement, and that a consumer cannot mutate the window
- [ ] 2.2 Land **F-2** (readiness per affected tick inside the deadline) — verify the oracle is the **derived** deadline
  (tempo, quantisation, phase advance, service availability) rather than a bar count or a fixed millisecond figure, and
  that it covers a short and a much longer loop in the same shape
- [x] 2.3 Land **F-3** (one pass late, and only that) — verify the previous revision of *that interval* stays
  authoritative for the traversal, that a **second** miss is reported as a named breach, that the allowance is not reset
  by a further edit, and that the consumer is never blocked
  *Checked off 2026-10-04 in [`RECONCILIATION.md`](RECONCILIATION.md) §1:* `test_playback_lag_rule_serves_the_previous_revision_once_and_names_a_second_miss`
  (registered in `RUN_TEST`) asserts all four clauses and the rule is total by construction, so a consumer cannot be blocked; **M4** (a further edit
  resets the allowance), **M5** (a published replacement ignored) and **M6** (the once-per-episode guard dropped) are red-proved and restored
  byte-identically ([`evidence/2026-10-02-s36b-lag-rule/EVIDENCE.md`](evidence/2026-10-02-s36b-lag-rule/EVIDENCE.md)). Nothing in the task remains owed.
- [ ] 2.4 Land **F-4** (nothing allocates or blocks on the edge) — verify the fixture fails when the commit edge takes
  capacity or builds inline, in the S4b.6/S4b.7 shape
- [x] 2.5 Land the **empty-interval readiness** fixture — verify a completed publication over an interval with no events
  is ready and sounds as silence, and that the fixture fails under today's non-empty term
  *Checked off 2026-10-04 in [`RECONCILIATION.md`](RECONCILIATION.md) §1:* `test_window_readiness_names_its_holder_and_an_empty_interval_is_ready` (registered)
  asserts a completed publication over an event-empty interval is `Ready` and that every non-ready answer names its holder; the silence half is asserted
  on the serving path by `test_cross_generation_closures_are_reconciled_and_an_empty_window_silences_nothing`; the pre-change **non-empty term** is
  red-proved by two mutations restored byte-identically ([`evidence/2026-10-02-s35-readiness-holder/EVIDENCE.md`](evidence/2026-10-02-s35-readiness-holder/EVIDENCE.md)).
- [x] 2.6 Land the **stale deferred completion** fixture — verify a completion arriving after an ownership change is
  rejected or re-attributed through the playback generation rather than published
  *Checked off 2026-10-04 in [`RECONCILIATION.md`](RECONCILIATION.md) §1:* `test_shared_view_transitions_state_the_ownership_rules_and_a_late_completion_publishes_nothing`
  (registered) asserts the late completion is refused through the operation generation and publishes nothing; **M10** (publication guard dropped),
  **M11** (identity not invalidated on clear/load) and **M12** are red-proved and restored byte-identically
  ([`evidence/2026-10-02-s311-shared-view-transitions/EVIDENCE.md`](evidence/2026-10-02-s311-shared-view-transitions/EVIDENCE.md)).
- [ ] 2.7 Close the measured coverage gap for the demoted shape — verify a fixture **fails** when the window build is
  removed on a demoted slot (RWO recorded that dropping it left every fixture green and pinned that as a harness
  tripwire)
- [ ] 2.8 Red-prove each fixture **inside the slice that implements it** — verify the named mutation fails that fixture at
  that slice and the file is restored byte-identically, with the focused suite green **once its behaviour exists** (a
  fixture whose behaviour has not landed yet is established and expected-red, never counted as passing)

- [ ] 2.9 **Retained-source hold/release fixture** — *slice §3.6*. Pause a window build, replace the published root, undo
  and reclaim passes, run the direct reset/replacement path (IDs reused), attempt slot release and load, then resume:
  assert exact old-version **effective** content, no stale aliasing, protection on both the reclamation consumer and the
  reset path, and eventual release. *(G2 pass 2, row 7)*
- [x] 2.10 **Inspected-work bound fixture** — *slice §3.2; landed with it*. Keep the requested window identical while adding
  non-intersecting chunks across progressively longer loops, including a window that intersects nothing: assert inspected
  work per grain is bounded (rejected chunks and lookup included) and total preparation cost does not grow with unrelated
  content. *(G2 pass 2, row 6 — a chunk quantum alone is not the oracle)*
- [ ] 2.11 **Acceptance-accounting fixture** — *slice §3.12*. Exercise one event below the limit, at the limit and above
  it, including same-tick events accumulated across passes, a `W`-wide interval crossing the loop wrap, a note closure at
  capacity, an edit that moves events into a full interval, and a loop restored from SD that predates the limit (served
  through bounded pages with cross-page closures): assert refusal by name at admission, complete ordered output for what
  was accepted, and that stop/switch/undo are never refused. *(G2 pass 2, row 8; G2 pass 3, rows 1–2 → DEC-108 §14)*
- [ ] 2.12 **Preview-source fixture** — *slice §3.13*. On a long loop, edit while playing, cross window boundaries,
  perform edit-session undo/redo, switch selected tracks, mutate or exit the session **between build grains**, then save:
  assert the preview is identified as preview, is invalidated by its own lifecycle, publishes no mixed or stale content,
  and never counts as committed currency. *(G2 pass 2, row 7 on preview lifetime)*

## 3. The window authority (owner first)

- [x] 3.1 Land the owner `Track::stepPlaybackViewMaintenance` with the declared `CoverageServiceContract` — verify its
  quantum, persistent build cursor, bounded consecutive denied turns and demonstrated handoff margin are implemented and
  asserted, that it is reachable in the states a pending window sits in, and that no new scheduling owner is introduced.
  **No behaviour changes behind it in this task** (`ExecutionBounds.md` rows 27–29 exclude active windowed playback from
  admission until these caps are proven)
  *Landed 2026-10-02. The owner, its declared contract and its eight-case oracle are in place (predicate truth table +
  owner-rotation control in `test_midi_service_drain`, three fixtures in `test_runtime_working_set_ownership`), four
  mutations are red-proved with byte-identical restores, the host is green (**38/38, 48/48, 190/190**, full native
  **2679 / 2672 succeeded / 7 skipped / 0 failed**) and `teensy41-capture-serial` builds through the RAM1 guard with the
  **approved line unchanged** (`variables 96,192`, padding 1,208, floor 840). Read "bounded consecutive denied turns" as
  the **declared allowance the admission reserves** (`canCompleteBeforeHandoff`'s `grains * (denied + 1)`): the owner's
  own turn cannot produce a repeatable denial run today, because a denied grain is terminal in the current build API —
  a repeatable run needs a request that survives a terminal denial, which is 3.2's. See
  [`evidence/2026-10-02-s31-window-owner/EVIDENCE.md`](evidence/2026-10-02-s31-window-owner/EVIDENCE.md)*
- [x] 3.2 Publish **immutable interval windows** out of the owner — verify publication is atomic, that a whole-loop
  coverage interval is treated as coverage spanning the loop, that enumeration goes through the effective-content
  resolver over the selected `LoopSnapshot` (the published-snapshot window gather) against the generation-qualified
  pinned source rather than raw committed chunks (§14), and that no whole-take
  reservation is restored for a demoted slot
  *Landed 2026-10-02; all four clauses verified.* **Publication is atomic**: the owner publishes inside the turn
  that completes the build (`RuntimePlaybackViewLifecycle::publishCompletedBuildAsCurrentWindow`, which keeps the
  "only a complete view is ever installed" invariant and performs `handoff`'s atomic swap — a demoted slot's
  *first* window had no publication path before it), the window carries its identity and coverage,
  `Track::publishedPlaybackWindowForSlot` reads it, and a second window is refused by name
  (`WindowAlreadyPublished`; **renamed `SuccessorAlreadyPrepared` by 3.4** — see the superseding note at the end of this record) with the published window unchanged. **A whole-loop interval is coverage spanning the
  loop**: `Track::playbackWindowInterval` resolves it for a loop the whole-take view serves, and the two-bar
  window at the playhead for a demoted one (one policy, shared with the live build). **Enumeration goes through
  the effective-content resolver**: `Track::requestPlaybackWindow(slot, phase, scratch)` enumerates the selected
  `LoopSnapshot`'s window membership via `Loop::tryGatherPublishedSnapshotEventsInWindow` — never raw committed
  chunks — refuses a rootless slot (`SourceUnavailable`) or a non-current root (`SourceNotCurrent`) by name, and
  stamps `{playbackRevision, playbackGeneration, publishedSnapshotIdentity}`. **No whole-take reservation for a
  demoted slot**: the publication in the fixture's windowed slot succeeds through the windowed primitive while
  `fullTakePlaybackViewAdmissible()` is false. Fixture **2.10** is asserted through the gather's new
  inspected-work counter: identical inspected work (4 references) for the same window on a 4-bar and a 64-bar
  loop, and 0 for a window whose bars carry no membership.
  *Verification:* this suite **50/50**, `test_midi_service_drain` 38/38, `test_playback_prewarm` 190/190, full
  native **2681 / 2674 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** with RAM1
  `variables 96,192` unchanged (no re-base); four mutations red-proved across the two increments, each restored
  byte-identically.
  *Named, not claimed:* an ordinary loop's whole-loop interval is refused by the declared contract's still
  **unmeasured** per-turn phase advance (task 6.3's measurement is what makes it admissible), and *replacement*
  sequencing is 3.4/3.5's successor lane (per the measured finding in
  [`evidence/2026-10-02-s32-interval-publication/EVIDENCE.md`](evidence/2026-10-02-s32-interval-publication/EVIDENCE.md)).
  *Superseded 2026-10-02 (slice 3.4, F-1):* the refusal this record names was a **placeholder for the successor lane**,
  and the lane now exists. `WindowAlreadyPublished` is renamed **`SuccessorAlreadyPrepared`** — a window merely
  *being served* is not a bound, because preparing its replacement **is** the successor lane; what is bounded is a
  second *prepared* successor. The measured finding above is therefore **resolved, not worked around**: the
  completed build routes to the `ready` successor and the served window is untouched until one atomic
  `handoffReadyCoverage`. Detail: [`evidence/2026-10-02-s34-window-identity/EVIDENCE.md`](evidence/2026-10-02-s34-window-identity/EVIDENCE.md).
- [x] 3.3 Budget the representation — verify current, ready and building windows (with reader retention, staging and
  transition overlap) are accounted, that the three rolling views' lifetimes are shown to suffice **before** any buffer
  is added, and that `test_playback_prewarm` stays green unchanged
  *Landed 2026-10-02 as [`S33-REPRESENTATION-BUDGET.md`](S33-REPRESENTATION-BUDGET.md).* The account: the lifecycle's
  five role names map onto **three** buffers, because `retained` always aliases `current` or `build` (`retainCurrent`,
  `handoff`, the set's `resetMetadataPreserveRetained`) and `idle` is a sentinel — and this change's publisher uses
  only `current + build`, refusing outright to recycle the retained view. **No buffer is added:** retention is the
  DEC-108 item 4 **pinned source** (3.6's term), not a fourth window; the windowed lane reserves no bridge and no
  whole-take base, so it has no transition overlap to budget; and the pins (`sizeof(LoopPlaybackRuntime) == 6368`,
  `sizeof(Track) == 384`) plus the RAM1 line (`variables 96,192`) are all unchanged — measured, not asserted. The
  holder's cost is the pre-existing "three views + two stagings" (333,824 B ≈ 326 KiB on the target ABI, from the
  same 35/29 B-per-event figures RWO's `S2C1` derived). `test_playback_prewarm` is **190/190 unchanged** and is
  cited, not restated: no new fixture duplicates the capacity arithmetic that suite already pins.
- [x] 3.4 Carry the identity `(slot, playbackGeneration, playbackRevision, interval)` on the published window — verify
  F-1 passes and that the generation guards ownership changes rather than becoming a second currency
  *Landed 2026-10-02; F-1's three clauses are asserted, and 2.1 is checked with it.* `PlaybackWindowIdentity` carries
  `{slot, playbackGeneration, playbackRevision, interval}` with equality over exactly those fields (the snapshot
  identity stays provenance *inside* `PlaybackViewSourceIdentity`), `Track::publishedPlaybackWindowIdentityForSlot`
  is the const, copy-returning consumer accessor, and `Track::adoptPreparedPlaybackWindow` is the consumer-facing
  adoption (one atomic `handoffReadyCoverage(Preparation)`), **not yet called by production** (3.7 owns the eligible
  boundary). Replacement became the **successor lane**: `RuntimePlaybackViewLifecycle::publishCompletedBuildAsReadySuccessor`
  accepts the *incomplete* prewarmed shell at `ready` — `publishCompletedBuildAsReady` refuses it, so a lane without a
  record-return shell could never publish a replacement — while keeping "one **real** successor at a time" and "only a
  complete view is ever installed", and the completed build **routes** by what it replaces (no window → `current`;
  a window being served → the `ready` successor, leaving the served window untouched). The request bound changed with
  it: `WindowAlreadyPublished` → **`SuccessorAlreadyPrepared`**, because a window merely *being served* is not a bound
  while a second *prepared* successor is. **The generation guards ownership, not currency:** a second request at the
  same revision and the same ownership but a **different interval** leaves generation *and* revision unchanged while
  the interval moves (DEC-108 item 5's separation, asserted). **3.2's measured finding is resolved, not worked
  around:** the fixture asserts the served window identical field-for-field before every preparation turn, that a
  prepared successor does not become the served window until it is adopted, and that the adoption swaps it atomically;
  reverting the routing reproduces the old symptom.
  *Verification:* this suite **50/50**, `test_midi_service_drain` 38/38, `test_playback_prewarm` **190/190** (unchanged;
  cited, not restated), full native **2681 / 2674 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial`
  **SUCCESS** on a **clean rebuild** (every TU recompiled) with RAM1 `variables 96,192` **unchanged** / `code 392,200 +
  padding 1,016` / locals 34,880 (floor 840) and **no re-base**; two mutations red-proved with byte-identical restores
  (sha256 verified).
  *Named, not claimed:* the adoption edge is **not wired into production** (3.7), readiness is still **3.5**'s
  predicate, nothing a consumer reads changed, and no device evidence exists. Detail:
  [`evidence/2026-10-02-s34-window-identity/EVIDENCE.md`](evidence/2026-10-02-s34-window-identity/EVIDENCE.md) and
  [`G2-EXECUTION-PACKET.md`](G2-EXECUTION-PACKET.md) § Slice 3.4.
- [x] 3.5 Make readiness the completed-publication predicate with a named holder — verify the empty-interval fixture and
  the AC-3 fixture pass, and that **RWO's two demoted-target fixtures are re-pointed** rather than left depending on
  empty ⇒ not-ready
  *Landed 2026-10-02.* `Track::playbackWindowReadinessForSlot` answers AC-3's question from the **published** window:
  `Ready` when a complete window is published at the loop's current `playbackRevision` — **with no event-count term**, so
  a legitimately empty interval is ready — and otherwise a value that **names the holder**: `OwesBuild` (a current
  published root exists, so the window-build owner owes one), `OwesSource` (no current root: the source owes it),
  `OwesCurrency` (published but lagging: a rebuild is owed), `NotServable`, `NoRuntime`; `playbackWindowReadinessHolder`
  renders the name in an if/else chain (no table, no `switch`). The generation stays an **ownership guard**, not a
  currency: a window carrying the current revision but a different generation is `Ready`. **RWO's two demoted-target
  fixtures are re-pointed** to ask this predicate and name the holder (`published_source` in the S4b.6 helper;
  `OwesBuild`/`OwesSource` per shape in the S4b.7 helper) instead of reading "the merged window is empty" — while their
  *commit* expectations stay on the **legacy** term, because **nothing in production requests a window yet**: red-proof
  (b) measures that flipping the commit's hold term now leaves the root-backed demoted switch pending forever, so the
  requester and the flip are 3.7's.
  *Verification:* fixture suite **51/51**, `test_midi_service_drain` 38/38, `test_playback_prewarm` **190/190**
  (unchanged; cited), full native **2682 / 2675 succeeded / 7 skipped / 0 failed** (baseline +1), `teensy41-capture-serial`
  **SUCCESS** on a **clean rebuild** with RAM1 `variables 96,192` **unchanged** / `code 392,200 + padding 1,016` /
  locals 34,880 (floor 840) and **no re-base**. Two mutations red-proved with byte-identical restores (sha256 verified).
  *RAM1, named and fixed:* the first build failed the guard (`variables 100,288`, +4 KB, `code` byte-identical) because
  this script places `.rodata` in DTCM and the six holder names crossed the `_VectorsRam`/periodictable `ALIGN(1024)`
  edge; marking them `TRACK_COLD_MEM` is rejected by the compiler (`.flashmem` is *code* in this TU), so the fix is the
  script's existing carve-out — `*TrackPlaybackWindowBuild.cpp.o(.rodata.str* .rodata.*.str*)` in `.text.progmem`. The
  RAM1 rule file carries the instance.
  *Named, not claimed:* F-2/task 2.2's deadline (readiness per affected tick, before the playhead returns) stays open;
  the **one-pass-late** allowance is 3.6's, so 3.5's currency is exact; `isPlaybackMergedMidiEventsReadyForSlot` remains
  the switch commit's term until 3.7; no retention, index or adoption wiring; no device evidence. Detail:
  [`evidence/2026-10-02-s35-readiness-holder/EVIDENCE.md`](evidence/2026-10-02-s35-readiness-holder/EVIDENCE.md).
- [x] 3.6 Implement **pinned-source retention**, the **interval index over the selected `LoopSnapshot`** and the lag rule — verify F-3
  passes, that later intervals are constructible from the pinned revision through the index over **pinned bodies** with no
  resident whole-loop root, that index updates cover both affected ranges and publish atomically with the source revision
  (§14), and that the retention and index cost is charged
  *Increment 3.6a landed 2026-10-02 — the task stays **open**; the index and the lag rule are the next increment.*
  `RetainedPlaybackSource` (identity `(revision, generation)` + descriptor **pins** + **owned bodies**) now lives in
  explicit **serving / candidate / retiring** roles on the per-slot runtime, admitted through
  `Track::retainPlaybackSourceForSlot` with typed outcomes (`Admitted`, `AlreadyRetained`, `NoRuntime`, `NotServable`,
  `RoleCapacityReached` — the bound refuses by name rather than discarding a still-needed source, item 13(a)).
  `tryReleaseRetainedPlaybackSource` is the **one release predicate** (the revision advanced past it; no window serves
  it; no builder holds a frozen source; no closure obligation; and the content is gone) and retention is a **named term
  of `Track::noLiveOwnerForSlot`**. The **reclamation consumer** now reads the retained pins
  (`mergeRetainedPlaybackPinsInto` merged into the set `TrackManager::reclaimUnreferencedDisabledPasses` passes to the
  loop), and the **direct reset/replacement path** is protected by **body ownership** rather than by a veto:
  `LoopEventStore::retainChunkRefs` means all eleven `Loop::resetPassTimeline` call sites stay ungated — the gesture
  works and the bodies survive it. Fixture:
  `test_retained_source_is_admitted_with_roles_and_survives_both_destruction_paths`. **Verification:** focused
  **52/52**, full native **2683 / 2676 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a
  clean rebuild with `variables 96,192` **unchanged** / `code 391,864 + padding 1,352` (512 B headroom, **336 B more
  than the 3.5 baseline**) and **no re-base**; three mutations red-proved with byte-identical restores. The S3.0 pin is
  re-based **per ABI** (6,368 → 6,464 target / 6,544 host) because the holder carries pointer-sized pin lists.
  **OWED in this task, named rather than implied:** the **interval index over pinned bodies** (later intervals
  constructible from the retained revision with no resident whole-loop root; both affected ranges; atomic publication
  with the source revision; inspected-work budgeting) and the **lag rule** (F-3: one pass late and only that, the named
  second-miss breach, the allowance not reset by a further edit, the consumer never blocked). Detail:
  [`evidence/2026-10-02-s36a-retained-source/EVIDENCE.md`](evidence/2026-10-02-s36a-retained-source/EVIDENCE.md).
  *Increment 3.6b landed 2026-10-02 — the **lag rule** (F-3/AC-5); the task **still stays open** for the interval
  index.* `PlaybackLagDecision { Current, PreviousOnePass, BreachSecondMiss }` is a **total** resolution — no waiting
  outcome, so a consumer is never blocked by a missed deadline — and `PlaybackLagState` keeps the per-interval
  accounting (the traversed interval, the served and desired revisions, the consecutive misses, the
  once-per-episode breach). At a traversal boundary a miss leaves the **previous** revision authoritative for that
  traversal with the served window **byte-for-byte unchanged**, the **second** consecutive miss is the **named**
  breach reported **once per episode** (`lag_breach` through the existing row emitter), a **further edit does not
  reset the allowance** (AC-5), and either a published replacement or a changed interval ends the episode cleanly.
  *Verification:* focused **53/53**, full native **2684 / 2677 succeeded / 7 skipped / 0 failed**,
  `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** /
  `code 391,928 + padding 1,288` (448 B headroom) and **no re-base**; three mutations red-proved with byte-identical
  restores (the AC-5 allowance reset; a published replacement ignored; the once-per-episode guard dropped). The S3.0
  pin moved per ABI again (target 6,464 → **6,488**; the host stayed 6,544 because the state fitted existing padding).
  **OWED for this task:** the **interval index over pinned bodies**. Detail:
  [`evidence/2026-10-02-s36b-lag-rule/EVIDENCE.md`](evidence/2026-10-02-s36b-lag-rule/EVIDENCE.md).
  *Increment 3.6c landed 2026-10-02 — the **interval index over pinned bodies**; **task 3.6 is checked**.*
  `RetainedIntervalIndex` is one **bit per bar** ("any pinned body carries content here"): a **descriptor**, never an
  event copy (`TickIndex`'s whole-history storage is not inherited; `CommittedEventRange` stays the body-reading
  primitive). Its lookup is **interval-directed** — a query reports how many bars of the interval the pinned bodies
  cover and how many are **skipped without touching a chunk** — and it folds the **edit destinations** in (`EditPass`
  carries no chunk bodies, but its `[startTick, endTick]` span is where the effective content will be, so a bar whose
  only content is a moved note is not skipped). It publishes **atomically with the source revision** (a candidate
  built and swapped by one assignment, revision stamped inside it), its **update strategy is a full rebuild per
  admitted revision** (so both the old and the new affected ranges are covered by construction and no reader holds a
  mutated index), its **resident bound** is `kRetainedIndexMaxBars = 512` bars (16 words) with a **named refusal**
  beyond it — because an index that answered part of a loop would present the complete scan as window-bounded — and its
  **lifetime** is the revision it describes (a query for another revision is `RevisionMismatch`, never a stale answer).
  The fixture also builds an interval from the **pinned chunk lists alone**, so "no resident whole-loop root" is
  demonstrated rather than asserted. *Verification:* focused **54/54**, full native **2685 / 2678 succeeded / 7 skipped
  / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** /
  `code 391,928 + padding 1,288` (448 B headroom) and **no re-base**; three mutations red-proved with byte-identical
  restores (edit destinations not folded; the revision guard dropped; every bar reported covered). The S3.0 pin moved
  per ABI again (target 6,488 → **6,512**: +96 B sources, +24 B lag, +24 B index; host unchanged at 6,544).
  *Limits recorded, not smoothed over:* the **consumer path is 3.7's** — nothing in production requests a window yet,
  so the fixture drives the index and the body enumeration directly rather than through a production caller — and the
  **envelope roll-up** of these per-slot figures into RWO's budget artifact belongs to that change's own terms.
  Detail: [`evidence/2026-10-02-s36c-interval-index/EVIDENCE.md`](evidence/2026-10-02-s36c-interval-index/EVIDENCE.md).
- [ ] 3.7 Remove the live mutable merged-window build — **the last step of the ladder, stage (4)**, and only after **every**
  dependency is implemented and verified: committed interval publication (3.2), readiness and retention (3.5/3.6), the
  bounded legacy compatibility path (3.12), preview publication (3.13), shared-view ownership (3.11), fault recovery
  (4.5) and undo/redo audible handoff (3.14). Keep a cold reference path for parity testing.
  Verify F-7 still passes on the cold path, that no demoted slot
  reaches the removed path (`playMidiEventsForSlot`'s fallback), and that `ExecutionBounds.md` row 2 is re-classed only
  with evidence
  *Historical status at 2026-10-02 (not a current prerequisite claim; later increments implement parts of these dependencies):* it requires **3.11**
  (shared-view ownership and the cancellation transition table), **3.12** (the bounded legacy compatibility path),
  **3.13** (preview publication), **3.14** (undo/redo audible handoff) and **4.5** (fault recovery) to be implemented
  and verified first, and **none of them exists yet**. The ladder's stage (4) is therefore not reachable, and the next
  slices are those dependencies — **3.11 first**, since the shared-view transition table is what the removal's
  correctness rests on. Recorded here so the gate is visible where the task is read, not only in a session log.
- [x] 3.8 Confirm reuse before invention — verify a review note names the existing resumable entry points used (the
  declared build contract, bounded body-format write/parse, resume arming, the single persistence drain budget) and that
  no second publication scheduler exists
  *Checked off 2026-10-02 as [`REUSE-BEFORE-INVENTION.md`](REUSE-BEFORE-INVENTION.md):* the note names, by path and
  symbol, the four entry points the contract names — the declared build contract (`CoverageServiceContract` +
  `Track::playbackViewCoverageContract`, the same struct the lane declares); the body format's bounded write/parse
  (`StorageIo`'s `write`/`read`/`peek` and the bounded batch accessor `getLastPersistedCapturePassWriteMaxBatchEvents`);
  the **resume-arming** cursor pattern (`PersistedLoopSnapshotReleaseCursor::begin`/`step(maximumUnits)`/`complete`); and
  the single persistence drain budget (`SyncDrainBudget` + `buildSyncDrainBudget(input, sliceBudgetUs)`), which this
  change neither consumes nor shares. Its two **INTERPRETATION** rows are marked as mappings a reader can dispute
  without the citation being wrong. **The audit the task asks for is recorded too:** one deferred-service enumeration
  (`DeferredIdleOwner`, `include/Utils/MidiServiceDrain.h:13`) — this change added a *value* (`PlaybackViewMaintenance`,
  task 3.1, on the `SlotProvision` precedent), the dispatch rides the existing frame (`src/main.cpp:463`), eligibility
  is state-driven (`playbackViewMaintenanceNeeded`), and the lane has no queue, timer, thread or request record of its
  own — i.e. **no second publication scheduler** exists. *No code changed*, so the verification is the last
  full-suite/build state (focused 54/54, full native 2685 / 2678 succeeded / 7 skipped / 0 failed,
  `teensy41-capture-serial` SUCCESS) and no red-proof applies to a note; the behavioural half — fair service among
  holders, no starvation — stays **task 3.10's**.
- [ ] 3.9 Deliver readiness receipts by the two delivery rules — verify a receipt produced under transport reaches the
  evidence stream and that diagnostic cadence cannot evict it
  *Increment 3.9 (increment 1) landed 2026-10-02 — **the readiness receipt and its two delivery rules**, and the half of that pair that had no fixture at all:
  `DebugSessionCapture::readinessReceiptImmediate(track, slot, readiness, holder, revision, leadTicks)` is implemented exactly as the precedent is
  (`serialWriteRoom(96)` → direct `Serial.printf`; otherwise `emitCapPrintf` → the ring), with the `#ifndef`-guarded `SC_WINDOW_READINESS` macro in both
  regions so a native suite can substitute a recorder; row `#CAP,<micros>,DIAG,win_readiness,<track>,<slot>,<readiness>,<holder>,<revision>,<leadTicks>`.
  **The direct path is the point** — a window is prepared *under transport* and the ring is retained by design while a transport runs, so an enqueued
  receipt arrives after the transport stops, i.e. after the readiness it explains (the precedent's own measurement). Fixture in `test_capture_ring`: with
  room the row reaches the stream and nothing is enqueued; with no room it is **delayed rather than lost** (the ring holds it, a drain delivers it) —
  **the second half closes a gap the precedent's own fixture never covered.** Verified: 16/16 (+1 fixture) + 69/69 + 39/39 + 190/190 (cited pin), full
  native 2703 / 2696 succeeded / 7 skipped / 0 failed, build SUCCESS with the RAM1 line unchanged; two mutations (the fallback dropped, the direct path
  dropped) red-proved with byte-identical restores. **The task stays open**: nothing in the lane **emits** it yet — the emitting call site (the owner
  turn's terminal outcomes and the refusal path, silent by row today) and the cadence bound (`TelemetryCadence::shouldLogOccurrence` with a `DMAMEM`
  counter; no `src/Track/*` consumer exists and the per-grain rows are ungated) are the next increment, so the task's own wording is **half met**. Detail:
  [`evidence/2026-10-02-s39-readiness-receipt-delivery/EVIDENCE.md`](evidence/2026-10-02-s39-readiness-receipt-delivery/EVIDENCE.md).*
  *Increment 3.9 (increment 2) landed 2026-10-02 — the **emission site and its cadence**, which increment 1 could not reach: `playbackWindowReadinessLabel` +
  `reportPlaybackWindowReadinessReceipt` (cadence **first**, then readiness + holder + the revision it judged + the lead from the served window), called on the
  owner turn's terminal outcomes only, with `TelemetryCadence.h`'s first `src/Track/*` consumer and a `DMAMEM` counter. **The RAM1 guard caught a second +4096
  and the precedent's fix covered it**: the cause was the compiler's own **jump tables** (`.rodata.CSWTCH.*` — the if/else form does not stop a dense enum chain
  being tabulated), so this TU's read-only routing was widened to `.rodata.CSWTCH*`, restoring the line **exactly** (`variables 96,192`, ITCM `code`
  byte-identical, **no re-base**). Fixture: the receipt's content (slot, readiness `ready`, holder `ready`, revision) through the `#ifndef`-guarded
  `SC_WINDOW_READINESS` recorder, plus the **cadence rate**. Verified: 70/70 (+1 fixture) + 16/16 + 39/39 + 190/190 (cited pin), full native 2704 / 2697 succeeded
  / 7 skipped / 0 failed, build SUCCESS; **M82–M85** fail the fixture, so the cadence is load-bearing. **The loose end this increment recorded was closed
  the same day, by measurement:** the cadence counter is a **process-wide TU static**, so any 16 consecutive outcomes contain exactly one multiple of the
  interval — the fixture now primes the counter (deliberately **unasserted**) and pins **exactly one row per declared interval** in two phase-independent
  blocks, which also pins **one reporter call per completed build** (M85 measured 2 calls/round, not the 7 predicted). The priming bound the first pass used
  was **removed because it masked the rate assertion**. Still open: the **refusal paths remain silent by row**, "one pass late but served" has no row, the
  interval (16) is a declared choice, no hardware observation, and fixture isolation was **repaired, not designed**. Detail:
  [`evidence/2026-10-02-s39b-receipt-emission-site/EVIDENCE.md`](evidence/2026-10-02-s39b-receipt-emission-site/EVIDENCE.md).*

  *Increment 3.9 (increment 3) landed 2026-10-02 — **the receipt's row family is Tier-A, because the transport regime sheds what is not**, and it
  **corrects increments 1–2**: their "delayed rather than lost" was true of the delivery rules in isolation and false about the device under
  transport, since the transport flush budget is 8 (`src/main.cpp`, `timingCriticalTrackActive ? 8 : 64`) and at that budget the ring drops
  non-Tier-A head records **silently, by design**. `DIAG,playback_build,` was already Tier-A; `DIAG,win_readiness` was not in `CaptureLineTier`
  at all, so the "fallback" was sheddable without report — which makes the direct path the only **guaranteed** delivery. Fix: the family is
  Tier-A (bounded by `strlen` of its literal), with the shed path's **47-byte prefix limit** recorded at the receipt's doc comment. Fixtures: a
  new `test_capture_line_tier` case (classification + exact boundary) and `test_capture_ring` half (C) driving the transport regime end to
  end (retained at budget 8, delivered by the idle drain, the drained row classified Tier-A). **M86–M89** red-prove it with byte-identical
  restores; verification **19/19 + 16/16**, full native **2705 / 2698 / 7 / 0**, build SUCCESS from a **true rebuild** with the RAM1 line
  byte-identical and **no re-base**. **Task 3.9 stays open** on the "one pass late but served" row, the declared interval, and hardware —
  and its noted **refusal rows** are now established to be the **caller's** rows, not 3.9's (the spec's "refused by name" requirement is the
  content-admission one landed in 3.12c; the request refusals are typed enum results on `Track`'s contract, returned to whoever requests).
  Detail: [`evidence/2026-10-02-s39c-timing-critical-delivery/EVIDENCE.md`](evidence/2026-10-02-s39c-timing-critical-delivery/EVIDENCE.md).*

  *Increment 3.9 (increment 4) landed 2026-10-02 — **the one-pass-late traversal names itself, through a seam that makes the lane's rows
  observable**. The gap was the AC-5 scenario "Further edits do not extend the allowance"'s last clause, **"the stale condition continues to
  be named"**: the **breach** was row-visible while the **allowance** (`PreviousOnePass`, the state the spec protects) was silent. Found on the
  way and closed with it: the lane's path rows were **observable nowhere** — `reportPlaybackViewMaintenanceRow` called `playbackRebuild` inside
  `#if defined(SESSION_CAPTURE)` with no seam, so 3.6b's "the breach is reported once per episode" could only be asserted as a **counter**,
  never as **the row**. Change: `DebugSessionCapture::playbackBuildRow(...)` + the `#ifndef`-guarded **`SC_PLAYBACK_BUILD_ROW`** macro (the
  `SC_ACCEPTED_LATCH` / `SC_WINDOW_READINESS` pattern) reached from `reportPlaybackViewMaintenanceRow`, and **`one_pass_late`** emitted on a
  `PreviousOnePass` traversal. Bound: one row per traversal — the rate the decision is made at — so no counter and no new state; the volume rests
  on the caller's traversal discipline, which no production caller exercises yet. Fixture: 3.6b's is **extended** (allowance names itself once and
  not as a breach; the breach row is emitted — 3.6b's claim as the row; a third stale traversal re-emits nothing; the eligible boundary adds
  nothing; the episode totals exactly two rows, measured). **M90–M94** red-prove it, including **M94** (the pre-seam shape) which shows the seam
  is what makes the rows observable. Verified: 70/70, full native **2705 / 2698 / 7 / 0**, build SUCCESS from a **true rebuild** (79.9 s) with the
  RAM1 line **byte-identical** and **no re-base**. **Task 3.9 stays open** on the declared interval (16) and hardware only — the "one pass late
  but served" item is **closed by this increment**. Detail:
  [`evidence/2026-10-02-s39d-one-pass-late-naming/EVIDENCE.md`](evidence/2026-10-02-s39d-one-pass-late-naming/EVIDENCE.md).*

  *Increment 3.10 (increment 6) landed 2026-10-02 — **the starvation half, the composed progress bound, and a measured contradiction in the declared
  envelope** (test-only: no production line changed). The task's own verification sentence is what this supplies: the **production selector** composed with
  the lane's own turn while the lane owes two windows, asserting that **every one of the seven owners — the lane included — is served exactly once per
  rotation** (no starvation, no extra share), that the lane progresses to completion with one completion per owed window, and pinning the measured turn
  count. **Not re-claimed:** the exact rotation was already proved including the lane (RWO's `…backlog_snapshot_two_of_eight`), one grain per call by 3.1's
  `expectedTurns == 5`, and a blocked holder by increment 1; the lane is never **denied** (`deferredIdleOwnerDependsOnVisualCacheWrap` covers only
  `VisualCache`/`LoadAndDisplay`), and the caller's single-dispatch shape is build-verified. **The finding, measured and numeric:** for one 64-bar loop and
  two owed windows carrying 81 notes, the lane needed **58 grains and 60 lane turns** (2 completions), while the contract it is admitted under declares
  `3 + 3 + 1 = 7` grains at quanta 64/48 with a 3-denied-turn allowance → **28 turns** worst case. So **the declaration does not bound this work shape**, and
  two of its own numbers disagree by ~an order of magnitude: `3` order grains × `48` = **144 ordered entries** against a window declared to hold
  `kEventsPerView = 2048`. In the contract's currency: admission judges the lead with the declared grains (29 bars / 22 272 ticks, and 4.4's derived 30-bar
  floor), while a 60-lane-turn shape needs `60 × 768 ≈ 60 bars` — so such a shape can be **admitted and still miss its handoff**. The one-pass-late rule is
  the designed answer to the miss; the *declared* envelope is unsupported for the shape, and **deriving it is this task's remaining item** — assertion (C)
  deliberately pins the violation so the derivation inverts it and forces this record to be updated. **M95–M97** red-prove the fixture (an extra lane turn
  per rotation starves an owner to 1 turn; a doubled fill quantum moves the pinned figure; a skipped turn fails 18 cases); verification 71/71, full native
  **2706 / 2699 / 7 / 0**, build SUCCESS (relink; RAM1 line byte-identical, no re-base). **Task 3.10 stays open** on the envelope derivation (now a concrete
  measured problem), the wiring, and the systematic grain-cost sweep. Detail:
  [`evidence/2026-10-02-s310f-composed-service-bound/EVIDENCE.md`](evidence/2026-10-02-s310f-composed-service-bound/EVIDENCE.md).*

  *Increment 3.10 (increment 7) landed 2026-10-02 — **the measured grain-cost model and the supported window derived from it** (test-only). This is the
  increment the previous two pointed at: **derive the envelope from grain costs without inventing a microsecond budget**. A sweep reads the
  counter-observable cost in **turns**, with `nextCoverageBuildGrain()` sampled before each turn, so the fill/order split is the owner's own answer. Measured:
  window events 1 and 10 → **1 turn, no order grain**; 20 → 11 orders (12 turns); 40 → 25 (27); 80 → **57 (60 turns)**; 160 → 127 (132). Controls in the
  same run: **80 notes cost identically on a 32-bar and a 64-bar loop**, and the same 80 notes **spread across the loop** cost **one** turn — so the cost
  is a function of the **window's event count alone**, it has a threshold near 10 events, and above it the order pass is **super-linear** (per-note cost
  0.55 → 0.79 grains). **The derived bound**, in the contract's own currency: a turn advances the playhead at most `maximumPhaseAdvancePerTurn`, so a
  window's need is `turns × 768` ticks of playhead travel — 80 events = **46 080 ticks ≈ 60 bars** (fits a 64-bar loop, not a 32-bar one), 160 events =
  101 376 (fits neither). **The contradictions, now numeric:** the declared `3 + 3 + 1 = 7` grains at quanta 64/48 (worst case 28 turns) cover roughly
  `N ≈ 5–10`, not a window of 80 — and an 80-event window on a **32-bar** loop was nevertheless **admitted** while needing 46 080 ticks ≫ the declared
  29-bar lead; `kWindowAdmissionLimit = kEventsPerView = 2048` extrapolates to ~1 600 lane turns ≈ 1 600 bars, an order of magnitude beyond any admissible
  loop. **Preferred reconciliation, stated and deliberately NOT applied:** keep the grain quanta (a deliberate timing choice with a measurement behind it)
  and derive both the exported grain counts and 3.12's admitted window size from the measured envelope and the loop's own length — confidence **high**
  that the capacity is the wrong side to keep, **medium** on the exact functional form (four points establish the shape, not the constant). It is not
  applied because it moves the declared lead, 3.12's admission limit and 4.4's floor and would refuse shapes admitted today; assertion (D) is written as
  the *contradiction*, so applying it inverts it. **M98–M100** red-prove the fixture (the order quantum doubled halves the pinned table; always entering
  the order phase breaks the threshold and the sparse-window assertions; inflating the declared `maximumPhaseAdvancePerTurn` breaks the derivation's
  equality pin — i.e. (D) reads the declared field, not a literal). Verified: 72/72, full native **2707 / 2700 / 7 / 0**, build SUCCESS (relink, RAM1 line
  byte-identical, no re-base). **Task 3.10 stays open** on the **reconciliation decision** (above), the wiring, and the systematic sweep. Detail:
  [`evidence/2026-10-02-s310g-cost-model/EVIDENCE.md`](evidence/2026-10-02-s310g-cost-model/EVIDENCE.md).*

- [ ] 3.10 **Implement fair, bounded, deadline-ordered service** (*DEC-108 §13b*) — a recurring bounded opportunity on the
  existing deferred machinery; round-robin across eligible holders; earliest outstanding playback deadline first within a
  holder; **cursor advances even when a holder cannot progress**; capture finalization and persistence keep their own
  opportunities (no starvation); one declared grain per admitted turn, no hidden stacking. Verify with a fixture that
  keeps an early holder blocked while later holders need preparation and capture/persistence work is pending, asserting a
  progress bound for **every admitted holder** (including inbound switch preparation and overlapping finalization), and
  derive the supported envelope from grain costs and the worst permitted service gap — invent no microsecond budget
  *Increment 3.10 landed 2026-10-02 — the **holder level** (the gap below the already-proved owner rotation): `include/Utils/MidiServiceDrain.h`
  gains the pure `selectPlaybackHolder(cursor, holders, count, out)` with `PlaybackHolder` (eligible / can progress /
  outstanding deadline) and the named `PlaybackHolderSelection { None, Service, AllBlocked }`. The deadline is **not new
  state**: it is the phase at which the playhead next enters the interval the build serves — the figure
  `Track::requestPlaybackWindow` already passes as `handoffPhase`. Proved as five bounds: each able holder served exactly
  once per rotation and the rotation continues from the cursor; earliest outstanding deadline first; a deadline outranks
  none while deadline-less holders still rotate evenly; a **blocked holder is never chosen** and is served within one
  rotation of becoming able; and "nothing owed" is distinct from "all blocked", with the rotation advancing in both
  (never a latch). Earliest-deadline-first is applied **globally** over the eligible set — the tighter reading, from which
  the per-holder one follows. Verified: 39/39 (+1 fixture) + 67/67 + 52/52 + 190/190 (cited pin), full native 2700 / 2693
  succeeded / 7 skipped / 0 failed, build SUCCESS with the RAM1 line unchanged (the policy is inline and unconsumed);
  four mutations red-proved with byte-identical restores. **The task stays open** on four named items: the **wiring** (no
  production caller yet: the holder scan and the `main.cpp` track scan stay ascending first-match, and nothing carries the
  handoff phase per holder); the **starvation half** (capture finalization/persistence are structural only today); the
  **envelope derivation** from grain costs and the worst permitted service gap (bounds in turns here, gap measurement
  owed); and **one declared grain per turn** as a uniform invariant, which is not there to assert yet. Detail:
  [`evidence/2026-10-02-s310-holder-service-fairness/EVIDENCE.md`](evidence/2026-10-02-s310-holder-service-fairness/EVIDENCE.md).*
  *Increment 3.10 (increment 2) landed 2026-10-02 — the **urgency figure, named once and shared**: `handoffLeadTicks` (the tick distance along the loop to a handoff)
  lands beside the contract whose `available` sub-term it is, and `canCompleteBeforeHandoff` now **calls** it, so the admission and the scheduler
  cannot diverge. The holder policy therefore orders by **ticks to the handoff**, not by a raw phase — a phase is meaningless across holders whose
  loops differ in length: the fixture pins the pair where the **more urgent holder carries the larger phase** (1,400 ticks on a 2,000-tick loop
  versus a whole 18,200-tick pass on a 20,000-tick loop), plus the wrap case and the refusals (an undeclared loop, an out-of-range phase — `0`, never a
  wrap). Verified: 39/39 (fixture extended) + 67/67 (the admission side of the shared definition) and full native 2700 / 2693 succeeded / 7 skipped /
  0 failed, build SUCCESS with the RAM1 line unchanged; three mutations red-proved with byte-identical restores — the wrap branch dropped (fails the
  new fixture **and 11 cases in the sibling suite**, the point of one definition), the out-of-range refusal dropped, and "a deadline beats none"
  inverted. **Recorded:** two of this task's own fixture attempts were wrong, not the code (increment 1's outcome block, and an assertion that
  claimed equal phase distances differ with loop length — they do not; the difference is the wrap). **The task stays open** on the same four items:
  the wiring (still no production caller; nothing computes the lead for a live holder), the starvation half, the envelope derivation, and one
  declared grain per turn as a uniform invariant. Detail:
  [`evidence/2026-10-02-s310b-shared-handoff-lead/EVIDENCE.md`](evidence/2026-10-02-s310b-shared-handoff-lead/EVIDENCE.md).*
  *Increment 3.10 (increment 3) landed 2026-10-02 — the **wiring**: `Track::stepPlaybackViewMaintenance(currentPhase = 0, cursor = nullptr)` takes the
  playhead's phase and the caller's rotation cursor, builds the holder set from live state (each owing slot's urgency from its **in-flight build's
  interval**, which `RuntimePlaybackViewFill::begin` already sets — no new field, no pin move) and lets `selectPlaybackHolder` choose; `src/main.cpp`'s single
  production call site passes the phase and one cursor. **The RAM1 guard rejected it twice** — an 8-cursor array and then a single DTCM byte each cost +32 B
  of `variables` against the approved 96,192 — so the cursor is **`DMAMEM`**, per DEC-108 §13b's own placement rule, and the approved line is untouched,
  **not re-based**. Verified: 67/67 + 39/39, full native 2700 / 2693 succeeded / 7 skipped / 0 failed, build SUCCESS with `variables 96,192` unchanged (48 B
  headroom). **The task's own gate is not yet met and is named: the wiring has no discriminating fixture.** Its new behaviour is observable only with **more
  than one owing holder on one track** (every existing fixture has exactly one), so those fixtures cannot tell the routed scan from the old ascending one —
  the RWO-recorded trap. **OWED, specified:** two slots on one track, both owing, the urgent one at the higher index and one grain from publication,
  asserting the single turn publishes the **urgent** slot rather than slot 0, plus a persistent cursor interleaving them. Until then the rotation and
  deadline order are **proved as a policy** (increments 1–2) and **wired but not proved end-to-end**. **Also not claimed:** nothing in production owes a
  window today, so no reachable behaviour changed. The other open items are unchanged (starvation half; envelope derivation; one declared grain per turn).
  Detail: [`evidence/2026-10-02-s310c-routed-scan/EVIDENCE.md`](evidence/2026-10-02-s310c-routed-scan/EVIDENCE.md).*
  *Increment 3.10 (increment 4) landed 2026-10-02 — **the owed fixture, closing increment 3's own gate**: `test_window_owner_serves_the_most_urgent_holder_and_rotates_between_them`
  (no production change). The observable is a holder's **own next grain** — "progressed" past `Fill`, since a small window finishes outright (`Complete`, which my first
  version wrongly expected as `Order`). Three cases, each on its **own slot pair** (a slot that already owes answers `BuildInFlight`, and that check is *per slot*, so two
  slots on one track can owe at once): the far/urgent pair proves urgency beats index order, the swapped pair is the control that it is not "the higher index always
  wins", and the equal-urgency pair proves one persistent cursor rotates between them. Verified: 68/68 (+1 fixture) + 39/39 + 52/52 + 190/190 (cited pin), full native
  2701 / 2694 succeeded / 7 skipped / 0 failed, build SUCCESS with the RAM1 line unchanged; **two mutations red-proved with byte-identical restores — M76 (the scan
  reverted to the pre-3.10 ascending order) fails case A, so the fixture does distinguish the routed scan from the old one** and the RWO-recorded "a dropped build left
  every fixture green" trap is closed for this wiring; M77 (urgency ignored) also fails case A. **The task stays open** on the starvation half, the envelope derivation
  and one declared grain per turn — plus one **new question this fixture raises and does not answer**: it exercises holders within a track, while the same starvation
  argument applies to the `main.cpp` loop that stops at the first *track* with work. Detail:
  [`evidence/2026-10-02-s310d-routed-scan-fixture/EVIDENCE.md`](evidence/2026-10-02-s310d-routed-scan-fixture/EVIDENCE.md).*
  *Increment 3.10 (increment 5) landed 2026-10-02 — the **cross-track question increment 4 raised is measured**: `Track::playbackWindowServiceOffer(currentPhase)` reads a
  track's **most urgent owing holder without serving**, and `src/main.cpp` builds every track's offer and lets `selectPlaybackHolder` choose **one track** per turn, so the
  lowest-index track with work no longer monopolises it (the loop it replaced stopped at the first track with work). A track almost always owes one holder, which is why the
  *track* level is where the starvation argument bites and the tick lead is what makes tracks comparable. Fixture (`…across_tracks_by_urgency_not_by_index`, two real tracks):
  each offer answers read-only and both still owe; the policy picks the **urgent track though it is the higher index**; equal urgency rotates; the choice is executable; and a
  track with two owing holders offers its most urgent. **The last block exists because a red-proof found the gap** — M79 (offer returns the first owing holder) passed before it
  and fails after. Verified: 69/69 (+1 fixture) + 39/39 + 52/52 + 190/190 (cited pin), full native 2702 / 2695 succeeded / 7 skipped / 0 failed, build SUCCESS with
  `variables 96,192` unchanged (48 B headroom); M78/M79 red-proved with byte-identical restores. **Not claimed:** the caller's own shape is **build-verified, not
  fixture-proved** (`src/main.cpp` is not native-linkable — the boundary carried since 3.1), nothing requests windows in production yet, and the **starvation half**, the
  **envelope derivation** and **one declared grain per turn** stay open. Detail:
  [`evidence/2026-10-02-s310e-cross-track-offer/EVIDENCE.md`](evidence/2026-10-02-s310e-cross-track-offer/EVIDENCE.md).*
- [x] 3.11 **Implement the shared-view ownership rules and transitions** (*DEC-108 §13d*) — published/current views
  read-only, only the owning builder writes its candidate, a ready view immutable until adopted or retired, storage
  reusable only after every owner releases it; then the transitions: new edit (retain the frozen source, coalesce the
  successor), slot switch (inbound coverage before binding; retain outbound until readers and closures finish),
  selected-track change (never retargets an existing worker), record/overdub stop with successor start (predecessor's
  frozen ownership preserved; successor never borrows its buffers), transport stop, clear/load, late completion (rejected
  for publication; releases only what it owns). Verify each production grain can be paused, the transitions injected, and
  ownership, **generation-qualified identity** (returning to a slot revives nothing), no premature reuse, no lost
  requests and eventual cleanup asserted
  *Checked off 2026-10-02 with [`evidence/2026-10-02-s311-shared-view-transitions/EVIDENCE.md`](evidence/2026-10-02-s311-shared-view-transitions/EVIDENCE.md).*
  The ownership rules are stated as **one table** (`PlaybackViewTransition` → `PlaybackViewTransitionEffect` →
  `resolvePlaybackViewTransition`, the pure resolution) covering all seven transitions, plus the operations it names:
  `Track::applyPlaybackViewTransition` (cancels the candidate and/or retires the prepared successor per row, invalidates
  the operation identity per row, and never touches the served window — the invariant asserted for **every** row),
  `Track::playbackViewRoleStateForSlot` (the role state a fixture asserts with), and the **publication guard**: the
  build records its operation generation where it begins, so a completion arriving after that identity moved is refused
  and releases **only its own candidate** (a late completion publishes nothing). Generation-qualified identity is now
  load-bearing: clear/load bumps it, so returning to a slot revives nothing. *Verification:* focused **55/55**, full
  native **2686 / 2679 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with
  RAM1 `variables 96,192` **unchanged** / `code 391,928 + padding 1,288` (448 B headroom) and **no re-base** — the
  S3.0 pin is **unchanged** at 6,512, since the 4-byte field fitted existing padding. Three mutations red-proved with
  byte-identical restores (the publication guard dropped; the identity not invalidated; the table's continuing rows made
  to cancel the candidate). *One attempted mutation did **not** discriminate and is recorded rather than dropped:* the
  explicit prepared-successor **retirement** branch, because `RuntimePlaybackViewBuild::discard()` clears its own
  destination's `complete` and after a successor publication that destination *is* the view the `ready` role holds — so
  the cancel already retires the lookahead for this lane's sequencing. That branch covers a `ready` view the current
  builder is **not** pointing at, which this fixture does not construct: a **named gap**, deferred to 3.7 where the
  successor lane gains a production caller. *Named, not claimed:* the worker-side "only the owning builder writes its
  candidate" rule is asserted through the roles' behaviour rather than by a concurrent writer; no timing/deadline claim
  (F-2 stays open); no envelope claim beyond the per-slot figure; no device evidence.

- [ ] 3.12 **Implement the admission limit on the decided accounting unit, and the bounded legacy compatibility path**
  (*DEC-108 §14*) — enforce the **sliding window-span invariant**: for the configured span `W`, every `W`-wide interval,
  **including one crossing the loop wrap**, fits the limit `L`, and window storage capacity is **exactly `L`** (2,048
  events, no partition multiplier). Count the **capture interval at acceptance** in **effective** events, with the
  note-off and generated-closure reserve **inside `L`**, aggregated across the slot's overdub passes, using
  **preallocated bounded accounting** that includes outstanding reservations and **never invokes the resolver on the MIDI
  edge**; per-bar counts only as a conservative bound over every intersected bar. Serve content that exceeds `L` — and any
  interval that outgrows one page — through **multiple fixed-capacity immutable event pages** consumed by a **resumable
  cursor** that preserves source revision, ordering and **exactly-once** emission across page boundaries, prepared
  **off-clock** from bounded preallocated storage, never delaying events to later ticks, rebuilding inline, restoring
  whole-take reservations or truncating. Over-capacity is a **supported compatibility case, not itself a deadline fault**;
  legacy content is **not exempt from timing**, so genuine deadline failures still follow fault containment.
  **MIDI-content admission stays distinct from interaction acceptance (DEC-078)** — verify the spec's "Accepted content is
  never truncated to fit a window" and "Legacy dense content is served through bounded pages" requirements, fixtures 2.11
  and 5.5, and that no stop/switch/undo request is ever refused by this limit
  *Increment 3.12a landed 2026-10-02 — the **task stays open**; the legacy paged path and the MIDI-edge wiring remain.*
  `WindowAdmissionAccounting` (in `RuntimePlaybackViewConfig`) accounts the capture interval's **effective** events plus
  **outstanding reservations** plus the **closure reserve inside `L`**, with one predicate (`fits`), an `admit` that
  returns a **named** `Outcome` and never half-accepts, and per-bar/span bounds that are **conservative and clamped by
  `L`**. Two invariants are pinned by `static_assert` where they cannot drift: `kWindowAdmissionLimit == kEventsPerView`
  (**`L` is the window's storage capacity** — no partition multiplier) and `kWindowClosureReserve < L` (the reserve is
  *inside* the limit; its value 128 is a **declared provisional** figure, not a measurement). Interaction acceptance is
  structurally outside the limit: the counters hold content only, so a stop/switch/undo request has no representation
  here to be refused by (DEC-078's separation upheld by construction). *Verification:* focused **56/56**, full native
  **2687 / 2680 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with
  `variables 96,192` **unchanged** / `code 391,928 + padding 1,288` (448 B headroom) and **no re-base** (header-only
  type, no per-slot state). Two mutations red-proved with byte-identical restores (the reserve moved outside `L`; the
  per-bar bound not clamped by `L`). **OWED in this task:** (b) the **bounded legacy paged path** — fixed-capacity
  immutable pages with a resumable cursor preserving revision, ordering and exactly-once emission (fixture 5.5) — and
  (c) the **MIDI-edge wiring** of the limit into capture acceptance, without which "capacity pressure is refused before
  acceptance" is a property of the unit rather than of the running system. Detail:
  [`evidence/2026-10-02-s312a-admission-limit/EVIDENCE.md`](evidence/2026-10-02-s312a-admission-limit/EVIDENCE.md).
  *Increment 3.12b landed 2026-10-02 — clause (b), the **bounded legacy paged path**; the **task stays open** for the
  MIDI-edge wiring.* `include/WindowPagedStream.h` serves content beyond `L` (and any interval that outgrows one page)
  through a **ring of three fixed-capacity immutable 128-event pages** with a **resumable cursor** `(sourceRevision,
  nextSequence, offsetInPage)`: `produce(...)` takes one bounded slice per call through the **existing**
  `CommittedEventRange::appendWindowBudgeted` + `WindowTraversalCursor` (reuse, not a second enumerator, per 3.8) and
  `consume(...)` continues exactly where it left off, so ordering and **exactly-once** emission hold across page
  boundaries from positions alone; a drained page returns to the ring, so **memory stays bounded while content is
  not**. There is deliberately **no blocking state** — an unpublished next sequence answers `NeedsPage`, never a wait —
  and a foreign revision is refused by name (`StaleRevision`) on **both** sides, the same generation-qualified rule as
  the shared-view identity. *Verification:* focused **57/57**, full native **2688 / 2681 succeeded / 7 skipped / 0
  failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** / `code
  391,928 + padding 1,288` (448 B headroom) and **no re-base** (the header has no production caller), the S3.0 pin
  untouched. Two mutations red-proved with byte-identical restores; the **first M15 attempt did not discriminate** (a
  300-event fixture filled exactly the ring, so reuse was never required), so the fixture now commits **700** events,
  and that test defect is recorded rather than skipped. **OWED in this task:** (c) the **MIDI-edge wiring** of the limit
  into capture acceptance (no resolver, no allocation on the edge; no stop/switch/undo request ever refused), and
  fixture **5.5**'s timing envelope with the over-capacity same-tick, wrap and cross-page-closure cases — so the ring
  size (3x128) and the closure reserve (128) remain **declared, unmeasured** choices for task 6.3's sweeps. Detail:
  [`evidence/2026-10-02-s312b-paged-path/EVIDENCE.md`](evidence/2026-10-02-s312b-paged-path/EVIDENCE.md).
  *Increment 3.12c landed 2026-10-02 — clause (c), the **MIDI-edge wiring**; the **task stays open** for fixture 5.5's
  timing envelope.* The gate sits in `Track::recordMidiEvents` (the edge `MidiHandler` and `noteOn`/`noteOff` use),
  **before** the canonical append, and its accounting is a **derived value** (`Track::captureIntervalAdmissionForSlot`:
  the serving window's content + this interval's accepted rows + the closures they owe + the reserve) — arithmetic over
  existing state, so **nothing is stored, nothing allocates, nothing is scanned, the resolver is never invoked**, and no
  size pin moves. `WindowAdmissionAccounting::admitsNewEvent` is the one decision (`kUnitsForNoteOn` = 2, because a new
  note-on also owes its closing row; one for every other event), a **closure is never refused** (its row is already
  inside `L`, so it moves a unit rather than adding one — the "closure guaranteed even when further input is refused"
  clause), and a refusal is **named** (`CaptureAppendDenyReason::WindowAdmission` / `"window_admission"`), gives a
  staged reservation back, and leaves no row and nothing owed. **The scope was decided by the first full-suite run, not
  by preference:** `L` is what a **window must hold**, so the gate is consulted exactly where a window serves the slot —
  the first wiring bounded every interval and broke the sibling suite's
  `test_record_normalization_preserves_take_larger_than_overdub_staging`, because a whole-take record take is not served
  by the 2048-event window and is therefore not window-bounded. Interaction acceptance stays outside by construction (the
  counters hold content only) and the fixture drives a stop through the same at-limit state. *Verification:* focused
  **59/59**, the sibling suite **60/60** (the over-reach oracle, green again), full native **2690 / 2683 succeeded / 7
  skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** /
  `code 392,072 + padding 1,144` (304 B headroom) and **no re-base**; five mutations red-proved with byte-identical
  restores. **Not claimed:** with no production window lane (3.7's requester) the gate is inert in today's running
  system, and the plain-event reservation release on refusal is executed but not asserted. **OWED in this task:** fixture
  **5.5**'s timing envelope with the over-capacity same-tick, wrap and cross-page-closure cases, and the paged path's
  production consumer. Detail:
  [`evidence/2026-10-02-s312c-midi-edge-admission/EVIDENCE.md`](evidence/2026-10-02-s312c-midi-edge-admission/EVIDENCE.md).
- [ ] 3.13 **Implement the preview contract — immutable deferred publication** — publish preview through the same
  immutable deferred discipline as committed content, defining **source freezing** (the preview revision a build works
  from), **cancellation** (session mutation, track re-focus, undo, exit between build grains) and **committed-content
  handoff** at session exit. Verify fixture 2.12 passes: no mixed or stale preview publication, preview is never accepted
  as committed currency, and the live mutable fallback's removal is not narrowed on preview's account
  *Increment 3.13 landed 2026-10-02 — the **preview source contract**; the **task stays open** for fixture 2.12's
  integration half and the production callers.* `PlaybackWindowOrigin { Committed, Preview }` is carried in **both**
  `PlaybackViewSourceIdentity` and `PlaybackWindowIdentity`, each **part of equality**, so a preview window for the same
  slot, generation, revision and interval is **not equal** to the committed one it stands in front of — the spec's
  "identified as preview rather than as a committed window" as an answer a consumer can act on. Preview is published
  through the **same deferred lane** as committed content (`Track::requestPreviewWindow`: whole-loop coverage, the
  session's preview revision as the lane's **freeze token** and its currency, `origin == Preview` in the identity), so
  the live mutable fallback's removal is **not narrowed on preview's account**. Cancellation
  (`Track::cancelPreviewWindowForSlot`, reasons `SourceMutated` / `SelectedTrackChanged` / `SessionExited`) answers a
  **named** `PreviewCancelOutcome` and reuses the shared-view `ClearOrLoad` row — candidate released, prepared successor
  retired, operation identity invalidated — so the served window is never torn, a mutated source leaves a served preview
  stale **by its own revision**, and a cancellation can never cancel a committed build (the lane marks its own work).
  The **committed-content handoff** is a boundary rather than a gap: at exit the preview window keeps serving, the
  committed successor is owed, and the obligation is discharged at the **adoption** boundary, with a repeated exit
  answering `AlreadyHandedOff`. *Verification:* focused **61/61**, full native **2692 / 2685 succeeded / 7 skipped / 0
  failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** / `code
  392,328 + padding 888` (48 B above the floor) and the **target size pin re-based with measurement and its reason at
  the assert** (6,512 -> **6,624**: +112 B, 8 B of payload and the rest alignment; the host stayed 6,544). Five mutations
  red-proved with byte-identical restores. **OWED in this task:** fixture **2.12**'s integration half (edit while
  playing on a long loop, cross window boundaries, in-session undo/redo, switch the selected track, mutate/exit between
  build grains, then save — needing a suite that links **both** the window lane and the real edit stack) and the
  production **callers** (the preview requester and the `EditManager` lifecycle hooks). Implement these with
  the serving-path integration in 3.10/3.13 BEFORE 3.7 fallback removal; removal is not their prerequisite. Detail:
  [`evidence/2026-10-02-s313-preview-contract/EVIDENCE.md`](evidence/2026-10-02-s313-preview-contract/EVIDENCE.md).

- [ ] 3.14 **Integrate undo/redo audible handoff** — establish a production-path fixture before changing
  `refreshPlaybackAfterCapturePassStateChange` and its capture/overdub undo/redo callers. Extend the existing
  `TrackUndo` request/refresh path to use the immutable publication owner and task 4.5's off-clock selective
  reconciliation, not `silenceSlotMidiOutput` as blanket cache invalidation. Keep checkpoint/undo semantics
  and ownership unchanged. Verify unchanged sustained notes receive no early NoteOff or retrigger, removed
  notes close at handoff and obsolete closures never fire; pause between real preparation grains for
  repeated undo/redo, track/slot switching and stale completion. Assert exact MIDI output, ledger state,
  retained-source lifetime and eventual release. Reuse task 2.12 for preview-session undo/redo. Red-prove
  the fixture with blanket silence restored. Gate normal windowed undo/redo enablement and task 3.7's
  fallback removal on this task; content parity in task 4.1 alone is insufficient.
  *Increment 3.14 landed 2026-10-02 — the **audible half** (the blanket silence replaced); the **task stays open** for
  the overdub-session identity source and the publication-owner request.* The four **edit-batch** call sites now
  reconcile before they refresh: `Track::reconcileAfterEditPassStateChange(slot, passIds, identitiesAreRemoved)` takes
  the affected edit passes' **own added events** as the identity set (a bounded, allocation-free `NotePresenceSet`) and
  one ledger walk classifies every sounding note as **closed** (its identity was removed), **suppressed** (its identity
  was re-established while the note never stopped sounding, so it is not retriggered), or **preserved** (untouched) —
  which is "unchanged sustained notes receive no early NoteOff or retrigger" as a mechanical claim. **The blanket
  invalidation had a second half that the fixture found:** the same refresh also **cleared the ledger**, so removing the
  silence alone would still have killed soundings; `LoopPlaybackRuntime::reset` now clears the revision/generation
  caches unconditionally (they describe the merged representation) while `preserveLedger` governs **only** the ledger.
  Undo semantics, the revision bump and the stack discipline are untouched, and the reconciliation is off-clock. *The
  task's own red-proof is satisfied:* restoring the blanket silence on either refresh path makes the fixture fail (one
  closure becomes three; none becomes two). *Verification:* focused **64/64**, full native **2695 / 2688 succeeded / 7
  skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** /
  `code 392,328 + padding 888` (48 B above the floor; byte-identical to the three previous slices, no size pin move);
  six mutations red-proved with byte-identical restores, and the first blanket-silence attempt did **not** discriminate
  (it mutated the generic refresh while the fixture drives the edit-batch one), so the fixture now asserts the generic
  path directly as well. **OWED in this task:** (a) the two **overdub-session** sites no longer silence and preserve the
  ledger but do **not** yet close the session's removed soundings — they need a **committed-pass identity source**
  (`PassId` list → the passes' own rows), and until then a note can remain sounding whose content an undo removed
  (strictly better than killing every sounding note, but not the requirement); (b) the refresh is not yet a **request
  through the immutable publication owner** — that needs a bounded content source for the affected interval, and the
  existing request path takes a caller-owned enumerated scratch, so the choice between a cold content-sized allocation
  and the owner's own companion-pass source is a design decision to state here first; (c) the between-grains matrix
  (repeated undo/redo, track/slot switching, stale completion — 3.7's serving path), retained-source lifetime and
  discharge (3.6a's release predicate, cited) and preview-session undo/redo (3.13/2.12, cited). One finding is recorded
  rather than silently changed: `UndoEntryKind::RecordPassAdded`'s undo case does not call the refresh at all. Detail:
  [`evidence/2026-10-02-s314-undo-audible-handoff/EVIDENCE.md`](evidence/2026-10-02-s314-undo-audible-handoff/EVIDENCE.md).
  *Increment 3.14b landed 2026-10-02 — the **committed-pass identity source**; the **task stays open** for the
  publication-owner request (3.14c) and the between-grains matrix.* The two **overdub-session** sites — which the
  previous increment could only stop from silencing — now **settle** as well. `Loop::overdubSessionPassIdAt` and
  `overdubSessionCompanionIdsAt` name the pass a session step just changed and the companion edit batches it changed
  with it (out-of-range answers are **named**, never clamped to a neighbouring step: the undo reads the cursor after
  decrementing, the redo before incrementing, and the live-undo branch never moves it), and
  `Track::reconcileAfterCommittedPassStateChange` takes the identities from that pass's **own bodies** — a row at a
  time through `chunkUsedCount`/`readSealedChunkEvent`, bounded and allocation-free — so a note the undo removed
  **closes** and the session case's **stuck-note risk is gone**. The classification is now **one shared core**
  (`settleLedgerAgainstPresence`: closed / suppressed / preserved over a single ledger walk) fed by two sources — an
  edit batch's added events and a committed pass's bodies — so the two cases cannot diverge in rule, only in input; the
  generic refresh is replaced by `refreshPlaybackAfterCommittedPassStateChange` at those two sites. A disabled pass
  keeps its chunk references ("keep disabled capture passes on the timeline so redo can re-enable them"), which is what
  makes the removal settleable at all. *Verification:* focused **64/64** (the 3.14 fixture **extended**, not added),
  full native **2695 / 2688 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild
  with `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B above the floor; byte-identical to the four
  previous slices) and **no size pin moved** — the `Loop` accessors add no state, which the build's own
  `static_assert(sizeof(Loop) == 984)` proves. Six mutations red-proved with byte-identical restores, and two
  bookkeeping facts are recorded rather than left implicit: the earlier M38/M39 anchors moved into the shared core and
  M36b's target was deleted, so **M42/M43/M44** are their successors on the code as it now stands; and M44's first
  attempt reported green only because its anchor sat in the wrong **file** — an unmatched anchor is not evidence.
  **OWED in this task:** (a) the refresh is still **not a request through the immutable publication owner** — the two
  candidate sources are now both visible in the code (the owner's own `RuntimeCompanionPasses`, which needs no caller
  buffer but whose authority for a slot at undo time is **unverified**, versus the existing request path's
  caller-owned enumerated scratch, correct but a cold content-sized allocation), and choosing between them is a design
  decision to state here first (3.14c); (b) the between-grains matrix (repeated undo/redo, track/slot switching, stale
  completion while preparation grains are paused — 3.10/3.14 serving-path integration); (c) retained-source lifetime and discharge
  (3.6a, cited), displayed provenance, and preview-session undo/redo (3.13/2.12, cited). Detail:
  [`evidence/2026-10-02-s314b-committed-pass-source/EVIDENCE.md`](evidence/2026-10-02-s314b-committed-pass-source/EVIDENCE.md).
  *Increment 3.14c landed 2026-10-02 — the **refresh's source question, verified** (no production code changed); the
  task stays open on the requester's own ordering.* Both no-caller-buffer candidates for an eager refresh request at this
  edge are rejected **on their own documentation**: `RuntimeCompanionPasses` is an *operation-scoped preparation*
  ("caller **freezes** active passes … and keeps their descriptors/events and rows immutable **until done**";
  `ready(source)` requires `phase_ == Complete && valid() && source == expected_`), and `RuntimeCanonicalPassInputs`
  documents *"invalidate before pass/history mutation or release"* — while this edge's act **is** that mutation, so a
  refresh sourced from either would read content the same gesture has just invalidated. The third option (the existing
  request path's enumerated scratch) is **measurably gated**: with a served window and a real `TrackUndo::undoForLoop`,
  readiness stops being `Ready` (a rebuild is owed, **named** in the owner's vocabulary) and `requestPlaybackWindow`
  answers **`SourceNotCurrent`** — the lagging root is named, never served as current, which is DEC-073's protection and
  exactly **4.1**'s lagging-checkpoint outcome. Source currency is produced by the existing Loop publication
  owner (`requestCommittedPassesRepublish` and its deferred service), not by parity verification in 4.1.
  The refresh requester belongs to 3.14 with 3.10's consumer/traversal integration, BEFORE 3.7 removal. What this edge owes it already does: advance the currency (revision + generation), settle
  the closures from the change's own identities, keep the ledger (the closure-obligation set), silence nothing and
  enumerate nothing. *Verification:* focused **64/64** (fixture extended again, not added), full native **2695 / 2688
  succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192`
  **unchanged** / `code 392,328 + padding 888` (48 B above the floor; byte-identical to the five previous slices). Three
  mutations red-proved with byte-identical restores, and **two of them deliberately mutate other owners' code** (the
  not-current guard, the revision bump) because those are the facts the handoff rests on — a fixture that only fails when
  its own slice is undone would not test the claim it states. **OWED in this task:** the refresh's requester (source publication -> 3.10/3.14 integration -> 3.7 removal);
  the between-grains matrix (repeated undo/redo, track/slot switching, stale completion while preparation grains are
  paused — 3.10/3.14 serving-path integration); retained-source lifetime and eventual release (3.6a, cited); displayed provenance; and
  preview-session undo/redo (3.13/2.12, cited). Its **audible** requirement and the red-proof the task demands
  ("restore the blanket silence") are complete. Detail:
  [`evidence/2026-10-02-s314c-refresh-source-verification/EVIDENCE.md`](evidence/2026-10-02-s314c-refresh-source-verification/EVIDENCE.md).

## 4. Checkpoint parity and content parity

- [ ] 4.1 Land **F-5** (checkpoint parity, undo) — verify an undo restores exactly what its entry named, including the
  **lagging** case, whose outcome must be named rather than reported as current
  *Increment 4.1 landed 2026-10-02 — the parity is **named** (`CheckpointParity` + the pure `resolveCheckpointParity`, and `Loop::checkpointParity()` reading the loop's own three figures: no new state, no pin move), and the **lagging** case is named through the *production* undo path with `publishedSnapshotIdentity()` null — selected, never presented as current. This also answers the question 3.14c left the refresh resting on: whether a root is current is now a **named** outcome (`OwesReconciliation` at the undo edge, measured), so the requester's precondition has a name even though the requester itself remains 3.14 implementation with 3.10 serving integration, before 3.7 removal. Verified: focused 65/65, full native 2696 / 2689 succeeded / 7 skipped / 0 failed, true clean rebuild with the RAM1 line byte-identical (48 B headroom); four mutations red-proved with byte-identical restores. **The task stays open** on the content-level comparison, whose instrument is 4.3's content-verification pipeline; save parity is **4.2**. No production caller consumes the name yet (4.2 and the readiness/lag reporting will) — stated rather than implied. Detail: [`evidence/2026-10-02-s41-checkpoint-parity/EVIDENCE.md`](evidence/2026-10-02-s41-checkpoint-parity/EVIDENCE.md).*
- [ ] 4.2 Land **F-6** (checkpoint parity, save) — verify a persisted body is equivalent to the root it was serialized
  from and a restore's outcome is either current **or** named as stale; the fixture must falsify today's
  `storePublishedSnapshot(…, kSnapshotCoverageCurrent)`-without-reconcile path
  *Increment 4.2 landed 2026-10-02 — the **verification** half: through the production writer (`writeLoopPersisted`) and the production restore edge (`applyLoadedLoopSlotBodyCommit`) the installed root is **equivalent** to the root that was serialized and the restore's coverage outcome is **named** (4.1's `CheckpointParity::Current`), which nothing asserted at that edge before. The task's **falsification** clause is answered by measurement, not by a pin: a body whose metadata and root cannot come from one committed state is accepted by the reader and installed as a *named-current* checkpoint (`publishedSnapshotIdentity()` non-null) — today's `storePublishedSnapshot(…, kSnapshotCoverageCurrent)` claims currency on the **writer's** admission, which the body does not record. **The task stays open** on the repair: persisting the root's coverage is a **format decision** (LSV1 v1 is pinned; no provenance field exists), and the cheap cross-check is **refuted by reading the code** (`EditSelectNoteState.cpp:188` → `Loop::allocateNoteId()`, so the note-id figure can advance while a settled root's committed state is unchanged). Verified: focused 52/52 (+1 fixture) + 65/65 + 190/190 (cited pin) + 38/38, full native 2697 / 2690 succeeded / 7 skipped / 0 failed, true clean rebuild with the RAM1 line byte-identical (48 B headroom); four mutations red-proved with byte-identical restores, their bound stated (a positive-outcome fixture cannot falsify an over-claiming parity). Detail: [`evidence/2026-10-02-s42-save-parity/EVIDENCE.md`](evidence/2026-10-02-s42-save-parity/EVIDENCE.md).*
  **Dependency/evidence correction:** the counter-only `nextNoteId` probe above does not establish divergent
  committed MIDI content; that counter can legitimately advance without changing a settled root. Establish the
  content-level oracle before choosing a repair. Persisted provenance is one possible format decision; bounded
  load reconciliation is another unresolved design option, not permission to change the format. This open parity
  work does not block unrelated request/handoff implementation with fallback retained.

- [ ] 4.3 Land **F-7** (content parity across a window boundary) — verify the windowed stream equals the whole-loop
  stream across the boundary, including **boundary note closures**, driven through the project's existing
  content-verification pipeline rather than a second one
  *Increment 4.3 landed 2026-10-02 — the **served window's stream is now readable** (`Track::playbackWindowEventAtForSlot`, emission order, served window only, no new state) and the parity clause has
  its first observable: 32 successive windows over a 64-bar demoted loop concatenate **row for row** to the whole-loop
  stream from the project's existing resolver, across all 31 boundaries, with the boundary note's ON in tile 0 and its
  OFF in tile 1 (measured: not synthesized at the edge) and the concatenation balanced, so no note-on is left unclosed
  by the transition. **The task stays open** on three named items: its **device leg** (6.4's windowed content-verification
  profile with 6.5's split; `ExecutionBounds.md` row 27 still denies active windowed playback), the **closure obligation
  at the transition** — the sequence is balanced while either window alone is not, so the closure is the consumer's
  (3.7 with 4.5's ledger set) — and the **overlap rule** (the interval policy is centred, so arbitrary-phase consecutive
  windows overlap; 3.7). Loop-wrap content is deliberately not in this fixture. Verified: 66/66 (+1 fixture) + 52/52 +
  190/190 (cited pin) + 38/38, full native 2698 / 2691 succeeded / 7 skipped / 0 failed, true clean rebuild with the RAM1
  line byte-identical (48 B headroom); four mutations red-proved with byte-identical restores. Detail:
  [`evidence/2026-10-02-s43-window-boundary-parity/EVIDENCE.md`](evidence/2026-10-02-s43-window-boundary-parity/EVIDENCE.md).*
- [ ] 4.4 Land **F-8** (length-independent root residency) — verify a residency statement per slot (windows resident,
  root demoted) plus content parity for the demoted state, **swept** over loop lengths
  *Increment 4.4 landed 2026-10-02 — **test-only**: the residency statement per slot, swept over 16/17/24/30/32/64 bars
  through the production policy and admission (`fullTakePlaybackViewAdmissible` + `prewarmPlaybackForSlot`, which
  decides the representation itself) and read from the slot's own accounting. Verified: the policy boundary is the
  declared bar cap; the whole-take representation is reserved **exactly while admissible and absent at every demoted
  length**; **every demoted length reports the same windowed reservation** (length-independent, and strictly smaller),
  with each view at the declared capacity and the stagings resident; at/above the declaration's floor the slot is
  **served from a window while its root is demoted**, and below it the refusal is **named**. **The floor as a number:**
  the lead the declaration's own arithmetic implies is 22,272 ticks ≈ 29 bars, so the floor is **exactly 30 bars** and
  loops between the bar cap and it have neither representation admissible — the number 6.3/3.10 need, pinned from the
  contract's fields rather than hardcoded. **The task stays open** on the parity half with the content-verification
  pipeline (6.4/6.5), the retained-source figure (3.6a), and 6.3's wider sweeps. Verified: 67/67 (+1 fixture) + 52/52 +
  190/190 (cited pin) + 38/38, full native 2699 / 2692 succeeded / 7 skipped / 0 failed, build SUCCESS and a no-op with
  the RAM1 line unchanged; three mutations red-proved with byte-identical restores, their two limits stated (the oracle
  is the policy function itself, so the contract's declared numbers are 3.10/6.3's to own; and the absolute figure is
  cited from `test_playback_prewarm`). Detail:
  [`evidence/2026-10-02-s44-length-independent-residency/EVIDENCE.md`](evidence/2026-10-02-s44-length-independent-residency/EVIDENCE.md).*

- [ ] 4.5 **Implement second-miss fault containment, the approved last resort, and cross-generation closures**
  (*DEC-108 §13e–g*) — latch a **named deadline-contract fault** on the second miss; continue the last coherent revision
  wherever valid coverage remains through an admitted bounded path; keep preparation progressing; recover at an eligible
  boundary; never reset the lag silently, publish partial content or block the clock. When no valid coverage remains,
  apply the **approved last resort**: targeted note closure plus affected-slot silence — never a global transport stop,
  never an unbounded rebuild. Closures: preserve unchanged sounding notes **without retriggering**; close
  removed/invalidated notes at handoff; suppress obsolete closures; emit note-offs **before** replacement note-ons; keep
  closure obligations alive after view retirement; and treat an **event-empty window as "no new events"**, never as
  silence for a sustained note. Verify with a forced two-miss-and-repeated-edit run, a closure whose note-off moves across
  the handoff, and a window that is empty while a note is sustained — asserting MIDI output, ledger state, displayed
  provenance and eventual recovery
  *Increment 4.5 landed 2026-10-02 — the **containment half**; the **task stays open** for the cross-generation closure
  half (§13g).* `ContinuityContainment { Nominal, ServeLastCoherentRevision, LastResortTargetedSilence }` with its
  **pure** `resolveContinuityContainment(faultLatched, validCoverageRemains)` is the containment rule, deliberately
  **total with no waiting and no rebuilding outcome** — so "block the clock" and "unbounded rebuild" are
  unrepresentable rather than merely avoided. The **second consecutive miss latches** `deadlineContractFaultLatched`
  (3.6b's once-per-episode breach *report* keeps its shape; the latch is what containment reads) and it clears at
  **one** place only — the eligible boundary where the current revision may be served — so containment is never mistaken
  for the one-pass-late guarantee and a further edit never forgives it (AC-5). `Track::containmentForSlot(slot, phase)`
  answers it as a **coverage** question (the served window must be readable and cover the traversal; an event-empty
  window still covers its interval, the structural half of "an event-empty window means *no new events*"), and
  `Track::applyLastResortForSlot(slot, phase)` applies the **approved last resort** — targeted note closure plus
  affected-slot silence through the existing **slot-scoped** owner, never a global transport stop and never a rebuild —
  answering `NotNeeded`, with **zero** emitted events, whenever coverage remains. *Verification:* focused **62/62**, full
  native **2693 / 2686 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with
  `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B above the floor; the line is byte-identical to
  the previous slice — FLASH-placed additions, no ITCM growth — and the new `bool` fitted padding, so **no size pin
  moved**). Six mutations red-proved with byte-identical restores; the first "track-wide silence" mutation did **not**
  discriminate (the fixture observed only the ledger, while that owner emits CC 123 and touches no ledger), so the
  fixture now asserts the real **MIDI output** through `MidiSendCapture`, and an assertion that was **vacuous in this
  suite** (`g_nativeSilenceTrackCalls`) was **removed** rather than kept as evidence, with a genuinely global mutation
  added so the sibling-slot assertion is shown to have teeth. **OWED in this task:** the **cross-generation closure**
  half (§13g) — preserve unchanged sounding notes without retriggering, close removed/invalidated notes at handoff,
  suppress obsolete closures, note-offs **before** replacement note-ons, obligations outliving view retirement — which is
  **emission at a version handoff** and therefore the consumption path's (3.7's consumer switch and 3.14's undo/redo
  handoff), together with its own verification (a closure whose note-off moves across the handoff, a window that is empty
  while a note is sustained). Detail:
  [`evidence/2026-10-02-s45-containment/EVIDENCE.md`](evidence/2026-10-02-s45-containment/EVIDENCE.md).
  *Increment 4.5b landed 2026-10-02 — the **cross-generation closure reconciliation** (§13g), the dependency 3.14 names
  ("task 4.5's off-clock selective reconciliation"); the **task stays open** for the serving-path integration.*
  `ClosureSettlement` is one handoff's plan as a **value** — the soundings the replacement **removes** (note-offs required
  at handoff), the ones it **re-issues** (their note-ons **suppressed**, because an unchanged note is never retriggered),
  and the ones it leaves alone — every list bounded by `ActiveNoteLedger::kMaxOpenNotes` and nothing allocating.
  `Track::planClosureSettlementForSlot` computes it **off-clock** (one bounded pass over the incoming note-ons into a
  256-byte presence set, then a walk of ≤128 ledger entries with no search), and
  `Track::applyClosureSettlementForSlot` emits the closures **first** and only closures — the replacement's note-ons are
  the caller's to emit afterwards, which is the spec's "note-offs before replacement note-ons" as a calling order rather
  than a promise — discharging exactly those ledger obligations through the ledger's own event path. The one
  **interpretive** rule is stated where it is decided and marked as such: absence from the incoming content is evidence
  of removal **only when that content is authoritative for the whole loop**, and never for an empty window, because a
  window is a *slice* and a note sustained across it is still sounding — which is what makes "an event-empty window means
  *no new events*" a consequence rather than a special case. *Verification:* focused **63/63**, full native **2694 / 2687
  succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192`
  **unchanged** / `code 392,328 + padding 888` (48 B above the floor; the line is byte-identical to the two previous
  slices — FLASH-placed additions, no ITCM growth, no size pin move). Four mutations red-proved with byte-identical
  restores (absence always removal; a re-issued note also closed; no closure emitted; the ledger not discharged); one
  non-unique-anchor harness repair is recorded, caught by the `shasum` oracle before the gates ran. **OWED in this
  task:** the emitted order at the **real** handoff (the clock-path caller that emits the replacement's note-ons after
  the closures), the **moving-closure** case (a note-off moved across the boundary, fired at its correct tick) and
  displayed provenance — all on the serving path (3.7's consumer switch). Detail:
  [`evidence/2026-10-02-s45-closure-reconciliation/EVIDENCE.md`](evidence/2026-10-02-s45-closure-reconciliation/EVIDENCE.md).

## 5. Capacity, envelope and placement

- [ ] 5.1 Record the window capacity and span as **provisional** — verify the 2,048-event ceiling is carried as the
  **window storage capacity itself** under the **sliding window-span invariant** (no partition multiplier; every `W`-wide
  interval including wrap fits, with the closure reserve inside the limit), that the two-bar span is the initial
  comparison configuration, that the derivation recomputes from the target-ABI `sizeof`s, and that neither is reported as
  a supported-density claim
- [ ] 5.2 Verify no accepted content is truncated to fit — verify a dense interval reports capacity pressure by name and
  serves every accepted event (the "Accepted content is never truncated" requirement)
- [ ] 5.3 Pin placement and cost — verify `static_assert` size pins cover any new state and are re-based with the reason
  recorded at the assert, no new state lands in DTCM (invariant 36), the corrected baselines (**6,368 B**
  `LoopPlaybackRuntime`, **984 B** `Loop`) are reflected in the docs, and `scripts/check_ram1_budget.py` passes on a
  fresh target build
- [ ] 5.4 State the envelope honestly — verify today's **eight** sounding roles are recorded as the supported envelope,
  **16** as the next target, and 32/48/64 as measured-without-promise, each supported claim gated on **both** a memory
  and a deadline test

- [ ] 5.5 Prove the **legacy paged compatibility path** within its declared envelope (*DEC-108 §14*) — verify page
  preparation, page handoff and MIDI emission each meet their deadlines for a restored loop whose requested interval
  exceeds `L`, including **over-capacity same-tick events**, **loop wrap** and **cross-page note closures**; assert
  complete, ordered, exactly-once output with no tick delay, no inline rebuild and no truncation; record that
  segmentation proves a **storage bound, not throughput**

## 6. Gates, sweeps and closeout

- [x] 6.1 Run the full native suite (`pio test -e native`) — verify every case passes and that no skipped or errored
  suite is counted as green
  *Verified 2026-10-05:* **2796 cases / 2789 succeeded / 7 skipped / 0 failed**, and at suite granularity **107 PASSED, 0
  FAILED, 0 ERRORED** with **4 suites SKIPPED and named** — `test_serial_midi_input_read`, `test_offline_boot_recovery`,
  `test_offline_undo_recovery`, `test_offline_workspace_recovery` (the isolated-recovery ABI / explicit-env suites the
  `native` env configures out; the runner reports them `[SKIPPED]`, never `PASSED`, so none is counted as green). The 7
  skipped **cases** carry their reasons in their names: `test_reconstructed_workspace_loads` ×3 and
  `test_reconstructed_history_roundtrips` ×2 (*isolated recovery ABI build required*), `test_copied_slots_decode` ×1
  (*set `MIDILOOPER_RECOVERY_SLOTS` to verified host-copy current/slots*) and
  `diagnostic_only_reconstruct_175026_tagged_off_pairing` ×1 (a parked finding, diagnostic only).
- [x] 6.2 Build the firmware env (`teensy41-capture-serial`) through the RAM1 budget guard — verify the build succeeds
  and the budget line is reported; do **not** flash without asking
  *Verified 2026-10-05:* `pio run -e teensy41-capture-serial` → **SUCCESS** with the budget reported:
  `variables 96,192 / ITCM 393,216 (code 391,576 + padding 1,640) / locals 34,880 | approved floor: padding >= 840`
  (800 B above the floor; no re-base). The qualification env added later reports its own recorded line
  (`code 392,232 + padding 984`). The board was **not** flashed by this task: every flash in this change's sessions was
  made only after explicit authorization, and the two authorized runs are recorded under 6.5.
- [ ] 6.3 Run the sweeps — verify loop length, content density, tempo, window span and competing deferred work are each
  varied and reported, and that a single configuration is never presented as the acceptance
  *Partially supplied 2026-10-05 — the **contention** and **window span** axes and part of **content density** are now
  measured and reported rather than assumed:* the `H x K` sweep drives the production composition at the declared
  permitted advance and writes down `drainTurns = H x (1 + K) x serviceTurns` with its grid (H 1/2/4/8 x K 0/1/2), the
  density axis (5/20/80 notes → 2/7/33 service turns) and the span axis (identical cost at 17 and 41 bars), plus the
  boundary pair where the declaration predicts a crossed promise and the crossing is named. The task stays **open**:
  tempo is swept only through the declared advance (not other tempos), chords/same-tick density are not swept, the
  device leg is not run, and no single configuration is presented as the acceptance — see
  [`hk-contention-sweep`](evidence/2026-10-05-s310x-hk-contention-sweep/EVIDENCE.md).
- [ ] 6.4 Add a dedicated **windowed HITL profile** through the existing content-verification pipeline (canonical
  stimuli preserved, never edited to chase a condition) — verify the profile is registered, authorized and used for the
  F-7/F-8 device legs and the AC-8 receipts under a running transport. Profile/oracle preparation may precede
  rolling implementation; registration, authorization, exercise and successful qualification remain distinct

  *Partially supplied 2026-10-05 — the **registration and authorization** half:* `STIM-WINDOW-CONTENT-01@v1`
  (`windowed_content_parity`) is registered in the canonical registry with a **profile of its own** — a 16-bar loop with
  one committed 16-bar revision against the lane's two-bar span, so a window is a strict sub-interval and the transport
  keeps running across boundaries — and it carries its **declared target authorization** (`track 8 slots 4,5`, source the
  standing 2026-09-30 grant narrowed to the bench target; `base_stimulus_id` is deliberately `None`, since no registered
  profile shares its recipe shape). Its **oracle is declared, not assumed**: `must_prove` names
  `content_parity_across_window_boundary` (F-7), `root_demoted_residency_over_loop_lengths` (F-8),
  `readiness_receipts_under_transport` and `windowed_service_coverage_under_transport` (AC-8's `DIAG,win_readiness`
  receipts under transport and the `DIAG,win_gap` coverage blocks). The task stays **open**: the scenario that would
  **drive** the leg is deliberately not registered (`PRESET_SCENARIOS` has no entry, so `resolve_scenarios` refuses it),
  and the pin `test_the_windowed_leg_has_a_registered_profile_and_no_driver_yet` asserts that absence on purpose — it
  turns red the day the driver lands. Red-proofed by MR-6.4a (declared scope widened to 4,5,6) and MR-6.4b
  (`base_bars` 16 → 2), each leaving the pin the only failing test and the file restored byte-identically. See
  [`s64-windowed-hitl-profile`](evidence/2026-10-05-s64-windowed-hitl-profile/EVIDENCE.md).

  *Also supplied 2026-10-05 — the **exercise** half, off-device:* the profile is now selectable by its own preset and
  read by a verifier, and the driver is the **existing content-verification leg, not a second pipeline** — as
  `design.md` and `S4C45` require. `run_windowed_content_parity` delegates to `run_long_loop_display_window` unchanged (a
  loop far longer than the served two-bar window, transport running across the boundaries; the run itself refuses
  `<= 16` bars), and `verify_windowed_content_parity` pairs that leg's oracle with the windowed lane's own checks:
  `readiness_receipts_under_transport` (AC-8 — a receipt that only arrives after the stop fails),
  `windowed_service_coverage_under_transport` (`win_gap` blocks with `running=1` and opportunities > 0),
  `no_windowed_overrun_under_transport` (the **named** exceedance, not a smoothed number) and
  `window_boundaries_crossed_under_transport` (≥2 `build_complete` publications under transport). Two of the profile's
  four declared terms stay **deferred with their verdict's owner** — the parity verdict to the content pipeline, and
  `root_demoted_residency_over_loop_lengths` to an **unidentified row set** recorded as unknown — and every result
  carries `oracle_complete: False`, so a green run is never read as F-7/F-8 qualification. Red-proofed by **MR-6.4c**
  (any receipt counts) and **MR-6.4d** (the named overrun no longer fails), each failing exactly its own test with the
  file restored byte-identically; the pin also **inverted** its own absence clause (the reminder that the driver was owed
  fired) and the exact-preset guard now names the new preset. Host suites green (new 8, plus 26 / 24 / 3 / 6 / 40 / 39 /
  9 / 2 / 45), native unchanged at **2796 / 2789 succeeded / 7 skipped / 0 failed**, strict validation and
  `git diff --check` clean. **Still owed, and not claimed:** the leg has **not run on a device** — the lane's inventory
  at a window boundary has never been observed, so the first authorized run may correct this oracle rather than the
  firmware. **6.4 stays open.** See
  [`s64-windowed-leg-driver`](evidence/2026-10-05-s64-windowed-leg-driver/EVIDENCE.md).

  *First **authorized device run** 2026-10-05 (owner authorized the upload this session):* the windowed leg ran on
  hardware — image `1337956a05c4aeda…` from git `abdbca63`, RAM1 unchanged — and it drove the target shape (a 24-bar take,
  `Recording stopped … length=18432`) on the **empty** bench slot 6, with the device's own row confirming nothing was
  removed (`Clear removed persisted slot 6 payload (absent)`); slots 4 and 5 (which do hold content) were never touched.
  **What the run corrects, rather than what it claims:** the lane produced 44 `win_gap` blocks — *all* with `running=0`,
  `permitted=64`, schema `3`, and **zero** eligible/progress/denied turns — plus **zero** `win_readiness` rows and **zero**
  `build_complete` publications, so `window_demand_observed` is now the verifier's **first** named check: the leg's real
  prerequisite is a **demand** (the rolling lane enabled, or a launch/retry in flight), not a running transport, because a
  plain record-and-play owes no window work and the lane's rows are event-shaped (red-proved by **MR-6.4e**). The run also
  validates the row reading on real rows (`permitted` in field 16, schema `3` in 19 — the parser is no longer a
  synthetic-rows-only claim) and records two findings: **F-11** the reused long-loop leg's *display* oracle is stale
  against the emitted DISP schema (12 fields vs the 14 it parses — that leg's business, and it blocks this composition
  because the verdict ANDs both halves), and **F-12** the lane's dispatch stopped entirely once the transport started
  (44 blocks idle, none across ~97 s of recording/playing; the deferred-idle-owner explanation is recorded as a
  hypothesis -- **withdrawn in the same session**: F-12 read a capture-regime property as a dispatch property, and the
  qualification run corrected it (see below), which matters for 3.10). **Then the owner took the enablement decision:** the
  lane was opened in a dedicated qualification build (`teensy41-capture-serial-window-qualification`, the shape the
  firmware's own guard requires; RAM1 baseline recorded deliberately at `code 392,232 + padding 984`, +656 B of ITCM code,
  144 B above the floor), flashed (`597ab7675a83d7cc…`) and driven again — and the second run **corrected F-12 and found
  F-13**: post-transport the capture carried exactly **one** `#CAP` row in ~262 s, because `flushCaptureBuffer` sheds
  **non-Tier-A** head records at the timing-critical budget of 8, and `CaptureLineTier.h` lists `playback_build`/
  `playback_order`/`win_readiness` — but **not** `win_gap`/`win_gap_overrun`. So the lane's coverage and overrun rows are
  shed silently in the very regime they exist to measure: 6.5's hardware envelope claims **cannot be measured** until that
  tier entry exists, which is a separate decision (the authorization here was the lane's enablement, not an instrument
  change). `win_readiness` is Tier-A **and** direct-when-room, so its absence in both runs remains evidence — no receipt
  was produced.
  *Corrected and extended 2026-10-05 (F-14):* F-13 was **wrong** — `DIAG,win_gap,` **is** Tier-A already, classified in
  `DebugSessionCapture.cpp` for a recorded RAM1 reason, and pinned by `test_capture_ring`. The real gap was
  narrower: the **named overrun row** `DIAG,win_gap_overrun,` did not share its sibling's prefix and so was not Tier-A —
  the row that reports an *exceedance* was the row shed in the regime exceedances happen. Fixed with one entry beside its
  sibling (M1/M2 red-proved, restored byte-identically). The wrapper also now ends the leg with a transport **stop** and a
  12 s **drain**, because the ring is retained under transport and the earlier runs stopped reading while it was still
  running; and the verifier's "under transport" rule was corrected from stream position to **device time** so a retained
  row keeps its own time. **The device then said something positive for the first time:** 54 `win_gap` blocks, **50 with
  `running=1`** and rising phase — the lane *is* dispatched under transport — every block `permitted=64`, `overruns=0`,
  and every block `eligible=progress=0`, so the lane still owed nothing. That is a real measurement of service under
  transport and **not** the envelope; the next step is a leg whose gestures create a demand (a demoted slot switch).
  *Switch leg supplied 2026-10-05 — the gesture is in the driver, and the device says why the lane still has no work.*
  `run_windowed_content_parity` gained a **switch leg** (select a filled non-active target, press its record control, wait,
  then the existing drain), absent by default and gated on a running transport; its press **order** is the safety argument
  (read from `MidiButtonActions.cpp`: with the track playing and the slot filled, `slotCanArmForRecord` is false, so the
  recording branch is unreachable and only a playback switch can be queued) and it is pinned, with M1 (order reversed) and
  M2 (gate removed) red-proving the pins and the file restored byte-identically. The successful device run shows
  **`Loop 4: Queued playback switch`** (never `Start Recording`) and the switch **held and named**
  (`slot_switch_hold_exceeded track=7 target=3 age=3072 ticks`) — and the lane *still* with `eligible=progress=0`
  everywhere, which the same capture explains: the target was **cold** (its on-demand restore was queued —
  `RRQ,7,3,…queued`, `capture_prewarm … ready=0` — and never completed inside the run), so neither the legacy merge nor the
  lane's launch had a source to prepare. *Revised by run 04:* with the target's restore **completed** the switch still
  held, and the window between the queue and the hold contains **no preparation activity at all** — the leading (and
  recorded-as-hypothesis) cause is now the **LOOP_EDIT session** that the slot selection itself opens immediately before
  the queue, since the lane refuses while an edit session is active and the S4b device legs prepared their switches
  without one. Also recorded: the reset for these runs must be a flash of the **qualification** image —
  `scripts/hitl_boot_census.py`, the documented reset-by-re-upload path, hard-codes the **default** env, where the lane is
  compiled off, so using it would silently invalidate the run. **Run 05 refuted that too:** the driver now exits the edit session
  (a long press on the edit control, pinned) and the device confirms the exit — yet the queued switch still emitted **no row
  of any kind** for its whole bound (no `RRQ`, no `capture_prewarm`, no provision, no launch, and no lane work: 59 `win_gap`
  blocks, all `eligible=progress=0`). **Three hypotheses tested, two refuted** (cold target; edit session), and the live one
  is no longer about the leg at all: the next investigation is the **code** — `servicePendingSlotProvision` and what it
  gates on — which needs no device, with the S4b legs (whose switches *did* prepare and commit on this board) as the comparison.
  **Run 06 (a 24-bar target) and the code gate:** `servicePendingSlotProvision` calls the lane **only** for an
  `immutableLaunch` target — one that is not the active slot **and** whose `shouldAvoidFullVisualRebuild(loopLength)` holds
  (`loopLength > 16 bars`, the same bound the reused leg enforces) — and run 01's own selection feedback shows the decisive
  input is the target's **runtime** length, which read **`length=0`** for the 2-bar slot (not resident), putting that
  switch on the **legacy** branch where no work was done at all. Run 06's outcome was identical to the others and its
  selection printed **no** loop-length row, so whether the 24-bar target took the launch branch is **unknown** — the one
  value still missing. **The measurement was taken and it classifies the blocker.** Read from the device's own selection feedback after a fresh
  qualification flash: the 2-bar slot's runtime length is **0** (not resident) while the **24-bar** slot's is **18 432** —
  so run 06's switch *did* satisfy the routing condition and `servicePendingSlotProvision` **did** call the lane. What stops
  it is the **root**: both demand paths refuse a source whose whole-loop root is not current
  (`TrackPlaybackWindowBuild.cpp:2374` `RollingOutcome::Excluded`, `:2861` `SourceUnavailable`), and a **demoted** slot has
  no root at all (`has_root_content=false` for both 24-bar slots; `true` only for the 2-bar one) — which is this change's own
  `shouldAvoidFullVisualRebuild` policy. **Classification: neither closure work nor a defect of this change** — it is
  RWO's already-recorded **S4b.7 published-root question** reproduced on hardware, and it exposes a tension with this
  change's **F-8** claim ("a window is served without the whole root being resident"). Two resolutions and both are
  decisions, not verification edits: the restore edge installs the root as current (RWO), or the demand paths stop requiring
  a whole-loop root for a demoted interval (a DEC-108 design decision here). **F-16 recorded:** the demand paths' refusals
  are **unobservable on device** — a typed enum with no row — which is exactly the distinction five runs were spent on. See
  [`switch-leg-demand/MEASUREMENT.md`](evidence/2026-10-05-switch-leg-demand/MEASUREMENT.md). See
  [`switch-leg-demand`](evidence/2026-10-05-switch-leg-demand/EVIDENCE.md). **Not claimed:** the lane was **not exercised**;
  F-7/F-8/AC-8 remain unverified on hardware; no firmware was changed and no oracle was bent to fit an observation. See
  [`s64-s65-device-leg-01`](evidence/2026-10-05-s64-s65-device-leg-01/EVIDENCE.md).
  *Update 2026-10-05 (selector fix, switch leg):* the launch's source publication is no longer an open blocker for the
  switch leg -- `Track::loopWithPendingSnapshotPublish` now ranks in-flight > startable > other pending work (active loop
  wins ties; native fixtures red-proved against `60767dbe`), and the leg completed end to end on hardware twice
  (`captures/s64-switch-12`, `-13`: `launch_requested → launch_ready`, no hold). **Parked:** the *starved* shape (a
  rootless, pending demoted target) was never produced on device and is not pursued unless it shows up as a real defect.
  6.4 stays open on its device-oracle items; nothing here claims F-7/F-8/AC-8.

  *Also supplied 2026-10-05 — the **oracle preparation** the device leg was missing (firmware instrumentation, natively
  verified; **6.4 stays open**):* `DIAG,win_gate` makes the **rolling demand path** name itself. Every exit of
  `Track::serviceRollingPlaybackWindow` returned a typed `RollingOutcome` to its caller with **no** row, which is **F-16**
  ("*a device reader cannot tell 'nothing was owed' from 'the source was refused'. That is exactly the distinction five
  runs were spent on, and a row would have settled it in one*") — and it is the exit this profile's own leg has been
  unable to explain: the lane is **dispatched under transport** (`win_gap` `running=1`) and builds nothing, with the
  demand's entry decision (`!clock.epoch || !shouldAvoidFullVisualRebuild(…) || loopLengthTicks % span != 0`) the one
  remaining explanation and the one that had no row. It is the launch/snapshot gate's third sibling: assembled from the
  **active slot's own state**, emitted **once per state change** per track (`gr` is excluded from the key: it is a
  progress counter, not a decision), `x` naming this call's exit, `ep`/`av`/`al` telling the entry exit's three terms
  apart so one token cannot hide which guard refused, and `rq` carrying the request's own ordinal. Qualification-only by
  build: the plain capture env compiles the call to `((void)0)` (RAM1 padding 1,640 unchanged) and the qualification
  image's `code 392,232 + padding 984` is byte-identical. No stimulus, oracle term or oracle verdict was bent to take
  it. Red-proofed by MR-W1 (the entry exit goes silent — *Expected 1 Was 0*), MR-W2 (dedupe removed — *Expected 1 Was
  2*) and MR-W3 (the terms stop being told apart — its `av=0` assertion fails), each failing exactly its own case with
  `TrackPlaybackWindowBuild.cpp` restored byte-identically (sha256 `27d6ffde…`). Verified: focused
  `test_runtime_working_set_ownership` **144/144**, `test_capture_ring` **16/16** with the new Tier-A boundary pin, full
  native **2,801 / 7 skipped / 2,794 succeeded / 0 failed**, both capture builds SUCCESS with RAM1 byte-identical, marker
  audit 0 over, `git diff --check` clean, strict validation valid. **Owed at that point:** the row had not been read on a
  device; that needed an authorized flash of the qualification image plus the registered leg re-run on a demoted active
  slot — which the owner then authorized, below. Native evidence:
  [`rolling-gate-row`](evidence/2026-10-05-rolling-gate-row/EVIDENCE.md).

  *Then the **authorized device run** (owner: "Authorize the upload + windowed leg now"), and it both read the answer and
  found a defect in this row* (`captures/s64-win-gate-0{3,4,5}`, the registered profile through the content-verification
  pipeline; `--serial-port` because the managed path injects flags the reused leg's strict parser rejects — **F-17**,
  recorded not patched, with both failures reproduced in `s64-win-gate-01/-02`)*:* the demand on the driven track dies at
  its **entry** because the loop the leg's own record stop commits is **not a whole number of two-bar spans**
  (`len=19200` = 25 bars, `al=0`) while it *is* demoted (`av=1`) — and `ep=1` in **every** row, so the clock-stamp
  candidate the earlier record carried is **refuted**; where the loop *is* aligned the second gate is named instead
  (`x=win_not_servable`, `rt=0`). The row's own **change key was wrong** and only the device showed it: bits 10
  (`coverageRoleAvailable`) and 13 (`viewBuild.active()`) alternated A<->B, the traps list's "dedupe that alternates
  forever" — 809 rows in one leg with up to 20 identical lines per state; with the corrected key the **same** leg is
  **57** rows with no alternation (run 04 is kept because it is the *wrong first guess*, bits 11+13, and it says so).
  The profile's registered verifier was replayed over run 05 (`captures/s64-win-gate-05-verify`): `window_demand_observed`
  false ("no slot owed a window build"), `windowed_service_coverage_under_transport` **true** (blocks=14,
  under_transport=1, permitted=64), `no_windowed_overrun_under_transport` **true**, receipts 0 and `build_complete` 0.
  **Not claimed, and the reason 6.4 stays open:** the lane was never given a *demand*, so F-7/F-8/AC-8 and 6.5's four
  hardware claims remain **NOT RUN**; the next shape needs a committed length that is a whole number of two-bar spans (a
  *leg-shape* decision), and the root-currency question stays the owner decision already on record. See
  [`DEVICE-RUN.md`](evidence/2026-10-05-rolling-gate-row/DEVICE-RUN.md).
- [ ] 6.5 Apply the **native/hardware verification split** (*DEC-108 §13 verification split*) — verify **native** forces, on
  the real request→worker→publication→consumption path with controlled ticks and explicit expected MIDI output (no
  sleeps, no copied state machines): A→B→A slot reuse with the first A completion arriving last, the interval A/B
  revision trace, cancellation between every preparation grain, repeated edits without a deadline reset, stop/start,
  undo/redo, clear/load, clock revalidation, resource refusal followed by recovery, and closures crossing version
  boundaries. Verify **hardware** covers grain duration and maximum service gaps, sustained multi-track playback with
  capture and persistence contention, external-clock behaviour, and memory high-water marks with boundary content parity
  — and record that reproducing the exact A→B→A race on hardware is **not** required once the native ordering proof
  exists (native cannot establish physical timing; hardware is not a substitute for the ordering proof)

  *Partially supplied 2026-10-05 — the **native** half's two gaps are now forced on the real
  request→worker→publication→consumption path:* **A→B→A view-slot reuse with the first A completion arriving last**
  (`test_rolling_path_reuses_the_view_slot_after_a_refused_completion`: the late completion publishes nothing, retains its
  stale identity, ends its own episode, and the slot it built in is re-populated by the next episode under the *current*
  identity and served — 1536 serving ticks, 22 events = 11 note-ons + 11 closures, no foreign note) and **cancellation
  between every preparation grain** (`test_rolling_path_cancellation_between_every_preparation_grain_recovers`: seven
  grain cycles cancel a retained demand and a live builder, each leaving no demand, no builder, no candidate, nothing
  emitted, and the coverage source retired — then one clean episode is served). The task stays **open**: the **interval
  A/B revision trace** is still only *partial* (shared-view fixtures, no rolling-chain trace), and the **hardware** half
  (grain duration and maximum service gaps, sustained multi-track playback under capture/persistence contention,
  external-clock behaviour, memory high-water marks with content parity) is **NOT RUN** — it needs 6.4's registered
  profile and an authorized upload. Mutations **MR-B** (late completion not recognised) and **MR-C1/MR-C2** (cancellation
  not releasing the demand / not discarding its owned build) red-prove the fixtures; **MR-A** is recorded as a
  non-red-proof (the operation identity is stamped on more than one path). See
  [`s65-native-verification-split`](evidence/2026-10-05-s65-native-verification-split/EVIDENCE.md).

  *Hardware half, first authorized run 2026-10-05:* the device was built, flashed and driven (see 6.4's note and
  [`s64-s65-device-leg-01`](evidence/2026-10-05-s64-s65-device-leg-01/EVIDENCE.md)). What it hands back is a **prerequisite
  finding rather than a measurement of the hardware claims**: with `PLAYBACK_WINDOW_ROLLING_ENABLED` at its default `0`,
  a plain record-and-play owes no window work, so the lane dispatched with nothing to report — the four hardware claims
  (grain duration and maximum service gaps, sustained multi-track playback under capture/persistence contention,
  external-clock behaviour, memory high-water marks with content parity) are **still NOT RUN**, and the run says why:
  they need a **demand** (the owner's enablement decision for the rolling lane, or a leg that drives a demoted switch).
  The task stays open on all four.

  *Also supplied 2026-10-05 — the **instrument the hardware half's reading was missing*** (firmware, natively verified;
  **6.5 stays open**): this task's four hardware claims are read off a lane whose demand path was silent, and
  `DIAG,win_gate` (see the note under **6.4**) now names that path's exit with the inputs that decided it. It changes no
  native expectation and no claim here — the interleavings, ordering proofs and cancellation forces above stand as
  recorded — but it is what the next authorized device run reads instead of inferring, and it is what will say whether
  the lane's own entry (`ep`/`av`/`al`) or something further in refuses. Full native after it: **2,801 / 7 skipped /
  2,794 succeeded / 0 failed**; both capture builds SUCCESS with RAM1 byte-identical. See
  [`rolling-gate-row`](evidence/2026-10-05-rolling-gate-row/EVIDENCE.md).

  **Updated by the authorized run (same day):** the instrument then **read the demand's decision on hardware** and the
  lane was still never given a demand -- the driven loop is unaligned (`len=19200`, `al=0`) and the clock is stamped
  (`ep=1`), with `x=win_not_servable, rt=0` naming the second gate where the loop is aligned. So this task's four
  hardware claims are **NOT RUN for a now-measured reason**, not for want of an instrument. See
  [`DEVICE-RUN.md`](evidence/2026-10-05-rolling-gate-row/DEVICE-RUN.md).
- [x] 6.6 Update RWO's dependency records now that the sibling exists and carries its DEC and reassessment — verify RWO
  `tasks.md` (S2.9/F) and `design.md:812` point at this change, and that S2.3's `N` and `S7 gates` status are restated
  accurately under [`docs/Authority/DOCUMENTATION_CLOSEOUT.md`](../../../docs/Authority/DOCUMENTATION_CLOSEOUT.md)
  *Updated 2026-10-05:* RWO's three records that still read "planning artifacts only" / "nothing is implemented" were
  corrected to the accurate state — the **phase table's row F**, the **S2.9 block** and the **dependency-contract block**
  in `tasks.md`, and **§3 Extracted stage** in `design.md:812` — each now naming what the sibling has actually landed
  (window identity, publication, readiness-holder vocabulary, lag rule, the admission boundary on the playing chain, its
  native forces, and a registered windowed HITL profile with two authorized device runs) while stating that its
  **capacity/scheduling/retention bounds stay unproven**, its gates are open and **nothing of it may be applied from
  RWO**. `S2.3`'s **`N` stays provisional**, with the reason sharpened: no `N` has been derived on the sibling's side
  (its record states today's **eight sounding roles** as the current capability, not a law). The **`S7 gates`** entry
  gained a status restatement: its phase-F clause is **unmet** — including the sibling's own hardware envelope being
  **NOT RUN**. Both changes validate `--strict`.
- [x] 6.7 Close out the docs — verify `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md`,
  `docs/DELIVERABLE_TRACKING.md` and this change's own task list reflect the shipped state, and
  `openspec validate windowed-playback-generation --strict` is valid
  *Verified 2026-10-05:* `PROJECT_STATE.md` and `CURRENT_WORK.md` carry this session's slices head-first (the native
  split, the profile's registration and exercise halves, both device runs, the F-13 retraction and the F-14 fix with the
  measured lane rows); `DELIVERABLE_TRACKING.md` now carries this change's own entry — it had **none**, so its absence was
  the gap — recording what shipped, the two device corrections, the measured lane result and the named open items;
  this task list reflects the same state, including the items that stay open. `openspec validate
  windowed-playback-generation --strict` and `runtime-working-set-ownership --strict` are both valid, and
  `git diff --check` is clean.
- [ ] 6.8 **Continuity evidence handoff** — reference the runtime spec's *Continuous runtime operation has
  one end-to-end acceptance contract* and its task S7.5a — envelope/timing oracle, rather than inventing
  local end-to-end thresholds. Before enabling the affected production paths, allocate and verify the
  window/index/retention/page/closure work against that shared service and memory budget. Under simultaneous
  playback, capture/finalization, undo/redo and persistence/display contention, record native transition
  evidence plus hardware grain/service-gap, input and route-specific output timing evidence. Assert no
  in-envelope capacity refusal, coverage fault, unintended note interruption or involuntary stop. A
  one-pass-late edit is measured as audible-action latency, never hidden as immediate execution. Supply
  evidence to runtime task S7.5b — dependency closure and task S7.5 — combined continuity acceptance;
  passing this subsystem does not certify the whole runtime. S7.5b/S7.5 are downstream consumers, not
  prerequisites for producing this handoff; S7.5a's oracle/budget is upstream. Keep task 4.5's fault-injection results separate.
  *Supplied 2026-10-05:* [`CONTINUITY-HANDOFF.md`](CONTINUITY-HANDOFF.md) — it states **no** local end-to-end threshold,
  point-for-point at S7.5a's unpinned oracle (so the window/index/retention/page/closure allocation is recorded as
  **owed**, not invented, and the enablement consequence is stated: the rolling path lives only in the dedicated
  qualification env); it supplies the **native transition evidence** this change has, and the **hardware** evidence as
  *partial and named as partial* — 50 service blocks under transport at the declared advance with zero overruns, and
  `eligible=progress=0` everywhere, so grain duration, service gaps under load, contention, external clock and residency
  were **not** measured; it relates this change to each of S7.5's assertions (coverage-fault names, the one-pass-late rule
  as latency and never hidden execution, the closure fixtures) while leaving each number to S7.5a; and it routes the
  artifact to **S7.5b/S7.5** as downstream consumers, with 4.5's fault-injection results kept separate.

  *Increment 3.10 (increment 8) landed 2026-10-02 — **source-lifetime repair, fairness repair, and the derived admission (not enabled)**. Two defects in my own
  earlier increments: (1) the fixture helper gathered the request source into a **stack-local** vector while the build reads `source_[cursor_]` **across turns**
  ⇒ dangling memory; the repair (a file-scope **per-slot** buffer) is what **proves the measurements were invalid** — three fixtures' outcomes changed with a
  valid source, and two cases of the urgency fixture turned out unable to fail at all and were removed. (2) `selectPlaybackHolder` applied earliest-deadline-first
  **globally**; the spec says **round-robin across** holders with the earliest deadline **within a holder**, so a permanently-earlier deadline was served every
  rotation and starved the rest. New `selectPlaybackHolderRoundRobin` is the across-holders rule, the within-a-holder doc is corrected, `main.cpp` uses the
  round-robin form, and a new fixture proves both tracks are served in one rotation with genuinely unequal deadlines. **Admission derivation:** the window's own
  event count through `RuntimePlaybackOrderBuilder::requiredGrains` + `ceil(events/64)` + 1 — corrected cost model: events = **2 × notes**, fills exactly
  ⌈events/64⌉, orders equal to the helper where a merge runs (20 events already ordered: bound 5, measured 1 — increment 7's "threshold near 10" was that skip;
  its 1 600-grain extrapolation was wrong, the helper gives **982**). It is **implemented and deliberately not enabled**: at the declared advance an 80-note
  window's derived need is ≈428 bars, so enabling refuses what the current constants admit — the capacity reduction the review said not to approve on unmeasured
  evidence. **The service-gap bound is split:** the rotation half is proven, the ticks-per-turn half is not (`maximumPhaseAdvancePerTurn` is still the declared one
  bar, now asserted as a statement). Verified: 73/73, full native **2708 / 2701 / 7 / 0**, build SUCCESS from a true rebuild with the RAM1 line byte-identical and
  no re-base. Detail: [`evidence/2026-10-02-s310h-lifetime-fairness-derivation/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s310h-lifetime-fairness-derivation/EVIDENCE.md).*

  *Increment 3.10 (increment 9) landed 2026-10-02 — **the service-gap unit pinned and increment 8's bound retracted as non-general**. The "≈428 bars" figure
  in increment 8 was wrong twice: it **double-counted** the owner rotation (the declared advance is *per this owner's opportunity*, which already contains the
  other owners' share) and it **omitted the contention inside the lane** (one track of every `H` eligible tracks per lane turn, plus the chosen track's sibling
  slots, whose earlier intervals may take that turn under the spec's within-holder earliest-deadline-first). `CoverageServiceContract::maximumPhaseAdvancePerTurn`
  now pins its unit at the point of declaration, names the `H × K` factors it does **not** contain, and states that `canCompleteBeforeHandoff`'s
  `grains × (denied + 1)` is **not yet a complete bound**; the sweep fixture asserts that structure (61 grains for an 80-note window; the still-declared one-bar
  advance; the admission's turn count smaller than the contention factor) rather than a fabricated bar figure. **Leaving cost enforcement disabled does not make
  the current admission safe**, so active windowed playback stays **gated** — as `ExecutionBounds.md` row 27 records and as 3.7's block depends on. Verified:
  73/73, full native **2708 / 2701 / 7 / 0**, build SUCCESS, RAM1 line byte-identical, no re-base. Detail: [`evidence/2026-10-02-s310i-service-gap-unit/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s310i-service-gap-unit/EVIDENCE.md).*

  *Increment 3.10 (increment 10) attempted 2026-10-03 — **the service-gap unit is not measurable from the existing instrumentation** (device run, negative result;
  no code changed). Ran the canonical baseline preset against the attached board (metadata pins `git_commit 24134bd8…` / `firmware_hex_sha256 5e1dfa21…`): the run
  is **INCONCLUSIVE_CAPTURE** (`RECA,entered=0`, the record gesture never armed — the baseline stimulus declares `targets=track 8 slots 4,5` and I passed
  `--track 5 --loop-slot 1`), so it is not a pass and nothing content-wise is claimed from it. The useful result is the **row semantics**: `service_period` and
  `idle_maint` are `<maxUs>,<overCount>` — service *durations* (2 604 µs and 84 µs observed), not opportunity gaps — `BAR,<tick>` is emitted on bar boundaries
  (one row in 2 559) and `DIAG,clk` carries 0, so **no row pairs the playhead phase with the number of opportunities**. The unit therefore needs its own
  **gated instrumentation** (a per-`PlaybackViewMaintenance`-opportunity phase row, cadence-bounded, `DMAMEM`/`FLASHMEM` per DEC-108 §13b, RAM1 guard checked)
  before device data can be collected; then one run with the stimulus's own targets, then measure → declare the `H × K` envelope → re-base and enable. Detail:
  [`evidence/2026-10-03-s310j-service-gap-not-measurable/EVIDENCE.md`](evidence/2026-10-03-s310j-service-gap-not-measurable/EVIDENCE.md).*

  *Increment 3.10 (increment 11) attempted 2026-10-03 — **the service-gap sampler written, native-green, and blocked by the held RAM1 pin** (reverted; nothing
  landed). Implemented and proved at 74/74: `windowServiceGapImmediate` + the `SC_WINDOW_SERVICE_GAP` seam, a sample at the **top of every lane
  opportunity** (a turn that serves nothing still counts, which is the case the gap must include), `DMAMEM` state, one row per 16 opportunities, each row
  carrying the **max gap since the previous row**, `DIAG,win_gap,` Tier-A, plus a fixture with a known progression (quiet 1 000, one 5 000 spike) and three
  mutations (`Expected 5000 Was 1000` no-running-max; `Expected 5000 Was 91400` no-per-row-reset; `Expected 1 Was 2` interval 16 → 8). **The RAM1 guard then
  refused the link** — `variables 100,288` vs approved `96,192` (**+4,096 B**), with `code` unchanged and `locals` down to 30,784, i.e. one DTCM `ALIGN` page.
  Four rebuild diagnostics showed the cause is **scale, not the literal**: header literal `100,288`; literal removed **`96,192` ✓**; literal moved into the single
  TU `100,288`; `inline`→`static constexpr` `100,288` — the section is within a few bytes of the boundary, so shaving cannot fix it. **The fix is the playbook's
  routing** (that TU's `.rodata` to FLASH in `linker/imxrt1062_t41_lcr.ld`, as lines 98/99/113 do), which lands `variables` **below** 96,192 and therefore requires
  **re-basing `scripts/ram1_budget.json` in the same commit** — a pin the project holds deliberately, so the decision is the user's: re-base downward with the
  routing fix, or retire ~4 KB of DTCM first. Everything was reverted to green (73/73; approved RAM1 line) and an **unrelated concurrent edit** in
  `docs/Runtime/CURRENT_WORK.md` was preserved rather than staged. Detail: [`evidence/2026-10-03-s310k-gap-sampler-ram1-blocked/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-03-s310k-gap-sampler-ram1-blocked/EVIDENCE.md).*

  *Increment 3.10 (increment 11, LANDED) 2026-10-03 — **the service-gap sampler is in, with no re-base and no linker change.** `windowServiceGapImmediate` +
  the `SC_WINDOW_SERVICE_GAP` seam, sampled at the **top of every lane opportunity** (a turn that serves nothing still counts, which is the case the gap must
  include), `DMAMEM` state, one row per 16 opportunities, each row carrying the **max gap since the previous row**, `DIAG,win_gap,` Tier-A through the
  production predicate (`isTierATextLine` — a single-TU literal, not one instantiated in every including TU). **The RAM1 blocker resolved without re-basing:**
  the first attempt was refused (`variables 100,288` vs 96,192, +4,096 B) because the DTCM `.data`/`.rodata` section sat within a few bytes of an `ALIGN` page
  boundary (removing one literal restored the line; moving it to a single TU did not); a **concurrent logging/telemetry workstream on this branch then freed
  ITCM** (padding 888 → 3,128) and the unchanged instrumentation links at **exactly the approved `variables 96,192`**, with **2,288 B** above the floor instead of
  48 B. Verified: `test_runtime_working_set_ownership` **76/76**, `test_capture_ring` **16/16**, full native **2715 / 2708 / 7 / 0**, build SUCCESS; mutations
  re-proved on the landed code (**M101** `Expected 5000 Was 1000`, **M102** `Expected 5000 Was 91400`, **M103** `Expected 1 Was 2`) with byte-identical restores.
  **Unblocks the measurement:** next is the HITL leg with the **stimulus's own targets** (`track 8`, slots 4–5) to collect the `DIAG,win_gap` series, then the unit
  is measured, the `H × K` contention envelope declared, and the derived admission re-based and enabled — in that order. Detail: [`evidence/2026-10-03-s310k-gap-sampler-ram1-blocked/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-03-s310k-gap-sampler-ram1-blocked/EVIDENCE.md).*

  *Increment 3.10 (increment 12) landed 2026-10-03 — **the service-gap unit measured on the device, with the first instrument's limit recorded.** The baseline
  stimulus with its own targets (`--track 8 --loop-slot 4`, `--build-upload`) **PASSED**, pinned by `metadata.json` to `git_commit a545def42d26…` /
  `firmware_hex_sha256 531259700436…` — the firmware carrying the sampler. 34 `DIAG,win_gap` rows over **528 lane opportunities**: the **phase advance per lane
  opportunity** (the unit the admission is declared in) measures **median 3.25 ticks, p90 3.5** (≈ 8 458 µs apart), against a **declared 768** (one bar) — the
  declaration is two orders too conservative for the steady state, which is why it refuses work the lane could service. The per-row **max** spans 0…9 367
  (mean 279.5); the large outliers are almost certainly **transport-state transitions or loop wraps rather than service gaps** (the capture spans
  record/overdub/stop legs and a stopped transport advances no ticks). **That exposes the instrument's own limit, recorded rather than averaged away: the row
  carries no transport state, so the typical figure is sound but the worst case — the one a bound needs — cannot be read off it.** Next: carry the
  transport/clock state in the row (or sample only while running), re-run the leg, and only then declare the supported `H × K` contention envelope and re-derive
  and enable the admission. **No bound is claimed; the derived admission stays disabled.** Detail: [`evidence/2026-10-03-s310l-gap-measured/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-03-s310l-gap-measured/EVIDENCE.md).*

  *Increment 3.10 (increment 13) landed 2026-10-03 — **the row carries the transport state; the unit is confirmed and one ambiguity remains.** The lane turn now
  takes `transportRunning` from its caller (`clockManager.isClockRunning()`), accumulates a delta only between two **running** samples, and the row reports the
  flag; the fixture asserts that a stop/restart jump is not a service gap and that a stopped interval accumulates none (76/76, approved RAM1 line held). **Run 2
  PASSED** (`git_commit 6913a7205332…`, `firmware_hex_sha256 1d7277fe868b…`): 22 rows, **18 running / 4 not-running**, with the running rows' `maxDeltaTicks`
  min 0 / mean **515.4** / max **4 337** and a **median 2.88 ticks per lane opportunity** (≈ 7 416 µs apart) — the steady state confirmed at roughly **265× below
  the declared one bar (768)**. **Still not a bound, for a named reason:** the max is either a genuine stall or a **tick reset misread as a loop wrap** (the sampler
  treats a *decrease* in the phase as a wrap — right for a loop-relative phase, wrong for a counter reset — and the row carries no discriminator). **Next:** carry
  the loop-relative phase with an explicit epoch/reset marker (or the clock-source state), then declare the `H × K` contention envelope and re-base and enable the
  derived admission. **No bound is claimed; the derived admission stays disabled.** Detail: [`evidence/2026-10-03-s310l-gap-measured/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-03-s310l-gap-measured/EVIDENCE.md).*

  *Increment 3.10 (increments 13-14) landed 2026-10-03 - **resets excluded and reported, the gate moved to the transport, and the worst observed advance identified as a clock batch.** `getCurrentTick()` is monotonic and set to 0 on transport start/re-base, so a *decrease* is a **reset, not a wrap**: resets are now **excluded from the max and counted in the row**, and the fixture proves a reset neither inflates the gap nor goes unreported (76/76, approved RAM1 line, 2 288 B headroom). **Run 3 refuted the reset hypothesis** (0 resets) and exposed that the gate was `isClockRunning()` - the *clock*: a running clock with a **stopped transport** still advances the tick while nothing needs service, which is what the earlier maxima really were. **Run 4** gates on `isTransportRunning()`: **PASS**, 25 rows, **21 running / 4 not-running**, running maxDelta min 0 / mean 459 / **max 4 355**, 0 resets, **median 2.88 ticks per lane opportunity** (~7 475 us apart, ~385 ticks/s). **The finding cuts both ways:** the typical gap is ~265x *smaller* than the declared 768, while the worst observed single-step advance is ~5.7x *larger* - and the mechanism is **clock batching** (`currentTick += Config::TICKS_PER_CLOCK`; the design itself records that batched pulses compress the effective deadline), which is real playhead travel and precisely what the constant exists to bound, so **one constant cannot serve both roles**. Next: a longer leg that deliberately exercises batching, then declare the `H x K` envelope on a measured maximum and re-base and enable. **No bound is claimed; the derived admission stays disabled.** Detail: [`evidence/2026-10-03-s310l-gap-measured/EVIDENCE.md`](evidence/2026-10-03-s310l-gap-measured/EVIDENCE.md).*

  *Increment 3.10 (increment 15) landed 2026-10-03 - **run 5 (the `accepted_sequence` preset, three legs) supplies 84 running samples and falsifies the constant's safety argument.** With the stimulus's own targets (`--track 8 --loop-slot 4 --slot-b 5 --midi-channel 8`; the leg injects on the track's own channel) and firmware `44bdec575d24...` at HEAD `c49e267d08...`: **84 rows, all running** (the transport gate held across three legs), running `maxDelta` **p50 4, p90 8, max 2 138**, a single advance above 100 ticks, **3 resets reported** (so resets occur and are now excluded and counted), and **median 2.94 ticks per lane opportunity** (~7 574 us apart). So the **typical** gap is 100-190x *below* the declared 768, while the **worst observed** (2 138 here, 4 355 in run 4) is 2.8x-5.7x *above* it. **That falsifies the argument the constant rests on** - `playbackViewCoverageContract` records that an unmeasured advance "can only refuse work, never admit too much", which holds **only if the declared value is an upper bound**; measured, it is not, so the admission can compute a required lead that is too small and **admit work it cannot complete**, the unsafe direction the comment rules out. **Consequence:** the derived admission must not be enabled on 768, and a single larger constant cannot fix it either (the typical case needs ~4, the worst case >=4 355); the admission needs two figures, or the batching must be excluded from the tick it judges (e.g. by bounding the per-turn tick advance). Still not claimed: that 4 355 is the worst case - the deliberate external-clock/burst leg remains unreachable, because no managed scenario drives a host clock. Detail: [`evidence/2026-10-03-s310l-gap-measured/EVIDENCE.md`](evidence/2026-10-03-s310l-gap-measured/EVIDENCE.md).*

  *Increment 3.10 (increment 16) landed 2026-10-03 - **the host clock is reachable from the managed harness, the 2x control, and the correction of increment 15's attribution.** Two corrections first, both to my own records. (1) **The mechanism was wrong:** increment 15 named **clock batching**, but `ClockManager::updateInternalClock()` - the only path while the clock source is INTERNAL, which is what run 5 ran - advances the tick by `currentTick++`, **one tick per IntervalTimer callback**, so a bulk jump there is not constructible; the `+= TICKS_PER_CLOCK` path is the *external* one, and run 5's `DIAG,clk_batch` read **0,0**. The 2 138-tick step is therefore **lane-service starvation under a running transport**, not a compressed tick. (2) **The leg was not blocked:** `runner.py` parses with `parse_known_args` and `scenarios/base.py` merges via `merge_legacy_cli_args` (*"user flags override via last-wins"*), so `--clock-source host --tempo-bpm <n>` reach the record baseline **through the authoritative entry point**, with **no temporary stimulus variant** - the "unreachable" note is retracted. **Run 6** (`--preset base --track 8 --loop-slot 4 --midi-channel 8 --clock-source host --tempo-bpm 240 --build-upload`): **PASS**, `git_commit 887ba773fe2c…` / `firmware_hex_sha256 44bdec575d24…` - **byte-identical firmware to run 5**, so the runs differ only in clock regime and flow. **The device adopted the host clock, measured:** `service_period` max **10 416 us** = `microsPerTick x TICKS_PER_CLOCK` at 240 BPM = **1.0 s/bar**. **Batching exists - and is bounded under the declaration:** `clk_batch` **0,0** (internal) vs **max 42 pulses / 480 batches** (host), i.e. 42 x 8 = **336 ticks = 44 % of the declared 768**, so batching alone never violated it. **The 2x control:** per-opportunity advance **2.9 -> 5.5 ticks (exactly 2x)** while the rotation stayed **7 475 -> 7 309 us** - so **the rotation is tempo-invariant and the tick-denominated constant is not** (768 ticks = 2.0 s at 120 BPM, **1.0 s at 240 BPM**). **The new maximum:** running rows `maxDelta` 0 / p50 **8** / p75 296 / p90 2 400 / max **8 520**, with **4 of 17 rows above 768** (vs 1 of 84 in run 5); one row consumed its whole interval in a **single** 7 784-tick advance (delta-micros 10 262 508 = **10.1 s**) against a nominal rotation of 7.3 ms. **Wall-clock starvation 5.6-11.3 s across runs, against a declared allowance of 2.0 s (120 BPM) / 1.0 s (240 BPM)** - exceeded by **2.8x-11.1x**. **It is not one blocking call:** the largest single spans are `load_frame` 501 ms, `idle_maint` 126 ms, `persist_save` 45-121 ms, ~20x smaller than the gap, so the gap is **cumulative starvation**. **Bounds stated:** the instrumented build (heap-walk dumps every 1 s, per-tick rows) makes the figure an **upper bound under instrumentation**, not a production figure; a mid-interval stop is **invisible** to the sampler (the delta needs two running samples, so `resets = 0` proves only that none occurred between two running samples); five runs, two flows, two tempos. **Consequence, stated not applied:** the split is **not** "typical + batched" (batching is bounded under the declaration) but **per-turn scheduling advance (boundable, measured near 8 ticks at the floor, ~2.9-5.5 per opportunity in the steady state)** versus **blocking/starvation episodes (seconds-scale, belonging to the lag / one-pass-late machinery, 3.6b, not to a per-turn constant)**. Enabling `applyWindowCostToContract` on the declared value stays **wrong in the unsafe direction**; the derived admission **stays disabled** and **no `H x K` envelope is declared** (the measured maximum is a starvation figure under instrumentation, not a scheduling bound). Measurement-only increment: no production line changed, so no native suite run and no RAM1 movement. Detail: [`evidence/2026-10-03-s310m-host-clock-starvation/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-03-s310m-host-clock-starvation/EVIDENCE.md).*

  *Increment 3.10 (increment 17) landed 2026-10-03 - **the service-gap declaration is corrected against its own measurement, and pinned so a larger constant cannot hide the gap.** The runs left a choice (increment 15): split the constant into "typical + batched worst step", or exclude batching from the tick the admission judges. **Run 6 refutes the premise of both** - `clk_batch` is bounded at **336 ticks = 44 % of the declared 768**, so batching is *inside* the declaration and cannot have produced the violation; the quantity that exceeds it is **starvation**, a run of *lost opportunities*, which is not a per-turn advance at all and therefore not a term any per-turn constant can hold. So the per-turn field keeps its role (an upper bound on the **scheduling** advance, measured 2.9-5.5 ticks per opportunity, ~140-265x to spare), the starvation component stays the **lag rule's** (3.6b's `one_pass_late`/`lag_breach`), and **enlarging the constant towards the starvation figure is the wrong repair** - which is now a fixture rather than a comment. **Two false claims corrected at the source:** (A) `playbackViewCoverageContract()`'s *"an unmeasured value can only refuse work, never admit too much"*, falsified because the starvation advance is 2.8x-11.1x the declaration, so an admitted window **can** be starved past its handoff; and (B) the same comment's claim that `maximumConsecutiveDeniedTurns` is *"the rotation, `kDeferredIdleOwnerCount - 1`"* - the very conflation `evidence/2026-10-02-s310h-.../` records as an error, since the rotation is **already inside** the per-turn field by that field's unit. Measured against it: `kDeferredIdleOwnerCount = 7` so the claim reads **6** while the code passes **3**; the field is the **within-lane** contention allowance (`H` tracks x `K` sibling slots, `main.cpp`'s offer round-robin), so the *comment* is corrected and the value is left alone (it is an allowance, not a proved bound - `H x K` remains this task's work). The header field docs now carry the measured figures and the same two-role reading. **Fixture** `test_window_owner_service_gap_declaration_is_bounded_by_measurement_not_by_its_own_claim`, four pins, each red-proved with its exact message: pin 1 (declared >= measured steady state 8 ticks) by **M104** (768 -> 7); pin 2 (declared < the measured starvation episode 2 138) by **M105** (768 -> 8 520) - *"a starved window's miss is the lag rule's, not this constant's"*; pin 3 (the allowance's wall-clock meaning is tempo-dependent: 2 000 ms at 120 BPM, 1 000 ms at 240 BPM) by **M107** (768 -> 1 536, chosen to satisfy pins 1 and 2 so pin 3 is isolated - **M105 aborts on pin 2, so without M107 pin 3 would be an assertion never shown to fail**); pin 4 (the turn-count factor is not the rotation) by **M106** (3 -> 6). One assertion was **removed** for the same reason - the halving relation cannot fail while both absolutes hold, so it is a comment, not evidence. All restores byte-identical (`TrackPlaybackWindowBuild.cpp` sha256 `b93fec3e...5970bd`). Verified: `test_runtime_working_set_ownership` **77/77** (+1 fixture), full native **2716 / 2709 / 7 / 0**, build SUCCESS (33.6 s, a partial rebuild because the header changed), RAM1 line **byte-identical** (`variables 96,192 / code 390,088 + padding 3,128`, 2 288 B headroom), **no re-base**. **Task 3.10 stays open** on the `H x K` declaration, the wiring, and the systematic sweep; **the derived admission stays disabled** and **no fixture was re-based to it**, since re-basing would encode the derived counts as the declaration without the envelope that gives them meaning. Detail: [`evidence/2026-10-03-s310n-declaration-corrected/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-03-s310n-declaration-corrected/EVIDENCE.md).*

  *Increment 3.10 (increment 18) landed 2026-10-03 - **review step 1: the work of each request is bounded and retained.** `applyWindowCostToContract` (increment 8's single source of truth) had **no caller**, the admission still judged the lead with the **declared** counts, and nothing bounded the *retained* window - so the cost of an admitted request was kept nowhere. Now: (1) the helper is the contract's own **`CoverageServiceContract::deriveGrainsFrom(windowEventCount)`** (declared in `RuntimePlaybackViewBuild.h`, defined where the quanta live), setting **only** the three grain fields so *deriving cost is not the same act as changing what may be admitted*; (2) a gather beyond `RuntimePlaybackViewConfig::kWindowAdmissionLimit` (which **is** `kEventsPerView`, `static_assert`ed as the admission limit, DEC-108 §14) is refused by a **new named outcome** `PlaybackViewRequestResult::WindowExceedsRetainedCapacity` - **refused, never truncated**; (3) the bounded work is **retained on the runtime** as `PlaybackViewAdmittedWork {sourceEntries, retainedEventBound, fillGrains, orderGrains, promotionGrains}`, read through `activeViewBuildWork()`, which **returns null unless the build is active** (a finished build owes nothing) - the record step 2's per-track budget needs. **No capacity change:** the lead judgement still uses the declared counts, so nothing admitted before is refused (the lead-side derivation is review step 4 and needs a measured `Delta-max` and `H x K` first). **Two defects the fixtures found, both mine:** the cost model **under-cut an empty window by one fill grain** (`ceil(0/quantum)` is 0, but the fill grain is what establishes the emptiness - measured: an empty window spends **one fill grain** plus its promotion), so `deriveGrainsFrom` now floors the fill count at one and the source comment that claimed otherwise is corrected; and my first fixture asserted an empty gather would be **refused**, where the run showed the empty interval is a legitimate Ready window that is **admitted** and owes its promotion - the fixture now asserts that. **A coupling defect of my own:** the first draft drove the **lane turn**, which samples the **process-wide** service-gap series at its top, so it perturbed the gap fixture later in the same binary; the fixture now drives the runtime's own grain primitives with the turn's own quanta (same work, no side effect - the gap fixture went from failing to passing on that change alone). **Fixtures:** `test_window_request_work_is_bounded_and_retained_across_source_shapes` (empty/sparse/dense/filtered: the record names the inspected entries and the retained bound, its grain counts equal the contract's own derivation rather than the declaration, the build finishes, **actual grains never exceed the calculated cost**, and the record is unreadable once finished) and `test_window_request_beyond_the_retained_capacity_is_refused_by_name_and_truncates_nothing`. **Red-proofs** (guarded replace, unique anchor asserted, byte-identical restores `TrackPlaybackWindowBuild.cpp 4a6a48df...` / `TrackPlaybackRuntime.h ed39edf7...`): **M108** capacity bound dropped - *"a gather beyond the retained capacity is refused by name, not truncated"*; **M109** record filled from the declared counts - *"an empty window costs no order grain"*; **M110** record not gated on the build - *"a finished build retains no readable work record"* and *"nothing is retained for a refused request"*; **M111** empty-window fill floor removed - *"an empty window still costs its one fill grain"* (the defect, now pinned). **Verified:** focused **79/79** (+2), full native **2718 / 2711 / 7 / 0**, build **SUCCESS**, RAM1 line **byte-identical** (no RAM1 re-base). **One recorded size re-base, measured:** `sizeof(LoopPlaybackRuntime)` **6 624 -> 6 640** (+16 B target, `extern`-storage per-slot state: no RAM1 `variables`, no DTCM), with the **host** pin **unchanged at 6 544** (its own untouched `static_assert` is the evidence). **Task 3.10 stays open** on review step 2 (the per-track outstanding-work budget and the every-admitted-window feasibility check), step 3 (the lane dispatch boundary), the `H x K` envelope and the systematic sweep; the derived admission stays **disabled**. Detail: [`evidence/2026-10-03-s310o-request-work-bounded/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-03-s310o-request-work-bounded/EVIDENCE.md).*

  *Increment 3.10 (increment 20) landed 2026-10-03 - **review step 2: the per-track outstanding-work budget.** The bound is **implemented and proved, and its enforcement is deliberately gated off** - with `Delta-max` unchosen, enforcing it now would reduce capacity, which the reviewer's ordering forbids at this step, and that is **measured, not asserted** (M115 below). Landed: **`Track::playbackViewOutstandingGrains(excludingSlot)`** sums every active build's **full** admitted grains on the track (full, not remaining, is deliberate: an upper bound on what is still owed, charging a served grain again - the safe direction; a tighter figure needs the build's remaining-grain count, which is not observable today); **`Track::playbackViewAdmissionKeepsAdmittedDeadlines(contract, currentPhase, loopLengthTicks, candidateGrains)`** requires that **every already-admitted window on the track** can still reach its own deadline once the candidate *and* all sibling work is charged against it - the reviewer's deliberate EDF-free *charge all sibling work* rule, evaluated through a per-slot copy of the contract so the arithmetic stays the contract's own `canCompleteBeforeHandoff`; the **deadline retained** as `PlaybackViewAdmittedWork::handoffPhase`, written where the build begins, so a later request is checked against it rather than only against its own margin; and the **named refusal** `PlaybackViewRequestResult::TrackInterferenceBudgetExceeded`, so a request that would break an existing promise is refused by name and may be re-issued when the interference clears - never silently dropped, and no existing deadline is traded away. **Why the gate:** with the declared one bar per turn, one sibling's charge already exceeds a 40-bar loop by more than an order of magnitude (fixture pin (C)), and **M115 flips the gate on and three existing fixtures fail** with `TrackInterferenceBudgetExceeded` (`Expected 0 Was 11`) at their "the fixture's loop must be long enough for the declared lead" assertion - the gate is load-bearing, and enforcing it now *is* the capacity reduction the review says not to start with; step 4's formula (`Delta-max x H x bounded per-track work`) turns it on. **Fixture** `test_window_track_budget_protects_an_older_windows_admitted_deadline_from_a_sibling`: two windows on one track, both requested **just past** their interval's start so their deadline is nearly a full pass away - the available lead is the distance to the handoff, and a *near* deadline binds long before any charge does, which is what my first two attempts got wrong (recorded) - asserting the exclusion/whole-track sums, both retained deadlines, that the same candidate charge **fits the loop the windows were admitted on and does not fit a 40-bar loop** (the bound refuses capacity, not siblings), and the measured inequality behind the gate. **Red-proofs** (guarded replace, unique anchor, byte-identical restores `TrackPlaybackWindowBuild.cpp 826bc01a45d1e794`): **M112** budget charges no siblings - *"the budget excluding slot 0 is slot 1's outstanding work"*; **M113** deadline not retained - *"the admitted window retains its deadline"*; **M114** budget does not exclude the named slot - the sum re-counts it; **M115** gate forced on - three existing fixtures fail by name. **Verified:** focused **80/80** (+1 fixture), full native **2719 / 2 712 succeeded / 7 skipped / 0 failed**, build **SUCCESS**, RAM1 line **byte-identical** (no RAM1 re-base), and one measured size re-base `sizeof(LoopPlaybackRuntime)` **6 640 -> 6 648** on the target with the **host** pin **still 6 544** (its untouched `static_assert` is the evidence; the prose above that pin is now stale in its explanation though its claim holds - recorded). **Task 3.10 stays open** on enforcement (awaiting `Delta-max`), the reviewer's **coalescing** rule (no pending-request store exists: the budget refuses by name and the caller may re-issue, which keeps the obligation without erasing a deadline, but it is not the same mechanism), step 3's instrumentation and device leg, the `H x K` envelope and the systematic sweep. Detail: [`evidence/2026-10-03-s310q-track-interference-budget/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-03-s310q-track-interference-budget/EVIDENCE.md).*

  *Increment 3.10 (increment 21) landed 2026-10-03 - **the deadline rollover fixed, and the enforced admission path tested** (the three review points on increment 20). **(1) A retained phase is not a retained deadline.** `handoffLeadTicks` measures **forward** distance, so the moment the playhead went past the deadline it wrapped and reported a nearly full pass of available lead - the predicate would have **silently re-granted a passed deadline another whole pass**. The repair retains the **intended traversal** (`admittedPhase`, `admittedLeadTicks`) and closes it arithmetically: the forward distance from the admission phase is inside the admitted lead exactly while the traversal is **open**, and wraps past it once the deadline is behind; a closed traversal is refused *before* the contract arithmetic can flatter it, so the requirement can only ever refuse. The boundary is **exact** - one tick past the deadline closes it (fixture pint (D)). **(2) A refusal retains no obligation** - the over-claim is **withdrawn rather than papered over**: `TrackInterferenceBudgetExceeded` is a *named refusal*, and becomes a promise only when a caller actually keeps the pending request and its original deadline; no such caller or pending-request store exists, so that half is **not implemented and not claimed**. **(3) The enforced path is tested now**, with a controlled timing contract: the switch is no longer a compile-time gate but a **runtime** flag (`sInterferenceBudgetEnforced`, `DMAMEM` on the target, plain on the host, like the service-gap sampler's state, so a scalar never moves the approved RAM1 line), and `test_window_admission_refuses_by_name_once_the_interference_budget_is_enforced` turns it on, drives the **real** admission and gets the **named refusal** (`Expected 11` in the mutation proof), while with the switch off the *same request* is admitted - which is what makes the refusal attributable to the budget rather than to another guard. Production activation stays gated until the timing bound is supported; the switch exists so the enforced path is exercised, not so it can be quietly believed. **Charging full admitted grains** stands as the conservative substitute for remaining work, with saturating sums and neither an omitted nor a double-counted candidate (each existing window is charged *its own* grains from its retained record plus all other outstanding work, the candidate included - asserted, including the exclusion). **Red-proofs** (guarded replace, byte-identical restores `TrackPlaybackWindowBuild.cpp ddd83a0b...`): **M116** traversal closure removed - *"once the playhead is past the deadline the traversal is closed, so no extra pass is granted"*; **M118** traversal lead not retained - *"on the loop both windows were admitted on, the same charge still fits"*; **M119** budget check removed from the admission - *"Expected 11 Was 0. with the budget enforced the real admission refuses by name, charging the sibling"*. **Verified:** focused **82/82**, full native **2720 / 2 713 succeeded / 7 skipped / 0 failed**, build **SUCCESS**, RAM1 line **byte-identical** (no RAM1 re-base - the flag is `DMAMEM`), plus one measured size re-base for the retained traversal. **Still owed:** enforcement activation (awaiting `Delta-max`), a caller that retains a pending obligation, step 3's instrumentation and device leg, the `H x K` envelope, the systematic sweep. Detail: [`evidence/2026-10-03-s310r-deadline-traversal-enforcement/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-03-s310r-deadline-traversal-enforcement/EVIDENCE.md).*

**Task 3.10 — absolute deadline identity correction (native verified, activation still gated).**
The prior `admittedPhase + admittedLeadTicks` guard could reopen after a complete
unobserved lap. Requests now accept an explicit monotonic 64-bit tick and nonzero
clock epoch, retaining an absolute deadline and the window's own loop length.
Expiry, reset epoch, backward time, missing clock identity, changed geometry and
overflow fail closed under enforced admission. Production clock-stamp wiring,
pending-request preservation and the service-gap envelope remain owed; enforcement
stays off. Focused 83/83; full native 2,722 / 2,715 passed / 7 skipped / 0 failed;
capture build SUCCESS with RAM1 unchanged. Target runtime measured 6,656 -> 6,680 B;
host stays 6,544 B. No upload. Prior increment 21's 82/82 claim was a count error:
its commit registers 81 tests. Evidence: [deadline clock identity](evidence/deadline-clock-identity/EXECUTION.md).

**Task 3.10 — timing-envelope qualification continuation: measurement repaired, NOT QUALIFIED.**
ClockManager now supplies a wrap-safe 64-bit tick/reset epoch, with PRIMASK-preserving
atomic access; the sampler observes every lane dispatch (including idle turns),
separates eligibility/progress/denial and is explicitly initialized despite NOLOAD
DMAMEM. The gated admission now charges derived candidate work and eight-track
rotation. Four mutation checks discriminate. Full native 2,727 / 2,720 passed /
7 skipped / zero failures; capture build holds RAM1 variables 96,192, padding 2,808
(floor 840 unchanged). Uploaded final image 7c2a1377...: 300 BPM baseline PASS;
internal-clock baseline and its fresh-boot retry are INCONCLUSIVE_CAPTURE with
`persistence_active_snapshot_unavailable_during_save`. Earlier sampler "starvation"
attributions are withdrawn: selected-worker gaps could include no-work time.
No live committed-window requester, retained retry/projection revalidation or
permitted-gap/overrun enforcement exists yet, and the baseline exercises shared
record/canonical builders. Thus task 3.10 and timing qualification remain open;
activation remains gated, no capacity reduced, no RAM1 rebase. Landed as `4a7cef70`
(increments 22-23, jointly with the absolute-deadline slice).
Evidence: [timing-envelope qualification](evidence/timing-envelope-qualification/EXECUTION.md).

**Task 3.10 (increment 24) — the retained pending preparation request (DEC-108 §13(c); native verified, O8 deferred).**
Both landed packets owed the same thing, and it is now in: **a refused request is a promise, not a dropped request.**
`PlaybackViewPendingRequest` + `PlaybackViewPendingReason` record the demand per slot on `LoopPlaybackRuntime` —
the identity, the interval, the **absolute deadline with its epoch and geometry**, and the bounded cost an admitted
window would carry — written by the admission that refuses it (`BuildInFlight`, `SuccessorAlreadyPrepared`,
`TrackInterferenceBudgetExceeded`, `CannotCompleteBeforeHandoff`), cleared by the admission that accepts it, and read
through **generation-qualified** accessors so returning to a slot never revives an earlier operation (DEC-108 §13(d)).
Coalescing keeps the **oldest outstanding deadline** as one coherent position (deadline, epoch, geometry, origin phase
and origin stamp together) while the content follows the newest attempt (DEC-108 §13(a)); an epoch or geometry change
**voids the position and never the obligation** (§13(c)); and a pending demand is deliberately **not** outstanding work,
so the per-track budget does not charge it. **Five fixtures** prove O1/O2/O3/O4/O5/O6/O7/O9 (+O10 by the build), **seven
mutations** (M120–M126) fail their named assertions and were restored byte-identically
(`TrackPlaybackWindowBuild.cpp e98595129187ceee`, `TrackPlaybackRuntime.cpp 35eec9683a0fdfd2`,
`TrackPlaybackRuntime.h 2e50b234a0cce56c`). Verified: focused **89/89** (was 84), full native **2,732 / 2,725 succeeded /
7 skipped / 0 failed**, `pio run -e teensy41-capture-serial` **SUCCESS** with the approved RAM1 line held
(`variables 96,192` unchanged; code 390,408 -> 390,472, padding 2,808 -> 2,744, 1,904 B above the floor), measured size
pins (target 6,680 -> **6,792**, host **8,624 -> 8,744** — the host pin was unreachable dead code carrying a stale
6,544 and is live from this slice), `git diff --check` clean, `openspec validate --strict` valid. Image
`89e4d0c1…` built and **not uploaded**; no device claim. **O8 — the re-attempt that would serve the retained demand on
its own lane turn — is deferred to increment 25 and is not claimed**: re-issuing the request needs a gather buffer that
outlives the build and the lane turn owns none, which is a RAM-shape decision. **Task 3.10 stays open** on O8, a
production requester (qualification blocker 1), the permitted service-gap/overrun policy (blocker 3), enforcement
activation (awaits `Delta-max`), `H x K` and the sweep. Detail:
[`evidence/2026-10-04-s310s-pending-request-retained/EXECUTION.md`](evidence/2026-10-04-s310s-pending-request-retained/EXECUTION.md).

**Task 3.10 (increment 25) — the review's step 4: the declared service envelope (native verified; activation gated on the callers' stamps).**
The review's formula `Delta-max x H x bounded per-track work` now has *declared* terms instead of placeholders: the
per-turn advance is the permitted **scheduling** bound **16 ticks** (2x the measured p50 of 8, ~2.3x the measured 7.3 ms
rotation at the fastest declared tempo — derived from the schema-2 lane measurement, not chosen for comfort), the denial
allowance is the across-track rotation **`Config::NUM_TRACKS - 1`**, the lead is judged on the window's **derived** cost
(`deriveGrainsFrom`) rather than the declared basis counts, and the **overrun policy is named** where the terms live (the
lag rule: one-pass-late served and reported, the second consecutive miss latches the named fault, never silently
re-granted). One new fixture pins the terms, their derivation, the derived-versus-basis accounting and the capability
statement; seven recorded re-bases moved fixture shapes **inside** the declared envelope or re-pointed a pin at the new
declared value, and none hides a capacity change — the declaration *is* the capacity statement. **Activation was
attempted and measured, and is not flipped:** enforced admission fails closed on an unqualified clock stamp (increment
22's rule, DEC-108 §13c), so with the switch on **twelve fixtures that prepare an unqualified window are refused by
name** — correct behaviour for an unqualified demand, and unsupported as a production state because the MIDI edge, the
slot-preparation path and the preview lane **do not pass stamps yet**. Activation therefore needs the **clock-owner
wiring**, not a better constant; the switch stays off and the enforced path stays exercised by the fixtures that turn it
on. Verified: focused **90/90** (+1), full native **2,733 / 2,726 succeeded / 7 skipped / 0 failed**, build **SUCCESS**
with the approved RAM1 line held (`variables 96,192`, no size-pin movement), `git diff --check` clean, `openspec validate
--strict` valid; **M127** (per-turn term back to the placeholder bar) fails the envelope fixture (`Expected 16 Was 768`)
and 23 fixtures in total, **M128** (lead judged on the basis counts) and **M129** (denial allowance back to 3) each fail
their named assertions, all restored byte-identically (`Track.h 5c4a5b0121796179`,
`TrackPlaybackWindowBuild.cpp f653a2d6559ac744`). No upload, no device claim. **Owed:** activation (the clock wiring),
increment 24's re-attempt, a production requester, the owner's confirmation of the declared Δmax, and the `H x K` sweep
+ device leg. Detail:
[`evidence/2026-10-04-s310t-declared-envelope/EXECUTION.md`](evidence/2026-10-04-s310t-declared-envelope/EXECUTION.md).


**Task 3.10 (increment 25b) — the declared `Delta-max` set from the device leg (the owner asked for proof first; it falsified the provisional value).**
The provisional 16 (2x the measured p50) was measured on the card and **falsified**: two managed baseline runs of the
authoritative entry point (canonical `STIM-BASE-2-2-01@v1`, track 8 slots 4/5) give the **scheduling** maximum —
`eligiblePredecessorMax`, the gap across a turn whose predecessor was *eligible*, which the schema-2 sampler separates
from idle intervals — as **17 ticks** on the internal clock (p50 0 / p90 9 / p99 15, 68 352 opportunities) and **48
ticks** at the fastest declared tempo, 300 BPM (p50 8 / p90 16 / p99 48, 5 888 opportunities): ~2.8x, as a
tick-denominated unit implies, and 50 ms of wall-clock. Two internal rows and six 300-BPM rows exceeded 16, so the
declaration moves to **64 ticks** — at or above both measured maxima with a stated margin (1.33x the worst, 3.8x the
internal one) — and the envelope fixture's pin was replaced with *measured* pins (declared >= 48, <= 4/3 of it, >= 17).
**Starvation did not appear in either leg** (0 saturated gaps; the largest all-dispatch gap, 366, had an *ineligible*
predecessor, i.e. it was idle time), and it remains the lag rule's: a bound on one turn cannot hold a run of *lost*
turns. Image honesty: both legs ran the **flashed** image `7c2a1377…` (the tree at `4a7cef70`), not this incremement's
tree — the measured path (lane dispatcher, sampler, row emitter) is byte-identical in both, verified by diff, no
production path requests a window, and **no upload was performed**. Leg A's baseline verdict was FAIL on the large-loop
bounds verifier's 100 ms slice declaration (max 104 640 us, a pre-existing verdict issue on that image) and leg B's was
PASS; the lane measurement does not depend on either. Capability consequence: one 63-entry window charged with a
same-track sibling needs ~14.3 bars of lead under this declaration (4.3 bars at 16; 161 bars under the placeholder).
Re-bases recorded: the mirroring constant 16 -> 64, the service-gap wall-clock absolutes 41/20 -> 166/83 ms, the
handoff-phase sites derived per content, the grain-cost sweep's loops 64 -> 128 bars (160-note windows need ~90 bars),
the two-holder fixtures 200 -> 300 bars, the retention fixtures 30 -> 70 bars with 100-note windows, the charge fixture
12/40 bars, the refusals-by-name fixture 70 bars. Verified: focused **90/90**, full native **2,733 / 2,726 succeeded /
7 skipped / 0 failed**, build SUCCESS with the RAM1 line byte-identical, `git diff --check` clean, `openspec validate
--strict` valid; **M127** (the placeholder bar) now fails **19 fixtures**, **M128** and **M129** their named assertions,
all restored byte-identically (`Track.h 3ae6b899699f2692`, `TrackPlaybackWindowBuild.cpp 1ff579cb96e31498`). Evidence:
[`evidence/2026-10-04-s310t-declared-envelope/EXECUTION.md`](evidence/2026-10-04-s310t-declared-envelope/EXECUTION.md)
§ Addendum.

**Task 3.10 (increment 25c) — the declaration re-measured on this increment's own image: it holds.**
Owner-authorized flash of `16a1f931` (hex `bde422e5…`, `git_dirty: false`), then three legs of the authoritative entry
point: internal (INCONCLUSIVE_CAPTURE/EVIDENCE_MISSING, its rows still used), internal fresh-boot (**PASS**), and 300 BPM
(**PASS**). Together with the two legs on the previously flashed image: **300 BPM reproduces 48 ticks on both images**,
internal p90 8-10 / p95 17 across four legs, **no saturated gaps anywhere**, and the only row above 64 in five legs
(0.24%) sits inside a **4 561-node snapshot allocation and publication** — a *blocked* gap by a heavy non-lane owner,
not a scheduling delay. The declaration **stands at 64 ticks** with its basis stated precisely (>= the measured 300-BPM
maximum with a 1.33x margin, >=2.9x the internal p95, and explicitly not a bound on every observed gap). The first
internal leg's `display_empty_after_overdub_stop` **did not reproduce on the retry**, so it is a run artifact, not a
finding — backed by the byte-identical ITCM code figure (390,472), `FLASHMEM` placement of the new code, untouched
`src/main.cpp`/sampler/emitter, and no production window request. No code change was needed. Detail:
[`evidence/2026-10-04-s310t-declared-envelope/EXECUTION.md`](evidence/2026-10-04-s310t-declared-envelope/EXECUTION.md)
§ Addendum 2.

**Task 3.10 (increment 26) — blocker 3, half 1: the permitted service-gap limit is wired into the live lane and its exceedance is named.**
`PlaybackLaneMeasurement` now compares every **eligible-predecessor** gap against the permitted advance (`overruns`,
`overrunMaxTicks`, the limit carried in the block row as **schema 3**), and a block that saw one emits the **named**
`DIAG,win_gap_overrun` row through the usual `#ifndef`-guarded seam. The limit is read from the declaration
(`kPlaybackViewPermittedAdvancePerTurn`), so bound and observation cannot drift; an **idle** gap above the limit is *not*
an overrun (the eligibility split is what separates scheduling from idle time), and neither is an epoch change. Fixture
`test_the_declared_permitted_gap_is_wired_and_its_overrun_is_named` (with one repair of my own recorded: the inter-case
gap was forming its own block) proves B1-B5; **M130-M133** red-prove it and were restored byte-identically
(`PlaybackLaneMeasurement.h 3245e37db9c710b7`, `TrackPlaybackWindowBuild.cpp c94103e134533e2a`). Verified: focused
**91/91**, full native **2,734 / 2,727 succeeded / 7 skipped / 0 failed**, build SUCCESS with the RAM1 line
byte-identical (no size-pin movement), `git diff --check` clean, `openspec validate --strict` valid. **Blocker 3 is half
closed:** the limit is wired and named, but the admitted window's own **deadline** is still not read by the live serving
path (the existing overrun policy is revision-driven and has no production caller), and wiring it needs three pinned
decisions — the episode key, the clock reaching the turn, and what a miss means at completion. Detail:
[`evidence/2026-10-04-s310u-permitted-gap-wired/EXECUTION.md`](evidence/2026-10-04-s310u-permitted-gap-wired/EXECUTION.md).

**Task 3.10 (increment 27) — blocker 3, half 2: the deadline the admission promised is serviced on the live path (a review's six points, adopted).**
The admission's promise (`PlaybackViewAdmittedWork::deadlineTick`/`clockEpoch`) was written by the admission and read by
the budget and the request bookkeeping, and **no serving path read it**; the overrun policy that existed was driven by
revision currency and had no production caller. Now `Track::serviceAdmittedDeadlineMisses(clock)` runs at the **top of
every lane turn** for every slot holding a live promise: each traversal of the promised interval that has passed is
counted **exactly once** (computed from the clock, keyed on the promise's own admission tick), judged only in the epoch
the promise was qualified in, and — on the first miss — named `one_pass_late` through the **one implementation** of what
a miss means (`Track::noteMissedTraversalForSlot`), which `advancePlaybackLagTraversal` now also calls, with the **second
consecutive** miss naming `lag_breach` and setting the latch. The accounting is **unconditional** (only the row is
capture-gated) and **never** routed through the revision resolver, so a late arrival is named even when the served window
already carries the wanted revision; the deadline is never re-positioned and nothing is published or adopted by the miss
path. The promise retains `admittedIntervalLength`, the turn takes the clock (`main.cpp` passes the one it already
holds), and the size pins were re-based on measurement (target 6,792 -> **6,816** B, host 8,744 -> **8,768** B; no RAM1
re-base). Three fixtures and **M134-M139** red-prove the review's requirements R1-R6 (`TrackPlaybackWindowBuild.cpp`
pre-mutation hash **518728238943f77a**), including one defect of mine the fixtures caught (the accounting loop called the
helper twice per newly missed traversal, so the breach appeared at the first miss). Verified: focused **94/94**, full
native **2,737 / 2,730 succeeded / 7 skipped / 0 failed**, build **SUCCESS** with the RAM1 line byte-identical,
`git diff --check` clean, `openspec validate --strict` valid. No upload; production judges nothing (no promise is ever
admitted, and the lane's production builds carry `clockEpoch == 0`), but the **policy is enforcement rather than
telemetry** — R5's own point. Detail:
[`evidence/2026-10-04-s310v-admitted-deadline-serviced/EXECUTION.md`](evidence/2026-10-04-s310v-admitted-deadline-serviced/EXECUTION.md).

**Task 3.10 (increment 28) — blocker 3: a review's three gaps in that service are closed, and containment is proved rather than counted.**
A read-only review of increment 27 (independently verified at `5cbd3bb8`) named three correctness gaps and one unproven
claim, all real. **(1)** `serviceAdmittedDeadlineMisses` reset the lag **episode** on every new `admittedTick`, so a
repeated request for the **same interval** minted a fresh first-miss allowance — the act DEC-108 §13(a) forbids. Request
**deduplication** (the per-promise traversal count) is now separate from **fault history** (the episode, reset only when
the *interval* changes, guarded by `coversInterval`). **(2)** An epoch mismatch `continue`d silently: it now
**revalidates and names** `promise_epoch_invalidated` (once per promise, per-slot `invalidatedPromiseTick`), claims no
traversal miss, leaves the interval's history untouched, and retains the obligation **unmodified** so returning to that
clock judges *that* promise again — an epoch change cannot mint an allowance, and the content still publishes (asserted).
**(3)** A deadline crossed at the turn's final grain escaped, because the turn's clock is read before the worker grain
and `activeViewBuildWork()` is null once the build completes: the promise is now **retained through completion**
(`completedPromisePendingJudgement`, consumed by the next opportunity's fresh clock, cleared by a new admission so a
successor can never be judged as its predecessor). **(4)** The review's closing point — *"accounting plus a latch does not
yet prove late-publication containment"* — is now a fixture: after the breach `containmentForSlot` reads
`ServeLastCoherentRevision` while coverage remains. Three fixtures and **MC1-MC6** red-prove each behaviour (always reset
the episode; skip the naming; reset on the mismatch; drop the retention gate; drop the retention record; never latch on
the second miss) with byte-identical restores (`TrackPlaybackWindowBuild.cpp` **377b5041b44a8f9d** before and after each
case, final **6f4961ef38d9186c**), and the two corrections made while writing them are recorded rather than smoothed (an
absolute `one_pass_late == 0` expectation that its own earlier turn contradicted; a retention bool cleared at admission
instead of a `tick != 0` sentinel, since tick 0 is a legal instant). Verified: focused **97/97**, full native
**2,740 / 2,733 succeeded / 7 skipped / 0 failed**, build **SUCCESS** with the RAM1 line byte-identical and one measured
size re-base (target 6,816 -> **6,832** B, host 8,768 -> **8,784** B), `git diff --check` clean, `openspec validate
--strict` valid. No upload, no device claim. **Operationally still open:** on the device the service judges nothing (no
production requester, `clockEpoch == 0` in production builds, enforcement off), so blocker 3's operational half is the
production requester's qualified clock stamp — the review's verdict stands where it concerns operation. Detail:
[`evidence/2026-10-04-s310w-episode-and-epoch-revalidation/EXECUTION.md`](evidence/2026-10-04-s310w-episode-and-epoch-revalidation/EXECUTION.md).

**Task 3.10 — completion-clock repair: reviewed correctness blockers fixed, timing qualification still open.**
Completion is judged immediately with a fresh ClockManager stamp, not at the next
dispatch. Late/invalidated candidates remain unpublished until explicit boundary
revalidation; their original deadline/history are preserved, and admission cannot
overwrite them. The dispatcher sweeps deadlines independently of worker eligibility.
Cancel/reset cleanup includes withheld previews (review-found defect red-proved).
Focused101/101; full native2,744 / 2,737 passed / 7 skipped / zero failed; capture
build holds variables96,192 and padding2,680, both runtime size pins unchanged.
Three guard mutations fail as intended and restore byte-identically. Uploaded image
dfb21ada3121… passes the registered baseline on authorized track8 slots4/5
(captures/completion-clock-repair-baseline); complete capture, advisory first-note
warning retained. No capacity reduction, timing-limit change, RAM1 rebase or commit.
Live requester/retry, boundary-consumer integration and timing-envelope qualification
remain open; baseline regression is not a committed-window deadline qualification.
Evidence: [completion-clock repair](evidence/completion-clock-repair/EXECUTION.md).

**Task 3.10 — requester retry-safety prerequisite verified; live integration remains open.**
A refused `requestPlaybackWindow` retry could clear the active builder's borrowed
scratch before returning `BuildInFlight` (red proof: 388 events became 0). Busy
builds, held completions and ready successors now refuse before gathering, retaining
the new demand with a conservative maximum-admissible cost until it is resolved.
The first build's source, identity/deadline and published content are preserved.
Focused103/103; full native2,746 / 2,739 passed / 7 skipped / zero failed; guard
mutation fails both new fixtures, restored byte-identically; capture build succeeds
with RAM1 unchanged (variables96,192, padding2,680). Strict OpenSpec and whitespace
checks pass. No upload, no commit, no live activation or timing-envelope claim.
Still required: bounded effective-source cursor/lifetime and retirement, cold view
provisioning, pending-only retry service preserving deadlines, shared-builder
exclusion, paired slot-phase/clock, ahead-of-playhead boundary/closure consumption.
The synchronous allocating gather cannot simply be placed in a deferred turn.
Evidence: [requester/retry integration](evidence/requester-retry-integration/EXECUTION.md).

**Task 3.10 — bounded immutable event-window cursor verified; runtime binding still owed.**
`LoopSnapshot::EventWindowCursor` pins at most three intersecting event-membership
chains for the initial two-bar linear window, not a whole-loop root or event-copy
buffer. Reads charge reference/page/bar work and retain canonical order/event ID;
retirement reuses the snapshot's one-child/emptied-node release primitive. Root
retirement, shared readers, cancellation, reuse, exact endpoints and length-independent
work are native-proved. Worst-case source read/release bound:13,059 units, not a
wall-clock guarantee; fixture120 references/80 output rows costs132 read units at
16/64/512 bars. Focused47/47, final native2,743 passed/7 skipped/zero failed; five
mutations fail and restore byte-identically. One review's endpoint gap fixed.
Capture build succeeds with RAM1 unchanged (variables96,192, padding2,680); strict
OpenSpec and diff checks pass. No upload/commit/live activation. Next: bounded
count/fill/order binding preserving canonical ties, candidate lifecycle/retirement,
and pending-only retry service; phase/clock, boundary and device timing remain open.
Evidence: [bounded source cursor](evidence/requester-retry-integration/SOURCE_CURSOR.md).


### Task 3.10 continuation — runtime fill/order and retained retries (verified, not activation)
- Typed window cursor binding now preserves full canonical/event-ID ties through
  two bounded sorts and rank assignment in existing prewarmed arrays.
- Runtime owns cursor pins; separate freeze/count/admit/fill/order/retire turns,
  pending-only committed recipes and runnable-first offers, original absolute
  deadline transfer, explicit shared-builder exclusion, bounded cancellation and
  preservation of newer demand. Clock-derived active-slot phase; jam/queued refusal.
- Seven new retry fixtures; ownership110/110; full native2,760 total/2,753 passed/
  7 skipped; five retry mutations restored byte-identically. Previous binding
  fixtures193/193 and three ordering/token mutations remain part of this slice.
- One bounded review: source staleness must not invalidate a newer request's clock
  position. Fixed, exact regression passes, reverted condition fails.
- Target runtime7,072 B (+240 external), host9,096 (+312). RAM1 not rebased;
  no upload. See RUNTIME_BINDING.md and retry-verification.json for final build.
- **Task remains open.** No production request/provisioning trigger, qualified
  boundary recovery/adoption or registered committed-window HITL. Existing lane
  dispatches retained recipes correctly in native production-path fixtures; that
  is not an exercised production workload. Pending expiry is named separately
  from admitted completion misses, not a claim of complete recovery policy.
- **Timing NOT QUALIFIED:** empty base build3 grains at H8/gap64 + margin768
  needs2,304 ticks > two-bar1,536; qualification/release add work. Historical
  gaps112/168/149 exceed64 and remain observations, not bounds. No capacity cut,
  fallback removal, gate flip or synthetic device pass.
Evidence: [runtime fill/order and retry](evidence/requester-retry-integration/RUNTIME_BINDING.md).


### Task3.10 — topology-qualified retirement unblocking
Completed cursor count now derives the pinned-page retirement bound in O(1),
including all fixed page slots and empty pages. This replaces the blanket205-turn
surcharge only after qualification; incomplete/rewound cursors keep maximum.
Empty retirement1 turn, nine-page bound156 units exact after root retirement.
Eight-track production composition at64ticks/lane turn: initial eight-bar lead
completes8 empty windows in48 turns; two-bar lead completes0 without regranting.
Source47/47, ownership113/113; full native2,756 passed/7 skipped; two mutations
and capture/RAM1/strict validation pass. No size growth, upload, activation or
fallback removal. Task stays open on qualified launch preparation/binding,
production trigger, steady-state service throughput and registered device proof.
Evidence: [unblocking retirement charge](evidence/requester-retry-integration/UNBLOCKING.md).


### Task 3.10 — production launch readiness continuation (partial, native verified)
Demoted-target **fallback** launch now retries readiness after occupancy, prepares
its own phase-zero interval, accepts successfully gathered empty intervals, checks
source/revision/generation/order/coverage, and does not rebuild on the commit's
first target tick. Focused116/116, full native2,759 passed/7 skipped; three mutations
caught, capture/RAM1 and strict validation pass. No new reservation or size rebase.
**Not closed:** immutable launch recipe/provisioning/boundary adoption, production
committed-window requester, continuous refill and device qualification. The lane
supplies3 turns/window/holder at H8/gap64; dense copying alone needs32. No upload,
fallback removal, timing-constant change or activation. Existing fallback gather
remains content-sized; off-clock placement is not a structural timing guarantee.
[Packet and evidence](evidence/requester-retry-integration/LAUNCH_READINESS.md).


### Task 3.10 — immutable initial launch and real ordering-work reduction
**Immutable initial launch bound; ordering work reduced; continuous two-bar NOT QUALIFIED.**
SlotProvision now reserves only the three rolling views for a cold demoted target
and records an explicitly unpositioned launch recipe. The existing playback lane
counts/fills/orders/retires its pinned source; the completed ready view binds only
at the real phase-zero switch boundary. Empty intervals are valid, stale identity
cannot bind, cancellation retires pins, and capture prewarm upgrades in place.
The first traversal consumes the immutable window without a launch rebuild; expiry
is sticky. Later coverage stays on the mutable fallback. Legacy dense overflow
uses the named `launch_legacy_dense` fallback, without truncation or capacity cut.
A blocked earlier switch no longer hides later tracks; sibling interference waits
without permanently refusing an unpositioned launch. Both review findings red-proved.
Ordering alternates existing arrays: dense sort982 ->555 turns at quantum48,
no extra buffer. Dense snapshot fill35 + order/ranks1,153 =1,188 turns; derived
shared-source lifecycle1,226 (was2,080). This still cannot meet the3 turns/window/
holder supplied at H8/gap64. Continuous refill and device qualification stay open.
Focused ownership122/122, prewarm196/196; full native2,768 passed/7 skipped;
five mutations caught/restored, strict validation and capture build pass. RAM1
variables96,192/code391,512/padding1,704/locals34,880, floor840 unchanged. Existing
runtime size pins still pass (target7,072/host9,096). No upload or commit.
Evidence: `IMMUTABLE_LAUNCH.md`, `ORDER_WORK.md`, `immutable-launch-verification.json`
in `openspec/changes/windowed-playback-generation/evidence/requester-retry-integration/`.

No task3.7 fallback-removal checkoff. Initial launch is a wait-for-ready binding,
not a continuous deadline guarantee; later rolling requests/eligible adoption,
full continuity/closure integration and registered device qualification remain.
No new service constants or supported-density claim. Existing limits preserved.


### Task 3.10 — device rootless-source hold correction
**Device rootless-launch hold fixed and observed on the preserved target.**
Track8 slot5 had24 bars/1,144 committed events but no published root (LIBOBS kp1,
kr0,kc0); the new requester waited forever without requesting its source. Explicit
launch preparation now queues the existing on-demand committed-pass republish only
when no current root/job exists. Bulk boot hydration stays disabled; construction
remains on the existing Snapshot owner, not the clock edge.
Native regression red-proved, focused123/123, full native2,769 passed/7 skipped;
capture build/RAM1 and strict validation pass. Uploaded image5e8a81b1ba9c91e52f566c40a7d4e0101850ea19ccb016f16b80af1878f5723a.
Device: queue at205.973s -> commit at233.897s, target length18,432. Post-run readback
retains1,144 events and now reports kr1/kc1; playback emits channel8 notes and the
24-bar display is populated. Slot5 was preserved; only authorized slot4 was rebuilt
for the retest. Final device state STOPPED, pending0, queue0/4.
Limits: cold source preparation took~27.924s and exceeded the4-bar hold report;
not timely-launch or continuous-two-bar qualification. Unmodified wrapper baseline
PASS did not send the switch. TEMP forced-stop-request variant sent it, but generic
baseline FAIL reflects its injected held note83; content verifier INCONCLUSIVE
(no unique recurring anchor), not a parity pass. Evidence preserved; no commit.
Packet/evidence: `evidence/requester-retry-integration/DEVICE_ROOTLESS_HOLD.md` in
`openspec/changes/windowed-playback-generation/`.

No task3.7 removal or continuous timing checkoff; cold-source latency remains open.


### Task 3.10 — single-pass qualification/fill
**Single-pass private qualification/fill verified; continuous two-bar still NOT QUALIFIED.**
The runtime now uses the existing typed fill while qualifying the pinned source,
writing only its private candidate. Admission preserves the original promise and
starts ordering without rewinding/refilling. Fill is charged zero only after it
has actually completed; capacity overflow remains named and unpublished, and
source-token failure/cancellation preserve the current view while retiring pins.
Dense2048 fixture: one2,178-unit/35-turn source pass +1,153 ordering/rank turns;
full shared-source lifecycle1,191 turns (was1,226). Eight empty initial windows
complete in40 lane turns (was48); two-bar lead still refuses without synthetic
publication or renewed deadlines. Five turns per empty request still exceed the
three turns/window/holder supplied under H8/gap64. No timing/capacity rebase.
Focused320/320; full native2,770 passed/7 skipped; three mutations caught/restored;
capture build, unchanged RAM1/runtime size pins and strict validation pass.
No device action/upload/commit. The~28s rootless cold-source preparation delay is
unchanged and remains a separate blocker. Evidence: `SINGLE_PASS_QUALIFICATION.md`
and `single-pass-verification.json` in the requester-retry evidence directory.

No task checkoff for continuous refill or task3.7 fallback removal. Completing
qualification sooner does not establish a supported sustained service envelope.

## Gated rolling slice closeout (no additional task checkoff)

**Gated rolling request → preparation → validated handoff → emission verified.**
`serviceRollingPlaybackWindow` runs off-clock before offers; the existing bounded
worker prepares the selected current-root interval; `resolveRollingPlaybackWindow`
validates absolute deadline/epoch, generation, geometry, revision, prior serviced
tick, completion and retired pins before adoption. The existing emitter/ledger is
reused; physical view/storage generation guards prevent reuse, and empty windows
preserve held notes. Refused original debts and unrelated newer demands survive.

`PLAYBACK_WINDOW_ROLLING_ENABLED` defaults0; enabling requires native or explicit
capture qualification marking. The ordinary `platformio.ini` is unchanged.
Capacity2048, span1536, fill64/order48, gap64,H8,margin768 remain unchanged. At
phase0, the selector targets [4608,6144): fallback covers [1536,4608) and resumes
on expiry. This proves later-window handoffs, NOT contiguous two-bar service.

Verification: focused329/329; full native2,779 passed/7 skipped/0 failed; three
mutations caught/restored; default and explicit qualification capture builds pass
RAM1 floor840 (padding1,640/984). Runtime metadata +48 external bytes per holder
(target7,120/host9,144), no event-buffer growth. No upload/commit. Native covers
Track caller/worker/emitter; main wiring is build-verified, not native linked.
Continuous timing, device qualification (including masked flash-boundary duration),
old-source reconstruction, preview/undo, full recovery/closures and paged enablement
remain open. Tasks3.10 service qualification and3.7 fallback removal stay unchecked.
Evidence: `ROLLING_HANDOFF.md` and `rolling-handoff-verification.json` in
`openspec/changes/windowed-playback-generation/evidence/requester-retry-integration/`.

**Task 3.10 / 6.3 — the `H x K` contention sweep: the declared envelope measured on the composition (2026-10-05; test-only).**
The envelope `Delta-max x H x bounded per-track work` had each multiplier *derived* and the **composition** never
measured; this slice measures it in the declaration's own currency. `test/test_support/window_contention_sweep.inc`
(three fixtures in the ownership suite) drives the production composition -- per track `serviceAdmittedDeadlineMisses` +
`playbackWindowServiceOffer`, `selectPlaybackHolderRoundRobin` across tracks, one grain through
`stepPlaybackViewMaintenance(phase, nullptr, …)` exactly as `src/main.cpp` calls it -- with the playhead advanced by the
**declared permitted advance** (64 ticks per lane turn = one opportunity), on loop lengths **sized from the declaration's
own arithmetic** over the charge the retained records carry. **Measured law:** `drainTurns = H x (1 + K) x serviceTurns`
and the target completes at `1 + (serviceTurns - 1) x H`; the 20-note grid drains 7/14/28/56/56/112/168 turns for
H,K = 1,0 / 1,1 / 2,1 / 4,1 / 8,0 / 8,1 / 8,2 against 192 promised turns (228 at K=2) with **zero** named misses, so the
declaration's `(denied + 1) == NUM_TRACKS` is slack below the full rotation and tight at it. `K` and `H` are each
**load-bearing by measurement** (the budget and the siblings' records agree, the drain exceeds the target's own turns,
the H ratio is linear); density moves the magnitude (2/7/33 service turns at 5/20/80 notes) and span is not an input
(identical cost at 17 and 41 bars). **A measured correction:** the charge is `serviceTurns + 1` (8 grains vs 7 turns at 20
notes) because the promotion grain is charged while its work happens inside the completing turn and the order **bound** is
skipped when the input is already ordered -- a *sufficient* bound, never an equality. **The boundary is predicted then
measured:** the 80-note/K=1 shape is ready with 0 misses inside the charge (50 bars, 588 promised turns) and crosses its
promise, named `one_pass_late`, just outside it (27 bars, 312), where each individual request still fits its own derived
cost -- i.e. exactly what the **unenforced** admission permits -- with the declaration's own `canCompleteWithinLead`
answering true/false/true for those three questions; the control (the same cell at **2x** the permitted advance) breaks and
names the miss. Four mutations red-prove and are restored byte-identically: **MC1** the sibling charge ignored
(`playbackViewOutstandingGrains` → 0), **MC2** the placeholder denial allowance (3), **MC3** the within-track tie order
(equal deadlines to the last holder), **MC4** the order quantum (48 → 96); the production diffstat after the round equals
the one before it. Verified: focused **135/135** (was 133), full native **2789 / 2782 succeeded / 7 skipped / 0 failed**,
capture build **SUCCESS** with the RAM1 line byte-identical (`variables 96,192` / `padding 1,640`, floor 840, no re-base),
strict validation valid, `git diff --check` clean. **No production line was edited**, no gate flipped, no capacity or
constant moved, no upload, no commit. **Task 3.10 and 6.3 stay open** (device service qualification; chords/same-tick
density, other tempos and the device leg); this slice supplies the contention axis, the measured cost law and the
boundary. Detail:
[`evidence/2026-10-05-s310x-hk-contention-sweep/EVIDENCE.md`](evidence/2026-10-05-s310x-hk-contention-sweep/EVIDENCE.md).

**Task 3.10 — activation audit: the production demand is stamped, and the exempt paths are pinned (2026-10-05; test-only).**
Increment 25 left *"activation … needs the clock-owner wiring"* and measured that *"the MIDI edge, the switch preparation
and the preview lane do not pass stamps yet"*. The audit pins what is true now, with no production line edited: the **only**
production window request is the lane turn's `Track::serviceRollingPlaybackWindow` (reached from `src/main.cpp` with
`clockManager.getPlaybackDeadlineClock()`); an unqualified stamp yields `Excluded` **before** any promise (no rolling
frame, no retained demand), while the stamped call's demand and frame carry the same epoch and absolute deadline;
`requestPlaybackWindow` and `requestPreviewWindow` have **no production caller** (the preview's belongs to 3.13). The
**launch** demand is unpositioned by construction (`clockEpoch == 0`, `deadlineTick == 0`), and a far-future clock names no
`one_pass_late`/`lag_breach`/`promise_epoch_invalidated`/`pending_position_invalidated` row for it — unpositioned is not
late; with `sInterferenceBudgetEnforced` on it still prepares and binds, because the budget governs promises and a launch
makes none. The **capture edge** (`captureIntervalAdmissionForSlot`) takes no clock: it is a capacity answer, not a
promise. So what remains for activation is the **owner's enablement decision plus the device leg (6.5)**, not a missing
stamp. Three mutations red-prove the audit, restored byte-identically (**MA1** the rolling epoch guard dropped → `Excluded`
becomes `Requested`; **MA2** the launch demand given a position; **MA3** the unpositioned guard dropped → a far-future
clock mints an epoch invalidation); `git diff --stat -- src include` is empty afterwards. Verified: focused **137/137**
(was 135), full native **2791 / 2784 succeeded / 7 skipped / 0 failed**, capture build SUCCESS with the RAM1 line
byte-identical, strict validation valid, `git diff --check` clean. **Recorded rather than smoothed:** three earlier focused
runs failed at the pre-existing `assert_s4b_7_a_demoted_target_through_the_turn` helper (`Expected 2 Was 0. the
root-backed demoted switch must commit`) and the same byte-identical source then passed 20+ runs (13 without a rebuild, 5
under `MALLOC_PERTURB_`), a clean cache rebuild and the full suite; the cause is **open and unproven** (a stale
incremental-build object or an uninitialised read in the launch/commit path), and no claim is made that the helper is
sound. **Task 3.10 stays open** on service qualification (device enforcement), and 6.3's remaining axes and 3.7's removal
are untouched. Detail:
[`evidence/2026-10-05-s310y-activation-audit/EVIDENCE.md`](evidence/2026-10-05-s310y-activation-audit/EVIDENCE.md).

**Task 3.10 — the rolling path's admission boundary on the playing chain (2026-10-05; test-only, no checkoff).**
Three fixtures drive the **playing** chain the production lane composes (`rollingTestTick`: selector + one worker turn per
64 ticks, transport per tick) with the interference budget enforced in tests only. **R1:** an admissible two-bar shape is
requested at the first aligned interval, prepared, adopted at its absolute boundary and **served repeatedly** — the served
view changes at each handoff with **no mutable build** at any of them — emitting **exactly 33 events** (16 ons + 17
closures of the loop's own note 48, nothing else), crossing **no** promise, leaving **no demand**. **R2:** a **dense**
two-bar interval (300 notes = 600 entries, prepared cost beyond the lead the selector grants from the declared basis) is
**requested but never adopted** — the served view never moves, the episode freezes and is priced, its obligation keeps its
**original absolute deadline**, `one_pass_late == 0`, and playback continues through the mutable fallback (603 events over
the same drive). **R3:** while a slot **owns its coverage source** (qualification/retirement) the per-track budget is
**non-zero** although that slot has no admitted work record, the enforced candidate on that track is refused **by name**
(`CannotCompleteBeforeHandoff`, obligation retained) and the **same request is admitted once the source has retired** —
before/after in one fixture. Capacity 2048, span <= 2 bars and every timing constant are unchanged; the claim is
**intermittent immutable coverage**, not continuous service. **Findings recorded:** (F-1) the earlier "intermittent"
S4b.7 helper failure is now reproducible with a **named culprit** — `test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one`
leaves suite-global state that leaves a later demoted switch **pending** while the target's launch is ready (probe:
re-creating the TrackManager + pool does not clear it; bisected at registration #89), mechanism unidentified, so the new
fixtures are registered before it; (F-2) the selector's refusal check reads the *pending* record while a post-freeze
refusal is written on the *frozen* one, so the episode ends by non-adoption rather than a named `Blocked` state — **the
next contract decision**; (F-3) the retry admission's *sibling* charge is not what refuses the dense shape (its own derived
cost is); (F-4) the selector must be serviced at the lane cadence, not every tick. Mutations: **MR2** (a coverage-source
owner not charged) fails R3 as intended and is restored byte-identically; MR1/MR1p/MR3/MR5 are recorded with the reason
each did not give a clean red-proof. Verified: focused **140/140**, full native **2794 / 2787 succeeded / 7 skipped /
0 failed**, capture build SUCCESS with the RAM1 line byte-identical, strict validation valid, `git diff --check` clean.
**No production line changed**; no upload; no optimization series. Detail:
[`evidence/2026-10-05-s310z-rolling-admission-boundary/EVIDENCE.md`](evidence/2026-10-05-s310z-rolling-admission-boundary/EVIDENCE.md).
