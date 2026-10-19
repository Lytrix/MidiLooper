# Tasks — windowed-playback-generation

Requirements: `specs/windowed-playback-generation/spec.md`. How: `design.md`. The contract is **DEC-108 — windowed playback publication**, as superseded for the representation split by **DEC-116 — one rolling path per sounding slot**; the trigger record and its addendum are `ARCHITECTURE-REASSESSMENT.md`. Sequencing and inherited criteria: `proposal.md` §Inherited contract (`S4C1` AC-1…AC-9, `S4C45` F-1…F-8).

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
- [x] 2.7 Close the measured coverage gap for the demoted shape — verify a fixture **fails** when the window build is
  removed on a demoted slot (RWO recorded that dropping it left every fixture green and pinned that as a harness
  tripwire)
  *Checked off 2026-10-07:* the mutation that removes the demoted slot's window build (an early `Idle` in
  `Track::stepPlaybackViewMaintenance` for `shouldAvoidFullVisualRebuild` loops, so the admitted demand never receives a
  grain and nothing is refused by name) is **RED** — 89 of 176 focused cases fail, including the demoted shape's own
  fixtures (`test_window_owner_resolves_the_interval_through_the_published_snapshot`,
  `test_retained_source_window_serves_a_rootless_interval_and_the_index_bounds_the_lookup`,
  `test_windowed_stream_concatenates_to_the_whole_loop_stream_across_a_boundary`,
  `test_demoted_slot_residency_is_length_independent_swept_over_lengths`,
  `test_window_readiness_names_its_holder_and_an_empty_interval_is_ready`,
  `test_window_owner_publishes_the_interval_window_atomically`) — and the file is restored **byte-identically**
  (`sha256 269331f615b66c6a…`, unchanged), with the focused suite **176/176** green afterwards. Detail:
  [`evidence/2026-10-07-s27-demoted-shape-tripwire/EVIDENCE.md`](evidence/2026-10-07-s27-demoted-shape-tripwire/EVIDENCE.md).
  *Not claimed:* task 2.8 (the same red-proof *inside the slice that implements it*) stays open, and RWO's own S4b.7
  tripwire (`ensurePlaybackMergedEventsForSlot`) is that change's.
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
  *Covered today:* below/at/above the limit, the closure at capacity, refusal by name (`window_admission`), a stop never
  refused, accumulation across passes, and — since 2026-10-06 — the **SD-restored over-`L` loop served through bounded
  pages with cross-page closures** on the production clock entry, in **both** of the shapes the declared service
  distinguishes: served **complete, ordered and exactly-once** inside its closure capacity, and **refused by name**
  beyond it (see § 5.5's record, which is also where the completeness clause was falsified and then repaired). **Still
  open, not faked:** *an edit that moves events into a full interval* — reading it literally against this owner, an edit's
  rows do not pass the capture-edge gate (`Track::recordMidiEvents`) that 3.12c wired, so the clause needs the
  edit/interval-projection owner's terms (or a decision that it is already covered by
  `…_the_capture_admission_counts_committed_windows_only`), and the rest of 2.11's list is covered by the existing
  fixtures (below/at/above `L`, the closure at capacity, refusal by name, stop never refused, accumulation across passes).
  full interval* — read literally against this owner, an edit's rows never pass the capture-edge gate
  (`Track::recordMidiEvents`) that slice 3.12c wired, so the clause needs the edit / interval-projection owner's terms
  or an explicit decision that the committed-window accounting fixture already answers it.
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
  *Limits recorded, not smoothed over:* the **consumer path is 3.7a's** — nothing in production requests a retained-source window yet, so the fixture drives the index and body enumeration directly rather than through a production caller — and the **envelope roll-up** of these per-slot figures into RWO's budget artifact belongs to that change's own terms.
  Detail: [`evidence/2026-10-02-s36c-interval-index/EVIDENCE.md`](evidence/2026-10-02-s36c-interval-index/EVIDENCE.md).
- [x] 3.7a **Wire the retained-source interval index into the production window consumer while retaining the legacy fallback** — extend the existing `Track::requestPlaybackWindow` / `Track::stepPlaybackViewMaintenance` / `RuntimePlaybackViewFill` path to query `RetainedIntervalIndex` and enumerate pinned source bodies through the effective-content resolver. Keep the current merged-window fallback active as a compatibility and cold-parity path. Reuse existing role-owned windows, retention and `WindowPagedStream`; do not add an interval-root `LoopSnapshot`, publisher, scheduler or second content authority. Supply the production consumer/caller integration owed by 3.12 (paged legacy serving) and 3.13 (preview request/lifecycle hooks). Verify root-demoted production serving without a whole-loop root, parity, empty-window ledger behavior, bounded inspected work, legacy pages, preview lifecycle, timing admission and differential fallback parity. After this integration is enabled with fallback intact, finish 3.14 and 4.5 against the real handoff, including their production-path interleavings and closure/recovery gates. A slice-specific G2 packet, production-path fixtures and red-proofs are required before enablement.
  **Increment I1 landed 2026-10-06 (firmware + tests; the task stays open).** The G2 packet
  ([`evidence/2026-10-06-s37a-production-consumer/EXECUTION.md`](evidence/2026-10-06-s37a-production-consumer/EXECUTION.md))
  fixes the slice's scope, oracle and a four-increment ladder (I1 this rootless serving + requester + readiness
  increment; I2 the deferred lane turn serving a rootless demand; I3 the 3.12 paged consumer and 3.13 preview hooks;
  I4 differential parity and the timing admission). I1 adds `Track::gatherRetainedSourceWindowForSlot` — the interval
  answered from the **retained revision's pinned bodies**, directed by `RetainedIntervalIndex` (an interval the index
  proves empty is served empty with **no chunk inspected**; a stale revision, a loop beyond the indexed ceiling, an
  interval with no published index, pinned Active note-edit rows and unreachable pinned bodies each refuse **by
  name**) — and takes it from `Track::requestPlaybackWindow` exactly where the published-root enumeration cannot
  answer at all, with the merged-window fallback untouched. `Track::playbackWindowReadinessForSlot` names the **build**
  owner, not `published_source`, for a rootless slot whose retained source is the current revision. **Verified:**
  focused **146/146**, full native **2,817 / 7 skipped / 2,810 succeeded / 0 failed**, capture build **SUCCESS** with
  `variables 96,192` unchanged and `code 391,768 + padding 1,448` (floor 840; **+192 B**, after the measured
  `std::sort` instantiation cost of **+1,280 B** was removed by sorting by hand), strict validation and
  `git diff --check` clean, four mutations red-proved with byte-identical restores — **one of them recorded as a

  non-red-proof** (the gather's own revision gate is redundant under the index's revision authority; MR-3.7a-B'
  removes both and fails 3.6c's fixture plus this slice's) — and the differential parity claim scoped to the
  unedited-content window (same-tick order stays 4.3's gate). **Not claimed:** no production lane service for a
  rootless demand (I2), no edited-content/long-window parity (I4), no device run, no fallback removal (3.7b), and
  3.12/3.13's consumer clauses remain owed to I3. Detail:
  [`evidence/2026-10-06-s37a-production-consumer/EVIDENCE.md`](evidence/2026-10-06-s37a-production-consumer/EVIDENCE.md).

  **Increment I3 landed 2026-10-06 (firmware + tests; the task stays open).** The **preview lane's production callers**
  — the clause task 3.13 owed — are firmware code now: `Track::servicePreviewWindowForNoteEditSession()` runs at the
  **revision-advance seam** (`EditManager::flushDeferredNoteEditDisplayRefresh`, the function that bumps
  `sessionPlaybackPreviewRevision()`, plus the session open), cancelling a superseded preparation by name
  (`SourceMutated`, whether or not the lane is busy), releasing a prepared successor that belongs to a revision the
  session has left (through the `ClearOrLoad` shared-view row), and requesting the session's own projection for the
  newest revision; the **exit** (`closeNoteEditSession`, before its own flush) yields the source and owes the committed
  handoff; the **re-focus** (`beforeSelectedTrackChange`) yields the departing track's preview without owing anything.
  The capture admission now counts **committed** windows only (`committedPlaybackWindowServesSlot` /
  `committedPlaybackWindowEventCountForSlot`), so a served preview — the session's uncommitted projection — is never
  inherited content. `Track::previewWindowSourceActiveForSlot` was added because the re-focus rule had no observable.
  **Verified:** focused preview **9/9**, focused working-set **151/151**, full native **2,825 / 7 skipped / 2,818
  succeeded / 0 failed**, capture build **SUCCESS** with the RAM1 line **byte-identical to I2's** (`variables 96,192`;
  `code 391,832 + padding 1,384`), strict validation and `git diff --check` clean, four mutations red-proved with
  byte-identical restores. **Recorded, not smoothed over:** the packet's named seam (the deferred idle turn) was written,
  measured and **removed** — it drags the whole deferred stop/persistence boundary into the preview suite's link set —
  and the implemented seam is the flush that owns the revision; the re-focus mutation was first a **non-red-proof**
  because the requester's own non-selected branch masked the hook (fixed by the new read and a stronger assertion); and
  the supersession fixture needed **two mutations with no drain between** to make the cancel load-bearing. **Not
  claimed:** no paged (over-L) consumer (I3b), no display consumer, no short-loop preview window (the requester is gated
  on the demoted shape), no production adoption boundary for a preview successor, no timing envelope, no device run.
  Detail: [`evidence/2026-10-06-s37a-i3-preview-wiring/EVIDENCE.md`](evidence/2026-10-06-s37a-i3-preview-wiring/EVIDENCE.md).

  **Increment I3b landed 2026-10-06 (firmware + tests; the task stays open for I3c).** The I3 packet split I3 into
  I3 (preview, above), **I3b (this: the paged legacy path in the production lane)** and **I3c (the hot-path emission
  adapter, still owed)**. An interval that cannot fit one window is now **prepared as pages** instead of ending as a
  refused demand: the retained lane's own slice ceiling (`kRetainedGatherMaxSlices`) and the **admission's own
  `WindowExceedsRetainedCapacity` refusal** both switch the demand to `WindowPagedStream` for the **same** revision and
  interval, from the **same** pinned bodies (one file-static `collectPinnedCaptureLists`, so the window gather and the
  pager cannot enumerate different content). `Track::beginPagedPlaybackForSlot` / `stepPagedPlaybackForSlot` produce
  **one bounded page per turn** (`RingFull` → `ProducerWaiting`: the producer waits for its consumer, never allocating or
  overwriting) and write the delivery by name (`PagedPlaybackOutcome::Prepared`) when the source completes; the frozen
  demand is the promise throughout and is discharged as that delivery. `Track::consumePagedPlaybackWindowForSlot` is
  **3.12's owed production consumer entry**: it walks the prepared interval exactly once through the runtime's own
  cursor, answers `NeedsPage` (never a wait) for the caller to produce, releases the ring's pages when the walk ends, and
  refuses a revision the loop has left by name (`StaleRevision`). A launch keeps its dense-legacy merged fallback (by
  name) because nothing emits from pages until I3c. **Verified:** focused working-set **154/154**, ring **3/3**, preview
  **9/9**, full native **2,829 / 7 skipped / 2,822 succeeded / 0 failed**, capture build **SUCCESS** (`variables 96,192`;
  `code 392,216 + padding 1,000`; **160 B** above the floor — the tightest figure on the lane, recorded as I3c's first
  constraint), `LoopPlaybackRuntime` pins re-based **compiler-measured** (target **7,312**, host **9,448**), strict
  validation and `git diff --check` clean, **five mutations red-proved** with byte-identical restores (MR-M the
  budget-branch switch, MR-N the page-per-turn bound, MR-O the read-side revision guard, MR-P the admission-trigger
  switch, MR-Q the cursor's own advance) — **one non-red-proof recorded as such**: MR-Q's first attempt was a no-op
  against a fixture that always consumed a whole page, so the walk was given a **partial-buffer** shape, which is where
  the cursor is observable. **Named limitation:** the pages emit in traversal order (pass-set order, chunk order within a
  pass); global tick-ordering *across layers* is I3c's, so the content fixture uses single-pass content where traversal
  order *is* tick order. **Also recorded:** the admission-refusal branch no longer drops the demand (it is coalesced
  back), and I3's "151/151" line counted its admission fixture twice (the committed tree registers 150). **Not claimed:**
  no hot-path emission (I3c — its blockers are the stream index → page base mapping, page retirement by emission
  position, the timing envelope, and the lane gate), no `ExecutionBounds.md` row change, no timing measurement, no
  device run, no fallback removal (3.7b). Detail:
  [`evidence/2026-10-06-s37a-i3b-paged-legacy/EVIDENCE.md`](evidence/2026-10-06-s37a-i3b-paged-legacy/EVIDENCE.md).

**3.7a increment I3c-b landed 2026-10-06 (firmware + tests; the task stays open for I3c-c):** the **lead across the wrap** (an emitting delivery keeps one page free, so the producer hands the next pass's head over while this pass's tail is still queued) flipped the acceptance measurement to **both passes exact** with `passesUnstraddled == 0`; the straddle judgement was made **walk-independent** (`passContentOrdinal`, and a boundary with no completed traversal counts unstraddled by definition); and the **hot-path seam** selects the paged stream for a slot with no served window/bridge/pair, skips the unbounded merged build there, and signals the pass boundary where the projection detects it. **The acceptance fixture then found a second defect**: the *second* pass's sequence drifts out of alignment past index 5,000 (the walk's per-pass index and the ring's ordinal do not re-align exactly at a boundary), so **selection is closed by construction** (`kPagedEmissionSelectionEnabled == false`, a compile-time constant) and the merged fallback keeps playing the slot -- emitting wrong content is worse than not emitting from pages, and I3c-c's first work is that mapping. A dry-based withdrawal written this increment was **removed** (dry reads at a pass's event-free tail are legitimate: 64 measured across two healthy passes). Verified: focused **157/157**, ring **3/3**, full native **2,832 / 7 skipped / 2,825 succeeded / 0 failed**, capture build **SUCCESS** with the guard passing (`variables 96,192`; `code 392,152 + padding 1,064`; 224 B above the floor), strict validation and `git diff --check` clean. **Not claimed:** no selection (closed), no timing claim, no row change, no device run. Evidence: `evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md`.

  **Increment I3c-a landed 2026-10-06 (firmware + tests; the task stays open for I3c-b, and its own assumption is the
  thing it falsified).** The **paged emission adapter** exists: `makePagedEmissionStream` presents the ring to the
  production walk (`advancePlaybackCursor`) by **stream ordinal** -- `WindowEventPage::baseOrdinal`, `producedOrdinal()`,
  `eventAtOrdinal()` and `retireThroughOrdinal()` (retirement by *emission position*), plus `rearmForNextPass()` (the
  additive pass re-arm I3b named as its blocker: the traversal restarts, the pages and the ordinal space are kept) --
  with **no staging copy**, no allocation, no producer call and no wait on the clock path (a ring with nothing ahead of
  the walk is a counted dry). The pass model is explicit: the walk's index is per-pass, the ring's ordinal is not, and
  the base is set at a boundary the projection **signals** (`notePassBoundary`). `Track::pagedEmissionDeliveryStateForSlot`
  names what a selection gate may read (`NoDelivery`, `StaleRevision`, `CannotStraddleTheWrap`, `Unproven`, `Admissible`).
  **The measurement falsified the increment's assumption:** driving two consecutive passes through the walk with the lane
  producing one page per turn shows pass one **exact** (2,500/2,500) and pass two **short by two pages** (2,244/2,500),
  both wraps judged `passesUnstraddled` -- a three-page ring cannot lead a wrap at this density, so the events the next
  pass's first frames cross are behind them. The delivery is therefore **refused by name for selection** (the gate stays
  closed; the merged fallback keeps playing the slot) and I3c-b's requirement is now *measured*: give the producer a lead
  across the wrap, then this fixture is its acceptance test. **Verified:** focused RWS **157/157**, ring **3/3**, full
  native **2,832 / 7 skipped / 2,825 succeeded / 0 failed**, capture build **SUCCESS** with the RAM1 guard passing
  (`variables 96,192` unchanged; `code 392,280 + padding 936`; 96 B above the floor -- after the run's first build
  **failed** the guard with +32 B of RAM1 and +320 B of ITCM, both fixed by moving the guard's event into the runtime's
  external state and the adapter's functions to flash), pins re-based compiler-measured (target **7,384**, host
  **9,528**). **Recorded as deviations:** the packet's **red-proofs were not run** (owed to I3c-b, where the wiring makes
  them load-bearing), and W-37a-20's expectations were **corrected to the measurement** rather than to the hoped-for
  result. **Not claimed:** no production selection (no gate is open), no timing claim, no `ExecutionBounds.md` row change,
  no cross-layer ordering claim, no device run, no fallback removal (3.7b). Detail:
  [`evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md`](evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md).

  **Increment I3c-c landed 2026-10-06 (firmware + tests; the task stays open for I4/I5 parity + timing).** The boundary
  mapping defect is **fixed** and paged emission **selection is enabled**. Diagnosis: the adapter served *everything
  published*, while the producer deliberately runs ahead into the next pass -- whose events occupy the same tick space --
  so the next pass's head leaked into the current pass's window. Fix: the adapter bounds the walk by the **emitting
  pass's own content end** (`passContentOrdinal`, the producer's last re-arm ordinal; the bound is self-correcting at a
  boundary, and the residual "a further pass completes inside one window" cannot arise because this path exists only for
  content over `L`, sixteen pages, so no pass fits the ring's three-page depth). `kPagedEmissionSelectionEnabled = true`:
  a slot with no served window, bridge or prepared pair whose delivery is admitted (one cached boolean, maintained
  off-clock) emits from the ring and the unbounded merged build is skipped for it. **Verified:** the acceptance fixture
  asserts both passes exact, element-wise, exactly 5,000 emissions; **MR-I3c-c-1** (the content bound removed) is **RED**
  (*Expected 5000 Was 4872*) with a byte-identical restore; focused **157/157**, ring **3/3**, full native **2,832 / 7
  skipped / 2,825 succeeded / 0 failed**, capture build **SUCCESS** with the guard passing (`variables 96,192`;
  `code 392,216 + padding 1,000`), strict validation and `git diff --check` clean. **Still owed:** the I3c-a packet's
  **MR-R/S/T/U**, and the end-to-end emission assertion on a real transport tick (I4/4.3's differential parity).
  **Not claimed:** no timing claim, no `ExecutionBounds.md` row change, no device run, no fallback removal (3.7b).
  Detail: [`evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md`](evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md).

**3.7a increment I3c closed out 2026-10-06 (firmware + tests; the task's remaining items are named):** driving the **production entry** (`Track::playMidiEventsForSlot`) found a real defect: the pass-boundary signal was gated on the *selection* flag while admission -- which the selection tests -- is the boundary's own output, so the delivery could never be admitted (`boundaries` stayed 0 through a whole pass). The signal is now gated on the delivery being **bound** (the judgement is the admission's input, so it must not depend on the admission), and the crossing test plus the judgement moved to **flash** (`Track::servicePagedPassBoundaryForSlot`) after the inline version put ITCM 32 B under the RAM1 floor (guard: `code 392,408 + padding 808`, failed; now `code 392,296 + padding 920`, 80 B above). The new production-path fixture asserts what this suite can drive: with the production entry as the driver, a prepared-but-unadmitted delivery is not selected, consumes nothing and leaves the merged store **empty** (a rootless dense slot is silent there -- the condition paging exists to change). **Named, not implied:** the *emitting* half on real ticks needs the slot's own playing state, which this suite's accessors do not provide -- I4/4.3's harness, where the differential-parity oracle lives. Verified: focused **158/158**, full native **2,833 / 7 skipped / 2,826 succeeded / 0 failed**, capture build **SUCCESS** with the guard passing, strict validation and `git diff --check` clean. Evidence: `evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md` (§8).
**3.7a increment I4 (first slice) landed 2026-10-06 (test-only):** the task's **timing-admission** clause is now measured rather than assumed. Declared: one page (128 events) per deferred turn at a permitted advance of 64 ticks; a dense over-`L` interval needs ~104 events per frame, so the producer keeps up at the declared rate and the ring's three-page depth absorbs jitter -- and at **half** the declared rate (one turn per two frames) the producer falls behind within the pass and the breach is **named** (a dry pass or an unstraddled wrap) rather than served as content. The fixture asserts the property in both directions and pins that measured outcome: the first evidence row for `ExecutionBounds.md` row 27's "caps declared, bounds unmeasured" on this path (a device envelope remains owed). **Measured prerequisite, named:** the *emitting* half of the production path (real transport ticks, the emitted stream compared) cannot be driven from this suite -- with `Track::startPlaying` (which clears the transport-stop output barrier), the projection re-anchored and the state asserted `PLAYING`, the clock path still recorded no tick through a whole pass (`loop.lastTickInLoop` stayed 0) -- and the parity oracle it needs (a MIDI sink, for velocity/tick-exact content and **4.3's same-tick order**) does not exist here either. Both belong to I4/4.3's harness. The scoped fixture still verifies something real: a prepared-but-unadmitted delivery consumes nothing, is not selected, and leaves the merged store **empty** (a rootless dense slot is silent on the clock path). Verified: focused **159/159**; production sources unchanged by this slice, so §8's device/guard figures stand; strict validation and `git diff --check` clean. Evidence: `evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md` (§9).
**3.7a increment I4 (third slice) landed 2026-10-06 (test-only):** the parity matrix is now stated once, so no property is claimed by silence. **Proven:** a paged pass's amount and content (element-wise, two whole passes, 5,000 events) and **4.3's same-tick order for a single-pass interval** -- the dense span packs several events per tick, the element-wise comparison would break on a slip inside a tick, and the fixture now *requires* that such ticks exist (so the claim cannot be vacuous). **Not proven, and named:** emission through the production send path (needs a MIDI sink this suite does not have), same-tick order **across layers** (the paged path emits in traversal order; multi-layer ordering is the window's hand-sort, not a page property), and real transport ticks (prerequisite narrowed to a line: the refusal is inside `playCommittedLoopMidi` before it records the tick, nearest candidate the stale-runtime reset path). **Measured:** the timing admission (one page per turn at a 64-tick advance keeps up; half that rate breaches within the pass and the breach is named). **Owed:** the device envelope for `ExecutionBounds.md` row 27. Verified: focused **159/159**, strict validation and `git diff --check` clean; production sources unchanged, so the device/guard figures stand. Evidence: `evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md` (IS11).
**3.7a increment I4 (fourth slice) landed 2026-10-06 (firmware + tests; native-only seam):** the tick path's silence is no longer a candidate pile -- the **refusing return is identified by measurement** and the **emitted stream is observable at the production edge**. **The bisection:** a temporary probe (a file marker at each of the five returns before the tick record, withdrawn before the commit) ran the whole focused suite: **24/24** of the I4 fixture's ticks stop at the **zero-content gate** (`eventCount == 0 && fixedBridge == nullptr && rolling.phase == Idle`) and **not one hit** was recorded at the other four (`runtime == nullptr`, `!runtime.reset(true)`, the bridge-stream refusal, the `UINT32_MAX` catch-up guard) -- the stale-reset candidate stays falsified, the pair path does not apply to this loop by its own gate, the rolling section *excludes* the slot (that clause is what keeps the gate reachable), and the merged fallback is empty because a rootless demoted slot has no published root to gather. The fixture now asserts those six door facts, the **no-tick-record** outcome, and that the MIDI edge committed nothing **with the output enabled**. **The oracle:** `Track::sendMidiEvent` gained a **native-only sink** recording the pair it hands the handler -- after its own gates and the channel remap -- bounded at 8,192 with a counted drop (`sentPlaybackMidiCountForTest()` / `sentPlaybackMidiAtForTest()` / `sentPlaybackMidiDroppedForTest()` / `clearSentPlaybackMidiForTest()`), proven end to end by the new fixture (one pass with the output disabled: events due, tick record advancing, nothing at either edge; the same pass enabled: exactly four events, element-wise equal to the wire boundary's own record). **Corrected:** the third slice's \"a MIDI sink at the production send path ... not implemented ... no handler-level capture\" was wrong as stated -- the playback route does reach `MidiSendCapture` via `sendRealtimePlaybackEvent` -> `MidiHandler::sendMidiEvent`; the gap was the Track-level record with the slot pairing. **Red-proved, byte-identically restored:** removing the zero-content gate's `return` fails the I4 fixture (*Expected 0 Was 1536*); hoisting the sink outside the emit gate fails the new fixture's control (*Expected 0 Was 4*). Verified: focused **160/160**, shared-capture **64/64**, full native **2,835 / 7 skipped / 2,828 succeeded / 0 failed**, capture build **SUCCESS** with the RAM1 line measured **identical with and without the change** (A/B by stash), strict validation and `git diff --check` clean. **Not claimed:** no selection change (this shape's delivery is still `Unproven`), no timing claim, no `ExecutionBounds.md` row change, no device run, no fallback removal (3.7b). Evidence: `evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md` (§13).
**3.7a increment I5 landed 2026-10-06 (firmware + tests; funded by an ITCM budget slice):** I3c-c's remaining item was the end-to-end emission assertion on a real transport tick; **driving it found that the assertion could not be made** -- a bound paged delivery could never earn admission from the clock path. Measured on both shapes (temporary probes, withdrawn): the **rootless** slot (the lane's reason to exist) records `bound=1 boundaries=0 admitted=0 sink=0 last=UINT32_MAX state=Unproven` -- its judgement never ran; the **root-backed** slot records `boundaries=4 unstraddled=4 contentEnd=0 emitting=0 admitted=0` -- every wrap judged, every one a violation, the producer never re-armed. The cycle: the zero-content gate returns above the boundary judgement; the straddle rule counts `passContentOrdinal == 0` as a violation, and one violation makes admission false forever; and `passContentOrdinal` comes only from the producer's re-arm, which happens only while `emitting`, which only a **read of the adapter** sets -- which admission gates. **The fix (four parts):** the judgement hoisted above the selection, in flash (`servicePagedPassBoundaryForTurn`); the gate's exemption for an admitted delivery folded into its count (a clause of its own measured +128..+256 B); the first boundary judged against the walk's position (`headRequest = passContentOrdinal != 0 ? passContentOrdinal : consumedOrdinal`); and a guard for the null merged store on the paged branch (measured **SIGSEGV** without it). **Landed by funding it -- the ITCM budget slice:** baseline `code 392,296 + padding 920`; the fix alone `392,488/728` (refused); the handoff seek moved to flash (`seekBridgeWindowPastServicedInterval`) `392,440/776` (**-48 B**); the fixed-source identity and its bridge/view resolved in flash (`resolveFixedPlaybackSourceForTurn`) **`392,360/856` -- the guard passes**, 16 B above the floor. Net cost of the fix **+64 B**; the slice's own movement **-128 B** of the same function. **Measured effect:** one frame judges, admits and walks (the pass head on the wire); a full interval emits `sink=2500 wire=2500`, exactly the dense span; a rootless 3-bar loop's pass 2 is **2,500 events element-wise equal** to `copyEffectiveCommittedEventsInRange` with `passesUnstraddled == 0` and `state=Admissible`. **Red-proved on the landed form, all restored byte-identically:** RP-I5-hoist (*Expected 1 Was 0. the frame judged the delivery's own pass boundary*; *the drive must reach the second pass*), RP-I5-first-boundary (*Expected 0 Was 1. and the judgement straddled*; *Expected 0 Was 3. every boundary...*), RP-I5-gate (*the same frame's walk took the delivery*; *Expected 0 to be greater than 0. the producer re-armed*). Verified: focused **161/161**, neighbours **83/83**, full native **2,836 / 7 skipped / 2,829 succeeded / 0 failed**, capture build **SUCCESS** with the guard passing, strict validation and `git diff --check` clean. **Device run (2026-10-06; `git_commit 9b964dbc8859...`, `firmware_hex_sha256 9697b7acacb262d2...`):** the I5 image booted fresh (`crash=0`) and the canonical leg **PASSed** (`overall_ok: True`; MIDI-boundary timing `overflow_free=True call_sites=True`, persistence and wrap storage green) -- the shared tick path is unchanged on hardware. The registered windowed leg drove its 24-bar record, `NOTE_EDIT` and window gestures without crash or wedge, but **the paged branch is not observable on device**: it has no serial row family, and the one image that emits the lane's rows (`teensy41-capture-serial-window-qualification`) **cannot be flashed -- it is over its own RAM1 budget** (`code 393,144 + padding 72` at HEAD~1, i.e. already under the 840 floor before I5; `code 393,288 + padding 32,696` with I5, a 13th bank the guard names as the documented silent boot hang), so the lane's rows and any 'one more row' instrument are unreachable until that env is funded (~912 B of pre-existing drift + I5's 64 B). Runs: `captures/i5-device-0{1,2}-base`, `captures/i5-device-03-windowed`; detail in the evidence (section 15). **Not claimed:** no timing claim, no `ExecutionBounds.md` row change, no fallback removal (3.7b). Evidence: `evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md` (§14).
**3.7a increment I6 (the handoff's closures) landed 2026-10-06 (firmware + tests; the slice tasks 4.5 and 3.14 both named):** I5 unblocked *"3.14's and 4.5's remaining clauses (written against the real handoff)"*, and both tasks name the **same** item -- the **emitted closure at the real handoff**. The defect, in the owner's own vocabulary: the full-loop handoff reconcile already computed the invalidated obligations (tagged open ledger entries whose committed NoteOn is absent from the incoming stream) and then **dropped them without a wire event**, so a note whose content the replacement removed stayed **sounding** until a stop or a clear -- and the replacement's own NoteOff could not close it either, because it carries the **new** identity and the ledger's identity-exact resolution refuses an unknown identity as an orphan. **The fix:** the predicate is stated once as a bounded plan (`ActiveNoteLedger::MissingCommittedNoteOnPlan`, a bounded 1,024 B value on the stack -- 128 Entries x 8 B, nothing allocating; `eraseOpenNotesMissingFromCommittedNoteOns` is now plan-then-drop), and the handoff **closes** each planned obligation on the MIDI wire -- the same immediate-send owner the two existing closure paths use -- at **the handoff's own tick** (a new `currentTick` parameter, threaded from all four call sites) and **only then** discharged through the ledger's **own** event path by identity -- the existing closure owner's order, so a discharge never precedes the wire (a LIFO pop could discharge a different entry of the same pitch), and the erase is what makes a second closure impossible. Ordering is **structural**: every caller runs the reconcile ahead of the walk that serves the replacement's note-ons. The reconciliation's own interpretive rule now guards it: **an event-empty** whole-loop replacement closes nothing **and drops nothing** (`handoffAbsenceMeansRemoval(true, true) == false`), so those obligations stay alive instead of being forgotten; a slice still returns early. **Measured:** the closure is the handoff turn's **first wire event** and precedes the replacement's note-ons; the unchanged sounding gets no early NoteOff and no retrigger, its own note-off arriving once at its own tick; an untagged entry survives; an obsolete closure never re-fires. Verified: focused **162/162** (baseline 161), full native **2,840 / 7 skipped / 2,833 succeeded / 0 failed** (baseline 2,839/2,832), capture build **SUCCESS** with the RAM1 line **byte-identical to the pre-slice baseline** (`variables 96,192`; `code 392,248 + padding 968`; floor 840 -- the reconcile is already FLASH-placed, so closing on the wire cost **no ITCM** and no re-base), strict validation and `git diff --check` clean, **three red-proofs** (RP-A the silent drop restored, RP-B emitted but not discharged, RP-C the empty-window rule dropped) each RED with a named assertion and a byte-identical restore that reproduces the slice's diff exactly. **Reviewed once** (code-reviewer, against the packet's contract): its one concrete finding -- the discharge preceded the wire send, the reverse of the existing closure owner -- was fixed before landing, and its other findings are recorded rather than smoothed (no delivery confirmation on the immediate-send edge, inherited; the closure not consulting the slot's output gate, named; the suppression holding by the erase rather than by a guard; the plan's real size). **Not claimed:** the pitch-addressed re-issue case (4.5b's `suppressedRetriggers`; closing the invalidated identity can cost one boundary before the new identity re-sounds), a closure that slid **behind** the re-anchored cursor, the off-clock call sites on device, displayed provenance, the between-grains undo/redo matrix, retained-source release, preview-session undo/redo, **no device leg** (no registered stimulus drives a sounding across a content-removing handoff), no timing claim, no row change, no fallback removal (3.7b). Detail: [`evidence/2026-10-06-s37-handoff-closures/EVIDENCE.md`](evidence/2026-10-06-s37-handoff-closures/EVIDENCE.md) with its [`EXECUTION.md`](evidence/2026-10-06-s37-handoff-closures/EXECUTION.md) packet.
**3.7a increment I7 (the retained revision's note edits, per layer) landed 2026-10-06 (firmware + tests; the task stays open, 3.7b's conditions unchanged):** the **residual I1 named as I2's** and I2/I3 deferred is closed: the retained-source gather **resolves the pinned revision's Active Note rows per pass layer** instead of refusing the revision, so a rootless (demoted / restored) slot **with note edits** is served rather than silent -- the shape 3.14's undo/redo handoff and the restored-library case live in. The rule implemented is the effective-content owner's own (`Loop::copyEffectiveCommittedEventsInRange` applies each committed list and then merges; `applyNoteEditPassSequence` is called per layer, never re-stated), and the rows are the revision's **pins**, not the live Active set. `Track::gatherRetainedSourceWindowForSlot` gained the caller-owned **layer scratch** (the primitive may not hold it: a requester and the deferred lane can gather for the same slot), and the traversal is now **one layer per call** (`appendWindowBudgeted(lists.data(), layerIndex + 1, ...)` cannot cross a layer boundary) with a bounded slice per call and the **last** layer's resolution as the completing call; a layer with no in-window event advances without spending the call's work budget. **The consequence this required:** with the refusal gone, the paged lane became reachable for such a revision and would have paged **unresolved** bodies, so `Track::beginPagedPlaybackForSlot` now refuses a revision whose pins carry an Active Note row **by name** -- `PagedPlaybackOutcome::PinnedEditRowsUnresolved` + `PlaybackViewPendingReason::PinnedEditRowsUnresolved` at both paging origins + `committedPlaybackRetryCanProgress` declining to re-attempt it (the 3.12f pattern, applied to the other refusal) -- establishing no ring and keeping the demand owed. The dead vocabulary (`RetainedWindowGatherResult::PinnedEditRowsUnsupported`, `RetainedGatherRefusal::EditRowsUnsupported`) is removed, and the index's empty-interval shortcut is now taken only when no Active Note row is pinned (a `Create` row adds events the committed bodies do not carry). Verified: focused **166/166** (baseline 164), full native **2,844 / 7 skipped / 2,837 succeeded / 0 failed** (baseline 2,842/2,835), capture build **SUCCESS** with the RAM1 line **byte-identical to the pre-slice baseline** (`variables 96,192`; `code 392,248 + padding 968`; 128 B of headroom) after the increment's real work -- the per-layer path's first working form cost **+320 B of ITCM** (the `std::vector<EditPass>` machinery emitted out of line from the large gather: `push_back` 204 B, `~vector()` 82 B, two 32 B allocator partials) and moving the row collection and the rows into small flash helpers restored the line byte-for-byte, **no re-base**; `LoopPlaybackRuntime` pins re-based **compiler-measured** (target 7,384 -> **7,400**, host 9,528 -> **9,552**); strict validation and `git diff --check` clean. **Four red-proofs, all RED with byte-identical restores** (`sha256 48c57b2fe9d7c78e…`): **RP-I7-flat** (rows applied to the merged window: *Expected 264 Was 266*), **RP-I7-merge** (the layer appended instead of merged: *Expected 150 Was 200*), **RP-I7-pins** (the live Active set instead of the pins: *Expected 1 Was 0*), **RP-I7-guard** (the pager's guard removed: *no ring is established*); **RP-I7-merge's first attempt was a non-red-proof and is recorded as such** (the fixture's second layer was fully deleted by the row, so append and merge coincided -- the fixture gained a surviving interleaving pair and the mutation then failed it). **The fixtures:** a two-layer revision whose **same-pitch pairs cross** in tick space (record On@200/Off@800, overdub On@100/Off@300, a pinned Delete of the overdub note, 130 filler pairs so the record layer is over one slice) with the owner's shape asserted first, the **flat** answer asserted to *differ*, the gather driven call by call (resumption + per-call slice bound), production serving it (`Admitted`, published with a null root identity), and an **unpinned** Active row shown not to be applied; and the over-capacity shape (2,100 events) refused by the pager. **Not claimed:** the pin filter's state is unreachable through production (it is a safety property the fixture reaches by mutating the pass set); a revision with more layers than the callers' ceiling (`kRetainedGatherMaxSlices` = 18) cannot be gathered for a window (it refuses, or is routed to the pager which refuses it by name); multi-overdub layer order is pass-set order here and `mergeSequence` order in the owner's collector (unsettled for several overdubs); the pager implements **no** per-layer resolution; same-tick order stays 4.3's gate; the per-layer scratch's allocation is not admission-gated (DEC-115's follow-up list); no device leg, no timing claim, no `ExecutionBounds.md` row change, no `kMaxOpenNotes` change, no fallback removal (3.7b). Evidence: `evidence/2026-10-06-s37a-i7-layer-edit-resolution/`.

**3.7a increment I8 (the differential lane parity) landed 2026-10-06 (test-only; the task stays open, 3.7b's conditions
unchanged):** the ladder row the I1 packet named as **I4** -- *"the shipped-vs-fallback comparison over the same
revision"* -- and the task's own verify clause *"differential fallback parity"* are now **measured**. What the landed
increments had shown was always against the **effective-content owner** (`Loop::copyEffectiveCommittedEventsInRange`):
the rootless gather (I1/I2), its per-layer note-edit resolution (I7) and the paged lane's emission (I3c-c/I5). Nothing
compared the **other** enumeration of one revision: the **published snapshot root** -- the primitive the shipped window
build of `Track::requestPlaybackWindow` reads **and** the one the merged-window fallback reads
(`ensurePlaybackMergedMidiEventsBuilt` -> `Loop::tryGatherPublishedSnapshotEventsInWindow`) -- against the retained
lane's **pinned bodies**, which is exactly the compatibility statement 3.7b's fallback removal rests on. **Four
fixtures** (`W-37a-27`..`W-37a-30`) compare the two lanes on the events' **wire identity** (tick, type, channel, note,
velocity element-wise; `noteId`/`eventId`/`canonicalOrder` are each lane's own bookkeeping, and the root's published
`canonicalOrder` is made to coincide with tick order here, so **same-tick order stays 4.3's gate**) for a slot holding
**both** a current published root and a retained source for the same revision: they agree at the resolver's own two-bar
window, at **every** window of a seven-playhead sweep across a 24-bar loop (5 windows with content, 13 bars reached),
on an **edited** revision (I7's crossing revision with a current root carrying the post-edit content: both lanes drop
the deleted note and keep the surviving and record pairs), and -- the negative direction -- a revision **only one lane
holds** is answered by the lane that holds it while the other refuses **by name** (`RevisionMismatch`, 0 chunks
inspected, 0 events appended); through production the requester is `Admitted` and its output equals the root's, and the
**served** window's content equals the **retained** lane's answer. Verified: focused **170/170** (baseline 166/166),
full native **2,848 / 7 skipped / 2,841 succeeded / 0 failed** (baseline measured with the slice stashed:
2,844/2,837), capture build **SUCCESS** with the RAM1 line **byte-identical** (`variables 96,192`;
`code 392,248 + padding 968`; floor 840; no production line changed -- `git diff --stat -- src include` empty), strict
validation and `git diff --check` clean, and **three red-proofs RED with byte-identical restores**
(`src/Track/TrackPlaybackWindowBuild.cpp` sha256 `48c57b2fe9d7c78e…`): **RP-I8-window** (the gather answers the whole
loop: *Expected 4 Was 48* on W-37a-27/W-37a-28), **RP-I8-revision** (both layers of the revision authority dropped:
*Expected 4 Was 0* on W-37a-29 and on I1's own fixture) and **RP-I8-branch** (a retained source preferred over the root
that answers: *Expected 0 Was 5* on W-37a-29). **Two deviations recorded rather than reworded:** RP-I8-revision's
first form -- the gather's own source guard alone -- was a **non-red-proof**, because the interval index refuses the
foreign revision first (`queryRetainedIntervalForSlot`), the same layering I1's **MR-3.7a-B/B'** records, so the
mutation was re-expressed to drop both; and **RP-I8-branch does not fail W-37a-27**, which **corrects the packet's
prediction** -- the identity a window carries is the **loop's** published root whether or not that root was the
enumeration used, and the two lanes answer identical content by the parity itself, so the branch choice is
discriminated only where preferring the retained lane changes an outcome (a lagging source in front of a root that
could answer). **Not claimed:** no device leg, no timing claim, no `ExecutionBounds.md` row change, no fallback removal
(3.7b); the comparison is one revision at a time and only for a **current** root (the stale-root `Overlay` path is
3.6b's allowance, deliberately not compared); nothing about note/event identity, channel remapping or same-tick order;
the pager, the preview session, retained-source release and the demotion matrix stay their own tasks; and
`retainPlaybackSourceForSlot` still has **no production caller**, so this increment adds no retainer -- it establishes
that the lane *is* content-equivalent wherever a retained source exists. Evidence:
`evidence/2026-10-06-s37a-i8-lane-parity/`.

  **3.7a I4 G2 verification/closeout (2026-10-07; test/evidence only; no production or test-source changes).** The remaining parity/admission proof uses the existing production-path evidence without duplicating fixtures: I5's `test_3_7a_i5_a_rootless_demoted_slot_is_served_from_the_ring_pass_exact` compares emitted page output against the same revision's effective-content oracle; I8's four `W-37a-27`..`W-37a-30` cases compare retained-source results with the published-root/fallback lane; I4's `test_3_7a_i4_the_paged_delivery_names_a_halved_service_rate` names the half-rate miss. These establish native content/admission behavior only; they do **not** qualify device timing or authorize enabling the unqualified service envelope. **Verified this closeout:** focused `test_runtime_working_set_ownership` **176/176**; full native **2,855 / 7 skipped / 2,848 succeeded / 0 failed**; `teensy41-capture-serial` **SUCCESS**, RAM1 `variables 96,192`, `code 392,248 + padding 968`, **128 B** above the 840 B floor; strict OpenSpec validation valid; `git diff --check` clean; `git diff --stat -- src include test` empty. No upload/device observation. I4 packet/evidence: [`2026-10-07-s37a-i4-integrated-parity/EXECUTION.md`](evidence/2026-10-07-s37a-i4-integrated-parity/EXECUTION.md). Task **3.7a is complete with fallback retained**; 3.10/6.3/6.5 timing qualification, 3.14, 4.5 and 3.7b remain open and are not checked off here.

**3.12 increment I9 (fixture 2.11's last dimension) landed 2026-10-06 (firmware + tests; the task's remaining items
are the hardware envelope and 6.3's sweeps):** *"an edit that moves events into a full interval"* is measured, and the
fixtures found and fixed a **defect in the retained lane**. **The defect:** a slot can hold a **retiring** retained
source beside its **serving** one (3.6a's roles, bounded at two), and the lane's readers took "the **first retained**
entry" -- so `Track::gatherRetainedSourceWindowForSlot` refused the **current** revision with `RevisionMismatch`
(measured: `request=5`, `gather=4`, `lists=0`, nothing inspected), `buildRetainedIntervalIndexForSlot` indexed the
*previous* revision, and the two readers that compare a source's revision with the loop's could name the wrong holder.
**Fixed at the owner:** `LoopPlaybackRuntime::servingRetainedSource()` (serving role first, first retained as fallback),
the gather preferring `revision == expectedRevision` **first** (a lagging caller is still served the revision it asked
for -- the one-pass-late allowance stays the caller's rule, so I1's fixture still passes), the index built for the
**serving** revision, and `Track::servingRetainedRevisionForSlot` -- a **revision read**, not a source copy -- for the
readers. `retainedPlaybackSourceForSlot` keeps its first-retained meaning because **3.6a W-19** pins it; its header
comment's *"Read the **serving** retained source"* is recorded as a **doc/behaviour mismatch** rather than changed
under that fixture. **Two readings the measurement corrected:** a `Move` row (`Update` + `NoteRange`) **cannot** grow a
**windowed** interval -- `applyNoteEditPassSequence` applies a Delete/Update row only when its target note is present in
the layer it is applied to, and a windowed layer holds the window's events only (measured: both the owner and the lane
still answered 1,916), so the clause is measured with the edit that **puts** events into the interval (`Create` rows) --
and the window build's capacity bound is the view's **storage** (`sourceCount > kEventsPerView` = 2048), **not**
`content + closureReserve` (that reserve is the **capture edge's** term; an edited 1,922-event interval was admitted,
`Expected 10 Was 0`, and the refusal appears at 2,050). **Fixtures:** W-37a-31 (the served window is the owner's
post-edit answer -- same tick order, per tick as a multiset, never a truncated prefix -- and the interval's content term
is that answer), W-37a-32 (the edit moves the **capture-edge boundary**: the same content ±the edit reads 2,044 vs
2,048 on the edge's accounting, `admitsNewEvent(true)` true vs false, the control slot's note-on accepted and the edited
slot's refused **by name** with no row and no dangling reservation), W-37a-33 (past the capacity the lane **never
truncates**: the gather still serves the 2,050-event interval, the requester refuses **by name**
`WindowExceedsRetainedCapacity`, the served window stays the old **complete** answer, and the deferred demand stays
owed). Verified: focused **173/173** (baseline 170/170), full native **2,851 / 7 skipped / 2,844 succeeded / 0 failed**
(baseline 2,848/2,841), capture build **SUCCESS** with the RAM1 line **byte-identical** to the pre-slice baseline
(`variables 96,192`; `code 392,248 + padding 968`; floor 840) -- **the increment's own +256 B was paid back**: the first
working form copied a whole `RetainedPlaybackSource` at its call sites (`code 392,504 + padding 712`, refused) and the
**revision read** restored the line, **no re-base** -- strict validation and `git diff --check` clean, and **three
red-proofs RED with byte-identical restores**: **RP-I9-source-selection** (the defect as it was: W-37a-31/W-37a-33),
**RP-I9-move** (the pinned rows collected empty: I7's fixture, I8's and all three of this increment's) and
**RP-I9-window-term** (the edge's window term dropped: W-37a-32 plus 3.12c's two edge fixtures). **Not claimed:** no
device leg, no timing claim, no `ExecutionBounds.md` row change, no fallback removal (3.7b); the closure-at-capacity
arithmetic stays **3.12c**'s (cited) and the edited slot's own stop route is **not** claimed (its stop admission
returned false in this fixture's hand-started transport state, recorded unattributed); the capture accounting's owed
term is the **track's** `pendingNotes`, measured and named, not fixed; the declared caps stay 6.3's; and
`retainPlaybackSourceForSlot` still has **no production caller** (the readers are what this increment fixed). Evidence:
`evidence/2026-10-06-s312-211-edit-into-full-interval/`.

**3.13 increment I10 (fixture 2.12's `then save` clause) landed 2026-10-06 (test-only; the task stays open for the
display consumer):** fixture **2.12**'s last clause -- *a preview is never committed currency* -- is measured at the one
consumer that **writes**: the save. The structural halves were already shipped (both identities carry
`PlaybackWindowOrigin::Preview`, and the capture admission counts **committed** windows only, 3.7a/I3); what never
existed is a harness holding **both halves at once** -- the window lane (so a preview is really served) and the
storage entry points (so a save is really driven). `test_windowed_preview_integration` now links `StorageLoopIo.cpp` +
`StorageLoopIoBodyFormatV1.cpp` -- the increment's one enabling change -- **measured before the fixture** as needing
**no** additional stub (`off_path_deps.h` already stubs the SD path and the display routes the save touches), with the
suite's nine fixtures still passing. **Fixture W-2.12-save:** the session is opened **before** any root is published (a
root-current session rematerializes with an empty store and would have no preview content at all), mutated and served
through the lane; the slot's checkpoint is then published and the loop's queued publications are **settled**
(`Loop::hasSettledPublishedSnapshotForSave()`, DEC-073 RC2) exactly as the storage harness does before a save -- which
is why the fixture reads the checkpoint **after** the settle instead of assuming which root survived (measured: the
publisher's own root carries **20** events, against the **8** the synthetic root carried and the session's projection).
It asserts: the served window is the session's projection, element-wise; `committedPlaybackWindowServesSlot == false`
and `committedPlaybackWindowEventCountForSlot == 0` while the preview serves; `writeLoopPersisted` returns true, the
body reads back `ok` and **not** `legacy`, its embedded snapshot's content equal to the **checkpoint's** and different
from the **projection's**; and after the save the preview is still the served window, at its own revision, with
`origin == Preview`. **The rule's nature, stated rather than implied:** the save's source selection is
`loop.publishedSnapshot()` plus the pass set -- it has **no preview input at all** -- so "a preview is never saved"
holds **by construction** at that layer, and the two rules whose failure *would* let a preview reach a save (the
committed-currency predicate and the body-format/source selection) are what this increment red-proves. Verified:
focused **10/10** (baseline 9/9), full native **2,852 / 7 skipped / 2,845 succeeded / 0 failed** (baseline 2,851/2,844),
capture build **SUCCESS** with the RAM1 line **byte-identical** (test-only) -- strict validation and `git diff --check`
clean, `git diff --stat -- src include` **empty** -- and **two red-proofs RED with byte-identical restores**:
**RP-I10-origin** (`committedServedPlaybackWindowView` ignores the window's origin: *"a preview window is not committed
content"*) and **RP-I10-route** (`loopUsesLoopSnapshotBodyFormatV1ForSave` returns false: *"the written body reads
back"*); a mutation making the save *read the preview* is **not expressible** (no preview input) and is named rather
than faked. **Not claimed:** no device leg, no timing claim, no format, admission or identity change, no
`ExecutionBounds.md` row change, no fallback removal (3.7b); the fixture does not release the snapshot the reader
returns (the storage suite's `releasePersistedLoopSnapshotChunks` is not linked here -- a native leak, recorded rather
than hidden); and 3.13's **other** owed item -- the display consumer of a preview window -- is untouched. The packet
records one deviation rather than smoothing it: the harness probe and the first fixture draft preceded it. Evidence:
`evidence/2026-10-06-s313-212-save-clause/` with its `EXECUTION.md`.
**Reconciliation (2026-10-06, documentation-only, no code):** that remaining item -- 3.13's **display consumer of a
preview window** -- stays **open** on an explicit cross-change dependency on `runtime-working-set-ownership` **S4e**
(the display rework), **not** on 3.7b's fallback removal; the producer, the consumer, the integration owner and both
requirements are recorded in [`DISPLAY-CONSUMER-RECONCILIATION.md`](DISPLAY-CONSUMER-RECONCILIATION.md).

  **Closeout of I3c-a's owed red-proofs (2026-10-06, docs-only commit).** The packet's MR set was written against the
  staging design this slice replaced with the ring's ordinal view, so it was re-expressed and run against the final
  revision, with byte-identical restores verified on all three touched files: **MR-R-baserebase** (the pass base not
  moved at a boundary) **RED** (*Expected 48 Was 0*); **MR-S-mapping** (the per-pass mapping not applied) **NOT RED** --
  recorded as a non-red-proof with its mechanism named (the walk's frame break bounds the damage; the load-bearing bound
  is the content clamp, red-proved as **MR-I3c-c-1**); **MR-T-predicate** **RED** (*Expected 3 Was 4*); **MR-U-rearm**
  (the re-arm releases the ring) **RED** (*Expected 2 Was 1*); **MR-S as written** (a per-event refill) **inapplicable**
  -- there is no refill; and **MR-V** (selection unconditional, added here) **RED by abort** (the run stops at 17 cases:
  an unbound stream is selected and `isCursorValid` fails). This **closes the "MR-R/S/T/U owed" line above**; the only
  remaining owed item is I3c's end-to-end emission assertion on a real transport tick (I4/4.3's differential parity).
  A process incident is recorded in the evidence: the first closeout harness's restores silently failed (an unresolved
  scratch path) and left the TU mutated; it was restored from git and the corrected harness restores via
  `git checkout --` with three sha256 checks per mutation. Detail:
  [`evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md`](evidence/2026-10-06-s37a-i3c-emission-adapter/EVIDENCE.md).

  **Increment I2 landed 2026-10-06 (firmware + tests; the task stays open).** The deferred lane's **production turn**
  now serves a demand for a slot with no current published root — the shape the device legs measured as
  `launch_requested … root=0 …` never reaching readiness. `LoopPlaybackRuntime` owns one **retained-source gather**
  per slot (phase, resumable `CommittedEventRange::WindowTraversalCursor`, the gathered window in external memory, the
  slice/chunk counters, and the **named refusal** that left the demand owed); `Track::stepCommittedPlaybackRetry`
  freezes the demand exactly as the root-backed qualification does, serves **one bounded page per turn** through I1's
  `gatherRetainedSourceWindowForSlot`, and on the completing page admits the gathered window through
  `requestPlaybackViewMaintenance` — the same production admission a requester uses. The demand's **own** promise is
  validated first (geometry/epoch/deadline) and a lost position is named and coalesced back rather than re-granted.
  `Track::playbackWindowSourceIsCurrentForSlot` states the window lane's source authority in one predicate (current
  root **or** the retained revision the loop currently carries) and is applied at `committedPlaybackRetryCanProgress`,
  the frozen-request validation, the turn's launch-preparation check, `playbackLaunchReadyForSlot`,
  `rollingCandidateReadyAtBoundary` and `revalidateCompletedPlaybackWindow`; a launch demand served this way gets the
  launch lane's own bookkeeping and reaches `Ready`/`Bound` with no root. **Verified:** focused **149/149**, full native
  **2,820 / 7 skipped / 2,813 succeeded / 0 failed**, capture build **SUCCESS** with `variables 96,192` unchanged and
  `code 391,832 + padding 1,384` (floor 840; I2's own **+64 B** of ITCM), both `LoopPlaybackRuntime` size pins re-based
  with **compiler-measured** figures (target 7,160 / host 9,216), strict validation and `git diff --check` clean, four
  mutations red-proved with byte-identical restores. **Recorded, not smoothed over:** MR-3.7a-H's first run *crashed*
  the suite (132 of 150 cases reported) because the fixture dereferenced a record after a non-fatal Unity assertion —
  the fixture was made crash-safe and the mutation then failed that one case cleanly; and the first fixture draft
  expected one page for an interval that completes in one, so the fixture now uses a 200-event dense bar.
  **Not claimed:** no paged (over-L) serving, no preview hooks or per-layer edit resolution (I3), no
  edited-content/long-window/same-tick parity (I4/4.3), no timing envelope, **no device run** (the rootless launch
  *binding* on hardware stays owed to 6.4/6.5), no fallback removal (3.7b). Detail:
  [`evidence/2026-10-06-s37a-i2-deferred-lane/EVIDENCE.md`](evidence/2026-10-06-s37a-i2-deferred-lane/EVIDENCE.md).
**3.7a increment I9 (task 4.5's moving closure) landed 2026-10-06 (firmware + tests; both tasks stay open).** 4.5's last
named implementation item — I6 closed the ahead side and recorded *"a closure that slid **behind** the re-anchored cursor
is out of scope and still owed"* — is measured and fixed on the serving handoff. The walk resumes from a **floor**
(`reanchorPlaybackIndex` points the cursor at the first event past `loop.lastTickInLoop`; the prepared pair resolves the
identical term as its `publicationPhase`) and `advancePlaybackCursor` skips everything at or behind it, so a sounding the
replacement **kept** while moving its NoteOff behind that floor was neither planned (its NoteOn is present) nor reachable:
it stayed audible past the end its own revision gives it and the wrap re-articulated it. The plan is now **one predicate
in one scan** (`ActiveNoteLedger::HandoffClosurePlan` + `planHandoffClosures`, renamed from
`MissingCommittedNoteOnPlan`): a tagged open Entry is planned when its committed NoteOn is absent **or** when the incoming
stream's own NoteOff for it lies at or behind `servedThroughPhase` — and never from the absence of a NoteOff (a
never-ending sounding says nothing). `Track::playbackClosureServeFloor` supplies that phase in the walk's own terms
(`loop.lastTickInLoop`, `UINT32_MAX` when there is no position yet **or** the playhead wrapped backward, where the pass
restarts and every closure is reachable), threaded from all **four** reconcile call sites; the wire half keeps I6's edge,
order and handoff-tick stamp (`closeHandoffInvalidatedNotes` → `closePlannedHandoffClosures`). **Measured** on the
production clock entry: the slid closure is the handoff turn's **first wire event** at the handoff's own tick, discharged
by identity; a closure the walk will still deliver is left alone and arrives once at its own tick; a never-ending
sounding is never silenced; an untagged Entry survives; every closure precedes the replacement's note-ons; a handoff on a
**restarting** pass closes nothing, and the restarted pass then delivers each closure exactly once. Verified: focused
**175/175** (baseline 174/174), full native **2,854 / 7 skipped / 2,847 succeeded / 0 failed** (baseline 2,853/7/2,846
measured with the slice stashed), capture build **SUCCESS** with the RAM1 line **byte-identical A/B** (`variables 96,192`;
`code 392,248 + padding 968`; floor 840) — **no ITCM cost, no re-base** —, strict validation and `git diff --check`
clean, and **four red-proofs RED with byte-identical restores** (RP-A the closure half dropped, RP-B the restart clause
dropped, RP-B' the floor dropped, RP-C the no-closure guard dropped; the packet's single RP-B prediction is recorded as
two mutations). **Recorded, not smoothed:** the fixture's first draft used the long-loop length and measured nothing —
a long slot's merged store is a **two-bar slice** and the reconcile returns early — which is itself the reach limit this
task now states (the demoted long-loop serving handoff is the windowed lane's, 3.7a/3.14). **Not claimed:** no device
leg, no timing claim, no `ExecutionBounds.md` row change, no fallback removal (3.7b); the off-clock call sites are
covered natively only; 4.5's **displayed provenance** (display → RWO S4e) remains owed -- the other item this note listed,
**eventual recovery through the real consumer**, was delivered by I10 (the adoption boundary is `advancePlaybackLagTraversal`'s
production caller). Detail:
[`evidence/2026-10-06-s45c-moving-closure/EVIDENCE.md`](evidence/2026-10-06-s45c-moving-closure/EVIDENCE.md) with its
[`EXECUTION.md`](evidence/2026-10-06-s45c-moving-closure/EXECUTION.md) packet.
**3.7a increment I10 (task 4.5's recovery half) landed 2026-10-06 (firmware + tests; both tasks stay open).** The
containment policy was implemented and measured long ago, but `Track::advancePlaybackLagTraversal` had **no production
caller at all**: nothing cleared a latched deadline-contract fault (its only clear site is that function's `Current`
branch), so a fault latched through the one route that *was* wired (an admitted promise past its deadline,
`serviceAdmittedDeadlineMisses` -> `noteMissedTraversalForSlot`) was permanent. The lane's serving resolver now owns the
**one** event that can end such an episode: the **adoption boundary** of `Track::resolveRollingPlaybackWindow` -- the
eligible boundary, where the boundary may serve the current revision because the prepared replacement was just adopted --
calls `advancePlaybackLagTraversal(slot, frame.startPhase, span, loop.playbackRevision, /*replacementReady=*/true)`, whose
`Current` branch ends the episode and clears the fault. **Measured** through the lane's real path (a demand opened by
`Track::serviceRollingPlaybackWindow`, then `stepPlaybackViewMaintenance` -> the resolver, driven by the suite's
`rollingTestTick`): a latched fault is cleared by a real adoption, the episode counter returns to zero, the served window is
untouched and nothing is closed or silenced -- recovery through the real consumer, not a reset. Verified: focused **176/176**
(baseline 175/175), full native **2,855 / 7 skipped / 2,848 succeeded / 0 failed** (baseline 2,854/7/2,847), default capture
build **SUCCESS** with the RAM1 line **byte-identical** (the call is behind `PLAYBACK_WINDOW_ROLLING_ENABLED`, which that
image keeps at 0 -- so the shipping image is unchanged by construction and by measurement), the lane-enabled qualification
image measured at **exactly its pre-slice baseline** (`code 393,240 + padding 32,744`; already over its own budget since I5,
so it cannot be flashed and nothing device-level is claimed), strict validation and `git diff --check` clean, and the
red-proof **RP-A RED with a byte-identical restore** (the adoption judgement removed -- *"the eligible boundary is the
production caller that clears the latched fault"*).
**Withdrawn on independent review, and recorded rather than smoothed:** the first form of this slice (commit `eefd90a5`)
also judged the **expiry** and applied the **last resort** at the resolver's three `Serving`-phase rejections. The review
falsified it, and the findings are kept: **(F1, verified) wrong silence** -- those rejections are precisely the turns the
**retained merged fallback** serves the slot, so applying the last resort there closes a sounding another serving route is
about to play; **(F4, verified) wrong interval** -- at the expiry the phase has advanced a whole span past `frame.startPhase`,
so the call named the interval the frame had already served (at worst one pass late) instead of the traversal going unserved
-- a different episode, and `PlaybackLagState` resets the episode when the interval changes; **(F3)** the expiry call could
also resolve `Current` and clear, so "the adoption is the only clear site" was false while it existed; **(F2/F5)** the
containment sites' reachability is not established -- `containmentForSlot`'s "no valid coverage" is defined on the
**published window**, which is not this resolver's rejection condition, and only one of the three sites is reachable at all;
**(F6)** the fixture manufactured a state the publication path cannot reach (`loop.playbackRevision + 1` as the desired
revision). The withdrawn calls had cost **+144 B of ITCM** in the lane-enabled image, and withdrawing them returned that
image to its baseline byte-for-byte. The last resort needs an owner-level "no serving route at all" predicate (or 3.7b's
fallback removal) before it can be wired. **Not claimed:** no device leg, no timing claim, no `ExecutionBounds.md` row
change, no fallback removal (3.7b); **`containmentForSlot` / `applyLastResortForSlot` still have no production caller**, so
the approved exception remains unreachable; `serviceAdmittedDeadlineMisses` is unchanged, and the promise route **shares**
the episode counter the adoption resets (the withdrawn draft's "the promise route keeps its own count" was wrong -- its
per-promise count is its own, the episode state is not). **Recorded, not smoothed (process):** the first fixture reused the
S4b.7a harness and produced a *trailing* harness failure plus an ERRORED suite in every arrangement tried (including with
the slice stashed), so the landed fixture is self-contained; and two pre-existing `RUN_TEST` entries that an over-wide cut
had removed were caught by diffing the entry list against `HEAD` and restored. Detail:
[`evidence/2026-10-06-s45e-lane-containment-recovery/EVIDENCE.md`](evidence/2026-10-06-s45e-lane-containment-recovery/EVIDENCE.md)
with its [`EXECUTION.md`](evidence/2026-10-06-s45e-lane-containment-recovery/EXECUTION.md) packet.

- [x] 3.7c **The active slot's first window** — a demoted slot that reaches playback **without** a switch (a freshly
  recorded body, or a restored one) SHALL be able to obtain its first window: the lane's own request SHALL not require a
  window to be already served. Verify the request is opened through the existing positioned demand (not a new owner, not
  a new scheduler, not the launch path), that the off-clock owner builds and publishes it, and that the slot is then
  servable — with a named refusal when the slot has nothing to build from or nowhere to publish.
  *Landed 2026-10-07 (firmware + tests) — the gap the authorized device run measured.* The device
  (`captures/s64-demand-01`) showed every demoted, span-aligned, playing slot leaving the lane as
  **`win_not_servable` with `sv=0`**, including where its root **was** current (`rt=1`) and its coverage role free
  (`rl=1`): the demand's entry required a window that was already *served*, and only a switch's launch could produce
  one, so a slot that never switched could never be served from a window at all — against the spec's own first
  requirement (*the demoted case is served by interval publication*; playback SHALL NOT require whole-loop publication).
  **The change:** in `Track::serviceRollingPlaybackWindow` the *served* window is no longer a precondition of
  *requesting* one — what still refuses (`win_not_servable`) is having no view set to publish into, or no source to
  build from (`playbackWindowSourceIsCurrentForSlot`: a current root **or** the retained revision the loop carries).
  Everything else is the existing machinery, unchanged: the same positioned interval the roll requests, the same
  admission judging its lead, the same off-clock owner building it, the completed build publishing as the **ready**
  successor and the resolver adopting it at its own boundary; the merged fallback still covers the traversals in
  between. No new owner, scheduler, authority or state; no launch change; no constant touched.
  *Verification:* the new production-path fixture
  `test_3_7c_an_active_slot_with_no_served_window_requests_its_first_window_and_is_served` drives the shape the device
  measured (demoted, span-aligned, playing, current root, **nothing served**) and asserts the request is opened
  (`Requested` + the `win_requested` row + a retained demand carrying the two-bar interval), that the owner builds it
  into the ready successor, and that preparation is not yet serving (the boundary adoption is the resolver's own step,
  covered by the serving fixtures). **Red-proved:** restoring the old guard makes that fixture — and only it — fail
  (`Expected 3 Was 1`: `Requested` vs `Excluded`, i.e. the device's own refusal), with the file restored
  **byte-identically** (`sha256 a59ddacb8200db2b…` identical before and after). Focused **177/177** (was 176).
  *Not claimed:* no device leg of this slice yet (the run that follows it is recorded separately), no timing or
  service-envelope claim, no `ExecutionBounds.md` row change, and 3.7b stays the final ladder stage.
  Detail: [`2026-10-07-s37c-active-slot-first-window/EXECUTION.md`](evidence/2026-10-07-s37c-active-slot-first-window/EXECUTION.md).
- [ ] 3.7b **Remove the legacy playback emission branches — final ladder stage (4)** only after 3.7a and **3.15a–f — single-path migration** establish production use for every supported short/long, Record, overdub, NOTE_EDIT, layered/jam, switch and restored-density route; tasks 3.12, 3.13, 3.14 and 4.5, F-7/F-8, service/timing and compatibility gates also remain required. Retire mutable merged, pair, fixed/bridge and separate paged **emission** selections without truncating accepted content; retain a cold parity oracle. Verify no supported slot reaches an old reader and reclassify `ExecutionBounds.md` row 2 only with evidence. Each temporary branch has removal trigger **3.7b**, not permanent authority; rollback before this stage retains today's serving route.
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

- [ ] 3.7d **Name the invalidation term on the blocked-demand row** — the `win_blocked_by_request` exit SHALL carry the
  term that stopped the retained demand (`PlaybackViewPendingReason`), so a device run names it instead of leaving the
  five candidates the retry lane's validation can produce (a changed loop length, a changed clock epoch, a passed
  admitted position, a passed deadline, or a source that is no longer current). **Prepared and deliberately not
  landed 2026-10-07:** the change, the host-side decode and its test were written (host **40/40** green) but the native
  row-contract fixture did not go green in three attempts — the lane reported `Waiting` where the blocked row is
  expected, because the record the resolver reads was not active-and-latched at that point — and a firmware change does
  not land without a green production-path fixture and a red-proof, so everything was reverted and the tree left
  byte-identical to `117b9a61`. The change as written, the ordinals to pin, the three attempts and the exact next
  diagnostic are recorded in
  [`2026-10-07-s37d-invalidation-term/PREPARED.md`](evidence/2026-10-07-s37d-invalidation-term/PREPARED.md).

  ***Authorized device run of the slice 2026-10-07* (`captures/s64-37c-01`; image `5786cdb23cf22623…`, git `1a61a6a9`;
  the same leg, target and invocation as `s64-demand-01`, so the slice is the only variable): the change is visible on
  hardware, on the active slot.** The same state that used to end the lane — demoted, span-aligned, playing, root
  current, **nothing served** — now reports
  `t=7,s=3,x=win_requested,len=18432,ep=1,av=1,al=1,pl=1,ed=0,od=0,pv=0,rw=0,sv=0,rt=1` where the previous run reported
  `x=win_not_servable … sv=0 rt=1 rl=1`; the frame then moves through `win_waiting` / `win_blocked` /
  `win_blocked_by_request` instead of stopping at a refusal, and the remaining refusal is the one that should remain
  (`win_not_servable` with `rt=0`, before the root is current). The lane **served** the demand
  (`win_gap elig=4 prog=3 denied=1`, and `elig=1 prog=1`) and the readiness receipt names the holder that still owes the
  build (`win_readiness,7,4,owes_build,window_build_owner,1,0`); the switch target's launch chain also went further than
  before (`launch_requested` → `launch_requested_match` → `launch_building_match` → `launch_ready` →
  `launch_ready_owns_source`). **Not claimed:** no `build_complete` publication for the active slot appeared in this
  run, so the slot is **not yet served from a window on hardware** — the device claim is exactly that the request is
  opened where it used to be refused; the next measured question is now *why a first window's build does not complete
  within the service the leg gives it* (the `denied=1` turn and the `window_build_owner` receipt are the surface, and
  the launch lane's slow-publication family from `s64-trace-01`/`s64-progress-01` is the same question). Detail:
  [`2026-10-07-s37c-device-run/DEVICE-RUN.md`](evidence/2026-10-07-s37c-device-run/DEVICE-RUN.md).
  (`DeferredIdleOwner`, `include/Utils/MidiServiceDrain.h:13`) — this change added a *value* (`PlaybackViewMaintenance`,
  task 3.1, on the `SlotProvision` precedent), the dispatch rides the existing frame (`src/main.cpp:463`), eligibility
  is state-driven (`playbackViewMaintenanceNeeded`), and the lane has no queue, timer, thread or request record of its
  own — i.e. **no second publication scheduler** exists. *No code changed*, so the verification is the last
  full-suite/build state (focused 54/54, full native 2685 / 2678 succeeded / 7 skipped / 0 failed,
  `teensy41-capture-serial` SUCCESS) and no red-proof applies to a note; the behavioural half — fair service among
  holders, no starvation — stays **task 3.10's**.
- [x] 3.9 Deliver readiness receipts by the two delivery rules — verify a receipt produced under transport reaches the
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


  *Increment 3.9 (increment 5) landed 2026-10-09 — **the emission site is the whole lane's, not one branch's: the frozen
  Record-return and live first-Record Capture completions name themselves**. A device leg on the pinned qualification image
  showed why the row's absence was being read as a lane failure: `DIAG,win_readiness` **0** rows and
  `DIAG,playback_build,…,build_complete` **0** rows in a 1.1 MB capture whose final `RING,stats` reports no Tier-A eviction
  (`tierA_evict=0,overflow=0,crit_rej=0`), while the run's own gate rows show the Record-return slot with a complete
  **published** window (`sv=1`) and a request/build cycle alternating every turn for ~36 s of transport. Both emitters had
  exactly one caller: the coverage grain tail of `Track::stepPlaybackViewMaintenance`. Two call sites now reach them from the
  branches that can complete a window without that tail — the live first-Record Capture candidate and the frozen
  Record-return successor — and the frozen branch's own refusal offers the cadence-bounded receipt alone (no path row: a
  refusal repeats every turn and a row per refusal is the flood the cadence exists to prevent). No new API, no new state, no
  behavior change. Verified: `test_shared_capture_finalization` **79/79** (three new assertions, incl. the new
  `test_frozen_record_return_terminals_report_the_slots_readiness_receipt`), `test_runtime_working_set_ownership`
  **187/187** unflagged and **190 succeeded / 1 pre-existing expected RED** flagged, full native **2,891 cases / 2,884
  succeeded / 7 skipped / 0 failed**, both capture builds SUCCESS with RAM1 **`96,192 / 390,360 + 2,856`** byte-identical to
  the pin, strict validation and diff check clean; red-proofs M1–M3 with byte-identical restores turn exactly the new
  assertions RED. **Device verification NOT RUN** (needs an owner-authorized qualification upload and a registered leg).
  **Task 3.9 stays open** on the declared interval and its hardware half. Packet:
  [`E2 — lane terminal-outcome rows`](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#lane-terminal-outcome-rows-for-the-frozen-record-return-and-live-capture-completions-2026-10-09-g2-execution-packet--task-39-receiptrow-half).*
  *Increment 3.9 (increment 6) landed 2026-10-09 — **the frozen completion names its own window, and the Snapshot publication is confirmed after its
  step**. The device leg behind increment 5 left two evidence gaps that a row's mere presence could not close. (A) The receipt is produced by one
  process-wide counter at interval 16 and reports the slot's **committed** readiness — for a first Record take `not_servable`, because
  `playbackWindowReadinessForSlot` exits on `!loop.hasCommittedPasses()` before a frozen Capture ready can be named — so run 2's single row could not
  distinguish a completed frozen successor from "nothing to serve". The frozen branch's completion terminal now also emits its own **Tier-A** family,
  `#CAP,<us>,DIAG,win_frozen_readiness,<t>,<s>,complete_ready,clock_edge,<rv>,<lead>,<sourceGen>,<captureToken>,<start>,<len>,<loopLen>`, through
  `Track::reportFrozenRecordReadinessReceipt` → `DebugSessionCapture::frozenReadinessReceiptImmediate` (direct-when-room, ring fallback; the capture
  token in full, `%llu`). It is **event-shaped with no cadence counter at all**: the existing `!wasReady` guard *is* the completion transition, so it is
  reached once per completed frozen window — the bound `build_complete` already has — and it cannot be starved by the busy `win_readiness` phase. That is
  a deliberate deviation from "extend the existing `DIAG,win_readiness` row": a separate family leaves the existing parser, its 6-field shape and its
  Tier-A boundary untouched, and `test_capture_line_tier` pins the new prefix's boundary and non-collision in both directions. `windowed_lane.py`'s
  `readiness_receipts_under_transport` now **requires** a source-qualified row under transport (nonzero token, valid interval), so a generic
  `not_servable` alone fails. (B) `record_snapshot.verify_record_snapshot_progress`'s `record_session_exit_root_published` now counts only a settled row
  that comes **after** the last row that still owed the session exit (`post_step_confirmed`/`owed_index` in the detail), so a publication that was the
  final owner turn with no row after it fails instead of passing on a restatement of the demand; and `SnapshotGateLastState`'s change key now includes
  `publishedRevision`, so a revision change is **always** named even when the pre- and post-step verdict bits coincide. Fixture first:
  `test_frozen_record_completion_emits_source_qualified_receipt` (real two-bar first Record, real `Track::stopRecording`, real Track owner) was RED at
  `FrozenReadinessRows::count == 0` before the emitter. Verified: ownership 187/187, line tier 20/20, flagged finalization **90 passed / 1 failed** (the
  pre-existing open `CannotCompleteBeforeHandoff` root-current RED — a *missing* completion, structurally unreachable by an added emitter), full native
  **2,892 cases / 7 skipped / 2,885 passed / 0 failed**, host verifier suite **50 tests OK**, both active specs strict-valid, diff check clean; both
  capture builds SUCCESS with RAM1 **`96,192 / 390,360 + 2,856`** byte-identical to the pin. The new literals first snapped the budget by 4 KB
  (`variables 96,192 → 100,288`, ITCM `code` unchanged); routing the ring-path format via file-scope `PROGMEM` and the tier tag via
  `CaptureLineTier::frozenReadinessTagPrefix()` restored it **exactly, with no re-base and no linker carve-out** (the wider
  `*DebugSessionCapture*.o(.rodata.str* …)` carve-out overshot to `92,096` and was reverted). **Device verification NOT RUN** (the
  `captures/.device-shutdown-incomplete.json` latch is untouched and still blocks a leg), so the tightened lane requirement is unexercised against a real
  capture. **Task 3.9 stays open** on the declared interval and its hardware half. Packet:
  [`2026-10-09 receipt/snapshot gaps`](evidence/2026-10-09-receipt-snapshot-gaps/EXECUTION.md).
  *Increment 3.9 (increment 7) landed 2026-10-09 — **the hardware half: the source-qualified receipt and the post-step
  Snapshot confirmation are read on a real capture**. The preceding increment left the tightened verifier requirement
  unexercised because `captures/.device-shutdown-incomplete.json` blocked an upload; this session discharged that latch
  **deliberately** (a non-reset attach, a fenced read-only `!CAP_IDENTITY_STATS` reply, then two idempotent-MIDI-Stop ->
  verified-persistence-idle cycles; the payload preserved and the clear recorded, then `clear_latch`), rebuilt and pinned
  the qualification image (default and qualification builds SUCCESS, RAM1 **`96,192 / 390,360 + 2,856`** exactly the
  approved budget, HEX SHA-256 `43663800…`), uploaded it, drained the boot's own deferred save, and ran the registered
  `windowed_content_parity` leg on the authorized track 8 / slot 4. **Combined verifier `ok: true`, exit 0** with every
  check green: `readiness_receipts_under_transport` **`source_qualified=8 of 8 frozen_receipts_under_transport`**
  (eight `DIAG,win_frozen_readiness,7,3,complete_ready,clock_edge,…` rows, token 1154, intervals walking
  0+768/768+768/1536+768/2304+768/3072+768/3840+768 over loopLen 19200, revisions 0->1),
  `window_boundaries_crossed_under_transport` **8**, coverage **609** blocks under transport at `permitted=64`,
  **no overrun** (run 3's single `win_gap_overrun,64,1,69,1,10` did not recur), ring intact
  (`tierA_evict=0,overflow=0,crit_rej=0` with non-Tier-A `shed=66`), and the post-step
  `record_session_exit_root_published` (`record_rv=0 -> published_rv=1, post_step_confirmed=True, owed_index=8667`,
  the settled `p=0,sep=0,fc=0,hp=1,rv=1` row 125 us after the publish). The cadence-bounded `win_readiness` row appeared
  **once in the whole capture and after the transport stopped** (`under_transport=0`), which is exactly the reading that
  made run 2 look like a pass and is why the evidence surface fences on the event-shaped frozen family. Full native
  **2,892 / 7 skipped / 2,885 succeeded / 0 failed**, host verifier suite **50 tests OK**, both builds SUCCESS at the pin,
  strict validation and `git diff --check` clean. **Task 3.9 is complete.** Not claimed: no timing/envelope claim, no
  scheduler change, no 3.10 attribution, no all-route reader (3.15a-f) and no legacy-emitter retirement (3.7b), and the
  declared interval (16) remains a declared choice. Packet:
  [`2026-10-09 3.9 hardware receipts`](evidence/2026-10-09-s39-hardware-receipts/EXECUTION.md).*
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
  **2026-10-09 — NOTE_EDIT entry competitor dependency (planning only).** Runtime
  [S7.5b.8 — MIDI-separated NOTE_EDIT entry and focus preparation](../runtime-working-set-ownership/tasks.md#s75b8-note-edit-midi-boundaries)
  now owns bounded open/reopen, selection/focus and feedback preparation/execution across input-service
  boundaries. Include that competitor's individual costs and composed gaps in 3.10 — bounded-service
  qualification; 3.15d — NOTE_EDIT reader migration and content parity do not prove entry-time MIDI safety.
  Same-gesture maxima of 51/69 ticks are correlation evidence only, not proof of a scheduler/worker cause
  or grounds to raise the 64-tick declaration. Reuse runtime evidence rather than add an edit owner here;
  no checkbox, scheduler policy, firmware route or existing service limit changes in this capture.
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
  *2026-10-08 within-Track tie increment: `Track::stepPlaybackViewMaintenance` now persists its production `nullptr` tie cursor per Track in DMAMEM without changing the RAM1 line, explicit cursor semantics or the cross-track selector. The registered two-slot, equal-urgency owner fixture failed before the edit at the second holder's unserved Fill grain; a reset-to-zero mutation reproduced RED and was restored. Focused 455/455, full native 2,867 passed / 7 skipped, default capture build padding 904, strict active-spec validation and diff check passed; independent focused review found no concrete defect. The contention sweep now permits either initial tie phase while retaining exact whole-track drain and admission assertions. **Task stays open:** this does not measure positioned per-slot or composed source/window-owner gaps, prevent starvation of the other deferred owners, qualify a device deadline or certify all-route cutover. Detail: [`3.10 — within-Track equal-urgency packet`](evidence/2026-10-08-s315bc-short-loop/EXECUTION.md#310--within-track-equal-urgency-service-fixture-first-packet-2026-10-08).*
  *2026-10-08 test-only positioned eight-slot increment: the existing production-owner contention sweep now records each admitted slot's first actual Fill-grain transition. Eight simultaneous equal-urgency positioned `(track 0, slot)` obligations receive one grain apiece in eight granted playback-owner turns; all publish inside the derived exact total drain without missed deadlines. Resetting the persistent tie cursor makes the new first-service assertion RED and was restored. **Still open:** the seven-way outer owner-grant gap, genuinely needed source-owner turns and their affected-tick composition, blocked-holder and finalization/persistence interleavings, hardware timing and route cutover. This conditional logical measurement does not check off 3.10. Detail: [`3.10 — positioned eight-slot measurement`](evidence/2026-10-08-s315bc-short-loop/EXECUTION.md#310--positioned-eight-slot-first-service-measurement-2026-10-08).*
  *2026-10-08 Record grant fixture **corrected**: the earlier flagged “two-bar” RED was a one-bar loop produced by `RecordStopLength::computeRecordStopLengthTicks` (Off@864 fell within the 128-tick bar-2 grace), so its bar-2 role/handoff diagnosis was invalid and **must not drive a firmware retirement edit**. With a real two-bar stop (Off@1008), a registered production-stop fixture proves the already sealed job-owned range prepares complete bar-2 coverage through Track window-owner turns while the deferred stop owner and whole-root Snapshot are withheld; exact source MIDI rows, unchanged first-bar physical current role/storage/revision and a required-window-grant RED mutation are pinned. Focused 457/457 before the last role pin; final full native 2,869 passed / 7 skipped after that pin; strict validations and diff check pass. E4's genuinely needed source-owner case and the composed affected-tick bound remain open; no 3.10 checkoff. Detail: [`3.10 / E4 — Record grant geometry correction`](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#2026-10-08-correction--one-bar-fixture-was-not-a-record-handoff-defect).*
  *2026-10-08 required-source sibling case: a 24-bar committed slot with no current root or retained source keeps its `[0,1536)` request pending under 16 window-owner turns (`AwaitingSourceQualification`). Seventeen measured production Track Snapshot turns publish its complete root; six subsequent Track window-owner turns publish the first complete `current` with exact four-row interval content. Withholding either owner separately turns the case RED; restored full native 2,870 passed / 7 skipped. This proves a genuinely required Snapshot source owner for the **no-retention committed route**, not E4's frozen Record source adapter, new-reader clock-edge output, the outer seven-owner gap or an affected-tick deadline; 3.10 stays unchecked. Detail: [`3.10 — needed Snapshot grant`](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#310--rootless-committed-source-that-actually-needs-snapshot-2026-10-08).*
  *2026-10-08 composed outer-rotation sibling: the same rootless no-retention committed source is requested at coherent bar-4 tick 3,072 against its own production deadline tick 18,432. The existing `selectDeferredIdleOwner` with Snapshot backlog, real Track Snapshot/window owners and the across-track holder selector publish source at tick 7,168 and exact first current at 9,792 under declared per-opportunity advancement. Separate Snapshot/window dispatch suppression mutations reach deadline RED; other owner kinds retain selected opportunities, **not real pending persistence/display work**. Full identity/MIDI-byte review corrections and rerun native **2,871 passed / 7 skipped** are recorded. This one-shape native composed-grant result is neither a hardware maximum-gap guarantee nor E4's frozen Record/new-reader route; 3.10 remains unchecked. Detail: [`3.10 — composed outer rotation`](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#310--composed-outer-owner-rotation-source-before-window-2026-10-08).*
  *2026-10-08 cross-track finalization reachability repair: the `hasRealtimeDeferredIdleWork` gate omitted an accepted stopped `CaptureStopJob`, so under another PLAYING Track the ordinary deferred scan skipped its stop owner. A production-stop native fixture was RED at that predicate before change. Track now reuses its existing `hasPendingDeferredOverdubStop()` term for eligibility; 28 actual Track 1 stop grains advanced `ReservedClosure → CanonicalOrder` while Track 0's same Snapshot/window demand kept tick 7,168/9,792 publication and exact content before its own 18,432 deadline. Suppressing only those stop grants turns finalization progress RED and was restored. Full native 2,872 passed / 7 skipped, default capture RAM1 padding 904 unchanged, one focused review no concrete defect. This is reachability, **not** full stop completion, device worst-gap or real persistence/display contention; task 3.10 stays open. Detail: [`3.10 — cross-track finalization`](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#310--simultaneous-cross-track-record-finalization-2026-10-08).*
  *2026-10-08 stopped RecordPublished background-root handoff: cross-track continuation reached a pending queued Snapshot needed to retire the stopped Record job while Track 0 stayed PLAYING. New `Track::canServiceBackgroundSnapshotOwnerTurn` pins only that exact phase/source; review found `main.cpp`'s broader `anyTrackRecordingOrOverdubbing()` mask still denied it, so a Snapshot-specific `TrackManager::backgroundSnapshotWorkAllowed` now excludes only this qualified stopped job from the capture veto, preserving live capture/other-stop exclusions and the existing one-unit background limit. Pre-fix global-gate and withheld-background-grant controls each turn retirement RED; corrected fixture publishes Track 1's whole root, retires/releases the job and preserves exact committed On@12/Off@96 plus Track 0 physical current. Focused 183/183, full native 2,872 passed / 7 skipped, default RAM1 padding 968. This is one conditional native handoff, not a maximum gap/device/new-reader verdict; 3.10 remains open. Detail: [`3.10 — stopped RecordPublished background Snapshot`](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#310--background-snapshot-for-a-stopped-recordpublished-job-2026-10-08).*
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
  restores. **Not claimed:** with no production window lane yet (to be connected in 3.7a) the gate is inert in today's running
  system, and the plain-event reservation release on refusal is executed but not asserted. **OWED in this task:** fixture
  **5.5 timing envelope** with over-capacity same-tick, wrap and cross-page-closure cases remains; production paging consumption is assigned to 3.7a and verified before 3.12 checkoff. Detail:
  [`evidence/2026-10-02-s312c-midi-edge-admission/EVIDENCE.md`](evidence/2026-10-02-s312c-midi-edge-admission/EVIDENCE.md).
  *Increment 3.12d landed 2026-10-05 — fixture 5.5 content/storage dimensions (test-only; task stays open).* New focused suite `test/test_window_paged_legacy/` exercises over-capacity same-tick events across two source lists (2,400 events > L), explicit `RingFull` then page reuse, a wrap-crossing W-range against independently assembled half-open source-order events, and a NoteOn/NoteOff pair separated across >3 pages. M-RING (drained page not released) makes all three cases fail to terminate; the production header was restored and verified diff-clean. **Verified:** focused 3/3; full native **2810 cases / 7 skipped / 2803 succeeded / 0 failed**; strict validation and `git diff --check` valid. **Not claimed:** timing/deadline measurement, production consumer or device run; 3.12 remains open for those gates, and its production consumer is assigned to 3.7a. Detail: [`evidence/2026-10-05-s312-fixture-55/EVIDENCE.md`](evidence/2026-10-05-s312-fixture-55/EVIDENCE.md).
  **DEC-114 sequencing:** the remaining production-consumer clause in 3.12 is delivered by 3.7a with the fallback retained; fixture 5.5's timing measurement remains a separate 3.12 gate.
  *Increment 2026-10-06 (3.7a I3b) — the **production paging consumer entry** this task owed, plus the production trigger.* `Track::consumePagedPlaybackWindowForSlot` is the consumer entry the ring's own fixtures could not provide: it walks the prepared interval exactly once through the runtime's cursor, answers `NeedsPage` (the caller produces; never a wait), releases the page storage at the end of the walk, and refuses `StaleRevision` once the loop's revision has moved. The production trigger is now real too: an interval that cannot fit one window — reached either by the retained lane's own slice ceiling or by the window admission's `WindowExceedsRetainedCapacity` — is prepared as pages for the same revision, one bounded page per turn, from the same pinned bodies the window gather reads. **Verified** in 3.7a's record (focused 154/154, full native 2,829 / 7 skipped / 2,822 succeeded, capture build SUCCESS, five red-proofs with byte-identical restores). **Still owed in this task:** fixture 5.5's timing envelope over the production paging path, and the hot-path emission integration (3.7a I3c) — the pages are prepared and walkable, but nothing emits from them on the clock path yet. Detail: [`evidence/2026-10-06-s37a-i3b-paged-legacy/EVIDENCE.md`](evidence/2026-10-06-s37a-i3b-paged-legacy/EVIDENCE.md).
  *Increment 2026-10-06 (the paged path's serving envelope; test-only, no `src/`/`include/` change) — **fixture 5.5's timing gate turns into a measurement, and its emission clause is falsified.*** The drive is the production per-track clock entry on a **retained-source (restored-library) shape** with an interval **over `L`** (2,100 events of note **pairs** inside the two-bar span the window path accepts), the oracle is the interval's own effective content read **per frame** (the due set from independent phase arithmetic), and the run spans **two whole passes** so the loop wrap is crossed. **Holds:** the switch to pages comes from the admission's **capacity** refusal (17 of 18 slices, so not a spent budget); no row is emitted outside its own due window; the emitted stream is an **order-preserving subsequence** of the interval over both passes; the ring's published-page high-water stayed **≤ 3** while the interval is **5.5× the ring's capacity** -- segmentation is a **storage** bound, exactly as the task's last sentence asks, and no duration is claimed (the end-to-end thresholds are **S7.5a — envelope/timing oracle**); no inline rebuild (the detector unchanged, the merged store empty, the paged delivery emitting); every wrap arrived straddled. **Falsified, with the numbers:** over two passes the walk consumed **4,200** ordinals (the interval twice, exactly) and the MIDI edge emitted **3,830** -- **370 events (8.8 %)** were consumed and then **skipped** as behind their frame (20 short frames; the dry-turn count in the final pass is 263, and that counter is per pass -- the boundary resets it), so the interval is **not** served complete, and the loss is a drop rather than a late delivery or a refusal by name. **Deliberately not attributed:** a plain per-turn page deficit -- doubling the lane's turns per frame in a withdrawn probe changed none of the numbers, so the binding constraint is the next slice's to find, with this fixture as its reproducer and acceptance test (its candidate repairs: a producer turn that publishes more than one page while the ring has room, or a **named refusal** of the emission selection at densities the declared service cannot carry, per the I3c-b lesson). Verified: focused **163/163** (baseline 162), full native **2,841 / 7 skipped / 2,834 succeeded / 0 failed** (baseline 2,840/2,833), capture build **SUCCESS** with the RAM1 line **byte-identical** (`code 392,248 + padding 968`; a test-only slice cannot move the image, and the diff confirms it), strict validation and `git diff --check` clean, red-proofs **RP-A** (a drained page never released) and **RP-C** (the emitting pass's content bound dropped) RED with byte-identical restores, and **RP-B** recorded as a **non-red-proof by equivalence** with its mechanism named. **Not claimed:** no repair, no `ExecutionBounds.md` row change, no device leg, no fallback removal (3.7b). Detail: [`evidence/2026-10-06-s312e-paged-serving-envelope/EVIDENCE.md`](evidence/2026-10-06-s312e-paged-serving-envelope/EVIDENCE.md) with its [`EXECUTION.md`](evidence/2026-10-06-s312e-paged-serving-envelope/EXECUTION.md).
  *Increment 2026-10-06 (the falsified clause's **repair**; firmware + tests) — **the emission selection refuses by name at
  the declared closure capacity, and 5.5's clause is asserted inside it.*** The mechanism is now measured, not guessed:
  over the 3.12e drive the walk consumed **exactly its own due set every frame** (so the producer's page rate was never the
  constraint, which is also why 3.12e's withdrawn doubling probe changed nothing) while the active-note ledger sat pinned
  at **128 = `ActiveNoteLedger::kMaxOpenNotes`** with `overflowed == 1`, so **every missing row was a `NoteOff` whose own
  `NoteOn` had already been sent** — a note-on nothing can close, i.e. wrong content rather than a late delivery or a
  refusal. `Track::beginPagedPlaybackForSlot` now reads the interval's own **closure profile** over the content the
  retained gather has already traversed (prefix-safe) and **refuses the switch by name** when the content **owes closures**
  and its peak exceeds the declared capacity: `PagedPlaybackOutcome::ClosuresExceedDeclaredCapacity`,
  `PlaybackViewPendingReason::ClosuresExceedDeclaredCapacity` and
  `Track::PagedEmissionDeliveryState::ClosuresExceedDeclaredCapacity`, with `committedPlaybackRetryCanProgress` refusing to
  re-attempt it exactly as for `WindowExceedsRetainedCapacity`. The ring is **never established** (no page, nothing bound,
  `admittedForEmission()` false) and the demand stays owed. The `owesClosures` term is load-bearing: note-on-only content
  exceeds the capacity while losing nothing, and dropping the term fails **ten** existing paged fixtures (RP-E). Fixtures:
  the 3.12e reproducer (1,050 pairs, 200-tick span, high-water 164) now asserts the refusal with **zero rows at the MIDI
  edge, zero pages, and `ledger.overflowed() == false`** — the harm class cannot arise from a path that served nothing —
  and the acceptance half (`…_the_paged_path_serves_a_restored_over_l_interval_at_each_due_tick`, 1,060 pairs = 2,120
  events > `L`, 132-tick span, high-water 109) asserts 5.5's clause strictly per frame: **`skipped == 0`,
  `shortFrames == 0`**, exactly twice the interval emitted, both edges agreeing, `ledger.overflowed() == false`.
  **Verified:** focused **164/164** (baseline 163), ring **3/3**, full native **2,842 / 7 skipped / 2,835 succeeded / 0
  failed** (baseline 2,841/2,834), capture build **SUCCESS** with the RAM1 line **byte-identical** (`variables 96,192`;
  `code 392,248 + padding 968`), `git diff --check` and **strict** validation clean, red-proofs **RP-D** (preflight
  disabled) and **RP-E** (`owesClosures` dropped, ten fixtures red) with byte-identical restores. **Not claimed:** the
  balance is not a ledger (mis-paired orphan-off content inside the capacity is not detected here); over-refusal of
  unpaired-excess wrap-crossing content is deliberate; channel remapping is not modelled; the refused interval is not
  served at all on this shape (silence by decision — the reason **3.7b** stays open for it); no **6.3** sweep, no duration,
  no device leg, no `ExecutionBounds.md` row change, and `kMaxOpenNotes` itself unchanged (that call is 6.3's). Detail:
  [`evidence/2026-10-06-s312f-closure-capacity-refusal/EVIDENCE.md`](evidence/2026-10-06-s312f-closure-capacity-refusal/EVIDENCE.md)
  with its [`EXECUTION.md`](evidence/2026-10-06-s312f-closure-capacity-refusal/EXECUTION.md).
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
  answering `AlreadyHandedOff`. *Verification:* focused **61/61**, full native **2692 / 2685 succeeded / 7 skipped / 0 failed**, capture build SUCCESS with RAM1 variables 96,192 unchanged / code 392,328 + padding 888 (48 B above the floor); target size pin measured/rebased 6,512 → 6,624 (+112 B), host 6,544. Five mutations red-proved with byte-identical restores.
  **OWED in this task:** fixture 2.12's integration half and production callers — preview requester at the revision-advance seam and `EditManager` mutation/re-focus/exit hooks — through the serving path in 3.7a with fallback retained. The `then save` clause belongs to the storage harness. Evidence: [`evidence/2026-10-02-s313-preview-contract/EVIDENCE.md`](evidence/2026-10-02-s313-preview-contract/EVIDENCE.md).
  *Increment 3.13 integration half landed 2026-10-05 — test-only + host tooling; no production line changed; task remains open for production callers and the save clause.* `test/test_windowed_preview_integration/` links the real note-edit owner and window lane. Five fixtures cover session source ownership, mutation between build grains, in-session undo/redo, selected-track cancellation and committed handoff on exit. Verified: focused 5/5; full native 2,815 / 2,808 succeeded / 7 skipped / 0 failed; capture build SUCCESS with RAM1 unchanged; three mutations red-proved and restored byte-identically. **Production callers DELIVERED by 3.7a I3 (2026-10-06):** the requester runs at the revision-advance seam and the three lifecycle hooks (supersession/mutation, exit, re-focus) are the `EditManager`'s own, each with its own fixture and its own red-proof (`evidence/2026-10-06-s37a-i3-preview-wiring/EVIDENCE.md`). **Fixture 2.12's `then save` clause DELIVERED by 3.13 / I10 (2026-10-06):** the window lane and the storage harness are linked in one TU, and the save is measured to serialise the slot's **checkpoint** and **never** the served preview (which keeps serving afterwards, at its own revision, `origin == Preview`) — `evidence/2026-10-06-s313-212-save-clause/`. **Still owed:** the display consumer of a preview window — 3.13's only remaining item. Evidence: [`evidence/2026-10-05-s313-preview-integration/EVIDENCE.md`](evidence/2026-10-05-s313-preview-integration/EVIDENCE.md).
  **Reconciliation (2026-10-06, documentation-only) — the display-consumer item above stays open on an explicit cross-change dependency, and is not deferred to 3.7b fallback removal.** The integration belongs to `runtime-working-set-ownership` **S4e** (the visual tier — the runtime change's display rework: S4e.1/S4e.2/S4e.3/**S4e.4**/S4e.6/S4e.7), and **S4e.4** forbids *"leaving a third variant"* of the display read paths, so no display read is wired here. [`DISPLAY-CONSUMER-RECONCILIATION.md`](DISPLAY-CONSUMER-RECONCILIATION.md) records it with both requirements cited (this change's `Preview content is a distinct source and is never committed currency` (`spec.md:86`) and `A published window is immutable and no consumer read straddles a replacement` (`spec.md:37`, whose `Consumers cannot mutate a window` names **display** among the four consumer kinds); the runtime change's `A compact visual envelope is retained for every slot, with a capped detail window` and `Display compose reuses owned frame storage`), the **producer** (`Track::requestPreviewWindow` + `origin == Preview` in both identities + the served-window read `playbackWindowEventAtForSlot`), the **consumer** (the display — `DisplayManager`, the piano-roll consumer per `S4C0-PUBLICATION-INVENTORY.md`'s consumer-group table and `docs/Authority/Architecture/Display.md`) and the **integration owner** (**RWO S4e**), with the dependency recorded on **both** sides (the reciprocal note sits in `runtime-working-set-ownership/tasks.md` § S4e). One pre-existing doc defect is recorded rather than fixed: `include/Track.h`'s `playbackWindowEventAtForSlot` comment is split by the `servicePreviewWindowForNoteEditSession` comment block, so its last line reads as if it documented the preview function.

  **DEC-116 — one rolling path per sounding slot (2026-10-07, design only):** the existing preview producer/identity checks remain useful, but whole-loop `requestPreviewWindow` and the demoted-only `servicePreviewWindowForNoteEditSession` gate are transition mechanisms, not the final NOTE_EDIT playback path. Task 3.15d moves short and long preview to interval-qualified committed-base + session-delta preparation on the same owner/reader; RWO S4e remains the distinct display consumer, not a second playback builder. No checkoff of 3.13 or 3.15d follows from this note.
- [ ] 3.14 **Integrate undo/redo audible handoff** — establish a production-path fixture before changing
  `refreshPlaybackAfterCapturePassStateChange` and its capture/overdub undo/redo callers. Extend the existing
  `TrackUndo` request/refresh path to use the immutable publication owner and task 4.5's off-clock selective
  reconciliation, not `silenceSlotMidiOutput` as blanket cache invalidation. Keep checkpoint/undo semantics
  and ownership unchanged. Verify unchanged sustained notes receive no early NoteOff or retrigger, removed
  notes close at handoff and obsolete closures never fire; pause between real preparation grains for
  repeated undo/redo, track/slot switching and stale completion. Assert exact MIDI output, ledger state,
  retained-source lifetime and eventual release. Reuse task 2.12 for preview-session undo/redo. Red-prove
  the fixture with blanket silence restored. Gate normal windowed undo/redo enablement and task 3.7b's
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
  (repeated undo/redo, track/slot switching, stale completion — 3.7a's serving path), retained-source lifetime and
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
  **Earlier increments 3.14/3.14b:** audible ledger reconciliation and committed-pass identity sources are implemented; their native proofs and evidence remain as recorded above. The remaining production request cannot use operation-scoped `RuntimeCompanionPasses` or invalidated `RuntimeCanonicalPassInputs` at the mutation edge. It must request currency through the existing Loop publication owner, without inline enumeration/publication. The production-path probe with a served window and real `TrackUndo::undoForLoop` shows readiness becomes non-Ready and names the owed rebuild. **Still owed:** 3.7a requester/consumer integration with fallback retained; then the repeated undo/redo, track/slot switch and stale-completion matrix, retained-source release, displayed provenance and preview undo/redo against the real handoff. Source currency remains the Loop publication owner's responsibility; parity task 4.1 does not produce it. Evidence: [`evidence/2026-10-02-s314c-refresh-source-verification/EVIDENCE.md`](evidence/2026-10-02-s314c-refresh-source-verification/EVIDENCE.md).
  *Increment I6 landed 2026-10-06 (3.7a's handoff closures; the task stays open):* the **serving-path half** the task owed is real now -- the version handoff over a full-loop window closes the soundings the incoming revision removed, on the wire, before the replacement's note-ons, instead of dropping those obligations silently (so a note an undo removed can no longer stay sounding because the ledger forgot it). Still owed: the refresh as a request through the publication owner (its requester is 4.1's current root, then 3.7's consumer), the repeated undo/redo and track/slot-switch between-grains matrix, retained-source release, displayed provenance and preview-session undo/redo. Detail: [`evidence/2026-10-06-s37-handoff-closures/EVIDENCE.md`](evidence/2026-10-06-s37-handoff-closures/EVIDENCE.md).
  *Increment 3.14d landed 2026-10-06 (test-only; the task stays open): the **between-grains matrix** this entry's own
  owed list names is measured.* Four cases pause the window lane's **real** preparation grains
  (`stepPlaybackViewMaintenance`, one declared grain per turn; 100 events = five turns) and let a production gesture
  land in the gap, asserting the wire, the ledger, the published window, the readiness **and its holder**, and the
  retained source together. **Measured, and one packet expectation corrected:** a **committed pass** advances the
  revision only, so the paused build stays legitimate, completes, **publishes at the revision it froze**, and readiness
  **names** the lag (`OwesCurrency` / holder `window_rebuild`); a real **undo** advances the revision *and* bumps the
  playback generation, and its invalidation **discards** the paused preparation (measured: `pending`/`buildOwed` false
  immediately after the gesture, nothing published, readiness `OwesSource` / holder `published_source`) — so the
  completion guard in `publishCompletedPlaybackWindow` is the **second** line of defence, not this case's mechanism, and
  `RP-D2a/b` are recorded as **non-red-proofs for this fixture** with that reason; a **selection change** (the slot row,
  the slot selector and the real `setSelectedTrack`) emits nothing, silences nothing, keeps the operation identity, and
  the worker still completes **into its own slot**; with a **retained source** admitted, no gesture re-aims or drops it,
  the readers keep answering the revision the source **holds**, and 3.6a's one release predicate refuses while that
  window serves it and releases it only once a newer window is served. Gesture outputs are exact (one NoteOff for the
  identity a removal owns, **nothing** for the re-establishing direction, the untouched soundings alive and never
  retriggered), and the negative controls are the untouched soundings, the incoming track's slot and the refused
  release. Verified: focused **174/174** (baseline 173/173), full native **2,853 / 7 skipped / 2,846 succeeded /
  0 failed** (baseline 2,852/7/2,845), capture build **SUCCESS** with the RAM1 line **byte-identical** (`variables
  96,192`; `code 392,248 + padding 968`; `locals 34,880`; floor 840), strict validation and `git diff --check` clean,
  `git diff --stat -- src include` **empty**, and **four red-proofs RED with byte-identical restores** (`RP-D1` the
  readiness lag not named, **`RP-D2c`** the invalidation's slot release dropped, `RP-D3` the `SlotSwitch` row releasing
  the candidate, `RP-D4` the release predicate's served-window clause, `RP-D5` the edit-batch settlement dropped).
  **Still owed:** the refresh as a request through the publication owner (4.1's current root, then 3.7's consumer),
  **displayed provenance** (the display's, per [`DISPLAY-CONSUMER-RECONCILIATION.md`](DISPLAY-CONSUMER-RECONCILIATION.md)
  — RWO S4e) and preview-session undo/redo (3.13/2.12). Detail:
  [`evidence/2026-10-06-s314d-between-grains-matrix/EVIDENCE.md`](evidence/2026-10-06-s314d-between-grains-matrix/EVIDENCE.md)
  with its [`EXECUTION.md`](evidence/2026-10-06-s314d-between-grains-matrix/EXECUTION.md) packet.

  **Sequence:** 3.7a first, then complete 3.14 and 4.5 against the real handoff. 3.7b removes fallback only after those plus 3.12/3.13, F-7/F-8, timing and compatibility gates pass.

### 3.15 — DEC-116 one rolling playback path for all sounding slots (new design scope; all unchecked)

**Final-state contract:** the existing Track window owner builds one source-qualified interval stream per sounding role, and one clock-edge reader consumes it. Different source lifetimes are adapters, not separate drivers. The present pair/fixed/bridge/paged/merged and first-Record branches are temporary only; task 3.7b removes their emission authority after these fixtures and device gates. A `current` view stays immutable while `ready` and `build` use distinct generation-qualified roles. The per-sounding allocation scales with active roles, not recorded slots; a shared global current buffer is forbidden. DEC-108's pinned-source, deadline, fault, fair-service and checkpoint contracts still gate every slice.

**2026-10-09 — 3.15a–f — one reader per sounding slot, investigation handoff saved (no checkoff).**
See [3.15a–f — investigation handoff](design.md#s315-investigation-handoff) for function anchors,
source/role lifetime tables, proposed fixture-first slices, RED controls and verification commands.
One assessment returned `REVISE`; five corrections were incorporated by the author without a second
assessment. This is a revised draft, not all-route implementation readiness or an execution packet.
Start with the test-only live-Capture oracle; native epoch-0 preparation does not establish device-epoch
Record playback. Stop-handoff lifetime, scheduled overdub, NOTE_EDIT, paged delivery, source/clock
cancellation and measured service/content gates remain required by the unchanged tasks below.
Keep default rolling disabled and live compatibility emitters/tests intact. All checkboxes are unchanged.

**I5 — release fully characterized: zero-slack admission refusal (2026-10-16; RED, numbers pinned).** T-RETRY2
probe: `stepCommittedPlaybackRetry`'s CannotCompleteBeforeHandoff branch retires the counted candidate because
`canCompleteWithinLead(768, 0)` allows zero turns at lead == leadTicks == 768 (grains 5, outstanding 0). The root
request waited behind the Capture ready by design and arrived at its own boundary inadmissible by construction.
Next: choose the fix (formula double-reservation vs deadline stamping) with the contract semantics, RED→GREEN,
gates, rebuild, attempt5. RED intentionally registered; device on exact default. No Git delivery.

**I5 — release primitive named: retireCoverageSource in the adoption dispatch (2026-10-16; RED).** Native-only
tags: retireCoverageSource fires exactly once, inside the tick-768 dispatch right after the frozen adoption;
completion-handler and launch-cancel branches did not fire. Next: tag the call sites (discardCoverageBuild callers
+ direct retire callers) to name the path, then apply the preservation fix. RED intentionally registered; no
gates/upload/attempt5; device on exact default. No Git delivery.

**I5 — held Stop release located in the tick dispatch (2026-10-16; no checkoff, RED).** Post-pulse probe: the
source is Retiring/frozen-cleared immediately after the tick-776 clock dispatch, before any owner turn — the
release is in the per-tick path (post-adoption hot-path/tick handling), not the rolling service, misses service,
or maintenance lane. Speculative `admittedOwned` Building gate tried and reverted (different call site).
Remaining: census the per-tick release site that destroys the qualified AwaitAdmission candidate. RED
intentionally registered; no gates/upload/attempt5; device on exact default. No Git delivery.

**I5 — held Stop release pinpointed to the clock path (2026-10-16; no checkoff, RED).** Entry-state probe: the
first post-adoption maintenance turn enters with the source already Retiring and the frozen request cleared — the
release happens on the clock path between the tick-768 adoption and any maintenance turn, then the retirement
drain completes (GrainServed) and the plateau goes Idle until the 1536 re-request with deadline 0. Next fix
target: the post-adoption clock-path release that destroys the qualified AwaitAdmission candidate. RED
intentionally registered; no gates/upload/attempt5; device on exact default. No Git delivery.

**I5 — held Stop root-bridge + whole-loop-window fixes applied (2026-10-16; no checkoff, RED further along).**
Passive maintenance-return trace pinned the plateau: the retirement branch's same-revision shortcut treated the
adopted Capture view as a root bridge (Idle every turn, qualified source starved), and once blocked the lane
demanded a whole-loop window for the 2-bar take that the declared contract refuses within a bar-boundary lead
(CannotCompleteBeforeHandoff cycle → expiry → stranded job — the device terminal shape). Applied rolling-gated
fixes: root-bridge shortcut requires Committed origin + exact current-root identity; Record-return lane requests
one-bar windows like the rolling reader. Fixture predicate aligned to the designed retirement edge (frozen
Capture or adopted root-backed committed window over the corrected 1536 geometry). New terminal: the qualified
one-bar candidate for the passed interval is discharged at the first post-adoption boundary without adoption,
then a fresh {0,768} request re-stamps with deadline 0; Stop job still bound. Next link: retire stale-interval
root requests as missed and request the next interval. RED intentionally registered; no gates/rebuild/upload/
attempt5; device on exact default. No Git delivery.

**I5 — held Stop local reproducer progress (2026-10-16; no checkoff, RED registered).**
Continuation of the held frozen mismatch Stop blocker. New production-path native fixture
`rollingRecordHeldMismatchStopWithDeferredOwners` (real Stop grain + Record Snapshot publisher via the existing
fixture friend routes, actual clock/holder/reader) proves a concrete **root-return admission stall**: root
published before the 768 edge, frozen [768,1536) ready adopts at 768, but the privately counted committed-root
candidate (AwaitAdmission, source retained) is never admitted — `beginQualifiedCoverageBuild` refuses silently
(reason stays `AwaitingSourceQualification`), the candidate's build-role rows are gone right after adoption
(candidate-integrity assertion: expected 2 rows, was 0), the request is re-stamped to deadline 1536, and at that
edge the deadline expires (`reason=DeadlineExpired`), the frame drops Idle, and the `RecordPublished` job stays
pending — the same terminal shape as the device trace. Speculative same-revision root-bridge restriction in the
retirement lane was tried, did not fix the RED, and was **reverted**; production source is back to the attempt4
image state. Not yet identified: which owner (stop-grain/finalization/publication reset) clears the build-role
candidate between AwaitAdmission and admission. Until that owner is found and fixed RED→GREEN, no upload and no
attempt5; device remains on the exact default image. Test file intentionally carries the failing reproducer;
suite is not green this turn by design. No Git delivery.

**I5 — managed close repair/approved attempt4 (2026-10-16; PARTIAL/no checkoff).** [I5 — latest evidence and next blocker](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-HITL-EXECUTION.md#managed-close-repair-and-approved-attempt4-2026-10-16): skill-guided teardown now defers native ports after DeviceSession two drains/capturestop, reuses retained output for teardown Stop/heap, and uses native10s fatal stack watchdog only after verified safety. Host94PASS including GIL-held hang injection; one review's pre-drain close finding repaired. Fullnative3284pass7skip121suitesPASS. Exact919e17e6… uploaded/evaluated/correctmetadata: growth+empty legsPASS, independent held live schedule/identity/no-retrigger/ledgerPASS. Held mismatch frozen Stop fails then idle guard refuses; focused native heldStop shapeGREEN is not the production reused-take reproducer. Matching physical Stop/full matrix open. Attempt4 normal2drains/capturestop/deferredclose; exact default restored/fresh close-loop-restored02PASSguard15/bothheaps/2drains/nolatch.4of6attempts used, stop on health blocker/no speculative patch; no SDcleanup/Git/defaultactivation/parentcheckoff,29/60.

**I5 — approved rolling HITL continuation (2026-10-16; BLOCKED/no checkoff).** [I5 — current execution and safety handoff](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-HITL-EXECUTION.md#owner-approved-bounded-continuation-2026-10-16): retained stale-shell candidate evaluated with correct metadata; initial current still stalled. Fixed newly reproduced publication-before-first-reader deadline0 admission race locally (specificRED→GREEN/exact MIDI/ledger), focused507/fullnative3283pass7skip/host23/default+enabled buildsPASS/padding1160/952≥840/one reviewAPPROVE. Latest919e17e6… candidate NOTuploaded. Query schema43 read-only roles/worker metadata; no new runtime fields or source walk. Diagnostic full03 host session.close hang prevented own shutdown report; separate canonical non-reset recovery2drains/no latch, exact default restored/freshpostflight02STOPPED/guard15/bothheaps/2drains/nolatch. Stop further stimulus on teardown blocker, not missing approval/retry cap. Empty/held/matching Stop/full matrix remain device-unverified; no SDcleanup/Git/defaultactivation/parentcheckoff,29/60.

**I5 — dedicated rolling HITL partial result (2026-10-16; no checkoff).** [I5 — execution and continuation](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-HITL-EXECUTION.md): full07/full08 growth verifies actual ordered3-pair wire inventory,6-row record/effective/NoteId parity, live period/window/source bindings and corrected frozen Stop/critical-loss0. Following empty take stalls; held/matching Stop NOTverified, so full recipe NOTpassed. Visual-owner cancellation and stale idle-shell reuse reproduced/fixed; latter latest candidate native/build/sanitizer only, NOTuploaded. Host/source snapshot/provenance corrections retained; original metadata defect documented, no retroactive metadata-qualifiedPASS. Retry cap reached, further stimulus needs explicit approval. Exact pretest default restored; postflight02STOPPED/guard15/bothheaps/two drains/nolatch. No SDcleanup/Git/defaultactivation/parentcheckoff,29/60.3.15a–f — all-route reader,3.10 — service,3.7b — legacy retirement remain unchecked.

**I5 — required HITL stop boundary (2026-10-15).** User requested work only until hardware verification is needed; the locally verified growing-Record slice is already there. Pause further firmware work and use [I5 — dedicated rolling-enabled HITL handoff](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-HITL-RECIPE.md) next, after registered TEMP scenario/receipt preparation. No hardware/upload/SD/Git/checkoff; prior native/sanitizer/build evidence unchanged and exact retained image hashes reverified. This device-content gate is separate from unqualified physical owner/IRQ/arrival bounds; neither default-OFF base compatibility nor native controlled grants discharge it.29/60/parents unchanged.

**I5 — subsequent live Record coverage (2026-10-15; local functional gate PASS, parents unchecked).** [I5 — current coverage evidence](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-COVERAGE.md) supersedes the first-bar-only functional limit: one-bar windows over growing accepted raw extent, wrap-only period/origin handoff, exact owed distinct interval retention, same reader/ledger, held/no-retrigger/empty/nonzero-origin/late controls, matching subinterval Stop and corrected frozen mismatch. Original2behavioralRED+reviewretriggerRED→GREEN; focused500/ASan8/fullnative3275pass7skip/three removalcontrols/default+enabled buildsPASS/padding1160/952≥840/ABI unchanged. No new field/view/queue/manager/Track growth, no budget change. Total raw-source inspection remains extent-dependent; target continuation/input-return timing/whole-owner IRQ/arrival and dense rolling-device qualification open. No upload/SD/Git/defaultactivation/checkoff;29/60/3.15a–f — all-route migration,3.10 — bounded service,3.7b — legacy retirement remain unchecked.

**I5 — connected first-bar rolling Record increment (2026-10-15; parents unchecked).** [I5 — execution and verification](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-VERTICAL.md): real nonzero-era request/production offers+RR maintenance, append-stable private Fill/order, complete edge adoption and actual MIDI/ledger while RECORDING; matching nonempty Stop plus existing mismatching/root-retention controls pass. Original8oracles ordinarily registered; realClock all3input routes+Stop+epoch tests, fullnative3267pass7skip/ASan5/two negativecontrols/default+enabled buildsPASS/padding1160/1080≥840. One lifecycle finding fixed/retested. Runtime+64external per slot, Track unchanged. Live scope is initial[0,768)/period768 only; subsequent/growing coverage, target input-return/physical timing and enabled-device evaluation remain open. Target continuation compile-rejected; no hardware/upload/SD/Git/defaultactivation,29/60 unchanged. This is neither 3.15a–f — all-route one-reader completion nor 3.10 — bounded service or 3.7b — legacy retirement checkoff.

- [ ] 3.15a **Freeze the one-reader production oracle before cutover.** Link the actual `playCommittedLoopMidi`/MIDI output path and the existing window owner in native fixtures for short/long loops, empty intervals, same-tick density, first Record bar and stop return, live overdub/wrap, NOTE_EDIT open/change/session undo/exit, layered/jam slot playback, slot switch, global undo/redo, clock-source/seek, and SD-restored legacy density. Pin independent expected ordered MIDI, ledger closures, source/slot/generation identity, bounded owner turns, non-destructive refusal and source release; fixtures must fail when the migrated route is removed. Reuse, do not duplicate, existing finalization/ownership/preview/paged tests. Record gaps as explicit prerequisites to their implementing slices; no production policy edit in this task.

  **I5 — live-Capture oracle increment (2026-10-13; test-only, task unchecked):** existing `UNIFIED_RECORD_CAPTURE_INITIAL_ORACLE` now additionally pins exact Capture token/origin, absence of raw/first-return authority, initial silent ledger/duplicate edge and successor identity. Two separately registered cases freeze accepted-input→real two-bar stop/frozen ready→actual edge MIDI/ledger and the nonzero-epoch non-claim. Gated 193 passed / three expected assertion failures / one Unity harness ERRORED: missing published live current blocks initial and stop cases; epoch-1 is independently unclaimed; downstream assertions NOT RUN, not a lifetime/content PASS. Withholding the private-build owner call adds exactly its completion RED; restored byte-identically. Ordinary focused 489/489, full native 3,099 succeeded / 7 skipped / 114 PASSED suites, strict validation/diff clean; one focused review no concrete defects. No production/build/upload/device/SD/Git action, default rolling off and compatibility registrations intact. Next requires coherent authorized initial-publication/three-role/stop-lifetime implementation; reader-removal/supersession controls and measured/device/all-route gates remain open. [I5 — execution evidence](evidence/2026-10-13-i5-live-capture-oracle/EXECUTION.md).

  **I5 — cold live-Capture publication/mismatching-stop increment (2026-10-13; parents unchecked):** Track's epoch-0 empty-source completion now promotes a qualified silent Capture current off-clock through the same three views; actual TrackManager tick/reader selects its physical storage using provisional coverage despite growing Record geometry prediction. Input preserves served token/bytes. Real mismatching two-bar stop clears only stale positional frame and prepares frozen ready with ordinary successor rotation. Both stop/reader cases GREEN exact On2316/Off2544/ledger/duplicate/source hold, including root-before-edge and stop release; reader/frame removals RED/restored, one lifecycle review no concrete defect. Gated195passed/twoexpected failures+harnessERRORED: original initial oracle now reaches missing late epoch-1 successor, separate epoch1 non-claim remains RED. Ordinaryfocused489/489/fullnative3099pass7skip/114suites/defaultbuildSUCCESS/padding1000≥840. Approved defaultupload3168ccb1 and registered300BPM host-clock base8/4 hardware **compatibility PASS**, overall_oktrue/issuesempty/critical-loss0/two-drainscomplete/nolatch; advisory second-overdub first-note lateness retained. Not new-reader/device/timing/all-route acceptance; pre-upload hash-check path failed and no before/after pin claimed. Default rollingOFF/compatibility emitters/registrations/limits unchanged, no Git or broader SD action. Next running-epoch request/owner plus measured remaining-work/service contract; 3.15a–f/3.10/3.7b stay unchecked. [I5 — publication/stop and hardware evidence](evidence/2026-10-13-i5-capture-publication/EXECUTION.md).

  **I5 — production-order live-Capture oracle/service measurement (2026-10-13; tests/docs only, parents unchecked):** new gated `test_live_capture_reader_survives_production_order_maintenance` drives actual rolling service→deadlines→offers/round-robin→Track owner after real TrackManager epoch1 bind. Current role/storage/token/bytes survive but reader frame becomes Excluded/Idle/no holder: exact expected RED. Separate epoch1 initial and late-successor failures retained; gated200total/196pass/3expected assertions+harnessERRORED. Ordinary `test_live_capture_builder_work_counts_inspected_rows_and_supersession` drives real accepted input/live source/isolated unpublished builder, preserves exact On700/Off740 and measures source0/2/4/66/130 rows with fill1/1/1/2/3, order0/1/1/1/1, begin1 each. Post-fill On supersession refuses old order; Off/restart green. This prices builder work only, not publication/source-owner/closure/retirement/competitors or timing. Focused191/191/fullnative3100pass7skip/114PASSED suites/strictspec/diff PASS, one focused review no concrete defect; interrupted first full run not counted. No production guard/admission/default/epoch-policy/build/device/upload/SD/Git change. Next retained running request/cancellation and composed remaining-work contract; affected-edge1468−Off740=728ticks is distinct from ready-boundary768−740=28, existing oracle unchanged. [I5 — service fixture evidence](evidence/2026-10-13-i5-running-capture-service/EXECUTION.md).

  **I5 — running-Capture request/composed service draft (2026-10-13; documentary capture only):** [I5 — contract draft](evidence/2026-10-13-i5-running-capture-service/CONTRACT.md) proposes existing Track request/three-view ownership, live-Capture recipe, oldest-deadline coalescing, generation-safe useful work, explicit source/stop/cancellation outcomes and composed source+window+competition accounting. Legal boundary768 after Off740 leaves28ticks, even though first changed On1468 leaves728; no mid-traversal adoption or assertion weakening. Current restart-on-token-change is measurement, not an accepted replacement for preserving useful work. G1 — architecture contract remains blocked on source-progress/lifetime certification, distinct-coverage coalescing and initial anchor; G2 — execution contract also needs numerical service closure. No firmware/test/policy/envelope change, new implementation tasks, checkoffs, device/build or fresh native run. Save approval is not Apply approval.

  **I5 — append-stability/prefix-delta proposal (2026-10-13; saved design only):** [I5 — source-progress proposal](evidence/2026-10-13-i5-running-capture-service/SOURCE-PROGRESS.md) narrows useful-work reuse to a Loop-owned stability receipt plus frozen extents and bounded delta-sort/merge/copy-back in existing private arrays. Ordinary append preserves raw prefix; destructive/reordering/transfer paths must invalidate before mutation. Prior focused discovery358/358 passes validate existing stale-source refusal, not reuse. Exact scalar-state/ABI cost, exhaustive invalidation coverage, lifetime/stop equivalence, distinct-coverage coalescing, initial anchor and numerical composed service remain open. No firmware/test/size-pin/policy/checkoff/hardware change; this is not an executable implementation slice.

  **I5 — host pricing/mutation/service feasibility (2026-10-13; parents unchecked):** [I5 — state/resource/service evidence](evidence/2026-10-13-i5-running-capture-service/STATE-RESOURCE-SERVICE.md) compiler-prices explicit target64B/host72B runtime record with forward source cursor plus8B Loop counter; target append aggregates1000/7696 and1024B payload at64shells+8runtimes, not implemented sizes/RAM1/pin approval. Audit maps missing pre-transfer/phase-close/closure/pool-lifetime/bounded-lookup/liveness hooks and fixtures; coverage is not complete. Production-selector counterexample obeys H8/Delta64 yet services accepted740track0at1252, missing768adoption even for one grain with zero extra costs. Current composed bound falsified;591/591 focused passes. No firmware/test-source/size-pin/policy/build-link/device/SD/Git change or task checkoff. Resolve explicit existing-owner service/adoption decision before claiming a HITL-only gate.

  **I5 — input-batch timing reassessment (2026-10-13; no implementation/checkoff):** [I5 — owner/input reassessment](evidence/2026-10-13-i5-running-capture-service/OWNER-SERVICE-REASSESSMENT.md) uses production dispatch-order helper plus source-backed Clock arithmetic to show Off736+fourClocks reaches768 with zero main-owner opportunities in a legal five-message batch. Not a linked full-input/clock fixture or hardware loss attribution.50/50 dispatch/service cases pass. Main-only urgency cannot solve this; proposed transport-preserving continuation and main checkpoints change the timing model and need explicit state/resource/fairness/physical-clock contract approval before firmware. No batch-cap, clock delivery, default/policy/source change or parent checkoff.

  **I5 — approved timing-contract development (2026-10-13; draft, parents unchecked):** [I5 — timing contract](evidence/2026-10-13-i5-running-capture-service/TIMING-CONTRACT.md) pins one-route batch continuation/USB-DIN transportfirst/hostFIFO/exactonce and mandatory-normal-owner checkpoint protocol without a new publisher. Candidate1064+16B on bothABIs; model19,531orderingtraces plusfullcapacity/fairness/RR passes; fivefocusednative599/599. Conditional2*H*G checkpoint bound means64turns forH8/G4 and <455.73µs percompletecheckpoint at28ticks/300BPM before source/input/margin—derived condition, not budget/WCET. Physicalreference/actualcontinuation/sourceimplementation/productionfixtures remain gates; no firmware/policy/cap/clockdelay/sizepin/device/checkoff. User approved contract development only; measurement slice needs its own scope/packet.

  **I5 — real input/clock production oracle (2026-10-13; test-only, parents unchecked):** [I5 — real-input evidence](evidence/2026-10-13-i5-running-capture-service/REAL-INPUT-EXECUTION.md) new isolated suite optsin realClockManager/ClockSourceStateMachine/BarStepButtonHandler over existing realMidiHandler/TrackManager/Track/Loop/window/reader closure. Hardwareports/callbacks/timer only; exact accepted704On/736Off/ID, immutableCapturecurrent/token/storage/bind/quietMIDI/ledger, routeorder/caps andsignedhostpitch. Ordinary10/10; gated7succeeded/3expectedassertions+harnessERRORED atpollreturnExpected736Was768 independently onallroutes; downstreamretainedclockclausesNOTRUN. MissingOff/ClockstimuluscontrolsRED; unsignedpitchcontrolRED/restoredbyteidentical. Onefocusedreview2fixturedefects fixed/retested. Finalnative3117total/3110pass/7skip/0fail/115PASSED suites. No production source/policy/sizepin/build/device/SD/Git/checkoff; coldnativeepoch0publication is explicitly limited prerequisite, notrunninginitialprep/timingclaim. Next separatequalification-gatedcontinuation/source implementationpacket; keeporiginalepoch1/service REDs anddefaultOFF/legacy.

  **I5 — native-gated input continuation mechanics (2026-10-13; parents unchecked):** [I5 — implementation evidence](evidence/2026-10-13-i5-running-capture-service/CONTINUATION-EXECUTION.md) implements DEC-122/MidiHandler-owned onefixedcoldbatch/nativeonlygate withUSB-DINtransportfirst/hostFIFO/captureonlyduringacquisition/16scan/oneactualdispatch/exactack andpureobservations. Qualification15/15 turns originalthreeOff736 return failuresGREEN, fourcopiedClocksdrain768 exactlyonce withaccepted704/736pair/ID/immutablecurrent/ledger. Newerrouteinput cannotovertake; realStop andexplicitrebasepreservebuffer; capacity/generation/coldreuse/refusal-recovery covered. Ordinary13/13/refusal1/1/neighbours339/fullnative3113pass7skip115suites/defaultcapturebuildSUCCESS/RAM1unchangedpadding1000≥840. Actualhelper1056BbothABI/no Track/Loop/runtimefield orpinchange; defaultELFqualificationabsent/targetflag1compile-rejected. Hostcapture/scanremovalcontrolsRED/restoredbyteidentical; onefocusedreviewnoconcretedefect. No device/HITL/upload/SD/Git/checkoff orphysicaldeadline/targetintegration/sourceprogress/initialepoch1claim; defaultrollingOFF/legacy andallparentgates stayopen.

  **I5 — passive checkpoint arbitration/deadline mechanics (2026-10-13; parents unchecked):** [I5 — checkpoint verification](evidence/2026-10-13-i5-running-capture-service/CHECKPOINT-EXECUTION.md) implements DEC-123 — checkpoint arbitration and deadline admission through caller-owned saved-normal debt and actual selected-owner cost preview/commit. Independent logical/physical guard requires extended64bit time/nonzero producer-era; fullwrap expiry resurrection independentlyRED→GREEN. Saved-normal and expiry/era removalcontrols5/6fail restoredbyteidentical. Focused69/69/qualifiedinput16/16/fullnative3127pass7skip116suites/defaultcaptureSUCCESS/RAM1unchangedpadding1000≥840. One focusedreview no blockingdefects/two suggestedcoverage cases added. Actual typeprices stamp16/deadline48/cost12/cursor2/decision4/table84B bothABIs; no residentinstances/field/pinchange. ActualClockManager rebase/Track live non-offer covered with mockedphysical/cost values; not ownerexecution/readiness. No main/ClockManager codechange, physicalproducer/enforcedbudget/targetactivation/sourceprogress/device/SD/Git/checkoff. Progress29/60 unchanged; target/source/lifetime/28tick composedbound gates remain.

  **I5 — physical-reference contract capture (2026-10-13; documents only, parents unchecked):** [I5 — physical-reference draft](evidence/2026-10-13-i5-running-capture-service/PHYSICAL-REFERENCE-CONTRACT.md) pins read-only installed framework1.159.0 evidence and proposes a ClockManager-owned chainedPIT elapsedsource, distinct from ingressdeadline provenance. Current USB-device/host/DIN queues lack arrivalreceipts; RTC corrections resetwallcounter/micros32 dependsSysTick, no unlimitedtimesource inferred. ExactPITread/reservation/debugpolicy, ingresshook/support/metadata price, correlation/delay/lifetime and completeowner-return bounds remain unresolved architecture dependencies, not new implementationtasks. User approved draftcapture only/no newdecision, peripheralreservation, numericbudget, firmware/dependency/defaultpolicy/targetactivation/build/device/SD/Git/checkoff. Previous native evidence is unchanged-code compatibility only; no newtests/builds inthiscapture. Progress29/60/defaultrollingOFF/legacy andnativegate targetrefusal retained.

  **I5 — private delta-order firmware prerequisite implemented (2026-10-13; parents unchecked):** [I5 — delta-order execution](evidence/2026-10-13-i5-running-capture-service/DELTA-ORDER-EXECUTION.md) implements DEC-124 — bounded private append ordering: frozen suffix/suffixonlysort/boundedprefixmerge-copyback/extensionworkbound on existing arrays, with rawprivate prefix/lifetime preconditions and typedview/borrow refusal. Fourcases/270 independentmatrix combinations/focused245/245/fullnative3131pass7skip116suites/defaultcaptureSUCCESS/RAM1padding1000≥840; onefocusedreview no findings/prefixmergecontrol3RED restoredbyteidentical. Actual order72target/136host/runtime7632/9768pinsunchanged. User approved existing-owner firmware/native work toward readiness, not justdoccapture; privateorderprerequisite closedonly. No live Capture stabilityreceipt/source-read/publication,740→768 service, physicalingress/sourcehealth, targetenablement/device/SD/dependency/Git claim. Remaining ingress integration/support/receipt decision is genuine; installeddependency edits forbidden. Progress29/60/defaultrollingOFF/legacy retained.

  **I5 — ingress correspondence prerequisite implemented (2026-10-13; parents unchecked):** [I5 — ingress execution](evidence/2026-10-13-i5-running-capture-service/INGRESS-EXECUTION.md) records DEC-125 — pinned receiver ingress receipts, owner-approved generated source-hash-guarded existing queue sidecars/defaultOFF/installed read-only. Ordinary/native-retained full payload/receipt grouping, reset/overflow/disconnect/sampler invalidation, lateFIFO and real MIDI completing-byte parser covered. Store9/body-parser14/ordinary17/retained19/host30/fullnative3154pass7skip118suites; one focused review no production defect, ordinary earlier-pair coverage gap fixed/2RED-restored, retainedcopy3RED-restored. Default/ingress buildsSUCCESS/padding1000/1304≥840/numericalbudgets/pinsunchanged; State19168target19192host/cell40/receipt32/continuation5152B/batch+4096stackpayload/input5352compilerframe. Default symbols absent/target continuation compile-rejected/main+ClockManager hashes unchanged. Prior integration-choice blocker resolved, not physical producer/era/arrivaldelay/correlation/enforcedwholecost/live source lifetime/request/initialanchor/740→768 readiness. No device/upload/HITL/SD/Git/legacyretirement or parentcheckoff; progress29/60 unchanged.

  **I5 — raw-prefix source-stability prerequisite (2026-10-13; parents unchecked):** [I5 — source-stability execution](evidence/2026-10-13-i5-running-capture-service/SOURCE-STABILITY-EXECUTION.md) records DEC-126 — raw-prefix source stability, existing-store terminal destructive/alias tokens and frozen-extent forward cursor, not Track borrower/semantic-currency/physical lease integration. Source15/fullnative3169pass7skip119suites/defaultcaptureSUCCESS/padding1016≥840; one focused review no concrete defect under retention/exclusion preconditions, pool-exhaustion coverage added, destructive/alias controls8/3specificRED restoredbyteidentically. Target store64/cursor48/Loop1000/runtime7640/stopring29064/Track384unchanged; each store+8B/aggregate64shells+8runtimes+8rings832Bpayload plus8Bglobal (other stores extra), measured pins updated/numericbudgetunchanged. Next Loop interval/phase/projection identity + Track cancellation/retention before adapter/fill integration; conservative unrelated static-retirement invalidation must be charged. Source request/initial anchor/coalescing/adoption/stop and physical/input bounds still gate740→768 readiness. No parentcheckoff/upload/HITL/device/SD/Git/defaultrolling or targetcontinuation change; progress29/60.

  **I5 — semantic live-source retention prerequisite (2026-10-13; parents unchecked):** [I5 — live-source integration](evidence/2026-10-13-i5-running-capture-service/LIVE-SOURCE-INTEGRATION.md) records DEC-127 — semantic live-source retention: phase-assignment/exact projection/raw descriptor-at-creation currency, charged forward fill, both-builder borrow/release refusal and cancellation before holder selection/source destruction. Retained/serialized preconditions only; internal timer ISR executes Track transitions/resets, so all-route read/cancel/adopt synchronization remains unqualified. Focused418/fullnative3178passed7skipped119suites/defaultcaptureSUCCESS/padding1720≥840; one focused review no concrete defect under contract; controls3/2/1RED restored byte-identically. Target Loop1008/runtime7792/Track384/stopring29064; incremental64shells+8runtimes1728B payload/other Capture+descriptor stack extra/no numerical RAM1 budget relaxation. Running request/initial anchor/coalescing/adoption/stop and physical/input costs remain open; no740→768 readiness or parentcheckoff, progress29/60/defaultrollingOFF/targetcontinuationdisabled/legacy retained. Concrete decision needed before target physical producer: proposed ClockManager two-of-four PIT elapsed source/resource/shared-module/debug-freeze policy; counter alone cannot establish sender-delay or complete owner-to-input bounds. No upload/HITL/device/SD/Git action.

  **I5 — ClockManager PIT elapsed-reference prerequisite (2026-10-13; parents unchecked):** [I5 — physical producer execution](evidence/2026-10-13-i5-running-capture-service/PHYSICAL-PRODUCER-EXECUTION.md) records DEC-128 — ClockManager PIT elapsed reference and owner-approved two-channel/stockFRZ1/Runmodeonly implementation, resolving prior resource decision. Coldallfourdisabled reservation, fixed masked high/latchedlow reads, nonrecycledera/overflow/backwards/configuration refusal and real MIDI timer-start bool qualify observations; optional ingresssampler, no musicalclock replacement/checkpointdispatch/deadlineclaim. State16BRAM2/linked+32B/no DTCMfield/pin/numericbudget change; ordinary24/ingress28/combinedcontinuation30/focused46/fullnative3188passed7skipped119suites/default+ingressSUCCESS/padding1688-1912≥840; one review no concrete defects under contract, controls1/1/1RED byteidenticallyrestored. Actualboard/accuracy/latency/errata/correlation/arrivaldelay/externalpacing/wholecost/sourceISR-mainprotocol remain qualification gates; runningrequest/anchor/coalescing/adoption/stop and740→768readiness still open. No taskcheckoff/upload/HITL/device/SD/Git/defaultactivation;29/60/defaultrollingOFF/targetcontinuationdisabled/receiptsdefaultOFF.

  **I5 — stopped-only PIT hardware qualification (2026-10-14; parents unchecked):** [I5 — hardware evidence](evidence/2026-10-13-i5-running-capture-service/PHYSICAL-HARDWARE-QUALIFICATION.md) fixes recipe/oracles before instrumentation, dedicated managed TEMP preset/full01 PASS/211points/210s/Readyera2/naturalcarry/MIDIch2/highTIFclear. Relativehostrate23.967714–24.058948MHz is grosscheckonly; observedread11.970–34.090µs/median29.555 is notWCET, stoppedpointbrackets7–22µs not movingcorrelation/deadline. HardwareC_DEBUGEN1withouthalt/sleep exposed false refusal; user explicitly approved DEC-129 — Run-mode physical-clock debug admission, genuineRED→GREEN/haltsleeprefusal/stockFRZ1retained. One focused review ringfallback/endfence repair retested; focused84/ingressnative33/host102/fullnative3196pass7skip119suites/defaultSUCCESS/padding1688≥840/numericbudgetsunchanged. Uploaded9b50680a…b770c/smoke05/full01/2drains/postflightcomplete/nolatch; later rebuiltcachef3d02128…d9581 NOTuploaded. No content/SDcleanup/destructive commands/Git/parentcheckoff;29/60/defaultrollingOFF/targetcontinuationdisabled/receiptsdefaultOFF. Movingclockcorrelation/absoluteaccuracy/errata/arrivalage/externalpacing/wholeownerIRQcosts/sourceISR-mainprotocol and runningrequest/anchor/coalescing/adoption/stop remain open.

  **I5 — moving PIT correlation/host pacing qualification (2026-10-14; parents unchecked):** [I5 — motion evidence](evidence/2026-10-13-i5-running-capture-service/PHYSICAL-MOTION-QUALIFICATION.md) pins separate gated motion row/all-track Capture/pending-action/storage guard/TEMP managed preset beforecode. Full01 OBSERVED:51matchedReadyera2rows/15movingpoints/internal+USB-device120/300BPM/stable per-leg epochs/physicalbrackets9–31µs/guard15 beforeall3Starts. Internal residual−408..520.5µs and300BPMhostsend0.005291..23.401125ms (catch-up) are observations, not calibratedaccuracy/WCET/wireage/boundedpacing. Exact HEX96e4160a…d6be24/ELFc801239e…0c75bf retained before verified no-build upload;2drains/postflightcomplete/nolatch/no content or SDcleanup, ordinary autosave possible. Review2findings fixed/retested, realclockguard35/ringtier56/host105/fullnative3203passed7skipped119suites/defaultSUCCESS/padding1688unchanged≥840/no budget rebase/observation56BbothABIs. [I5 — owner/IRQ qualification](evidence/2026-10-13-i5-running-capture-service/OWNER-IRQ-QUALIFICATION.md) records finite completeowner admission NOTREADY; calibration BLOCKED by no independent calibrated source, wireage BLOCKED by only host USB-device input. No running successor/sourceconcurrency/740→768readiness/checkoff;29/60/defaultrollingOFF/targetcontinuationdisabled/receiptsdefaultOFF/no Git.

  **I5 — complete ingress-hook/mask native prerequisite (2026-10-14; parents unchecked):** [I5 — ingress mask qualification](evidence/2026-10-13-i5-running-capture-service/INGRESS-MASK-QUALIFICATION.md) pins and delivers native-only/defaultOFF/targetrejected mask seam, actualreceiver samplerfrequency/refusal/pressure/FIFO and realcounter + receipt/dispatch helper nestedmask assertions on incoming0/1. Original target PRIMASK/owners/sourcepins/transforms/state unchanged; original DIN unconditionalenable distinguished, not repaired. Baseline13RED/restore10RED/duplicatesampler3RED restored byteidentically; one focusedreview2oracle gaps fixed/retested. Receiver18/receipt9/ordinaryclock35/ingress45/retained47(pre-review assertions)/host30/ASan18/fullnative3207passed7skipped119suites/defaultSUCCESS/padding1688/variables96192 unchanged/no budgetrebase. Native workcount/mask proof only, no targetelapsed/wholeIRQ/WCET/arrival/calibration/readiness claim; next gated target duration+overhead slice must use these controlled boundaries. Parent29/60/defaultrollingOFF/targetcontinuationdisabled/receiptsdefaultOFF/no checkoff/upload/device/SD/Git; cached1771efe7HEX NOTuploaded, prior evaluated motionimage retained.

  **I5 — ingress-cost arithmetic prerequisite (2026-10-14; parents unchecked):** [I5 — cost qualification](evidence/2026-10-13-i5-running-capture-service/INGRESS-COST-QUALIFICATION.md) fixes conditional full-wrap/refusal/saturation oracles before target instrumentation. Passive caller-owned MidiIngress header only, no firmware caller/resident instance or build/default/target change. Independent local noncopyable tokens/exact-once fault consumption and separate hook/mask/publication/empty-probe lanes; continuity/rate bound and enclosing extended-time brackets remain producer prerequisites, not endpoint-inferred facts. Native12/neighbor87/ASan12/host30 PASS, 1,028 generated short observations, omit-wrap3specificRED with live header unchanged; ARM/host sizes32/8/32/128/40B endpoint/contract/stats/aggregate/token. One focused review no concrete bug, non-1MHz rounding coverage suggestion added/retested. Fullnative3219passed7skipped120suites/0failed; default build verification and final closeout recorded in linked packet. No target duration/wholeIRQ/WCET/calibration/arrival/sourceconcurrency/740→768readiness or checkoff/upload/device/SD/Git;29/60/rollingOFF/targetcontinuationdisabled/receiptsdefaultOFF preserved. Next target opt-in producer/protected publication/overhead and managed observations remain unimplemented.

  **I5 — gated target ingress-cost observations (2026-10-15; parents unchecked):** [I5 — target cost execution/evidence](evidence/2026-10-13-i5-running-capture-service/TARGET-INGRESS-COST-EXECUTION.md) supersedes the preceding passive-only next step. Existing-owner default-OFF explicit opt-in wrappers/protected384B aggregate payload/strict12row command/managed TEMP stopped Clock-only120/300BPM delivered. Native3228passed7skipped121suites/host112/ASan8/ON+OFFbuilds/one repaired focused review; floor840unchanged. Smoke02/full01 PASS each8complete snapshots/6Readyera2guard15; full USB hook deltas239/598/exact1:1:3:1/no new rejection/no saturation. Lifetime hook21699..33039/mask5895..15983cycles conditional instrumented only, not exclusiveIRQ-off/fullIRQ/WCET/calibration/arrival/readiness. Exact ON HEX61a91281…f26603/ELFcf0ef724…994f73 preserved/verified no-build upload; exact OFF HEX4d9aa84c…18a9bd/ELFa7846154…b9d51f restored/verified. Restored02 stopped/idle/guard15/two drains/postflight/no latch; boot/storage preflight refusals retained. No Start/content/slot/SD-cleanup gestures; automatic persistence occurred. Completeowner/IRQ finite bounds/sourceISR-mainprotocol/running request-anchor-coalescing-adoption-stop remain open. No parentcheckoff/Git/defaultactivation;29/60/rollingOFF/targetcontinuationdisabled/receiptsdefaultOFF.

  **I5 — builder-call cancellation/live-source reads (2026-10-15; parents unchecked):** [I5 — source concurrency packet/evidence](evidence/2026-10-13-i5-running-capture-service/SOURCE-CONCURRENCY-EXECUTION.md) records DEC-130 — builder-call cancellation and live-source exclusion. Both builder calls retain executing state, refuse re-entry, latch discard and resolve false/Denied; runtime reset requests cancellation/returns false before destructive work. Fixed saved-mask descriptor/currency/one-row or charged-transition reads, no whole-grain mask. Registered baseline lost begin cancellation + Fill SIGSEGV; focused477/ASan9/original4case witness PASS, one focused review approved under retained-source/serialized-external-writer preconditions. Target builder256/runtime7792 unchanged; host400/9976. Final broad native/default build and RAM1 evidence owned by packet. Complete Track role/publication/adoption protocol, running request/anchor/coalescing/stop and finite owner/IRQ/reference/wirearrival bounds remain open; no740→768 readiness/checkoff/upload/device/SD/Git/defaultactivation,29/60 unchanged.

  **I5 — exact Track completion authority (2026-10-15; parents unchecked):** [I5 — Track publication evidence](evidence/2026-10-13-i5-running-capture-service/TRACK-PUBLICATION-CONCURRENCY.md) records DEC-131 — exact Track completion authority: destination-generation-qualified completion/query/cleanup; final role/storage/operation receipts before judgement and commit; short saved-mask publication/adoption; ready consumption never promotes private completion; explicit main bridge-owner publication. Four baselineREDs fixed/six newcases/focused468/ASan6/wrong-mask2RED/liveheader unchanged/one focused review approved and coverage added/retested. Fullnative3243passed7skipped121suites/defaultcaptureSUCCESS/padding1368≥840/margin528/no budget relaxation. Targetbuilder264/runtime7808, host408/9992 (+16/runtime); targetTrack/Loop unchanged. No upload/device/SD/Git/defaultactivation/checkoff,29/60. Acquisition-before-builder-entry/direct Record release-start-bind-seek-projection, running request/anchor/coalescing/stop and finite owner/IRQ/calibration/wirearrival remain open; no740→768 readiness.

  **I5 — preparation acquisition/Record exclusion (2026-10-15; local and compatibility HITL gates PASS, parents unchecked):** DEC-132 — preparation acquisition and Record exclusion uses paired non-masking turns through selection/commit/reset/lease retirement, actual queued Record success/pre-roll/geometry preservation and protected direct writers/bridge receipts. Eleven newcases/focused479/ASan12/four specificRED controls; one review's two findings fixed/retested/no repeat review; fullnative3254passed7skipped121suites/defaultcaptureSUCCESS/padding1160≥840/no sizepin/budget relaxation. Owner approved scratch8/4–5 only; exact default-OFF HEXf48aca66…b0068/ELF7cd88398…a3563 no-build upload verified. Registered base8/4/channel8/hostUSB300BPM `captures/i5-acquisition-compat-t8s4-01` PASS/overall_oktrue/cap_full/issuesempty/critical-loss0, Record/two overdubs/undo-redo/geometry/grid/span/cleanup/persistence pass; advisory second-overdub lateness retained, no deadline safety proof. Fingerprinted replay02 PASS/headerpresent/must-prove notskipped; raw legacy replay limitation named. Parallel native3254passed7skipped; final managedSTOPPED/closedoverlay/guard15/two drains/nolatch; newdefault-OFF image installed/scratchcontent retained. Early postflash storage-work refusal retained and settled without guard change/reset. No SDcleanup/quarantine/delete/rename/Git/defaultactivation/parentcheckoff;29/60. Running request/anchor/coalescing/stop and finite owner/IRQ/calibration/wirearrival remain open; no740→768 readiness/full inventory claim. [I5 — acquisition and hardware evidence](evidence/2026-10-13-i5-running-capture-service/ACQUISITION-CONCURRENCY.md).



  **Fixture-reuse audit (2026-10-07; inventory only, task unchecked):** carry the independent musical assertions from `test_playback_prewarm` (`test_runtime_order_builder_yields_and_preserves_same_tick_off_before_on`, `test_runtime_order_builder_preserves_equal_phase_off_before_on_across_wrap`, `test_runtime_view_build_retains_empty_non_note_and_repeated_note_id_instances`, `test_shared_window_fill_filters_before_capacity_and_preserves_loop_end_order`, `test_three_view_rotation_keeps_retired_current_idle_until_build_completes`); from `test_runtime_working_set_ownership` (`test_windowed_stream_concatenates_to_the_whole_loop_stream_across_a_boundary`, `test_window_readiness_names_its_holder_and_an_empty_interval_is_ready`); from `test_shared_capture_finalization` (`test_deferred_record_interval_serves_beyond_bar_one_from_the_frozen_source`, `test_record_return_lookahead_readies_every_boundary_under_owner_contention`); and from the real-EditManager `test_windowed_preview_integration` (`test_current_root_note_edit_open_keeps_the_committed_playback_interval`). These are input traces/oracles to **port** to a production entry→owner→clock reader fixture, not proof that their current direct-builder or merged-reader calls are already the single path. Do not copy assertions that require the old prepared-pair/fixed-bridge/whole-take reservation or inspect `mergedEvents` as final authority. Existing registrations such as `test_full_take_admissibility_follows_the_windowed_policy`, `test_windowed_slot_upgrades_to_whole_take_at_a_cold_admission`, `test_release_refuses_while_a_bridge_is_selected`, the bridge-specific tests and `test_stopped_oracle_publishes_a_fixed_current_view_without_growth` stay active as **temporary compatibility/cold-parity protection** while their production branches still run; remove/relabel each only with its disabled branch at task 3.7b and after the replacement output/ledger fixture is RED when that route is removed. No fixture deletion or 3.15a checkoff from this audit.
  **Mutation polarity:** removing the **new unified reader** or one of its accepted source routes must turn the replacement fixture RED; removing an old pair/bridge/merged emitter **after** the cutover must leave the same fixture GREEN. No production branch or registered legacy test is retired by the audit itself.
  **3.15a — one-reader short-loop fixture G2 packet (specified; fixture NOT IMPLEMENTED; task unchecked, 2026-10-08):** Owner `Track::stepPlaybackViewMaintenance`; production entry `TrackManager::updateAllTracks` → `Track::playMidiEvents` / `playMidiEventsForSlot` → `Track::playCommittedLoopMidi`; publication and retirement through the existing role-owned `LoopPlaybackRuntime` views. Primary harness: extend the real-edge `track_harness.inc` seam already used by `test_windowed_preview_integration` and `test_shared_capture_finalization`; no new scheduler or fake emitter. Preconditions: prewarmed short-loop sounding slot and a known generation/revision, with all accepted content committed; use controlled tick advancement and a bounded explicit sequence of deferred owner grants, not sleeps or a whole-take prebuild. Mutation: **test only** — capture the ordered output and ledger across initial coverage, empty interval, same-tick Off/On, wrap and a preview-source revision change. Postconditions: the served view is complete and identity-qualified `(slot, generation, revision, interval)` before adoption, current remains immutable during build, MIDI/ledger match independently written expectations, and an ungranted owner turn neither builds on the edge nor consumes a partial candidate. Preserve the old registrations and fallback while live; no firmware policy change, branch removal, global buffer, admission-limit change, or device-deadline claim. Commit boundary: a standalone test-only characterization may land first; the *one-reader* assertion remains expected-red/unlinked until 3.15b/c/d routes exist, and 3.15a cannot check off on characterization alone.
  **Oracle and reuse mapping:** In `test_playback_prewarm`, use `test_runtime_order_builder_yields_and_preserves_same_tick_off_before_on`'s CC@10 / Off@20 / On@20 as an expected output sequence, and `test_runtime_order_builder_preserves_equal_phase_off_before_on_across_wrap`'s Off@loop-end / On@0 as a wrap sequence; do not assert `view.storage.order` from the main reader. Use `test_shared_window_fill_filters_before_capacity_and_preserves_loop_end_order`'s filtered boundary content as a coverage input, but do not treat a direct `RuntimePlaybackViewBuild` call as production publication. From `test_current_root_note_edit_open_keeps_the_committed_playback_interval`, retain committed two-pair output on open, the edited On@800 and ledger identity `2001`, and the single targeted Off@840 after deletion; replace the direct `ensurePlaybackMergedMidiEventsBuilt` and `mergedMidiEvents` cardinality/order assertions with window identity, emitted MIDI and active-note ledger observations once NOTE_EDIT uses the reader. These existing builder/legacy fixtures remain registered for their own owners. A **short-loop production** case is missing; the long-loop NOTE_EDIT test does not establish short-loop NOTE_EDIT. Include both lengths in the migration oracle.
  **Controls / RED-GREEN / verification:** Record fixed content, loop length, channel, tick sequence, owner-grant count, expected MIDI bytes/order and ledger state after each edge, plus source release after supersession. A pre-cutover legacy characterization may be GREEN but is *not* single-reader evidence. The migrated fixture must be RED when the unified reader or its short-loop source route is disabled; only after that route is enabled and parity is GREEN may disabling its old emitter leave the same fixture GREEN and permit 3.7b retirement. Focused commands: `pio test -e native -f test_playback_prewarm`, `pio test -e native -f test_windowed_preview_integration`, then the new fixture's native suite; full `pio test -e native`, default `pio run -e teensy41-capture-serial` with RAM1 guard, strict OpenSpec validation and diff check for the eventual cutover. Device timing/continuity is NOT RUN and remains gated by 3.15f/6.5. Evidence: this task note until a fixture-specific packet records actual commands/counts and a discriminating mutation; no checkoff or removed registration from this specification.
  **E2 frozen-return fixture-first increment (2026-10-08; 3.15a unchecked):** `UNIFIED_RECORD_RETURN_READER_ORACLE` conditionally enables the native rolling lane in the shared-finalization harness and reuses the registered two-bar real Record stop / sealed-range bar-2 ready oracle. The flagged `test_record_return_bar_two_uses_unified_window_reader` is behavioral RED at the production `Track::playMidiEvents` edge (`RollingPhase::Serving` expected, `Idle` actual) while 76 other focused cases pass; legacy bar-2 MIDI alone cannot pass its rolling-role/last-tick gate. Downstream physical view, exact MIDI bytes, channel/pitch ledger and duplicate-edge assertions remain behind this missing-reader gate and are NOT RUN. Unflagged focused 460/460 and full native 2,872 passed / 7 skipped. G0 pins the separate live-Capture barrier: first-bar raw coverage has no final loop geometry, VisualCache still builds it outside `Track::stepPlaybackViewMaintenance`, and source identity carries no Capture append generation; a coherent G1 Capture-generation/provisional-geometry/stop handoff contract is required before production Record cutover. See [`E2 — frozen Record bar-2 fixture`](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2--frozen-record-bar-2-new-reader-fixture-first-boundary-2026-10-08). No default gate, fallback removal, firmware edit or task checkoff.
  **E2 mutation inventory refinement (2026-10-08; design only):** [The E2 execution packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-capture-mutation-inventory--read-only-g0-refinement-2026-10-08) now traces same-count replacements, in-place stop/Snapshot edits, clear/retire and direct Track sealed-range transfers. A Loop-owned generation and job-frozen pre-transfer receipt require helper-level change classification and atomic transfer wiring; three-view fixtures, ABI/LoopPool cost and affected-tick service remain owed. No firmware Apply, reader cutover or 3.15a–f checkoff.
  **E2 initial live-Capture fixture and G1 red-team input (2026-10-08; task unchecked):** `UNIFIED_RECORD_CAPTURE_INITIAL_ORACLE` conditionally registers a real TrackManager/Track start→deferred owner→first-edge case. After 64 Track owner grants the current image is RED at complete new-reader silent current (`owner progress=0`, legacy raw shell `complete=1 published=0 raw=1 coverage.loopLengthTicks=0`); no Capture reader adoption or downstream append/stop MIDI claim. Unflagged focused 460/460 and full native 2,872 passed / 7 skipped. A G1/G2 draft in the [E2 execution packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2--live-capture-to-frozen-record-g1g2-design-input-2026-10-08-no-firmware-edit) was red-teamed: the three physical views are occupied by a legacy raw shell, the Track owner and edge both exclude Record, and a row-count/16-bit display token cannot generally guard Capture mutation. The corrected existing-Loop-owner contract names a 64-bit Capture content generation, Capture-origin view identity, provisional projection, three-role migration, exact stop hold and source/edge guards. **No Capture production Apply yet**: prove all mutation-site token bumps, initial/mid-build/stop RED controls, target ABI placement and measured affected-tick service before default enablement. The old emitter and its tests remain live; 3.15a/b/c and 3.7b unchecked.
  **E2 late-Capture next-traversal fixture (2026-10-08; task unchecked):** The flagged initial Record case now pins an accepted On@700/Off@740 with an interleaved Track-owner grain, unchanged silent physical `current`, two-row complete `ready`, and On@1468/Off@1508 through the same new reader with exact MIDI/ledger and duplicate-edge silence. It still fails earlier at the initial raw-shell/no-owner gate, so these downstream clauses are **NOT RUN**. Unflagged focused 460/460 and full native 2,872 passed / 7 skipped; strict spec/diff checks pass. A scratch-only target ABI probe measured hypothetical +8 B per Loop and per source identity, but actual job/view placement, RAM1 and affected-tick service remain open. No firmware edit, default cutover, device result or checkoff; [E2 fixture packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-late-append-next-traversal-oracle--fixture-first-test-only-slice-2026-10-08).
  **E2 / 3.15b — Capture-content generation prerequisite (2026-10-08; task still unchecked):** `Loop` now owns a 64-bit mutation-sensitive live Capture token and a successful sealed-range cut freezes its pre-transfer value on the existing Track stop job; no Capture window consumes it yet. Native tests prove accepted vs refused input, same-count dirty sort, in-place ID assignment, clear/removal, failed/successful range cut and the real Record stop receipt; a restored sort-bump suppression is RED. Review found the existing Record stop's post-seal empty-count preparation bound; a new real multi-chunk fixture RED 209/137, now GREEN using pre-cut scalars. Focused 229/229, full native 2,874 passed / 7 skipped, default capture build RAM1 padding 888 (48 above approved floor), Track 384 B unchanged, Loop 992 B and external ring 28,968 B. No Track-owned Capture `current`/ready/new-reader selection, affected-tick bound, device qualification or default enablement; 3.15a–f and 3.7b remain unchecked. [E2 source-receipt packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-capture-content-generation--looprecord-stop-prerequisite-g2-2026-10-08).
  **E2 / 3.15b — distinct Capture-origin borrowed source (2026-10-08; task unchecked):** `PlaybackViewSourceIdentity` now carries `Capture` and Loop's 64-bit content token independently of Track operation generation and committed Snapshot identity. `RuntimePlaybackViewSource::fromLiveCapture` borrows live producer rows with the existing generation hook; private fill/order/Complete rejects same-count mutation and refuses a legacy frozen descriptor labeled Capture. Four focused primitive oracles turned behavioral RED then GREEN; one review's post-fill and reverse-origin findings repaired/retested. Focused prewarm **201/201**, full native **2,878 passed / 7 skipped**, default capture SUCCESS RAM1 padding **3,688**; target Track 384 B, per-slot runtime 7,616 B and external view set 33,248 B. The default image still selects no Capture reader; the flagged TrackManager start→Track owner→edge case remains RED at the legacy raw shell before its late MIDI/ledger clauses. No Track-owned Capture `current`/three-role stop handoff, affected-tick service, device verdict, task checkoff or flash. [E2 adapter evidence](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-capture-origin-borrowed-source--fixture-first-adapter-slice-2026-10-08).
  **E2 / 3.15b — private initial Capture builder (2026-10-08; task unchecked):** Under the already disabled-by-default rolling qualification lane, real Record start now offers only the empty active slot to the Track owner at native epoch 0. Two bounded owner grants build a complete private silent Capture-origin view with provisional one-bar playback coverage, preserving the legacy raw `current/retained` and sealed Record return; accepted input cancels only the private builder on VisualCache, and real stop preserves job/range while stale raw binding refuses publication. A withheld owner-grant mutation is RED and byte-identically restored. Flagged three new cases GREEN, original full E2 new-reader case still expected RED on raw current (`owner=2`), downstream MIDI/ledger NOT RUN. Unflagged focused 465/465, full native 2,878 passed / 7 skipped, default capture SUCCESS RAM1 padding 3,688 (no default route change); one lifecycle review found no concrete defect. An epoch-0 fixed native preparation is not epoch-1 device reachability or a deadline. No published Capture current, reader/stop hold, default cutover, task checkoff, flash/SD/device run. [E2 private-first packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-first-silent-capture-current--qualification-only-three-role-transition-2026-10-08).
  **E2 / 3.15b — frozen Record source receipt adapter (2026-10-08; task unchecked):** A real playing-destination Record stop's job-frozen pre-transfer content generation is now distinct from the sealed range's read/lifetime generation in `RuntimePlaybackViewSource::fromFrozenRecordCapture`. Private bar-2 Capture-origin fill/order matches exact On@780/Off@1008 with no current-root or MIDI emission; a mismatched content receipt refuses, range release during fill denies, release after copying permits order. Legacy `fromCaptureRange` retains Committed origin and live Capture alone retains post-fill generation checks. Real-stop fixture RED before factory, focused final **424/424**, default capture build RAM1 padding **3,688**; full native final-source rerun pending. No Track owner adoption, matching/nonmatching job hold/closure, reader, affected-tick/device verdict or task checkoff. [E2 frozen job packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-frozen-record-capture-source-identity--job-receipt-adapter-2026-10-08).
  **E2 frozen receipt final-source closeout (2026-10-08; no checkoff):** Exact real-stop private Capture content/range identities now asserted distinct. Focused **424/424**, full native **2,880 passed / 7 skipped**, default capture RAM1 padding **3,688**, focused review no actionable finding. No production Track-owner frozen view/current/reader, timing or device claim; see the E2 packet's final-source receipt closeout.
  **E2 / 3.15b — Track frozen-job source qualifier (2026-10-08; task unchecked):** A real admitted Record job may now answer a request only for its exact `(slot, expected capture interval, Loop pointer/LoopId, final length, zero pending closure)` with ready sealed range and pre-transfer Capture token; its returned descriptor stays private. Real two-bar stop fixture RED before the query, GREEN after with exact private On@780/Off@1008; LoopId and expected-interval mutations separately RED and byte-identically restored. Focused 467/467, full native 2,880 passed / 7 skipped, capture build RAM1 padding 3,688. No owner-turn publication, current/ready range hold, new reader/ledger, affected-tick/device gate or default enablement; [E2 job qualifier packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-track-owned-frozen-record-source-qualification--no-publication-2026-10-08).
  **E2 / 3.15b — qualified job guards existing Track owner (2026-10-08; task unchecked):** A real Record stop with only its LoopId tampered made the old owner prepare bar-two ready (RED); the frozen-range holder offer and selected worker now require the same interval/LoopId/content/closure-qualified job before serving. Restore LoopId → exact complete legacy-format On@780/Off@1008 ready, raw first current/job range intact. One focused review found root-current replacement over-gated; narrowed frozen-only and retested. Final focused 467/467, full native 2,880 passed / 7 skipped, default capture RAM1 padding 3,688. This is not Capture-origin ready/reader adoption or timing; [E2 owner gate](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-frozen-record-job-qualification-reaches-existing-track-owner-2026-10-08).
- [ ] 3.15b **Use one owner and window-sized role lease for all source preparations.** `Track::stepPlaybackViewMaintenance` consumes frozen live Capture, job-owned Record range, selected current `LoopSnapshot`/pinned retained source, live overdub overlay and `EditManager` preview delta through one request/work/cancel/ready protocol. Preserve each owner's mutation authority; source supersession, undo, clear/load, slot reactivation, transport stop and late completion cannot publish a stale or partial view. Bar one is the same `current` role before and after Record stop; already-bound coverage is not redundantly rebuilt while the next affected interval owes work. No extra Manager, queue, global buffer, whole-take reservation per recorded slot, or stop-edge build. Fixture-first production-path interleavings and RAM1/PSRAM/LoopPool accounting are required.
  **E2 / 3.15b — legacy Record identity guard (2026-10-08; task unchecked):** A real sealed range privately built Capture-origin bar-two ready; the old `LoopPlaybackRuntime::adoptFrozenRecordWindow` incorrectly reidentified it as Committed on matching revision/Track generation (behavioral RED). Matching origin and Capture token now guard reidentification: wrong-origin/token ready refuses, already adopted Capture `current` stays intact, exact Capture adoption and Committed→Committed root swap retain their physical view. Focused native 468/468 before review extensions; final full native 2,881 passed / 7 skipped, default capture RAM1 padding 3,688. The flagged Track owner→reader Record fixture remains RED at its *earlier* Capture-ready identity; no Track publication, job future hold, reader, deadline or device acceptance. [E2 safety packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-frozen-capture-identity-cannot-pass-through-legacy-reidentification-2026-10-08).
  **E2 / 3.15b–c — frozen Record bar-two Capture reader, native subcase only (2026-10-08; tasks unchecked):** Real playing-destination two-bar stop now reaches Capture-origin Track `ready` and the existing rolling reader/ledger under the disabled-by-default qualification lane, with exact On@780/Off@1008 and stable physical ready→current. All 80 flagged Record tests pass, including legacy zero-token closures and a wrong-LoopId edge refusal. A focused review's root-current interleaving was RED until the frozen view kept its pre-publication revision; actual deferred retirement before and after adoption was separately RED on premature source release, then fixed to hold matching Capture ready/current. Reader-removal RED and Track-grant RED controls restored. Full native 2,881 passed / 7 skipped; default capture build RAM1 padding 3,688. Live first-Record `current`, repeat wrap/future demand/release, service/timing and pinned device remain open; [E2 integrated packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-frozen-record-readyreader-integrated-qualification-slice-2026-10-08).
  **E2 / 3.15b–c — one frozen Record next-wrap successor qualified natively (2026-10-08; tasks unchecked):** A flagged real two-bar Record stop prepares Capture-origin bar-one ready while its bar-two current remains immutable; the same reader adopts that physical successor at wrap and emits exact On@3084/Off@3168 with ledger/duplicate-edge parity. Before change rolling was Blocked at wrap (3 expected/4 observed). Withholding only the successor's Track grants or disabling only Serving→ready adoption separately turns the fixture RED; restored flagged shared-finalization 81/81, full native 2,881 passed / 7 skipped, default capture build padding 3,688, strict specs/diff clean. Root-current wrap, future-demand release/cancel, live first-Record current, affected-tick bound and pinned device verdict remain open. [E2 wrap packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-frozen-record-next-wrap-continuation--fixture-first-2026-10-08).
  **E2 / 3.15b–c — root-current next-wrap G2 BLOCKED (2026-10-08; test-only, tasks unchecked):** Registered flagged real-stop variant publishes the complete root before Capture bar-two edge and demands Committed full-loop `[0,1536)` ready for the next wrap from the existing Track owner. The root holder is offered, but a correctly stamped request at bar-two phase1008 has only 528 ticks left and remains named `CannotCompleteBeforeHandoff` (2) after 128 owner turns; Capture current/physical role and job range stay intact. The root reader/ledger clauses are NOT RUN; flagged suite 81 passed / one expected failure / Unity ERRORED, unflagged full native 2,881 passed / 7 skipped. No firmware or admission-policy edit. Investigate useful private root work before Capture-ready adoption versus a measured stronger composed service envelope; do not lower the declared deadline, count old MIDI as success, or check off 3.15b/c. [E2 root wrap packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-root-current-next-wrap-handoff--g2-fixture-first-2026-10-08).
  **E2 / 3.15b — root-current owner preserves owed frozen Capture ready (2026-10-08; task unchecked):** Real stop/root publication followed by a stamped Track owner turn one tick before the frozen bar-two edge previously retired the ready role to `kNoView` (behavioral RED), so the Capture reader could not adopt. The root-current branch now leaves complete job-token/Track-generation/geometry-qualified Capture ready intact for that edge; turning off only this guard makes both root-before-edge cases RED, restored flagged suite 81 passed / one separate expected root-wrap `CannotCompleteBeforeHandoff` failure. Full native 2,881 passed / 7 skipped; default build RAM1 padding 3,688, strict specs/diff clean; focused review found no concrete defect. The existing three-view lifecycle can promote a completed third build after ready adoption, but committed qualification currently refuses while ready is complete: early root work needs its own G2 admission/role/cancellation contract, not a ready retirement or lowered deadline. [E2 ready guard packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-root-publication-while-frozen-ready-awaits-adoption--g2-safety-slice-2026-10-08).
  **E2 / 3.15b — early root request survives Capture ready and clock rebase (2026-10-08; task unchecked):** A real stopped Record/root-published owner turn at tick1537 now retains a full-loop CommittedSnapshot request with the root identity, epoch1 and wrap deadline3072 while frozen Capture ready/raw current/job remain physically unchanged. Pre-fix both root-before-edge cases were RED at missing request; suppressing just issuance reproduces them. Focused review found that a stale pending epoch could block a new request: separate real-stop epoch2 control was RED at epoch1 until the exact-pending worker check and narrow job-qualified holder restamp opportunity were linked; disabling only that holder restamp makes the new case RED. Final flagged shared-finalization 82 passed / one expected root-current-wrap `CannotCompleteBeforeHandoff` failure; full native 2,881 passed / 7 skipped; default build RAM1 padding 3,688; strict specs/diff clean. The request is only named waiting work, **not private root qualification or ready publication**: `coverageRoleAvailable` still refuses while Capture ready is complete, and root reader MIDI/ledger remains NOT RUN. [E2 root offer packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-root-current-full-coverage-offer--g2-shortcut-audit-2026-10-08).
  **E2 / 3.15b — private root source qualified behind Capture ready; HITL timing gate next (2026-10-08; task unchecked):** Real stopped Record/root-published fixture now uses existing third `build` role for bounded `LoopSnapshot` source qualification while raw first current and frozen Capture bar-two ready remain immutable; source inspection advances and can complete privately. Focused review found completion could consume/retire the source before ready freed; two production-path cases RED at `AwaitAdmission` vs `Retiring` until the exact job/root/epoch/ready wait predicate and a defensive admission refusal preserved the frozen request without monopolising owner turns. Epoch2 invalidation cancels only private root work; a separate native edge variant shows source survives Capture adoption, then fails the *unchanged* remaining-work contract by named `CannotCompleteBeforeHandoff`. Flagged shared-finalization 84 passed / one expected root-wrap failure / Unity ERRORED; unflagged full native 2,881 passed / 7 skipped; default build RAM1 padding 3,688, strict specs/diff clean. No root ready/reader, affected-tick or device verdict. Task 3.10/6.3/6.5 requires a pinned lane-enabled HITL service-gap/content run before any stronger deadline claim; default rolling off, 3.15a–f/3.7b unchecked. [E2 private root packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2-private-root-source-qualification-behind-capture-ready--g2-first-grains-slice-2026-10-08).
  **E2 / 3.15b–c — frozen Capture interval index (2026-10-08; tasks unchecked):** The dense 24-bar phase473 real-stop production fixture was RED at bar 1 with 768 source rows, an eligible Track holder and no ready. The Capture store now maintains per-32-row bar membership on append (including out-of-order input), carries it with sealed chunks, and invalidates it before tick-changing rewrites. The existing Track Fill charges each summary/candidate row, skips only proven-irrelevant blocks, scans unknown/overflow/out-of-envelope blocks conservatively, and preserves the same source-generation/readiness/reader contract. The flagged dense fixture is GREEN with four early bars × 16 exact On/Off pairs and ledger closure; removing index selection makes it RED again. New direct native controls cover out-of-order late rows, overrun NoteOn projection and pre-seal tick-rewrite invalidation. The separate root-current next-wrap `CannotCompleteBeforeHandoff` remains RED, default rolling is off, and all-route/device/affected-tick gates remain open; do not check off 3.15b/c. [E2 bounded frozen Capture packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2--bounded-frozen-capture-interval-inspection-implementation-packet).
  **E2 / 3.15b–c — pinned device verdict (2026-10-09; tasks unchecked):** Indexed qualification HEX `908965dc…` under registered `STIM-WINDOW-CONTENT-01@v1` committed a non-span-aligned 25-bar take; device channel-8 NoteOns were absent ~4.8 s after stop although persisted early local bars contain 14/16/16/16/16/16 NoteOns. Ring integrity passed, but lane readiness receipts and whole-root publication were absent; the managed verifier exited 2. Verified two-Stop drains preserved persistence. This is a hardware continuity FAIL, not index-service proof or all-route acceptance. Keep the native 24-bar index GREEN but add a non-aligned real-stop/root-private RED oracle before any owner/reader gate change; root-current wrap remains separately RED. [E2 hardware verdict](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2--bounded-frozen-capture-interval-inspection-implementation-packet).
  **E2 / 3.15a–c — 25-bar late-ready handoff (2026-10-09; tasks unchecked):** The indexed device take stopped at local phase18,922/19,200 and its early channel-8 output gap remained ~4.8 s. A flagged real 25-bar `Track::stopRecording` fixture was behavioral RED with complete late bar-zero Capture `ready` blocking bar one's holder; a before-wrap control was GREEN. Existing `fillPlaybackHolders` and `stepPlaybackViewMaintenance` now preserve ready only at its exact boundary, retire a late job-qualified ready off-clock, name `record_ready_late` once and grant the next bar's owner. The late case serves exact next-bar MIDI/ledger on the same reader under one Track turn per 64 advanced ticks, without mid-bar replay; a skipped-entire-bar marker control also passes after focused review. Flagged 90 cases: 88 passed, one **separate** root-current `CannotCompleteBeforeHandoff` failure, one Unity harness ERRORED. Unflagged focused 470/470, full native 2,883 passed / 7 skipped; default/qualification builds SUCCESS RAM1 padding 2,856 (floor 840), Track still 384 B. **HITL not run:** preflight managed drain timed out with zero-byte capture and no USB modem device on bounded recheck; no verified persistence idle, flash/reset or SD clear. Native test links real Track owner and playback but not full TrackManager dispatch; bar-zero affected-tick, root-current, all-route and device gates remain open. No 3.15a–c/3.10/3.7b checkoff. [E2 late-ready packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2--unaligned-record-return-late-ready-handoff-2026-10-09-g2-proposal-unverified).
  **DEC-136 — private committed-root preparation before Capture-ready adoption (2026-10-16; native GREEN, task unchecked, device gate NOT run):** The held frozen mismatch Stop's stranded `CaptureStopJob` is fixed natively. The recorded root-current next-wrap `CannotCompleteBeforeHandoff` RED is *not* closed by weakening the envelope: `Track::rootReturnSuccessorIsOwedForSlot` classifies only the demand that is the bound Record job's qualified frozen reader's **root-backed** successor, and `Track::ordinaryAdmissionCannotServeDemand` (`!canCompleteWithinLead(loopLengthTicks)` on the demand's own derived cost) is the sole waiver, so the lead term is waived only where the envelope proves no legal lead exists; the demand is marked `PlaybackViewPendingReason::PrivateRootReturnPreparation`, carries **no position** (no fabricated deadline) and keeps every other requirement (candidate validity, single successor role, sibling promises, atomic boundary adoption, S7.5b.5 release). The Record-retirement lane also no longer intercepts the turn while a coverage build needs service (the admitted build is served by the existing grain tail). Registered reproducer `rollingRecordHeldMismatchStopWithDeferredOwners` RED→GREEN, plus two new contract fixtures; focused **57/57**, full native **3 294 / 3 287 succeeded / 7 skipped / 0 failed**, both builds SUCCESS with the RAM1 floor intact (`padding 1,080` / `888` ≥ 840; the first rolling build failed at `760` from an ITCM-resident lambda, now a `TRACK_COLD_MEM` helper), strict OpenSpec valid. **Attempt 5 was not spent: no Teensy on USB**, so the device half — and with it 3.15b–c's reader/content/affected-tick evidence — stays open. [DEC-136](../../../docs/DECISION_LOG.md#dec-136--private-committed-root-preparation-before-capture-ready-adoption) · [I5 packet](evidence/2026-10-13-i5-running-capture-service/PRIVATE-ROOT-RETURN-PREPARATION.md).
  **DEC-137 — the held-leg Stop oracle is DEC-136 aware (2026-10-17; host tooling only, no checkoff).** The attempt-7 device red was the *contract's*, so the owner decided it: update the held leg's `stop_handoff` contract to DEC-136's designed terminal state with its own discriminating change. `stop_handoff` now reads the ordered post-stop sequence; the matching-Stop branch and `corrected_frozen_handoff` are unchanged (growth/empty still pass), and the held leg passes only with the new `corrected_frozen_root_return` — a bound frozen serving phase carrying **the take's own frozen token** (the frame may be reported unbound because the truncation rewind re-anchors the projection while the reuse branch keeps the same physical view), then a released job serving Committed-origin content at the frozen geometry that is actually being read. Named refusals cover every other shape (never discharged, release not terminal, early release while the frozen Capture content still serves, unqualified successor, geometry not the take's, not serving, no frozen receipt while bound, invalid or empty observation). `full_recipe_complete` additionally requires the **held** leg to reach that terminal, and the scenario requires the released samples to serve the take's own accepted content. Evidence: the retained traces replay as designed — the route images (`b83c7f6d…` `i5-dec136-releasefix-attempt7-02`, `f11444ab…` `i5-dec136-fix-attempt6-02/03`) give the root return, while the pre-route stranding image `919e17e6…` stays **RED on all three of its captures** (`stop_root_backed_successor_not_observed`; job bound for the whole window, no Committed takeover); focused **16/16**, four mutations each killing exactly their own case(s) with byte-identical restores, `test_accepted_sequence` **194/194**, and the full host sweep **1 818 / 7 failures / 3 errors** against a **measured pre-change baseline of 1 817 / 7 / 3** with an **identical** failing set (none in this change's modules). **Recorded limits:** the pre-completion and completion route images are indistinguishable in this table, so the release edge's discrimination stays with the native reproducer; and the full recipe is still not `PASS`, because no leg in it produces `matching_physical_reader_preserved` (all three stops are mismatching) — that separately named matching-physical-Stop coverage is untouched by this update. No firmware/build/upload/SD/device action, device on the exact default `f48aca66…`, no checkoff, 29/60. [DEC-137](../../../docs/DECISION_LOG.md#dec-137--the-held-leg-stop-oracle-is-dec-136-aware-the-frozen-serving-phase-the-root-return-and-the-release-edge) · [I5 — packet § Held-leg contract update](evidence/2026-10-13-i5-running-capture-service/PRIVATE-ROOT-RETURN-PREPARATION.md#held-leg-contract-update--the-owner-decision-and-the-dec-136-aware-stop-oracle-2026-10-17).
  **DEC-138 — the non-truncating Stop terminal is the preserved reader, and the recipe has a matched leg (2026-10-17; host tooling only, no checkoff).** The recipe's last requirement was unsatisfiable for two independent reasons. (1) **No producing leg:** all 23 recorded device stops across 12 captures are truncating (`raw_length` ≈ 6400 for the eight-bar legs, ≈ 3400 for the held leg, against `final_length` 2304/1536) because every leg joins its fixture stream before pressing Record-stop and the join/read latency (~330 ticks) puts the press past the take's own wrap. (2) **An unsatisfiable terminal:** the matching branch compared `epoch`, `frameOrigin`, `framePeriod`, `boundRole`, `boundStorage`, `currentRole`, `currentStorage`, `cursor` and `ledger` for equality, while every recorded stop advances `epoch` by one and **clears** the positional frame (`frameOrigin`/`framePeriod` 0, `boundRole` 255, `boundStorage` 0) until a successor is adopted, preserving only `(currentRole, currentStorage)` — in 8 of 9 stops inspected. The terminal is now specified from that measured behaviour (era advanced, **same serving view**, the take's own frozen token/geometry, a **consistently cleared** frame or the ordinary serving terms if still bound; named refusals `stop_matching_era_not_advanced`, `stop_matching_reader_not_preserved`, `stop_matching_frame_inconsistent`, `stop_frozen_geometry_or_token_mismatch`, `stop_responsibility_unproven`), and a new `matched` leg produces the class: three streamed bars, the stop pressed in the **pre-wrap third window** of the take's cycle (`1950 <= phase <= 2150`) **before** joining the stream, so the extent never passes the bar-aligned loop length; its claim is the Stop class only (inventory/interval rotation stay with growth/empty, recorded explicitly). `full_recipe_complete` is now leg-attributed and the leg set is membership not a count (extra legs allowed and must pass), and the scenario records `stop_terminals_by_leg`. Evidence: retained traces unchanged (attempt7-02/attempt6-03 growth+empty `corrected_frozen_handoff`, held `corrected_frozen_root_return`; the pre-route stranding image `919e17e6…` still RED); a designed matched trace returns `matching_physical_reader_preserved` while the truncating receipt is refused; a four-leg recipe built from the real attempt-7 evidence plus a matched leg makes `full_recipe_complete` **True** for the first time (and **False** without it); focused **18/18**, five mutation controls each killing exactly their own case(s) with byte-identical restores, full host sweep **1 820 / 7 failures / 3 errors** with the **same failing set** as the measured baseline. **Owed: one device run** — no board was attached, and the matched leg's timing is a labelled hypothesis derived from the measured `raw_length`/`final_length` relation; a truncating press refuses by name, and a rebuild at a non-truncating stop refuses `stop_matching_reader_not_preserved`. No firmware/build/upload/SD action, device on the exact default `f48aca66…`, no checkoff, 29/60. [DEC-138](../../../docs/DECISION_LOG.md#dec-138--the-non-truncating-stop-terminal-is-the-preserved-reader-and-the-recipe-covers-each-stop-class-from-its-own-leg) · [I5 — packet § The matching terminal and the matched leg](evidence/2026-10-13-i5-running-capture-service/PRIVATE-ROOT-RETURN-PREPARATION.md#the-matching-terminal-and-the-matched-leg-2026-10-17).
  **I5 — rolling-Record device recipe PASS (2026-10-17; aggregate `PASS`, 6.4/3.15f still unchecked).** One campaign on the DEC-140/141 qualification image `32f6501d2051bc38af046c204d2e38e6773987f2cc3ee36714ada7069e13acaa`, track 8 / slot 4: `growth-empty`, `held` and `matched` each ran as a separate managed pass over the same image and host-verifier fingerprint and each returned `SMOKE_ONLY: []`; the offline aggregate then issued the real verdict — `verdict=PASS`, `ok=true`, `full_recipe_complete=true`, `issues=[]`, `pass_groups=['growth-empty','held','matched']`, `stop_terminals_by_leg={growth: corrected_frozen_handoff, empty: corrected_frozen_handoff, held: corrected_frozen_root_return, matched: matching_physical_reader_preserved}` (`captures/i5-slot45-diagnosis-01/campaign-01/`). Device restored to the pinned default `08f43e45…` no-build with a PASS postflight. **This is the rolling Record route's device acceptance, not 6.4/3.15f:** 3.15f requires every migrated route (overdub, NOTE_EDIT, switch, undo/redo, internal/external clock) compared against its own oracle and the cold legacy reference, and 6.5's four hardware claims (grain duration, maximum service gaps, sustained multi-track contention, memory high-water marks) are NOT RUN. Both tasks stay unchecked at 29/60.
  **DEC-141 — the matched leg PASSES on the device (2026-10-17; host tooling + fixture timing, no firmware change).** With DEC-140's stall fixed the leg reached DEC-138's terminal and refused `stop_matching_era_not_advanced`; two of that terminal's terms proved unsatisfiable by measurement — the clock `epoch` never changes across a Record stop (`4 → 4`, `8 → 8`), and `captureToken == before.liveToken` compares against a receipt that advances with accepted input (serving `2` while live had reached `4`). Replaced with measured facts: Stop processed = the pre-Stop sample was RECORDING (post-Stop non-recording is already enforced by `stop_snapshot_invalid`) plus the existing `stopJob` ownership check, and served content = the pre-Stop *serving* generation (`captureToken == before.captureToken`); every other DEC-138 term stands. Fixture timing also fixed: the grid press moved 36 → 46 clocks so the post-Stop query lands inside the cycle (`matched-fix-01` had sampled at tick 776 of a 768 cycle, after the wrap rotated the reader), and the narrow `256..440` pre-Stop phase window was dropped (`rolling_snapshot_not_recording` in `matched-fix-02`); `recording` + one-bar geometry + `rawMax >= 200` already prove a valid pre-Stop sample. **Device:** `matched-fix-04` on qualification `32f6501d…`, track 8 / slot 4 — `stop_outcome = matching_physical_reader_preserved`, `issues = []`, `functional_legs_ok = true`, `stop_terminals_by_leg = {matched: matching_physical_reader_preserved}`, drain complete, `stop_job_state = idle`. Verdict `SMOKE_ONLY` (correct: only the `matched` group ran). Host gates `test_rolling_record` 20/20, native 5/5, import gate and stimulus contract green, strict spec valid, diff clean; device restored to `08f43e45…` with a PASS postflight. **6.4/3.15f stay unchecked:** one campaign must produce `growth-empty` + `held` + `matched` on one image and host-verifier fingerprint for recipe `PASS`. [DEC-141](../../../docs/DECISION_LOG.md#dec-141--the-matched-terminals-two-unsatisfiable-terms-are-replaced-by-measured-ones-and-the-leg-passes-on-hardware).
  **DEC-140 — the matched `RecordPublished` stall is found and fixed (2026-10-17; native RED→GREEN, device-verified, no checkoff).** The device reproducer was reduced to a native one: `test_unadopted_frozen_capture_successor_does_not_park_the_record_job` builds the device's stalled shape (stopped transport, root current, the job's own frozen-`Capture` successor complete and unpublished in ready, no reader frame) and hung at `steps=8192 pending=1 phase=16 root=1 exitQueued=0 hold=0 bound=1 readyComplete=1 readyPublished=0 readyOrigin=2(Capture)`. **Cause:** retirement's un-bounded gate waiting for that successor to be "adopted or cancelled by the Track window owner" — unsatisfiable with the transport stopped — which also sat above DEC-136's hold and kept its counter at zero. **Fix:** delete the gate and make the same successor a second `owedSuccessor` case inside the existing bounded hold (audible ⇒ normal wait, count zero; no boundary ⇒ count and expire through `discardCoverageBuild()` + `releaseFrozenRecordWindow()`). No new field/queue/view/owner; RAM1 byte-identical (`392,136 + 1,080` and `392,280 + 936` ≥ 840). New suite **5/5** (the new case is the red→green proof), full native **3 303 / 7 skipped / 3 296 succeeded / 0 failed**. Device: qualification build `32f6501d…` on the fix — `#STOP_STATUS` reaches `pending=0, queue=0/4` and the heap observation is complete, so the leg no longer fails `record_evidence_drain_incomplete`; it now stops on the separate DEC-138 terminal `stop_matching_era_not_advanced`. Device restored to default `08f43e45…` with a PASS postflight. **3.15b–c stays open** on that era term; no recipe PASS, 6.4/3.15f unchecked. [DEC-140](../../../docs/DECISION_LOG.md#dec-140--the-unadopted-frozen-capture-successor-is-bounded-the-matched-recordpublished-stall-is-found-and-fixed) · [I5 — packet](evidence/2026-10-13-i5-running-capture-service/MATCHED-RECORDPUBLISHED-RETIREMENT-PACKET.md).
  **DEC-139 — the matched one-bar `RecordPublished` retirement is sound in isolation (2026-10-17; no firmware change, no checkoff, device gate still open).** The failed `matched` leg left the Record job bound at `phase=16` for 60 s with `build_active=0`, `current=0`, `ready=1`, `build=2`, `retained=255` and a stale root. Before any patch, a production-path reproducer was built: `test_rolling_record_retirement` compiles the finalization suite's real Track/Loop/storage closure **with `PLAYBACK_WINDOW_ROLLING_ENABLED`** (the qualification image's shape; without it `Track::completeAdmittedRecordStop` cannot take the preserved-reader edge at all) and drives only existing owners — real accepted input → `serviceFirstRecordingBar` on VisualCache turns → `Track::stopRecording` at a pre-wrap tick (one-bar geometry kept) → the real deferred grain to `RecordPublished` → the real six-owner `MidiServiceDrain` selector plus `Track::processDeferredIdleMaintenance` → the window lane's offer/RR/`stepPlaybackViewMaintenance` → `Track::playMidiEvents` as the clock-edge reader. **It does not stall:** both Snapshot gates admit the stopped job (`canServiceBackgroundSnapshotOwnerTurn`, `backgroundSnapshotWorkAllowed`), the Snapshot owner publishes the queued root, root currency alone does not release the frozen source, and the job then retires with the served reader **root-backed** (`Committed` origin + current `publishedSnapshotIdentity`), queue empty, bound return window cleared and sealed extent returned — the designed handoff, not the `supersedeDeferredStopJob` escape. Red-proved by one mutation (`canServiceBackgroundSnapshotOwnerTurn`'s `RecordPublished` term → `false`) killing exactly that case at the eligibility assertion, restored byte-identically (`sha256 7a01e659…`). Four shapes pass (Playing destination ending on the root-backed reader; the device's own **STOPPED**/not-audible destination; an audible sibling Track), and four gate-level hypotheses are closed by evidence: `pendingSnapshotSessionExitQueued()` implies `hasPendingSnapshotPublish()` so the owner can never be "eligible but idle", `allowSnapshotIdleReserve` returns true when not timing-critical, an audible sibling does not exclude the job, and DEC-107 has no build to stall (`build_active=0`). Gates: **4/4** new suite, **85/85** shared suite unchanged, full native **3 302 / 7 skipped / 3 295 succeeded / 0 failed** (pre-change 3 298 / 7 / 3 291; delta = these four cases). **No production source line changed**, so no build/upload/RAM1 work is owed and no device/content attempt was made or re-authorized; the board stays on the restored default image. **The device cause remains unknown:** next diagnostics are which resolved track/slot the device's Snapshot dispatch actually served (`selectedTrackIsRealtime` vs `selectStoppedSnapshotTrack`, neither native-linkable), whether `hasPendingSnapshotPublish()` was true while `pendingSnapshotSessionExitQueued()` was, and whether the 20-drain `!TRACE_DRAIN` fence loss shares the cause. 3.15b–c remains open and its `matched` device evidence stays failed. [DEC-139](../../../docs/DECISION_LOG.md#dec-139--the-matched-one-bar-recordpublished-retirement-is-sound-in-isolation-the-device-stall-needs-state-this-shape-does-not-carry) · [I5 — packet](evidence/2026-10-13-i5-running-capture-service/MATCHED-RECORDPUBLISHED-RETIREMENT-PACKET.md).

  **E2 / 3.15a–c — pinned post-upload HITL observation (2026-10-09; tasks unchecked):** Qualification HEX `b58a3960…` on registered `STIM-WINDOW-CONTENT-01@v1` recorded a 25-bar stop on track 8 slot 4; device channel-8 NoteOns arrived +1.059 s after stop, then five complete BAR windows emitted **13/16/16/16/16** NoteOns with max inter-onset gap **0.133 s** (zero gaps >0.5 s). Ring critical-loss counters were zero, Snapshot root published (`rv=0→1`), display leg passed. Combined verifier exit 2 still reports zero rolling readiness receipts/build completions under transport; no complete accepted-content dump was obtained for this take, so exact MIDI inventory parity and one-reader device acceptance remain open. The first post-run shutdown timed out with an empty capture and set the safety latch; a later non-reset fenced read-only probe plus **two verified Stop→drain cycles** recovered persistence idle. Latch remains; no reset or second run. The prior 4.8 s device gap is not reproduced here, but this is **not all-route/HITL PASS** or task checkoff. [E2 execution evidence](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md#e2--unaligned-record-return-late-ready-handoff-2026-10-09-g2-proposal-unverified).
- [ ] 3.15c **Unify clock-edge window adoption and emission for short/long, Record-return and layered/jam routes.** Replace the migrated route's fixed/bridge/pair/merged selection with one completed-window reader, emission cursor and active-note ledger contract. A short loop has coverage spanning its length if it fits the selected tunable span; wrapping does not replay, omit or tear events. A missing/late successor retains the prior valid source for its permitted traversal and names the owed interval, never rebuilds or sorts inline. Native traces under cross-track contention and a capture-build timing measurement must pass before enabling a route; keep its old emitter as a named compatibility fallback until 3.7b.
  **Started 2026-10-08 (3.15b/c — short-loop owner/reader packet; no cutover/checkoff):** [`evidence/2026-10-08-s315bc-short-loop/EXECUTION.md`](evidence/2026-10-08-s315bc-short-loop/EXECUTION.md) pins one-bar short-loop output, immutable role handoff, generation/switch refusal and the new-reader mutation polarity through the existing production entry/owner/edge. The rolling lane currently refuses short loops and non-active slots and fixes coverage at two bars; a guard-only relaxation cannot fulfill this task. The packet explicitly retains the default-image fallback and the 3.15a — one-reader production oracle all-route prerequisite. Implement the discriminating oracle before changing reader policy; do not treat a legacy-only green musical trace as proof of this task.
  **First evidence:** `test_short_committed_loop_edge_baseline_preserves_two_wrap_traversals` is registered and PASS (**178/178** focused ownership suite): exact two-wrap MIDI and ledger through `TrackManager::updateAllTracks`, plus the old rolling selector's short-loop exclusion. It characterizes the still-active legacy emitter only. The owner-turn/adoption/refusal fixture and removal-of-new-reader RED proof remain unimplemented; no firmware file, test registration for an obsolete branch, or task checkbox was changed by this baseline.
  **G2 correction (2026-10-08; one-bar active handoff BLOCKED, task unchecked):** registered `test_one_bar_short_loop_does_not_bypass_the_declared_owner_deadline` proves the existing `CoverageServiceContract` cannot admit even an empty replacement before the next one-bar wrap: `768 lead + 2 grains × 8 turns × 64 ticks = 1,792 > 768`. Focused ownership suite **179/179**. The first-slice packet now records cold-prepared initial coverage plus a separately qualified affected-tick successor as the preferred design input; it does **not** authorize lowering the bound, shifting MIDI, treating fallback as a deadline pass or dropping one-bar loops. Resolve the service/admission contract and establish a new-reader RED production oracle before this one-bar firmware cutover; no task box is checked.
  **Readiness-contract refinement (2026-10-08; design only, no checkoff):** A complete, source-qualified `current` needs **zero additional build grains or fixed bar margin** on an unchanged wrap; do not request a fresh one-bar window just to replay that interval. For accepted late Record input and other affected-tick changes, 3.15b/c must show incremental useful preparation and a separate worst-case *remaining-work* proof: `M + (G_remaining + K) × H × Δ ≤ D_affected`, with every source lookup, order, closure, stop-geometry, publication, same-track sibling and across-track opportunity charged. `M` is a demonstrated residual for that workload, not today's automatic 768-tick extra reserve; `Δ=64` is an opportunity gap, **not** a complete build. With today's two/three grains and H=8, even `M=0` gives **1,024/1,536 ticks**, still above one bar. Task 3.10 — bounded owner service and 6.3/6.5 — sweep/device qualification must establish a smaller remaining-work and/or stronger service contract before claiming late one-bar input ready; do not simply zero `leadTicks`, weaken acceptance, or credit a fallback. Preserve 3.15a — one-reader production oracle and all-route checkoff gates.
  **Unified delivery order (2026-10-08; design only, all 3.15 tasks unchecked):** (1) 3.15a — one-reader production oracle freezes exact output/ledger and source-lifetime traces for Record start, late last-bar input, changed final geometry, stop return, one-/four-/64-bar loops, simultaneous soundings, NOTE_EDIT, overdub, switch, undo and restored density; old-route musical output alone is not proof. (2) 3.15b — single-owner preparation moves live Capture, frozen Record, selected committed/retained, overdub and preview onto one slot-qualified `current`/`ready`/`build` lifecycle, preserving complete served content and useful incremental work. (3) Task 3.10 — bounded owner service and 6.3/6.5 — sweep/device qualification must bound **the composed source-owner plus playback-owner service**, including Record first-bar preparation now on VisualCache turns, Snapshot progress, finalization, persistence and display contention. A deadline-aware opportunity belongs to the *owed affected interval*, not an always-first Record/overdub mode; preserve existing fairness and non-starvation, and run the formal timing-model review before firmware arbitration changes. (4) 3.15c/d/e — one clock reader adopts completed source-qualified windows for all routes; 3.15f — all-route cutover qualification, then 3.7b — legacy emitter removal only after exact MIDI/ledger, RAM1/PSRAM and device continuity pass. Reuse the old path during migration without crediting it as a new-reader or deadline pass. The previously measured ~2.121 s default-image post-stop gap is a **failure with cause unassigned**; a pinned qualification trace must distinguish missing request, source-not-ready, denied service, stale role, publication and edge-adoption failures before assigning the scheduler as its cause.
  **Formal end-state timing review / new-path trace contract (2026-10-08; no checkoff):** [DEC-116 timing-model reassessment addendum](ARCHITECTURE-REASSESSMENT.md#2026-10-08-addendum--end-state-timing-model-review-dec-116-sourcewindow-obligation) names the owners, seven-kind outer rotation, source/window/reader boundary, rejected blanket-priority and whole-root-gate alternatives, and the formal timing trigger. [End-state trace packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md) defines E1 — unchanged complete new-reader `current` and E2 — accepted Capture/Record changes through that same reader as **behavioral outcomes, not instructions to seed a legacy first-bar buffer, prepared pair or whole-take view**. One-bar MIDI is independent input inventory; only a Track-owned, source-qualified complete window selected at the production edge qualifies. E3 — unneeded whole-Snapshot turn withheld stays GREEN, E4 — genuinely required source-owner turn withheld goes RED; new-reader/window RED controls, full retained-source release and per-slot liveness remain required. The already-gated long-loop fixture qualifies only its own committed route. No one-bar wrap/Record source adapter or all-route reader fixture is linked; device NOT RUN, no new stimulus or marker, and no old-gap/scheduler attribution. Keep task boxes open until route and hardware gates pass.
  **Production-path long-loop oracle increment (2026-10-08; 3.15a remains unchecked):** `test_long_committed_window_owner_to_edge_selects_the_reader` drives the already-gated 24-bar committed route through the real TrackManager tick and deferred owner, checks ready/current source and physical storage identity, exact On/Off plus ledger, duplicate-edge suppression and no hot build. Withholding the needed window-owner grants turns its readiness assertion RED; disabling qualification reader selection turns its adoption assertion RED. Both controls restored and focused native **180/180**; full native **2,861 passed / 7 skipped / 0 failed**. See [end-state trace packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md). This is not a one-bar E1/E2 fixture, required source-owner RED control, affected-tick service proof or device qualification; old emitters stay live.
  **E1/E2 correction and partial E1 continuity (2026-10-08; no checkoff):** E1/E2 specify new-reader outcomes, not legacy initialization. The gated long-loop fixture now serves its second bar with zero further owner turns and asserts exact bar-7 MIDI/ledger, unchanged physical `current`, no new request/hot rebuild. Focused **180/180**, full native **2,861 passed / 7 skipped**, both active specs strictly valid and diff whitespace clean. It does not cross a short-loop wrap or exercise Record; the one-bar and Capture/frozen-source adapters plus affected-tick proof remain prerequisites. See [end-state trace packet](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md).
  **3.15b/c cold-current entry audit (2026-10-08; no checkoff):** The short-loop packet's C1 — new-reader initial current now requires a real off-clock Track-owned `[0, loopLength)` request and physical current before the first edge, with no old fixed/first-bar buffer counted. Current production prewarm does not issue that request; the inactive launch API excludes the active slot and hardcodes two bars, positioned one-bar admission refuses the changed deadline, fixed/launch selection precedes rolling, and rolling serving expires after two bars. A guard-only change cannot be accepted. The cold C1 fixture and qualification-only route can be implemented before the separate *changed-successor* composed-service proof, but may not enable default playback or claim Record E2. The legacy short-loop baseline stays registered. [Corrected short-loop execution packet](evidence/2026-10-08-s315bc-short-loop/EXECUTION.md).
  **C1 — cold short-loop reader fixture RED (2026-10-08; 3.15a/b/c unchecked):** A gated `UNIFIED_SHORT_READER_COLD_ORACLE` case drives the real prewarm → Track window-owner → TrackManager edge for accepted one-bar content without test-written views or a legacy-buffer success condition. It fails at the missing complete `[0,768)` `ready` after 256 bounded cold owner turns (**expected RED**, 180 existing cases green); unflagged focused **180/180**, full native **2,861 passed / 7 skipped**, strict active-spec validation and diff check pass. Link the qualification-only cold path coherently, then turn this case GREEN and RED-prove reader/grant removals. Changed successor still requires its own affected-tick service proof before enablement. See [short-loop execution packet](evidence/2026-10-08-s315bc-short-loop/EXECUTION.md).
  **Cold C1 — epoch-0 lifetime G2 stop (2026-10-08; no firmware edit/checkoff):** Production `prewarmPlaybackForSlot` also runs on Record-start MIDI input, so only resource admission belongs there; the deferred Track owner must discover the cold obligation. Current retry rejects epoch-0 `CommittedSnapshot`, qualified-build admission rejects an unpositioned frozen request, and rolling completion requires a positioned/playing frame. `CommittedLaunch` cannot substitute because switch provisioning cancels non-target launch and its bound view expires. A bounded implementation pass reran the flagged expected-RED C1 (180 other cases green) and unflagged focused 180/180, then stopped without changing source. The [short-loop packet](evidence/2026-10-08-s315bc-short-loop/EXECUTION.md) names the coupled pending→source→build→ready→first-edge and cancel/retirement contract owed before any guard changes. Changed one-bar successor and all-route gates remain separate/unmet.
  **C1 — cold one-bar committed reader linked under qualification gate (2026-10-08; tasks 3.15a–c remain unchecked):** Existing Track owner now discovers/prepares a complete epoch-0 full-loop window off-clock while STOPPED; first stamped eligible edge adopts it into the rolling reader and keeps physical `current`/ledger across unchanged wraps without a new grant. Early PLAYING before ready waits and may join at a later phase-0 boundary; source revision supersession refuses stale ready, names missed affected-tick debt and re-prepares when STOPPED. Three C1/lifecycle fixtures run in the normal ownership suite. Focused ownership+finalization+prewarm **452/452**, full native **2,864 passed / 7 skipped**, default capture build SUCCESS RAM1 padding **968** (128 above floor); reader-removal and owner-grant controls RED and restored. One independent review's cross-slot cancellation finding remains for C3 — simultaneous sounding slots; the selector-only attempt was reverted to preserve the live stop/slot-exit test. No default route enablement, device timing, changed one-bar successor service bound, E2 — Record handoff, old-emitter removal, or task checkoff. [C1 execution evidence](evidence/2026-10-08-s315bc-short-loop/EXECUTION.md).
  **C3 — two-sounding-slot G2 fixtures RED; switch shortcut withdrawn (2026-10-08; 3.15b/c unchecked):** The gated `UNIFIED_LAYERED_READER_ORACLE` exercises two accepted slot-qualified one-bar sources and a real TrackManager switch; after 512 bounded turns both complete new-reader ready windows are not available. A second gated fixture uses a valid two-bar outgoing note On@700/Off@800 and a NextGrid switch at 768: the unchanged switch clears the held outgoing ledger with 12 CC123s. Focused independent review found the no-replacement `requestSlotSwitch` is internal API-only (current button gestures request enabled-set replacement) and the original short note ended before the switch. A conditional CC123 suppression was attempted and **reverted**: it kept the held ledger open but lost the outgoing Off@800 because `commitQueuedPlaybackStart` re-anchors the Track projection. No partial layered reader, global-silence change or C3 pass remains. `coldRollingShape` now retains C1's complete stopped ready across extra off-clock turns with a registered assertion; normal focused **452/452**, full native **2,864 passed / 7 skipped**, default build RAM1 padding **968**. C3 requires a reachable keep-enabled gesture/API policy and slot-qualified projection/closure handoff with bounded edge work before the switch may stop using CC123. [C3 packet](evidence/2026-10-08-s315bc-short-loop/EXECUTION.md); [timing-model correction](ARCHITECTURE-REASSESSMENT.md#c3-correction--conditional-cc123-suppression-withdrawn-2026-10-08).
  **C3 session-manager partial integration (2026-10-08; 3.15a–c unchecked):** Three independent read-only lanes traced a reachable `OVERDUB_FOR_SLOT` double-press retention route, per-slot phase/closure displacement, and active-only cold owner plus null-cursor fairness gap. Disjoint write sessions then changed only `TrackPlaybackHotPath.cpp` (qualification-only layered phase from `Loop::startLoopTick`), `TrackPlaybackWindowBuild.cpp` (enabled non-active cold one-bar role/reader and outgoing Serving retention), and the guarded ownership fixture. The production action branch was extracted into `MidiButtonActions::handleOverdubForSlot` for host linking without changing gesture semantics. The real-action fixture passes exact MIDI/ledger and outgoing physical window retention. Two API-only queued-switch fixtures remain expected RED on unchanged CC123/global ledger closure, so C3 is not closed. Normal focused **452/452**, full native **2,864 passed / 7 skipped**, default capture build RAM1 padding **904** (64 above floor); one focused independent diff review is pending. Positioned successor fairness, incoming overdub Capture through the reader, mixed lengths/origins, CC123 closure policy, device timing and all-route cutover remain open. [Execution packet](evidence/2026-10-08-s315bc-short-loop/EXECUTION.md).
  **C3 focused review repair and phase oracle (2026-10-08; no task checkoff):** An independent read-only review caught a rootless pinned-source cold request stranded when transport begins during retained-window gathering. A fixture with 144 accepted events (>128-event page) now forces that production source-owner continuation; its pre-fix readiness assertion was RED, its required-grant control RED, and after a request-identity-qualified epoch0 correction it is registered and GREEN. The same review found the aligned one-bar action fixture did not discriminate Track-vs-slot phase: a registered reachable-action outgoing two-bar On@700/Off@800 fixture now passes at tick800, and restoring the shared Track origin turns it RED. That phase result still uses the long-loop compatibility emission path, not the unified reader. The review's previously dirty Record-stop tests directly stepping Snapshot publication remain untouched and are not credited as production-admission proof. Final normal focused **454/454**, full native **2,866 passed / 7 skipped**, default capture build padding **904**; flagged C3 two queued-switch API oracles remain expected RED on unchanged CC123. Fair positioned service, live overdub Capture through the reader, all-route closure/device gates remain open. [C3 packet](evidence/2026-10-08-s315bc-short-loop/EXECUTION.md).
- [ ] 3.15d **Serve short and long NOTE_EDIT from the same interval window reader.** `EditManager` owns identity-qualified session changes and preview lifecycle; Track composes the requested committed interval with changed/hidden/added/deleted identities under `origin == Preview`, freezes its source per build, and adopts it atomically. Opening without edits preserves committed content; mutation, in-session undo/redo, re-focus and exit cancel or re-version only the affected preview, close removed soundings selectively and hand back to committed currency without a whole-loop mutable session projection. Native fixtures cover empty/partial edits, mid-grain supersession, ledger continuity and source retirement; display remains RWO S4e's own consumer.
- [ ] 3.15e **Preserve bounded dense/legacy and overdub content through the single reader.** The existing paged storage becomes an internal bounded window delivery, never an alternative clock-edge stream; restored over-limit rows emit completely and in order through bounded pages rather than being retroactively refused for density. Same-tick accumulation and note-off/closure capacity keep their approved named admission or closure refusal only at its owning boundary; loop wraps do not duplicate or omit accepted events. Live overdub Capture remains a source overlay with exactly-once note/ledger ownership; no second writer to `current` or hidden whole-loop materialization. Exercise short and long lengths against one independent effective-content oracle and the approved capacity envelope.
  **Design-intent tightening (2026-10-08; not a fixture decision):** 3.15b/c must offer, serve, adopt and release coverage for **every sounding slot** (including non-active layered/jam slots), with slot-qualified identities and no second `activeLoopIndex` authority; an active-slot change alone must not evict a still-sounding slot. 3.15d must compose an empty edit delta over committed interval content and allow a genuinely empty effective interval as ready silent coverage, with source-qualified selective closures on supersession. 3.15e must remove independent scheduled live-Capture replay and edge sorting as that route migrates: input passthrough/append are separate, but overdub MIDI and ledger events belong to the same reader exactly once through wrap, stop and pass handoff. Choose data representation, owner turn scheduling and fixture mechanics in the implementing slices; these are outcome constraints, not a new buffer, queue or task checkoff.
- [ ] 3.15f **Qualify all-route cutover and hand off continuity evidence.** Under the existing 3.10/6.3/6.5 service gates and task 3.14/4.5 handoff/closure gates, compare every migrated production route against its independent MIDI/ledger oracle and the cold legacy reference; run the registered content profile plus simultaneous-track Record, overdub, NOTE_EDIT, switch, undo/redo, persistence/display contention, internal/external clock and RAM1/PSRAM high-water checks. The default image cannot claim qualification-only lane rows; use a correctly pinned device image and its own readiness/content verdict. Name missed deadlines and refusals as failures, not fallback passes. Only when **all** supported routes meet these gates may 3.7b remove compatibility emitters and RWO S7.5 accept combined continuity.
- [x] 3.15g **Wrapped short-loop Record finalization — separate immutable presentation lifetime from the interaction FIFO.** Owner-approved mechanism: **DEC-142 — Record presentation lifetime split**. The stable slot-owned extent is admitted before finalization; nonempty `RecordPublished` activates presentation reader ownership, releases the finalizer and pops the interaction FIFO synchronously. The accepted successor stop can then close its own slice while the frozen reader remains represented. Preserve the live-finalizer pre-publication reader path, off-clock preparation/E2 adoption, ordinary admission, current-root takeover and bounded stopped-successor expiry. No ring-owned RecordPublished impersonation, speculative retirement hold, deadline bypass, default rolling activation or RAM1 budget change. The earlier positioned-demand diagnosis below is retained as historical evidence, not the current repair contract.
      *2026-10-19 — DEC-143's overlay now reads the sealed extent (native + builds green; device owed).* The recorded next
      step ("hand the gather the presentation's sealed `rawRange`") is implemented at both owners. `Loop::
      tryGatherPublishedSnapshotEventsInWindow`'s `RecordOverlay` branch reads the take's sealed extent through this
      Loop's own read borrow (`captureReadSource().finalizing`, keyed on the slot-owned `CaptureStopPresentation`), with
      `CaptureRange::read` revalidating generation/pool occupant/seal per row; rows pass `projectRecordStopTick`, are
      filtered to the half-open window, tick-sorted, and lanes left open are closed **once at the loop end only when the
      window is the loop** (a partial window's off may be a later window). `passes.recordPass.committedChunkIds` is no
      longer a source. `ensurePlaybackMergedMidiEventsBuilt` states only the publication edge
      (`hasPublishedRecordPresentationForSlot`) and keys the reuse guard on the extent's own address, so the pre-edge
      empty build cannot be served after the edge and an unchanged extent performs no gather. Fixtures:
      `test_loop_take_survival` +4 (withdrawn form's refusal control; wrapped-take projection + whole-loop-only closure;
      gather idempotence; undone pass refused) with a seal→borrow→commit fixture, and `test_rolling_record_retirement` +1 production-path case
      (`test_the_merged_fallback_serves_a_published_record_slice_before_its_root`: non-empty window at `RecordPublished`
      with no root, one rebuild, 16 no-op turns, one rebuild at the root, no duplicate note). Red-proved by two mutations
      (Track predicate off → only that case fails; Loop source off → exactly the five serving cases fail, control green),
      both restored byte-identically. One focused review's two findings and coverage gap are fixed and re-verified (incremental overdub admit keeps its wrap
      while the overlay serves; an undone Record take is refused by pass state; a failed row read is a whole miss; the
      negative gate is covered). Gates: focused 356/356; full native **3,323 / 7 skipped / 3,316 succeeded /
      0 failed**; both builds **byte-identical** (`392,088 + padding 1,128`; qualification `392,376 + padding 840`, floor
      840 unchanged); `git diff --check` clean; strict validation valid. One neighbour assertion updated in
      `test_deferred_record_interval_is_audible_only_with_a_bound_window` (the layered entry now serves the take's pair,
      because that fixture binds the post-publication owner split; the "no fixed view for this target" intent is kept).
      **Device NOT RUN** — the qualification image is not flashed, and the last attempt on this branch hung the board, so
      the flash waits for owner approval. Task stays unchecked; 6.4/3.15f open.
      **Device (approved):** qualification `7f48a41b…` flashed; `STIM-BASE-2-2-01@v1` on 8/4 → `INCONCLUSIVE_CAPTURE`, sole issue `persistence_active_snapshot_unavailable_during_save` (5/8). The overlay **served on hardware** (`record_overlay` build, 93 events, 41.642 s) and was **superseded once** by the root (`snapshot` build, 42.743 s), yet the wire stayed silent until the wrap after the root published (first note 43.216 s = 3.076 s after the stop). The remaining gap is **emission-side**; no verifier/bound/floor touched; default `0cf0f538…` restored and the read-only postflight PASS.
      **Audible-gap fix (same day, device re-run):** the survivor was a **latched phase reset** in the clock-edge reader — an unknown previous phase (`lastTickInLoop == UINT32_MAX`) plus a playhead past the first bar returned without writing the phase back, so the pass stayed silent until the wrap. One-line fix + RED `test_a_post_stop_phase_reset_does_not_silence_the_rest_of_the_pass` (red-proved); focused 254/254; qualification `392,376 + 840` unchanged. Device `captures/i5-dec143-phase-seed-base-01`: first note **112 ms after the overlay build** and before the root published; silence **3.076 → 1.712 s**, the survivor being the stop→publication interval DEC-143 leaves unreadable; witness coverage **5/8 → 8/9** with the remaining window (`slot:7:4`) inside that interval, which needs an **owner decision** (seal-time eligibility, or a frozen window covering the playhead's bar).
      **Option 2 (owner-selected): current-window frozen return progression, DEC-143 edge intact.** The frozen reader's first window is the one the playhead is in (then forward; the pass's last tick keeps the wrap rule) and a complete frozen ready is adoptable mid-window under the `Committed` or `Capture` frozen identity (qualified first, never relabelled). RED `test_the_first_frozen_return_window_covers_the_playhead_interval`, red-proved twice; focused 496/496; qualification `392,376 + 840` unchanged. **Device `PASS`, `issues: []`** (`captures/i5-frozen-current-window-base-01`): stop 40.735 s → frozen serving 41.464 s → overlay 42.375 s → root 43.264/43.278 s; witness windows 8/8. Residual **corrected**: a per-pass recount shows no pitch articulated twice (the earlier flag was a coarse-scan artifact around a pass boundary), so no identity reconciliation is owed.
      [Execution packet](<evidence/2026-10-19-dec143-sealed-extent-overlay/EXECUTION-PACKET.md>).
      *Measured evidence.* Device, held-leg probe: `DIAG pend=1 kind=1 reason=7 epoch=0 deadline=0 admitted=1536 invalid=1` with `cov=0/768` and the frame `Idle`/`epoch 0` ([`ROLLING-RECORD-HITL-EXECUTION.md`](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-HITL-EXECUTION.md), § "the consumed work-record fix is falsified"). Rolling-on 2-bar base, two runs: the stop is **accepted and latched** (`DIAG,latch,accept,7,3,2,0,playing`) and never consumed, the job stays `#STOP_STATUS … pending=1,phase=16,queue=1/4` with the session-exit root already current (`snap_gate hp=1,rv=1`), and the lane refuses the loop (`win_window_ineligible`, `win_not_servable`) — [`EVIDENCE.md`](evidence/2026-10-18-rolling-base-2bar-rerun/EVIDENCE.md).
      *Current acceptance.* (1) Real admitted wrapped Record reaches publication with FIFO empty and presentation/extent retained; an accepted successor stop executes exactly once independently. (2) Real admission plus closure can prepare/adopt/emit bar two before pass publication, without activating retirement early. (3) Actual current-root reader takeover discharges presentation through ordinary deferred turns even when no window holder owes work; a multi-chunk extent remains owned through partial release and re-arms before subsequent stop admission. (4) Stopped unadopted successor retirement is bounded and disabling presentation retirement fails the registered anchor with FIFO empty. (5) Full native, one focused lifecycle review's concrete findings, both firmware/resource gates, strict validation, and the registered qualification-image base 2-bar device recipe reach its second overdub and undo/redo acceptance. Waiting-stop observability remains with **S4f.8 — waiting accepted-stop receipts**; all-route cutover remains separate.
      *2026-10-19 verification.* **DEC-142 — Record presentation lifetime split** and all four review repairs implemented/discriminated; device attempt1's empty-successor extent loss RED→GREEN (empty overdub jobs reference no range). Final native3,304PASS/7SKIP/0FAIL,122suitesPASS; focused479/479; default/qualification buildsPASSpadding1,144/856≥unchanged840; strict/diffPASS. Corrected qualificationa2313c1d… registeredbase2bar8/4 (`i5-presentation-review-base-02`) reaches both overdubs/undo-redo,32/16/8 phase note pairs,latency7/4clocks≤12,stopjobidle. **Overall INCONCLUSIVE/EVIDENCE_MISSING**, not PASS: only `persistence_active_snapshot_unavailable_during_save` remains (6/9 active windows witnessed; unmatched slot:7:4/5/6). Verifier unchanged, task stays unchecked. Exact prior default08f43e45… restored/hashverified; fresh finalpostflightPASSSTOPPED/pending0/queue0/4/bothheaps/twodrains/nolatch. Next is trace-based discrimination of the missing playback/persistence witness, not a speculative retirement change. [Presentation execution packet](evidence/2026-10-18-rolling-base-2bar-rerun/PRESENTATION-OWNER-NATIVE.md).
      *ATTEMPTED 2026-10-18 — the RED is NOT yet reachable; acceptance (3) is now a green native control.* Built
      `test_wrapped_two_bar_record_stop_retires_while_the_transport_runs` in `test_rolling_record_retirement`
      (rolling enabled, production owners only): a take that **wraps** its bucket (`raw > final`, two-bar geometry, so
      `win_window_ineligible` for `av`), reaching `RecordPublished`, a **live overdub started while the job is
      pending**, the session-exit root published **during** that overdub, then the overdub's stop edge — and the whole
      chain completes: the job retires, the accepted stop executes, the queue empties, within a few thousand 16-tick
      steps. It is green, so the wrapped 2-bar geometry **plus** a live accepted stop does not reproduce the board's
      `pending=1,phase=16` stall; the case is therefore kept as the guard that a future repair must not break, and its
      *OPTION 1 LANDED (2026-10-18) — the existing receipt carries it.* The Tier-A `win_frozen_readiness` receipt gained
      three **appended** fields (`mask`,`result`,`why`; nothing reordered, parser tolerates >=15 fields), emitted at the E2
      edge before and after an attempt and cadence-bounded in `DMAMEM`; fits the line (`96,192 / 392,360 + padding 856`,
      default unchanged) with full native **3,305 / 7 / 3,298 / 0**.
      *CORRECTION: THAT TRANSITION IS EXCUSED BY THE CRITERION; THE COMMIT HANDOFF IS SOUND (2026-10-18).* The definition
      said the ownerless set is illegal *unless the same transition records satisfaction/retirement*, and the campaign rows
      satisfy it: at the flagged instant the run carries `RECS,stop` ×1 and `Recording stopped` ×1 (the commit = the
      recorded satisfaction), and the next row is `win_gate … x=win_track_not_playing … od=1` — the lane deliberately
      excluded while the job owns the work (the designed DEC-136 exclusion). The native control for the edge
      (`test_the_record_stop_commit_keeps_a_retained_committed_demand`) **passes**: `commitRecordStop`'s matching branch
      clears only the **live-capture** recipes it supersedes, so a retained `CommittedSnapshot` demand survives. **No fix is
      owed at the commit handoff**; my 'illegal transition' verdict was a misreading. **What remains unexplained:** after
      that handoff the owners alternate `build+job ↔ job` ~3× and settle at **`pending+job`** with no frozen and no build
      (34.6 s onward, 5/5 runs) — an owner that exists and never acts. Full native **3,313 / 7 skipped / 3,306 succeeded /
      0 failed**; both builds inside the floor.
      *FIVE-RUN CAMPAIGN ON ONE IMAGE: THE ILLEGAL OWNERSHIP TRANSITION IS IDENTIFIED (2026-10-18).* Owner-approved route 1:
      fixed image (`sha256 28479ac7…`, unchanged before/after), 5 runs, all stalled identically, with **transition**
      instrumentation instead of more end-state predicates -- one row per ownership change over
      `pending`/`frozen`/`build`/`job`, plus a flag when the job is pending and none of the three records owns the work.
      Trace (5/5 runs agree within ms; 59-62 rows each): during recording the chain repeats
      `pending → pending+frozen+build → pending+frozen`; then at **~29.5 s `pending+frozen → job` alone, flagged ILLEGAL**;
      after that only `build+job ↔ job` alternates. **The first ownerless transition is the native RED target:** as the
      Record job becomes pending, both the retained demand and the frozen obligation are released with **no satisfaction or
      retirement recorded**. In code that is the Record-stop commit path clearing `pendingViewRequest_` while the frozen
      obligation is empty and the job is pending; the fix must keep an owner across that edge (retain the demand, or record
      the satisfaction that lets the job retire). **Lane-ineligible / DEC-136 contract untouched, as instructed.** Instrument
      fits (`padding 856 ≥ 840`); no other production change.
      *RUN-TO-RUN VARIANCE MEASURED; DESK-SIDE WORK AT ITS BOUNDARY (2026-10-18).* The frozen request's state now rides
      `hold_lane`'s `result` (active, CommittedSnapshot, live-rev, invalidated, reason), because
      `beginCoverageQualification` freezes the retained demand and clears the pending record — the pending stamp alone cannot
      say whether the chain moved. Run `i5-rolling-base-2bar-18` (same build as 17) read **one** row with `mask=110` and
      `frozen: active=0`, never reaching the `mask=239` state run 17 spent 71 receipts in: two runs on one build differ in how
      far the chain gets, and at the sampled instant nothing was in flight. **So the stall is a race, not a deterministic
      predicate failure.** Desk-side work has reached its boundary: three defects fixed with REDs (stale terminal reason,
      stale retained identity — board-confirmed, cached-revision gate, plus the role argument), every nameable predicate
      measured or falsified. **Next needs an owner decision:** a multi-run instrumented campaign (several runs, per-turn
      trace of the three records) or bounding the route differently — not another single-run guess. Full native
      **3,312 / 7 skipped / 3,305 succeeded / 0 failed**; both builds inside the floor.
      *THE ROLE ARGUMENT: FIXED WITH THE CLEANEST RED OF THE SESSION, AND IT MOVES THE BOARD (2026-10-18).* The fixture
      printed both answers side by side: `canProgress=0 roleAvailable=0 roleAvailableKeepCapture=1 readyComplete=1
      bound=1`. `coverageRoleAvailable(false)` refuses whenever a complete successor sits in `ready` (by design), and
      `keepCaptureReady` exists so the DEC-136 private route may keep a **Capture-origin** successor while it builds the
      Committed one (it still requires it unpublished, Capture-origin, with the build role distinct). The retry gate called
      the default. **Fix:** `committedPlaybackRetryCanProgress` passes `privateRootOwed` there. RED -> GREEN; suite 13/13;
      full native **3,312 / 7 skipped / 3,305 succeeded / 0 failed**; both builds inside the floor. **The board moves:** run
      `i5-rolling-base-2bar-17` (`4a18287a…`) emits **71 receipts instead of 17** and reaches `retire_hold mask=239` — root
      current, frozen window served, ready complete, start = playhead, frame serving — the fully formed DEC-136 state —
      where the demand stamp reads `why=96` (**no active demand**) and later reappears as `15`/`21`. The stall remains.
      **Next question:** what consumes the retained demand exactly while that state is formed?
      *THE CACHED-REVISION GATE: FIXED (RED -> GREEN); THE ROLE IS THE BOARD'S BLOCKER (2026-10-18).* The discriminating
      measurement decided the shape: the retained record flips active/inactive *inside* a turn (`hold_lane` reads `revEq=0`,
      and 18 µs later the stamp reads `active=0`), so a start-of-turn refresh misses it and the gate judges a cached stamp.
      **RED** `test_the_retry_gate_accepts_a_committed_demand_whose_cached_revision_is_stale` (`canProgress=0 demandRev=0
      loopRev=1 rootCurrent=1`) -> **GREEN** by treating a Committed demand's cached identity as a cache in
      `committedPlaybackRetryCanProgress`: when the published root is current the demand is *for* it (foreign demands are
      refused by the source/generation/coverage terms above). Suite 12/12; full native **3,311 / 7 skipped / 3,304
      succeeded / 0 failed**; both builds inside the floor. **Board unchanged** (run `i5-rolling-base-2bar-16` `ef8b97ee…`
      identical to 15), and the row **corrects an earlier reading**: `roleFree = 0` on hardware — `coverageRoleAvailable()`
      is false with a complete successor in `ready`, so `beginCoverageQualification` cannot start. **Role occupancy is real
      on the board**; the next fix candidate is retiring that non-adoptable successor when the root is current and the bound
      job is owed a Committed successor — and what is missing is a *native* reproducer of the occupancy, because two board
      runs now agree on it while the fixture frees a role.
      *CORRECTION (2026-10-18): THE COALESCING COPY INSTALLS, IT DOES NOT OVERWRITE.* Read in full,
      `retireCoverageSource` does `if (!pendingViewRequest_.active) pendingViewRequest_ = frozenViewRequest;` — a
      whole-record copy **only when the retained record is inactive**, i.e. when there is no newer retained identity to
      overwrite: it *installs* the frozen (possibly pre-publication) identity where there was none. The `else if (…)`
      compatible branch copies `deadlineTick`/`work`/`requestedPhase`/`positionInvalidated`/`countedMisses` and **never
      `source`**, so it cannot overwrite an identity at all. Consequently the stated fix ('preserve the newer/live
      identity') is not expressible at this owner — it cannot see the published root, and a blank identity fails the gate's
      `sourceKind == CommittedSnapshot` test and the revision equality outright. The two coherent shapes are: (1) install the
      frozen obligation with a **needs-refresh** mark that the track's per-turn refresh honours, so no gate sees a stale
      identity as current; or (2) refresh **after** the turn as well as at its start, accepting one turn of staleness.
      **Choosing between them needs one measurement** — does the installed identity ever reach the gate while the root is
      current? — answerable from the existing `hold_lane` demand stamp taken beside the offer. No code changed this round.
      *THE SECOND WRITER IS FOUND: `retireCoverageSource`'s COALESCING COPY (2026-10-18).* Enumerating the writers of
      `pendingViewRequest_` named the mechanism: `LoopPlaybackRuntime::retireCoverageSource` coalesces the frozen obligation
      back with `if (!pendingViewRequest_.active) pendingViewRequest_ = frozenViewRequest;` — a whole-record copy that carries
      the frozen request's **pre-publication source identity** — while `Track::refreshRetainedDemandIdentityForSlot` writes the
      live root at the top of each lane turn. The two alternate, which is exactly the measured OFF→ON→OFF sequence: a race
      between owners, not a predicate that is merely false. **Fix next, RED first:** the coalescing copy must not resurrect a
      stale identity — preferably keep the newer identity when merging (one owner for the identity rule), alternatively
      refresh after the coalescing rather than only at the turn's start. Native RED: retain a Committed demand, retire the
      coverage source so the frozen record coalesces back, require the retained artifact to still name the live root. Full
      native **3,310 / 7 skipped / 3,303 succeeded / 0 failed**; both builds inside the floor.
      *BOARD: THE OFFER IS HEALTHY, AND `revEq` FLIPS (2026-10-18).* The offer rides `hold_lane`'s `length` now (phase,
      eligible, canProgress, hasDeadline, lead). Run `i5-rolling-base-2bar-15`: `revEq=0 roleFree=0`, offer
      `eligible=1 canProgress=1 hasDeadline=1 lead=176 ticks`, `pending=1 phase=16`. So **'the lane never offers the slot' is
      falsified** — a servable holder with an urgent promise exists and the owner turn still does nothing — and **`revEq` is 0
      here but was 1 in run 14**, i.e. the identity refresh does not hold: either `serviceRollingPlaybackWindow` did not run for
      the track in this state, or the identity is **re-written to a pre-publication revision after the refresh**. Two writers,
      one of them not the live root. **Next probe:** the write sites of `pendingViewRequest_.source` and the offer's re-stamp
      block, with a one-bit 'written by the re-stamp' flag on the same row. The OFF→ON→OFF sequence across three runs says
      *race between writers*, not a predicate that is merely false. Full native **3,310 / 7 skipped / 3,303 succeeded / 0
      failed**; both builds inside the floor.
      *AND THE OFFER PATH RESOLVES NATIVELY TOO (2026-10-18).*
      `test_a_committed_demand_is_offered_progress_after_the_frozen_handoff` — positioned, identity-current demand, frozen
      reader served, complete Capture successor in `ready`, audible transport — **passes**: the root-backed successor is
      produced and served without needing the offer's DEC-136 re-stamp block. Every state constructible from the board's own
      measurements therefore resolves natively once the deadline clock is qualified, so the remaining difference is
      **board-only** (clock/epoch transitions across the handoff, or real storage/DMA timing). **Next probe is board-side:**
      report the slot's offer (`eligible`, `canProgress`, `hasDeadline`, lead) on the existing `hold_lane` row — the last
      distinction a desk-side fixture cannot make. Full native **3,310 / 7 skipped / 3,303 succeeded / 0 failed**; both
      builds inside the floor; three green controls added across this stretch.
      *PROBED AND CORRECTED: THE FILL NEVER REFUSES; THE PREPARATION IS NEVER STARTED (2026-10-18).* A one-byte denial code
      in the fill (set at each `Denied` return, forwarded by the build) read **`denial = 0`** from the reproducer, while
      `coveragePreparation = Idle` — so `stepCoverageQualification` is never called, the state never reaches `Counting`, and
      the demand's `SourceNotCurrent` stamp is a leftover from an earlier attempt. **The last open question is holder
      selection**, not qualification: why does the lane never offer this slot as a servable holder in a state that is
      positioned, identity-current, role-free and unblocked by every other measured term? The byte was **reverted** — the
      fill lives inside `LoopPlaybackRuntime`, whose size is pinned at 7,880 B, and that pin is deliberate. A case that
      manufactured this state was removed rather than kept as a RED: its hand-assembled demand was unpositioned, so it
      measured the fixture. Full native **3,309 / 7 skipped / 3,302 succeeded / 0 failed**; both builds inside the floor.
      *THE DENIAL ENUMERATED (2026-10-18).* `LoopPlaybackRuntime::stepCoverageQualification` maps a denied qualification to
      `SourceNotCurrent` and retires the source; reading it to its origin, `RuntimePlaybackViewFill::step` (SnapshotWindow
      scope) denies on exactly six conditions: (1) the destination is already published; (2) `destination_->source !=
      expected_`; (3) `storage.eventCount != copied_`; (4) `snapshotSource_->idle()`; (5) `snapshotSource_->retiring()`;
      (6) `traversalGeneration() != baselineGeneration_` or `readUnits() != cursor_`. **Capacity ruled out**:
      `kEventsPerView`/`kWindowAdmissionLimit` are 2048 and this take's committed rows are ~160, so neither the whole-loop nor a
      one-bar window can overflow `append`. **Next:** print those six at the denial (all readable without new state) — that
      converts 'qualification denied' into one named cause. No production change for this step; the enumeration is the
      deliverable.
      *AND THE ROLE HYPOTHESIS IS REJECTED BY ITS OWN RED (2026-10-18).*
      `test_a_redundant_frozen_successor_does_not_hold_the_role_forever` (identity-current demand, redundant frozen
      successor parked in ready) fails with `rootBacked=0 roleFree=1 ready=255 build=0 retained=1 demandActive=1
      demandReason=7 prep=0 buildActive=0 ownsSrc=0`: **the role does free**, so the occupant does not hold it forever, and
      the demand is then refused one level lower — `LoopPlaybackRuntime::stepCoverageQualification`'s **Denied** branch
      stamps `SourceNotCurrent` and retires the source, so the sequence is demand → qualification start → denied → retained,
      repeated. **Last named term:** the **source qualification read** in this state (why the retained source descriptor read
      is denied), not the role, the identity, the reason or the wait. No production change was shipped for this step (the
      scoped role retirement is refuted by the RED and would be the fifth inert patch); the case is registered as a
      **deliberate RED**. Full native **3,311 / 1 failed (this RED) / 7 skipped / 3,302 succeeded**; both builds unchanged.
      *STALE-IDENTITY FIX LANDED AND CONFIRMED ON THE BOARD (2026-10-18).* RED
      `test_a_stale_retained_revision_does_not_refuse_a_demand_for_the_current_root` (failed `demandRev=0 loopRev=1`) → GREEN
      with `Track::refreshRetainedDemandIdentityForSlot`, which re-stamps a retained Committed demand's identity (revision,
      generation, published-snapshot identity, loop length) from the live root — reason, position and coverage untouched —
      and is called from the **per-turn lane service**, not from the owner turn: a refresh behind holder selection never
      runs in the state that needs it. Suite 10/10; full native **3,309 / 7 skipped / 3,302 succeeded / 0 failed**; both
      builds inside the floor. **Board:** run `i5-rolling-base-2bar-14` (`850a8592…`) reads `gates=50956` with **`revEq` ON**
      (run 13: `50948`, OFF). **Stall persists on the next term:** `roleFree` OFF with `servedCopy`/`servedIsRoot` OFF — no
      view role is free while the ready role holds a complete successor that is neither the root's window nor a served
      copy, and the handoff never reaches its own `retireSupersededReady` site because the retained-demand branch returns
      into `stepCommittedPlaybackRetry` first, where `coverageRoleAvailable()` refuses. **Next fix:** retire that successor
      as superseded in the retry path when the root is current and the bound job is owed a Committed successor; RED
      constructible from this row.
      *DECISIVE ON THE BOARD: THE HANDOFF IS ENTERED, THE RETAINED REVISION IS STALE (2026-10-18).* `hold_lane` now also
      carries the handoff block's entry terms (bits 8-15). Run `i5-rolling-base-2bar-13` reads `gates=50948`: ON
      srcCurrent/ownsRaw/servableView/audible/rootCurrent/bound, OFF roleFree/outstanding/revEq/buildNeedsSvc/servedCopy/
      servedIsRoot. So **every guard term of the current-root handoff holds and the block is entered**; the served reader is
      the frozen Capture view (no same-revision bridge, not the root's own window); and the story ends at **`revEq = 0`** —
      the retained demand's `source.playbackRevision` is not the loop's current revision, so `committedPlaybackRetryCanProgress`'s
      final equality refuses the retry. **The defect is a stale retained identity** (captured before the root publication,
      never refreshed) — the identity twin of the stale-reason defect fixed here. **Next fix:** refresh the retained
      Committed demand's `source` (revision, generation, published-snapshot identity) from the live root when it is re-raised
      while the root is published; RED constructible natively by retaining the pre-publication revision and requiring the
      retry to progress and the root-backed successor to be served.
      *AND THAT IS FALSIFIED NATIVELY TOO (2026-10-18).* `test_a_superseded_ready_successor_frees_the_role_for_the_root_backed_build`
      parks a second complete Capture-origin successor in the ready role with the root current and **passes**: the
      current-root handoff's `retireSupersededReady` does run and the root-backed build starts. So `role = 0` is the
      **symptom**, and the board differs in **whether that path is reached**, not in what it does. **Next probe:** report
      that site (is the current-root handoff block entered while the job is bound and the root current? which of its terms
      releases the ready role?) from the same receipt family and the same `hold_lane` packing — it already has a call site,
      so no new veneer and no new literal. The case stays as a green control pinning 'a superseded complete successor must
      not block the root-backed build'. Full native **3,308 / 7 skipped / 3,301 succeeded / 0 failed**; qualification
      `392,360 + padding 856` unchanged.
      *BLOCKER IDENTIFIED: NO FREE VIEW ROLE (2026-10-18).* The four remaining gate terms and the frame phase now ride
      `hold_lane` (`start` packed, `length` = rolling phase). Run `i5-rolling-base-2bar-12` reads **`gates=00000100`**:
      **`role = 0`** (`coverageRoleAvailable()` false — current, ready, build and retained all occupied), **`revEq = 0`**
      (the demand's revision differs from the loop's), `srcCurrent = 1`, `outstanding = 0`, frame **Idle** with a differing
      epoch. So the stall is not the demand's reason, not the identity rule, not the wait: a complete unpublished
      **Capture-origin** successor sits in the ready role while the root is current and is never retired, so no build can
      start and bit4 (root-backed) never sets. **Next fix (narrow):** retire that successor as *superseded* when the root is
      current and the bound job is owed a Committed successor of it — `RuntimePlaybackViewLifecycle::retireSupersededReady`
      exists for exactly this — leaving `current` untouched and E2 / hold / accepted-stop / identity rules unchanged. Native
      RED constructible from this row: drive the observed state until the ready role holds a non-adoptable complete
      successor and require a root-backed build to start. Full native with the stale-stamp fix: **3,307 / 7 / 3,300 / 0**;
      both builds inside the floor.
      *THE STALE-STAMP FIX: LANDED, NECESSARY, NOT SUFFICIENT (2026-10-18).* RED first —
      `test_a_stale_terminal_demand_reason_does_not_block_the_root_backed_successor` builds the board's state and stamps the
      demand terminal while `pagedOutcome = None`; it failed `rootBacked=0 reason=11 pagedOutcome=0 pending=1`. **Fix**
      (derivation): the two reasons that *are* the paged lane's own answer are terminal only while `pagedOutcome` still says
      so; `WindowExceedsRetainedCapacity` stays unconditional (a property of the interval's size). RED GREEN, control GREEN,
      full native **3,307 / 7 / 3,300 / 0**, both builds inside the floor. **The board is unchanged**: run
      `i5-rolling-base-2bar-11` (`ec97be8e…`) is behaviourally identical to run 10 (same receipts, stamps 15/96/21/15, same
      `pending=1, phase=16`, same `refused=track_work`). **Remaining gate terms, narrowed:** `coverageRoleAvailable()`,
      `playbackViewOutstandingGrains() != 0`, `playbackWindowSourceIsCurrentForSlot()`, the revision equality — and every
      `retire_hold` row has **bit7 (frame serving) clear**. Next probe: report those four on the same row (one more packed
      byte on the existing `hold_lane` vehicle).
      *THE LANE STATE PAIRED WITH THE DEMAND STAMP — AND THE TERMINAL REASON IS STALE (2026-10-18).* The hold now also
      emits `hold_lane` (`why` packs `pagedOutcome` in bits 0-3, `retainedGatherRefusal` in 4-7). Run
      `captures/i5-rolling-base-2bar-10` (HEX `0aae9c97…`) decodes: frozen successor built and adopted -> e2 rows with
      **bit1 (root current) and bit4 (root-backed) clear** -> `retire_hold` rows with root current and ready complete but
      bit4 clear, `hold_lane` reporting **`pagedOutcome = None`, `retainedGatherRefusal = None`**, and demand stamps
      alternating **15** (reason 7 `SourceNotCurrent`) and **21** (reason **10 `ClosuresExceedDeclaredCapacity`**) at
      instants twenty microseconds after a lane that reports no such outcome. **The demand's terminal reason outlives the
      outcome that produced it**, and a terminal reason is never retried — so the job never retires and the accepted stop
      waits. **The fix is in the derivation, not a bound:** a terminal reason must not survive its producing outcome
      (re-derive/clear it when the live outcome no longer says so, or let the retry gate read the live outcome). The RED
      is the invariant 'a terminal reason implies its producing outcome is still set', falsifiable natively by setting the
      reason and clearing the outcome. **Run 09 is a separate arm** (no frozen receipts, `snap_gate hp=0`, same stall): two
      entries, one symptom.
      *THAT LEAD IS REJECTED NATIVELY (2026-10-18).* Tested as an invariant, not accepted: the observed-state fixture
      records the demand's reason across every driven step and fails if the bound job's demand is ever stamped
      `ClosuresExceedDeclaredCapacity` (the reason whose contract calls it terminal). Brought up to the board's own content
      (~32 notes/bar, take stopping at ~1920 raw ticks so the bucket stays two bars), it is **green**: `terminalSteps=0`,
      root-backed successor adopted, job retires, accepted stop executes exactly once. So at board density the demand is
      **not** terminally refused by its own closure profile; the board's stamp comes from a different attempted interval
      or from a **sticky** `pagedOutcome`/`retainedGatherRefusal`. **Next probe:** pair the demand stamp with the lane's own
      refusal name in one row (the `why` encoding has room) — no new firmware surface. Fixture value kept: the densest
      faithful control in the family (board density, wrapped two-bar take, qualified clock, full chain asserted) and the
      guard that will falsify any fix that weakens a gate to make the board pass. Full native **3,306 / 7 skipped /
      3,299 succeeded / 0 failed**; both builds unchanged.
      *DEMAND STAMP READ ON THE BOARD (2026-10-18) — and one value is terminal.* A `retire_hold` row's `why` field now
      carries the demand's own position stamp. Device run `i5-rolling-base-2bar-07` (HEX `71e9369b…`, same stall) returned
      three stamps: **active/`SourceNotCurrent`/positioned**, **active/`ClosuresExceedDeclaredCapacity`/positioned**, and
      **no active demand**. So the board's demand is *positioned* (the earlier `epoch=0 deadline=0` row belongs to a
      different arm), it is re-stamped `SourceNotCurrent` with its position intact, and in the same window it is once
      stamped `ClosuresExceedDeclaredCapacity` — which that reason's own contract calls **terminal for the lane's retry**
      ('a refusal with a name is an answer'). A demand there is never retried, which would fully explain a job that never
      retires while the accepted stop waits behind it. **Lead, not verdict** (four rows, this session's own encoding):
      confirm or reject by asserting the slot's demand reason across the stall with the fixture hook that settled the clock
      question. If confirmed, the fix belongs to the **closure capacity/derivation** for that content — a different owner
      from the successor identity and from the wait.
      *CORRECTION 2026-10-18 — that RED was a harness artifact; the state is GREEN once the clock is qualified.*
      With `committedPlaybackRetryCanProgress`/`rootReturnSuccessorIsOwedForSlot` exposed to the fixture, both answered false at
      `epoch=0` while `getCurrentTick()` read 133312: the native harness **stubs the deadline clock** (`track_harness.inc:127`
      never stamps it), so every epoch-gated window is structurally unreachable in this family — which is why two sessions of
      candidate patches measured inert. Qualifying it the harness's own P1 way (`nativePlaybackDeadlineClock = {tick, 3u}` per
      step) makes the same fixture **pass**: the production path builds, adopts and serves the root-backed successor. So the
      defect is **not** successor identity; it is that on the board the retained Committed demand is stamped `SourceNotCurrent`
      with no position (`epoch=0`, `deadline=0`, the 2026-10-16 row) while a real epoch exists. **Next probe: the
      `SourceNotCurrent` re-stamp site, with the live epoch recorded.** The case stays registered as a green control for the
      observed state. Full native **3,306 / 7 skipped / 3,299 succeeded / 0 failed**; both builds unchanged.
      *NATIVE RED BUILT FROM THE OBSERVED STATE (2026-10-18) — failing, deliberately registered.*
      `test_observed_hold_state_produces_a_root_backed_successor` feeds the board's state (wrapped 2-bar take, root current,
      frozen Capture successor complete in ready, audible transport) and asserts the identity every release edge needs — a
      **Committed** served window of the published root. It fails with `cur=1 ready=255 root=1 bound=1 pending=1 |
      pendKind=1 pendReason=7 pendEpoch=0 pendDeadline=0`: the retained demand is Committed but **re-stamped
      `SourceNotCurrent` and unpositioned**, so it can never progress and nothing builds the root-backed successor. **Two
      named next probes, one line each:** (1) `playbackWindowSourceIsCurrentForSlot` while the served window is the
      pre-publication frozen Capture view and the demand is for the current root; (2) `rootReturnSuccessorIsOwedForSlot`'s
      `qualifiedFrozenRecordCaptureSourceForSlot` term, which needs the raw source the adoption consumed — relaxing just
      that is **inert while (1) stands** (tried and reverted). Full native **3,307 / 1 failed (this RED) / 7 skipped /
      3,298 succeeded**; the wrapped-two-bar control stays green as the no-weakening guard; both builds unchanged.
      *HOLD OBSERVATION ADDED (2026-10-18) — and it names the fault.* The same receipt also fires from the retirement hold
      (`holder=retire_hold`, one-bit site flag; a string parameter cost 64 B of ITCM and did not fit). Run
      `i5-rolling-base-2bar-06` gives `retire_hold` **mask 239**: root current, served view the job's frozen window, ready role
      valid + **complete** + loop match + frame serving — **bit4 (root-backed) clear**. The ready successor exists and is
      complete but is **Capture-origin**, never a **Committed successor of the published root**, and every release edge
      requires that identity, so none adopts while the hold correctly keeps waiting. **The repair is successor identity, not
      the wait:** for a loop the lane cannot serve, the private root-return preparation (DEC-136) must target the **published
      root** once it is current. The native RED is now constructible from an observed state (complete Capture successor +
      root current + bound job + audible transport). Cadence: the two sites alternated on bit5 (79 rows for one stall); the
      signature now excludes it.
      *E2 INSTRUMENT ATTEMPTED 2026-10-18 — implemented, verified, and **blocked by RAM1**; reverted.* Built exactly as
      specified: one read-only row (`#CAP,<us>,DIAG,e2_ret,t,s,c,r,f,ph,rp,a,w`) carrying the eight adoption terms as a
      flag mask plus the adoption's own refusal codes (1 no runtime, 2 no view set, 3 current role unset, 4 ready role
      unset, 5 ready incomplete, 6 preparation handoff declined with every readable term holding), `SC_FMT`/`PSTR`
      formats on both delivery paths, `DMAMEM` cadence state (one scalar per track+slot, transition signature = flags |
      wrap marker | adopt answer), Tier-A registration in `DebugSessionCapture.cpp` and the exact boundary pinned in
      `test_capture_ring`, and a clean marker pre-flight (`check_marker_contract.py`: EMITTED, TIER-A,
      `'DIAG,e2_ret,'` 12/31 bytes). **It cannot land inside the approved line:** qualification `variables`
      96,192 → **100,288 (+4,096)** with `code` 392,280 → 392,552, i.e. the documented alignment snap — and it
      survived three targeted trims (cadence state moved to `DMAMEM`, file-scope format replaced by `SC_FMT` at both
      call sites, emitter stubbed to a no-op), so it is the small `.h`/TU additions crossing a `.rodata` edge, not one
      symbol. Padding fell 936 → **664**, under the 840 floor. Headroom is the real constraint: qualification **96 B**,
      default **240 B**. Reverted in full (no `e2_ret`/`rootReturnBoundary*` symbols remain) and the line is back to
      byte-exact baseline (qualification `96,192 / 392,280 + 936`; default `96,192 / 392,136 + 1,080`), both builds
      SUCCESS, full native **3,305 / 7 skipped / 3,298 succeeded / 0 failed**. **Owed, in this order (owner decision on
      the first two):** (a) carry the same information in an existing row so no new family is added — e.g. the E2 term
      mask as extra fields on `win_frozen_readiness` (its own source-qualified receipt, already Tier-A and already
      emitted at this edge); (b) or add a `.rodata.str*` carve-out to the linker script for the emitting TU (the
      traps' own remedy); (c) or re-base the budget deliberately (never by an agent). Then run 6.4's 2-bar profile once
      and read which term refuses.
      comment says so. Two intermediate shapes were also measured: the wrapped take **without** an overdub retires
      (first assertion set green), and the geometry is only reachable when accepted input keeps arriving into bars 2–3
      (a clock-advance-only take still stops as one bar — the `stopRecording` extent re-derivation the 2026-10-17
      session hit). **Consequence:** the mutation point this task names (positioned demand at construction) is still
      unproven on hardware-shaped state, so **no production line was changed** — patching here would repeat candidates
      1 and 3, both withdrawn. The missing state remains what the 2026-10-17 packet named: a **complete, unpublished
      root-backed successor in the ready role** (device `ready=2`) *or* a serving frozen Capture view at the release
      edge; this fixture reaches neither (`ready=255`, no frozen current view). **Next, in order:** (i) instrument the
      E2 root-return block to name the refusing term on the board (one bounded row per slot+term, the
      `LaunchGateLastState` cadence pattern, `SC_PLAYBACK_BUILD_ROW` vehicle — no new behaviour, FLASHMEM site), run
      6.4's 2-bar profile and read which of `fromThisFrozenJob(current)` / root currency / `ready` completeness /
      `startPhase` / `adoptPreparedPlaybackWindow` refuses; (ii) only then build the fixture that carries *that* state
      and take the RED. Full native suite with the new control: **3,305 cases / 7 skipped / 3,298 succeeded / 0 failed**.
      *BUILD-DROP LIFECYCLE INSTRUMENTED — THE PREMISE IS FALSIFIED: **NO RELEASE SITE FIRES** (2026-10-18).* Owner-approved
      route: instrument the job → `build+job` → `job` lifecycle, name the exact destroy/detach site and its reason,
      correlate by demand/root identity, and record whether the disappearing build published, transferred authority or made
      retryable progress. A `build_drop` row (same `win_frozen_readiness` vehicle, `#ifndef`-guarded) now probes the builder
      at all ten release sites (`applyPlaybackViewTransition`, `cancelPlaybackLaunchForSlot`, `cancelRollingPlaybackWindow`,
      the three `retireCoverageSource` edges, both source-superseded `discardCoverageBuild` edges, the late-completion
      discard, the DEC-136 root-return hold expiry) **plus** the two transitions no site owns: `completed_unpublished`
      (`RuntimePlaybackViewBuild::active()` is false for `Phase::Complete`, so a finished build leaves the owner set with no
      release running) and `released_without_site`. Two board runs (`captures/i5-builddrop-01`, `-02`, image `d849d755…`):
      **one** release-site row total, none in the stall; the alternating `build` bit is the **live-capture** builder resting
      in its designed `Phase::Complete` between `extendLiveCapture` calls; the stall's own rows read `completed_unpublished`
      with `windowServing` **and** `successorPrepared` set, `owners = 8` (**job only**), 48/48 qualified/ordered, and
      **`captureToken = 0`, `revision = 0`** — *no published Committed root at all*. **So no builder is dropped and no drop
      site is owed a fix.** The obligation instead never reaches an adoption boundary: the DEC-136 hold's *audible* branch
      (`rootReturnHoldGrains = 0; return Pending;`) leaves that wait unbounded, upstream of every drop site. A native RED
      for the drop sites was **deliberately not written** — it would encode a hypothesis the board contradicts. Next RED
      target, from the measurement: a slot whose coverage build completes and prepares a successor while the loop has **no
      published root** and the Record job is pending must not park that job on an unbounded audible-branch wait. Focused
      14/14; full native **3,313 / 7 skipped / 3,306 succeeded / 0 failed**; qualification `392,360 + 856`, default
      *THE RECORDED SPLIT DESIGN IS FALSIFIED BY THE RING'S OWN STRUCTURE (2026-10-18, measurement only — no code changed).*
      The next-shaped slice was the "completed-slice / deferred-presentation" split recorded in
      [`EVIDENCE.md`](evidence/2026-10-18-rolling-base-2bar-rerun/EVIDENCE.md) § "Slice design": mark a
      `RecordPublished` `CaptureStopJob` presentation-only **by phase** and let the interaction gates consult "a
      slice-incomplete job is pending" instead of ring occupancy. Read against the code, two independent structures
      refuse that path, so it cannot be implemented without an owner decision:
      1. **The ring is serviced at its head only.** `Track::processDeferredOverdubStopGrain` binds `captureStopJobHead()`
         once for the whole grain and never re-resolves ("no intermediate step may re-resolve the ring and pick up a
         different job"), and its Record arm dispatches by `stop.phase`. A successor admitted **behind** a
         presentation-only holder is therefore never serviced, so its slice can never close — "release the job's
         interaction role" frees the *gate*, not the *service*.
      2. **There is one finalizer role per slot, and the presentation obligation holds it.** `LoopPlaybackRuntime`
         carries a single `captureStopOwner`, and every serving/retirement/hold predicate keys on it through
         `hasBoundRecordReturnWindowForPendingJob()` (`captureStopOwner != nullptr && captureStopOwner->pending`), as
         does the E2 frozen-reader route (`captureStopJobHead()` **and** that predicate). A successor's admission is
         denied by the same pointer (`beginDeferredRecordStopForSlot` → `finalizer_owner_held`;
         `beginDeferredOverdubStopForSlot` → the same term beside `hasPendingDeferredOverdubStop()`). Giving the
         pointer to the successor would re-key the presentation and stop serving the frozen range; keeping it with the
         presentation holder leaves the successor unreachable. A second per-slot role is a **new `LoopPlaybackRuntime`
         field**, and that struct is size-pinned (7,880 B) with no budget re-base authorized.
      **Measured while testing the premise (probe removed, suite unchanged):** the native harness cannot currently hold
      the board's state. Two numbers decide it. (a) `loop.lastTickInLoop` is `UINT32_MAX` at the Record stop unless the
      slot is *played back across a wrap* first, so `stepDeferredRecordRetirement`'s S7.5b.5 block is skipped entirely
      and the job retires within 16 deferred turns: `[probe] at stop: pending=1 phase=16 queue=1 latch=1 bound=1
      lastTick=4294967295` → `pending=0 queue=0`. (b) Once the loop **has** wrapped (`lastTick=608`, `pending=1 phase=16
      bound=1 playing=1`) and a live overdub is started and served for two bars, the frozen window stops being the
      *published* served reader (`publishedPlaybackWindowForSlot` answers false) and the job again retires immediately:
      `[probe] overdub served: pending=0 served=0`. The board's held state (`retire_hold mask 239`: served = the job's
      frozen window, `recordReturnBound=1`, `pending=1,phase=16`) therefore needs a **serving** frozen reader that
      survives an overdub start — the same wall the 2026-10-17 session recorded, now with its two numbers.
      **Next step, and the decision it needs:** a presentation obligation that outlives the interaction requires an
      owner answer on the **finalizer role** — (A) a second per-slot presentation role (one pointer field in
      `LoopPlaybackRuntime`: EXTRAM, no RAM1 effect, but a measured size-pin re-base), or (B) a harness-level
      construction of a serving frozen reader that survives an overdub start, which is test-only work and is the
      prerequisite for *any* native RED of this slice.
      `392,136 + 1,080`.
      *CHECKED OFF 2026-10-19 — acceptance (1)–(5) met.* (1) Real admitted wrapped Record reaches publication with FIFO empty and the presentation extent retained; the accepted successor stop executes once — registered fixtures in `test_rolling_record_retirement` (12/12). (2) Admission + closure prepare/adopt/emit bar two before pass publication without early retirement. (3) The current-root reader takes over through ordinary deferred turns and the extent is released and re-armed (`test_dense_record_presentation_releases_every_chunk_before_readmission`, `test_published_record_presentation_prepares_frozen_return_before_root_is_current`). (4) Stopped unadopted successor retirement stays bounded (`test_unadopted_frozen_capture_successor_does_not_park_the_record_job`). (5) Full native **3,325 cases / 7 skipped / 3,318 succeeded / 0 failed**; two focused independent reviews in this session (DEC-143 overlay: two concrete defects fixed and re-verified; DEC-136 window progression: one bounded risk recorded — a pre-seal **live** copy carries the same capture token, so it can pass as the frozen reader after a *refused* bind; same take, nothing relabelled, a successful bind clears such views, settlement fixture owed); default build `392,088 + 1,128`, qualification `392,376 + 840` (floor 840 unchanged); strict validation valid; **registered qualification-image base 2-bar device recipe `PASS`** (`captures/i5-frozen-current-window-base-01`, `issues: []`, playback witness 8/8, stop 40.735 s → frozen serving 41.464 s → DEC-143 overlay 42.375 s → root 43.264 s, each sounding articulated once). Still with S4f.8 (waiting accepted-stop receipts) and the all-route cutover in 3.15f/6.4. *SETTLED 2026-10-19 — the owed settlement fixture landed (native GREEN, no firmware change): `refusedBindPreSealLiveCopyNamesTheSameTakeOnly` + discriminating control `refusedBindUnextendedLiveCopyKeepsTheOlderToken` (`test_midi_input_clock_integration`, rolling-record coverage). The over-inclusion is real and self-limiting — the frozen token is the take's content-generation identity, so an extended pre-seal live copy carries it and holds every accepted row (same take, still audible), while an unextended copy keeps the older token and a partial row set and the identity refuses it, with the sealed extent still serving the same take. Red-proved by removing the append-time `advanceCaptureContentGeneration()` in `LoopCapture.cpp` (both cases fail), restored byte-identically (`968a449e…`). No checkoff moves.* *DEVICE 2026-10-19 — the three-group campaign on the current qualification image (`0b4b6fe2…`, `captures/i5-3g-campaign-01`, aggregate `INCOMPLETE_REQUIRED_STOP_COVERAGE`) passes `growth-empty` (`corrected_frozen_handoff` ×2) and `held` (`corrected_frozen_root_return`) but the `matched` leg fails `stop_matching_reader_not_preserved`: a non-truncating Stop (`geometry.raw_length 560 → final_length 768`) moved the reader `(currentRole,currentStorage) (0,49)→(2,56)` and the frozen token `captureToken 32→34` (post-stop sample at tick 744, inside the same 768 cycle, so not the wrap-rotation artifact). The pre-stop served window was a DEC-136 frozen current-window at token 32 while live had reached 34; the stop re-sealed at 34 and installed a newer frozen reader. Oracle was not edited at that point (no-weakening rule). *DEC-144 2026-10-19 — settled natively and the oracle realigned:* the pre-stop window was empty (`rows 0`) while the take lived in the capture store, so preserving it would serve silence; the rotation is **semantically required**. The captured matched samples show the post-stop window is the sealed take (`captureToken 34`, `rows 2`, ticks 200/296). The terminal is now conditional — same reader where the pre-stop window was current, sealed generation plus the take's rows where it lagged — and it searches the observed sequence for the sealed take (the earlier images served it on the second post-Stop sample), refusing a sequence that never seals or seals without the rows. Native fixtures `matchedStopServesTheSameTakeAcrossTheGenerationHandoff`, `matchedStopHandoffNamesTheSealedTakeOnly`, `truncatingStopDoesNotTakeTheMatchedTerminal` (focused 66/66; full native **3,330 / 7 skipped / 3,323 succeeded / 0 failed**); offline re-verification of `captures/i5-3g-campaign-01/matched` returns `matching_physical_reader_preserved`, with `held`/`growth-empty` unchanged. 6.4/3.15f and S4f.8 stay open. *DEVICE PASS 2026-10-19 — the realigned terminal on the same qualification image:* `matched-fix-01` returns `matching_physical_reader_preserved` (`issues: []`, `SMOKE_ONLY: []`; pre-Stop token 37 / live 38 / rows 0, first post-Stop sample the sealed take, token 39, rows 2, ticks 200/296). The same-contract campaign `growth-empty-03` + `held-02` + `matched-fix-01` aggregates to **`PASS`** (`full_recipe_complete: true`, `issues: []`, firmware `0b4b6fe2…`, commit `689ec471…`, `captures/i5-3g-campaign-01/aggregate-fix-02`). The board is left on that image by owner instruction. This is the rolling Record route's device acceptance, not a 6.4/3.15f/S4f.8 checkoff.*

## 4. Checkpoint parity and content parity

- [ ] 4.1 Land **F-5** (checkpoint parity, undo) — verify an undo restores exactly what its entry named, including the lagging case, whose outcome must be named rather than reported as current.
  *Increment 4.1 landed 2026-10-02 — parity is named (`CheckpointParity` and `resolveCheckpointParity`; `Loop::checkpointParity()` reads the loop's own three figures without new state), and the lagging case is named through the production undo path with `publishedSnapshotIdentity()` null: selected, never presented as current. This establishes a named precondition for 3.14's refresh, but does not implement its requester. The task stays open on content-level comparison through 4.3's content-verification pipeline; save parity is 4.2. No production caller consumes the outcome yet. Detail: [`evidence/2026-10-02-s41-checkpoint-parity/EVIDENCE.md`](evidence/2026-10-02-s41-checkpoint-parity/EVIDENCE.md).*

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
  at the transition** — either window alone may omit the closure, so the consumer's 3.7a serving handoff must reconcile
  it with 4.5's ledger — and the **overlap rule** (the interval policy is centred, so arbitrary-phase consecutive
  windows overlap and the consumer must avoid double-serving). Loop-wrap content is deliberately not in this fixture.
  Verified: 66/66 (+1 fixture) + 52/52 + 190/190 (cited pin) + 38/38, full native 2698 / 2691 succeeded / 7 skipped /
  0 failed, true clean rebuild with RAM1 line unchanged; four mutations red-proved and restored byte-identically. Detail:
  [`evidence/2026-10-02-s43-window-boundary-parity/EVIDENCE.md`](evidence/2026-10-02-s43-window-boundary-parity/EVIDENCE.md).*
  *(This note's 2026-10-02 paragraph arrived with a duplicated tail — the device-leg clause restated mid-sentence and a
  stray "line byte-identical (48 B headroom)" fragment — repaired 2026-10-19; no claim changed.)*
  *State of the two remaining items after F-7 (device, 2026-10-19):* the **overlap rule**'s consumer obligation is
  **measured satisfied** on the demoted target — the 1 536-tick span steps 768 ticks (measured `windowStartTick` deltas
  of 768/1 536 across all three captures), so consecutive windows share a bar, and `content-review.json` reads `extra=0`
  on **every** cycle at **three** demoted lengths (24, 25 and 32 bars), i.e. the shared bar was served exactly once and
  no window was double-served. The **closure obligation at the
  transition** is observed **for the served stream** — per-cycle `inventory_equal` **and** `exact_order` at those three
  lengths mean the boundary note's ON and its closure appear once each, in `effective`'s order, so no note-on is left
  unclosed across the handoff — while its **ledger reconciliation** stays with 4.5's cross-generation closures, which is
  why 4.3 stays open.
  *Device leg crossed 2026-10-19 (the lane's boundary half only; **the F-7 parity verdict is still not
  claimed**):* the registered `windowed_content_parity` profile ran on the current qualification image
  (`git a9d41abf`, HEX `6cf5f874…`, track 8 / slot 4) and the lane observed **391** `build_complete`
  publications **under transport** (`loop_len 768`, revisions {0,1}) with **zero** named overruns — so the
  boundary crossings this task names are now produced on a device, not only in a fixture. The **parity
  verdict itself** (the windowed stream equals the whole-loop stream across the boundary, closures included)
  stays the content pipeline's (`hitl/content_deduction.py`, `scripts/hitl/mixed_content_report.py`) and was
  **not** taken — the same deferred term the lane records. 4.3 stays open. Detail:
  [`2026-10-19-s64-windowed-profile-device-leg/EXECUTION-PACKET.md`](evidence/2026-10-19-s64-windowed-profile-device-leg/EXECUTION-PACKET.md).*
  *Route identified 2026-10-19 (host reading, no device run):* the parity verdict's owner is the **existing**
  content-verification pipeline, and it already makes this comparison — `dump_and_capture.py` stops the transport,
  sends `!CAPTURE_CONTENT` (record / overdub / **`effective`** = `LoopPasses::materializeToEventVector`, the
  whole-loop merged view) and records several playback cycles, then `verify_playback_content.py` compares **each
  cycle's inventory against `effective`** (`inventory_equal`, per-bar counts). On a **demoted** target the recorded
  playback is the window lane's served stream, so "the windowed stream concatenates to the whole-loop stream across
  the boundary" is read as `content-review.json`'s per-cycle `inventory_equal` — no second oracle, no firmware line.
  **What the leg still needs:** the demoted target must be the **dump target** (the dump prints one loop, and opening
  the port resets the board), which is **already possible without a destructive gesture**: `!CAPTURE_CONTENT_SLOT <n>`
  names the slot (0-based) on the **selected** track, so only the track must be selected — and
  `scripts/hitl/actions/slots.py::press_track` (short, `TRACK_SELECT_NOTE_BASE + n - 1`) is a non-destructive
  `SELECT_TRACK`, unlike the loop button, whose **short** press is `TOGGLE_RECORD_FOR_SLOT` and would record over the
  take being dumped. So the recipe reuses `scripts/hitl/actions/`, needs no firmware line and no new oracle; what it
  owes is the host wrapper plus the `--capture-seconds` that a 24-bar loop (48 s at 120 BPM) needs for ≥2 cycles, and
  whether the demoted take is still resident (reuse) or must be re-recorded.
  *F-7 landed 2026-10-19 (device, no firmware change) — **PASS** on the demoted target:* `content-review.json` reports
  3 cycles of 1 152 events each with `inventory_equal` **and** `exact_order` true on all three (`cycle_cut:
  event_count`, `storage_matches_effective: true`). The boundaries were **crossed, not avoided**: 80 `playback_build`
  rows for the target with a 1 536-tick (2-bar) window **stepping 768 ticks (1 bar)** over **24 distinct window starts**
  (`0…17 664`) and **79 advances** — 25 window positions per 25-bar cycle, so ≈25 boundaries per cycle (the span
  overlaps its neighbour by one bar, and `extra=0` on every cycle is direct evidence the consumer served the shared bar
  once). The premises come from the device's own gate flags
  (`win_gate,t=7,s=3,len=19200,av=1,al=0`): **demoted** (`av` = above the 16-bar bound, 12 288 ticks) and **not
  span-aligned** (`19 200 % 1 536 = 768`), so the *demand* lane excludes the slot while the serving **gather** builds
  the windows. The target is **resident after a cold boot** (`record` = `effective` = 1 152 rows). Two host-tooling gaps closed first: the pipeline can now reach the target **without a
  record-toggle gesture** (`--select-track`, `--dump-slot`, `--dump-only`; the loop button's short press is
  `TOGGLE_RECORD_FOR_SLOT` and would record over the take being dumped), and the verifier can now cut cycles on a
  **fully periodic** loop (event-count cut, starting at the loop's own first event and then strict — a phase search
  would re-align a dropped event away instead of reporting it; this take has 160 signatures and **none** unique, so the
  anchor rule could never fire). New suites 4 OK + 4 OK; defaults unchanged. **Still open in 4.3:** the closure
  obligation at the transition and the overlap rule. **F-8's length sweep is untouched and 6.4 stays open.** Detail:
  [`2026-10-19-s64-f7-demoted-content-parity/EXECUTION.md`](evidence/2026-10-19-s64-f7-demoted-content-parity/EXECUTION.md).
- [ ] 4.4 Land **F-8** (length-independent root residency) — verify a residency statement per slot (windows resident, root demoted) plus content parity for the demoted state, **swept** over loop lengths.
  *Increment 4.4 landed 2026-10-02 — **test-only**: the residency statement per slot, swept over 16/17/24/30/32/64 bars through the production policy and admission (`fullTakePlaybackViewAdmissible` + `prewarmPlaybackForSlot`, which decides the representation itself), and read from the slot's own accounting. Verified: the policy boundary is the declared bar cap; the whole-take representation is reserved exactly while admissible and absent at every demoted length; every demoted length reports the same windowed reservation, with each view at declared capacity and stagings resident; at/above its floor the slot is served from a window while its root is demoted, and below it refusal is named.
  **The floor as a number:** the lead implied by the declaration's arithmetic is 22,272 ticks ≈29 bars, so the floor is exactly 30 bars; loops between the bar cap and that floor have neither representation admissible. The number is pinned from 6.3/3.10 contract fields, not hardcoded.
  **The task stays open** on parity through the content-verification pipeline (6.4/6.5), the retained-source figure (3.6a), and 6.3's wider sweeps. Verified: 67/67 (+1 fixture) + 52/52 + 190/190 (cited pin) + 38/38, full native 2699 / 2692 succeeded / 7 skipped / 0 failed, build SUCCESS and a no-op with the RAM1 line unchanged; three mutations red-proved with byte-identical restores. The oracle is the policy function itself, so its declared figures remain 3.10/6.3's to own; the absolute floor is cited from `test_playback_prewarm`.
  Detail: [`evidence/2026-10-02-s44-length-independent-residency/EVIDENCE.md`](evidence/2026-10-02-s44-length-independent-residency/EVIDENCE.md).
  *Device leg 2026-10-19 (the residency row set is now **identified**):* the registered profile ran on the current
  qualification image, and the per-slot residency statement the lane records as `judged: nothing` is present in
  **two already-emitted rows**, on two axes. **Playback representation** —
  `#CAP,<us>,DIAG,win_gate,t=,s=,x=,len=,ep=,av=,al=,pl=,ed=,od=,pv=,rw=,sv=,rt=,rl=,ow=,dm=,bld=,gr=,rp=,rq=`
  states, **per slot**, the loop's `len` and its demotion/servability flags: `av=1` is the emitter's own "the loop
  is demoted", `pl` playing, `sv` a complete published current window, `rt` the published root current, `rl` the
  coverage role available, `ow` the slot owns a coverage source — so "windows resident, root demoted" reads off one
  row at one length, and `len=` is the axis a sweep varies. **It is already readable:** the shipped
  `scripts/hitl/verify/windowed_lane.py::gate_rows` returns each row's `track`, `slot`, `loop_len` and the whole
  `flags` dict, so no new parser is owed. **Content representation** — `LIBOBS` per-slot `S`
  records (`scripts/hitl/library_observation.py`) give `k` committed, `kp` pass-backed, `kr` root-backed, `kc` root
  current: the demoted state's content, which is what F-8's parity half reads. **Measured in the recorded capture:**
  `t=7,s=3,len=19200,av=1,al=0` (track 8 / slot 4, 0-based — the 25-bar target, demoted) and
  `t=1,s=3,len=49152,av=1,al=1,pl=1,sv=0,rt=0` (a 64-bar slot refused `win_not_servable`).
  *Row-set census 2026-10-19 (read-only over `captures/*/*.log`; no device run):* **the axis is populated, and an
  earlier reading of it in this session was wrong.** A first pass globbed only `captures/*/serial.log`, found 71 rows
  and concluded that "no row anywhere reads `sv=1`" — but the pipeline's captures are mostly `session_*.log` and
  `host_midi_hitl_*` logs. Over `captures/*/*.log` there are **19 215** `win_gate` rows, and `av=1` **together with**
  `sv=1` (demoted **and** a complete published current window — F-8's statement) occurs at **five** demoted lengths:
  **23 bars** (17 664; 1 265 rows, 1 261 `pl=1`, from 2026-10-05), **24 bars** (18 432; 1 447 / 1 439, 2026-10-07),
  **25 bars** (19 200; 7 756 / 7 728, 2026-10-07), **26 bars** (19 968; 2 / 2, 2026-10-06) and **32 bars** (24 576;
  11 / 8, 2026-10-19). The demotion bound is `16 × TICKS_PER_BAR` = **12 288 ticks (16 bars at 768 ticks/bar)**, so
  every one of those lengths is above it. **The residency half therefore needs no device work to be stated** — it has
  been on disk since 2026-10-05. *What the census does still establish:* **`win_serving` and `win_ready` are absent
  globally — 0 of 19 215.** A *resident* published window is common; the demand lane's own **serving** exit has never
  been recorded in this project, which is a real observation about the lane (6.4's receipts territory), not a gap in
  F-8's row set. `win_busy` occurs **twice, both in today's 32-bar run**; `win_blocked` is **not** new (from
  `captures/s64-37c-01/`, 2026-10-06). The 25-bar target's alignment refusal (`al=0`, `19 200 % 1 536 = 768`) is real
  but gates only the *demand* entry: 7 756 rows show `sv=1` at that same length.
  *Qualifiers withdrawn 2026-10-19:* an earlier note here required a demoted **+ span-aligned + root-current +
  role-free** slot. The rows refuse all three extra terms: `sv=1` appears at `al=0` (23 and 25 bars) and `al=1`
  (24 and 32 bars), with `rl=0` (today's runs) and `rl=1` (the 25-bar rows), and with `rt=0` as well as `rt=1`. `al`
  gates the demand entry only; neither a free coverage role nor a current root is a precondition of a resident window.
  *Parity half, three demoted lengths (device, owner-authorized; the registered target re-recorded three times; no
  firmware line, no upload):* `--record-bars 23` → `len=17664`, `--record-bars 24` → `len=18432` and `--record-bars 32`
  → `len=24576` (the axis is exactly the requested bar count, 768 ticks/bar), with **758**, **344** and **1 028**
  `build_complete` rows naming the target at a 1 536-tick span; the content pipeline gives `content-review.json`
  **`PASS`** at **24, 25** (F-7) **and 32 bars**. **The 24-bar take first read `FAIL` — and the defect was the
  oracle's.** `effective` there ends with `(128,8,49,0) tick=18431 NoteId=0`, the **loop-boundary terminal Off** the
  guide already documents as an expected residual; because playback does not serve it, the served loop is **1 147**
  events against `effective`'s **1 148**, so the event-count cut's fixed stride drifted one event per cycle — one
  missing **and** one extra row in *every* cycle (`missing=[[128,8,49,0,1]]`, `extra=[[144,8,50,98,1]]`, note 51 in
  cycle 2 as the drift stepped), which reads as a content failure that is not one. The verifier now drops only that
  documented residual from the reference and cuts at the served period (`cycle_period_events`, `cycle_trimmed` in
  `content-review.json`; guide updated), **only when the strict cut does not already fit**, so a genuinely dropped
  event still fails — `scripts.test_verify_playback_content_cycle_cut` **7 OK**, with a trim-must-not-rescue-a-drop
  guard and a served-Off case that keeps the strict cut. Re-running the 32-bar and F-7 captures reproduces `PASS` with
  `cycle_trimmed: null`. **Remaining in 4.4:** parity stands at **three** demoted lengths (24, 25, 32 bars) against a
  residency half of five; the axis is `record-bars`, so further lengths are a recipe, not a search. The retained-source figure
  (3.6a) and 6.3's wider sweeps stay with their owners; the gate row fires only at gate **edges** and `LIBOBS` states
  content, not window residency. 4.4 stays open. No firmware line changed. Detail:
  [`2026-10-19-s64-f8-residency-row-census/EXECUTION.md`](evidence/2026-10-19-s64-f8-residency-row-census/EXECUTION.md)
  and [`2026-10-19-s64-f8-residency-and-parity-sweep/EXECUTION.md`](evidence/2026-10-19-s64-f8-residency-and-parity-sweep/EXECUTION.md).
  Detail:
  [`2026-10-19-s64-windowed-profile-device-leg/EXECUTION-PACKET.md`](evidence/2026-10-19-s64-windowed-profile-device-leg/EXECUTION-PACKET.md).*

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
  the previous slice. Six mutations red-proved with byte-identical restores. The first track-wide-silence mutation did not discriminate because the fixture observed only the ledger; it was corrected to assert real MIDI output, and a global mutation now fails as intended.
  *Increment 4.5b landed 2026-10-02 — cross-generation closure planning implemented; task remains open for real serving-path integration.* `ClosureSettlement` is a bounded, allocation-free handoff plan. `Track::planClosureSettlementForSlot` computes it off-clock; `Track::applyClosureSettlementForSlot` emits closures before replacement note-ons. Unchanged notes are preserved, re-issued notes are suppressed, and absence means removal only when the input is authoritative for the whole loop (never for an empty window). Verification: focused 63/63, full native 2694 / 2687 succeeded / 7 skipped / 0 failed, capture build SUCCESS with RAM1 unchanged; four mutations red-proved and restored byte-identically. Evidence: [`evidence/2026-10-02-s45-closure-reconciliation/EVIDENCE.md`](evidence/2026-10-02-s45-closure-reconciliation/EVIDENCE.md).
  **OWED:** production containment/recovery, actual emitted ordering, a moving closure at its correct tick, sustained-note behavior through an empty window, displayed provenance and eventual recovery through the real consumer. 3.7a connects that consumer while preserving fallback; complete 3.14 and 4.5 against the real handoff before 3.7b removes fallback.
  *Increment I6 landed 2026-10-06 (3.7a's handoff closures; the task stays open):* the **emitted order at the real handoff** is no longer owed -- the handoff reconcile now closes each invalidated obligation on the MIDI wire, at the handoff's own tick and ahead of the replacement's note-ons, discharging it by identity and suppressing an obsolete closure; the **empty-window** clause is measured there too (an event-empty whole-loop replacement closes nothing and drops nothing). Detail: [`evidence/2026-10-06-s37-handoff-closures/EVIDENCE.md`](evidence/2026-10-06-s37-handoff-closures/EVIDENCE.md).
  *Increment I9 landed 2026-10-06 (3.7a's serving handoff; the task stays open):* the **moving closure at its correct tick** is no longer owed -- the plan is one predicate and now also plans a tagged open Entry whose committed NoteOn the replacement **kept** while its own NoteOff moved **behind** the phase the walk resumes from (`planHandoffClosures` + `playbackClosureServeFloor`), so it is closed at the handoff's own tick instead of outliving the revision that ended it and being re-articulated at the wrap; the never-ending-sounding and restarting-pass guards are measured with it. **Still owed:** the **moving closure inside a long/demoted slot's serving handoff** (the merged store is a two-bar slice there, so the windowed lane owns it -- 3.7a/3.14), displayed provenance, eventual recovery through the real consumer, and the device leg. Detail: [`evidence/2026-10-06-s45c-moving-closure/EVIDENCE.md`](evidence/2026-10-06-s45c-moving-closure/EVIDENCE.md).
  *Increment I10 landed 2026-10-06 (3.7a's serving boundary; the task stays open):* **"eventual recovery through the real
consumer" is no longer owed.** `Track::advancePlaybackLagTraversal` had **no production caller at all**, so a latched fault
never cleared; the lane's serving resolver now calls it at the one event that can end the episode -- the **adoption
boundary**, where the prepared window was just adopted, so the boundary may serve the current revision and the fault clears
by recovery rather than by a reset. **`containmentForSlot` / `applyLastResortForSlot` still have no production caller**, so
the approved last resort remains unreachable and is owed. Judging the **expiry** and applying the last resort at the
resolver's three `Serving`-phase rejections were landed first (commit `eefd90a5`) and then **withdrawn on independent
review**: those rejections are exactly the turns the retained merged fallback serves the slot (so the last resort would
silence a sounding another serving route is about to play), the expiry named the interval the frame had already served
instead of the one going unserved, and the containment sites' reachability is not established -- the finding list is in the
evidence. **Still owed:** **production containment / the approved last resort** (an owner-level "no serving route at all"
predicate, or 3.7b's fallback removal, is required first), **displayed provenance** (the display's, RWO S4e), the device leg
(the lane-enabled image is over its own RAM1 budget, so it cannot be flashed), the moving closure inside a long/demoted
slot's serving handoff (3.7a/3.14), and the `serviceAdmittedDeadlineMisses` route's own boundary. Detail:
[`evidence/2026-10-06-s45e-lane-containment-recovery/EVIDENCE.md`](evidence/2026-10-06-s45e-lane-containment-recovery/EVIDENCE.md).

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
- [x] 5.4 State the envelope honestly — today's **eight** sounding roles, **16** as the next target,
  and 32/48/64 as measured-without-promise, each supported claim gated on **both** memory and deadlines.
  **Verified 2026-10-11 (documentation only):** `proposal.md` § Design commitments states all clauses;
  `design.md` retains provisional/unproven capacity and deadline status. This checkoff records honest
  declarations, not qualification of the rolling implementation; tasks5.1–5.3/5.5 and hardware gates stay open.

- [ ] 5.5 Prove the **legacy paged compatibility path** within its declared envelope (*DEC-108 §14*) — verify page
  preparation, page handoff and MIDI emission each meet their deadlines for a restored loop whose requested interval
  exceeds `L`, including **over-capacity same-tick events**, **loop wrap** and **cross-page note closures**; assert
  complete, ordered, exactly-once output with no tick delay, no inline rebuild and no truncation; record that
  segmentation proves a **storage bound, not throughput**
  *Increment 2026-10-06 (test-only) — the envelope is **measured**, and this task's own assertion is **falsified** for
  the over-`L` shape.* On a restored-library (retained-source) slot with **2,100 events of note pairs** inside the
  two-bar span, driven on the production clock entry over two whole passes: the storage-bound half **holds** (the
  ring's published-page high-water ≤ 3 against 5.5× its capacity, no inline rebuild, every wrap straddled, no row
  emitted outside its due window, the emitted stream an order-preserving subsequence of the interval), while the
  emission half does **not** — **370 of 4,200 events (8.8 %)** were consumed and then skipped as behind their frame
  (20 short frames), so output is neither complete nor exactly-once at this density, and the events are **dropped**
  rather than served late or refused by name. No duration is claimed (S7.5a owns the end-to-end thresholds).
  **Repaired 2026-10-06 (firmware + tests) — the task's clause now holds *inside* the declared closure capacity, and
  what exceeds it is refused by name.** The mechanism the 3.12e measurement deliberately left unattributed is now
  measured (the walk consumed its due set every frame; the active-note ledger sat pinned at its declared **128** and the
  missing rows were all `NoteOff`s whose `NoteOn` had already been sent — a note-on nothing could close), so the first
  candidate repair is **falsified**: no producer turn can move a per-frame ledger occupancy the content's own musical
  concurrency fixes. The second is implemented: `Track::beginPagedPlaybackForSlot` reads the interval's own closure
  profile over the content the retained gather already traversed and **refuses the switch by name** beyond the declared
  capacity (a new `ClosuresExceedDeclaredCapacity` name on the lane's outcome, the demand's reason and the selection
  gate), never establishing the ring. Two fixtures now split the 3.12e drive: the **reproducer** (unchanged content)
  asserts the refusal with zero rows, zero pages and `ledger.overflowed() == false`; the **acceptance** half
  (2,120 events > `L` at a 132-tick note span, high-water 109 ≤ 128) asserts *complete, ordered, exactly-once output with
  no tick delay* per frame (`skipped == 0`, `shortFrames == 0`), with no inline rebuild and no truncation, and the
  storage-bound half unchanged (page high-water ≤ 3 against 5.5× the ring's capacity). No duration is claimed (**S7.5a**
  owns the end-to-end thresholds). Detail:
  [`evidence/2026-10-06-s312f-closure-capacity-refusal/EVIDENCE.md`](evidence/2026-10-06-s312f-closure-capacity-refusal/EVIDENCE.md).
  **5.5 stays open** on: the **6.3** sweeps that may move the declared capacity itself, a device leg, and the named
  residuals of the repair's criterion (a mis-paired orphan-off content inside the capacity is not detected; unpaired-excess
  wrap-crossing content is over-refused deliberately). Earlier record: [`evidence/2026-10-06-s312e-paged-serving-envelope/EVIDENCE.md`](evidence/2026-10-06-s312e-paged-serving-envelope/EVIDENCE.md).

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


- [x] 6.4a Stage the I5 rolling-Record HITL into short, independently drivable managed passes without weakening 6.4 or 3.15f.
  `growth` then `empty` SHALL remain one `growth-empty` report/session/boot to preserve the cross-take transition;
  `held` and `matched` may be separate passes. Every partial report SHALL be `SMOKE_ONLY`, never recipe `PASS`.
  The offline aggregate SHALL accept either one complete `all` report or a `growth-empty` + `held` + `matched`
  bundle only when every report has the same qualification HEX hash, stimulus/authorization and host-verifier
  fingerprint, and successful canonical shutdown/MIDI cleanup; it SHALL require leg-attributed terminals and reuse
  `full_recipe_complete`. Standalone `growth`/`empty` diagnostics are not aggregate inputs. The `rolling_record` preset
  SHALL default to and refuse a non-qualification build environment. No firmware or playback-policy change; this
  tooling task does not qualify hardware or check off 6.4/3.15f.
  *Implemented 2026-10-17 (host tooling; 6.4/3.15f remain open):* the scenario exposes `all`, `growth-empty`, `held`, and `matched` passes, with optional standalone `growth`/`empty` diagnostic runs; the offline aggregator enforces report identity, functional evidence, leg-specific terminals, two-drain shutdown and MIDI cleanup before reusing `full_recipe_complete`. Rolling runs pin the qualification build environment. Host tests: aggregation **14/14**, rolling-record **20/20**, import gate **5/5**, stimulus contract **33/33**; synthetic CLI complete bundle PASS and missing-group refusal. A separately authorized three-group device campaign passed `growth-empty` and `held` as `SMOKE_ONLY`, but `matched` failed `record_evidence_drain_incomplete` with `STOP_STATUS pending=1`; aggregate refused recipe PASS. The pending stop was then retired in place by the authorized track-8/slot-4 clear; `pending=0`, queue `0/4`, and empty content were verified. The last-recorded default HEX was restored no-build and no-content postflight passed. The matched leg's missing drain evidence remains a failure; no 6.4/3.15f checkoff or firmware source change.
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

  *And the **one controlled switch episode** the owner asked for — into an inactive, committed, even-length, demoted
  target, traced across the three families together (`captures/s64-trace-01`; the registered recipe, **no** new
  diagnostics; firmware byte-identical to run 05, so the target's length is the only variable).* The device names the
  target itself: `launch_gate,t=7,s=5,len=18432,cmt=1,avoid=1,root=0,pend=1,req=1` with `snap_gate … s=5,a=4,…,elig=1` —
  **18432 = 24 bars (span-aligned), committed, demoted, inactive, rootless**, arranged by the registered recipe after the
  preparation run (`s64-etarget-02`: `length=18432`) and a re-flash. The trace: **(1) switch pending** —
  `Loop 6: Queued playback switch`, never a record start; **(2) launch preparation** — `x=launch_requested`, `req=1` (the
  republish was **accepted**), then `x=launch_requested_match` (`prep=1`, `root=0`) which is the **last** launch exit in
  the whole capture; **(3) source publication — the FIRST FAILED TRANSITION**: the `Snapshot` owner selects the target in
  135 of 136 rows with `p=1,sep=1,elig=1,idle=1,step=1` (admitted, not gate-refused) while **zero `snapshot_unit` rows
  are serviced anywhere in the 22 s the switch is pending** (they begin only after the transport stops, 66–80 per 2 s);
  the switch then reports its bound and stays pending (`slot_switch_hold_exceeded track=7 target=5 age=3073`,
  `a=4` = the active slot never moved), so **(4–6) initial view ready/bound, rolling request, preparation and adoption
  are never reached** (`launch_ready`/`ready_owns_source` appear nowhere, no `root=1` on any launch row, no rolling
  `win_requested` for the target). **The gate state to report is therefore: not a refusal — an admitted, idle
  producer.** 6.4 stays open, F-7/F-8/AC-8 and 6.5's four hardware claims stay NOT RUN, and the episode stops here per
  instruction (the restore-versus-rootless question is **not** reopened: the producer was reached and admitted). Also
  fixed as the bounded prerequisite this needed, with its host regression: **F-17** — the reused leg now *accepts* the
  runner's managed-capture handshake (`--follow-serial-log`, `--follow-current-session`, `--out-dir`), reading the
  capture's log instead of opening the port (nothing stripped), pinned by
  `test_windowed_content_parity_verify::ManagedRunnerHandshakeTests` (**27/27**). See
  [`switch-trace`](evidence/2026-10-05-switch-trace/TRACE.md).

  *Increment landed 2026-10-07 (host tooling + host tests; **no firmware line changed**, no device run) — the demand's
  own reading and the time it needs.* The gap F-16 recorded is closed **on the host side**: the leg's verifier now reads
  the lane's own `DIAG,win_gate` rows by `DebugSessionCapture::winGateRow`'s field order, so a run with no demand **names
  the term that refused** (the entry trio `ep`/`av`/`al` with the failing bit spelled out, `win_not_servable`'s missing
  published current window, `win_busy`'s holder, `win_requested`'s own `rq`) instead of leaving a reader to infer it —
  and a gate exit that can only exist with a frame or a request (`win_requested` … `win_frame_invalidated`) now counts
  as a demand in its own right, so a request that has not yet produced a receipt is not read as "the lane was not
  asked". The reading travels in the result (`demand`), and a capture carrying no gate row says so explicitly (the
  family is qualification-only). **The demand is also given the time the measurement says it needs:** the switch leg's
  wait was a fixed 20 s while a rootless demoted target's publication needed *well over 30 s* of service at the measured
  ~105 eligible turns/s x 2 units (`captures/s64-progress-01`), so `SWITCH_SETTLE_S` is now **45 s** — derived from that
  measurement, documented against the ~8 s switch hold bound and the 360 s managed-run bound, and overridable per run
  with `--switch-settle-s` (a float with its own splitter, refused **before any device contact** when non-positive,
  unparsable with the token named, or given without `--switch-target-slot`). Verified: the leg's host suite **39/39**
  (was 28), and the full host discover (1,254 tests) is **A/B identical to HEAD** — its 6 failures and 2 errors
  (`test_compatibility_consumer_inventory`, `test_foundation_runner`, `test_layered_verifiers`,
  `test_occupy_paint_serial_verify`, `test_6_3_machinery_closeout_gate`, `test_hitl_serial_timing`,
  `test_edit_session_state_serial_verify`) are pre-existing in this environment and unchanged by the slice; no
  firmware or C++ test file changed, so the native suite (**2,855 / 7 skipped / 2,848 succeeded / 0 failed**) stands.
  **The task stays open:** the leg has still never run with a demand — the next step is an authorized upload of
  `teensy41-capture-serial-window-qualification` (back inside its RAM1 guard since the same day's repair: `code 392,312
  + padding 904`) driven on a **demoted, span-aligned** target through the switch leg, and the first run may correct
  this oracle rather than the firmware. Not claimed: no device result, no timing/envelope claim, no F-7/F-8/AC-8
  qualification, no change to the launch that owns the demand's `sv` term. Detail:
  [`2026-10-07-s64-demand-leg`](evidence/2026-10-07-s64-demand-leg/EXECUTION.md).

  ***Authorized device run 2026-10-07* (`captures/s64-demand-01`; owner: *"Yes — flash and run the leg now"*, target
  *"Track 8 slot 4"*; image `teensy41-capture-serial-window-qualification`, `firmware.hex` sha256 `8e558cd7ff846d68…`,
  git `71bcfe2c`) — the demand now exists on hardware, and its refusal is one named term.** The lane was given work for
  the **first time** in this change's device history: `win_gap` blocks with `elig=10/prog=10` and `elig=1/prog=1` and
  `denied=0`, a `build_complete` publication (`t=7 s=4 len=19200`), and a readiness receipt **under transport**
  (`win_readiness,7,4,owes_build,window_build_owner,1,0`) — with `window_demand_observed` **true**, receipts,
  coverage and the no-overrun checks **passing**, and **zero** exceedances of the declared `permitted=64`. The
  **entry trio that five earlier runs could not satisfy now holds** (`ep=1 av=1 al=1`): the leg's record committed
  **19968 ticks = 26 bars**, and `19968 % 1536 == 0`, so the recorded loop is demoted *and* span-aligned. Every refusal
  on a demoted, aligned, playing slot is then the **servability** term alone — `win_not_servable` with `sv=0` (no
  complete published current window), including where the root is current (`rt=1`) and the coverage role is available
  (`rl=1`) — and the one window that *was* published belongs to the **switch target** (`t=7 s=4`), whose switch then
  **held at its bound** (`Loop 5: Queued playback switch` → `slot_switch_hold_exceeded track=7 target=4 age=3072`) beside
  the receipt naming the holder that still owes it. The lane's only verifier failure is
  `window_boundaries_crossed_under_transport` (`build_complete_under_transport=1`, needs ≥2). The reused leg's own oracle
  failed on its **length expectation** (`unexpected_loop_len expected≈17664 observed=[19968]`) — a **host** expectation,
  not a firmware defect, since the record-stop quantisation is what made the loop span-aligned; reconciling the leg's
  `--record-bars` → committed-length relationship belongs with the leg. **The next step is therefore no longer "get a
  demand": it is the active slot's first window** (the launch's source publication for a demoted target — the admitted
  idle producer of `s64-trace-01` plus this run's hold and `owes_build` receipt). 6.4/6.5 stay open, F-7/F-8/AC-8 and
  6.5's hardware claims stay **NOT RUN**, and no timing/envelope claim is made. Detail:
  [`2026-10-07-s64-demand-device-run/DEVICE-RUN.md`](evidence/2026-10-07-s64-demand-device-run/DEVICE-RUN.md).

  ***Authorized device leg on the current tree 2026-10-19* (dedicated HITL preset — upload without a
  prompt; `captures/i5-s64-windowed-parity-01`/`-02`):** the registered `windowed_content_parity` profile was
  run on the current qualification image (`git a9d41abf`, HEX `6cf5f874…`, RAM1 `96,192 / code 392,376 +
  padding 840`, track 8 / slot 4 / channel 8, `--record-bars 24`, `--no-boot-reset`). **Run 2 is the clean
  combined verdict** — `ok: True`, `issues: []`, `leg_exit_code: 0` — with the lane passing all six checks
  (ring intact; demand 399 blocks / 24 receipts; `readiness_receipts_under_transport` `source_qualified=27 of
  218` under transport; coverage 425/505 at `permitted=64`; `overrun_rows=0`; `build_complete_under_transport`
  **391**, `loop_len 768`, revisions {0,1}) and the snapshot passing all three. **Run 1 is kept as the
  case-1 artifact and diagnosed, not smoothed:** the combined verifier failed on the reused leg's display
  oracle (`leg_exit_code 2`) while the lane passed; the cause is an **evidence-pairing defect** — the leg
  verifies inline (pre-drain) and pairs rows by **stream position**, while the retained ring delivers
  transport-produced rows late, so the complete capture (20 505 lines, 859 DISP rows, 292 bounded) holds rows
  the inline snapshot (5 053 lines, 0 bounded) does not. The lane oracle already reads device time
  (`windowed_lane.row_us`); the leg's `display_window.py` is still position-based, and run 2's leg pass rides
  the `marker_fallback` on the pre-clear take as s39's did. **Acceptance met at the lane:** demand observed,
  source-qualified frozen readiness under a running transport, ≥2 boundary `build_complete` publications, no
  named overrun, ring intact. **Not claimed:** the F-7 **parity verdict** (4.3) and the F-8 **residency row
  set** (4.4) stay deferred; `oracle_complete` stays `False`; no firmware line changed. **6.4 stays open** on
  those two. Detail:
  [`2026-10-19-s64-windowed-profile-device-leg/EXECUTION-PACKET.md`](evidence/2026-10-19-s64-windowed-profile-device-leg/EXECUTION-PACKET.md).

  ***Pairing fix landed 2026-10-19* (host tooling, no firmware line, no device run — the two recorded captures are
  re-derived):** the named candidate slice above is done. `display_window.py` now brackets a phase's rows by
  **device time** (`_phase_rows`, reusing `windowed_lane.row_us`) with position kept only for markers that carry no
  stamp, and `verify_windowed_content_parity` re-derives `leg` from the **drained** capture it is handed, carrying
  the runner's pre-drain report as `leg_run_report` provenance. Measured: run 1's session log went from
  `insufficient_disp_during_note_edit_freeze count=0` to `ok=True, issues=[], warnings=[]`, and **run 1's combined
  verdict is now `ok=True`, `issues=[]`** (lane and snapshot already green) — the case-1 artifact failed on the
  reading, not on the device. Run 2 keeps `ok=True` and no longer rides `freeze_disp_sparse:marker_fallback`. Host
  suites 60 OK / 93 OK. **F-8 stays deferred, `oracle_complete` stays `False`, and 6.4 stays open** on it — **F-7 has
  since landed** (4.3, `PASS` on the demoted target; see the 4.3 note). Detail:
  [`2026-10-19-s64-windowed-leg-pairing-fix/EXECUTION.md`](evidence/2026-10-19-s64-windowed-leg-pairing-fix/EXECUTION.md).
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

  **Hardware claims read off the DEC-140/141 campaign 2026-10-17 — two measured, one recorded, one still NOT RUN; 6.5
  stays open.** The recipe PASS (`32f6501d…`, qualification env, `rolling_record`, track 8 / slot 4, host clock 120 BPM,
  `captures/i5-slot45-diagnosis-01/campaign-01/`) is the first device campaign in which the lane had a demand under a
  running transport in every leg, so the rows this task names were read rather than inferred, from the shipped emitter
  fields and the shipped parser (`scripts/hitl/verify/windowed_lane.py::gap_rows`, whose 6.5 fields are pinned by a new
  host test). **Grain duration** — worst lane worker span **595 µs** (`growth-empty`; `held` 448 µs, `matched` 467 µs),
  a maximum observed, not a WCET, and **not** attributed to persistence (the worst block is 2.49 s from the nearest save).
  **Maximum service gaps** — worst eligible-predecessor gap **40 ticks** against the row's own `permitted` **64 ticks**
  (104.2 of 166.7 ms at 120 BPM), with `overruns=0`, **zero** `win_gap_overrun` rows and `saturatedGaps=0` across
  79 running blocks in 74.7 s of transport. **Memory high-water** — **4,097 KB**
  (4,195,328 B) external pool high-water with the internal heap never below **100 KB free**; named limits are that `high`
  is boot-lifetime and therefore cumulative per leg, the row cadence is 1 s, and the **boundary content parity** half of
  this claim belongs to F-7 (task 4.3), which is not landed. **Still NOT RUN:** sustained **multi-track** playback
  contending for the same lane owner (this recipe runs one track, though capture and persistence contention are present
  — every save of every leg completes while the transport runs), and **external/DIN clock** behaviour. The task's
  **native** remainder is unchanged too: the **interval A/B revision trace** is still partial (shared-view fixtures, no
  rolling-chain trace). No firmware line, no instrumentation, no limit and no checkoff; verification was the host suite
  **51/51**, `test_envelope_timings` **56/56**, and the full host discover **1 841 / 7 failures / 1 error** whose failing
  names are the known group and none in this change's modules, with the new case red-proved by a field-index mutation and
  a byte-identical restore. See
  [`s65-hardware-claims-reading`](evidence/2026-10-17-s65-hardware-claims-reading/EVIDENCE.md).

  **Native remainder closed on the rolling chain 2026-10-17 — force 2 is now forced; 6.5 stays open on its hardware
  half.** `test_rolling_path_interval_ab_revision_trace_serves_each_interval_its_own_revision` drives the rolling chain
  through its production entry over a demoted 24-bar target whose **published root is replaced by a new revision while
  interval A serves**, and measures what the chain actually does rather than what the shared-view path did: A is adopted
  at its own deadline, the edit makes the **already-prepared successor stale**, the episode **ends at the boundary**
  (`endedAt == 6144`), and the chain then demands a **later grid-aligned window** (measured bar 14) which serves r1's
  content — r0's sustained note is articulated once and never re-served, r1's replacement note exactly once with one
  closure. The closure rule is pinned where it is decided: a **slice** is not removal evidence
  (`planClosureSettlementForSlot(…, incomingIsWholeLoop=false)` → `closureCount == 0`, `preservedCount >= 1`, nothing
  emitted, entry still sounding), while whole-loop authority closes it **exactly once** through
  `applyClosureSettlementForSlot`, whose assembly order *is* the spec's "closures before the replacement's note-ons".
  **Red-proof:** M2 (`planClosureSettlementForSlot`'s `absenceMeansRemoval` forced `true`) fails this fixture at "the
  slice handoff closes nothing" plus the pre-existing `test_cross_generation_closures_are_reconciled_and_an_empty_window_silences_nothing`;
  two revision-currency mutations (M1 alone, M3 with the resolver's own Ready-branch term) are recorded as **non-red
  proofs** — the stale-successor outcome is redundantly guarded — with a token-liveness control proving the trace
  measurement itself is live. Gates: focused **214/214**, full native **3,304 / 7 skipped / 3,297 succeeded / 0 failed**
  (pre-change 3,303 / 7 / 3,296), no production line changed and every mutation restored byte-identically
  (`sha256 0cae6d6b…`), `git diff --check` clean. **Still owed on 6.5:** the hardware half's multi-track contention,
  external clock and boundary content parity; **not claimed:** NOTE_EDIT integration (3.15d) and any device observation
  of the closure ordering. See
  [`s65-rolling-ab-revision-trace`](evidence/2026-10-17-s65-rolling-ab-revision-trace/EVIDENCE.md).
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

## 7. DEC-113 disposition — Route A withdrawn; DEC-108 pinned-source index governs

DEC-113 is superseded by DEC-114 and has no implementation authority. Route A's interval-root publisher, LoopSnapshot sub-root, per-role root admission and root-specific device instrumentation are retired; none was implemented. The earlier Route A red-team blockers are historical and do not gate the DEC-108 retained-source consumer path.

Carry forward only independent landed work: the exact modular `CommittedEventRange` chunk-gate correction from the former 7.2 remains a correctness fix; the `PendingPassChangeProgress` window fields are unused Route A residue and are cleanup candidates in 3.7a. Do not build an interval-root object or alternate content source.
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
