# Task 3.7a (I7) — the retained-source gather resolves the pinned revision's note edits **per layer**

Execution packet for the next increment of slice **3.7a** (*"Wire the retained-source interval index into the production
window consumer while retaining the legacy fallback"*), written **before** the firmware edit it authorizes. Authority:
[IMPLEMENTATION_READINESS.md §G2 execution packet](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), `tasks.md` item **3.7a**,
[`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) §"Slice 3.7a", the slice packet of I1
([`../2026-10-06-s37a-production-consumer/EXECUTION.md`](../2026-10-06-s37a-production-consumer/EXECUTION.md)),
**DEC-108** (§10, §13(c), §14(c)) and **DEC-114**.

## Why this increment, and what it is the next step of

3.7a's retained-source gather (`Track::gatherRetainedSourceWindowForSlot`, landed by **I1**) answers an interval from a
retained revision's pinned bodies. It **refuses** a revision whose pins include an **Active Note note-edit row**
(`RetainedWindowGatherResult::PinnedEditRowsUnsupported`), and I1 recorded why it must: the effective-content owner
(`Loop::copyEffectiveCommittedEventsInRange`) resolves Active Note edits **per committed list** and then merges, "and
says why (a merged-then-apply pass lets a LIFO delete pair a hidden On with another pass's Off)", so applying them flat
in the gather "would be a *different* answer". I1 named per-layer resolution as **I2's**; I2 and I3 both deferred it, and
it is still owed. Consequences today, measured by reading the shipped code:

* a rootless (demoted / restored) slot whose revision has note edits is served **nothing** by the window lane: the
  gather refuses, `requestPlaybackWindow` answers `SourceUnavailable`, the deferred lane names `EditRowsUnsupported` and
  `committedPlaybackRetryCanProgress` declines to re-attempt it — while the merged-window fallback is empty for that
  shape by construction. That is *silence for edited content*, the case 3.14's undo/redo handoff and the restored-library
  shape both live in.
* **and the paged legacy lane is a latent wrong-content path the moment this refusal goes away**: the pager
  (`Track::beginPagedPlaybackForSlot` → `WindowPagedStream`, task 3.12) enumerates the same pinned lists through
  `collectPinnedCaptureLists` and pages them **without** any edit resolution. Today the gather refuses first, so the
  pager is unreachable for such a revision. This increment removes that accident, so it must replace it with a named
  refusal.

## Identity and scope

- **Slice:** 3.7a / increment **I7**. **DEC-114's direction is unchanged:** the whole-loop `LoopSnapshot` stays the
  checkpoint and effective-content authority; the playback source for a root-demoted slot is the retained revision over
  pinned bodies. No interval-root object, no second publisher, no second scheduler, no new content authority. The
  *rule* being implemented is the effective-content owner's own, cited rather than re-invented.
- **Owner and production chain (unchanged):** the gather is reached from `Track::requestPlaybackWindow`
  (the 3.7a-named requester) and from the deferred lane's `Track::stepRetainedSourceGatherForSlot`
  (reached from `Track::stepCommittedPlaybackRetry` → `Track::stepPlaybackViewMaintenance`, the declared owner). Both
  keep the merged-window fallback intact.
- **Primary files:** `src/Track/TrackPlaybackWindowBuild.cpp` (the gather, its callers' refusal vocabulary and the
  pager's guard), `include/Track.h`, `include/TrackPlaybackRuntime.h`,
  `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp`.
- **Preconditions already present:** I1's gather + index bound + readiness vocabulary; I2's deferred lane and retained
  gather state; I3b's pager and its own refusal vocabulary; 3.12f's named-refusal pattern
  (`PagedPlaybackOutcome::ClosuresExceedDeclaredCapacity` + `PlaybackViewPendingReason::…` + the retry rule);
  `EditApply`'s `applyNoteEditPassSequence`.
- **External blockers:** none. The device leg stays 6.4/6.5's, and the upload rule is untouched (no build is uploaded).
- **Non-goals (explicit):** no per-layer resolution in the **pager** (it refuses such a revision by name here — the
  same "wrong content is worse than not serving" trade I3c-b/3.12f took); no change to the non-edited (single-traversal)
  path — its bytes and behaviour are deliberately unchanged, so every landed fixture and red-proof of I1–I5 and 3.12f
  keeps its meaning; no change to `applyNoteEditPassSequence` or to the oracle
  (`Loop::copyEffectiveCommittedEventsInRange` is **not** touched, so the differential comparison stays independent); no
  change to `activeLoopIndex`, to the merged-window fallback, to `ExecutionBounds.md` rows, or to `kMaxOpenNotes`; no
  fallback removal (3.7b).
- **Commit boundary:** one commit on the working branch `feature/windowed-playback-generation` with the fixtures in the
  same commit; no PR without an explicit request.

## Oracle fixed before the behavior edit

The oracle is **content truth by comparison with the effective-content owner**, plus named refusals — never "a build
happened".

| Case | Initial state | Ordered actions | Expected observation | Counterexample that must fail |
|---|---|---|---|---|
| **W-37a-21** edited content is served, per layer | a retained-source (restored-library) slot, rootless, whose committed revision carries **two layers** (record + one overdub) with the **same pitch** in both, plus a pinned **Active Note Delete** row targeting the **overdub layer's** note-on; the row set is pinned with the revision | `gatherRetainedSourceWindowForSlot(0, 0, 2 bars, retainedRevision, cursor, scratch, layerScratch)` to completion | `Served`; `scratch` **element-wise equal** to `Loop::copyEffectiveCommittedEventsInRange(out, 0, 2 bars)` — the owner's per-layer answer — with the record layer intact and the overdub note's own On/Off gone | applying the rows to the **merged** stream (the flat answer): it pairs the overdub On with the **record layer's** Off through `findNoteOffForOnIndex`'s LIFO pairing, so the record layer's On survives **unclosed** and the overdub's Off dangles → a different vector. The fixture asserts that premise explicitly (`flat != oracle`), so the case cannot pass vacuously. |
| **W-37a-22** the per-layer answer is *reached through production* | as W-37a-21, with the demand made by `Track::requestPlaybackWindow` and served by the deferred turn | `requestPlaybackWindow(0, playhead, scratch)`; then drive `stepPlaybackViewMaintenance()` to no pending work | `Admitted` (not `SourceUnavailable`), the published window's identity is the retained revision's, `origin == Committed`, `snapshotIdentity == nullptr`, and the **served** window equals the owner's per-layer window | today's behaviour: `SourceUnavailable` for a revision whose note edits are resolvable (the refusal this increment replaces) |
| **W-37a-23** the layer bound is the slice bound, and the gather resumes | as W-37a-21, with the **first** layer's in-window content over one slice (`kRetainedGatherSliceEvents` = 128) | drive the gather call by call, recording `complete`/`inspectedChunks`/`eventsAppended` per call | more than one call, `complete` false until the last, and the final `scratch` still element-wise equal to the owner's answer; no call appends more than `kRetainedGatherSliceEvents` events **for one layer** | a gather that resolves the merged stream when a layer's traversal completes mid-call, or that reports `complete` before the last layer was merged |
| **W-37a-24** the pager refuses such a revision **by name** | a rootless retained revision with a pinned Active Note edit row and an interval over `L` (the slice ceiling route) | drive the deferred turn until the lane answers | `PagedPlaybackOutcome::PinnedEditRowsUnresolved`, **no ring established** (`pagedActiveForInterval` false), nothing emitted, the demand still owed with `PlaybackViewPendingReason::PinnedEditRowsUnresolved`, and `committedPlaybackRetryCanProgress` **false** | today's accidental guard (the gather refusing) removed and nothing in its place: the pager would page the raw pinned bodies and the deleted note would sound (unresolved content), which is the harm class this case exists for |
| **W-37a-25** an unedited revision is untouched | a retained revision with **no** Active Note edit row | same drive as W-37a-21 | the single-traversal answer, byte-identical to I1's fixtures' expectation, and `inspectedChunks` unchanged from I1's measurement | a per-layer path taken when no rows exist (it would spend one call per layer on content that needs none) |
| **W-37a-26** an edit row outside the pins is not applied | the retained revision's pins **exclude** the Active row (a row created after retention) | gather for the retained revision | `Served` with the **unapplied** content (the revision's own membership decides) | applying the live Active set instead of the pinned set (a later revision's edit leaking into an earlier revision's window) |

**Preserved invariants (asserted, not assumed):** the non-edited gather path and its bytes are unchanged; the
merged-window fallback still answers the same revision; `activeLoopIndex` is still activation authority; no new owner,
scheduler, buffer-lifetime or content authority exists; a refusal never mutates the retained source, the index or the
served window; the differential oracle is not touched.

**Differential oracle.** `Loop::copyEffectiveCommittedEventsInRange` (the effective-content owner) over the same
revision and window — the same oracle I1 used. A difference is a finding, never a tolerance.

## Controls and coverage

- **Rule reuse, not rule re-invention:** the per-layer apply uses the shipped `applyNoteEditPassSequence(layer, rows,
  loopLength)` and the owner's order (each list: window → tick sort → apply), and the layer merge reproduces the
  owner's `mergeSortedLoopCaptureLayers` rule (`std::merge` semantics: on equal ticks the **accumulated** side wins) by
  hand, because that instantiation is not exported and this TU must not pull introsort/merge machinery into flash
  (I1's measured `std::sort` finding, +1,280 B).
- **Production paths exercised:** the gather itself, `requestPlaybackWindow`, the deferred turn
  (`stepPlaybackViewMaintenance`), the pager's begin, and the retry rule — never a copy of any of them.
- **Boundedness:** one layer per call is the granularity; the traversal primitive is bounded by
  `kRetainedGatherSliceEvents` exactly as before, and a layer whose slice returns no event advances without spending
  the call's work budget. The **named residual**: a revision with more layers than the callers' own ceiling
  (`kRetainedGatherMaxSlices`) cannot be gathered for a window in one request — it refuses (request path) or is routed
  to the pager, which refuses it by name (W-37a-24).
- **Untestable before link:** none — all six cases run in the native suite. No device claim.
- **Allocation:** the per-layer scratch is the *caller's* buffer (the lane's own runtime field; a local in the request
  path), because two callers may be in flight for the same slot and the shared cursor does not carry it; one transient
  layer copy per completed layer is the only allocation this increment adds, off-clock. Point-of-use admission gating
  for this family is DEC-115's follow-up list, not this slice.

## Red-proofs (planned, each restored byte-identically and sha256-checked)

| ID | Mutation | Must fail |
|---|---|---|
| **RP-I7-flat** | the rows applied to the whole gathered window (the flat answer) instead of per layer | W-37a-21 (element-wise equality) |
| **RP-I7-pins** | the rows taken from the loop's live Active set instead of the pins | W-37a-26 |
| **RP-I7-merge** | the layer merged into `out` by plain append (no tick merge) | W-37a-21/23 |
| **RP-I7-guard** | the pager's pinned-edit-row guard removed | W-37a-24 (the ring is established) |

A mutation that does not fail its fixture is recorded as a non-red-proof with its mechanism named, never reworded.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership`; the change's neighbouring suites
  (`test_window_paged_legacy`, `test_windowed_preview_integration`) unchanged.
- Broad: `pio test -e native` (baseline: 2,842 cases / 7 skipped / 2,835 succeeded / 0 failed).
- Build/RAM1: `pio run -e teensy41-capture-serial` through the RAM1 budget guard; baseline line
  `variables 96,192` / `code 392,248 + padding 968` (floor 840). ITCM growth is not expected: every function this slice
  touches is `TRACK_COLD_MEM`. A `.data` byte that snaps an alignment edge is a finding to fix, not to re-base.
- Any `LoopPlaybackRuntime` size pin re-based must carry a **compiler-measured** figure.
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`,
  `git diff --stat -- src include`.
- Device: **NOT RUN** (no device-visible behaviour change is claimed; the lane has no row family on the shipping env).
- Evidence path: `evidence/2026-10-06-s37a-i7-layer-edit-resolution/`.

## Completion record

**Slice I7's record (2026-10-06).** Landed as designed, with one design change **measured** rather than planned and one
red-proof that had to be re-earned. Full detail and the raw numbers: [`EVIDENCE.md`](EVIDENCE.md).

- Oracle/contract unchanged: **YES** for the content property (the differential oracle is
  `Loop::copyEffectiveCommittedEventsInRange`, untouched, and the fixture asserts the flat answer differs *before* it
  asserts equality). **One case's expectation moved with the measurement:** W-37a-26's state (an Active row outside the
  revision's pins) is unreachable through production, so the case is a *safety* property reached by mutating the pass set;
  it is red-proved (RP-I7-pins) and named as unreachable rather than presented as production behaviour.
- Fixtures implemented and discriminating: **YES** — `test_3_7a_i7_pinned_note_edits_are_resolved_per_layer`
  (W-37a-21/22/23/26) and `test_3_7a_i7_the_paged_lane_refuses_pinned_note_edits_it_cannot_resolve` (W-37a-24).
- Red-proofs: **four, all RED with byte-identical restores** (RP-I7-flat, RP-I7-merge, RP-I7-pins, RP-I7-guard;
  `src/Track/TrackPlaybackWindowBuild.cpp` sha256 `48c57b2fe9d7c78e…`). **RP-I7-merge's first attempt was a
  non-red-proof** (the fixture's second layer was fully deleted by the row, so append and merge coincided); the fixture
  gained a surviving interleaving pair and the mutation then failed it. Recorded, not reworded.
- Native verification: **GREEN** — focused **166/166** (baseline 164), full native **2,844 / 7 skipped / 2,837 succeeded /
  0 failed** (baseline 2,842/2,835).
- Build/RAM1: **GREEN and byte-identical** — `variables 96,192 / ITCM 393,216 (code 392,248 + padding 968)` /
  `locals 34,880`, floor 840, 128 B of headroom. **The ITCM budget was the increment's real work:** the first working
  form cost **+320 B** (the `std::vector<EditPass>` machinery emitted out of line because the gather is large), and moving
  the row collection and the rows themselves into small flash helpers restored the line byte-for-byte. **No re-base.**
- `LoopPlaybackRuntime` pins re-based **compiler-measured**: target 7,384 → **7,400**, host 9,528 → **9,552**.
- Hygiene: `git diff --check` clean; `openspec validate windowed-playback-generation --strict` valid.
- Device: **NOT RUN** — no device-visible behaviour is claimed (the lane has no row family on the shipping env).
- Task checkoff: **3.7a stays open** (no 3.7b conditions changed), as do 3.12/3.13/3.14/4.5. What this increment closes is
  the *retained-source gather refuses note-edited revisions* residual that I1 recorded and I2/I3 deferred.
