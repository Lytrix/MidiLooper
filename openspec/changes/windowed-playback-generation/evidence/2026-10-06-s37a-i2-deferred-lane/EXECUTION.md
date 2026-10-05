# Task 3.7a — increment I2: the deferred lane turn serves a rootless demand

Execution packet for **I2** of slice 3.7a, written **before** the production edit it authorizes. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), the slice's own
[`../2026-10-06-s37a-production-consumer/EXECUTION.md`](../2026-10-06-s37a-production-consumer/EXECUTION.md) (ladder row
**I2**), `tasks.md` item 3.7a, **DEC-108** (§10, §13(a)/(b)/(c), §14(c)) and **DEC-114** (sequencing).

## Identity and scope

- **Increment:** I2. I1 (landed) made the *requester* and *readiness* answer from the retained revision. I2 makes the
  **declared owner's production turn** serve a demand for a slot with **no current published root** — the shape the
  device legs measured as `launch_requested … root=0 …` never reaching `ready`.
- **Owner and chain:** `Track::stepPlaybackViewMaintenance` (the owner) → `stepCommittedPlaybackRetry` (the deferred
  source qualification) → `requestPlaybackViewMaintenance` (the **one** production admission, unchanged) →
  `RuntimePlaybackViewBuild` fill/order → `publishCompletedPlaybackWindow`.
- **Entry condition (I1's primitive, reused):** `Track::gatherRetainedSourceWindowForSlot` — one bounded page of the
  interval per call, directed by `RetainedIntervalIndex`.
- **What I2 adds:**
  1. **`LoopPlaybackRuntime` state** for one gather per slot: phase (`Idle`/`Gathering`), the resumable
     `CommittedEventRange::WindowTraversalCursor`, the gathered window (`SessionMidiEventVec`, external allocator), the
     slice count and the named refusal that left the demand owed.
  2. **The deferred turn's retained branch**: freeze the pending demand (the snapshot path's own rule), gather one page
     per turn, and on the completing turn admit the gathered window through `requestPlaybackViewMaintenance` — with the
     demand's *own* deadline validated first (the promise is honoured, never re-granted).
  3. **The window source authority as one predicate** — `Track::playbackWindowSourceIsCurrentForSlot` = the current
     published root **or** a retained revision equal to the loop's current revision — applied at the window-lane root
     checks (`committedPlaybackRetryCanProgress`, the frozen-request validation, the launch-preparation validity check,
     `playbackLaunchReadyForSlot`, `revalidateCompletedPlaybackWindow`, the rolling boundary candidate).
  4. **Boundedness guard**: the gather's turn budget (the same page/slice arithmetic I1's request path uses); exceeding
     it refuses by name (`WindowExceedsRetainedCapacity`) rather than gathering without bound.
- **Preserved:** the snapshot (root-backed) qualification path, the merged-window fallback, the checkpoint/effective-
  content authority (`Loop::publishedSnapshotIsCurrent()` still gates everything that is *not* the window lane), the
  launch's own binding/handoff rules, `activeLoopIndex` activation authority, and every existing refusal name.
- **Primary files:** `include/TrackPlaybackRuntime.h`, `src/Track/TrackPlaybackRuntime.cpp`, `include/Track.h`,
  `src/Track/TrackPlaybackWindowBuild.cpp`, `test/test_runtime_working_set_ownership/…`.
- **Non-goals:** no second scheduler or owner; no removal of the merged fallback (3.7b); no paged (over-L) or preview
  consumer (I3); no per-layer edit resolution (the retained gather still refuses a revision with Active note-edit rows
  by name); no device run; no timing-envelope claim.
- **Commit boundary:** independently buildable; the lane's root-backed behaviour is byte-for-byte what it was.

## Oracle fixed before behavior edits

| Case | Initial state | Ordered actions | Expected observation | Counterexample that must fail |
|---|---|---|---|---|
| **W-37a-7** the committed demand is served rootless | demoted 512-bar slot with committed content in two bars, **no published root**, retained source at the current revision, prewarmed runtime; a CommittedSnapshot demand retained for the interval | `requestCommittedPlaybackWindow(slot, coverage, phase, clock)`; drive `stepPlaybackViewMaintenance(…)` with a qualified clock | the demand is retained (`DeferredSourceQualification`); the first turn freezes it and serves **one slice** (`retainedGatherActive()`, `retainedGatherUnits == 1`, no coverage source owned); further turns advance the gather; on the completing turn the admission begins the build and the pending record is discharged; the window publishes with `origin == Committed`, `snapshotIdentity == nullptr` and the loop's revision; its content equals the fallback's effective gather for the same interval | today's behaviour (the demand sits `AwaitingSourceQualification` forever, `committedPlaybackRetryCanProgress` false); or a window published from another revision; or a demand dropped instead of retained when the gather refuses |
| **W-37a-8** the launch demand reaches `Ready` and binds rootless | the same slot, launch shape (2-bar interval at phase 0, `preparePlaybackLaunchForSlot`) | `preparePlaybackLaunchForSlot(slot)` → `Requested`; drive turns; then `playbackLaunchReadyForSlot` / `bindPlaybackLaunchForSlot(tick)` | the launch is served from the retained source, reaches `Ready`, and `bindPlaybackLaunchForSlot` **returns true** and moves the state to `Bound` — the chain the device leg measured as never completing for a rootless target | keeping the root requirement in the launch readiness check (the exact measured starvation: `Requested` forever) |
| **W-37a-9** the promise is honoured, not re-granted | as W-37a-7 | drive the turns with the clock past the demand's `deadlineTick` | the gather is discarded, the demand stays owed with a **positional** refusal (`DeadlineExpired` / epoch / geometry as applicable) and **no** window is published | admitting against a fresh lead after the promise expired (the deadline silently re-granted) |
| **W-37a-10** refusal and teardown are named | as W-37a-7, with (a) the retained revision **lagging** the loop, (b) a transport stop / transition mid-gather | request; drive | (a) the gather refuses by name, nothing is inspected beyond the currency check, the demand stays owed; (b) the transition discards the gather (its own candidate work) while the served window and the demand survive | (a) serving the lagging revision as current; (b) leaving a gather running for a demand that no longer exists, or dropping the demand with the gather |

**Invariants asserted, not assumed:** the root-backed path is unchanged (its own fixtures stay green); the demand is
never dropped by a refusal (retained or discharged by an admission, never silently cleared); the gather's buffer is
released on discharge/cancel/reset; the served window is untouched by a gather discard; no RAM1 variable grows.

## Controls and coverage

- **Production paths exercised:** `requestCommittedPlaybackWindow` (retention), `preparePlaybackLaunchForSlot` (the
  launch demand), `stepPlaybackViewMaintenance` (the turn), `requestPlaybackViewMaintenance` (the admission),
  `publishCompletedPlaybackWindow` (publication), `bindPlaybackLaunchForSlot` (the launch binding). Fixtures read state
  through the existing `TrackCaptureStopTestAccess` runtime handle — no new test-only seam.
- **Bounded work:** one gather slice per turn; the slice is I1's page-sized primitive; the turn budget is the window's
  own capacity arithmetic.
- **Baseline failure/green reuse:** the existing committed-retry fixtures (root-backed) must stay green and unmodified;
  the I1 fixtures stay green.
- **Not covered here (named):** over-capacity content through the paged path (I3), preview (I3), per-layer edit
  resolution on a retained revision (I3), hardware timing.

## Red-proofs (I2)

| ID | Mutation | Must fail |
|---|---|---|
| **MR-3.7a-E** | the deferred turn's retained branch removed (the lane keeps requiring a current root) | W-37a-7 and W-37a-8 |
| **MR-3.7a-F** | the predicate reduced to `publishedSnapshotIsCurrent()` (root only) at the launch readiness check | W-37a-8 |
| **MR-3.7a-G** | the gather's deadline validation skipped (admit whenever the gather completes) | W-37a-9 |
| **MR-3.7a-H** | a refused gather drops the demand instead of retaining it | W-37a-10(a) |

Each mutation is reverted and the files restored byte-identically (sha256 recorded); a mutation that does not fail its
fixture is recorded as a non-red-proof rather than reworded.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership` (new fixtures plus the lane's existing ones).
- Broad: `pio test -e native` — the baseline is **2,817 cases / 7 skipped / 2,810 succeeded / 0 failed**.
- Build/RAM1: `pio run -e teensy41-capture-serial` through the RAM1 budget guard; the approved line is
  `variables 96,192` with `code 391,768 + padding 1,448` (floor 840). `LoopPlaybackRuntime`'s two `static_assert` size
  pins are re-based **in the same commit** with the measured reason (external per-slot storage; no RAM1 variable).
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`, `git diff --stat -- src include`.
- Device: **NOT RUN**; the launch fix this increment reaches could be exercised later by 6.4/6.5's profile, and that is
  recorded as owed rather than claimed.
- Evidence path: `evidence/2026-10-06-s37a-i2-deferred-lane/`.


## Completion record

Filled after the run; planned results are never copied here as passes. Detail: [`EVIDENCE.md`](EVIDENCE.md).

- [x] Increment I2 implemented: the retained-source gather in `LoopPlaybackRuntime`, the deferred turn's retained
      branch, the window source-currency predicate and its seven application sites, the teardown/ownership terms.
- [x] Fixtures W-37a-7 … W-37a-10 implemented and discriminating (three new cases; the committed-demand one uses a
      200-event dense bar so the per-turn page accounting is real).
- [x] Mutations red-proved with byte-identical restores: **MR-3.7a-E** 2 fails, **MR-3.7a-F** 1 fail, **MR-3.7a-G**
      1 fail, **MR-3.7a-H** 1 fail — and one **recorded deviation**: MR-3.7a-H's first run *crashed* the suite (132 of
      150 reported) because the fixture dereferenced a dropped record after a non-fatal assertion; the fixture was made
      crash-safe and the mutation then failed that case cleanly.
- [x] Focused **149/149**; full native **2,820 cases / 7 skipped / 2,813 succeeded / 0 failed**.
- [x] Capture build **SUCCESS** — `variables 96,192` unchanged / `code 391,832 + padding 1,384` (floor 840; I2's own
      **+64 B** of ITCM). Both `LoopPlaybackRuntime` size pins re-based with compiler-measured figures (target 7,160 /
      host 9,216).
- [x] `openspec validate windowed-playback-generation --strict` valid; `git diff --check` clean.
- [x] Task checkoff: **3.7a stays open** — I3 and I4 remain, the fallback stays until 3.7b, and the device leg that
      would exercise the rootless launch binding is owed (6.4/6.5).
