# Post-Record overdub handoff — execution packet

User approved three steps: repair post-Record routing, admit/retain overdub intent through existing owners, and investigate normalization latency independently. Source baseline is feature/unified-rolling-playback-path with existing dirty three-issue/display changes preserved; installed image01f70896 remains unchanged until separate upload approval.

## Contract and historical review

DEC-078 — unconditional interaction acceptance/scheduled execution already names Track stop/start acceptance and the per-interval latch; DEC-067 — stop-directed finalization retains admission checks. Reuse Track::CaptureStopJobs::interaction (already32bytes with unused Start kind), not manager record queue, new state owner, extra finalizer, buffer or scheduler. Existing TrackManager owns overdub admission; Track owns accepted intent and deferred retry. No persistence format/undo/rolling/source-budget change.

Scope: MidiButtonActions record/overdub short press and TrackManager overdub start for the active slot while its first Record job is private. Track's retained Start binds slot/LoopId/capture interval. Repeats coalesce, never mute or start a new Record. Ordinary deferred interaction service retries the existing manager admission after Record publication/resource release; successful actual overdub start consumes once. Source identity/state mismatch supersedes; explicit clear, active-slot exit and transport Stop cancel this start before reuse/resume. Track focus change alone does not retarget it. Existing Stop latch retains its semantics. No extra allocation/field in Track/runtime/latch; existing32-byte pin stays.

Conservative implementation choice: while the matching first Record pass is private, retain the start instead of opening an overdub session against an uncommitted base. Once committed, use ordinary admission, retaining across memory denial. One user press is sufficient; no guarantee of immediate ingress or removal of normalization delay is claimed. Step3 measures that delay rather than weakening work quanta or scheduling fairness.

## Fixture-first transition oracle

Existing test_shared_capture_finalization links real MidiButtonActions, TrackManager, Track, Loop, Record workers, persistence owners and native time/memory boundaries. Initial continuous-capture placement lacked deferred-owner storage dependencies; fixtures were moved rather than adding success stubs.

1. Clear a committed active slot; record32bars of deterministic accepted pairs, stop via the real button route, hold publication by not stepping deferred owners. Assert PLAYING, no committed pass, exact frozen job and take bytes.
2. Press once: latch pending, slot still enabled/unmuted, no new Record interval or live overdub, job/content unchanged. Repeat: same one request and same interval.
3. Step real finalizer to publication. Force memory admission denial using existing native seam, step deferred interaction: request survives, base unchanged. Recover; first valid turn starts one overdub, request disappears, exactly one new interval; later turns cannot start twice. Accept a distinctive pair into the new live overdub.
4. Clear, transport Stop (including resume before retry), active slot exit, replacement identity, empty/filtered Record result: request cannot start on a different/cleared take or unexpectedly restart transport. Focus change does not change its slot. Existing committed stopped-track and slot-button mute semantics remain unchanged.

Bounded fixtures cap drains and print phase on failure. No real sleeps; no on-path domain success stubs. Existing native forced reserve denial is the allocator boundary, not a fake publication result. RED must be behavioral before production edit.

## Verification

Focused continuous_capture/shared_capture_finalization plus existing reentry/stop tests; full pio test -e native; shipping pio run -e teensy41-capture-serial with unchanged RAM1 guard; strict OpenSpec and diff checks; one focused independent lifecycle review. No flash/content test/SD data changes in this implementation step. Normalization investigation records actual captured grain cadence and deterministic production work units separately from device timing guarantees.

Status: local fixture/native/build/review verified; not uploaded or device-qualified. Results below.

## Actual verification and closeout

- Baseline faa089b8 plus prior dirty changes. Production source touched only Track.h, MidiButtonActions,
  TrackManagerCaptureQueue, TrackOverdubLifecycle, TrackLoopJamGeometry and TrackStateFacade. Tests
  live in shared_capture_finalization; transient continuous-capture additions were removed, preserving
  its prior contents. No normalization algorithm, rolling geometry, display or undo implementation edit.
- Initial continuous suite placement failed to link real deferred owner dependencies; NOT behavioral RED.
  Corrected shared-suite RED: button hit fail-closed forbidden slot-content fallback; manager failed
  the retained-intent assertion (opened session early). No compilation/link failure counted as a pass.
- Seven new cases: button interception/coalescing/frozen rows; manager retention/forced admission denial/
  recovery/exact interval+1 and live accepted pair; Stop→resume/clear/slot-exit cancellation; replacement
  LoopId; track focus; filtered-empty publication; final Record geometry while Start remains denied.
- Focused shared+continuous before review:199/199PASS(shared101+continuous98). Fixture setup initially
  over-filtered its seed; replaced with the existing known short-pair policy setup, not a weaker empty
  assertion. Old slot-button path is unchanged and is forbidden on the new pending-Record route.
- One focused independent review found final Record geometry held by the accepted Start. Added case
  RED `Expected Final Was PendingStop`; fixed recordStopGeometryForSlot to inspect real jobs/Stop only.
  Its independent final-geometry oracle is the committed Loop, not the predicted stop stamp. Native
  setup can finalize a shorter geometry; the manager must match that final owner. Review was not
  restarted; concrete issue fixed and retested. Actual transport handler was read by review, while
  the cancellation fixture controls state transitions at the transport/state boundary.
- Final `pio test -e native -f test_shared_capture_finalization`:102/102PASS,7.334s,exit0.
- Final `pio test -e native`: **3446total/7skipped/3439passed/0failed**,213.257s,exit0.
  Log `$PI_SCRATCH_DIR/post-record-native-final.log`.
- Final `pio run -e teensy41-capture-serial`:SUCCESS,7.517s incremental (prior full build48.816s).
  RAM1variables96,192;ITCM393,216(code392,120+padding1,096);locals34,880;floor840 unchanged,
  256bytes above floor. RAM2static168,384;EXTRAMstatic11,680. Track/latch/runtime field size unchanged;
  existing32-byte latch and384-byte target Track pins pass. No warning-clean claim.
- HEX SHA256 `74392e15f3d2184f3c3948faee5715aca01c69178d86f313b5b7bebf18a4af7f`, artifact
  `/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/teensy41-capture-serial/firmware.hex`.
  NOTuploaded; installed01f70896 remains the prior image. No board/reset/content/SD/Git action.
- [Normalization report](NORMALIZATION.md): captured7.905447s/1,411calls, production-cursor unit
  measurements and32/32preparation checks. No optimization/performance improvement is claimed.
- Stimulus registry listed; no canonical stimulus edit/new serial marker. Device one-press response,
  physical timing and memory behavior remain unqualified. Existing base stimulus is available for
  separately authorized evaluation; no content overwrite permission assumed here.
- Session closeout: runtime scope/state, active tasks/spec, DEC-158 and playback/execution-bound docs
  updated. DELIVERABLE_TRACKING unchanged because this image has not shipped. Existing unrelated
  dirty edits preserved. Local tasks5.1–5.3 complete; prior display/undo/CanonicalTail and family-removal
  gates are not closed by this handoff repair.

## Subsequent approved upload

Image74392e15… is now installed; [programming/boot receipt](UPLOAD.md). Earlier no-upload statements
describe the implementation stage. Pre/post canonical drains complete; no content test or manual SD
mutation. Device one-press response and normalization latency remain unqualified/unchanged.
