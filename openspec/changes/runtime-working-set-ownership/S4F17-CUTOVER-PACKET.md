# S4f.17–S4f.21 — Record admission and caller cutover (execution packet)

Status: **S17.1–S17.5 landed 2026-09-29; host-level evidence only, no device run.**
The production Record stop is a deferred job on every mode: admission binds the raw range, geometry
and held-note receipts; both stop entry points run a bounded transport edge; a retained stop is
driven by the common accepted-stop dispatch; the switch, the manager continuations and the
transport-stop/retarget path all consume or supersede the job correctly; the S4f.20 publication
residuals are closed; and the strict R→R/R→OD/OD→R/OD→OD matrix is an **enforced** gate. §3's exit
list has no unexercised item this environment can reach (see S17.5a–c). The remaining verification
is the change's device leg, which is **S4f.22** and is not claimed here.
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

**S17.3a — the switch guard: done.** `finalizeCaptureAndSelectSlot`'s Record branch now mirrors the
overdub branch's slot-scoped rule.
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

**S17.3b — job-qualified geometry result: done.** `Track::recordStopGeometryForSlot(slot,
lengthOut)` is the qualified read: `Final` means no stop is in flight for that slot, so
`lengthOut` is the authoritative committed length (zero for a take that published nothing);
`PendingStop` means an admitted job or a retained obligation still owns the slot. It is
deliberately **mode-neutral** (it reports the slot's stop obligation, not the capture mode) and
slot-scoped (`hasStopInFlightForSlot`), so another interval's finalization cannot make this one
look undecided.

`stopRecordingTrack`, `finalizeCaptureAndSelectSlot`'s Record branch and `handleTransportStop`
(the transport-stop continuation; there is no separate `stopAllTracks` symbol) now adopt the
geometry **once**, only when it is final. Until then they retain a per-track obligation
(`pendingRecordGeometryMask` + `pendingRecordGeometrySlot`) that
`servicePendingRecordStopGeometry` consumes at the next dispatch boundary, and auto-align is
applied through the new slot-qualified `Track::setLoopLengthForSlot` because by then the
obligation's slot may no longer be the active one.

The fix is observable, not cosmetic: the stop edge stamps the *predicted* length (one quantized
bar for a sub-bar take), so the old synchronous read adopted a `masterLoopLength` from a take the
minimum-length policy then deleted, and auto-aligned the emptied interval with it. Fixtures:
`test_record_stop_adopts_published_geometry_not_the_stop_edge_prediction` (nothing adopted at the
stop edge; the published 768 adopted exactly once after the worker retires) and
`test_filtered_record_take_publishes_no_master_length` (a 20-tick pair removed by the policy
adopts nothing). Reverting the transport continuation to the stop-edge read fails both, the second
with the exact defect message.

Coverage boundary: this suite keeps `Track::startPlaying` fail-closed (the play branch is out of
scope), so the *PLAYING*-destination `stopRecordingTrack` wrapper is not driven there; it calls the
same two helpers, and the STOPPED-destination transport-stop route is the one exercised end to end.

RAM1: 32 B of DTCM for the obligation (9 B of state at the section granularity). The same slice
moved `stopRecordingTrack`, `retainRecordStopGeometryObligation` and
`servicePendingRecordStopGeometry` to FLASH, so ITCM code fell 391,240 → 391,224 and padding rose
1,976 → 1,992 — the trade is 32 B of DTCM for ITCM headroom. `scripts/ram1_budget.json` was
re-based deliberately (`ram1_variables` 96,160 → 96,192, `locals_free` 34,912 → 34,880) with the
reason recorded here and in the file's note; `itcm_bytes` (12 banks) and `padding_floor` (840) are
unchanged.

**S17.4 — S4f.20 residuals: done.**

*Deferred REVT validation.* The deferred Record publication now queues the stored note-on scan the
inline `finalizeCommitSideEffects` queued on commit. The queue was also made **slot-qualified**:
`deferredRecordRevtsPending` (a bool) became `deferredRecordRevtsSlot` (the same byte, holding the
interval's slot or `Config::INVALID_LOOP_SLOT`), `queueDeferredRecordRevts(slot)` names the
publishing interval, and `processDeferredRecordRevts` scans *that* loop rather than
`getActiveLoop()`. Before this the side effect was missing entirely on the deferred route, and an
active-slot read would have scanned a successor take (or found nothing to scan).

*`invalidateCaches()` parity.* The atomic handoff calls `Loop::notifyCommittedContentChanged`,
which is `markPassDerivedStale` — `playbackOrderDirty`, `visualCacheDirty` and
`invalidatePlaybackCaches` (which itself covers `noteCache` and `eventIndexValid`). That is a
superset of `Loop::invalidateCaches()` for the job's own loop, so the remaining gap was the
**Track-level** work the inline `Track::invalidateCaches()` did: the committed-MIDI scratch
(`CommittedMidiScratch::invalidate`), the note-edit selected-slot invalidation, and the REVT queue.
`Track::invalidateCommittedContentForSlot(slot, queueRecordRevts)` now applies exactly those,
against the job's slot, and `invalidateLoopDerivedCaches()` delegates to the same slot-qualified
body with the active index.

Fixtures: `test_deferred_record_publication_queues_revts_for_its_own_slot` (queued for the
publishing slot; a successor take does not redirect it; the scan runs to completion) and
`test_deferred_record_publication_invalidates_the_track_committed_scratch` (the scratch is stamped
fresh, then the publication invalidates it — the `CommittedMidiScratch::isFresh` consumer contract,
not an assumption). Removing either wiring fails its fixture.

**S17.5a — the transport-stop / retarget path for a Record job: done.** The packet's risk list
named this: *"The transport-stop and retarget paths (`runtime->reset`, `retargetFrozenCommandEpoch`,
the DEC-070 raw-only fallback) currently assume a job exists for overdub; Record jobs must be
handled deliberately rather than by falling into the overdub fallback."* They now are.

`requestDeferredOverdubStopToStopped` is mode-aware: for a **Record** job the destination change
(`requestTransportStop`) is the whole retarget, because the Record chain owns none of the companion,
canonical-tail or undo-collection roles the overdub recovery manipulates — falling through would
call `restoreSupersededPreparedCompanionHistory` / `discardPreparedCompanionHistory` and
`retargetStoppedRecovery` against roles a Record job never populated, and would arm the overdub
grain's `CanonicalTail..CompanionBridge` stall fallback for phases a Record job never enters.

"Transport stop wins" is then resolved at that edge, once, for the Record job and only while the
interval still owns the live transport: `silenceTrackMidiOutput` + `TRACK_STOPPED` +
`resetPlaybackState`. Before this, a transport stop during a Record finalization left the track in
`TRACK_PLAYING` while the job's destination said STOPPED — `handleTransportStop`'s `isPlaying()`
branch is unreachable once the pending-job branch has taken the turn, and a Record job has no
deferred cleanup to resolve it.

It is deliberately **not** in the playback-runtime reset path: `LoopPlaybackRuntime::reset` also
calls `requestTransportStop()` to retarget a job whose window it is about to discard, and that is
not a user transport stop — the interval's retained window is still the sounding source (S4f.19),
so that path keeps the transport state. The distinction is fixture-visible:
`test_record_normalization_playing_inventory` (a runtime reset during the drive) still ends PLAYING,
while `test_transport_retarget_of_a_record_job_resolves_stopped` ends STOPPED and still commits the
take. Removing the resolution fails the latter.

**S17.5 — Gates, mutations and accounting: done for the native leg.**

*Enforced gate.* The finalization suite no longer reads `S4F16_REQUIRE_CONTRACT`; every assertion,
including the R→R/R→OD/OD→R/OD→OD rotation oracle, is unconditional. Ordinary suite success *is* the
strict result, and the suite header says so and says why not to reintroduce a conditional. Running
with and without the env var is now the same binary behaviour.

*Mutation set* (each red-proven in this change):

| Mutation | Fails |
| --- | --- |
| Reintroduce the inline `prepareRecordStop` + `commitCaptureForStop` Record route | 7 cases (the strict three plus four content fixtures) |
| Drop the stop-edge `loopLengthTicks` stamp | 8 cases |
| Close open tails at loop end instead of the stop playhead's phase | the three normalization inventories |
| Require the live `loopLengthTicks` to equal the job's for a Record job (the old prologue clause) | the filtered-empty geometry case |
| Retire while a frozen-window build still borrows the raw range | `test_published_record_interval_does_not_replay_the_return_window` |
| Switch away from an accepted-but-unbound Record stop | `test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one` |
| Read the stop-edge prediction instead of the published geometry | both S17.3b geometry fixtures |
| Skip the deferred REVT queue | `test_deferred_record_publication_queues_revts_for_its_own_slot` |
| Skip the Track-level committed-scratch invalidation | `test_deferred_record_publication_invalidates_the_track_committed_scratch` |

*Accounting.* `scripts/check_ram1_budget.py` runs post-link on every firmware build:
`variables 96,192 / ITCM 393,216 (code 391,224 + padding 1,992) / locals 34,880`, with
`padding_floor 840` and `itcm_bytes` at 12 banks. The structural pins are compile-time and active
in this configuration (`SESSION_CAPTURE=1`, `PERF_TELEMETRY=1`): `sizeof(Track) == 384` (the
eight-way DTCM array) and `sizeof(CaptureStopJobs) == 28936` (the externally allocated,
PSRAM-resident ring carrying the per-job Record receipts and the per-track preparation role).
Net across the cutover: ITCM is 16 B *below* the approved baseline (391,224 vs 391,240) and RAM1
variables are 32 B above it (the S17.3b geometry obligation), re-based deliberately in
`scripts/ram1_budget.json` with the reason recorded there and in this packet.

*Verification line.* Strict matrix 47/47; `pio test -e native` 2 617 cases / 2 610 succeeded /
7 skipped; `pio run -e teensy41-capture-serial` SUCCESS.

*§3 exit breadth — what is covered and what is not.* Transport Stop is covered across the deferred
stop's phases by existing fixtures plus this change: `test_continuous_capture` carries
`test_transport_stop_during_canonical_copy_preserves_accepted_rows`,
`test_transport_stop_retarget_retires_a_midflight_playing_job`,
`test_transport_stop_while_successor_is_live_keeps_its_rows_and_root`,
`test_b03_transport_stop_retargets_an_admitted_playing_stop_without_losing_it` and
`test_s4f10c_accept_and_latch_then_transport_stop_supersedes_before_the_drive`; the Record leg adds
S17.5a's retarget fixture. Clock-source *classification* is covered by
`test_clock_transport_master` (internal master, external active, external timed out).

**S17.5b — load/clear underneath a finalizing stop: done.** Two real defects, both found by
writing the fixtures the packet's exit list asked for.

*An admitted job whose interval identity is gone used to park forever.* The deferred-grain prologue
returned early when `loopIdForSlot(job.slot) != job.loopId`, so a slot load or re-record adopting a
new LoopId over a finalizing take held the job, its owned raw range, its capture-stop owner and its
ring entry indefinitely — no later stop on that track could ever be admitted. S4f.4 transition 3
already says such a request is **superseded** — reported and retired — and `driveAcceptedInteraction`
applies that to a request that never bound; `supersedeDeferredStopJob` now applies it to one that
did. It releases everything the job owns (frozen window, staging role, note-change and preview
borrows, owned pass chunks, private canonical output, and the sealed extent one grain per turn) and
retires the ring entry **without** touching the live interval, so a superseded finalization can
never resurrect or mutate a successor's content. A latch needs nothing here: the replacement fails
its own identity check and is consumed.

*Clearing the slot could receive the in-flight take back.* `Track::clear()` destroys the interval's
ledger, but a job that still owned an unpublished take for that slot would go on to publish it into
the slot the user had just emptied. `clear()` now supersedes that job (its one-time ownership
release runs on the user's action, the bounded extent release and ring advance finish on the
deferred owner's turn via the grain's `mode == None` resume path). `Track::clear()` deliberately
does **not** bump the LoopId — identity preservation is existing behaviour with its own consumers —
so this is an explicit supersede, not a side effect.

Fixtures: `test_replaced_loop_during_a_record_finalization_retires_the_job` (LoopId adoption — the
load/re-record shape; fails by parking without the fix) and
`test_slot_clear_during_a_record_finalization_does_not_republish` (fails with *"a cleared slot must
not receive the in-flight take"* without the clear supersede). The overdub counterpart is
`deferredPersistence(stale=true)`, renamed to
`test_stale_publication_is_superseded_without_persistence`: it previously encoded parking as the
expected behaviour, and now asserts the supersede-and-publish-nothing contract instead.

**S17.5c — the transport-not-running leg of the retained stop: done.** The clock source reaches
this path through `isTransportRunning()`, which is false both for a stopped internal transport and
for an external clock that has gone quiet. `test_retained_record_stop_admits_stopped_once_the_transport_is_not_running`
retains a **PLAYING**-destination Record stop behind another interval's job, stops the transport
before the drive runs, and asserts the DEC-078 item 3 re-derivation: the stop admits for STOPPED,
the take still publishes, and the transport resolves `TRACK_STOPPED`. Replacing the re-derivation
with the stored destination fails it. (The transport-state assert is the discriminator: the PLAYING
edge's own runtime reset can also retarget the destination field, which S17.5a pins separately.)

With that, §3's exit list has no unexercised item left that this environment can reach: the
external-clock *classification* itself is covered by `test_clock_transport_master`, and every route
the Record stop takes is now fixture-covered. No device acceptance is claimed from host results.

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

## Device leg (S4f.22) — first attempt, 2026-09-29

Run identity: `git_commit 006b67c3`, `build_env teensy41-capture-serial`,
`firmware_hex_sha256 8aa70cd9aab9272c05c8ccf7ecb5bbea59d3350f2bfeff171ea71979c3b2ed9e`
(captured by the harness in `captures/s4f22-base-device-01/metadata.json`, so the
evidence is pinned to the build). Target: the authorized scratch slot, track 5 /
slot 1 (internal track index 4, slot 0). Entry point: the authoritative baseline
preset, `host_midi_hitl.py run --preset base`.

**Verdict: `INCONCLUSIVE_CAPTURE` (`SERIAL_INCOMPLETE`) — not a pass, and not a
firmware contradiction.** `firmware_contradiction: false`,
`firmware_assertions_pass: true`, `stimulus_valid: true`,
`capture_integrity: COMPLETE`, `setup_transitions_confirmed: false`,
`capture_complete: false`.

**What the device did establish (positive evidence for this change).** Boot banner
present (`DIAG,boot_reset,crash=0`), no `RING,overflow`, no crash. Record started
and stopped (`Recording started @ tick 362`, `Recording stopped @ tick 2377
recStart=362 length=2304`). The overdub started and its **deferred stop chain ran
on device**: `[TRACK] Reentry window open slot=0` at the overdub-stop button
release, then `Reentry released slot=0 generation=48` 346 ms later — i.e. the
accepted stop bound, the canonical capture owner retired and the reentry capacity
was released. Memory stayed flat through it (≈5.1 MB external live, 370–372 free
chunks, heap 160 KB). That is the S4f.17/S4f.21 admission-and-retirement path
working on hardware.

**What it did not establish.** The run aborted with *"second overdub transition
not confirmed"*; the second overdub press never reached the device (the log's last
button event is `MIDI Button A: Stop Overdub`), so **content, undo and persistence
parity are not verified on device**. Treat the device gate as still open.

**Two open observations, recorded rather than resolved.**

1. *No record-family rows in the capture.* The session log contains `ST`, `PERS`,
   `SEVT`, `VCACHE`, `MO`, `MI`, `DIAG` and `RRQ` families but **no `RECA`,
   `RECS`, `ODUB` or `REVT` rows at all**, while `capture_integrity` reports
   `COMPLETE`. The harness therefore reports `record_stop_stage_missing:record_stop`,
   `record_first_note_missing` and `record_stored_revt_missing`. Two candidates,
   not yet separated: (a) the capture ring's documented Tier-A displacement shed
   them (`RECS,` is Tier-A and may be displaced by newer Tier-A rows — the same
   mechanism that once dropped `RID,`), or (b) the record-stop emission path this
   change rewrote no longer emits them. The separating test is a **short
   record-only run** that greps the fresh log for `RECA`/`RECS`; until that is run,
   neither explanation may be assumed.
2. *Record length 2304 where the harness expected 1536.* The take quantized to
   three bars, not two. Consistent with a late stop press (the length policy
   quantizes the raw stop tick), but not yet attributed; the harness's own model
   for this preset assumes two bars.

Neither observation is evidence against the deferred Record stop: the stop
admitted, retired and released its capacity on device. They are evidence that the
**oracle** needs a short-run visibility check before the device gate can be read.

### Device leg — second probe (visibility), 2026-09-29

A short record-leg probe (`run --preset base` with `--second-overdub-bars 0
--no-stop-after-overdub --no-undo-redo-after-overdub-stop`, same scratch slot)
after a **fresh upload**, `captures/s4f22-recvis-02`. This is *not* a gate run —
it deliberately changes the registered stimulus's args, so
`setup_transitions_confirmed` is false by construction — it exists only to
separate the two open observations. It does.

**Both observations resolved, neither was a firmware defect.**

1. *The record family is emitted and visible.* The fresh short log carries
   `RECA` ×1, `RECS` ×7 and `RID` ×1, including
   `RECS,stopToStopped,0,379,0,379,768,0` and every stage row this change added:
   `RECS,stage,record_stop,...,entered`, `…,finalize,…,deferred`,
   `…,visual_cache_request,…,deferred`, `…,revt_queue,…,deferred`,
   `…,state_advance,…,ok`. The first run's absence was therefore **capture
   visibility** — the documented Tier-A ring displacement over a long, noisy run
   (`DIAG,reentry`/`DIAG,canonical_release`/`timing_max`/`midisvc`/`tracks` are all
   Tier-A and flood during an overdub finalization) — not an emission regression.
   A second, invalid probe is also recorded: re-running **without** re-uploading
   produced a log with no `#CAP` framing at all and
   `INCONCLUSIVE_CAPTURE (SPARSE_CAPTURE)`. That is the skill's documented
   "device did not reset" failure and it must be re-uploaded before any content
   run.
2. *Record length.* The valid short run stopped at tick 379 with `length=768` for
   its own one-bar configuration, and the earlier valid run's second attempt
   reported `length=1536` (two bars). The first attempt's 2304 was a late stop
   press, not a length-policy change.

**New oracle gap found (belongs to the recipe correction, §4).** With the record
family visible, the harness's next missing stage is
`record_stop_stage_missing:seal`. The inline path emitted a `seal` stage from
`Loop::commitCapturePass`; the deferred route has no such stage — its commit
happens in the worker's publication and the edge reports
`finalize,…,deferred`. The canonical oracle still looks for `seal`, so a valid
long run would report the same miss even though the stop published correctly. That
is a **recipe/oracle reconciliation**, not a firmware defect, and it must land
before the record leg can be read as a pass.

`record_first_note_missing` and `record_stored_revt_missing` are also consistent
with the deferred route: the REVT queue is served only when the transport is fully
idle (`processDeferredRecordRevts`' guard), which a continuous-transport
overlap run never reaches. Their expectation likewise needs reconciling.

### Oracle reconciliation and the third run, 2026-09-29

**Landed: the stop-stage oracle is now route-aware.**
`scripts/hitl/legacy_record_baseline.py` required a fixed stage set
(`record_stop, seal, publish, finalize, …`). The deferred route has no `seal` and
no `publish` — its commit is the worker's publication, evidenced by the
`RecordPassAdded` undo entry and the loop's `PERS` rows, which the oracle already
asserts separately — so the record leg reported `record_stop_stage_missing:seal`
on every run while the stop published correctly.

The required sequence is now selected from the edge's own evidence: a `finalize`
row carrying the `deferred` outcome selects
`record_stop, finalize, visual_cache_request, revt_queue, state_advance,
save_request`; anything else keeps the inline sequence unchanged. This is
route-dependent, **not** a blanket tolerance — without a `finalize` row the inline
requirement stands, so a run that never handed the commit off still fails. Three
host tests cover it (inline unchanged; deferred accepted; missing-`finalize` still
reports `seal`), 51/51 in `scripts/test_capture_transitions.py`.

**Third device run** (`captures/s4f22-base-device-02`, same pinning): the
`record_stop_stage_missing:seal` issue is **gone** — the reconciliation reads the
deferred route correctly on hardware. The run aborted for a different, earlier
reason: *"record stop did not reach PLAYING"*, with the device log showing
`Track 4 armed, waiting for clock to start recording` — **the transport was not
running when the fixture's record press was sent**, so the press armed the track
and the take only started once the transport did
(`Recording started @ tick 0`, `Recording stopped (to STOPPED) @ tick 378
length=768`), one bar instead of the configured two.

The same signature explains the earlier length mismatch: press/transport pacing
decides which bar the stop press lands in, and the stop-length policy then
quantizes to the nearest bar with a half-bar grace (raw 2015 → 2304; raw 379 →
768). So `record_loop_length_mismatch` is not evidence about the firmware — the
oracle's `expected_final_length` assumes the press landed in the configured bar,
which a wall-clock-paced fixture cannot guarantee.

**Two recipe items are therefore outstanding, both in the fixture/oracle, neither
a firmware defect:** (1) pace the transport start and the record press so the
transport is verifiably running before the press (the oracle already has the
receipts — `ST,Track,STOPPED,ARMED` and `DIAG,clk` — to gate on rather than wait);
(2) make the length expectation prove the firmware's own policy — the committed
length equals the documented quantization of the observed raw length — instead of
assuming the press landed in the configured bar. Both belong to the HITL recipe
correction; until they land, the S4f.22 device gate cannot be read as a pass, and
no device acceptance is claimed.

### HITL fixes and runs 05–06, 2026-09-29

Three fixture/oracle defects were fixed to make the record leg readable, and two of
them are verified on device.

**Fixed — transport toggle race (scoped).** `ensure_midi_clock`'s press is a
*toggle*, and this board emits no USB MIDI clock out, so its pulse wait always
expires and the press always fires — pressing while the transport already ran
stopped it (run 03: start 25.086, toggle 25.603, `Recording stopped (to STOPPED)`,
"record stop did not reach PLAYING"). `ensure_transport_clock` now takes
`started_since_index`; a caller that is confirming a start press it has just made
passes the serial length captured *before* that press, and only that press's own
`Transport started` acknowledgement may suppress the toggle. A stale start earlier
in the run cannot, which is what over-corrected a first attempt into
`SETUP_UNCONFIRMED` with no recording at all (run 04) — that attempt was reverted.

**Fixed — receipt-anchored record stream** (`d8375b1c`): the record pass now paces
its notes and stop from the firmware's `RECA` tick rather than a modelled
transport-start wall clock, so the take is no longer outrun by its own stream.

**Fixed — the record family is now `isCriticalLine`.** `RECA,`/`RECS,` join `RID,`:
they are the only evidence of the record stop and are bounded per take. This did
**not** restore them (see below), but the policy is right on its own terms.

**Run 05** (all three fixes): the record leg ran correctly on device —
`Recording started @ tick 378`, `Recording stopped @ tick 2392 (recStart=378
length=2304)`, `Overdubbing started @ tick 2812`, then `Reentry window open slot=0`
→ `Reentry released slot=0 generation=48`. No `record_loop_length_mismatch`.

**Run 06** (after making the record family critical): the first overdub's stored
content now verifies — `count_ok=True span_ok=True bars_ok=True` — and the second's
`count_ok/bars_ok` pass. The run still aborts on *"second overdub transition not
confirmed"*.

**Open, and the reason the gate is still unreadable.** Both long runs carry **no
`RECA`, `RECS` or `RID` rows at all** while the short run (`recvis-02`) carried all
three. `RID,` was already critical before this change, so its loss **rules out ring
displacement as the sole mechanism** — the record family is being dropped somewhere
else (the flush-budget path, or the emitter's own drop policy), not merely evicted
from the ring. Making `RECS` critical therefore changed nothing observable. That is
the next thing to find: instrument the enqueue path for `RECA`/`RECS` and compare a
short and a long run, rather than reasoning about tiers.

### Record-family loss: pinned to the firmware identity, mechanism narrowed

> Historical hypotheses below are superseded by the 2026-09-29 Record evidence
> loss accounting discovery at the end of this packet. Firmware identity is useful
> evidence, but the claimed elimination of loss paths and attribution to critical
> reentry/release traffic were not established.

Run identity matters here and is now unambiguous (from each run's `metadata.json`):

| run | git | firmware sha256 (first 16) |
| --- | --- | --- |
| 01–03 | `006b67c3` | `8aa70cd9aab9272c` |
| 04–05 | `760acb6f` / `d8375b1c` (dirty) | `8aa70cd9aab9272c` (host-only fixes) |
| 06 | `d8375b1c` (dirty) | `62599057cd79c11f` (the `isCriticalLine` build) |

So run 06 **did** run with `RECA,`/`RECS,` in the critical set, and it still carried zero
`RECA`/`RECS`/`RID` rows. **Making the record family critical changed nothing observable**, and
that rules out ring *eviction* as the mechanism: a critical row is not a displacement candidate.

Two drop points exist in the ring, and the evidence now points at the second:

1. **Enqueue displacement** (`incomingMayDisplace`) — evicts the head when the incoming record may
   displace it. An identity row lost here is counted (`identityEvicted` / `identityEmitRejected`).
2. **Refused append** (`if (!incomingMayDisplace(...)) { noteTimingShed(); return false; }`) — the
   incoming record is **dropped outright**. Run 06's log contains exactly two `RING,shed,1` rows
   (16.424 s and 30.142 s), and `noteTimingShed()` is emitted *only* on that refused-append branch.
   So appends were refused twice in that run, and a record family emitted inside such a window is
   lost with no eviction involved.

That is consistent with `RID` (critical, and already protected before this change) being lost too,
and with the short run keeping the whole family: a short run never reaches the pressure that
refuses appends.

**Run 07 rules out the flush path too.** With `RECA,` added to `isTierALine` (run identity:
`git 7351c41c`-era tree, firmware rebuilt) the record family is *still* absent — `RECA=0 RECS=0
RID=0` over 5,393 lines. So the timing-critical flush (which sheds while the head is *not* Tier-A)
is not the cause either.

That leaves exactly one mechanism consistent with all the evidence:
`incomingMayDisplace(true, true, headTierA, headCritical)` returns **true even when the head is
critical**, so a critical record row is enqueued and later **evicted by the next critical row**.
The run's critical traffic is dominated by the fixture's own `DIAG,reentry*`,
`DIAG,canonical_release,` and `DIAG,ws_release,` rows during the overdub finalization, so the
handful of record rows emitted at the stop edge are pushed out again within the same second. The
two `RING,shed` rows are a *separate* symptom (non-critical appends refused) and not the loss.

The fix is therefore in the eviction class, not the tier: make the one-shot record rows
(`RECA,`/`RECS,`/`RID,`) **unevictable** — refuse an append rather than displace one — and report
that refusal in `capture_integrity`. The wedge risk is bounded here (a handful of rows per take),
but it must be bounded *per session* before landing, which is why this is recorded rather than
guessed at.

**Next step, concrete:** instrument the enqueue path with per-family emitted/enqueued/refused
counters (the `identityEmitEnqueued`/`identityEmitRejected` pair already models this for `RID`) and
either (a) reserve ring capacity for the critical record rows, or (b) report the refusal in
`capture_integrity` so the oracle can say "lost under pressure" instead of
`record_stop_stage_missing:record_stop`. Option (b) is the smaller change and keeps the failure
attributable; option (a) is the one that makes the record leg readable on a long run.

Not claimed: any device acceptance. The gate still aborts on the second overdub transition, and the
record leg's own rows are still lost under pressure in a long run.

### S4f.22 — Record evidence loss accounting (2026-09-29)

Status: specified; baseline `cb2fa7e4`, clean tree. This slice extends the existing
DebugSessionCapture owner and CaptureLineClock integrity consumer. No retention-policy
change or device acceptance is implied.

**G0 discovery / historical review.** VERIFIED: `recStart`, `recStop`,
`recStopStage`, and `recSlotIdentity` enqueue text through `appendCaptureRecord`;
the fixed 1 MiB ring is drained only under a safe serial budget. `RECA`, `RECS`,
and `RID` are critical and Tier-A. Contrary to the previous paragraph,
`DIAG,reentry*`, `canonical_release`, and `ws_release` are Tier-A **but not
critical** and cannot evict a critical head. Critical-to-critical eviction is
possible, but its attribution to that diagnostic traffic is disproven by
`CaptureLineTier::isCriticalLine`. The device loss mechanism remains UNKNOWN
until entry/enqueue/reject/evict/transmit counts are observed. Existing RID counters
are the extension point; no new owner, store, transport, persistence or undo path.
DECISION_LOG searches: capture ring, Tier-A, telemetry, instrumentation; preserve
the existing capture-only instrumentation and no USB fallback on append failure.
G0 PASS for accounting; not approval of an unevictable-row policy.

**Contract and G2 review.** Count RECA and RECS emitter entries and each ring
outcome independently, alongside the existing RID counters. Counters have boot
lifetime, fixed storage and saturating increments for the new families. Read them
through the existing stopped-only `!CAP_IDENTITY_STATS` command, directly over
serial so ring pressure cannot erase the answer. Keep the legacy RID stats row.
Any refused critical append (including allocation failure) must latch a separate
`RING,critical_rejected` notice until safe USB capacity exists; host capture
integrity becomes INCOMPLETE and stays so after later healthy rows. Ordinary
diagnostic rejection remains timing shed. Do not block/flush USB on append.

COVERED: overlapping tracks share the existing ring; source/job ownership, slot
switch, undo, load/save, geometry and playback are unchanged. Counters add only
fixed arithmetic and bounded tag tests; no new allocation or scan. TIGHTENING:
new stats must also count allocation failure, and notices must survive serial
backpressure; real-ring native fixtures are required, not just a tier truth table.
Resource allowance: 40 bytes for two five-counter families plus two 32-bit
critical-rejection notice fields (48 bytes before section alignment); no PSRAM
increase, Track/job growth or new playback role. Measure linker result against
the unchanged 840-byte padding floor. G2 PASS with these requirements.

**Transition oracle, fixed before edits.**
- R1: emit each family into a controlled ring, drain, assert entry/enqueue/transmit
  conservation and emitted text; existing RID wire fields remain compatible.
- R2: place RECA at the head, saturate with Tier-A diagnostics: the critical head
  remains and RECA eviction stays zero; saturate with actual critical rows and
  assert eviction attribution.
- R3: force ring allocation failure: entry=1, rejected=1, enqueued=0; no immediate
  USB write; safe flush emits critical rejection even without an allocated ring.
- R4: USB backpressure retains that notice; retry emits it once. Critical vs
  ordinary rejection must not be conflated. Host marks critical rejection
  incomplete, including through the file-follow collector, and never recovers
  integrity from a later healthy row.

**Verification recipe.** Native fixture includes the production capture TU with
only Arduino/serial hardware seams; supply fixed ring backing in the fixture.
Focused native ring/tier suites and Python capture-integrity suite, full native,
`pio run -e teensy41-capture-serial`, OpenSpec validation. Reuse the registered
baseline stimulus for eventual device comparison. No SD mutation or upload in this
accounting slice. Device mechanism and S4f.22 acceptance remain open.

**Completion record (2026-09-29).** Native verified, not device verified.
- `pio test -e native -f test_capture_ring`: 4/4. It links the actual
  DebugSessionCapture TU; only serial/Arduino are substituted, fixed ring bytes
  are fixture-owned. Both record families and existing RID are exercised.
- `pio test -e native`: 2,625 cases, 2,618 succeeded, 7 skipped, no failures.
- `.venv/bin/python -m unittest scripts/test_hitl_capture_integrity.py`: 16/16.
- `.venv/bin/python -m unittest scripts/test_hitl_command_forwarding.py scripts/test_hitl_identity_one_session.py`: 40/40.
- Mutation in an isolated `/tmp` source copy (working source unchanged): removing
  the critical-refusal latch yields 1/4 failures, expected 2 notices vs observed 0.
- `pio run -e teensy41-capture-serial`: PASS. Linker reports variables 96,192 /
  ITCM 393,216 (code 391,224 + padding 1,992) / locals 34,880. Whole-link totals
  remain at the approved baseline despite the 48-byte ring-state addition; no
  budget rebase, view/job field, or additional PSRAM allocation. ELF symbol size
  for `sCaptureRing` is 120 bytes on ARM.
- `openspec validate runtime-working-set-ownership --strict`: PASS.
- Stimulus inventory inspected; reuse `STIM-BASE-2-2-01@v1` for eventual device
  comparison. Device test NOT RUN; no firmware upload or SD operation.

Logs for this execution are `/tmp/capture-ring-test.log`,
`/tmp/capture-full-native.log`, `/tmp/capture-integrity-test.log`,
`/tmp/capture-related-host.log`, `/tmp/capture-mutation.log`, and
`/tmp/capture-firmware-build.log`; those are ephemeral execution logs, not device
acceptance artifacts. Production source at this checkpoint is the working diff
on `cb2fa7e4`. Oracle unchanged. Runtime docs, guide, bounds inventory and task
notes updated. No new ownership/architecture decision or exclusion: the existing
packet already required per-family accounting before a retention change. No task
checkbox changed; S4f.22 remains open. Next device evidence must use before/after
boot-lifetime counter snapshots, drain queued rows, and reconcile counters with
observed rows before selecting a retention fix.

### S4f.22 — same-session Record evidence drain (2026-09-29)

Specified before host edits. Device run `s4f22-base-device-08` (reported firmware
hash prefix `d126bb86fb3c9553`) reports RID 1/1/0/1/0, RECA 1/1/0/1/0,
RECS 7/7/0/0/0 for entered/enqueued/rejected/transmitted/evicted. This supports
queued-but-undrained RECS. Transmitted means the firmware called Serial.println;
it does not prove receipt by this managed log. Earlier eviction hypotheses are
not supported for this boot. Follow-up reconnect/drain is inconclusive because
it may reset the source ring.

Trace: the baseline closes its MIDI contexts, then waits for host-side quiet and
snapshots/stops its collector; ManagedCaptureSession.stop later kills the serial
owner. No final !TRACE_DRAIN is issued. Quiet is not an empty-ring receipt.

Host-only contract: after the measured leg, before either MIDI or capture reader
closes, send one idempotent MIDI Stop (never a transport toggle), obtain the
existing stopped-only fenced stats response, then perform bounded fenced drains.
Wait for the selected track's stop queue to settle using !STOP_STATUS; reconcile
all three families' emitted/enqueued/transmitted/lost counts and the actual rows
in this same managed log. Missing fences, incomplete stats, saturated counters,
loss or missing transmitted rows fail closed. Do not infer completion from quiet.
This is Record evidence retrieval, not a new persistence-completion assertion.
The original leg failure remains the primary abort reason. No measured-sequence
Stop, reconnect, firmware change, SD clear or acceptance checkoff is authorized
by this slice. The existing owner/serial protocol parser are extended.

Tests must prove: Stop before drain, repeated 64-row drains, delayed finalizer,
actual file receipt, missing gate response, stats loss, counter regression,
exception cleanup before port close, and non-managed no-op. Preserve the single
serial owner and outstanding command exclusion. Deadline bounds the entire
retrieval; no arbitrary settle sleep is evidence. Full native rerun requested by
user; host fixture tests and strict OpenSpec validation required. Device rerun
remains separate.

**Completion record — host implementation verified.** `ManagedCaptureSession`
owns the bounded retrieval; `serial.protocol` owns stats/status/receipt parsing;
the baseline's inner cleanup context runs before the MIDI contexts exit, including
on leg exceptions. Final verification re-reads the original capture file after
collector shutdown, accounting for follower lag and late integrity notices.
The report preserves both the original abort reason and the retrieval result.

Accounting tightening from review: firmware counters span a boot, so compare
new transmissions to new log rows from the first stopped stats reply, using the
exact fence endpoint of each reply (not a later moving file tail). This supports
an attached run with prior transmissions and catches asynchronous post-fence
flushes without arbitrary sleeps. Earlier leg rows remain the baseline verifier's
responsibility. Boot-lifetime loss still fails conservatively when no pre-leg
snapshot can attribute it; this slice does not erase loss or certify persistence.

- Full native rerun requested by user: `pio test -e native` — 2,625 cases,
  2,618 succeeded, 7 skipped, no failures (`/tmp/capture-full-native-rerun.log`).
- Host: `python -m unittest` through `.venv/bin/python`, covering
  `test_managed_record_drain`, `test_hitl_capture_integrity`,
  `test_hitl_command_forwarding`, `test_capture_transitions`,
  `test_device_shutdown`, `test_record_target_precondition`, and
  `test_hitl_identity_one_session` — 154/154, `/tmp/managed-drain-all-host.log`.
  Fifteen new cases drive the real managed sender, fence parser, log file and
  cleanup context against a controlled endpoint. Deliberate missing transmission,
  lost fence, counter regression, saturation and leg-exception cases fail closed.
- `openspec validate runtime-working-set-ownership --strict`: PASS.
- Firmware source unchanged in this follow-up; no build/upload required for this
  host-only change. No device run, reconnect, clear or other SD operation.
- Runtime docs, delivery notes, guide and task notes updated. No checkbox changes:
  S4f.22 and the original overdub-transition device gate remain open. Next rerun
  must inspect `assertions.record_evidence_drain` and the same-session RECS rows;
  retrieval success does not override the original transition failure.

### S4f.21 — Record playback invalidation is not transport Stop (2026-09-29)

G0 VERIFIED: run 08 logs `Recording stopped`, two `Record stop retarget` rows,
then `Playback started`, all on the Record stop edge. `completeAdmittedRecordStop`
calls `resetPlaybackState`, which calls `invalidatePlaybackMergedMidiEvents`.
That method unconditionally suppresses output and calls the explicit transport-stop
retarget for any deferred job, including a new Record job. The existing native
fixture asserts PLAYING after the edge but not the job destination/output barrier,
so the later state assignment masks the defect. No harness stub hides this path.
The same owner is reused; DEC-093 shared finalization requires retained PLAYING
playback while Record publishes. G0 PASS; device attribution beyond this matched
trace remains to verify.

Contract: a Record job owns retained playback/source lifetime across cache resets.
Ordinary merged-cache invalidation must leave that job's destination and output
barrier untouched, retaining its scratch until retirement. Explicit
`stopRecordingToStopped` and `requestDeferredOverdubStopToStopped` continue to
resolve STOPPED and suppress output. Keep the overdub recovery path unchanged.
No new field, role, allocation, work loop, undo or persistence behavior.

G2: COVERED explicit Stop calls retarget directly before reset; recursive reset
must not manufacture another Stop. Slot reset/load/clear use the existing
supersession owner; successor identity and window pinning remain unchanged.
Oracle before edit: actual Record start -> actual stop-to-playing leaves a pending
Playing-destination job, output barrier clear, then a real same-slot overdub start
is live while the predecessor remains owned. Existing stopped-retarget and
normalization fixtures must remain green. Mutation is the pre-fix implementation.
Focused shared-finalization + continuous-capture tests; full native and default
capture-serial build/RAM1 gate required. Hardware rerun requires slot-clear
permission; no checkoff from native proof.

**Completion record — native verified (not device verified).**
- Pre-fix `pio test -e native -f test_shared_capture_finalization`: new case
  `test_record_stop_playback_reset_preserves_playing_destination` fails:
  expected Playing (0), observed Stopped (1). `/tmp/record-invalidation-red.log`.
- Fix extends the existing invalidation owner with a constant mode guard. No
  field, allocation, role, new Stop action, persistence or undo change.
- Strengthened fixture checks output barrier clear and starts a same-slot
  overdub while the Record predecessor remains pending/Playing-destination.
- `pio test -e native -f test_shared_capture_finalization -f test_continuous_capture`:
  156/156; `/tmp/record-invalidation-green.log`. Includes explicit Record
  transport Stop and existing overdub recovery coverage.
- `pio test -e native`: 2,626 cases, 2,619 succeeded, 7 skipped, no failures;
  `/tmp/record-invalidation-native.log`.
- `pio run -e teensy41-capture-serial`: PASS; variables 96,192, ITCM 393,216
  (code 391,144 + padding 2,072), locals 34,880; floor 840 unchanged.
  `/tmp/record-invalidation-build.log`.
- Strict OpenSpec validation and diff whitespace checks pass. Guide, runtime
  state/current scope, delivery and task notes updated. No architecture decision
  superseded: this implements the existing shared-finalization continuity contract.
- Device run not performed: explicit track 5 / slot 1 clear permission requested
  and still pending. No upload, port open/reset, or device-SD operation. The local
  build now contains the fix; the previously reported device build does not.
  S4f.21/S4f.22 stay open until their remaining gates pass.

## S4f.22 device legs — 2026-09-29, after the HITL corrections

The host-side corrections (managed boot reset, successor-start receipts, deferred-shape
oracle, `save_request` outcome) made the legs readable. Evidence, newest first:

| run | fixture | verdict | notes |
| --- | --- | --- | --- |
| `captures/s4f22-base-device-17` | registered base (record → OD → OD, one press each) | **PASS** | `issues []`, integrity COMPLETE, `boot_reset` present, `critical_rejected 0`, `RING,overflow 0`; record `32/32`, OD1 `16/16`, OD2 `8/8`, both stored spans `count_ok/span_ok/bars_ok`, `display overdub_ok=True` |
| `captures/s4f22-r-od-overlap` | **temp variant** `STIM-BASE-2-ONE-OD-01-TEMP-R-OD-OVERLAP` (successor one beat after the record stop) | FAIL (FIRMWARE_CONTRADICTION) | overlap **exercised** — `OVERDUBBING: 2`, `OVERDUB_STOPPING: 1` on continuous transport; transitions and content green (`32/32` record, `16/16` OD1, stored span ok, `display overdub_ok=True`); residual issues `record_stored_revt_missing`, `persistence_active_snapshot_unavailable_during_save` |

The temp variant is a non-registered sidecar and was **not** promoted; the canonical
`STIM-BASE-2-ONE-OD-01` was not edited.

**Residual, and what it is not.** `record_stored_revt_missing` is intermittent across
otherwise-identical runs (absent in run 17, present here): the deferred REVT scan is served only
when the transport is fully idle, which a continuous-transport overlap leg does not guarantee.
`persistence_active_snapshot_unavailable_during_save` is the large-loop bounds check. Neither is a
transition or content failure, and neither was waived.

**Setup flake, measured.** A boot-reset capture can leave the device finishing its boot-time
restore, and the clear precondition then spends its whole save-idle budget without observing idle:
120 s → 24 polls → blocked (`s4f22-base-device-16`); 240 s → 48 polls → same
(`s4f22-base-device-18`). Doubling the timeout scales the polling and changes nothing, so the
precondition is **not time-limited** — it needs a receipt for a boot-restore save that is already
complete before the fixture's baseline, not a longer wait.

No task checkoff: S4f.17/S4f.18/S4f.20/S4f.21/S4f.22 stay open, and the OD→OD and R→R device legs
are still outstanding.
