# S4f.17 — common interval and stop admission

Status: **connected implementation authorized**, in progress. On 2026-09-27 the user approved the required S4f.17–S4f.21 scope. No individual task is complete yet.
Baseline: `c4e130d5`, plus the existing dirty shared-finalization contract, S4f.16 native
fixtures and earlier HITL changes. No firmware edits made during this preflight.
Authority: [shared contract](SHARED-CAPTURE-FINALIZATION.md), DEC-093,
[execution-packet requirements](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice).

## G0 evidence and dependency closure

VERIFIED from current production code:

- `Track::startRecording` reserves/reset/prepares first-bar state, transitions to recording,
  opens canonical Record capture and admits pre-roll. Unlike `startOverdubbing`, it does not
  advance `captureInterval_`. This is an independently testable identity prerequisite, but
  implementing it alone would not complete S4f.17.
- `driveAcceptedInteraction` calls `beginDeferredOverdubStopForSlot` for every accepted stop.
  That admission requires live overdub staging, an overdub transfer reservation and a
  session-exit receipt; it exchanges staging and binds an overdub stop reason. Record input
  does not populate those reservations. Reusing the entry unchanged is not shared admission.
- `processDeferredOverdubStopGrain` requires the job's matching frozen staging before any
  phase runs. Canonical ordering/copy and held-note closure read that staging. A raw Record
  range alone cannot make this worker complete. The bounded Record reader/normalization
  dependency is S4f.18.
- `TrackPreparedStopCommit` selects `OverdubStop`/`OverdubStopToStopped`; deferred undo uses
  the overdub session transaction. Record pass/geometry/history semantics are S4f.20.
- `prepareRecordStop` currently scans the last event, closes pending notes and remaps the
  store before inline commit. Moving that body into admission or a single deferred grain
  would retain content-size work and violate the bounded contract. Its maintained summary
  and bounded source preparation are prerequisites, not an allowable temporary fallback.
- `stopRecording` currently binds the first recording bar only after inline publication.
  A first-bar role prepared at start is not proof that the frozen, normalized source is
  ready; onwards playback before publication is S4f.19.
- `TrackManager::stopRecordingTrack` reads length immediately after the void stop call.
  `finalizeCaptureAndSelectSlot` does the same and requests persistence before switching.
  `stopAllTracks` also relies on synchronous finalization. Those continuations must migrate
  with enabling deferred record entry (S4f.21), not after it has become reachable.

Owners and lifetimes remain those in DEC-093: Track latch/ring; job raw/geometry/held-note
receipts; Loop canonical construction; runtime audible sources; protected undo; storage
admission. No additional Manager, queue, finalization capacity, schema or undo kind is proposed.

G0 verdict: **PASS for the dependency trace**. G2 verdict: **BLOCKED for standalone production
enablement of S4f.17**. The listed later tasks are prerequisites of its safe production route.
The task numbering is not a valid independently enabled firmware commit sequence.

## Adversarial findings

BLOCKER — Record source mismatch: binding record into the current worker either fails
preflight or leaves a job without the staging/source needed for progress. Test: freeze record
A through production admission and run its bounded worker to terminal publication.

BLOCKER — Caller continuation: making the void record stop return accepted/waiting lets the
manager consume unready geometry and switch early. Test: withhold readiness/capacity, issue
one switch, assert A remains live and B does not activate until A binds.

BLOCKER — Wrong publication semantics: the current prepared path selects overdub reasons and
undo. Test: record job publishes one RecordPassAdded entry with its own before/after geometry,
slot-history treatment and persistence identity while selection has moved.

TIGHTENING — First-bar and later-window ownership, runtime regeneration, empty capture,
transport/clock retargeting, same-slot restart, clear/load/save and undo during pending work
must be verified across the connected route; native binding alone does not prove these.

COVERED by the approved contract, still requiring implementation evidence — fixed-role
RAM/PSRAM, maximum active working set, no extra MIDI service/scheduler, bounded long/dense
preparation, generation-qualified display/playback, no synchronous fallback. RAM1 floor remains
840 bytes; this preflight adds zero firmware state and makes no new measured-memory claim.

## Proposed implementation boundary (awaiting scope choice)

Implement the connected required portions of S4f.17–S4f.21, preserving their separate checkboxes
and test gates. Stage source preparation, typed receipts, playback handoff and record publication
inside existing owners; enable production record stop and its manager continuations only once
that path is complete. No production switch to an unfinished worker, dead-end accepted request,
whole-take staging copy or inline fallback is an acceptable checkpoint.

An alternative narrower slice can implement and verify common start identities and receipt
prerequisites only, without changing record stop routing. That is explicitly partial S4f.17;
it cannot be described as shared record finalization or close the parent task.

## Fixed transition oracle and verification

Retain the S4f.16 four-mode fixtures and their strict assertions. Before changing behavior,
extend the fixture to observe accepted/bound mode, slot/LoopId/interval, raw and held-note
ownership, final geometry, unchanged published generation, successor input and exactly-once
publication/undo/persistence. Failed preflight moves no receipt. Repeated drive is not a press.

Add stale replacement, empty input, occupied canonical owner, runtime regeneration and terminal
wait/recovery cases using the real owner and allocator/clock boundaries. Preserve MIDI content
with independent expected rows. Bound grains by admitted work and explicit setup/terminal
allowances; expose stuck phase/identity/receipts. No sleeps or production pause hook.

Verification remains focused native, full native, capture-serial build and role/RAM accounting,
then the required registered response-driven HITL gates. Existing S4f.16 evidence (2,490 passed,
7 skipped; four strict expected failures) applies only to unchanged baseline fixtures.
This preflight has not rerun native, firmware build or device tests. No task is checked off.

## Authorized connected implementation — first executable prerequisite

User answered yes to the connected scope. The standalone-enable blocker is resolved by retaining
production record stop until its complete consumer/continuation path exists. First fix shared
interval advancement at actual start and reject invalid record state transitions before reset.
No new state owner or memory field is needed for this prerequisite. Fixed oracle: one identity
per actual start, no increment or content reset on a repeated/invalid start, different-mode
successors get distinct identities. Existing job identity must survive successor start.
Later source preparation, handoff, publication and caller gates remain mandatory.

## Implemented prerequisite evidence (2026-09-27)

- Shared start identity: `beginCaptureInterval` is reused by both actual capture starts.
  Record eligibility is checked before slot reset; repeated/invalid requests neither increment
  identity nor erase input/committed content. Pre-fix focused run: four intended failures
  (missing record stamp, invalid-start reset, R/R and OD/R identity).
- Accepted-stop identity now also binds LoopId and Capture mode. The drive checks these before
  admission/parking and avoids allocation when the loop pool is absent. Two pre-fix tests
  demonstrated replacement/mode requests incorrectly remaining pending. Both pass after the fix.
- The job records its capture mode in existing layout padding. Latch LoopId/mode use its padding;
  existing native/device layout pins remain authoritative. No new owner, queue or memory field on
  the Track array. The native fixture still measures job/ring sizes; firmware build checks pins.
- Record maximum tick reuses Loop's previous last-append scalar. Maximum is maintained on
  accepted append and the existing reset/sort/removal paths, so the record length query no longer
  materializes all rows. No full-stop bound is claimed: later normalization/closure remain inline.
- Maximum-tick mutation: replacing maximum with last appended tick made the out-of-order fixture
  produce **768 instead of 2304**. Source restored automatically. The first removal-fixture
  attempt lacked the removal API's nonzero geometry precondition and failed both before and
  after the edit; it was corrected to exercise that API legitimately, not counted as red proof.
- Real chunk reservation exhausts the pool for record-start preflight: old content, geometry
  and interval survive denial, and retry starts once after the reservation is released.

Focused suite before the final mode-receipt assertion: **11/11**, 7.203 s. Earlier full native
(identity/summary prerequisites): **2,494 passed / 7 skipped**, 211.509 s. Final full/native and
build results follow below; previous counts do not imply the later edits were covered.
Baseline capture-serial: RAM1 variables 96,160, ITCM code 392,360, padding 856, locals 34,912.
Identity/summary build: variables unchanged; code 392,376, padding **840**, locals unchanged.
Record-start/length mutation adds no fields; the receipt additions must preserve the existing
layout pins in the final build. No upload or device run. Reused registered stimulus for eventual
baseline regression: `STIM-BASE-2-ONE-OD-01@v1` (listed through the authoritative CLI); the
response-driven mixed acceptance remains separate and open.

S4f.17–S4f.21 remain open: record binding, bounded normalized raw-source preparation,
held-note transfer, frozen-source playback beyond bar one, record undo/publication and all
manager continuations are not implemented by these prerequisite edits.

## Verified prerequisite checkpoint

Final focused production suite: **11/11** (1.231 s). Full working-tree native suite:
**2,497 passed / 7 skipped** (187.353 s). Capture-serial build passed (59.648 s):
RAM1 variables **96,160**, code **392,376**, padding **840**, locals **34,912**;
RAM2 variables **155,232**, EXTRAM variables **9,312**. Native/device layout pins pass.
No upload or hardware acceptance.

Strict contract rerun: **7 passed / 4 expected failures**. Record-first rotations still
fail deferred job ownership; overdub-to-record still lacks B's deferred record job;
overdub-to-overdub still fails persistence admission. The common interval assertion
now passes. These failures remain mandatory implementation gates, not accepted exclusions.

Checkpoint scope is the actual-start/accepted-stop identity and scalar length invariant,
plus its production fixture dependencies. Earlier unrelated HITL driver changes remain
outside this checkpoint. Full native totals above describe the working tree, which also
contains those earlier fixture additions. Connected implementation remains unfinished.
