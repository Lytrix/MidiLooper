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
