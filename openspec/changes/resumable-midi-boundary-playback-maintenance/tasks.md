## 1. Decision and baseline gates

- [x] 1.1 Complete `docs/Templates/DECISION_REVIEW.md`, the OpenSpec preflight, and `docs/Templates/OWNERSHIP_TRANSFER.md` for moving derived playback-view ownership and audible-source admission from `Loop` direct order storage to `LoopPlaybackRuntime`; add and validate the `playback-runtime-prewarm` delta; verify the reassessment, canonical-commit compatibility, migration, removal trigger, user approval, and required decision-log entry are resolved before firmware edits.
- [x] 1.2 Capture an overflow-free active-transport baseline that separates each `playbackOrderDirty` cause and every `load_frame` child; map the `appendCaptureEvent` → `sealCapture` → `commitPendingCapturePass` and `maybeCommitOverdubWrap` state transitions. Replace direct `OVERDUBBING → PLAYING` window detection with `OVERDUBBING → OVERDUB_STOPPING → PLAYING|STOPPED` segments plus subsequent PLAYING when present. Verify each active segment can be isolated and prove that the existing in-place incremental seal helper cannot be used as an immutable successor bridge.
- [x] 1.3 Add the affected `ExecutionBounds.md` rows and a capture-gate note for order-build and load-frame grains; add a per-owner long-loop `CoverageServiceContract` row and a `StopAdmissionContract` row that name verified phase-advance/denied-turn or stop-finalization bounds, required grains, lead, and fail-safe action. Verify each row names its owner, interval entry point, declared quantum, continuation, overrun policy, and bound class without assigning a policy ceiling from observed cadence.

## 2. Resumable playback-order successor

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

- [ ] 4.1 Extend capture-only telemetry with owner, grain kind, input quantum, capture/staging/frozen generation, NoteReservation allocation/closure/rejection, stop-admission state, command epoch, requested destination, and latency, effective/pending canonical source generation, cycle-qualified emission cursor, ledger-summary result, coverage interval/service-contract state including source-change horizon, bridge state, load-job phase, duration, and complete/yield/denied/discarded outcome; verify `test_runtime_timing_telemetry` and `test_capture_line_tier` retain parent `track_order`/`load_frame` compatibility and recognize every new line.
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
