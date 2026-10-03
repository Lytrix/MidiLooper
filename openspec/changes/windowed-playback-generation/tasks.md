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
- [ ] 2.3 Land **F-3** (one pass late, and only that) — verify the previous revision of *that interval* stays
  authoritative for the traversal, that a **second** miss is reported as a named breach, that the allowance is not reset
  by a further edit, and that the consumer is never blocked
- [ ] 2.4 Land **F-4** (nothing allocates or blocks on the edge) — verify the fixture fails when the commit edge takes
  capacity or builds inline, in the S4b.6/S4b.7 shape
- [ ] 2.5 Land the **empty-interval readiness** fixture — verify a completed publication over an interval with no events
  is ready and sounds as silence, and that the fixture fails under today's non-empty term
- [ ] 2.6 Land the **stale deferred completion** fixture — verify a completion arriving after an ownership change is
  rejected or re-attributed through the playback generation rather than published
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
  *Not started 2026-10-02, and the reason is this task's own gate rather than a judgement call:* it requires **3.11**
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
  production **callers** (the preview requester and the `EditManager` lifecycle hooks), which belong to the same stage
  that admits the committed requester (3.7). Detail:
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
  completion while preparation grains are paused — 3.7's serving path); (c) retained-source lifetime and discharge
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
  exactly **4.1**'s lagging-checkpoint outcome. So the refresh's requester belongs to **4.1** (a current root) and then
  **3.7** (the consumer/traversal lane, which knows the interval and the phase) — an ordering statement backed by two
  measurements, not a deferral. What this edge owes it already does: advance the currency (revision + generation), settle
  the closures from the change's own identities, keep the ledger (the closure-obligation set), silence nothing and
  enumerate nothing. *Verification:* focused **64/64** (fixture extended again, not added), full native **2695 / 2688
  succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192`
  **unchanged** / `code 392,328 + padding 888` (48 B above the floor; byte-identical to the five previous slices). Three
  mutations red-proved with byte-identical restores, and **two of them deliberately mutate other owners' code** (the
  not-current guard, the revision bump) because those are the facts the handoff rests on — a fixture that only fails when
  its own slice is undone would not test the claim it states. **OWED in this task:** the refresh's requester (4.1 -> 3.7);
  the between-grains matrix (repeated undo/redo, track/slot switching, stale completion while preparation grains are
  paused — 3.7's serving path); retained-source lifetime and eventual release (3.6a, cited); displayed provenance; and
  preview-session undo/redo (3.13/2.12, cited). Its **audible** requirement and the red-proof the task demands
  ("restore the blanket silence") are complete. Detail:
  [`evidence/2026-10-02-s314c-refresh-source-verification/EVIDENCE.md`](evidence/2026-10-02-s314c-refresh-source-verification/EVIDENCE.md).

## 4. Checkpoint parity and content parity

- [ ] 4.1 Land **F-5** (checkpoint parity, undo) — verify an undo restores exactly what its entry named, including the
  **lagging** case, whose outcome must be named rather than reported as current
  *Increment 4.1 landed 2026-10-02 — the parity is **named** (`CheckpointParity` + the pure `resolveCheckpointParity`, and `Loop::checkpointParity()` reading the loop's own three figures: no new state, no pin move), and the **lagging** case is named through the *production* undo path with `publishedSnapshotIdentity()` null — selected, never presented as current. This also answers the question 3.14c left the refresh resting on: whether a root is current is now a **named** outcome (`OwesReconciliation` at the undo edge, measured), so the requester's precondition has a name even though the requester itself stays 3.14 → 3.7. Verified: focused 65/65, full native 2696 / 2689 succeeded / 7 skipped / 0 failed, true clean rebuild with the RAM1 line byte-identical (48 B headroom); four mutations red-proved with byte-identical restores. **The task stays open** on the content-level comparison, whose instrument is 4.3's content-verification pipeline; save parity is **4.2**. No production caller consumes the name yet (4.2 and the readiness/lag reporting will) — stated rather than implied. Detail: [`evidence/2026-10-02-s41-checkpoint-parity/EVIDENCE.md`](evidence/2026-10-02-s41-checkpoint-parity/EVIDENCE.md).*
- [ ] 4.2 Land **F-6** (checkpoint parity, save) — verify a persisted body is equivalent to the root it was serialized
  from and a restore's outcome is either current **or** named as stale; the fixture must falsify today's
  `storePublishedSnapshot(…, kSnapshotCoverageCurrent)`-without-reconcile path
  *Increment 4.2 landed 2026-10-02 — the **verification** half: through the production writer (`writeLoopPersisted`) and the production restore edge (`applyLoadedLoopSlotBodyCommit`) the installed root is **equivalent** to the root that was serialized and the restore's coverage outcome is **named** (4.1's `CheckpointParity::Current`), which nothing asserted at that edge before. The task's **falsification** clause is answered by measurement, not by a pin: a body whose metadata and root cannot come from one committed state is accepted by the reader and installed as a *named-current* checkpoint (`publishedSnapshotIdentity()` non-null) — today's `storePublishedSnapshot(…, kSnapshotCoverageCurrent)` claims currency on the **writer's** admission, which the body does not record. **The task stays open** on the repair: persisting the root's coverage is a **format decision** (LSV1 v1 is pinned; no provenance field exists), and the cheap cross-check is **refuted by reading the code** (`EditSelectNoteState.cpp:188` → `Loop::allocateNoteId()`, so the note-id figure can advance while a settled root's committed state is unchanged). Verified: focused 52/52 (+1 fixture) + 65/65 + 190/190 (cited pin) + 38/38, full native 2697 / 2690 succeeded / 7 skipped / 0 failed, true clean rebuild with the RAM1 line byte-identical (48 B headroom); four mutations red-proved with byte-identical restores, their bound stated (a positive-outcome fixture cannot falsify an over-claiming parity). Detail: [`evidence/2026-10-02-s42-save-parity/EVIDENCE.md`](evidence/2026-10-02-s42-save-parity/EVIDENCE.md).*
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

- [ ] 6.1 Run the full native suite (`pio test -e native`) — verify every case passes and that no skipped or errored
  suite is counted as green
- [ ] 6.2 Build the firmware env (`teensy41-capture-serial`) through the RAM1 budget guard — verify the build succeeds
  and the budget line is reported; do **not** flash without asking
- [ ] 6.3 Run the sweeps — verify loop length, content density, tempo, window span and competing deferred work are each
  varied and reported, and that a single configuration is never presented as the acceptance
- [ ] 6.4 Add a dedicated **windowed HITL profile** through the existing content-verification pipeline (canonical
  stimuli preserved, never edited to chase a condition) — verify the profile is registered, authorized and used for the
  F-7/F-8 device legs and the AC-8 receipts under a running transport
- [ ] 6.5 Apply the **native/hardware verification split** (*DEC-108 §13 verification split*) — verify **native** forces, on
  the real request→worker→publication→consumption path with controlled ticks and explicit expected MIDI output (no
  sleeps, no copied state machines): A→B→A slot reuse with the first A completion arriving last, the interval A/B
  revision trace, cancellation between every preparation grain, repeated edits without a deadline reset, stop/start,
  undo/redo, clear/load, clock revalidation, resource refusal followed by recovery, and closures crossing version
  boundaries. Verify **hardware** covers grain duration and maximum service gaps, sustained multi-track playback with
  capture and persistence contention, external-clock behaviour, and memory high-water marks with boundary content parity
  — and record that reproducing the exact A→B→A race on hardware is **not** required once the native ordering proof
  exists (native cannot establish physical timing; hardware is not a substitute for the ordering proof)
- [ ] 6.6 Update RWO's dependency records now that the sibling exists and carries its DEC and reassessment — verify RWO
  `tasks.md` (S2.9/F) and `design.md:812` point at this change, and that S2.3's `N` and `S7 gates` status are restated
  accurately under [`docs/Authority/DOCUMENTATION_CLOSEOUT.md`](../../../docs/Authority/DOCUMENTATION_CLOSEOUT.md)
- [ ] 6.7 Close out the docs — verify `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md`,
  `docs/DELIVERABLE_TRACKING.md` and this change's own task list reflect the shipped state, and
  `openspec validate windowed-playback-generation --strict` is valid
- [ ] 6.8 **Continuity evidence handoff** — reference the runtime spec's *Continuous runtime operation has
  one end-to-end acceptance contract* and its task S7.5a — envelope/timing oracle, rather than inventing
  local end-to-end thresholds. Before enabling the affected production paths, allocate and verify the
  window/index/retention/page/closure work against that shared service and memory budget. Under simultaneous
  playback, capture/finalization, undo/redo and persistence/display contention, record native transition
  evidence plus hardware grain/service-gap, input and route-specific output timing evidence. Assert no
  in-envelope capacity refusal, coverage fault, unintended note interruption or involuntary stop. A
  one-pass-late edit is measured as audible-action latency, never hidden as immediate execution. Supply
  evidence to runtime task S7.5b — dependency closure and task S7.5 — combined continuity acceptance;
  passing this subsystem does not certify the whole runtime. Keep task 4.5's fault-injection results separate.
