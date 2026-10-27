# Record → overdub production-route regression execution

## Identity and scope

- User approval: 2026-10-26, implement the investigation's immediate admission, frozen-reader survival, and missing live overdub display corrections.
- Active changes: `immediate-record-overdub-handoff` (capture/display); `shipping-rolling-activation` (frozen rolling reader).
- Authority: capture-state-guards immediate ingress/early Stop; realtime-visual-cache coherent presentation; DEC-159 — immediate sealed custody, DEC-160 — predecessor-local finalization, DEC-161 — additive frozen staging, DEC-162 — bounded private-source progress. Existing P-A/P-B/P-C/P-D contracts remain unchanged.
- Status: implemented, native and shipping-build verified; fresh device acceptance NOT RUN.
- Existing owners: button → TrackManager admission → Track live Capture and sealed custody → serialized deferred finalization; Loop private-source and preview producers; DisplayManager draw-source compose; Track rolling maintenance and clock-side playback.
- Primary files: TrackOverdubLifecycle, TrackManagerCaptureQueue, MidiButtonActions, Track declaration; DisplayNoteWindowGather as required by a demonstrated failure; TrackPlaybackWindowBuild; existing immediate-handoff, shared-finalization, playback-prewarm, and live-display native harnesses.
- No new queue, resource role, state field, scheduler, bound quantum, second reader, committed/private currency reinterpretation, or SD schema. Preserve extensive pre-existing dirty changes. No automatic upload or device-data authorization.
- Independent buildable slice; no commit requested.

## Historical decision review

Searched DECISION_LOG and active contracts for private Record, retained Start, custody, source, and display publication gaps. Reuse existing Track admission, interaction latch, private predecessor cursor, sealed custody, and draw-source overlay. DEC-158's queued-only behavior is intentionally replaced under ratified DEC-159/DEC-160; installed-image evidence stays historical. No new architectural choice or ownership transfer. Recovery latch remains identity-qualified when ordinary admission refuses.

## Oracle fixed before behavior edits

1. Immediate admission: existing `test_immediate_start_accepts_input_before_record_publication` drives actual button/manager against sealed private Record; next accepted note enters a distinct successor interval while predecessor remains private and unchanged. Manager explicit starts are idempotent. The default suite must include this oracle after correction.
2. Admission denial/recovery: force the existing operation-memory admission seam to refuse, require retained intent and no producer mutation; recover while A remains private and require one actual start. Clear, replacement identity, active-slot exit, transport Stop and focus controls retain their intended semantics. A filtered empty predecessor cannot erase an already accepted successor; denial with no eventual base remains superseded.
3. Playback: private multi-bar Record source, live successor overdub, alternating production rolling main maintenance and clock/playback ticks inside and across multiple bars. Exact expected MIDI edges, not just adoption/boundary notes; current Capture identity survives ordinary maintenance, stale/transport cases remain rejected. Worker edits limited to playback builder and its prewarm tests.
4. Display: retain a complete qualified base through Record publication gap, append real accepted overdub previews and paint via `resolveDisplayNotes`/draw-source entry. Distinctive live note identity/coordinates must appear; base retained once; repeated paints stable; viewport/source supersession coherent. Establish RED before any display correction, do not assume visual-cache producer tests cover live overlay.

## Controls and coverage

Reuse native clock, allocator/operation admission, MIDI-output capture, and physical-I/O boundaries; no success stubs or copied domain transitions. Explicit event tuples and seeded identities are independent expected output. Existing custody/early-Stop/private-source/empty/undo and moving-window display controls remain enabled. Bounded loops use existing fixture caps and report assertion failure rather than hang.

## Verification recipe

- `IMMEDIATE_HANDOFF_CASE=start pio test -e native -f test_immediate_capture_handoff` (baseline RED already reproduced in preceding investigation).
- `pio test -e native -f test_immediate_capture_handoff -f test_shared_capture_finalization -f test_live_capture_display_boundary -f test_immediate_visual_cache_probe -f test_playback_prewarm`.
- Explicit source/stop/empty and visual-bound profiles as applicable; full `pio test -e native`.
- `pio run -e teensy41-capture-serial`; record RAM1 budget and HEX SHA-256 without changing budget pins.
- One focused independent review of this turn's diff; fix/retest concrete findings only.
- `openspec validate immediate-record-overdub-handoff --strict`; related shipping validation and `git diff --check`.
- Device gates NOT RUN unless separately authorized. Native content/order and firmware build are not physical timing, PSRAM fragmentation, stack high-water, display pixels, or MIDI parity qualification. Existing capture has no build identity and reports ring eviction; no acceptance from missing rows.

## Completion record

- Baseline: branch `feature/unified-rolling-playback-path`, extensive existing dirty work. Immediate Start profile fails with `B ingress waits for private Record publication` (runner also SIGHUP); default handoff18 + visual4 controls pass.
- Firmware mutation: existing-owner corrections applied. New identity-qualified private-base query routes button/manager into normal admission before retaining a denial. Populated live preview no longer loses to a predecessor borrow; paint composes both. Frozen Serving-frame preservation accepts OVERDUBBING with all original qualification checks.
- Discriminating fixtures: Start RED→GREEN and included in default handoff19/19; sealed/live display counterexample expected4/observed3 (A+B, C missing)→GREEN, including exact submission/completion identities; no-first-maintenance-turn display control also PASS. Playback original-predicate RED at tick19212 (`Expected 1 Was 0`), fixed profile4/4. Registered `test_frozen_record_reader_survival` in default suite via existing native boundary table; wrapper4/4 PASS.
- Focused/native: isolated `PLATFORMIO_BUILD_DIR="$PI_SCRATCH_DIR/native-parent-build"` runs handoff19 + shared102 + display13 + visual4 =138/138. Full native before wrapper registration3472total/7skip/3465pass/0fail (355s); registered wrapper additionally4/4. A first concurrent shared-build run failed object rename; isolated rerun recovered. Final full sweep/review pending below.
- Shipping build: `pio run -e teensy41-capture-serial` PASS, RAM1 variables96,192 / code389,736 + padding3,480 / locals34,880; floor840 unchanged,2,640B margin. HEX SHA-256 `6f07d7cb792900e4497c62281444943f9e4c6809373f27bb3da208f058c47327`. No object/RAM1 state additions; accessor methods remain flash-resident.
- Device: NOT RUN, no upload or SD operation authorized by this packet.
- Review and durable documentation: one focused independent review found two concrete preview defects; both red-proved, fixed and retested (below). Authority, decision migration, tasks, runtime and delivery tracking updated; no new architectural decision or second review round.
- Checkoff: no broad hardware gate eligible; only explicitly verified regression tasks may be checked.

### Coverage limits

Playback integration checks exact Record edges with an active empty successor, then proves successor ingress;
it does not qualify concurrent nonempty-overlay output, full-loop wrap or final committed-root return.
Source/stop/empty semantic controls remain distinct. Existing publication/visual work budgets were not widened.
Resource-denied recovery remains subordinate to the admitted stop owner in production dispatch; direct
interaction-driver fixture proves its admission is no longer intrinsically gated on publication, not a
new latency guarantee under exhaustion. The installed image is unchanged and this manual log cannot pin it.
No new decision or exclusion was invented: these are verification limits, with device acceptance still open.

### One focused review and concrete repairs

Reviewer requested changes for two reachable presentation defects: after C stops, its borrow replaces
B's still-unpublished preview; and overview density drops predecessor bands when the accessor switches
to a populated live preview. Added counterexamples before correction: detail expected4/observed3 at
C Stop, overview predecessor mask expected16/observed0 when live input arrives (both RED).
Repair: Track exposes synchronous identity-qualified preview reads over the live stop-ring window,
without fields/resources, allocations or content scans. Detail composes existing previews oldest-first
then live; overview incrementally folds the ordered preview set with existing mask/cursor and resets
on preview identity/replacement changes. The temporary single-finalizing-preview accessor was removed.
Focused post-review native139/139 (handoff19/shared102/display14/frozen-reader4). Both REDs now GREEN.
No second review pass. Final verification below includes both repairs.

### Final verification (post-review source)

- `PLATFORMIO_BUILD_DIR="$PI_SCRATCH_DIR/native-parent-build" pio test -e native`: **3477 total / 7 skipped / 3470 succeeded / 0 failed**, 327s. Includes normally registered frozen-reader suite4/4 and display14/14. Log: session scratch `regression-final-native.log`.
- `pio run -e teensy41-capture-serial`: **SUCCESS**,45.89s. RAM1 variables96,192 / code389,736 + padding3,480 / locals34,880, floor840 unchanged (2,640B margin); RAM2 variables168,576, EXTRAM variables11,680. Target methods remain flash-resident; no Track/Loop state-size addition. Log: session scratch `regression-final-firmware.log`.
- Final HEX SHA-256 **`1131e857d6f5a578640095afa3f8942d0e06a6989675cfdc4096cfce22766128`**. Earlier6f07d7cb hash above belongs to the pre-review build and is not the deliverable.
- Both active changes pass `openspec validate <change> --strict`; `git diff --check` clean; `ram1_budget.json` unchanged. Registered stimuli listed read-only; no stimulus modified or hardware run.
- No upload, reset, device-content/SD operation, commit or push. Original dirty work preserved on `feature/unified-rolling-playback-path`.
- Closeout: actual source behavior documented; DEC-158 queued-only policy annotated as superseded by ratified DEC-159/DEC-160 application. No new ownership/lifetime/resource contract or overlap/ledger algorithm change; no additional decision or evaluation-guide change required. Device acceptance and other pre-existing OpenSpec tasks remain open; not archived.

### Subsequent approved upload

User approved installing the exact final image. CLI2.3 completed Programming/Booting; fresh serial
crash0/loadok and Teensy MIDI USB independently verified. Initial STOPPED,pending0,queue0/4. Numeric
loader exit unavailable due detached launch; later serial contention limits final drain/transport
assertions. No content stimulus or explicit SD mutation commanded. This supersedes the historical
NOTuploaded statements above, not the outstanding behavioral device gates. [Upload receipt](2026-10-26-UPLOAD.md).
