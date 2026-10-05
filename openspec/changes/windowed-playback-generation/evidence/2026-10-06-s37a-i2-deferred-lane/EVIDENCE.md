# 3.7a (I2) — the deferred lane turn serves a rootless demand

Slice evidence for increment **I2**, whose packet was written before the edit
([`EXECUTION.md`](EXECUTION.md)). Task **3.7a stays open**: I3 (the 3.12 paged consumer and 3.13 preview hooks) and I4
(differential parity over edited content/long windows + the timing admission) remain, and the merged-window fallback
stays until 3.7b.

## What landed

**The declared owner's production turn now serves a demand for a slot with no current published root** — the shape the
device legs measured as `launch_requested … root=0 …` and never reaching readiness.

1. **`LoopPlaybackRuntime` gained the retained-source gather**: a phase, the resumable
   `CommittedEventRange::WindowTraversalCursor`, the gathered window (`SessionMidiEventVec`, external allocator), the
   slice and chunk counters, and the **named refusal** that left the demand owed (`NoSource`, `RevisionMismatch`,
   `BarsUnsupported`, `NoIndex`, `EditRowsUnsupported`, `BodiesUnavailable`, `WindowExceedsCapacity`,
   `DeadlineExpired`, `EpochInvalidated`, `GeometryInvalidated`). `discardRetainedGather()` releases the buffer (swap,
   not `clear`), and `LoopPlaybackRuntime::reset` drops it with the lane's other half-owned work.
2. **The turn**: `stepCommittedPlaybackRetry` freezes the demand (the root-backed qualification's own rule — the record
   is discharged by the freeze), then serves **one bounded page per turn** through I1's
   `Track::gatherRetainedSourceWindowForSlot` (sliced at `kWindowPageCapacity`), and on the completing page admits the
   gathered window through **`requestPlaybackViewMaintenance`** — the same production admission a requester uses, so the
   derived cost, the retained work record, the deadline arithmetic and the discharge rule cannot differ between the two
   callers. A launch demand gets the launch lane's own bookkeeping (`launchPreparation = Building`), exactly as
   `beginQualifiedCoverageBuild` performs it for the root-backed path; the promotion to `Ready` stays the owner's
   existing branch.
3. **The promise is honoured, never re-granted**: before anything is served, the demand's own position is validated
   (geometry, epoch, `deadlineTick`), and a lost position is **named** and the demand is **coalesced back** into the
   record the admission path reads (the oldest deadline of the same operation survives — `retainViewPendingRequest`'s
   rule). The admission's own phase input is the phase a **qualified clock names now**; an unqualified clock (a launch
   may have one) falls back to the phase the demand was made at.
4. **One predicate for the window's source authority** — `Track::playbackWindowSourceIsCurrentForSlot` = the current
   published root **or** a retained revision equal to the loop's current revision — applied at
   `committedPlaybackRetryCanProgress`, the frozen-request validation, the turn's launch-preparation check,
   `playbackLaunchReadyForSlot`, `rollingCandidateReadyAtBoundary` (the slot is now a parameter of that helper) and
   `revalidateCompletedPlaybackWindow`. Everything that is *not* the window lane keeps `publishedSnapshotIsCurrent()`
   as its authority (DEC-114), and the launch/rolling gate rows keep reporting the **root** state, not the predicate's.
5. **Teardown and ownership by name**: `applyPlaybackViewTransition`'s candidate-release row discards the gather and
   coalesces its obligation back; `cancelPlaybackLaunchForSlot` discards a gather that was serving the launch demand it
   clears; `fillPlaybackHolders` / `hasPendingPlaybackViewMaintenance` / `noLiveOwnerForSlot` all see the gather as
   this lane's own work.

## Verification

| Check | Result |
|---|---|
| focused `pio test -e native -f test_runtime_working_set_ownership` | **149/149** (146 + the three new I2 fixtures) |
| full `pio test -e native` | **2,820 cases / 7 skipped / 2,813 succeeded / 0 failed** (I1's baseline 2,817 / 2,810) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` (byte-identical) / `code 391,832 + padding 1,384` / locals 34,880; floor 840, **544 B above it**; I2's own cost is **+64 B of ITCM** |
| `LoopPlaybackRuntime` size pins | re-based **in this commit**, compiler-measured on both ABIs: target **7,120 → 7,160** (+40 B), host **9,144 → 9,216** (+72 B) — per-slot external state, no RAM1 variable, no DTCM |
| `openspec validate windowed-playback-generation --strict` | valid |
| `git diff --check` | clean |

Red-proofs, each run against the final revision (`src/Track/TrackPlaybackWindowBuild.cpp` sha256 `ab9a5a5417eabe5f…`,
`TrackPlaybackRuntime.cpp` `862a1850d2915ccc…`, `include/TrackPlaybackRuntime.h` `cb80ecf8c84536d1…`,
`include/Track.h` `1ca80a39c10baf2a…`, the test file `cf2343581fd37ec6…`) and each restored byte-identically:

| ID | Mutation | Observed |
|---|---|---|
| MR-3.7a-E | the pass's predicate at the turn's retained branch forced off | 2 failed — the committed-demand fixture (`Expected 1 Was 3`: `GrainServed` expected, `Idle` observed) and the launch fixture ("the rootless launch reached readiness") |
| MR-3.7a-F | the launch readiness check reduced to `publishedSnapshotIsCurrent()` | 1 failed — the launch fixture, exactly the measured device starvation shape |
| MR-3.7a-G | the gather's deadline validation skipped | 1 failed — the refused/cancelled fixture (`Expected 8 Was 1`: `DeadlineExpired` expected, `GrainServed` observed) |
| MR-3.7a-H | a position-lost refusal drops the demand instead of retaining it | 1 failed — the refused/cancelled fixture (`Expected 10 Was 0`) |

## Recorded, not smoothed over

- **The first version of MR-3.7a-H crashed the run instead of failing a case** — 132 of 150 cases reported. The cause
  was the *fixture*, not the mutation: `TEST_ASSERT_NOT_NULL` is non-fatal in Unity, so the next line dereferenced the
  dropped record. The assertions now read the record only once it is known to exist, and the mutation then fails that
  one case cleanly. Recorded because a crashing mutation is a weaker signal than a failing one.
- **The first I2 fixture draft asserted the wrong thing about a single-page interval**: it expected
  `retainedGatherUnits >= 1` after the first turn, but the turn *discharges* the gather when its first page completes
  the interval (the counter is reset by the discharge). The fixture now uses a **200-event dense bar** so the interval
  genuinely needs two pages, and it asserts the per-turn accounting (page 1 exactly `kWindowPageCapacity` events, page
  2 completing and admitting).
- **The turn's first grain is "begin + first page", not "begin"**: measured while fixing the above — the root-backed
  qualification spends a turn on its own beginning, and one turn that only began the gather would waste a lane
  opportunity. The packet's oracle said "the first turn freezes it and serves one slice"; the code makes that literally
  true.
- **`rollingCandidateReadyAtBoundary` now takes the slot** (and the predicate's answer) as parameters: as a
  file-scope helper it could not call a `Track` member. No behaviour change for the rolling lane beyond the same
  source-currency question the other sites ask.

## Fixtures

`test_3_7a_i2_a_rootless_committed_demand_is_served_from_the_retained_revision` — W-37a-7: the demand is retained while
nothing is retained (the pre-I2 shape, asserted so the change is visible), `committedPlaybackRetryCanProgress` is false
then true, the first turn freezes and serves exactly one page (`kWindowPageCapacity` events, one slice), the completing
page discharges the demand through the admission and releases the window, and the published window carries
`origin == Committed` with a **null** root identity and the **fallback's own** effective content (200 events, compared
pairwise by tick).

`test_3_7a_i2_a_rootless_launch_target_binds_from_the_retained_revision` — W-37a-8: a 24-bar demoted switch target with
no root, its switch queued, the current revision retained; the production drain reaches `Ready` and
`bindPlaybackLaunchForSlot` **returns true** (`Bound`), with the bound view's `snapshotIdentity` still null.

`test_3_7a_i2_a_refused_or_cancelled_gather_keeps_the_demand` — W-37a-10/W-37a-9: a **lagging** retained revision
leaves the lane `Idle` with the demand owed and its reason intact and nothing published; a **transition** discards the
gather and coalesces the demand back; an **expired promise** refuses with `DeadlineExpired` (`RetainedGatherRefusal`
and the pending reason both named) and publishes nothing.

## Not claimed

No paged (over-`L`) serving and no preview hooks (I3); no per-layer edit resolution on a retained revision (the gather
still refuses Active note-edit rows by name); no differential parity over edited content, long windows or same-tick
order (I4/4.3); no timing envelope; **no device run** — the launch fix this increment reaches would be exercised by
6.4/6.5's profile, and the launch *binding* on hardware stays owed; no fallback removal (3.7b); no change to
`ExecutionBounds.md`. Tasks **3.7a, 3.12, 3.13, 3.14, 4.5** all stay open.
