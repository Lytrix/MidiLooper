# S4f.17–S4f.21 — Record admission and caller cutover (execution packet)

Status: **S17.1 + S17.2 landed 2026-09-29 (Record admission bind and both stop entry points routed
through the drive); S17.3–S17.5 open.**
Baseline `aac1e8df` (§1 preparation/publication boundaries, §2 record playback handoff both
closed). Authority: [shared contract](SHARED-CAPTURE-FINALIZATION.md),
[S4F-CLOSURE-PLAN.md](S4F-CLOSURE-PLAN.md) §3, DEC-093 — shared capture finalization,
DEC-067 — re-entry resources, DEC-078 — interval identity,
[S4F17-EXECUTION.md](S4F17-EXECUTION.md) (the G0 dependency trace this packet turns into
executable slices).

## Fixed oracle — the strict contract

```
S4F16_REQUIRE_CONTRACT=1 pio test -e native -f test_shared_capture_finalization
```
Current result: **45 cases, 45 succeeded.** Before S17.1/S17.2 this was the cutover's red oracle —
**45 cases, 3 failed**:

- `test_record_to_record` — *"A must own a deferred job before any worker grain (record
  currently publishes inline)"*.
- `test_record_to_overdub` — the same assertion.
- `test_overdub_to_record` — B's deferred record job is absent.

The rotation oracle is no longer contract-gated: the deferred stop is the production shape on
every mode, so `rotation()` asserts the deferred bind with or without `S4F16_REQUIRE_CONTRACT`.

## Current routing (VERIFIED, post-S17.2)

- Two production Record stop entry points: `Track::stopRecording` and
  `Track::stopRecordingToStopped`. Both admit the interval through
  `beginDeferredRecordStopForSlot` (which stamps the final length from the maintained
  maximum-tick summary, seals and borrows the raw range under a `canSealCaptureRange`
  preflight, moves the held-note receipt, takes the job-owned geometry receipt, declares the
  Record preparation bound and sets the runtime capture-stop owner), run the transport edge in
  `Track::completeAdmittedRecordStop` (origin alignment, truncation rewind, loop phase,
  destination state and the record-stop playback handoff) and return. `prepareRecordStop` and
  `commitCaptureForStop` remain as the legacy/fault owners but are no longer on the Record stop
  route; S17.4/S17.5 decide their remaining callers.
- A refused admission is not a refusal: `latchAcceptedStop` retains the obligation against the
  interval's own slot and generation, the interval keeps recording, and
  `driveAcceptedInteraction` dispatches on the retained request's **capture mode** — Record
  through the Record chain, overdub through the overdub form. The overdub form still requires
  `isOverdubbing()` and its own live staging, transfer admission, session-exit handoff, causal
  base and ranked preparation budgets; Record populates none of those and needs none of them.
- The deferred worker and the whole Record chain are verified and now **production-reachable**:
  closure, bounded source staging, normalization, transfer, atomic publication with job-owned
  geometry, retirement, and the empty-take terminal.
- The manager continuations still read length synchronously after the void stop:
  `TrackManager::stopRecordingTrack`, `finalizeCaptureAndSelectSlot`, `stopAllTracks`. The stop
  edge stamps `loop.loopLengthTicks`, so those reads are still well defined — which is exactly
  why S17.3 must convert them to a job-qualified geometry-ready result rather than relying on it.
- `Track::prepareRecordStop`'s body is a content-size scan and must not move into a single
  deferred grain; the maintained maximum-tick summary already exists (S4f.17 prerequisite).

## Slice sequence

Each slice keeps its own gate; the parent tasks stay open until the strict matrix is green.

**S17.1 — Record admission bind (no routing change).** Add a Record sibling to the admission:
`beginDeferredRecordStopForSlot(destination, currentTick, slot, interval)` binds a job from the
interval's **own** slot/interval through the existing ring, with the record receipts the
controlled-bind fixture already proves the worker consumes: the `pendingNotes` swap, the raw-range
cut under a `canSealCaptureRange` preflight (no partial cut), the sealed-range borrow, the
job-owned geometry receipt (`captureRecordGeometryReceipt`), the frozen generation, the phase
`ReservedClosure` with `remainingClosureGrains`, the owner/loopId/mode/interval/phaseTick, and the
runtime capture-stop owner. The final length comes from the **maintained maximum-tick summary**,
never a store walk. Record must not be required to hold the overdub-only reservations; those
guards become mode-conditioned. Gate: a fixture that calls the production admission and drives the
worker to publication, asserting exact content, one `RecordPassAdded` undo entry with the job's
before/after geometry, once-only persistence and retirement.

**S17.2 — Route both stop entry points through the drive.** `stopRecording` and
`stopRecordingToStopped` admit the interval instead of calling `commitCaptureForStop` for Record;
they keep their state/destination behavior (PLAYING vs STOPPED) and the transport-barrier
handling. Geometry, undo, persistence and display staleness come from the worker's publication
(the empty take through `RecordEmpty`). Remove the inline Record fallback and the caller-side
duplicate save. Gate: strict R→R and R→OD pass; a mutation reintroducing inline commit fails them.

**S17.3 — Caller continuations (S4f.21).** `stopRecordingTrack`, `finalizeCaptureAndSelectSlot`
and `stopAllTracks` consume a **job-qualified geometry-ready result** rather than reading
`loopLengthTicks` immediately after a void stop. Protect a same-slot destructive restart while
permitting a different-slot successor; a successor must not be serialized behind predecessor
cleanup. Gate: strict OD→R passes; the successor/baseline fixtures in the contract's MODE-ROTATION
and TRANSPORT/GEOMETRY matrices.

**S17.4 — S4f.20 residuals.** Resolve the deferred revts validation's active-slot dependence and
prove `invalidateCaches()` parity against its actual consumers rather than assuming
`notifyCommittedContentChanged` is equivalent.

**S17.5 — Gates.** Strict R→R/R→OD/OD→R/OD→OD green as an enforced regression gate; ordinary
suite success without `S4F16_REQUIRE_CONTRACT` is insufficient. Focused + full native +
capture-serial build with RAM1 padding ≥ 840 and per-track/job/raw/output/overlap PSRAM
accounting. Mutations: reintroduce the inline Record commit; read live geometry after a void stop;
retire a predecessor while a successor still owns it.

## Slice status

**S17.1 — Record admission bind: done (no routing change).**
`Track::beginDeferredRecordStopForSlot(destination, currentTick, slot, interval)` in
`src/Track/TrackOverdubLifecycle.cpp` (TRACK_COLD_MEM) admits against the interval's own slot and
interval behind the same ring/latch guards as the overdub form. It binds: the raw cut under a
`canSealCaptureRange` preflight so a refusal leaves the window untouched, the sealed-range borrow,
the job-owned geometry receipt (`captureRecordGeometryReceipt`), the held-note receipt, the frozen
generation (the interval identity), `ReservedClosure` with `remainingClosureGrains`, the
owner/slot/loopId/mode/interval/phaseTick fields and the runtime capture-stop owner. The final
length comes from `computeRecordStopLengthTicks` over the maintained maximum-tick summary — no
store walk at the stop edge — and `canonicalCaptureOutputAdmitted()` only checks the output
reserved at the record start.

Evidence:

- `test_record_stop_admits_through_the_common_drive` — the production admission, then the real
  grains driven to `RecordPublished` and retirement, asserting the raw/held-note/geometry receipts,
  one `RecordPassAdded` undo entry, once-only persistence and a drained queue.
- Mutation: removing `captureRecordGeometryReceipt` fails the fixture.
- Strict oracle 46/3 (the same three); full native 2 612 cases / 2 605 succeeded / 7 skipped;
  `teensy41-capture-serial` SUCCESS with RAM1 padding 1 016 (floor 840).
- RAM1: the held-note transfer's 160 B out-of-line `_Hashtable::swap` in ITCM was offset by moving
  the cold `SESSION_CAPTURE`-gated `PersistenceDiagnostics::maybeEmitPeriodic` to FLASH; the floor
  was not re-based.

**S17.2 — Route both stop entry points through the drive: done.** Both entry points moved together;
neither route admits while the other still commits inline. `stopRecording` and
`stopRecordingToStopped` now admit through `beginDeferredRecordStopForSlot` and keep their
destination behaviour at the stop edge, in `Track::completeAdmittedRecordStop` (TRACK_COLD_MEM):
origin alignment for the PLAYING route, the truncation rewind against the admitted final length,
the loop phase and projection cycle, `TRACK_STOPPED_RECORDING` → `TRACK_PLAYING`/`TRACK_STOPPED`,
and the record-stop playback handoff (`sendAllNotesOff`, `resetPlaybackState`,
`bindFirstRecordingBar`, `startPlaying`, `refreshViewportAfterRecordStop`). A denied stop is
*retained*: `latchAcceptedStop` records the obligation against the interval's own slot and
generation, the interval keeps recording with its rows and reservations intact, and
`driveAcceptedInteraction` admits it later — now dispatching on the retained request's capture
**mode** so a Record obligation runs the Record chain. The inline `prepareRecordStop` /
`commitCaptureForStop` Record route is gone from both entry points, and the pass, the
`RecordPassAdded` undo entry and the once-only persistence admission come from
`stepDeferredRecordPublication`.

The strict matrix is green:

```
S4F16_REQUIRE_CONTRACT=1 pio test -e native -f test_shared_capture_finalization
=> 45 cases, 45 succeeded   (R→R, R→OD, OD→R, OD→OD)
```

`pio test -e native`: 2 612 cases, 2 605 succeeded, 7 skipped. `pio run -e
teensy41-capture-serial`: SUCCESS, RAM1 padding 2 040 (floor 840).

Fixtures converted from the inline oracle (the deferred chain now produces their content):
`recordNormalizationInventory` (stopped/playing/minimum-length-disabled),
`test_record_normalization_preserves_take_larger_than_overdub_staging`,
`test_record_normalization_filtered_empty_resets_geometry`,
`test_record_stop_commits_exact_held_note_closures`, and in the working-set suite
`test_the_successor_take_commits_with_content_after_a_transport_stop`. The R→R/R→OD/OD→R/OD→OD
rotation oracle is no longer contract-gated: the deferred stop is the production shape on every
mode.

Mutations proven: reintroducing the inline `prepareRecordStop` + `commitCaptureForStop` Record
route fails 7 cases (the strict three plus the four content fixtures); dropping the stop-edge
`loopLengthTicks` stamp fails 8; reverting the tail-close policy to loop end fails the three
inventory cases; reverting the job-owned-geometry exemption fails the filtered-empty case.

**Defects found by the converted fixtures** (all fixed in this slice; each was invisible while the
inline route published synchronously):

1. `stepDeferredRecordNormalization` passed `geometry.openTailCloseTick = UINT32_MAX`, closing an
   open tail at loop end. `Loop::finalizeCaptureWrapWindowAtStop` closes it at the **stop
   playhead's loop phase** (falling back to `loopLengthTicks - 1` when that phase precedes the
   note's start), so the published row set differed. The Record job now passes `job.phaseTick`.
2. The deferred-grain prologue required `job.owner->loopLengthTicks == job.loopLengthTicks`. A
   Record job **owns** its geometry: an all-filtered take resets the live scalar to zero, and a
   successor take resets it too, so the equality parked a completed job at `RecordPublished`
   forever — holding its raw range, the capture-stop owner and its ring slot. The clause is now
   overdub-only; the owner/slot/loopId clauses still prove the interval was not replaced.
3. The S4f.19 continuity gate waited on `frozenWindowBuildActive()` unconditionally. That build is
   advanced only by the playback owner (`TrackPlaybackHotPath`), which never runs for a slot with
   no audible source, so an empty take deadlocked retirement. The wait now applies while the
   interval still has geometry.
4. The Record admission declared no preparation credits, so the job's declared service bound was
   the fixture's fixed slack. It now declares a bounded estimate (fixed MIDI-key scratch resets,
   row passes, chunk moves) read from O(1) scalars and consumes one credit per Record grain.

**S17.3 — Caller continuations: partially done (S17.3a landed; geometry result still open).**
`finalizeCaptureAndSelectSlot`'s Record branch now mirrors the overdub branch's slot-scoped rule.
An **admitted** Record job carries its own identity (`job.owner`, `job.slot`, `job.loopId`, the
interval latch, `captureStopOwner`) and does not hold the active slot, so the switch commits and a
different-slot successor is not serialized behind predecessor cleanup. An **accepted-but-unbound**
Record stop refuses the switch and leaves the active slot unchanged.

This closed a silent content-loss regression introduced by S17.2: the Record branch switched
unconditionally (correct only while the stop committed inline), which advanced `captureInterval_`
past the retained interval, after which `driveAcceptedInteraction` judged the obligation
superseded and dropped the take. Fixture
`test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one` covers the refusal, the
unchanged active slot, the retained obligation, the post-bind commit, the still-finalizing
predecessor and its committed pass; removing the guard fails it.

**Still owed (S17.3b):** `stopRecordingTrack`, `finalizeCaptureAndSelectSlot` and `stopAllTracks`
still read the stop-edge-stamped `loopLengthTicks` synchronously. The stamp is the *predicted*
length from the maintained maximum-tick summary, so an all-filtered take — whose published geometry
is zero — can still set `masterLoopLength`, and auto-align can still be computed from it. The
callers must consume a job-qualified geometry-ready result: the interval's geometry is final only
once its admitted job has published (or its retained obligation has been superseded/consumed), and
until then the manager must hold the master-length/auto-align obligation rather than latch it.

S17.4–S17.5 remain open.

## Fixed fixture requirements (from the trace)

Accepted/bound mode, slot/LoopId/interval, raw and held-note ownership, final geometry, unchanged
published generation, successor input, and exactly-once publication/undo/persistence. A failed
preflight moves no receipt. Repeated drive is not a press. Add stale replacement, empty input,
occupied canonical owner, runtime regeneration, and terminal wait/recovery cases using the real
owner and allocator/clock boundaries. Preserve MIDI content with independent expected rows. No
sleeps and no production pause hook. Existing S4f.16 evidence applies only to unchanged fixtures.

## Risks and open decisions

- The admission's overdub-only guards must be conditioned on mode **without** weakening the
  overdub path; each guard needs its own Record-side or mode-neutral proof.
- The transport-stop and retarget paths (`runtime->reset`, `retargetFrozenCommandEpoch`, the
  DEC-070 raw-only fallback) currently assume a job exists for overdub; Record jobs must be
  handled deliberately rather than by falling into the overdub fallback.
- `prepareRecordStop`'s inline scan removal must not change the stop-length policy; the oracle is
  the existing out-of-order/removal/truncation fixtures.
- Production Record admission is a cutover, not a flag: no checkpoint may leave one entry point
  deferred while a manager caller still assumes inline completion.

## Non-goals

No new owner, queue, schema, undo kind, or finalization capacity. No content-size walk at the stop
edge. No full-take staging copy. No device acceptance claim from host results.
