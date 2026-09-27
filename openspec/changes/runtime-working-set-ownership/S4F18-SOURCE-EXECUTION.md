# S4f.18 — frozen record source consumer

## Scope and readiness

Status: consumer native/build verified; parent gates open. Baseline: d47d2117 with pre-existing HITL/doc changes preserved.
Contract: SHARED-CAPTURE-FINALIZATION.md; connected S4f.17–S4f.21 authorization.
Reuse DEC-064 — exclusive raw capture ranges and DEC-093 — shared finalization.
Extend RuntimePlaybackViewSource/Fill, not the worker ownership graph. CaptureRange
retains exclusive chunks until fill completes; destination owns copied window rows.
The existing order builder handles ordering in separate bounded grains.
No record-stop admission, closure synthesis, canonical publication, undo, or device
acceptance is claimed by this prerequisite. No new format, owner, or queue.

## Oracle fixed before implementation

- Frozen source larger than the 2,048-row overdub staging capacity: each fill step
  examines at most its quantum, including rejected rows; a small matching window
  fits without requiring a whole-take destination. Producer appends do not enter it.
- Record tick projection wraps overflow NoteOns, drops overflow other rows, and
  preserves the immutable source. Existing inline normalization uses the same tick
  policy. This is tick projection only, not held-note or minimum-length finalization.
- Released/reused/reset or not-yet-sealed sources deny; an expired source cannot
  mark a destination complete. A failed row read denies without publication.
- Matching rows exceeding prewarmed capacity deny. Empty sealed sources complete.
- Fill and order remain separate. No physical chunk allocation during consumption.

## Controls and bounds

Real CaptureRange and RuntimePlaybackViewBuild in test_playback_prewarm. No mocks
on the asserted path. New source API cannot link before implementation; this is an
API prerequisite, not a claimed pre-existing behavioral red test. Fixed drains use
source count/quantum plus bounded order iterations. O(quantum) source reads per
fill; total scanning remains O(source rows), so onward playback deadline proof is
still required. Borrowed source object must outlive fill. Tagged callback union
reuses the existing pointer slot; ARM layout pins and RAM1 padding remain gates.

## Verification

Planned: focused playback/capture suites, full native, capture-serial build,
OpenSpec validation. No device action: consumer is not yet connected to record
stop. Parent tasks remain open. Results appended below after execution.

## Review of the implemented consumer

G0 PASS: CaptureRange::read validates sealed chunk identity/extent and reads one
row by directory index; ready/size/generation are constant-time. The existing fill
and order owners already separate copying from sorting. No Loop or slot selection
is introduced. The old record path keeps its complete normalization sequence;
only its tick projection expression is shared.

G2 PASS WITH TIGHTENING: generation is checked even when a reused source has the
same extent; a reader may fail independently of generation and must deny. Tests
exercise both. Source lifetime remains the caller's responsibility until fill is
complete; ordering then borrows only destination rows. Producer append uses a
separate chunk directory. Pool reset invalidates the frozen reader. Partial output
is never marked ready after denial. Existing AllSource, PlaybackCoverage and
RawCoverage paths retain their interpretation; typed baseline provenance refuses
RecordCoverage. No stop, slot/track switch, undo, load/save, clock, persistence or
ledger ownership route changes in this consumer slice. Those integration attacks
remain mandatory in the connected contract, not exclusions from it.

Long source extent is not limited to overdub staging; matching window overflow
still denies. This is not a proof that rescanning a long source meets a playback
handoff deadline. No synchronous full-source walk or allocation was added to
begin/step; each step visits at most its supplied row quantum, with one stack row.
The copy-reader tag uses padding in the persistent fill owner; the temporary
source descriptor gains a tag (ARM layout/build verification below).

Session closeout: implements existing decisions, no new architectural policy or
excluded contract case. No decision-log append or shipped-deliverable claim.

## Verification results

- Initial focused playback/range suites: 186/186 before adding the third fault
  fixture and strengthening the same-extent reuse test.
- `pio test -e native`: **2,504 passed / 7 skipped**, 2,511 total, 326.464 s.
  The same-extent fixture was tightened to leave spare destination capacity
  after its full-run executable had run; the restored focused rerun below covers
  that test-only change. Production code is unchanged from the full run.
- `pio run -e teensy41-capture-serial`: PASS, 103.080 s. Existing ARM runtime layout
  pins pass. RAM1 variables 96,160; code 392,376; padding **840**; locals 34,912.
  RAM2 variables 155,232; EXTRAM 9,312. FLASH code 1,041,600 (+320 vs baseline).
- `openspec validate runtime-working-set-ownership --strict`: PASS.
- No upload, device run or SD operation. No parent task eligible for checkoff;
  OpenSpec progress remains 50/176. Existing strict record-integration failures
  are not addressed by this consumer prerequisite.

- Final focused run: `pio test -e native -f test_playback_prewarm -f test_capture_range`:
  **187/187**, 7.110 s, including all three new fixtures.
- Mutation: remove the fill step's generation validation. The same-extent reuse
  case fails with Denied expected / Pending observed; size and capacity cannot
  accidentally satisfy that assertion. Exit 1.
- Mutation: bypass record tick projection. The long-source window case fails with
  Order expected / Denied observed (overflow Off/CC rows were wrongly retained).
  Exit 1. Both mutations restored before the final focused pass; firmware source
  matches the full-native/build version.
- Fixture correction during validation: prewarmed view capacity cannot grow in
  place. An initial tightening incorrectly tried to resize it and failed at setup;
  that failed run is not mutation evidence. The final fixture prewarms two rows,
  uses three matching rows for overflow, and reuses a same-size frozen source with
  one destination row still free for the independent expiry assertion.
- `git diff --check`: PASS. Runtime state, current work, task progress and execution
  bounds updated; no playback ledger/order policy or device behavior changed.
