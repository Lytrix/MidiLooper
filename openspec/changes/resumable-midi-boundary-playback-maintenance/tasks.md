## 1. Decision and baseline gates

- [x] 1.1 Complete `docs/Templates/DECISION_REVIEW.md`, the OpenSpec preflight, and `docs/Templates/OWNERSHIP_TRANSFER.md` for moving derived playback-view ownership and audible-source admission from `Loop` direct order storage to `LoopPlaybackRuntime`; add and validate the `playback-runtime-prewarm` delta; verify the reassessment, canonical-commit compatibility, migration, removal trigger, user approval, and required decision-log entry are resolved before firmware edits.
- [x] 1.2 Capture an overflow-free active-transport baseline that separates each `playbackOrderDirty` cause and every `load_frame` child; map the `appendCaptureEvent` → `sealCapture` → `commitPendingCapturePass` and `maybeCommitOverdubWrap` state transitions. Replace direct `OVERDUBBING → PLAYING` window detection with `OVERDUBBING → OVERDUB_STOPPING → PLAYING|STOPPED` segments plus subsequent PLAYING when present. Verify each active segment can be isolated and prove that the existing in-place incremental seal helper cannot be used as an immutable successor bridge.
- [x] 1.3 Add the affected `ExecutionBounds.md` rows and a capture-gate note for order-build and load-frame grains; add a per-owner long-loop `CoverageServiceContract` row and a `StopAdmissionContract` row that name verified phase-advance/denied-turn or stop-finalization bounds, required grains, lead, and fail-safe action. Verify each row names its owner, interval entry point, declared quantum, continuation, overrun policy, and bound class without assigning a policy ceiling from observed cadence.

## 2. Resumable playback-order successor

Latest Button A checkpoint (2026-09-15): the live DROID investigation found an
empty frozen tail stuck at atomic transfer with one unused canonical setup
credit. Skipped companion work now retires its credits after closure/order
completion, allowing both normal and transport-retargeted empty stops to finish.
Uploaded firmware passes two empty overdubs and two eight-second wrapped
overdubs on track 5 / slot 1. Final status is STOPPED with no pending stop.
Native playback-prewarm regressions: 147 passed; host probe/transition tests:
26 passed. See [focused evidence](FOCUSED-STOP-HITL.md#droid-button-a-empty-tail-deadlock-2026-09-15).

The immediate PLAYING state edge is distinct from canonical/audible commit.
The 17/20 µs state measurements do not prove emission deadlines or DROID feedback
latency. Task 2.3 remains open for the full atomic handoff/cleanup requirements;
task 4.1 must retain service/emission correlation through background finalization;
5.3, the real-time reservation for 6,240 grains, and 5.4 remain open. Restored-slot
prewarm and the wider implementation/matrix tasks retain their existing gates.

Implementation note (2026-09-15): successor bridge preparation now carries
canonical row ranks, cycle masks, and owned held-note closures across contiguous
coverage windows. The closure is emitted once at its due phase and cannot repeat
in steady cycles. This remains an implementation checkpoint only: production
Track dispatch, rolling refill, and HITL are still open.

2026-09-15 update: the missing reserved session-exit identity handoff remains
repaired; the forced four-bar wrap fixture now reaches `OVERDUB_STOPPING` and
passes transport-Stop silence after the output-barrier repair. Task 2.3 remains
open for its protected cleanup/undo, bounded publication, bridge, and full
admission/parity requirements. Full timing and layered gates remain open. See
`FOCUSED-STOP-HITL.md` for the distinct verdicts and evidence.

- [x] 2.1 Add fixed, external-memory-first playback views containing events, order, source/projection identity, cycle-qualified `EmissionCursor`, cursor anchor, ledger-boundary summary, and explicit long-loop coverage. Prewarm three view buffers (current, ready successor, successor-after-next build), two independent bridge buffers (selected/candidate) sized across the current/ready horizon, generation-matched capture-source admission staging, and `CaptureNoteReservation`/stop-finalization credits. Verify firmware-size/RAM1 budgets under simultaneous selected bridge, candidate bridge, source-change successor build, and accepted open-note reservations; published-view immutability during a paused build; coverage wrap capacity; ledger-summary equivalence; and no allocation on an active playback tick.
- [x] 2.2 Implement phase/index fill and bottom-up merge ordering as independently bounded, resumable grains, including canonical equal-phase Off-before-On ordering. At every handoff, retain an incomplete successor as pending and keep the retired current view idle; only after that successor becomes ready may the retired buffer begin its successor-after-next build, without overwriting current, ready, or pending coverage. Add both unchanged-source and source-change coverage-service continuations, with the latter proving the new-source handoff and each following refill from the latest permitted predecessor completion point. Verify each contract against maximum phase advance and denied turns; if either cannot be proven, reject active windowed selection or the source mutation and exercise the full-loop/STOPPED-only fallback. Verify native parity against empty, long, wrapped, equal-tick, non-note, repeated-`NoteId`, denied-refill, three-buffer rotation, source-change preemption near handoff, source preparation completing midway through each of two successive coverage windows, and coverage-fault fixtures.
- [ ] 2.3 Add generation-matched `CaptureAdmissionStaging` before `appendCaptureEvent` mutates `capture.store`. On accepted NoteOn, reserve its matching NoteOff, wrap continuation/synthetic closure, staging, bridge, and stop-finalization credits in a `CaptureNoteReservation`; a matching Off consumes that reservation and cannot be rejected under ordinary pressure. Reject an unreservable NoteOn before it creates capture, pending-note, preview, or ledger state; its later unmatched Off bypasses capture mutation. Build immutable candidates from a stable prefix plus resumable tail, and run copy-on-write semantic equivalents of sorting, NoteId assignment, wrap finalization, and filtering before seal. Modify `commitCapturePass` to atomically transfer the frozen candidate and ready `PlaybackSourceAdmission` without sorting, finalizing, detaching mutable chunks, or building a bridge. Add `OVERDUB_STOPPING`: latch stop input, freeze the generation, run reserved stop-finalization grains between MIDI boundaries, and commit only to a command-epoch-validated destination. Transport Stop has precedence: it selects STOPPED, invalidates every pending PLAYING transition, emits all-notes-off, and allows frozen canonical recovery only under STOPPED policy. Reserve the fault stop for a declared-contract breach; never clear/restart capture as a false commit or silently omit an accepted event. Add source-identity invalidation, coalescing for later-effective-cycle revisions, cursor-safe atomic successor selection, and a mandatory immutable delta bridge whenever full copy-on-write cannot finish before the earliest affected due event. Build fixed-storage insert/suppress/replace overlay records, consume them by a bounded two-way cursor merge, retire them through the cycle-qualified cursor, and retain a base NoteOff for an already-emitted ledger entry. Verify phase-zero overdub-wrap admission, accepted-On/rejected-On pressure, accepted-On/matching-Off pressure, wrap-held closure, append-and-stop with no intervening grain, bounded deferred-stop latency, transport Stop between finalization grains, stale pending builder, selected-bridge immutability, second-source candidate coalescing, capture preservation after fault stop, and promotion without replay, omission, or delay.
- [ ] 2.4 Change active `playCommittedLoopMidi()` order handling from synchronous full rebuild to request/consume behavior while retaining the complete in-place builder as a STOPPED-state oracle; verify instrumented native tests fail if active transport invokes `std::sort`, vector growth, full gather, or full reanchor.
- [ ] 2.5 Differential-test the published/ready/build/bridge path against the complete oracle across record stop, phase-zero overdub-wrap admission, an append just before wrap, pre-seal candidate invalidation/rebuild, rejected-On/later-Off, accepted-On/matching-Off under staging pressure, wrap-held NoteOn closure, append-and-stop without an intervening grain, bounded deferred-stop completion, promotion before wrap, long-window handoff, maximum phase advance, denied refill, three-buffer rotation, source-change preemption near handoff, source preparation completing midway through each of two successive coverage windows, second-source bridge replacement, coverage fault, same-tick NoteOff/NoteOn, a source change touching an open note, layered slots, slot switch, and first/second revision/source changes. Verify byte-identical MIDI traces with no missing, duplicate, delayed, or reordered event for every accepted canonical commit, and an explicit deferred-stop trace for normal stop admission, except the specified fail-safe stop trace after an injected contract violation.

## 3. Resumable load/display-frame orchestration

- [x] 3.1 Add fixed continuation state to the `main.cpp` load/display owner that selects exactly one expensive active-transport child per eligible turn; verify display phases retain their continuation, admitted due display frames preempt load slices while load progress stays saved, denied work stays pending without restart, and no display phase/load job/prewarm/boot admission stacking occurs.
- [x] 3.2 Retain the existing resolve/draw/transfer display state machine and DMA polling, but make its one admitted phase an explicitly selected load/display child; verify a pending transfer coalesces newer paint requests and the incomplete frame is never published.
- [ ] 3.3 Keep `DeferredJobScheduler::runFrame(budgetUs)` as the existing load-job child, but expose its next phase and isolate its turn from other active-transport children; split Commit preparation and cleanup into continuations, measure the complete atomic Commit body, and deny active admission for any Commit without a content-independent bound. Verify focus-load behavior, demote-on-focus, bounded-Commit native coverage, and STOPPED-only fallback coverage.
- [ ] 3.4 Replace synchronous first-commit playback prewarm in a timing-critical load frame with bounded prewarm grains; verify a load Commit cannot expose an unprepared audible slot, prewarm resumes without an active-path allocation, and Commit cleanup cannot run hidden inside an active `runFrame` grain.
- [x] 3.5 Preserve post-overdub and `OVERDUB_STOPPING` MIDI polls and persistence non-stacking around the selected child; verify native `MidiServiceDrain` policy tests cover one-child turns, denial, deferred save behavior, and transport-Stop precedence over a pending stop continuation.

- [x] 3.6 Fix PLAYING display starvation: require actual pending restore work for active load admission/retention, prioritize admitted due display frames between load slices, and preserve STOPPED housekeeping. Validate sustained refresh with a retained load backlog, one-child turns, and load resumption in the native scheduler regression.

## 4. Attribution and capture gates

- [ ] 4.1 Extend capture-only telemetry with owner, grain kind, input quantum, capture/staging/frozen generation, NoteReservation allocation/closure/rejection, stop-admission state, command epoch, requested destination, and latency, effective/pending canonical source generation, cycle-qualified emission cursor, ledger-summary result, coverage interval/service-contract state including source-change horizon, bridge state, load-job phase, duration, and complete/yield/denied/discarded outcome. Separate immediate USB/serial playback deadline scoring from deferred DROID USB-host feedback, and retain bounded queue age/depth/overflow telemetry for the latter. Verify `test_runtime_timing_telemetry` and `test_capture_line_tier` retain parent `track_order`/`load_frame` compatibility and recognize every new line.
- [x] 4.2 Add a host-side timing report that scores RECORDING, PLAYING, OVERDUBBING, and `OVERDUB_STOPPING` intervals; segment `OVERDUBBING → OVERDUB_STOPPING → PLAYING|STOPPED` without relying on the old direct transition; reject `RING,overflow` inside every scored interval; and assert no extra MIDI service call sites. Verify fixtures reject mixed stopping/post-overdub windows and attribute each retained outlier to a single grain.

## 5. Verification and closeout

Latest evidence (2026-09-15): [track-specific emission correlation](FOCUSED-STOP-HITL.md#track-specific-emission-evidence-2026-09-15-0939).
Target-track NoteOn/NoteOff records now correlate with stop state edges and
service timestamps without trace loss. The dense run nevertheless reports
231 ms accumulated deadline lateness that was already present at stop entry.
Correlation is complete for this checkpoint; deadline safety and the reservation
are not accepted. Restored-slot prewarm integration remains open under 3.4.

Evidence update (2026-09-15): [fresh normal stop and correlated interruption](FOCUSED-STOP-HITL.md#correlated-transport-stop-rerun-2026-09-15-0922).
The focused interruption passes stop precedence/silence with zero trace drops;
state edges and grain-start service snapshots are retained on the device clock.
Those earlier short stop windows contained no note emissions, and the broad baseline
remains inconclusive. No task checkbox or real-time reservation is closed.

Evidence update (2026-09-14): see [focused stop timing](FOCUSED-STOP-HITL.md#timing-relative-to-midi-boundaries-2026-09-14).
The normal stopping sample is 9.291 ms (0.446 nominal MIDI clock intervals at
120 BPM); nearby wrap work is 31.690 ms and reported clock lateness is 25.774 ms.
These duration comparisons do not verify boundary alignment, per-grain slack,
or the 6,240-grain real-time reservation. Tasks 4.1 and 5.3 remain open for
complete deadline attribution and the broader transport-Stop matrix;
no task completion or acceptance threshold changes follow from this sample.

- [x] 5.1 Run the focused native suites for playback ordering, MIDI service drain, telemetry, capture tiers, slot load, and new continuation fixtures; verify `pio test -e native` passes.
- [x] 5.2 Build the capture-serial firmware and verify RAM1/firmware-size limits remain within the project gate.
- [ ] 5.3 Run overflow-free HITL record, overdub, wrap, canonical-versus-pending-audible admission, normal `OVERDUB_STOPPING`, transport Stop during `OVERDUB_STOPPING`, short/long loop coverage handoff, coverage-contract fault, source-change bridge, open-note bridge change, second source change, slot switch, bounded-Commit and excluded-Commit load, and post-overdub PLAYING scenarios with DMA display enabled. Score stopping and subsequent PLAYING separately; verify no NoteOn, NoteOff, or clock deadline lateness is attributable to an order-build or load-frame grain and the 4b.3 post-overdub MIDI-boundary criterion is met.
- [ ] 5.4 Remove the direct active-transport full order rebuild and compound load-frame compatibility paths only after the previous gates pass; verify STOPPED-only throughput remains separately reported and update `CURRENT_WORK.md`, `PROJECT_STATE.md`, the 4b.3 evidence, and the decision log before archive.

Evidence update (2026-09-15): [pre-stop emission burst attribution](FOCUSED-STOP-HITL.md#pre-stop-emission-burst-attribution-2026-09-15) identifies a 258-message target-track burst after snapshot publication, with 233.444 ms nested send time before stop. Rebuild telemetry now retains occurrence time and previous/current playback phases to distinguish source-admission catch-up from stop-grain blocking. Cursor cause and full prepared-source integration remain unverified; no acceptance gate is closed.

Device follow-up (2026-09-15 09:52): [fresh-origin catch-up confirmed](FOCUSED-STOP-HITL.md#device-confirmed-fresh-origin-catch-up-2026-09-15-0952): first snapshot reaches phase 375 with previous phase UINT32_MAX; nested send max is 144.102 ms. This pins the next 2.4/3.4 work to prepared-source admission plus initial ledger/cursor anchoring. Normal stop completes, but no in-stop notes were captured, so the emission checkpoint remains open.


User correction (2026-09-15): deferred first-record launch is rejected. Tasks 2.3–2.5 and 3.4 must integrate first-record input with rolling one-bar playback preparation, establish and retain bar 1 from recording start, and select it at the existing final-bar return boundary. Add one-bar late-input/no-intervening-grain, held-note, unknown-final-length, multi-bar refill, and snapshot-replacement parity fixtures. Task 4.1 must correlate record start, window readiness/generation, final-bar boundary, selection, and first emission. Task 5.3 must verify this on-device without delayed launch, global rewind, skipped notes, or a catch-up burst. The rejected proposal is historical only; no new first-record rejection policy or unmeasured reservation is accepted.

Implementation continuation (2026-09-15): first-record setup now installs an
empty raw bar-1 view through the shared playback lifecycle before RECORDING is
entered, and record stop binds its final loop length before the normal audible
handoff. The shared fill worker accepts a borrowed source descriptor for the
chunked canonical capture store. During recording, one bounded fill/order grain
per VisualCache owner turn refreshes the retained bar; capture-display
generation changes cancel and restart the unpublished build without mutating the
audible view. Cache reset preserves the retained raw view. Native focused
coverage/prewarm tests pass (132/132); both Teensy targets build. Deferred
main-loop maintenance is explicitly placed in FLASH so the capture-serial
image stays below the flexRAM bank boundary with 6,496 bytes of RAM1 local
headroom. HITL upload and tasks 2.3–2.5/3.3–3.4 acceptance remain open.

HITL update (2026-09-15): the memory-safe capture firmware was uploaded. The
track 5 / slot 1 transport-Stop interruption rerun passed focused stop
precedence and silence verification, while the broader host baseline remained
inconclusive by design because the injected Stop aborts overdub verification.
A clean normal-stop rerun remained inconclusive after missing the expected
overdub-stop transition confirmation. No HITL acceptance checkbox closes.


Implementation increment (2026-09-15): added `RuntimeRecordingWindow`, a private
prewarmed one-bar order with constant-work append/unlink, lazy-epoch reset,
stable tick/kind buckets, and immutable sealing. Native regressions cover a
last-tick Off arriving after Ons without a worker turn, retained bar 1 across
later-bar reuse, capacity/removal/reuse, and randomized parity against stable
tick/Off-before-On ordering. The focused playback-prewarm suite passes 122 tests.
This is not yet wired to Track capture or playback. Closure/capacity reservations,
canonical final geometry, source/ledger admission and coverage integration remain
required; task checkboxes and HITL gates remain unchanged.

Build validation for this increment: production and capture-serial both pass; capture RAM1 local allowance remains 6,496 bytes. The new component is not selected by production playback, so this build result does not measure its eventual runtime memory reservation or boundary cost. No firmware upload or HITL run was performed for this isolated ordering increment.


Recording-window continuation (2026-09-15): per-slot runtime now owns its private
return window. Playback-cache resets retain it; full slot reset retires it.
Same-window NoteOn admission reserves a physical terminal Off row before exposing
the On, preserving closure at full capacity. Handles bind owner and epoch, including
cold re-prewarm, and wrong/duplicate/stale closure attempts leave reservations intact.
Canonical short-pair removal consumes the reserved closure without emitting either
row. A window with unresolved local reservations cannot seal. These APIs are not yet
called by Track first-record input or selected as an audible source. Cross-bar/loop-end
closure transfer, geometry, full accepted-input capacity, canonical publication and
boundary activation remain open; no new recording rejection policy is introduced.

Validation of recording-window ownership/closures: 127 focused native tests pass, strict OpenSpec validation passes, and both final firmware builds pass. Capture RAM1 code is 425,816 bytes with a 6,496-byte local allowance. No upload/HITL was performed: Track first-record ingestion and audible selection still do not use this window. The verified first-record catch-up defect is not yet fixed.


Recording closure increment (2026-09-15): added reserved destination rows for
cross-bar terminal Offs and explicit final-loop-end closure into the bar-1
phase-zero bucket while retaining canonical raw ticks. Wrong/stale/foreign credits
and invalid geometry preserve both private windows; short-pair filtering consumes
both obligations. Focused native suite: 133 tests pass. Track first-record ingestion,
rolling obligation propagation, full accepted-input capacity, finalization work
bounds and atomic audible selection remain open. No checkbox or HITL gate closes.

Cross-bar/loop-end closure validation: all 133 focused native tests and both firmware builds pass; capture RAM1 local allowance remains 6,496 bytes. Strict OpenSpec validation and diff whitespace checks pass. No upload or HITL was performed; these private-window methods are not yet wired to first-record input or audible playback.


Prepared recording stream increment (2026-09-15): a borrowed sealed-window
stream now exercises the existing MIDI cursor advancement path. Differential
validation against RuntimePlaybackOrderBuilder covers a 514-event bar with
repeated ticks, stable Off/On/other ordering, a raw loop-end Off at phase zero,
and a mid-bar handoff after the serviced phase without historical replay. Both
layered and active committed emission policies match the canonical trace.
Retired/reused storage is rejected before emission; incompatible final geometry
is denied. The focused native suite passes all 135 tests.

Selection validates geometry and seeks in O(retained events + ticks per bar);
it belongs before activation, not inside the final boundary. Playback traversal
can inspect up to three buckets per bar tick across a window; a sparse seek can
spend that full bound in one call. No device timing reservation is established.
The adapter is not yet selected by Track. Full accepted-input capacity, capture
wiring, rolling readiness and atomic boundary activation remain open, as does
the observed first-record catch-up defect. No task checkbox or HITL gate closes.

Capacity integration finding: LoopEventStoreConfig provides 512 shared chunks
of 256 events (131,072 physical event rows before other owners/reserves). First
record append uses that pool rather than a fixed per-bar admission limit. The
private window and shared playback cursor currently use 16-bit row positions,
so merely increasing a proposed 2,048-event window reservation cannot establish
coverage of all previously accepted capture input. Resolve storage/index capacity
against pool admission before enabling this path; do not introduce silent input
rejection or late launch as a fallback.

Prepared-stream build validation: production and capture-serial both pass;
capture RAM1 local allowance remains 6,496 bytes. Strict OpenSpec validation and
whitespace checks pass. No firmware upload or HITL was performed for this
component; unused stream code does not establish its eventual active memory or
timing cost.


Continuation (2026-09-15): replaced the prepared stream's full retained-event
geometry scan with the append/closure invariant described in design.md. Final
length is bound per return-window epoch before filtering, and conflicting
closures preserve reservations. Added a 32-bit logical stream/cursor overload
of the shared MIDI walker; existing callers retain the narrow overload.
All 137 focused native tests pass, including a 131,072-event synthetic source
that crosses the 16-bit limit and recovers from invalid-entry denial without
replay. Physical recording-window capacity, capture wiring, sparse seek cost
and atomic activation remain unresolved; no task or HITL gate closes.

Validation of geometry binding and wide cursor: 137 playback-prewarm tests plus
154 tests across playback-cursor, MIDI-output and pending-note-change suites
pass (291 total). Both production and capture-serial builds pass. Capture RAM1
code is 425,880 bytes; the local allowance remains 6,496 bytes, with 104 bytes
of padding before the next ITCM bank. OpenSpec strict validation and whitespace
checks pass. No upload/HITL; physical recording storage and Track selection
remain unchanged, so the observed first-record timing defect is still open.


Physical recording storage continuation (2026-09-15): dedicated prewarmed rows
replace borrowed 16-bit playback-sort arrays. Row/bucket links, closure and
incoming credits, and the recording stream now use 32-bit positions. Actual
131,072-row storage traversal and full-capacity terminal closure pass, including
removal across the old index limit and an incoming credit above it. All 139
focused native tests pass. Cold allocation is overflow checked; no active
append/reuse growth is introduced. Device-wide shared memory admission, Track
input wiring, rolling readiness and atomic boundary activation remain open.
The physical index limit is resolved; this does not yet fix the first-record
catch-up defect or close any task/HITL gate.

Physical-row validation: both firmware builds pass; capture RAM1 code remains
425,880 bytes and local allowance 6,496 bytes. Strict OpenSpec validation and
whitespace checks pass. No firmware upload or HITL was performed.

Device reservation constraint: the existing capture
`captures/resumable-stop-rebuild-phase-device-hitl/session_20260915_095242.log`
reports `psram chip=8 MB pool=8185 KB`. Multiple full-pool event copies for return
and rolling windows cannot fit alongside the canonical pool and other runtime
owners. The wide-row fixture proves representable extent, not available device
capacity. Resolve shared canonical event references and their immutable lifetime
(or equivalent storage sharing) before enabling Track ingestion; do not turn this
memory shortfall into a new capture rejection policy or delayed launch.


User DRY correction: removed RuntimeRecordingWindow, its separate stream,
per-slot member, and prototype-only tests. Their historical validation notes
above are superseded, not shared-path acceptance evidence. Reuse the existing
RuntimePlaybackView/Fill/Build, order builder, lifecycle and bridges for recording.
The shared fill/build now supports explicit PlaybackCoverage selection, charging
all scanned rows to the quantum and denying unpublished candidate overflow.
All 122 focused native tests pass, including raw loop-end Off preservation,
modular coverage, a 65,538-row source, and overflow/retry. Existing AllSource
callers retain their default behavior. First-record source/lifecycle integration,
late input, closures, capacity and HITL remain open. No task checkbox closes.

DRY refactor validation: both firmware builds pass. Capture RAM1 code is 425,816
bytes; local allowance remains 6,496 bytes. Strict OpenSpec validation and
whitespace checks pass. No firmware upload/HITL. Recording-specific code is gone;
shared-path first-record integration and its behavior/timing proof remain open.


Shared lifecycle implementation: retained return coverage is a role in the
existing three-view lifecycle, not a separate store or stream. Pending and
already-complete successor paths preserve it while the other buffers rotate.
The return operation detaches other coverage without clearing event/ledger
arrays; owner preflight and in-flight worker discard remain required. Full
runtime reset invalidates retention. The shared runtime coverage-build wrapper
now uses PlaybackCoverage fills. All 125 focused tests pass, including ten-bar
successor rotation followed by return and a new successor, reset invalidation,
and filtering at the runtime owner. These are shared-owner methods; Track
recording/deferred-maintenance does not yet invoke them. Source identity, cache
refresh, late-input/closure handling and first-record activation remain open.

Shared builder cancellation correction: discard now detaches both borrowed fill
and ordering continuations and invalidates only unpublished candidate readiness.
Previously the fill worker retained its destination pointer, preventing a new
build after mid-fill cancellation. Regression covers cancellation during fill
and order, rebuilding into a different view, and preserving already published
coverage. All 126 focused native tests pass.

Final shared-owner validation: 126 focused native tests, both firmware builds,
strict OpenSpec validation and whitespace checks pass. No firmware upload/HITL.
Track recording input and first-record return activation remain unconnected;
this increment does not claim the observed first-record timing defect is fixed.


Raw preparation continuation: added RawCoverage to the shared fill worker and
RawPlaybackTick to the existing order builder. They exclude later raw ticks
without provisional modulo and preserve audible stable Off-before-On ordering.
Completed raw coverage binds final length without an event/order walk. Shared
lifecycle Preparation mode rotates unbound windows; default audible handoff and
retained-return activation reject them until binding. All 129 focused tests pass,
including eight-bar preparation rotation and parity against projected playback.
Track::startRecording now sets the recording origin before pre-roll ingestion.
Live capture feeding, terminal closure updates, capacity, cache/commit ownership
and record-stop activation remain open. No task or HITL gate closes.

Raw-preparation validation: 129 focused native tests, both firmware builds,
strict OpenSpec validation and whitespace checks pass. No firmware upload or
HITL. The recording-origin correction is implemented in Track, but raw window
feeding/activation is still not connected to live capture; the first-record
snapshot-delay/catch-up defect remains open.


Playback display scheduling fix (2026-09-15): permission to restore no longer
keeps an empty load child selected during PLAYING. Admitted due display frames
also preempt retained load slices without resetting StorageManager progress.
The regression models 1,000 owner turns with a permanent load backlog: 100
complete three-phase display frames and 700 separate load slices. All 161 tests
across `test_midi_service_drain`, `test_display_window_utils`, and
`test_load_loop_selection_policy` pass. The production `teensy41` build passes
using an isolated temporary build directory after the shared cache build failed
with missing object files. No upload or device verification was performed.
Task 5.3 remains open and must include sustained PLAYING refresh with empty and
deferred background-load queues, plus RECORDING/OVERDUBBING transitions and
MIDI timing checks. This fix does not close the other playback or Commit gates.

### 2026-09-15 stop liveness and display regression

device `!TRACE_FENCE` answered
while the interface appeared frozen: main-loop liveness was retained. A stopped-only
`!STOP_STATUS` query now exposes the selected track's stop and history worker phases.
`captures/normal-stop-status-20260915/stopped-status.log` proves a stall in
CompanionHistory, with canonical tail complete. Recording start prewarmed resources
before resetting their canonical generation; the reset now precedes prewarm, and
snapshot cleanup retains the reserved companion-ID destination. The next device
report (`captures/normal-stop-resources-fix-20260915/stopped-status.log`) proves
matching generations but history Trim parked with zero canonical estimates.
Preparation/recycle counters now saturate as estimates rather than denying further
valid worker turns. Completed workers retire unused estimates without padding turns.
The display owner reuses its sliced paint continuation during OVERDUB_STOPPING.
Native prewarm/edit suites passed 259 tests after resource fixes; final continuation
prewarm/survival suites passed 259 tests (edit suite also passed 124). Capture firmware
built/uploaded. The next device run advanced through history to CompanionBridge
(phase 8), with history Ready and bridge Idle; normal stop still fails. Evidence:
`captures/normal-stop-continuous-display-20260915/stopped-status.log`. The current
full-loop bridge requirement and transport-stop retarget need further investigation.
The user-observed rerun also failed normal-stop verification:
`captures/normal-stop-display-user-verify-20260915/result.json`. Transport Stop was
sent for cleanup; completion was not established. Screen responsiveness still needs
the user observation. No task 2.3, timing reservation, or HITL acceptance gate closes.

### 2026-09-15 first-record frame gaps

**2026-09-15 first-record display regression:** user confirmed the screen now
continues updating in OVERDUB_STOPPING, but reported multi-second frame gaps during
the first two recording bars. A recording-only mode in the existing stop probe
isolates that interval from the stalled stop trace flood. Before the scheduler
fix, stopped background tracks rebuilt 56/353/1429-row display caches during the
recording owner turns: idle maintenance reached 281,346 us, a sampled display frame
538,356 us, and the window MIDI-service gap 281,883 us. The realtime dispatcher now
services realtime tracks only; pending stopped-track wrap work cannot block the
active display. The after-run sampled frames were 14,260–15,942 us, with no reported
long idle-maintenance span. Both captures are overflow-free. The after window still
has a 180,003 us MIDI-service maximum around recording entry; this is not deadline
acceptance. Evidence: `captures/record-only-background-fix-20260915/comparison.json`
and both linked session logs. Capture build/upload succeeded; six existing host
probe tests pass. The recording sequence completed and transport ended STOPPED.
CompanionBridge stop liveness remains open; no OpenSpec acceptance gate closes.
A stopped-only `!TRACE_DRAIN` diagnostic retrieves retained logs on explicit request.

### 2026-09-15 companion bridge and stopped recovery

**2026-09-15 companion-bridge checkpoint:** user confirmed first-record display
responsiveness after the background-cache dispatch correction. The remaining
normal-stop stall is now explicitly attributed on device:
`ODUB,stop,bridge_base,...,missing_full_loop_base`. A first recording retains a
one-bar raw playback view; the compatibility bridge mapper, selection preflight,
and hot-path consumer still require a full-loop view with canonical provenance.
Do not fix this by synchronously materializing a full-loop base or weakening
provenance/coverage checks. Task 2.3 must integrate coverage-aware canonical
successor preparation with the existing window lifecycle and task 2.4 consumer,
including current-phase cursor/ledger and subsequent-window handoffs.

Transport-Stop recovery from CompanionBridge is repaired: completed history skips
audible bridge work and calls the existing stopped canonical/companion commit
body, shared with its undo-aware entry point. Frozen-command/owner checks and
history cleanup remain enforced. Native stop/prewarm and take-survival suites:
259 tests passed, including no-published-base recovery and stale-command rejection.
Capture build/upload passed. Device `captures/bridge-stopped-recovery-20260915/`
shows OVERDUB_STOPPING -> STOPPED, retained correlated state/grain evidence, no
trace drops and no ring overflow. The normal-stop verifier still fails, correctly;
this is recovery evidence, not normal-stop, silence-matrix, or realtime acceptance.
The device was left STOPPED. No task checkbox closes.

### 2026-09-15 canonical window worker implementation

**2026-09-15 canonical window implementation (partial task 2.3):** the existing
RuntimePlaybackViewFill and RuntimePlaybackViewBuild now accept typed canonical
coverage-scoped sources, copy events and pass provenance together, and keep fill
and order on separate bounded turns. Whole-source length need not fit the view;
only retained rows consume destination capacity. The existing companion bridge
mapper now filters the prepared per-pass result to that same coverage and checks
the borrowed base's coverage, publication, storage and provenance receipt on every
turn. The bridge compiler accepts a matching partial coverage with an empty ledger;
open-note obligations remain denied until carried across subsequent window rotation.
No raw/projected source is silently accepted as canonical provenance.

Native verification covers nonwrapped, wrapped and empty windows, one-entry quanta,
source larger than destination, provenance surviving worker reuse, per-pass MIDI
parity, changed coverage and unsupported held-note closure rejection. Final focused
prewarm suite: 138 passed; edit suite: 124 passed. Capture firmware build passed.
This is shared-worker implementation, NOT completed Track integration: stop-owner
base-window preparation, current-phase admission, closure-carrying rotation and the
ordinary PLAYING consumer must be connected together before enabling normal stop.
The new pieces were not uploaded; the device retains the verified display fix and
transport-Stop recovery. Normal-stop device verification and task 2.3 stay open.

**2026-09-15 stop-owner canonical window preparation (partial task 2.3):**
CanonicalPrefix now prepares a one-bar canonical base when the current view has
no usable full canonical provenance. It merges the normalized prefix through the
existing RuntimeCompanionPasses worker, fills/orders a spare view through the shared
RuntimePlaybackViewBuild, and publishes an immutable ready view without changing
audible current. Fill, order and publication resume on separate turns. The pass
worker is released before frozen-tail processing; copied origins survive its reuse.
A busy shared builder stays pending, and source/storage changes cancel only the
owned preparation. Both initial-ready and post-record no-ready lifecycle shapes
are covered. No new event buffers or active allocations were introduced.

Native prewarm/take-survival: 265 passed. Capture build passes with 6,176 bytes of
RAM1 local headroom. This does not fix normal-stop liveness yet: the bridge selector
and PLAYING consumer still require the full-loop compatibility base. Current-phase
admission and held-note closure carry through subsequent window rotation remain
required before enabling the new ready base. No upload, HITL verdict or task closure.

**2026-09-15 ready-window bridge integration (partial task 2.3):** frozen
companion bridge preparation now borrows the exact prepared canonical ready
window, falling back to the existing full current base when needed. Mapping and
compilation no longer re-resolve through the audible-current index. Admission
checks that the preparation anchor lies inside this base coverage. Retirement,
provenance loss and coverage changes invalidate the borrowed base, including
completed candidates. Physical views now advance a storage generation on reuse;
mappers, bridge compilation, selection and stream cursors reject stale indexes
even when source identity and coverage repeat. The stopped diagnostic identifies
`prepared_window` separately from a missing canonical base.

The integrated native fixture covers prefix -> ready window -> frozen tail ->
bridge compilation -> the existing read-only MIDI cursor, with nonwrapped,
wrapped and empty coverage and equal-phase Off-before-On output. It verifies that
retiring the unrelated old current does not invalidate the ready base, while
retiring/reusing the borrowed base does. Audible Track selection remains guarded:
held-note closure carry, current-phase admission and following-window playback
are not complete, so this is not a normal-stop or HITL pass. No firmware upload.
Native prewarm/take-survival: 265 passed; capture firmware build passed with
6,176 bytes RAM1 local headroom. Strict OpenSpec validation and diff checks pass.

**2026-09-15 bridge-owned held-note closure state (partial task 2.3):** the
existing bridge compiler now stores its affected-open-note flags, closure time
and next-On phase in the bridge's fixed ledger-sized storage. When the existing
closure walk finds the original Off, it also copies that event into the bridge.
The copied closure survives compiler discard and base-window reuse. The old
base index remains only for the current compiled stream; it is not a transferable
closure receipt. No alternate pairing or closure-resolution algorithm was added.
Bridge reset retires closure metadata by count without clearing every payload;
compilation rejects a changed ledger count. Unresolved or incomplete receipts
are not exposed by the closure accessor.

Native prewarm/take-survival: 266 passed, including the unchanged cross-wrap MIDI
trace, all 128 ledger slots, base reuse, bridge reuse and changed-ledger rejection.
Capture build passes with 6,176 bytes RAM1 local headroom. Successor inheritance,
closures outside the initial window, current-phase selection and rolling playback
consumption remain required; the partial-window held-note guard stays in place.
This is ownership preparation, not completed cross-window playback or a normal-stop
fix. No upload or HITL acceptance; task 2.3 remains open.

**2026-09-15 successor closure integration (partial tasks 2.3–2.4):** the
existing RuntimePlaybackBridgeBuild now has a same-source successor entry point.
It checks contiguous coverage/cycle boundaries and a stable selected predecessor,
then inherits owned closures in bounded grains. A copied Off is inserted only in
the window containing its original due phase and only in its original cycle.
Steady insertion ordering compacts already-read indexes so closure-only Offs cannot
repeat in later loops; the existing MIDI cursor consumes the resulting streams.

Typed shared view fills now retain each row's complete canonical merge rank.
Compiled bridges retain canonical cycle masks across the complete rank horizon,
so later windows inherit gating decisions without reconstructing them from NoteIds.
Masks shift at cycle advance and survive windows containing no events. Replaced
old base rows cannot overwrite the effective canonical row's mask. Both bridge and
view buffer generations protect builders and cursors against reuse. All new slabs
are prewarmed; the existing 6,240-grain reference-case estimate remains unchanged.

Native prewarm/take-survival: 270 passed. Fixtures verify original Off emission,
no repeated closure in steady cycles, carry through two windows at quanta 1/32,
shortening the revised Off before the old closure, stale predecessor/source denial,
and canonical ranks surviving source-worker reuse. Capture firmware build passed:
RAM1 local headroom remains 6,176 bytes. This is shared compiler/cursor integration,
not completed production Track dispatch or a device normal-stop verdict.

Still required before enabling rolling Track selection: initial partial-window
closure context for already-open notes (its guard remains); coverage-qualified
cursor consumption across wrapped windows; atomic current-phase view/bridge handoff;
ordinary successor refill; and normal-stop cleanup without silence/reanchor.
No upload, task checkbox closure or HITL acceptance is claimed.
