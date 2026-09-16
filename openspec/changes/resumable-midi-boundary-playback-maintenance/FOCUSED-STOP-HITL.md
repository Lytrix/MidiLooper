# Focused transport-Stop HITL checkpoint

Status: boot recovery and normal stopping observed; single-track transport-Stop interruption and continued silence PASS on 2026-09-14. Restart survival PASS after the 2026-09-14 publication repair (three complete 49-note cycles). The forced four-bar wrap-adjacent run reached deferred stopping and passed after the 2026-09-15 transport output-barrier repair. Layered playback and full timing acceptance remain open. This is a regression check of the production Stop emission guard, not completion of the joint canonical/bridge/undo feature.
Tasks 2.3, 2.4, 2.5, 3.3, 3.4, 4.1, 5.3 and 5.4 remain open.

## Why a device check is useful now

`Track::playMidiEvents` and `playMidiEventsForSlot` now reject emission when a
pending stop's destination is STOPPED. The administrative state deliberately
remains OVERDUB_STOPPING while frozen capture finishes. Global transport Stop
still sends All Notes Off immediately. Check on the device that subsequent
clock service cannot restart notes through either playback entry point.
Native tests prove the destination policy and frozen command-epoch recovery;
they do not prove USB MIDI output timing or the physical control interaction.

The new protected undo publication, bounded disabled-pass reclamation and
session-close continuations are not yet connected to the production stop
pipeline. This checkpoint provides no device evidence for those helpers or for
the provisional 6,240 ranked-grain allowance.

## Preconditions

- Connect Teensy and verify its current serial and MIDI ports. On 2026-09-14,
  `pio device list` exposed no Teensy serial port.
- Obtain explicit upload confirmation, as required by
  `docs/Authority/DELIVERY_RULES.md` (Teensy upload gate).
- Use a user-approved scratch track/slot and preserve unrelated material.
- Use the built `teensy41-capture-serial` firmware with DMA display enabled.
  Do not infer the flashed revision from a successful host build.
- Use `.venv/bin/python`; the system Python lacks pyserial. Keep one serial
  capture owner, retain the complete log and monitor actual USB MIDI output.

## Procedure

1. Use the existing STIM-BASE-2-ONE-OD-01 seed/one-overdub recipe, or the standard
   manual record/overdub control flow on the approved scratch slot. Retain the
   actual stimulus identity; the intervention below is additional coverage.
2. Record a repeating phrase and start overdubbing. Include a held capture note.
3. Request normal overdub stop. Confirm the fresh serial transition
   OVERDUBBING -> OVERDUB_STOPPING before injecting global transport Stop.
   A legacy direct transition or an intervention after PLAYING does not cover
   this test. Repeat only after evidence shows the intended interval was missed.
4. Record the Stop control action and its resulting All Notes Off/closure output.
   Continue observing through STOPPED and several former loop cycles, without
   sending another Play command. Check that no subsequent playback NoteOn is
   emitted, no return to PLAYING occurs, and the held note is silenced.
5. Restart playback explicitly after cleanup. Verify the accepted overdub notes
   survived and are neither duplicated nor missing. Check normal stop-to-PLAYING
   separately to ensure that the Stop-only guard does not mute that destination.
6. Repeat with layered playback so the slot playback entry point is covered.

## Evidence and result

Keep the firmware identity, ports, target track/slot, intervention timestamp,
complete serial log and USB MIDI output trace. Require the explicit stopping
transition, STOPPED destination, immediate silence and no later NoteOn before
an explicit restart. Score stopping and subsequent playback separately with the
existing MIDI-boundary report. Reject overflow in scored intervals. Missing
output capture, lost/rotated serial, a reset or a missed stopping interval makes
this INCONCLUSIVE_CAPTURE, not PASS. Do not use this result to close task 5.3.

## Remaining implementation after this checkpoint

Production integration still needs protected-stack reader/writer exclusion,
reserved snapshot-after attachments and snapshot-exit queue work, full cleanup
and admission accounting, empty-final-capture handling, and a bridge completion
proof while normal playback changes the ledger. The active rebuild/load-commit
and full telemetry gates also remain open. No compatibility path is removed.

## 2026-09-14 attempted device run

User approved capture-firmware upload and track 5/slot 1. Upload succeeded.
Normal stop preflight aborted before any record/overdub transition. The fresh
serial log reports an 8 MB PSRAM chip with 0 KB free, followed by
`ExternalMemoryFirstAllocator` fatal exhaustion of external memory and internal
heap. A device reconnect occurred. This is a firmware startup blocker observed
on hardware, not evidence for or against the Stop emission guard.

The host runner also bound the legacy baseline to the previous day's default
`captures/.current_session` target despite this run using a custom output
directory. Do not use that report's stale verification counters. Correct the
binding to the exact managed log before rerunning; do not retry transport
controls on stale evidence. No workspace quarantine or unrelated-slot clear was
performed. Capture processes have stopped.

Evidence: `captures/resumable-stop-hitl-normal/focused-result.json`,
`runner.log`, `midi-output.jsonl`, and `session_20260914_221243.log`.
No HITL gate is closed. Full bridge integration and the 6,240-grain device
reservation/timing determination remain open.

## Boot recovery and retest (2026-09-14)

The user confirmed the device had hung on its load title screen. Eager boot
prewarm had filled PSRAM with eight full per-slot view/bridge/companion bundles
before loading saved content. Separating only companion workers was insufficient;
full view/bridge buffers still consumed PSRAM. The final fix reserves only the
existing capture staging at boot. Full view/bridge/companion allocation remains
an explicit cold prewarm. Staging-only capture ordering can complete, but it
cannot pass full bridge selection. Upgrading while staging contains accepted
rows or frozen/open-note state is denied before metadata reset.

Device evidence after upload: PSRAM **4,401 KB free**, internal heap **344 KB
free** at the startup memory sample; selected `LoadLoopJob done 4/0` follows.
The user confirmed return to the normal interface. No workspace quarantine or
unrelated saved-loop deletion was used. The final firmware SHA-256 is recorded
in `captures/resumable-boot-fix/result.json`.

Managed capture now passes the exact new log path and custom output directory
to the legacy runner. Its JSON report also serializes that path. Host integrity
checks: 13 passed; serial timing checks: 22 passed. Native: 2,077 passed, 1 skipped.
Firmware: RAM1 code 425,784, variables 91,808, locals 6,496 bytes; flash code
945,904 bytes. Full new-path memory admission across multiple simultaneously
prepared slots remains an open integration requirement; boot success does not
prove that all slots can own full bundles concurrently.

The normal retry recorded OVERDUBBING -> OVERDUB_STOPPING at 80,170,050 us and
OVERDUB_STOPPING -> PLAYING at 80,179,341 us (9,291 us for this short input).
Verify-only replay passed its baseline verifier; this is not comprehensive
feature or worst-case latency acceptance. The first interruption attempt did
not arm because serial buffering delayed its state observation. The second
attempt was aborted before record when firmware refused a clear while deferred
save was pending (drain cap 256, estimate 2,352 slice steps). Neither attempt
sent the timed interruption, so transport-Stop-during-stopping and layered
coverage remain unverified. A MIDI Stop was sent after abort; the settling log
is preserved. Do not bypass the clear refusal or mark task 5.3 complete.

## Timing relative to MIDI boundaries (2026-09-14)

Source: `captures/resumable-stop-hitl-retry2/session_20260914_222604.log`
(device microsecond timestamps), with nominal tempo from that directory's
`runner.log`. At nominal 120 BPM, a quarter note is 500 ms, an internal tick
at 192 PPQN is 2.604167 ms, a MIDI clock interval at 24 PPQN is 20.833333 ms,
and a sixteenth note is 125 ms. These are reference intervals, not measured
available slack. Internal resolution is defined in `include/Globals.h`.

The normal stop sample contains:

- 80,170,050 us: OVERDUBBING -> OVERDUB_STOPPING (relative time 0).
- 80,179,309 us: `MO,176,5,123,0`, All Notes Off, +9.259 ms.
- 80,179,341 us: OVERDUB_STOPPING -> PLAYING, +9.291 ms.

The stopping state lasts 9.291 ms: approximately 3.568 internal ticks,
0.446 MIDI clock intervals, or 7.43% of a sixteenth note. This is elapsed
state-transition latency for one short input, not a single-grain execution
cost, worst-case stop bound, or measurement of the complete new stop pipeline.
The later duplicate ST lines are not separate stop intervals.

Nearby, before this stopping interval:

- At 80,109,316 us, `DIAG,wrap,total=31690` reports 31.690 ms of wrap work,
  approximately 12.169 internal ticks or 1.521 MIDI clock intervals.
- At 80,127,593 us, `DIAG,late_event,clk,25774,4616` reports 25.774 ms
  clock lateness, approximately 9.897 internal ticks or 1.237 MIDI clock
  intervals. Preserve this as an observed lateness sample; temporal proximity
  alone does not attribute it to one wrap child, order-build or load-frame grain.

The runner explicitly used a wall-clock tempo proxy for bar synchronization.
This capture does not establish synchronized clock-edge/due-event timestamps
around the stop interval. A 9.291 ms duration being shorter than 20.833333 ms
does not prove completion before the next clock edge, nor before an internal
tick or due NoteOn/NoteOff. The BAR notification is not an exact clock-edge
anchor. No boundary deadline PASS or policy ceiling follows from these ratios.

For tasks 4.1 and 5.3, retain device-clock-aligned due/service/emission timing,
owner/grain start and end, and state transitions in overflow-free scored
intervals. Establish remaining slack at each admitted grain and attribute
lateness separately in OVERDUBBING, OVERDUB_STOPPING and subsequent PLAYING.
Repeat the transport-Stop interruption with an intervention proven inside the
stopping interval after the deferred-save setup blocker is resolved.
The provisional 6,240 ranked preparation grains remain unchanged; this sample
does not measure their total execution time or establish real-time reservations.

## Deadline correlation instrumentation (2026-09-14)

Task 4.1 now retains send, cadence-derived due, and deadline microseconds in the
existing rate-limited `DIAG,late_event` row, after the legacy class/lateness/tick
fields. The leading CAP timestamp remains diagnostic emission time. Host reports
attribute valid retained samples by send time, including micros wrap, and leave
legacy/inconsistent samples unattributed. Sparse samples never prove deadline
safety by absence. Firmware lateness tolerance is unchanged.

`DIAG,stop_phase` aggregates every production stopping turn by Track, slot,
frozen generation, command epoch, destination and phase. Fields after the tag:
`track,slot,phase,generation,epoch,destination,calls,crossed,unknown,startUs,endUs,slackUs`.
Start/end/slack describe the maximum-duration call; crossed and unknown count all
calls in the group. A phase/identity change flushes the group. Phase values follow
`PendingStopPhase` (0 closure, 1 canonical order, 2 canonical rewrite, 3 audible
order, 4 transfer preparation, 5 atomic transfer, 6 cleanup). Destination 0 means
PLAYING, 1 STOPPED. Slack UINT32_MAX denotes unknown cadence. Crossing is elapsed
call time greater than entry slack to the next known service deadline; this is
not proof the call blocked service (interrupts may service MIDI during it).
A missing/incomplete phase summary cannot prove progress or completion. Formatting
is outside measured work, capture-only, and flash-resident. Overflow invalidates
scored evidence. Full load/order owner, outcome, source/ledger and reservation
telemetry and full task 4.1 acceptance remain open.

The clear-setup investigation found persistence returns without work while
transport is active; the prior settling log confirms PLAYING until the explicit
Stop. The host precondition now also recognizes recent sequencer activity,
checks activity after its stop press, and blocks clear on unresolved stuck
persistence or save-wait timeout. It retains evidence from the stop wait and
never bypasses the firmware save-before-clear guard. Device confirmation pending.

### Device-clock stop timeline instrumentation (2026-09-15)

Capture firmware now emits an exact `DIAG,stop_grain` row for every admitted
`OVERDUB_STOPPING` finalization grain. It retains Track, slot, phase, frozen
generation, command epoch, requested destination, the preceding MIDI-service
sequence, grain start/end, actual service instant, cadence-derived due and next
deadline, period, entry slack, and phase-complete flag. The existing
`DIAG,stop_phase` aggregate remains unchanged for compatibility.

While a stop trace is active, timing-critical code places the following actual
device-clock records into a fixed 32-record queue; only the main loop formats
them as capture lines: `DIAG,stop_service` (sequence, actual, due, next
deadline, period) and `DIAG,stop_emit` (sequence, NoteOn/Off, tick, send, due,
deadline, lateness). `DIAG,stop_trace_drop` makes any queue loss explicit.
The host report rejects dropped, malformed, or uncorrelated records. The CAP
timestamp remains flush time and is never used as a service or emission time.

This establishes the stop-grain/service/emission correlation required for the
next focused HITL. It does not complete Task 4.1: owner/outcome and complete
capture-reservation, source/ledger, bridge, coverage, and load-job attribution
remain to be added with their respective integrated paths.

### Device results for the correlation checkpoint

Capture firmware uploaded successfully. Native: **2,080 passed, 1 skipped**;
focused telemetry/tier: 42 passed; host timing: 26 passed; clear preconditions:
25 passed; capture integrity: 13 passed. Capture build RAM1 code 425,080,
variables 91,808, locals 6,496 bytes; flash code 944,984 bytes.

- `captures/resumable-stop-hitl-deadlines`: baseline runner PASS, but direct
  OVERDUBBING -> PLAYING, so no deferred-stop acceptance. New retained timestamps
  attribute a 24,678 us clock-late sample to OVERDUBBING and a 26,049 us NoteOff-late
  sample to PLAYING. These are reported deadline misses, not timing acceptance.
- `captures/resumable-stop-hitl-held-normal`: temporary held-note hook never
  fired; direct stop, no deferred-stop evidence. Do not claim the intended
  held-note stimulus was delivered.
- `captures/resumable-stop-hitl-held-observed`: temporary held-note hook also
  did not fire, but the actual baseline input produced OVERDUB_STOPPING from
  202,305,139 to 202,402,899 us (**97,760 us**). Six stop-phase summaries were
  retained. Canonical order: 2 calls, 1 crossing; maximum call 248 us with 33 us
  entry slack. Canonical rewrite: 4 calls, maximum 77 us; audible order: 2 calls,
  maximum 170 us; transfer preparation: 37 calls, maximum 54 us; atomic transfer:
  1 call, 25 us; cleanup: 1 call, 106 us. Other groups report zero crossings and
  all report known cadence. No retained late-event or RING overflow rows occur
  in this log; this does not prove exhaustive deadline safety. Baseline result
  is INCONCLUSIVE_CAPTURE (record unmatched open notes), so use only as partial
  stopping timing evidence. No held-note preservation claim is supported.
- `captures/resumable-stop-hitl-interrupt-deadlines`: timed intervention did not
  fire; baseline INCONCLUSIVE_CAPTURE with missing transition evidence. No
  transport-Stop-during-stopping result. A separate raw MIDI Stop and settling
  capture follow, after the managed capture closes.

Per-run `timing-correlation.json` reports retain parsed deadline/phase samples
for the first three runs. All scratch setup clears completed without the prior
persistence refusal. This demonstrates successful setup in these runs, not a
proof that all deferred-save cases are resolved. The temporary intervention
fixture must reliably send and verify Stop inside the stopping interval before
this checkpoint can close. Delayed serial evidence and legacy proxy confirmations
must not substitute for that proof. Full task 4.1 and task 5.3 remain open;
no compatibility path or provisional 6,240-grain allowance changed.

## Repaired intervention fixture and focused PASS (2026-09-14)

The first temporary interceptor patched `mido.ports.BaseOutput.send`, but the
RtMidi concrete output overrides that method. It therefore never saw outgoing
messages. A second assumption counted the stop as the second short press;
stream-scheduled sends and the baseline's stale-state `already_satisfied`
shortcut made that unreliable. The maintained fixture now wraps the concrete
output only for the overdub pass. After actual musical note sends, it recognizes
the stop-button press/release, injects held note 83, and schedules one MIDI Stop
305 ms after the actual release send. It records host action timestamps and
MIDI-monitor open/close timestamps. The focused stop transition cannot be skipped
by stale PLAYING and has zero automatic retries; only an explicit
OVERDUB_STOPPING -> STOPPED confirms it. No firmware changes or uploads were
needed for this fixture repair.

Run with an explicitly approved scratch target and a new output directory:

```sh
.venv/bin/python scripts/hitl_stop_interruption.py --track-number 5 --loop-slot 1 --out-dir captures/<new-run>
```

The fixture clears that scratch slot. It uses one managed serial owner and
retains the modified stimulus identity. `scripts/hitl/stop_intervention.py`
provides the tested trigger and independent focused verdict. The broader
baseline verdict is retained separately and cannot substitute for intervention
coverage. Tests cover the backend override, optional missing start press,
velocity-zero release, one-shot behavior, stale PLAYING, disabled retries,
actual Stop overlap, missing monitor duration, overflow and resumed NoteOns.

Evidence: `captures/resumable-stop-hitl-strict-wrapper/interruption-result.json`,
`intervention.jsonl`, `midi-output.jsonl`, and `session_20260914_231934.log`.
The focused result is **PASS: single-track stop precedence and silence only**:

- Enter OVERDUB_STOPPING: 910,004,977 us.
- Device receives MIDI Stop: 910,014,230 us, **9,253 us after entry**.
- First target All Notes Off: **70 us after Stop receipt**.
- OVERDUB_STOPPING -> STOPPED: 910,191,090 us, **176,860 us after Stop receipt**.
- Continued device observation: **9,485,468 us after STOPPED**; no target-track
  NoteOn or return to PLAYING, no capture-ring overflow/reconnect, and bounded
  USB MIDI monitoring with no target-track NoteOn after the host Stop.

An earlier concrete-port run also observed Stop inside stopping at +4,348 us,
but lacked explicit MIDI monitor lifetime markers. The next repeat exposed the
stale-state skip and was inconclusive; the final strict run above closes only
this focused single-track interruption/silence checkpoint. Broad baseline
verification remains INCONCLUSIVE_CAPTURE and is not promoted to PASS.
Restart/accepted-note survival, layered playback and complete owner/grain timing
attribution remain unverified. Task 5.3 and all other currently open tasks remain
open (11/19 complete); no 6,240-grain real-time reservation is established.

Validation: 9 focused host tests, 13 capture-integrity tests and 26 host timing
tests passed. Firmware/native validation remains the previous uploaded build
(2,080 passed, 1 skipped). Transport is STOPPED and all managed capture/monitor
processes closed at the end of the final fixture.

## Restart survival failure (2026-09-14)

After the strict interruption PASS, the existing track 5/slot 1 content was
restarted without clearing or recording additional input. A single serial owner
and USB MIDI callback captured 20 seconds of playback, followed by explicit
MIDI Stop and 5 seconds of settling. Evidence is in
`captures/resumable-stop-restart-survival/{serial.log,midi-output.jsonl,actions.jsonl,restart-result.json}`.
Transport is stopped and the capture ports are closed.

**Restart content result: FAIL.** Three complete cycles, bracketed by the
fixture's unique base pitch 48, each reproduce base pitches 48–79 and omit all
overdub pitches 24–39 plus held pitch 83. The fixture-specific verifier
`scripts/hitl/verify/stop_restart_survival.py` records missing, duplicate and
unpaired notes independently of deadline timing. Its three host tests pass.
The input fixture sent every expected pitch once; no new clear or destructive
operation preceded restart. The existing single-track Stop/silence PASS remains
valid only for its stated scope and cannot be expanded to capture survival.

The serial playback rebuild at 1,109,082,352 us reports the overlay path. Display
reports 49 content/frame notes but 32 epoch notes, and subsequent canonical pass
resolution reports 49 (`DIAG,lcr,pairrows,at=passes,count=49`). This supports a
stale audible representation rather than a claim that canonical content was
physically deleted. Exact full canonical event parity remains to be proven.

Code inspection: `Track::processDeferredOverdubStopGrain` transfers the prepared
capture and later calls cleanup and `closeOverdubSession`, but does not queue the
session-exit snapshot handoff. Closing clears session pass/companion identity;
playback subsequently gathers the published root plus the available overlay.
The legacy `queueSessionExitSnapshot` explicitly carries those identities to
publication, but contains growing vectors and nested scans, so simply calling
it from an active stopping grain would violate this change's bounds.

Priority within task 2.3 is now the already-required reserved, bounded
session-exit publication handoff and protected cleanup integration. Preserve all
accepted session pass and companion identity until that handoff is ready; make
restart consume the current canonical result without an active synchronous
rebuild or a fabricated revision match. Verify native snapshot/companion parity
and then repeat interruption + restart as one fixture. Defer layered HITL until
this single-slot survival regression passes. No firmware change was made to
mask the failure; the 6,240-grain allowance and task checkboxes are unchanged.

## Restart publication repair (2026-09-14)

The prepared-stop production path now transfers session pass IDs, companion
rows and the final committed pass ID into a receipt reserved by the existing
cold transfer prewarm. The handoff uses vector ownership swaps and scalar writes;
it neither allocates nor calls the legacy queue assembler in a stopping grain.
Session close preserves in-flight snapshot work while that receipt is pending.
Playback's stale-root overlay reads the retained identities immediately, before
any idle publisher turn. New overdub admission waits for the receipt to be
consumed, preventing overwrite; transport Start/Stop do not depend on that slot
becoming available. Clear/reset cancels the receipt while retaining its prewarm.

The existing idle snapshot owner merges the receipt through the legacy queue
assembler and returns before starting construction. This is **not** a claim that
queue assembly, snapshot construction, cleanup or playback overlay collection
now satisfy the full bounded-work contract. Those integration/timing gates in
2.3/4.1 remain open. No task checkbox is closed by this repair.

A companion regression also exposed that session-exit publication updated note
membership without updating MIDI-event membership. The companion publication
batch now updates both, preserving the original MIDI channel and emitting the
final result once for a sequence of edits to the same note. The focused tests
cover restart before/after publication, retained Delete companions, empty stop,
receipt backpressure, construction-disabled turns and reset cancellation.

`scripts/hitl_stop_restart.py` is the maintained follow-up to the interruption
fixture. It sends one Start, observes 20 seconds, sends Stop and observes another
5 seconds, with no clear/new capture/upload. Its verifier remains specific to
the 32-base + 16-overdub + held-83 fixture and does not prove timing safety.
### Validation and device rerun

- Full native suite: 2,083 passed, one skipped (`/tmp/resumable-handoff-native-all.log`).
  Final focused suite after the stale-target guard and in-flight wrap test:
  124/124 passed (`/tmp/resumable-handoff-native-final.log`). Host fixture/timing
  tests: 38 passed. Strict OpenSpec validation and `git diff --check` passed.
- Uploaded capture firmware: `/tmp/resumable-handoff-admission-upload.log`.
  RAM1 locals remain 6,496 bytes; RAM1 variables 91,808 bytes, code 425,096;
  EXTRAM variables 1,056. No real-time reservation limit was changed.
- `captures/resumable-handoff-stop-hitl/`: first run is **INCONCLUSIVE** for
  interruption. It committed an overdub wrap immediately before stop, then
  used `OVERDUBBING -> PLAYING` rather than deferred stopping. No claim about
  the cause of admission rejection is justified by that capture. The fixture
  stimulus was not changed to obtain a passing result.
- Capture-only failure diagnostics now distinguish unavailable staging or
  canonical transfer capacity (`ODUB,stop,admission,...,storage_denied`) from
  freeze/contract rejection (`freeze_or_contract_denied`). For storage denial,
  the event/ref fields report staging-present and transfer-ready booleans;
  for freeze/contract denial they report final frozen rows and open reservations.
- `captures/resumable-handoff-admission-hitl/`: identical stimulus, no wrap
  immediately before stop; focused interruption **PASS**. Stop arrived 657 us
  into stopping; All Notes Off followed after 70 us; STOPPED followed 183,388 us
  after receipt. Silence observation lasted 8,483,898 us. The broader baseline
  verdict remains `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)` and is not promoted.
- `captures/resumable-handoff-restart-hitl/`: **PASS** after that interruption,
  without an intervening clear/record/upload. Three complete cycles each contain
  all base pitches 48–79, overdub 24–39 and held 83 exactly once, with matching
  NoteOffs and no unexpected notes. Serial capture also reports the deferred
  snapshot publication. Transport is STOPPED and both capture owners are closed.

The original single-slot restart omission is repaired and device-verified.
The forced wrap-adjacent transport-Stop regression below repairs the prior
emission failure. Continue with protected cleanup/undo, layered playback, parity
and full timing gates. All eight open tasks remain open; progress is still 11/19.
The stop-phase samples in the passing run have unknown deadline slack after
transport Stop, so they do not prove MIDI-boundary safety.

## Device-clock normal-stop trace (2026-09-15)

`captures/resumable-stop-timeline-route-fix-finalcheck-hitl/session_20260915_085258.log`
contains a retained normal-stop trace on the authorized track 5 / slot 1 scratch
target, flushed from an earlier run. Its device timestamps precede the current
run controls; it must not be attributed to the named host run or used to prove
the speculative playback-route change fixed a freeze. The trace starts at device time 79,799,501 us and returns to PLAYING at
79,811,416 us. Its canonical-order, canonical-rewrite, audible-order,
prepare-transfer, atomic-transfer, and cleanup grains are each attributed to
the preceding playback service boundary (2,596 us period); no recorded grain
crossed its available slack. The longest observed grain was 146 us.

This is recovered diagnostic evidence, not a fresh regression checkpoint.
No stop-emission records were retained. It does not close task 4.1:
the entry edge was not yet emitted in that captured image, and the new deferred
state-edge record is awaiting its own device rerun. It also does not close the
transport-Stop, long-loop, or full 5.3 matrix gates.

## Forced wrap-adjacent transport Stop (2026-09-15)

The four-bar variant on the approved scratch target (track 5, slot 1) reached
`OVERDUBBING -> OVERDUB_STOPPING` at 50,790,737 us. MIDI Stop arrived 8,689 us
later. The previous firmware emitted a target note-on after this point while
the Stop handler was still fanning out CC123. The repair publishes a volatile
per-track output barrier before that fan-out and rechecks it at the final Track
MIDI emission call; a new play, record, or overdub entry clears the barrier.

The rerun is **PASS: single-track stop precedence and silence only**:

- First target All Notes Off: 83 us after Stop receipt.
- `OVERDUB_STOPPING -> STOPPED`: 173,607 us after Stop receipt.
- Observation after STOPPED: 18,900,752 us, with no target-track NoteOn.
- Serial evidence contains no `MO,144,5` or replayed target controller after
  Stop; the USB MIDI monitor likewise contains no target-channel NoteOn.

Evidence: `captures/resumable-handoff-wrap-forced4-barrier-hitl/` including
`session_20260915_000223.log`, `midi-output.jsonl`, `intervention.jsonl`, and
`interruption-result.json`. The fixture wrapper ended before its automatic
result-write step, so the stored result is the output of its existing
`verify_interruption` verifier over those complete artifacts. This passes only
the focused silence checkpoint. It does not establish the 6,240-grain realtime
reservation, deadline attribution, layered playback, or the remaining task 2.3
requirements.

## Trace/probe audit (2026-09-15)

The probe previously waited for retained `stop_grain` records while transport
was active. A timeout therefore did not establish a device hang. It now allows
one second for normal completion, sends transport Stop to permit flushing, and
scores the closed log, including failure paths. Missing or invalid state edges
cannot satisfy the normal-stop check. Fresh-run attribution still needs an
explicit device-clock fence before this probe can close a regression gate.

Telemetry no longer ends its service/emission trace at an intermediate phase
completion; only a terminal state edge followed by the final grain ends it.
Non-capture builds exclude the stop-state hooks. Exact entry-boundary capture,
concurrent-stop trace ownership, and a fresh device rerun remain open under 4.1.
No task checkbox is closed by these corrections.

Validation for this audit: 43 focused native telemetry/capture-tier tests and
29 host timing tests passed. Both `teensy41` and `teensy41-capture-serial` built
successfully; reported RAM1 free was 19,264 and 6,432 bytes respectively. These
images were built only, not uploaded during this audit.

## Fresh device-clock stop checkpoint (2026-09-15, 09:13)

`captures/resumable-stop-fenced-timeline-hitl/session_20260915_091328.log`
and its `result.json` contain a fresh normal stop on track 5 / slot 1. The
32-character run token was acknowledged at device time 29,416,669 us before
scenario controls. The host excludes older event timestamps even when their
records are printed after the acknowledgement, and rejects serial reconnects.

The state edges are 46,509,000 us (OVERDUBBING to OVERDUB_STOPPING) and
46,520,970 us (OVERDUB_STOPPING to PLAYING): 11,970 us total. Seven grains
range from 7 to 130 us. Four actual timer-service callbacks are retained;
there are no trace drops, malformed rows, or capture-ring overflow notices.
No note emission occurs in this short stop trace, so emission correlation
still requires a stimulus that emits notes during finalization.

All seven grains start after the accumulated telemetry deadline was already
past. Service callbacks remain spaced by 2,596 us, with the accumulated due
time one period behind actual entry. The callback samples `micros()`; this is
not a scheduled timestamp mislabeled as actual. Neither zero slack nor this
pre-existing cadence offset establishes that a grain blocked a boundary.
The host report now distinguishes `already_overdue_at_start`,
`crossed_during_grain`, and `within_deadline`, and separately reports overlap
with the next observed callback. An overlap can include timer preemption and
is not itself evidence of blocking. The missing callback after final trace
closure remains unknown, not an inferred success.

Follow-up code queues state edges with their boundary snapshot at the state
transition, retains per-track active-stop ownership, and never resets an
undrained trace queue on another entry. Shared queue publication preserves
the interrupt mask around fixed-size updates. Legacy STOPPING state markers
are formatted by the deferred drain using the occurrence timestamp. Cold
state-transition/trace-entry helpers reside in flash to stay below the RAM1
code-bank boundary (6,496 bytes reported free). These follow-up changes need
the interruption checkpoint recorded below; the 09:13 capture predates the
publication guard and restored legacy state marker.

Validation: 44 focused native tests; 29 host timing tests; 3 fresh-run fence
tests; 13 managed-capture integrity tests; 10 intervention policy tests passed.
Both production and capture builds passed before the final capture-only guard;
the guarded capture image also built and uploaded successfully. Task 4.1 and
the broad 5.3 matrix remain open; no real-time reservation is established here.

## Correlated transport-Stop rerun (2026-09-15, 09:22)

Final-image evidence:
`captures/resumable-stop-boundary-snapshot-interruption-hitl/session_20260915_092239.log`,
`interruption-result.json`, and the adjacent MIDI-output JSONL.
The single-track stop-precedence/silence verifier passes. Stop entry is
49,314,746 us; received Stop is 49,320,691 us (5,945 us into stopping).
All Notes Off follows by 66 us. The exact STOPPED state-edge timestamp is
49,431,795 us; the legacy state marker used by the focused verifier is
49,431,810 us. Its reported recovery is 111,119 us, with another 8,193,932 us
of output-silence observation.

The final trace retains two state edges, nine grains, and 39 service callbacks,
with zero dropped records, malformed rows, or capture-ring overflow notices.
All grain-start service snapshots precede their grain start. After transport
Stop resets cadence, later grains explicitly report unknown cadence. The
largest active grain before Stop is 143 us; final STOPPED-policy cleanup is
760 us. There are no note-emission records in this stop window, so this is
not proof of the note-emission deadline gate or the full reservation contract.

The preceding 09:17 run passed the same narrow functional checkpoint but lost
six service records during Stop handling. That failure motivated increasing
the fixed PSRAM queue from 32 to 128 slots (127 usable), while retaining explicit
overflow reporting. Each grain now copies its service boundary and start time
together, before work, rather than reading a possibly newer service at its end.
The enlarged telemetry state is placement-constructed directly in PSRAM;
its cold initializer lives in flash and no longer creates a large stack temporary.
Capture build: RAM1 code 425,624 bytes, 6,496 bytes free for locals; EXTRAM static
usage 6,208 bytes. Final capture firmware was built and uploaded successfully.
45 focused native tests and 45 host timing/fence/capture-integrity tests passed.

The broad baseline verifier still reports `INCONCLUSIVE_CAPTURE
(SETUP_UNCONFIRMED)`: missing phase boundaries, overdub first-note/SEVT evidence,
and a display post-stop evidence failure. Its wall-clock transport proxy also
reported a 3,840-tick record loop against 3,072 expected. These results are
preserved, not waived by the narrow focused PASS. Loaded-slot prewarm, broad
setup/parity/display validation, emission-bearing stop stimuli, tasks 2.3–2.5,
3.3–3.4, the remaining 4.1 telemetry, full 5.3, and 5.4 remain open. The provisional
6,240 grains are unchanged and still have no accepted real-time reservation.

## Dense emission stimulus and restored-slot prewarm audit (2026-09-15)

The optional `--dense-stop-probe` records a unique pitch-95 marker followed by
160 overlapping short notes (8 ms onset spacing, 32 ms duration). It waits for
that marker on actual USB MIDI input before issuing the normal-stop gesture,
placing finalization within the dense section. The probe now requires valid
NoteOn and NoteOff stop-trace records, not merely a successful state transition.

First capture:
`captures/resumable-stop-dense-emission-hitl/session_20260915_092953.log`.
Fresh fence: 453,543,026 us. Stop entry 476,749,636 us, PLAYING edge 476,760,343 us
(10,707 us). One Off and one On were retained at 476,753,647 and 476,753,663 us,
both at service sequence 15,999 and both before deadline 476,756,164 us. All
recorded grains were within their start-snapshot deadlines, with no drops or
malformed rows. This image did not identify emission ownership; that limitation
prevents assigning these events to the scratch track solely from this trace.

The capture protocol now appends track, slot, remapped channel, and pitch to
`stop_emit`. The host retains compatibility with older rows but requires the
selected track/slot identity for the dense-stimulus gate. Send time is sampled
immediately after the existing MIDI send, before resolving its owner metadata.
No MIDI scheduling or pairing semantics change.

Restored-slot audit: `TrackManager::prewarmPlaybackRuntime()` reserves staging
for boot slots. `main.cpp::prewarmPlaybackMergedEventsForSlot()` subsequently
prepares only merged playback events after a focused load Commit. It does not
call `Track::prewarmPlaybackForSlot()` to reserve canonical transfer capacity
or the full view/companion bundle for a newly restored slot. This is an existing
setup gap in the remaining 3.4 integration, not authorization to allocate from
active overdub entry or to waive prepared-audible-slot admission. The rejection
at `Track::startOverdubbing()` remains intact pending that integration.

## Track-specific emission evidence (2026-09-15, 09:39)

Evidence: `captures/resumable-stop-owned-emission-offset-hitl/`, including
`session_20260915_093949.log`, `result.json`, and `midi-output.jsonl`.
The fresh, marker-triggered test used a recorded additional 550 ms delay.
The preceding zero-offset run in `resumable-stop-owned-emission-hitl` completed
normal stop but correctly failed its emission gate. Its host MIDI marker and
many following notes arrived in bursts; host reception did not reliably locate
the dense section on the device clock. The offset was chosen from the observed
stored event range/playback phase; actual device evidence remains the gate.

The final run retains NoteOff pitch 61 and NoteOn pitch 65, both owned by
zero-based track 4 / slot 0, remapped MIDI channel 5. Stop entry is 241,894,713 us;
PLAYING is 241,902,140 us: 7,427 us total. Seven grains peak at 136 us. Both note
emissions use service sequence 13,428 (actual callback 241,901,050 us): Off at
241,901,127 us (+77 us) and On at 241,901,150 us (+100 us). No trace drops,
malformed records, or uncorrelated emissions were reported.

This is **complete target-track correlation, not a deadline-safety pass**.
The accumulated deadline is 241,670,006 us, yielding reported Off/On lateness
of 231,121 / 231,144 us. Service actual-minus-due is consistently 233,640 us
(90 periods of 2,596 us) throughout the retained stop service records, and the
entry edge already lies after its accumulated deadline. That existing offset
must not be hidden by rebasing telemetry to the last actual callback. Its origin
before stop remains to be attributed. Six grains end before their next observed
callback; the callback after the final grain is not retained, so that comparison
remains unknown. These facts do not prove global clock or MIDI deadline safety.

The probe now reports `emission_evidence_complete` separately from
`stop_emission_deadlines_met`; this run is true / false respectively. Its zero
process exit denotes completion of the correlation probe, not a real-time gate.
The stored result was enriched with these explicit verdict fields from the same
unaltered raw capture after adding the distinction to the host evaluator.

Validation: 45 native telemetry/capture-tier tests; 30 host timing tests and
5 probe/fence/emission-gate tests passed. Production and capture firmware builds
passed; capture RAM1 code is 425,752 bytes with 6,496 bytes free for locals.
The capture firmware carrying emission ownership was uploaded and tested.
Task 4.1, broad setup/parity validation, remaining implementation tasks, 5.3,
and the provisional 6,240-grain real-time reservation remain open.

### Shared first-record window upload and interruption rerun (2026-09-15)

The memory-recovered capture-serial firmware was uploaded successfully. The
approved track 5 / slot 1 transport-Stop fixture then reached
`OVERDUB_STOPPING` and passed the focused interruption verifier: stop offset
10,299 us, All Notes Off delay 3,032 us, STOPPED recovery 133,742 us, and
12.26 s of target-channel silence. The broader baseline is intentionally
`INCONCLUSIVE_CAPTURE` because the injected Stop aborts overdub note-count
checks; this does not close layered playback or reservation gates.

A clean normal-stop rerun recorded and returned to PLAYING, but the host did
not observe the expected `OVERDUB_STOPPING -> PLAYING` confirmation before its
timeout. It is retained as an inconclusive normal-stop result, not a pass.


### Pre-stop emission burst attribution (2026-09-15)

Fresh-fence analysis of `captures/resumable-stop-owned-emission-offset-hitl/session_20260915_093949.log` (fence 218258060 us) locates a snapshot publication at 240805727 us, followed by a 256-event playback-order rebuild at 240810297 us. Track 5/channel 5 then emits 258 MO records from 240810359 through 241043026 us (232667 us between first and last pre-send notices). The first retained late-event occurrence is 240811984 us, before stop entry 241894713 us. MO timestamps are pre-send notices, not physical completion timestamps.

The enclosing telemetry window reports a maximum track-5 update of 236155 us, total-track update of 236187 us, and nested send time of 233444 us; rebuild and exclusive order maxima are 1034 and 5768 us. Thus the large pre-stop offset coincides with playback emission work, not the later 136-us stop grain. HardwareSerial's local Teensy implementation waits when its transmit buffer is full, consistent with the approximately 640/1280-us spacing within this dense burst. This does not yet identify every contributing output route or prove why this many events became due.

The legacy gather path returns early on an empty committed source, before updating `lastTickInLoop`. A later snapshot can therefore expose an old or fresh-origin catch-up interval. This is a code-supported hypothesis, not yet a device-proven cursor diagnosis. Queued `playback_build` telemetry now appends rebuild occurrence time, previous phase, current phase, and event count to test it. The maximum-duration sample retains its own context; older rows remain supported, and fresh-run filtering uses the new occurrence time when available. No playback policy or cadence rebase is introduced. Prepared-source admission, task 2.4 route integration, and the real-time reservation gate remain open.


### Device-confirmed fresh-origin catch-up (2026-09-15, 09:52)

Capture `captures/resumable-stop-rebuild-phase-device-hitl/session_20260915_095242.log`, fresh fence 36189745 us, uses the extended rebuild metadata. At rebuild occurrence **59884454 us**, the first retained snapshot contains **253 events**, with **previous phase UINT32_MAX and current phase 375**. Its queued notice is printed at 60034366 us. The subsequent overlay rebuild retains ordinary phase progression 745→746, and the next snapshot 14→15. This confirms that the first available committed source reaches playback with the fresh-origin cursor still set despite an advanced playhead; the legacy catch-up predicate then admits earlier phases. The empty-source early return is before interval bookkeeping. Prepared-source admission and initial ledger/cursor anchoring must be integrated together; merely rebasing the telemetry clock or discarding early events would not establish the required audible/held-note contract.

The enclosing window reports track-5 update max **148627 us** and nested send max **144102 us**. At stop, service actual-minus-due is **145376 us** (56 periods of 2596 us). This run also has a separate earlier clock-lateness sample of 6034 us and total-track update maximum 196543 us near initial recording; do not attribute the entire run's timing failure exclusively to the snapshot burst.

Normal stop entry 60976276 us reaches PLAYING at 60991403 us: **15127 us**, seven grains, six retained service records, zero trace drops/malformed rows, and **zero in-stop note emissions**. The probe correctly exits 1 for incomplete On/Off evidence. One grain overlaps an observed service callback; overlap alone can be timer preemption and is not proof of blocking. This is diagnostic evidence, not a passing normal-stop emission checkpoint or a real-time reservation.

Validation: 45 native telemetry/capture-tier tests and 36 host tests pass; production and capture-serial builds pass. Capture RAM1 allowance remains 6496 bytes, EXTMEM telemetry state is 6240 bytes (unrelated to the provisional 6240-grain allowance). The capture firmware was uploaded successfully. The first sandboxed probe could not open USB serial and was stopped before setup; only the escalated device run above performed the authorized track-5/slot-1 fixture. All device/capture processes completed.

### Deterministic clean normal-stop checkpoint (2026-09-15, 13:22)

Evidence: `captures/normal-stop-deterministic-20260915-rerun3/`, including
`session_20260915_132206.log` and `result.json`. The fixture was corrected to
allow the empty-slot record arm to start transport without sending a second
toggle, and to hold the seed for a complete two-bar window. The fresh run
cleared the authorized track 5 / slot 1 target, recorded a 2,304-tick seed,
entered overdub, added note 83, and completed a normal stop.

The device-clock state edges are `OVERDUBBING -> OVERDUB_STOPPING` at
39,460,230 us and `OVERDUB_STOPPING -> PLAYING` at 39,474,748 us: 14,518 us
total. Eight deferred-stop grains were retained; the longest was 1,827 us.
State and service correlation completed with zero trace drops, zero malformed
rows, no uncorrelated emissions, and no reported timing issues. This closes
the focused normal-stop transition checkpoint. The run did not use a dense
emission stimulus, so it does not close task 4.1's emission/deadline gate or
the 6,240-grain reservation. Tasks 2.3-2.5, 3.3-3.4, and 5.3-5.4 remain open.

The implementation-side repair is in `Track::startRecording`: first-record
entry now requests the existing full playback-window prewarm so bar 1 is
prepared before capture and can be bound at record stop. Focused native tests
remain 256/256 passing; capture firmware was rebuilt and uploaded before this
run.

### Deterministic transport-Stop interruption checkpoint (2026-09-15, 13:28)

Evidence: `captures/transport-stop-interruption-deterministic-20260915/`,
including `session_20260915_132717.log`, `intervention.jsonl`,
`midi-output.jsonl`, and `interruption-result.json`. The same managed fixture
cleared track 5 / slot 1, recorded the two-bar seed, entered overdub, and sent
transport Stop 8,051 us after the overdub-stop button release while the
deferred stop was active.

The focused interruption verifier is **PASS**: the first target All Notes Off
was observed 3,037 us after Stop receipt; the track reached
`OVERDUB_STOPPING -> STOPPED` after 187,161 us; and 12,325,392 us of subsequent
target-channel silence contained no replayed NoteOn. This proves Stop
precedence and silence for the single-track interruption scope. The wrapper's
broader baseline is intentionally `INCONCLUSIVE_CAPTURE` because the injected
transport Stop aborts its normal overdub note-count checks. Restart survival,
layered playback, emission deadline attribution, and the 6,240-grain
reservation remain open.

### Transport-stop rerun after bounded STOPPED recovery (2026-09-15, 14:04)

Evidence: `captures/transport-stop-interruption-2_3-rerun5-20260915/`, including
`session_20260915_140418.log`, `intervention.jsonl`, `midi-output.jsonl`, and
`interruption-result.json`. The fixture captured 5,765 serial lines and passed
its record, overdub, persistence, and post-stop display checks. The prepared
stop admission nevertheless rejected the candidate with
`preparation_horizon_denied` (`ranked=73039`, `canonical=4`), above the
provisional 6,240 ranked-grain allowance. The bounded legacy fallback completed
`OVERDUBBING -> PLAYING` before the injected transport Stop, so the verifier
reported `INCONCLUSIVE_CAPTURE` (`no_device_stop_inside_stopping`). No trace
overflow was observed. This run does not replace the earlier focused
interruption PASS and closes no additional task or HITL gate; the companion
history admission/setup condition remains to be resolved before another
prepared-path interruption run.

An admission-bound experiment immediately after that rerun removed the
history term from the prepared-tail estimate. The device entered the deferred
stop but then stopped emitting serial output during preparation, confirming the
estimate was under-reserved. The change was reverted and the safe firmware was
rebuilt and uploaded; this is diagnostic evidence only and closes no gate.

### Sparse admission probe (2026-09-15, 14:53)

The diagnostic wrapper generated a one-bar overdub using 48 MIDI clocks between
notes and 48-clock gates, reducing the candidate event count without changing
the production admission code. The authorized track-5 / slot-1 fixture reached
the host overdub-stop action but did not produce a device-confirmed
`OVERDUBBING → OVERDUB_STOPPING` edge before the baseline timeout. The focused
result is `INCONCLUSIVE_CAPTURE` (`no_device_stop_inside_stopping`), so no
`ODUB,stop,admission` estimate can be interpreted from this run. This is a
fixture synchronization/setup failure, not evidence for removing the 16-bar or
rolling-window logic. Restore the clean stop fixture before comparing sparse and
dense admission candidates; no task or acceptance gate closes.

### Continuous-stop implementation checkpoint (2026-09-15)

The stop-service contract now keeps preparation counters wide and resumable.
`PendingStopAdmission` no longer rejects a valid frozen candidate when ranked,
canonical, or recycle work exceeds the provisional 6,240 or legacy 16-bit
aggregate credits. Fixed staging, closure, generation, and invalid-bound
failures remain denial conditions. A valid overdub stop enters
`OVERDUB_STOPPING` and continues one MIDI-separated grain per eligible turn
until the command-epoch-validated PLAYING or STOPPED commit is ready.

Focused native playback-prewarm/edit coverage is **256/256**; the capture image
builds with **6,176** bytes of RAM1 local headroom. The first post-upload
track-5 / slot-1 HITL was `INCONCLUSIVE_CAPTURE` during clear preconditions
(transport remained active), before overdub, so it provides no device admission
or state-transition evidence.
### Deferred DROID feedback upload and probe (2026-09-15 18:15)

The firmware containing the deferred DROID USB-host feedback queue was built and
uploaded successfully. The authorized track 5 / slot 1 `base` probe started
recording and the first overdub, but the harness could not confirm the second
overdub transition and aborted with `INCONCLUSIVE_CAPTURE` / `SERIAL_INCOMPLETE`.
The capture contains 1,437 serial lines and no `DIAG,droid_queue` records, so
this run did not exercise an active DROID USB-host endpoint. It is diagnostic
only; no OpenSpec acceptance or real-time deadline gate is closed.

Evidence: `captures/resumable-droid-deferred-hitl/`, including
`session_20260915_181455.log` and
`host_midi_automation_baseline_20260915_181549.json`.

The immediate rerun at 18:26 reached the device but failed the scratch-slot
clear precondition because the deferred-save blocker was reported. It produced
no capture lines and is therefore also inconclusive.

Evidence: `captures/resumable-droid-deferred-hitl-rerun/`.

### Deferred-save clear blocker diagnosis and fix (2026-09-15)

The clear failure was caused by `handleClearTrack()` synchronously draining a
pending save while the selected track remained `PLAYING`. The persistence
scheduler correctly refused all SD work during audible playback, so the drain
made no progress and hit its stuck guard after 256 iterations. Clear now stops
that playing track before the prerequisite drain; the existing
`restorePlaybackAfterSlotClear()` path restores a remaining enabled slot after
the mutation, and a failed drain restarts the original slot. The capture
firmware and full native suite pass with this fix.

The 19:19 rerun confirmed the fix on device: track 5 / slot 1 cleared, record
started and stopped, and the first overdub was admitted. The canonical harness
again could not confirm the second overdub transition and aborted, so the run
remains `INCONCLUSIVE_CAPTURE`; no DROID endpoint was visible and no queue
telemetry was emitted.

Evidence: `captures/resumable-deferred-clear-fix-hitl/`.

### Deferred overdub-stop continuation deadlock fix (2026-09-15)

The stop continuation could remain in `OVERDUBBING` after the button request
because `beginFrozenCaptureCanonicalOrder()` rejected a valid canonical binding
whenever the separate ordinary frozen-order binding was already present. That
cross-binding guard caused every canonical stop grain to return `Denied`.
Removing the unrelated guard allows the canonical binding to initialize and the
deferred owner to advance toward `OVERDUB_STOPPING → PLAYING` (or STOPPED for a
transport Stop). Firmware build passes with 6,176 bytes of RAM1 local headroom.

Follow-up inspection found the same guard in the canonical step function, so
initialization could succeed but every subsequent grain still returned
`Denied`. Removing that second cross-binding check completes the continuation
fix. Focused state/playback tests pass (20/20), and the capture firmware builds
with 6,176 bytes of RAM1 local headroom.

The canonical rewrite stage contained the same independent-binding mistake.
Its guard is now removed as well, allowing the full canonical order → rewrite →
transfer sequence to reach the requested destination. The capture firmware
rebuild passes with 6,176 bytes of RAM1 local headroom.

The 19:39 uploaded rerun captured the repaired continuation on device:
`OVERDUBBING → OVERDUB_STOPPING → STOPPED`. The harness later aborted when its
MIDI clock disappeared before the complete second-overdub matrix, so this is a
continuation checkpoint rather than a full acceptance run. The post-overdub
display snapshot reported `state=STOPPED`.

Evidence: `captures/resumable-overdub-stop-continuation-hitl/`.

### Record/overdub-button continuation to PLAYING (2026-09-15 20:51)

The normal record/overdub button path stalled because a bridge builder was
allowed to prepare against the immutable one-bar canonical window, but bridge
selection still required that base to be `lifecycle.current` and full-loop. The
prepared window is intentionally non-current while the old audible view stays
readable, so the completed candidate could never be selected. Selection now
validates the exact immutable view generation recorded by the bridge and accepts
its prepared coverage when the boundary lies in that coverage. Legacy full-loop
bridges retain their existing current-view proof.

The same investigation found completed bridge workers surviving a previous
pass/clear. Deferred-cleanup and new overdub-stop admission now discard only
those temporary workers, retaining the selected bridge and freeing its alternate
candidate buffer.

The authorized track-5 / slot-1 focused run passed after upload. It recorded a
prepared-window bridge and `OVERDUB_STOPPING → PLAYING`; the post-flush
device-clock trace has complete state/deadline correlation and no timing issues.
The host probe now evaluates this deferred capture after its idle flush rather
than treating the absence of live serial lines during active transport as a
failure.

Evidence: `captures/resumable-overdub-stop-canonical-window-pass-20260915/`
(`session_20260915_205104.log`, `result.json`). This is a focused normal-stop
checkpoint; the full task 5.3 matrix and real-time reservation work remain open.

### Immediate Button A release and sequential two-overdub checkpoint (2026-09-15 21:13)

The normal Button A path had kept the visible track state in `OVERDUB_STOPPING`
until canonical ordering/rewrite reached `PrepareTransfer`. That made the user
interface wait behind resumable background work. The capture is now frozen at
the button boundary, the existing prepared playback window is released to
`PLAYING` immediately, and the frozen transaction remains the admission and
transport-stop barrier until its atomic commit/cleanup completes.

The authorized track-5 / slot-1 device-clock run cleared the scratch slot,
recorded a seed, then completed two note-based overdub passes (notes 83 and
84). Both subsequent overdub admissions succeeded after the one-second
background settle window. The trace contains two `OVERDUBBING →
OVERDUB_STOPPING → PLAYING` pairs: the release edges occurred 20 µs and 17 µs
after their respective stop boundaries, on a 2,603 µs MIDI service period.
The analyzer reported 762 bounded stop grains, complete state correlation, and
no timing issues. This validates the focused two-pass Button A continuation;
full task 5.3 and real-time reservation acceptance remain open.

Evidence: `captures/resumable-overdub-stop-button-release-two-pass-retry-20260915/`
(`session_20260915_211327.log`, `result.json`).


### DROID Button A empty-tail deadlock (2026-09-15)

The subsequent user report invalidates treating the short two-note probe as
complete Button A acceptance. Live `!STOP_STATUS` retrieval captured a stopped
transport with `state=PLAYING,pending=1,phase=9,dest=1,order=0,ranked=0,canonical=1`
and no pending capture pass (`captures/button-a-current-state-20260915.log`).
The empty successor skipped companion preparation but retained its one setup
credit; atomic transfer therefore could never finish, and the pending admission
guard rejected subsequent capture commands. Immediate PLAYING state publication
alone masked this unfinished transaction.

Empty sealed captures now retire skipped companion preparation through the same
checked helper as stopped captures. Closure/order obligations and command epoch
checks remain prerequisites. The direct stop-admission path publishes PLAYING
only for a Playing destination. Native regression covers empty, filtered-empty,
and transport-retargeted completion (147 playback-prewarm tests pass).

The earlier 17/20 microsecond measurements are state-assignment latency only.
They do not prove audible handoff, DROID feedback delivery, or real-time deadline
success: the immediate terminal state edge ends service/emission tracing before
background finalization. Those acceptance gates remain open.


After upload, both targeted continuations passed on track 5 / slot 1:

- `captures/button-a-empty-two-pass-20260915/`: two overdubs with no new notes;
  both stop edges observed and the second overdub admitted.
- `captures/button-a-wrapped-two-pass-20260915/`: two eight-second overdubs;
  each committed a wrap before its empty-tail stop, and both returned to PLAYING.

The final serial check in `captures/button-a-stopped-reentry-20260915.log`
reported `state=STOPPED,pending=0,canonical=0`. That separate check also includes
additional controller input beyond the three scripted presses, so its control
sequence is not scored as an isolated stopped-reentry acceptance test. The two
focused reports show no analyzer issues, but remain functional continuation
checkpoints rather than emission-deadline or full task-5.3 acceptance.


The user confirmed that the additional DROID Button A and transport presses in
the stopped-reentry capture were manual input. They are not evidence of duplicate
controller delivery. This confirmation does not turn the mixed-input sequence
into an isolated automated button-cycle test.

## Normal-stop cleanup continuation (2026-09-15)

The normal PLAYING completion path no longer silences output, clears playback
ledgers or reanchors the cursor. Shared `TrackPreparedStopCommit` cleanup keeps
the selected bridge while retiring completed bridge and canonical-tail workers.
Native coverage exercises the actual cleanup body, held-note closure, snapshot
receipt retry, and subsequent prefix preparation: 147 playback-prewarm tests pass.
The stricter host gate and transition tests pass: 27 tests. Capture build and
upload succeed; RAM1 local headroom stays at 6,176 bytes.

Two populated overdubs were run on authorized scratch track 5 / slot 1:

- `captures/normal-stop-cleanup-two-pass-20260915/result.json` and
  `session_20260915_213424.log`: first normal cleanup completed in a 185 µs grain;
  the second stop remained in prefix preparation until transport Stop recovery.
  Its old `normal_stop_verified=true` result is superseded: that verdict checked
  visible state transitions, not background completion.
- `captures/normal-stop-cleanup-tail-two-pass-20260915/result.json` and
  `session_20260915_213807.log`: after adding canonical-tail worker retirement,
  the new gate correctly returns failure. Two visible PLAYING transitions are
  observed, but only one normal cleanup completes (224 µs grain, destination 0).
  The second stop remains in CanonicalPrefix during its one-second observation;
  cleanup completes only under transport Stop (827 µs grain, destination 1).

The second-pass owner/readiness blocker remains unresolved; canonical-tail
retirement alone did not resolve it on-device. Both traces contain zero correlated
service/emission rows. Cleanup-grain durations are neither total stop latency nor
proof of audible continuity or emission deadlines. Transport ends STOPPED. Task
2.3, task 4.1, full 5.3 and the provisional 6,240-grain reservation remain open.

## Second-stop ready-window ownership (2026-09-15)

This supersedes the second-pass failure immediately above. A native reproducer
showed that the selected bridge's base stayed in the ready slot, and cleanup
retained its old canonical-window receipt. A subsequent prefix completed, but
window preparation denied against that stale receipt. Atomic bridge publication
now promotes the base via the existing ready-to-current lifecycle, and cleanup
retires the preparation receipt while keeping the audible current storage.
The new native second-window regression fails before this correction and passes
afterward, including continued readability of the selected bridge.

Uploaded result: `captures/stop-window-handoff-two-pass-20260915/result.json`,
log `session_20260915_214557.log`, track 5 / slot 1. The strengthened verdict is
true: two populated overdubs, two PLAYING-destination cleanup completions, no
probe errors, both completed before final transport Stop. Stop-entry to cleanup
end intervals are 240,823 and 244,832 µs; the final cleanup grains themselves are
287 and 299 µs. These are functional completion measurements, not emission
lateness or a real-time reservation. Service/emission row counts are both zero.

Native playback-prewarm: 147 passed. Native take-survival: 124 passed. Host
probe/transition tests: 27 passed. Build/upload passed; moving the newly linked
infrequent lifecycle handoff to flash restores 6,176 bytes RAM1 local headroom.
Task 2.3 remains open for the full rolling/current-phase handoff, undo and bounded
cleanup requirements. Task 4.1/5.3 and the 6,240-grain real-time reservation remain
open. The device was left with transport stopped.

## Joint publication and Add-only revision (2026-09-15)

The production atomic caller could bypass joint-preflight denial and publish
canonical data alone while PLAYING. Removing that fallthrough exposed a repeatable
failure in `captures/stop-atomic-dispatch-two-pass-20260915/result.json`: pass two
could not start because pass one remained at AtomicTransfer. Narrowed diagnostic
runs `captures/stop-atomic-denial-20260915/` and
`captures/stop-atomic-proof-20260915/` identified a target identity mismatch
(reason 288 = reserved admission 256 + target mismatch 32; 352 also included
transient bridge-selectability denial). Canonical preparation checks passed.

Pending Add changes produced no companion edit rows, but the proposed target
assumed any pending changes required a second revision. Canonical publication
correctly advanced once. Reserved preparation now derives the target revision
from the existing bounded companion worker's completed row count, before derived
pass construction. The native Add-only fixture starts with the formerly incorrect
provisional revision and verifies corrected joint publication through the same
reserved-tail and atomic-dispatch bodies used by Track. All 147 prewarm tests pass.

Uploaded checkpoint `captures/stop-joint-revision-two-pass-20260915/result.json`,
log `session_20260915_220034.log`: two populated overdubs and two completed normal
cleanups, no probe errors or atomic-preflight diagnostic denials. The device ends
with transport stopped. Build/upload pass; RAM1 headroom remains 6,176 bytes.
This supersedes reliance on earlier cleanup-only checkpoints where the fallback
remained reachable. Service/emission row counts remain zero, so this is not a
real-time deadline pass. Task 2.3 current-phase/rolling handoff, protected undo and
bounded cleanup, plus 4.1/5.3 acceptance, remain open.

## Running-cycle anchors (2026-09-15)

Track had constructed every preparation/publication anchor with cycle zero,
while the bridge consumer retained its running projection cycle. A later bridge
could therefore select next/steady-cycle masks on its first use. The three Track
anchors now use the runtime's current cycle; ordinary/overdub playback advances
that same counter on serviced wraps without a selected bridge. The existing
native held-note fixture now runs at cycles 0 and 3, proving a cross-wrap original
Off is retained, emitted once and absent in subsequent cycles. All 147 prewarm
tests and 27 host probe/transition tests pass.

Uploaded checkpoint: `captures/stop-cycle-wrapped-late-two-pass-20260915/result.json`,
log `session_20260915_222345.log`, authorized track 5 / slot 1. Command options:
`--overdub-passes 2 --overdub-duration-s 8 --late-overdub-note --clear-before-seed`.
Both normal cleanups complete before transport Stop, with no probe errors or
atomic-preflight denials. Atomic grains are 155/155 µs; cleanup grains are
125/123 µs. This is functional evidence for the longer late-note sequence;
service/emission row counts are zero, so audible cycle/phase and deadline
correlation remain unproven on-device.

Firmware fits with 6,176 bytes RAM1 local headroom and 425,784 bytes ITCM code.
Transport projection reanchoring uses the existing cold-code flash placement.
The phase part of the anchor still uses the frozen stop phase. Task 2.3
current-serviced-phase/rolling handoff, protected undo and bounded cleanup, along
with 4.1/5.3 deadline acceptance, remain open. Transport was left stopped.

## Production closure/window integration validation (2026-09-15)

The production stop path now owns complete immutable closure source bases beyond
worker cleanup, projects them through the shared rolling-window worker, refreshes
bridge preparation against the current serviced phase/ledger, and retains original
pending Off obligations across another source edit. Full native validation reports
2,123 passed and one skipped. The capture firmware build passes, with 6,176 bytes
RAM1 local headroom. These are host/build results, not a new device PASS.

The next manual regression uses DROID Button A: record two bars, enter overdub,
return through OVERDUB_STOPPING to PLAYING, then enter and exit a second overdub
without stopping transport. Verify both layers remain audible and visible, clocks
and display continue, and final cleanup completes. Preserve the existing manual
loop. Deadline/reservation acceptance and task 2.3 remain open.

Upload command completed successfully (`/tmp/finish-closure-upload.log`). The
subsequent passive serial check enumerated no `usbmodem` port, so device reboot
and runtime operation are not confirmed. No clear, note, or transport commands
were sent during this upload/check. Confirm the interface has returned before
performing the manual regression; restore serial access to retain its trace.

## Empty post-wrap routing regression (2026-09-15)

**2026-09-15 manual clean-stop routing regression:**
`captures/session_20260915_235602.log` records wrap commit at 48.314 s, Stop
Overdub at 48.428 s, and `bridge_begin` denial at device 48,438,934 us
(mask 1652 / 0x0674). Later Button A presses reach the finalization-pending
handler. The empty post-wrap candidate bypasses canonical/bridge preparation,
but the AtomicTransfer refresh guard incorrectly required a bridge for every
PLAYING destination and routed it back to CompanionBridge. The guard now
requires an actual pending capture pass. Empty stops proceed to existing
cleanup; populated PLAYING commits retain mandatory bridge preflight and
transport Stop retains precedence. Shared native routing coverage exercises
all three cases. The capture has ring overflow, so it cannot establish deadline
acceptance. Manual device confirmation and task 2.3 remain open.

Validation of the routing correction: 152 focused native tests pass; full native
suite reports 2,123 passed and one skipped. OpenSpec strict validation and
`git diff --check` pass. Logs: `/tmp/clean-stop-routing-native.log` and
`/tmp/clean-stop-routing-all-native.log`. The added regression executes the
same routing predicate used by Track before atomic dispatch, so it covers the
guard omitted by the earlier direct atomic-transfer tests.

Capture firmware build and upload both pass. RAM1 local headroom remains
6,176 bytes. `/dev/cu.usbmodem154944801` re-enumerated after upload; no MIDI
stimulus, clear or transport command was sent. Build/upload logs are
`/tmp/clean-stop-routing-firmware.log` and `/tmp/clean-stop-routing-upload.log`.
Device reentry still requires the manual two-overdub check; enumeration alone
is not behavioral acceptance.

## Future-window cleanup starvation (2026-09-16)

**2026-09-16 post-finalization reentry starvation:**
`captures/session_20260916_000034.log` shows the populated bridge ready at
30.256907 s and phase-10 cleanup completed at 30.354501 s. Button A reaches
Live Overdub at 31.709, 35.371 and 37.227 s, then the combined transfer/snapshot
admission guard rejects it. Code inspection and a failing native reproducer
identify `serviceBridgeWindow` discarding a completed but unpublished future
window. It immediately rebuilds that window on subsequent turns, starving the
existing snapshot receipt consumer and leaving its admission slot occupied.
The Complete branch now releases the builder with `discard(&target)`, preserving
the ready/build destination. No snapshot admission policy is relaxed. The native
regression fails before this change and passes after, including repeated idle
yields, immutable future windows and the existing multi-cycle MIDI continuity
fixture (152 focused tests pass). Admission warnings now distinguish transfer
capacity from an occupied snapshot receipt. Device reentry remains unverified;
task 2.3 and HITL/deadline gates remain open.

Validation: the new idle-yield assertion fails on the previous implementation
(`/tmp/window-yield-before.log`) and passes after preservation of the destination
(`/tmp/window-yield-after.log`, 152 tests). Full native validation reports
2,123 passed and one skipped (`/tmp/window-yield-all-native.log`). Capture
firmware builds with 6,176 bytes RAM1 local headroom
(`/tmp/window-yield-firmware.log`). OpenSpec strict validation and diff checks pass.

Upload succeeded (`/tmp/window-yield-upload.log`) and the serial port
`/dev/cu.usbmodem154944801` returned. No clear, MIDI stimulus or transport
command was sent. Manual DROID two-overdub reentry remains the next device gate.

## Moving-ledger refresh loop (2026-09-16)

**2026-09-16 repeated bridge refresh blocks the third overdub:**
`captures/session_20260916_000904.log` confirms another overdub starts at
61.028 s. Its stop at 69.606 s remains pending. The retained trace contains
23 completed bridges immediately followed by refresh, from 72.194397 through
75.511650 s, at intervals of 144,372–158,456 us. Compilation captures a live
ledger/cycle/phase; admission checks exact ledger equality and the retained
Off horizon. Normal emission advances these while compilation runs, and
`refreshFrozenCompanionBridge` resets and recompiles the whole bridge. Repeated
ready/refresh is therefore not bounded progress toward admission. The first
stop finishes; the second stop does not, so later Button A presses correctly
retain the pending transaction. Ring overflow prevents a complete timing verdict.

Required correction: separate stable canonical mapping/ordering from bounded
current-boundary closure reconciliation, preserving progress under continuous
NoteOn/Off emission. Do not remove ledger validation, pause playback, wait for
silence, or increase the quantum as a substitute for a progress proof. Add a
moving-ledger regression with multiple consecutive source changes and verify
original Offs exactly once before treating another upload as a fix. This is
diagnosis and an implementation requirement, not implemented or device-verified.
Task 2.3 remains open.

## Closure-only rebind implementation (2026-09-16)

**2026-09-16 bridge boundary reconciliation implementation:**
`refreshFrozenCompanionBridge` now invokes the shared compiler's
`rebindBoundary` rather than resetting bridge storage. Records, mapping, base
ranks, sorted base order and complete insertion order survive. The existing
merge scratch retains the complete insertion permutation after sorting; steady
compaction no longer destroys the order needed by another boundary binding.
Reconciliation runs only inheritance, affected-record checks, closure resolution
and cycle-index output in the existing entry quantum. No allocation, copy of
canonical events, or sort is introduced during this path. Original carried Offs
are enabled only by a matching live closure and disappear after it closes.

Admission checks the relevant subset of the ledger using fixed pitch/identity
summaries built from all changed records and inherited closures. Identity hash
collisions conservatively request reconciliation; they cannot bypass protection.
Changes to unrelated notes no longer invalidate preparation. Changes to affected
notes, cycle or original-Off deadline retain the closure-only reconciliation
path; no note protection or command-epoch check is removed. These smaller steps
still require device service/reservation validation; no unconditional deadline
or maximum stop-latency claim is made.

Native coverage compares repeated bindings against fresh compilation over six
cycles, changes unrelated notes every preparation turn, verifies unchanged map
and bridge storage generations, and preserves an inherited original Off during
a second source change. Capture refresh telemetry now says `closure_only`.
The provisional 6,240-grain baseline remains unchanged. Task 2.3 and hardware
acceptance remain open.

Validation of boundary reconciliation: full native suite reports 2,124 passed
and one skipped (`/tmp/bridge-rebind-all-native.log`). The final focused suite
passes all 153 tests (`/tmp/bridge-rebind-final-native.log`), including final
mapping-generation guards, repeated bindings and copied-Off retirement.
Next device checkpoint: at least four normal overdub start/stop passes without
transport Stop, retaining overflow-free `closure_only` refresh and completion
telemetry. Check actual reentry and original-Off continuity; host results do not
establish a real-time pass or close task 2.3.

Capture firmware build passed (`/tmp/bridge-rebind-firmware.log`) with 6,176
bytes RAM1 local headroom. Initial upload returned loader Error -1; one retry
succeeded (`/tmp/bridge-rebind-upload-retry.log`) and
`/dev/cu.usbmodem154944801` returned. No clear, MIDI stimulus or transport
command was sent. Consecutive-overdub behavior and deadlines are not yet
verified on this firmware.

## Consecutive overdub reentry latency (2026-09-16, 003056)

`captures/session_20260916_003056.log` confirms six accepted overdub starts in
the late test sequence. Deferred stops begun at 215.823160, 224.445991,
231.486392 and 242.378785 s finish phase-10 cleanup at 216.806890, 226.116419,
233.277526 and 243.835403 s: 0.984, 1.670, 1.791 and 1.457 seconds. Failed
reentry presses at 225.696, 232.595 and 243.256 s occur before cleanup completes;
the handler reports finalization continuing, not snapshot admission denial.
Subsequent presses enter overdub. Closure-only reconciliation completes in
roughly 17–36 ms in retained examples; initial canonical/tail preparation and
bridge compilation account for most of the delay. These are elapsed phase
spans, not continuous MIDI-blocking execution times.

Two additional stops, at 237.040780 and 247.351353 s, report
`preparation_service_unavailable` with an invalid ranked estimate and use the
synchronous compatibility path. Inspection shows admission falls back to the
small frozen-tail count when no full-loop current view exists, while the bound
rejects maximumPassCount > capacity. This can incorrectly reject a short tail
after several committed passes. Correct the estimator inputs using owned
prewarmed horizons before treating the sequence as wholly resumable. Then
overlap stable canonical/tail/bridge preparation with overdubbing to reduce
post-stop work without enlarging grains or bypassing atomic admission.

Ring overflow is present, so this capture is not a real-time deadline PASS.
Task 2.3 remains open. This checkpoint changes documentation only.

**2026-09-16 live canonical-base prework and stop estimate correction:**
The `003056` manual capture exposed a small-tail estimate incorrectly used as
candidate capacity. Stop now estimates from the prewarmed capture owner and the
prepared canonical base (or its conservative full capacity when unavailable).
The pass estimate includes record, existing overdubs and the incoming capture.
It no longer infers capacity from a rolling emission window or final tail count.

Live overdub and Stop share `stepCanonicalBase`: prefix normalization followed
by the existing pass merge and view fill/order worker, one child per owner turn.
The live VisualCache turn can prepare the immutable candidate bridge base while
recording the overdub. Paint dirtiness alone does not block this work while
visual reconstruction is disabled; wrap append and source-span work retain
priority. Source/storage changes invalidate prework; current playback and the
selected bridge remain untouched. Stop resumes partial work or reuses a ready
base. Frozen-tail, companion and cleanup work still remains after Stop, so this
is not proof of immediate reentry or a realtime deadline pass. Task 2.3 and the
HITL gates remain open; the provisional 6,240-grain baseline is unchanged.

Validation: full native suite 2,126 passed / one skipped; final focused suite
189 passed after the prefix-owner validation adjustment. Strict OpenSpec and
`git diff --check` pass. Capture-serial build and upload succeeded; serial
`/dev/cu.usbmodem154944801` returned. RAM1 local headroom remains 6,176 bytes.
The device upload finished at 00:44:27 local time on 2026-09-16. Manual reentry
and deadline acceptance remain open.

Manual capture `session_20260916_004305.log` ended at 00:44:02 local, before
this upload completed, so it is not acceptance evidence for the latest binary.
It contains four accepted overdub entries (21.003, 30.297, 33.451, 44.952 s).
Three PLAYING-destination stop spans, measured from first retained stop grain
to completed cleanup, are 744,987 / 649,353 / 1,730,182 us. Their canonical-base
phase takes only 55 / 54 / 52 us. The third stop spends 889,051 us in canonical
tail preparation, 113,740 us in companion history, 564,044 us in companion
bridge work and 97,083 us in cleanup (wall spans, not uninterrupted CPU work).
The 43.398 s reentry press occurs before cleanup ends at 44.009912 s; the
44.952 s press succeeds. Transport Stop later reaches STOPPED. No literal
`ring_overflow` record was found; this alone is not a deadline or completeness
PASS. The log also contains unmatched-Off capture warnings requiring separate
input/accepted-On correlation before attributing them to data loss.

**2026-09-16 continuous preparation across punch-in/out:**
Canonical preparation now runs during PLAYING as well as OVERDUBBING, using the
existing VisualCache owner and otherwise-unused wrap-append turns. Existing
record, stop, wrap-append and source-span priority remains. A confirmed newest
pass append with unchanged companion history preserves completed normalized
passes and resumes the first unfinished/new pass; snapshot identity publication
at the same content revision also preserves completed normalization. Unproven
source changes or changed history retain the existing safe invalidation path.

The companion merge now retains final per-pass output, in its original order,
in the canonical owner's existing raw scratch buffer. One extra prewarmed pass
descriptor array is required (16 KiB at 2,048 entries on Teensy); no additional
event buffer or active allocation is introduced. Each existing merge addition
also writes its normalized scratch entry, within the same Q-entry grain.
Successful stop cleanup adopts these results with pointer swaps after matching
source/worker generations, including when the committed companions changed
history. Reselecting current descriptors/history is resumable; completed pass
normalization is not replayed. Selected bridge/base and audible cursor remain
untouched. A scratch-owner receipt prevents another builder from being mistaken
for the retained result. `prefix_reuse` / `prefix_restart` records describe the
low-frequency ownership transitions rather than logging every prework grain.

Task 2.3 and HITL acceptance stay open. This does not claim instant reentry:
frozen-tail/bridge/finalization work remains, and new history during a wrap may
invalidate normalization for all potentially affected passes. The provisional
6,240-grain baseline remains unchanged; added bounded stores need device timing
validation alongside rapid punch-in/out, wraps and original-Off preservation.

Continuous-preparation validation: full native suite 2,130 passed / one skipped;
final focused suite 194 passed, including superseded scratch-owner rejection.
Post-commit adopted per-pass events match fresh canonical replay across the
existing stop variants, including companion edits and the large-capture fixture.
Strict OpenSpec and diff checks pass. Capture-serial build succeeds with RAM1
local headroom 6,176 bytes, unchanged. Upload and one retry failed: the Teensy
loader reported no boards on USB and requested the physical program button;
no `cu.usbmodem` port was present. This firmware is built but NOT uploaded or
HITL-validated. Reconnect/program-button action is needed before retrying.


## Next intermediate checkpoint: reserved output and reset recovery (2026-09-16)

The next local build adds physically reserved canonical-copy output and protects
an admitted stop job against playback-cache reset. This checkpoint measures memory
and single-job recovery before increasing simultaneous interval reservations.
It does **not** accept task 2.3: the three service-paused reentry fixtures still
fail and the FIFO/session-owner migration is unfinished.

After explicit upload authorization and selection of an empty scratch track/slot:

1. Capture boot through the existing saved-slot load. Check no allocation fatal,
   reset loop or hang. Keep the startup heap/PSRAM samples and each Tier-A
   `DIAG,capture_prewarm` record. Fields are zero-based track/slot; `full` is the
   requested mode; `ready` is canonical-output admission only. Expected successful
   output admission reports `ready=1,credits=8`; retain exact free pool and heap
   values. Do not clear saved material to make a failing prewarm pass.
2. In the authorized scratch slot, record a base and perform a paired-note overdub,
   then a held-note overdub. Let each job drain. Check exactly one terminal Off,
   correct committed notes/preview, and ability to begin the next drained session.
3. Repeat with transport Stop immediately after punch-out while preparation is
   pending. Confirm prompt output silence, preservation of accepted notes, no
   return to PLAYING from late cleanup, and a working later explicit restart.
4. Include enough events to cross a 256-row chunk boundary. Inspect existing grain
   telemetry around canonical copy, raw-chunk retirement and post-commit refill;
   correlate with MIDI deadlines. Require an overflow-free scored interval.

Do not inject destructive slot clear/load merely to force reset; native fixtures
cover explicit cache invalidation. Rapid reentry remains a known unimplemented
requirement and must not be reported as a pass at this intermediate checkpoint.
No firmware has been uploaded for this checkpoint yet. Final build identity and
native results are recorded in CONTINUOUS-CAPTURE-OWNERSHIP.md.


## 2026-09-16 authorized track 5 / slot 1 hardware checkpoint

Firmware on device: hex SHA-256
`d926209f15fd02f37a28371fb45fbf34e70e2a91fc86d523b34e30b50b017a35`.
User authorized upload and named track 5, slot 1 as the scratch location. The
normal probe did not clear it; subsequent interruption recipes cleared only that
authorized scratch slot. No other slot was cleared or workspace quarantined.

- `captures/task23-reset-normal/result.json`: two normal overdub stops verified
  through DeferredCleanup, no scored timing issues or capture overflow. This run
  had no correlated emission rows and is not a full audible-deadline proof.
- `captures/task23-reset-interruption/interruption-reverified.json`: focused
  single-track Stop precedence/silence **PASS**. Device Stop arrived 9,406 us after
  job entry; all-notes-off followed in 3,031 us; stopped cleanup completed after
  142,109 us. More than 15.9 seconds of post-cleanup device observation and actual
  USB MIDI output show no late NoteOn or return to PLAYING.
- The original interruption report was INCONCLUSIVE_CAPTURE because its verifier
  expected the obsolete visible OVERDUB_STOPPING-to-STOPPED edge. The corrected
  host verifier requires same track/slot/capture generation, a PLAYING-intent
  pre-Stop grain, a newer command epoch, and STOPPED DeferredCleanup completion
  before another job entry; earlier completion or another slot cannot qualify.
  Original logs/results are preserved; reevaluation has a separate filename.
  The broader baseline still reports incomplete expected musical/display markers;
  only the explicitly focused interruption verdict passes.
- `captures/task23-reset-restart/restart-result.json`: **PASS**, three complete
  playback cycles contain every expected base/overdub/held note, without extra,
  duplicated or unmatched notes. This is content survival, not deadline safety.
- `captures/task23-reset-dense-interruption/interruption-result.json`:
  **INCONCLUSIVE_CAPTURE**. The dense attempt lost USB, reconnected at
  13:44:47.654881 and showed a new boot. The captured boot marker says crash=0;
  no retained diagnostic identifies the reset cause. Stop/commit evidence was
  lost, so the greater-than-one-chunk device check has NOT passed. User confirmed the reset was spontaneous, after an overdub note needed to
  resolve a record-pass note. This identifies the observed trigger, not the
  underlying cause; allocator exhaustion and stack exhaustion remain unproven.

Full prewarm for the target slot reported output-ready=1 with eight credits;
internal heap samples were 294,912 and 73,728 bytes in the normal/interruption
runs respectively. This does not measure complete PSRAM headroom. The startup
capture was partial; retain the earlier INCONCLUSIVE_CAPTURE label.

Host regression tests: 24 pass across stop intervention, restart survival and
timeline-probe suites. Firmware unchanged during these host verifier edits.
Task 2.3 remains unfinished (FIFO/session owners and three reentry failures), and
neither the dense gate nor full task 5.3 matrix is accepted by this checkpoint.


**2026-09-16 overlap reboot investigation / human verification checkpoint:**
User confirms the dense-run reboot was spontaneous after an overdub note resolved
a record-pass note. A new production-route native test exercises Shorten of the
record-pass note (Off at hold-start minus one) and stop completion; it passes.
Focused native result: 12 pass, the same 3 unfinished reentry requirements fail
(`/tmp/task23-overlap-final.log`). This does not reproduce or explain the device
fault; the capture-only overlap diagnostics are absent from the native fixture.

Found and fixed a crash-evidence defect: the installed Teensy core's
`CrashReport::printTo` clears its retained report. Startup previously printed to
Serial, then tried to print again to SD and sampled the now-cleared boolean.
Consequently the dense-run `crash=0` cannot exclude a recorded core fault. Startup
now snapshots the boolean before printing and renders once to a sink that writes
both Serial and `crashlog.txt` when SD opens. No claim that this fixes the reboot.

Capture-serial build and upload pass (`/tmp/task23-crash-report-build-final.log`,
`/tmp/task23-crash-report-upload.log`). Uploaded hex SHA-256:
`11bbc435befcd5dba617678bdd29ce4ecb5d9bbf65d04b792613a5190a460e73`.
RAM1 local headroom remains 5,600 bytes; startup sink methods reside in flash.
Post-reboot dense capture reported PSRAM 4,158 KB free / 4,024 KB used at startup;
that is not the memory state at the failure.

Next HITL: reproduce the observed record-pass/overdub overlap on authorized track
5 / slot 1 with the serial reader in `captures/task23-overlap-crash-report`.
No automated MIDI stimulus is running. Preserve the reconnect and startup output;
if Serial misses it, the one-pass crash sink can retain the report on SD.
Task 2.3, the dense gate, and full task 5.3 remain open.


**Dense-test interpretation clarification:** the failed hardware run was explicitly
intended to exercise more than one 256-event chunk. It did not complete, so native
chunk tests cannot close that device gate. The user's observed overlap is a clue,
not an established alternative cause. The retained log does not establish that
the first-to-second chunk transition caused the reboot, nor that deferred
canonical-copy processing was reached. Preserve both boundary and overlap paths
in the next reproduction. Serial capture is now open in
`captures/task23-overlap-crash-report/session_20260916_135500.log` (exec session
28933); the prior 135431 attempt could not open the sandboxed serial device and
was stopped. No automated MIDI stimulus is running.


**2026-09-16 user correction / automated rerun:** the dense chunk test is an
agent-operated HITL recipe, not a required manual reproduction. The standalone
serial reader was stopped, and the exact earlier dense recipe was rerun against
track 5 / slot 1 with the diagnostic firmware. Evidence directory:
`captures/task23-dense-crash-report-rerun`; runner output:
`/tmp/task23-dense-crash-report-rerun.log`. Do not require manual gestures to
repeat an already automated test. Result pending.


**Automated dense rerun result (2026-09-16 13:57):** identical recipe on track 5 /
slot 1 rebooted again on diagnostic firmware
`11bbc435befcd5dba617678bdd29ce4ecb5d9bbf65d04b792613a5190a460e73`.
`captures/task23-dense-crash-report-rerun/session_20260916_135633.log` records
Overdubbing start at 188.453 s, last pre-reboot output at 190.100 s, reconnect at
13:57:21.424792, and `boot_reset,crash=1`. This confirms a retained core crash
report, unlike the previous destructive boolean read. Serial reconnected after
the early crash text, so the fault address/type are not in this capture; the
new sink attempted to preserve them in SD `crashlog.txt` (file not yet read).
The runner finished and its serial/MIDI owners closed. Focused verdict remains
INCONCLUSIVE_CAPTURE with incomplete_capture/no_device_stop_inside_stopping;
the dense hardware gate has not passed. Chunk-boundary causation remains unproven.
No manual reproduction is required to repeat this automated recipe.


**Crash diagnosis and targeted diagnostic-stack change (2026-09-16):**
The stopped-only `!CRASH_LOG` command retrieves the last 4 KiB of `crashlog.txt`
without modifying it, into the host capture. Retrieval succeeded:
`captures/task23-dense-crash-report-rerun/crash-log-retrieval.log` contains a report
at system time 13:57:08 matching the rerun: PC=0x6358C, CFSR=0x82 (DACCVIOL),
MMFAR=0x20016A3F, identified by the core as the stack boundary. The saved matching
ELF `/tmp/task23-crashed-11bbc435.elf` maps PC to `_svfprintf_r`, with a failing byte
store during numeric formatting. This establishes a stack-boundary access fault;
it does not identify chunk rollover as the cause or reconstruct the caller stack.

The overlap observation path stacked two 128-ID sets and a 256-byte formatting
buffer above noteOn's PendingNote. Moved only these diagnostic temporaries to
foreground-only RAM2 scratch, cleared on every use. No note/session ownership or
resolution semantics change. Compiled snapshotOverlapHoldCandidates local stack
allocation falls from 1,372 to 76 bytes (register saves excluded in both figures).
RAM1 headroom remains 5,600 bytes; RAM2 free becomes 472,928 bytes.

Build/upload pass (`/tmp/task23-overlap-stack-build-final.log`,
`/tmp/task23-overlap-stack-upload.log`). Firmware hex SHA-256:
`e8a6b46d45576f7c96e4d6d3423beb267abdabde3c67b0cad4b3da0564417d86`.
Identical dense recipe is running under `captures/task23-dense-stack-fix`.
No acceptance claim pending its result.


**Post-fix device results (2026-09-16):**
- Exact original recipe, `captures/task23-dense-stack-fix`: no reboot; canonical
  copy phase and terminal DeferredCleanup complete. Copy phase has 19 observed
  turns, maximum 104 us, including raw-source retirement turns. The 578 grain
  field is capture generation, NOT a measured row count. Do not derive chunk
  count from it. Stop arrived 47,320 us before job entry, so the focused
  interruption verifier correctly remains inconclusive for this run.
- Same dense musical stimulus with only intervention delay changed from 305 to
  380 ms, `captures/task23-dense-stack-fix-stop-window/interruption-result.json`:
  focused single-track Stop precedence/silence PASS. Device Stop arrived
  82,042 us after job entry, all-notes-off followed after 3,039 us, stopped cleanup
  completed 168,895 us after Stop, and 18,976,636 us of subsequent observation
  contain no late NoteOn. No reboot occurred.
- Both broad baseline reports remain INCONCLUSIVE_CAPTURE for their reported
  setup/content/display/persistence marker issues. These are not waived. Neither
  run proves restart survival, layered playback, full deadline coverage, or the
  remaining FIFO/reentry requirements. Both runner/capture owners have exited.

The matching crash evidence plus two post-change no-reboot dense runs support
keeping the reduced-stack diagnostic scratch. Task 2.3 remains open. Next
integration work remains fixed FIFO and isolated session/preview/undo ownership;
full dense content/count and timing acceptance still needs explicit evidence.


**2026-09-16 dense content/timing verification and descriptor integration:**

Read-only `!CAPTURE_CONTENT` prints selected stopped-loop raw passes and effective
canonical events into the serial capture. It denies active capture, transport or
pending stop; it does not edit the slot. The last successful upload for this
readback has hex SHA-256
`f8565f07728784bb114ee14370432916957d59b9a4a6c296658ef34aad18af8f`.

`captures/task23-dense-content-retry/input-review.json` verifies 568 stored overdub
events (three 256-event chunks): 188 NoteOns, 188 matching Offs, 192 CCs. Note
coordinates/values match the original dense capture exactly; CC values match in
order. Five input pairs (pitches 19, 101, 121, 52, 63) lasted 7, 6, 7, 7, 1 ticks
and are excluded by the existing enabled eight-tick minimum. Held pitch 83 closes
at the original stop phase 2835. No unexplained missing/extra retained note rows.
The record pass has 192 events; effective content has 760. Source checks and
capture acquisition scripts are retained beside the evidence. This does not
prove CC tick equality or full effective companion semantic parity.

`captures/task23-dense-stack-fix-stop-window/dense-timing-review.json` shows no
capture loss/malformed stop rows but is NOT a timing PASS: one CanonicalOrder grain
crossed its recorded service deadline, another began overdue; two captured clock
samples were 3,899 and 1,885 us late during PLAYING. CanonicalCopy observed maximum
102 us. There are zero correlated emission rows, so audible deadline safety is
unproven regardless of the coarse verifier's empty issue list.

Restored playback verification is incomplete: unclocked and explicitly clocked
restart probes received no target-channel musical events, so no full cycles could
be compared to effective canonical rows. See `captures/task23-dense-content-clocked`
(content-review.json and service-status.log). Transport and output-policy/readiness
cause are unresolved; do not call this content-playback PASS or infer data loss.
Selecting an already selected slot toggles playback, so acquisition explicitly
stops again before the content request. It never clears/records new data.

The stop receipt now lives in a cold-prewarmed external-memory-first ring of four
stable descriptors on Track. The existing worker borrows the head; cleanup clears
its runtime pin before advancing to the next descriptor. Completed slots are
reused without copying job-owned maps. The ARM allocation is 616 bytes/track
(4,928 bytes for eight tracks), excluding map nodes and allocator overhead;
RAM1 static headroom improves from 5,600 to 6,752 bytes. These four metadata slots
are NOT a declaration that four simultaneous captures are supported. Admission
still retains the singleton staging/session guard. Missing prewarm rejects before
opening input. No full staging buffers or worker graphs were duplicated.

Native sequential rotation test covers stable receipt address, pin lifetime,
head wrap/reuse, and existing snapshot handoff service between sessions. Three
public reentry requirements still fail. Raw-range/staging/preview/companion/undo
and predecessor isolation remain required before allowing multiple queued jobs.

Descriptor build passed (`/tmp/task23-fifo-descriptor-build.log`). Attempted upload
failed: no Teensy found and serial port absent (`/tmp/task23-fifo-descriptor-upload.log`).
The descriptor migration is NOT verified on device. User has been asked to
reconnect/press PROGRAM. No serial/MIDI owner is active. Broader native validation
and final build results follow below; task 2.3 and full hardware gates stay open.

**2026-09-16 dense verification / FIFO descriptor slice:** stored dense overdub
content matches input (568 events, three chunks, five expected short-pair removals).
Timing is NOT passed: a stop ordering grain crossed a service deadline and two
late clock samples are retained. Restored musical playback produced no target
channel rows in the probes; output policy/readiness remains under investigation.
Stable external Track descriptors replace the singleton stop receipt; sequential
rotation passes, but staging/session/predecessor isolation and three reentry tests
remain open. Full native: 2,159 pass, one skipped, three expected reentry failures;
final focused admission/rotation: 14 pass and the same three failures. Build passes
with 6,752 bytes RAM1 headroom. See active FOCUSED-STOP-HITL.md for evidence and
latest upload status; no task checkbox closes.


**2026-09-16 latest device state / verification blocker (14:29):**
The serial port reappeared after the first upload failure. Final descriptor build
and retry upload succeeded (`/tmp/task23-fifo-final-build.log`,
`/tmp/task23-fifo-final-upload.log`), hex SHA-256:
`a9a7217fc99a0eca5a1cbee199179211822046201707f9327f1018a043ca2066`.
This is the last successfully flashed firmware; earlier f856 readback was on the
previous firmware. It includes the final reject-before-capture descriptor guard.
Full native run: 2,159 pass, one skipped, three existing reentry failures
(`/tmp/task23-fifo-all-native.log`). Final focused run after the guard test:
14 pass and the same three failures (`/tmp/task23-fifo-final-focused.log`).
Strict OpenSpec validation and diff checks pass. No commit or archive performed.

Read-only post-upload status shows track index 4/slot 0 unmuted, audible and
enabled. The barrier=1 was sampled after transport Stop and is expected; it does
not explain earlier no-output playback. Boot output reservation prewarm succeeds
with eight credits, 440 free chunks and heap_free=339968; full-view readiness is
not implied. Descriptor allocation is cold and source/session migration remains
incomplete.

A fresh dense hardware recipe on this build cleared only the authorized track 5 /
slot 1. It then lost the serial device during INITIAL RECORDING, before overdub
or intervention. Last retained device output is 98.445 seconds. No reconnect or
new crash marker was captured. Evidence: `captures/task23-dense-fifo-descriptors/`
and `/tmp/task23-dense-fifo-descriptors.log`, verdict INCONCLUSIVE_CAPTURE. Do not
call this the same overlap crash, a proven FIFO regression, or a successful
hardware validation. The runner and serial/MIDI owners have finished; no serial
port is currently visible. User reconnection/program-button help was requested;
no answer confirming manual disconnect has been received.

Next: reconnect, retain startup/crash evidence for this disappearance, resolve
restored playback output and the measured deadline failures, then finish sealed
range/live reservation/session/predecessor isolation before multiple FIFO admission.
No additional FIFO buffers/guards were loosened to bypass these verification gaps.
Task 2.3 remains incomplete (12/20 overall).


### 2026-09-16 resumed dense verification and CC preservation

The device reconnected. `captures/task23-fifo-reconnect/reconnect.log` retained
only the old 13:57 fault report, not a cause for the later USB loss.
`task23-dense-fifo-retry` stopped at setup. Host save-idle classification wrongly
treated `FinalizeWorkspace,singleton,done,ok` after `PERS,result,...ok` as new
work. The classifier now recognizes terminal completion; 19 host tests pass,
including later-request/work and intermediate-completion counterexamples.

`task23-dense-fifo-idle-retry` completes on a9a7217f without reboot. Focused
interruption PASS: Stop +85,102 us, cleanup +193,717 us, all-notes-off +3,039 us,
silence 18,784,522 us. This remains a scoped result. Its timing review has one
late NoteOn (1,216 us) and two copy-grain deadline crossings; no timing pass.
`task23-dense-fifo-playback/input-review.json` matches 356 stored note rows to
input coordinates after 15 configured minimum-length removals and held closure;
192 CC values match in order. Overdub 548 rows/three chunks, effective 740.
Four immediate restart cycles contain all notes by multiplicity, but omit three
CCs at ticks shared with older CC74 rows. Exact ordering is not passed.

The incremental seal merge treated all coincident non-note events as duplicates.
Its new native regression fails before the fix and passes afterward (133 tests).
That fix alone, firmware de1411f4c51e2ac2f48cdd9cfced7458c84ceda9c392ea5fdb458fc3ed3e1fa7,
built/uploaded and was exercised by `task23-dense-cc-fix`. Stop again passes
(+70,568 us intervention, +212,718 us cleanup, 19,062,193 us silence).
`task23-dense-cc-fix-playback` matches all 350 overdub note rows and 192 CC values
against input (18 expected short-pair removals); four restart cycles still omit
two coincident CCs. One extra raw terminal Off is also suppressed; ledger semantics
must be accounted for before comparing effective data to physical output.

The remaining CC loss is reproduced in snapshot publication: its event-membership
comparison also ignores CC controller/value. The snapshot fix compares both; a
public journal/gather regression fails before and passes afterward (128 survival
tests). The full native run before this second fix: 2,161 pass, one skipped,
three known public-route reentry failures. Latest snapshot firmware verification
is still in progress. No FIFO/reentry/content/timing/task checkbox is closed.
