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
- [ ] 3.5 Make readiness the completed-publication predicate with a named holder — verify the empty-interval fixture and
  the AC-3 fixture pass, and that **RWO's two demoted-target fixtures are re-pointed** rather than left depending on
  empty ⇒ not-ready
- [ ] 3.6 Implement **pinned-source retention**, the **interval index over the selected `LoopSnapshot`** and the lag rule — verify F-3
  passes, that later intervals are constructible from the pinned revision through the index over **pinned bodies** with no
  resident whole-loop root, that index updates cover both affected ranges and publish atomically with the source revision
  (§14), and that the retention and index cost is charged
- [ ] 3.7 Remove the live mutable merged-window build — **the last step of the ladder, stage (4)**, and only after **every**
  dependency is implemented and verified: committed interval publication (3.2), readiness and retention (3.5/3.6), the
  bounded legacy compatibility path (3.12), preview publication (3.13), shared-view ownership (3.11), fault recovery
  (4.5) and undo/redo audible handoff (3.14). Keep a cold reference path for parity testing.
  Verify F-7 still passes on the cold path, that no demoted slot
  reaches the removed path (`playMidiEventsForSlot`'s fallback), and that `ExecutionBounds.md` row 2 is re-classed only
  with evidence
- [ ] 3.8 Confirm reuse before invention — verify a review note names the existing resumable entry points used (the
  declared build contract, bounded body-format write/parse, resume arming, the single persistence drain budget) and that
  no second publication scheduler exists
- [ ] 3.9 Deliver readiness receipts by the two delivery rules — verify a receipt produced under transport reaches the
  evidence stream and that diagnostic cadence cannot evict it

- [ ] 3.10 **Implement fair, bounded, deadline-ordered service** (*DEC-108 §13b*) — a recurring bounded opportunity on the
  existing deferred machinery; round-robin across eligible holders; earliest outstanding playback deadline first within a
  holder; **cursor advances even when a holder cannot progress**; capture finalization and persistence keep their own
  opportunities (no starvation); one declared grain per admitted turn, no hidden stacking. Verify with a fixture that
  keeps an early holder blocked while later holders need preparation and capture/persistence work is pending, asserting a
  progress bound for **every admitted holder** (including inbound switch preparation and overlapping finalization), and
  derive the supported envelope from grain costs and the worst permitted service gap — invent no microsecond budget
- [ ] 3.11 **Implement the shared-view ownership rules and transitions** (*DEC-108 §13d*) — published/current views
  read-only, only the owning builder writes its candidate, a ready view immutable until adopted or retired, storage
  reusable only after every owner releases it; then the transitions: new edit (retain the frozen source, coalesce the
  successor), slot switch (inbound coverage before binding; retain outbound until readers and closures finish),
  selected-track change (never retargets an existing worker), record/overdub stop with successor start (predecessor's
  frozen ownership preserved; successor never borrows its buffers), transport stop, clear/load, late completion (rejected
  for publication; releases only what it owns). Verify each production grain can be paused, the transitions injected, and
  ownership, **generation-qualified identity** (returning to a slot revives nothing), no premature reuse, no lost
  requests and eventual cleanup asserted

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
- [ ] 3.13 **Implement the preview contract — immutable deferred publication** — publish preview through the same
  immutable deferred discipline as committed content, defining **source freezing** (the preview revision a build works
  from), **cancellation** (session mutation, track re-focus, undo, exit between build grains) and **committed-content
  handoff** at session exit. Verify fixture 2.12 passes: no mixed or stale preview publication, preview is never accepted
  as committed currency, and the live mutable fallback's removal is not narrowed on preview's account

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

## 4. Checkpoint parity and content parity

- [ ] 4.1 Land **F-5** (checkpoint parity, undo) — verify an undo restores exactly what its entry named, including the
  **lagging** case, whose outcome must be named rather than reported as current
- [ ] 4.2 Land **F-6** (checkpoint parity, save) — verify a persisted body is equivalent to the root it was serialized
  from and a restore's outcome is either current **or** named as stale; the fixture must falsify today's
  `storePublishedSnapshot(…, kSnapshotCoverageCurrent)`-without-reconcile path
- [ ] 4.3 Land **F-7** (content parity across a window boundary) — verify the windowed stream equals the whole-loop
  stream across the boundary, including **boundary note closures**, driven through the project's existing
  content-verification pipeline rather than a second one
- [ ] 4.4 Land **F-8** (length-independent root residency) — verify a residency statement per slot (windows resident,
  root demoted) plus content parity for the demoted state, **swept** over loop lengths

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
