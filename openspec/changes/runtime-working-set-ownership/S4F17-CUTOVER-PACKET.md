# S4f.17–S4f.21 — Record admission and caller cutover (execution packet)

Status: **S17.1 landed 2026-09-29 (Record admission bind, no routing change); S17.2–S17.5 open.**
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
Current result: **46 cases, 3 failed, 43 succeeded** (OD→OD passes). One case was added by S17.1
(`test_record_stop_admits_through_the_common_drive`); the three failures are unchanged and remain
the oracle for this cutover:

- `test_record_to_record` — *"A must own a deferred job before any worker grain (record
  currently publishes inline)"*.
- `test_record_to_overdub` — the same assertion.
- `test_overdub_to_record` — B's deferred record job is absent.

## Current routing (VERIFIED)

- Two production Record stop entry points: `Track::stopRecording` and
  `Track::stopRecordingToStopped` (`src/Track/TrackCaptureStopCommit.cpp:384` and `:539`). Both
  call `prepareRecordStop` (an inline scan: last-event lookup, pending-note finalization, store
  remap) and then `commitCaptureForStop` (inline `Loop::commitCapturePass` → publication, undo,
  persistence) before advancing state and binding the first-bar window.
- `driveAcceptedInteraction` admits only through `beginDeferredOverdubStopForSlot`
  (`src/Track/TrackOverdubLifecycle.cpp:514`), which requires `isOverdubbing()` and then a chain
  of **overdub-only** resources: live capture staging, `hasActiveOverdubTransferAdmission()`,
  `canonicalCaptureOutputAdmitted()`, `canHandoffSessionExitSnapshot()`, causal base and ranked
  preparation budgets. Record input does not populate those.
- The deferred worker and the whole Record chain **already exist and are verified** (§1): closure,
  bounded source staging, normalization, transfer, atomic publication with job-owned geometry,
  retirement, and the empty-take terminal. They are currently reachable only through the
  controlled-bind production-worker fixture.
- The manager continuations read length synchronously after the void stop:
  `TrackManager::stopRecordingTrack`, `finalizeCaptureAndSelectSlot`, `stopAllTracks`.
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

S17.2–S17.5 remain open. **Do not route one stop entry point without the other**: S17.2 must move
`stopRecording` and `stopRecordingToStopped` together, and the strict failures stay red until then.

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
