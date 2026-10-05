# Task 3.7a — the retained-source window consumer: execution packet and increment ladder

Execution packet for slice **3.7a** (*"Wire the retained-source interval index into the production window consumer
while retaining the legacy fallback"*), written **before** the production edit it authorizes. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), `tasks.md` item **3.7a**,
[`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) §"Slice 3.7a", **DEC-108** (§10, §13(a), §14(c)) and
**DEC-114** (the sequencing: 3.7a with fallback retained → 3.14/4.5 against the real handoff → 3.7b removal).

## Identity and scope

- **Slice:** 3.7a. **DEC-114's active direction:** the whole-loop `LoopSnapshot` stays the checkpoint and
  effective-content authority; the *playback source* for a root-demoted slot is the generation-qualified retained
  revision over pinned bodies, queried through the `LoopSnapshot` effective-content owner and `RetainedIntervalIndex`.
  No interval-root object, no second publisher, no second scheduler.
- **Status:** specified here; implemented incrementally below. Only the increments whose rows are checked in the
  completion record are claimed.
- **Owner and production chain:** the owner is `Track::stepPlaybackViewMaintenance` with its declared
  `CoverageServiceContract`. The production chain this slice extends:

  | stage | symbol | today's behaviour for a slot with no published root |
  |---|---|---|
  | request (window) | `Track::requestPlaybackWindow` (`src/Track/TrackPlaybackWindowBuild.cpp:2908`) | enumerates through `Loop::tryGatherPublishedSnapshotEventsInWindow`, which returns `false` when `publishedSnapshot()` is absent → **`SourceUnavailable`** |
  | request (deferred, production) | `Track::requestCommittedPlaybackWindow` → `pendingViewRequest_` with `reason = AwaitingSourceQualification` | retained; the demand survives as a *named* debt |
  | deferred turn | `Track::stepCommittedPlaybackRetry` (`:2573`) | `beginCoverageQualification(*snapshot)`; a null snapshot → `GrainDenied`; and `committedPlaybackRetryCanProgress` (`:2554`) requires `loop.publishedSnapshotIsCurrent()`, so the slot is not even eligible |
  | readiness | `Track::playbackWindowReadinessForSlot` (`:939`) | rootless → `OwesSource` / holder `published_source` |
  | fallback (kept) | `ensurePlaybackMergedMidiEventsBuilt` → `buildMergedPlaybackFallbackCold` / `Loop::copyEffectiveCommittedEventsInRange` | the measured silence path (`GatherMissHoldEmpty`) for a rootless long loop |

- **Primary files:** `include/Track.h`, `src/Track/TrackPlaybackWindowBuild.cpp`,
  `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp`, this directory.
- **Preconditions already present:** 3.6 retained source + interval index + lag rule; 3.11 shared-view transitions;
  3.5 readiness; 3.1 owner/contract; 3.2/3.3/3.4 publication, budget and identity. The 3.6 limit *"the consumer path
  stays 3.7's"* is exactly what this slice discharges.
- **External blockers:** none for the increments below. The hardware half stays with 6.4/6.5 and the upload rule.
- **Non-goals (explicit):** no interval-root `LoopSnapshot`; no new publisher/scheduler/SD format; no fallback
  removal (that is 3.7b); no change to `activeLoopIndex` activation authority; no MIDI-edge resolution or preparation;
  no `ExecutionBounds.md` row 2 reclassification; no pruning of the Route A residue in this increment.
- **Commit boundary:** every increment is independently buildable and leaves the merged-window fallback intact; the
  slice's temporary compatibility is exactly "the fallback is still there".

## Increment ladder

The parent packet names one slice; the production work it asks for spans four separable behaviours, each with its own
production reach and its own discriminating fixture. Ordering follows DEC-114 (fallback intact throughout).

| # | Increment | Production reach | Fixture that must fail without it |
|---|---|---|---|
| **I1** | **The retained-source window gather + the rootless request branch + the readiness holder vocabulary.** A new bounded gather (`Track::gatherRetainedSourceWindowForSlot`) answers an interval from the **retained revision's pinned bodies**, directed by `RetainedIntervalIndex`, applying that revision's Active note-edit rows through `applyNoteEditPassSequence`; `requestPlaybackWindow` takes it when the published-root enumeration cannot answer; readiness names the **build owner** (not `published_source`) for a rootless slot whose retained source is the current revision. | `requestPlaybackWindow` (the 3.7a-named requester) and `playbackWindowReadinessForSlot` (read by the receipt and the switch/launch gates) | W-37a-1 … W-37a-6 below |
| **I2** | **The deferred lane turn serves a rootless demand.** `committedPlaybackRetryCanProgress` / `stepCommittedPlaybackRetry` qualify and build from the retained source (a bounded retained-body cursor with its own unit accounting), so `stepPlaybackViewMaintenance` — the declared owner — completes a rootless window and `publishCompletedPlaybackWindow` adopts it. | the production deferred turn (`Track::stepPlaybackViewMaintenance`, reached from `main.cpp:508`) | to be authored with I2 |
| **I3** | **The overdue consumer halves:** 3.12's paged legacy serving (`WindowPagedStream::produce/consume` driven by a production caller over the same pinned revision) and 3.13's preview requester/lifecycle hooks (`requestPreviewWindow` + `cancelPreviewWindowForSlot` at the revision-advance/mutation/exit seams). | the paged path and the preview lane | 3.12/3.13's own fixtures, extended |
| **I4** | **Differential fallback parity and the timing admission** for the integrated path (the shipped-vs-fallback comparison over the same revision, and the per-turn cost of indexed lookup + page production). | the admission decision | to be authored with I4 |

**I1 is the increment this packet authorizes now.** I2–I4 are named so the ladder's ordering is on the record; each
needs its own row before it is implemented, and none of them is claimed here.

## Oracle fixed before behavior edits (I1)

The oracle is stated as *content truth* plus *named refusals*, never as "a build happened".

| Case | Initial state | Ordered actions | Expected observation after each step | Counterexample that must fail |
|---|---|---|---|---|
| **W-37a-1** rootless serving | slot with committed content (2 events in bar 0) and loop length 64 bars, prewarmed runtime, **no published root**, retained source for the current revision | `requestPlaybackWindow(0, playhead, scratch)`; then drive `stepPlaybackViewMaintenance()` to no pending work | gather result `Served`; `Admitted`; `scratch` equals the effective content of the **same revision** in that window (differential oracle below); a window is published with `origin == Committed`, `snapshotIdentity == nullptr` (rootless by construction), `playbackRevision == loop.playbackRevision`, and the coverage the resolver named | returning `SourceUnavailable` for a slot whose retained source is current (today's behaviour), or publishing a window whose `snapshotIdentity` is non-null (a root it never read) |
| **W-37a-2** the index bounds the lookup | as W-37a-1 | `gatherRetainedSourceWindowForSlot` over an interval whose bars the index marks empty (`40 * TICKS_PER_BAR`, two bars) | `Served` with `inspectedChunks == 0` and an empty destination: the index proved the interval empty, so **no chunk was inspected**; `coveredBars == 0`, `skippedBars == 2` | enumerating the pinned bodies anyway (`inspectedChunks > 0`) — the index would then be decorative |
| **W-37a-3** revision currency, by name | as W-37a-1, then `loop.playbackRevision` advances (an edit) | gather with `expectedRevision = current + 1`, then with `expectedRevision = retained revision` | `RevisionMismatch` for the revision the index was not built for (**no bits read, nothing inspected**), `Served` for the retained one — the one-pass-late *allowance* is the caller's rule (3.6b), not this primitive's | answering the stale request from the index and the bodies (a silently stale window), or refusing the retained revision itself |
| **W-37a-4** no retained source, still refused | a rootless slot with committed content and **no** retained source | `requestPlaybackWindow(0, playhead, scratch)` | `SourceUnavailable` — the named refusal 3.6c's fixture already pins is **re-pointed, not deleted**: the retained source is the only new authority | serving it from the ragged bodies with no retained revision (this is what would make the retention meaningful in name only) |
| **W-37a-5** the readiness holder moves | rootless slot with a **current** retained source and no published window; and the same slot with **no** retained source | `playbackWindowReadinessForSlot(0)` in both states | retained → `OwesBuild` / `window_build_owner` (a build could gather: the source is the retained revision); no retained source → `OwesSource` / `published_source` (unchanged) | continuing to name `published_source` for a slot whose window is buildable from the retained source (the holder would be the wrong owner) |
| **W-37a-6** the indexed ceiling refuses | a 513-bar loop with a retained source (`kRetainedIndexMaxBars = 512`) | `gatherRetainedSourceWindowForSlot` over its first bar | `BarsUnsupported`, destination untouched: a partial answer is never presented as window-bounded | falling back to a full scan while calling the result window-directed |

**Preserved invariants (each asserted, not assumed):** the merged-window fallback still answers the same revision
(`IsPlaybackMergedMidiEventsReadyForSlot` / `copyEffectiveCommittedEventsInRange` unchanged); `activeLoopIndex` is still
activation authority; no new owner or scheduler exists; the served window's identity tuple is unchanged in shape; a
refusal never mutates the retained source, the index or the served window.

**Differential fallback oracle.** For W-37a-1's parity assertion the comparison is against the **existing**
effective-content path — `Loop::copyEffectiveCommittedEventsInRange(out, start, length)` (the merged fallback's own
windowed apply, `include/Loop.h:301`) — over the same revision and window, not against a hand-written expectation. A
difference is a finding, never a tolerance: W-37a-1 fails on any mismatch.

## Controls and coverage

- **Seams reused:** the retained-source API (`retainPlaybackSourceForSlot`, `retainedPlaybackSourceForSlot`), the
  interval query (`queryRetainedIntervalForSlot`), the budgeted body primitive
  (`CommittedEventRange::appendWindowBudgeted`, the primitive `WindowPagedStream` already uses), the edit resolver
  (`applyNoteEditPassSequence`), and the existing harness helpers (`prepareWindowOwnerSlot`,
  `publishBarSpacedRootForSlot`, `commitRecordPassIntoTrackSlotOfLength`). **No new test-only seam is added.**
- **Production paths exercised:** the requester and the deferred turn (`stepPlaybackViewMaintenance`) — not a copy of
  either; stubs are excluded from every asserted path.
- **Failure/stale/edge cases in this increment:** rootless (W-37a-1/4), empty interval (W-37a-2), stale revision
  (W-37a-3), ceiling refusal (W-37a-6), and the readiness holder pair (W-37a-5).
- **Untestable before link:** none — all six cases run in the native suite. The **hardware** half (device duration,
  contention, external clock) is 6.5's and is **NOT RUN** here by construction.
- **Boundedness of the gather:** the interval query is O(bars) with no chunk inspection; the enumeration walks the
  pinned chunk lists with the budgeted primitive in fixed slices. `inspectedChunks` is the reported cost, and the
  empty-interval case is where the index's contribution is measurable.

## Red-proofs (I1)

| ID | Mutation | Must fail |
|---|---|---|
| **MR-3.7a-A** | drop the interval query from the gather (enumerate always) | W-37a-2 (`inspectedChunks == 0`) |
| **MR-3.7a-B** | ignore `expectedRevision` (answer from whatever index is present) | W-37a-3 (`RevisionMismatch`) |
| **MR-3.7a-C** | remove the retained-source branch of `requestPlaybackWindow` | W-37a-1 (`Admitted`) |
| **MR-3.7a-D** | keep `OwesSource` for a retained-backed rootless slot | W-37a-5 |

Each mutation is reverted and the file restored byte-identically (sha256 recorded); a mutation that does **not** fail
its fixture is recorded as a non-red-proof rather than reworded.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership` (owner/retained/readiness fixtures) and the
  change's neighbouring suites (`test_window_paged_legacy`, `test_windowed_preview_integration`) unchanged.
- Broad: `pio test -e native` (full suite; the baseline is 2,815 cases / 2,808 succeeded / 7 skipped / 0 failed).
- Build/RAM1: `pio run -e teensy41-capture-serial` through the RAM1 budget guard; the approved line is
  `variables 96,192` with `code 391,576 + padding 1,640` (floor 840). A `.data`-byte change that snaps an alignment
  edge is a finding to fix, not to re-base.
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`, `git diff --stat -- src include`.
- Device: **NOT RUN** in I1 (no consumer-visible device behaviour changes; the lane still serves rootless slots only
  through the fallback until I2).
- Evidence path: `evidence/2026-10-06-s37a-production-consumer/`.

## Completion record

Filled after the run; planned results are never copied here as passes. Detail:
[`EVIDENCE.md`](EVIDENCE.md).

- [x] Task 3.7a G2 packet authored — **this file**.
- [x] Increment I1 implemented: `Track::gatherRetainedSourceWindowForSlot` + the rootless branch of
      `Track::requestPlaybackWindow` + the readiness holder vocabulary in `Track::playbackWindowReadinessForSlot`.
- [x] Fixtures W-37a-1 … W-37a-6 implemented and discriminating (two new cases in
      `test_runtime_working_set_ownership`).
- [x] Mutations red-proved, restores byte-identical — with **one recorded non-red-proof**:
      **MR-3.7a-A** 1 fail (this slice's `indexAnsweredEmpty`); **MR-3.7a-B** *green* (the gather's own revision gate
      is redundant under the index's authority — recorded, not reworded) and **MR-3.7a-B'** 2 fails (3.6c's fixture
      plus this slice's `RevisionMismatch`) as its replacement; **MR-3.7a-C** 1 fail; **MR-3.7a-D** 1 fail.
      `src/Track/TrackPlaybackWindowBuild.cpp` sha256 `f13fbc9bda0e51a0…`, restored byte-identically.
- [x] Focused **146/146**; full native **2,817 cases / 7 skipped / 2,810 succeeded / 0 failed**.
- [x] Capture build **SUCCESS** — `variables 96,192` (unchanged) / `code 391,768 + padding 1,448` / locals 34,880
      (floor 840, **608 B above it**; the slice's own +192 B, after the measured `std::sort` instantiation cost of
      +1,280 B was removed by sorting by hand).
- [x] `openspec validate windowed-playback-generation --strict` valid; `git diff --check` clean.
- [x] Task checkoff: **3.7a stays open** — I2 (the deferred lane turn serving a rootless demand), I3 (the 3.12 paged
      consumer and 3.13 preview hooks) and I4 (differential parity + timing admission) remain, and the fallback stays
      until 3.7b.
