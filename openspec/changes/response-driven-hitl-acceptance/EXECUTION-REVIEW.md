# Execution review

## Slice 1 — production input and historical evidence fixtures

Status: native verified. Implements tasks 1.2 and 1.3; task 1.1's
complete instrumentation-owner contract remains open until every field is pinned.
Authority: design decisions 2, 5 and 7; DEC-092 — response-driven HITL.
No firmware behavior edits, device access or content mutation in this slice.

### Pre-implementation review

Existing owners reused: production `MidiButtonProcessor`, `MidiButtonConfig::loadConfiguration`,
production native Logger boundary, existing `test_support/firmware/Arduino.h` clock seam.
The native clock already supports `nativeMillisSet/Advance`; the old momentary clear fixture
is not a limitation of today's hardware boundary. No new timing implementation is needed.
The callback records recognized gestures only; it does not impersonate action execution.
USB/DROID dispatch is covered by the separate production route fixture documented below.

Dependency closure: processor + production mapping + Logger, native Arduino headers; no
Track transport or storage owner is replaced to make this processor test link. Existing
Python oracle reads attributed excerpts of failed captures. Preserve current dirty edits.
Commit boundary: independently runnable fixtures and provenance; no runtime changes.

### Fixed transition oracle

- Short: note-on at t, note-off at t+120, no recognition before release+300;
  after processor update at the expiry, exactly one short callback on the mapped channel/note.
- Early second tap: same-control release inside 300 ms yields pending double, then double
  after its 400 ms triple window. It must never be counted as two accepted short gestures.
- Third tap: third release inside the triple window yields one triple callback.
- Clear: release at 599 ms is short after timeout; release at 600 ms is long once;
  held input alone does not synthesize clear. Orphan release causes no action.
- Interleaving: a record control between two loop-control edges cannot discharge the loop's
  pending classification. Each control keeps its own identity.
- Repetition: after observed short completion, another legal short produces one new callback.
- Failure corpus: previous queue/reaffirmation traces and synthetic contamination/wrong identity,
  duplicate stop/stale boot/missing metadata cannot return PASS. Missing source identity is
  retained as unknown, never invented by fixture extraction.

Expected thresholds come from `ButtonConfig` defaults and the production DROID mapping.
Mutations: replace production timing with momentary dispatch; prematurely assume release means
recognition. Both must fail assertions. No wall-clock sleeping in the native fixture.

### Verification recipe

`pio test -e native -f test_midi_button_timing` and focused Python accepted-sequence corpus tests.
Run full native after the fixture is stable. Baseline: existing working tree at slice entry;
no new build/upload is needed for test-only code. Hardware/content acceptance NOT RUN.

### Completion record

Pending actual results. No implementation task checked off by this packet alone.


## Instrumentation prerequisite — BLOCKED on record overlap contract

Trace verified against `TrackManager::finalizeCaptureAndSelectSlot`,
`Track::stopRecordingToStopped`, `Track::commitCaptureForStop`,
`Loop::commitCapturePass`, `Track::beginDeferredOverdubStopForSlot` and
`TrackPreparedStopCommit`: record publishes inline, while the specified capture-stop
ownership window and release belong to the overdub job. Record's queued persistence and
derived-view work must not be renamed to make the same oracle pass. The design records
this finding and the pending user choice. Task 1.1 remains open; no instrumentation edit.

Native route boundary note: `MidiHandler::handleMidiMessage` is real for SOURCE_USB and
SOURCE_USB_HOST, followed by real manager, processor, mapping and clear action. The unrelated
bar/step classifier permits only musical channel 1 and the exact mapped Record control;
bar/step execution and loop layering remain fail-closed. This proves common dispatch parity,
not physical USBHost driver delivery or real-device clear durability.


## Slice 1 completion — 2026-09-27

Baseline HEAD `c4e130d5`, branch `feature/runtime-working-set-ownership`, prior dirty
accepted-sequence driver/oracle/tests and planning documents preserved. No firmware source edits.

- Historical corpus: four immutable excerpts, 252 selected lines, original file SHA256 and
  line numbers plus excerpt hashes in `scripts/hitl/fixtures/accepted_sequence/manifest.json`.
  Source metadata is absent and remains absent. No invented firmware identity.
- `.venv/bin/python -m unittest discover -s scripts -p 'test_accepted_sequence*.py'`:
  **33 passed**, including corpus, seed contamination and unattributable/malformed chains.
- `pio test -e native -f test_midi_button_timing`: production processor/mapping fixture,
  ultimately **12 passed** in the full native run; the initial 10-case focused run passed too.
- `pio test -e native -f test_drain_reentry_callback_safety`: **10 passed**, including
  two new real USB/DROID dispatch-to-clear controls. Native memory-backed storage only.
- Mutation: temporarily changed the fixture's loaded loop mapping to momentary dispatch;
  **11 assertions failed**, including early recognition, short/double/triple/long and repeat
  counts. Restored the file before the full run. No production file mutated.
- `pio test -e native`: **2,486 passed, 7 skipped**, 2,493 cases; 82.721 seconds.
  The seven skips remain skips; no new required case was skipped.
- Strict validation of the response-driven change: PASS. `git diff --check`: PASS.
- Firmware build/RAM1: NOT APPLICABLE to test-only slice. Device, content parity and timing:
  NOT RUN. No clear, upload, reset, commit, push or PR.

Task 1.2 and 1.3 eligible for checkoff. Task 1.1 and firmware instrumentation remain blocked
on the record-overlap contract; the user choice is pending. This is not acceptance closure.


## Contract direction resolved (2026-09-27)

The user requests a shared firmware implementation contract. Audit of the runtime OpenSpec
shows shared capture finalization was already intended, including mixed record-A → overdub-B
in design Revision 23. Runtime `SHARED-CAPTURE-FINALIZATION.md`, Revision 25 and tasks
S4f.15–S4f.22 close the specification gap. The pending separate-oracle choice above is superseded;
retain both full overlap oracles. Firmware implementation and device proof remain outstanding.
Task 1.1 can now pin instrumentation against that common-mode contract; its checkbox remains
open because the observation execution packet itself has not been completed.


## Host readiness re-verified and hardware leg blocked (2026-09-28)

The shared firmware contract advanced in the runtime change: `RecordPreparationCursor`
now composes the bounded Record preparation (canonical order, wrap repair, minimum-length
policy) over one private prepared store. See
[runtime composition evidence](../runtime-working-set-ownership/S4F18-COMPOSITION-EXECUTION.md).
Record stop is still inline and no observation instrumentation exists yet, so task 1.1
remains open and task 2.x is not startable against production behavior.

Host-side acceptance tooling re-verified on this branch (no device access, no SD action):

- `.venv/bin/python scripts/test_accepted_sequence.py`: **30 passed**.
- `.venv/bin/python scripts/test_accepted_sequence_corpus.py`: **3 passed**.
- `.venv/bin/python scripts/test_retention_handoff_plan.py`: **23 passed**.
- `.venv/bin/python scripts/test_device_shutdown.py`: **15 passed** (exit 0).
- `.venv/bin/python scripts/host_midi_hitl.py stimulus list`: the registered baseline
  `STIM-BASE-2-ONE-OD-01@v1` and the existing stimuli resolve; no temporary variant added.
- `pio test -e native`: **2,571 passed / 7 skipped** (includes the new preparation suite).
- Strict validation of this change and the runtime change: PASS.

The device-dependent leg is **BLOCKED, not failing**: no `/dev/cu.usbmodem*` is present in
this session, so the identified capture-serial image cannot be uploaded and the
response-driven acceptance cannot be measured. Tasks 4.x–6.x remain unstarted; no manual
golden pass and no acceptance criterion has been relaxed. An upload/device leg requires the
board to be connected and the user's authorization in the same session.


## Task 2.1 investigation — RESOLVED: the observation serves; the ring hid it (2026-09-28)

Superseded and corrected within the same session. The earlier "does not serve" reading was wrong:
it was a capture-ring visibility artifact, and a read-only diagnostic proved it.

- Added `!LIB_OBSERVE_STATUS` (stopped-only, emitted **directly**, bypassing the capture ring, like
  the existing identity-path stats). It prints the whole `LibraryObservationView`.
- On device, after `!LIB_OBSERVE` the counters read
  `req=0,ready=1,phase=3,tr=8,ts=0,turns=64,restarts=0,collected=64,emitted=64,startUs=22907457,endUs=23469348`
  — the observation **completed** (phase 3 = Done, 64/64 slots collected and emitted) in ~0.56 s.
  The same read also returned the 66-row `LIBOBS,B … LIBOBS,S×64 … LIBOBS,E` family.
- So the family is emitted through the capture ring and can sit unflushed on an idle device; the
  earlier negative runs were visibility, not serving. The host now drains while it waits
  (`ManagedCaptureSession.drain_capture_ring()` every 2 s inside `library_observation`) — a
  read-only, anonymous helper that does not consume the outstanding-request slot.
- Verified end to end through the managed capture: `closed=True, rows=131, slots=64`, with the
  real per-slot rows (`LIBOBS,S,t=…,s=…,k=1,kp=1,…,l=15360,v=729`). This is the read-only census
  task 4.1's target selection needs.
- Lesson recorded for the next session: **absence of a row is not evidence of absence** when the
  producer writes through the capture ring; use the direct diagnostic before concluding a path is
  not serving.

Task 2.1's bounded on-demand observation and its coherent 64-slot snapshot are therefore
device-confirmed as *available*; what remains for 2.1 is the acceptance-level work (malformed/busy
requests, disabled capture, partial writes, stalled reader and per-turn bounds in native tests,
plus the identity-snapshot correlation that task 2.2 builds on).


## Slice 4.1 — census-driven preparation and a verified no-manual-step run (2026-09-28)

Status: the preparation half of 4.1 is complete and device-verified; the acceptance gates
remain open. Builds on the transport (2.3) and the device-confirmed observation (2.1).

What landed:

- `library_observation.occupied_slots(lines)`: the read-only per-slot occupancy map from one
  **complete** observation, using the device classifier's union content signal; an incomplete
  window returns `None`, so "unknown" can never be read as "empty".
- The reproducer runs the census before preparing (`_run_census` through the single serial
  owner), records it in the report, and uses it to decide preparation: an empty target skips
  the clear entirely; a **filled** target is selected first (the target's own loop-button
  gesture) and then long-pressed to clear.
- `--clear-attempts` / `--clear-retry-delay-s`: a `REFUSED`/`ABORTED` clear is the firmware's
  explicit "deferred save in flight" refusal, so the driver waits for it to quiesce and
  retries the gesture a bounded number of times instead of failing the leg.

Device run (port `/dev/cu.usbmodem154944801`, track 8, A=4, B=5, `--no-upload`, no manual
step, no reboot): census `slots=64, occupied=61`; slot 5 `CLEARED` on attempt 0; slot 4
`ABORTED` on attempt 0 (`Clear aborted: could not complete deferred save first (none)`) and
`CLEARED` on attempt 1; both seeds committed; `result: DROVE_LEG`, exit 0. Bundle:
`captures/hitl_accepted_sequence/20260928-od-census2/`.

Correction to an earlier claim: the census reports **61 of 64 slots occupied**, so the
round-4 reading that slot 5 was empty (from `k=0,l=0,v=0`) was wrong — the device's union
content signal is the authority, and slot 5 was filled and cleared. This is why 4.1 must use
the classifier's signal rather than a hand-read field.

Oracle verdict on that run: `SETUP_INCOMPLETE`, with the causes named rather than waived —
no fresh boot banner (`--no-upload` attached to a running device, so not verdict-grade), two
`RING,shed` rows, and the overdub interval-identity links still unknown (task 2.2). The links
the oracle *can* prove are green: `L2_no_capacity_denial`, `L3_a_finalization_window`
(device-timestamped), `L4_a_capacity_release`.

Remaining for 4.1: a fresh-boot, ring-shed-free run of both legs plus the mixed
record-A → overdub-B sequence, and 2.2's correlated overdub identity to close L1/L5/L6/L7.


## Final round — fresh-boot run and the fresh_boot sequencing gap (2026-09-28)

Second device run, this time with `--upload` so a real flash reset precedes the leg (track 8,
A=4, B=5, no manual step): census `slots=64, occupied=29`, slot 5 `CLEARED` on attempt 0, slot 4
`REFUSED` on attempt 0 with the named blocker
`Clear refused: deferred save temporarily blocked by post_load_holdoff; slot left untouched,
save still owed`, then `CLEARED` on attempt 1, both seeds committed, `result: DROVE_LEG`,
exit 0. Bundle `captures/hitl_accepted_sequence/20260928-od-fresh/`. So the bounded retry covers
the firmware's real refusal vocabulary (`post_load_holdoff` and the earlier `(none)`), and the
clear never mutates a slot it refused.

Oracle: still `SETUP_INCOMPLETE`, now for three structural reasons rather than the leg's:
1. `fresh_boot` **FAIL** even after a genuine flash. The oracle requires a boot banner in the
   **first 80 lines**, but the driver flashes first and attaches the managed capture afterwards,
   so the reset happens before any owner is reading. `capture_session.py`'s "Teensy resets when
   serial opens" does not reliably hold for an already-enumerated board, and there is **no reset
   command** in the HITL serial surface. Fix direction (next slice): start the managed capture
   first and trigger the reset while it is attached, or add a minimal stopped-only reset command
   to the allowlist; either way the boot banner must land inside the attached window.
2. `overflow_free` **FAIL** — one `#CAP,…,RING,shed,1` row. Ring shedding during preparation is
   telemetry pressure, not a leg defect, but the gate is absolute; the run must be shed-free.
3. `L1/L5/L6/L7` and `overdub_interval_identity` unknown — task 2.2's correlated overdub
   start/accepted-stop/retirement evidence, unchanged and still the largest open HITL item.

Green on the same run: `L2_no_capacity_denial`, `L3_a_finalization_window`,
`L4_a_capacity_released`. Nothing is waived: the leg drove, and the verdict stays non-PASS.

## Goal-round closeout (2026-09-28, round 12/12)

Whole-tree state at closeout: focused shared-finalization/preparation/staging **57/57**; host
HITL suites 46/23/23/15; full `pio test -e native` **2,585 passed / 7 skipped** (2,592);
`pio run -e teensy41-capture-serial` PASS with RAM1 padding **840**; both OpenSpec changes
strict-valid; whitespace clean; no leftover mutation; nothing committed (owner's standing
choice). The objective is **not** complete and the remaining work is enumerated below.

Firmware (`runtime-working-set-ownership`): S4f.18's bounded Record preparation is complete and
verified — canonical ordering, identity completion, union workspace, source staging, closure
merge, per-track preparation role, pass transfer — as is the S4f.20 publication/retirement with
job-owned geometry. Open: S4f.19 (first-bar/onward playback handoff), S4f.20's three named
residuals (deferred revts validation, `invalidateCaches()` parity, the production Record bind
site), S4f.17 production Record admission, S4f.21 caller migration, S4f.22 integration gates,
and the S4f.13 device acceptance.

HITL (`response-driven-hitl-acceptance`): stage 1 complete; 2.3 complete; 2.1's observation and
64-slot snapshot device-confirmed with acceptance coverage open; 4.1's preparation half complete
and device-verified. Open: 2.2 identity correlation, 2.1 acceptance coverage, the 4.1
fresh-boot/shed-free both-leg and mixed-sequence runs, 5.x oracle integration and 6.x
qualification.


## Slice 2.3 — allowlisted command forwarding (2026-09-28)

Host-verified (23/23) and device-confirmed at the transport boundary. The capture session
now forwards allowlisted read-only requests on stdin, fenced by two 32-char tokens
(device-learned constraint), and `serial/protocol.py` decodes the receipt and the
self-delimiting observation family once. A first device attempt used 8-char tokens and the
firmware silently ignored both fences; the corrected framing is acknowledged in the log.
The `LIBOBS` family did not settle within 60 s on an idle, long-running boot, so the decoder
reported loss rather than an empty census — observation serving remains task 2.1/2.2 and is
the next HITL slice. Firmware sources are untouched; build and full native re-ran green.
[Evidence](COMMAND-FORWARDING-EXECUTION.md).


## Slice 4.1 (partial) — scoped stopped preparation, device-exercised (2026-09-28)

Status: scoped clear implemented, integrated and exercised on hardware. The
target-selection step and the RAM/SD census + persistence verification remain open, so
task 4.1 is **not** checked off.

Implemented: `scripts/hitl/flows/stopped_preparation.py` owns one authorized target's
clear through the production loop-button long press, correlated only by the firmware's own
evidence. `classify_clear_evidence` distinguishes `CLEARED` (evidence names the requested
slot), `ALREADY_EMPTY` (recognized long press, empty target's deliberate silent no-op),
`REFUSED` (`Clear refused: deferred save temporarily blocked`), `ABORTED`, `NOT_SELECTED`
(a switch row, i.e. the target was not the selected slot) and `UNCONFIRMED`; only the first
two are success. `clear_slot` refuses a press shorter than the long-press discriminator, so
a short (record) gesture can never masquerade as a clear.

Integrated: `scripts/hitl_accepted_sequence.py` runs `clear_slot` after the track select
and the transport stop, before seeding each target (`--clear-targets`, default on;
`--clear-timeout-s`; `--no-clear-targets` for observation-only). Preparation outcomes are
recorded in the report and a non-success raises `PreparationFailure`, so the leg stops
before any record gesture touches the slot. TEMP variants
`STIM-BASE-2-ONE-OD-01-TEMP-ACCEPTED-SEQ-{REC,OD}` were written through the sanctioned
`stimulus temp` mechanism under `captures/stimuli/`; deliberately unregistered. No reboot
and no manual clear prepare a repeat.

Host verification: `scripts/test_accepted_sequence.py` **42 passed** (12 new: evidence
classification, the long-press guard, the empty no-op, the unselected-switch refusal, and
two managed-run integration cases proving the clear precedes each seed and that a refused
clear produces `PREPARATION_FAILED` with zero record presses). Corpus **3**, retention
plan **23**, device shutdown **15**; full native **2,571 passed / 7 skipped**. Strict
validation passes.

Device verification (port `/dev/cu.usbmodem154944801`, image `git_commit bed90c04` + dirty
tree, `firmware.hex` sha256 `451000aa8de422b2c2499879660c02eedd29f8be883c957265b288a0360a602f`,
RAM1 padding 840, no re-upload between legs):

- **Record leg** (track 8, A=4, B=5): prep `CLEARED` with
  `MIDI: Clear selected slot 4`; seed committed; leg drove; device shutdown complete
  (2 verified drains). Oracle verdict `SETUP_INCOMPLETE` — no fresh boot banner — with
  L4 (A release) and L5 (successor start) failing: the known inline-record gap, not a
  staging problem. Bundle: `captures/hitl_accepted_sequence/20260928-rec/`.
- **Overdub leg**: prep `ALREADY_EMPTY` on the empty slot 5 (recognized long press, empty
  no-op), then `NOT_SELECTED` on populated slot 4
  (`Loop 4: long-press start (not playing)`) and `PREPARATION_FAILED` **before** any record
  gesture; shutdown complete. Bundle: `captures/hitl_accepted_sequence/20260928-od/`.

Findings that bound task 4.1 (measured, not inferred):

- The firmware clears `CLEAR_TRACK_FOR_SLOT` only when the target is the **selected** slot;
  otherwise the long press switches playback, and an empty selected target is a deliberate
  silent no-op (there is no `Clear ignored` row on this route).
- A loop-button **short** press is a record toggle, not a harmless select: on an empty
  target it armed and started the transport (`Armed for record — starting transport`,
  `RECA,4`). It therefore cannot be used as a "select the slot" step.
- Two presses on the same button inside the firmware tap window degrade the second press:
  a requested 900 ms hold measured 458 ms and was classified short. Any future select step
  must respect the tap window.
- Consequence: clearing a populated target that is not the selected slot needs a real
  target-selection step (or a read-only census plus the documented operator selection)
  before the long press. That is the remaining 4.1 work, together with the RAM/SD census
  and persistence-completion proof. This slice is fail-safe in the meantime: an unselected
  or unconfirmed target aborts before mutation.
