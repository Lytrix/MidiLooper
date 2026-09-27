# S4f.16 — production finalization fixtures

Status: complete (fixtures only). Scope is fixtures only; no S4f.17 behavior implementation.
Contract: SHARED-CAPTURE-FINALIZATION.md, COMMON-IDENTITY and MODE-ROTATION.

## Preflight and owner closure

Continuous-capture now links real record stop, but its stop-family persistence requests are
inert and its empty reset is duplicated in the harness. It cannot prove the required owner
closure. Reuse the drain suite's linked storage/undo closure as a shared include, enable the
real record-stop and cold-helper TUs, and keep existing drain cases unchanged. Physical MIDI,
display and off-path edit operations remain boundaries; no stop/commit/undo/persist owner may
be impersonated. All prior dirty edits are preserved.

## Fixed oracle before edits

Start A through the real record/overdub entry; inject a note pair; stop exactly once; inspect
live job ownership before stepping the worker. Record's current inline publication must fail
an explicit contract-run assertion requiring a deferred owner. Normal baseline characterization
records that known gap, never claiming shared firmware behavior passed.

Prepare independent B on another slot, start it through production entry, require distinct
intervals in contract mode, inject content, stop once with A unstepped, then step the actual worker with a finite
turn bound and assert no duplicate publication. Matrix: R/R, R/OD, OD/R, OD/OD. Read source
identities, raw range/queue/phase and undo/persist effects, not arbitrary log text. Fixture
selection of a slot is an input seam; manager switch behavior is S4f.21, not mocked proof here.

## Bounds and resources

Use existing native clock and prewarmed pool. No production pause hook. Fixed two-note inputs
and 32-entry worker quantum; cap scheduling by admitted credits plus terminal allowance, with
a 20,000-turn diagnostic watchdog. Print phase/identity on failure and record job/ring sizeof
and free-chunk changes as native diagnostics;
device RAM1 floor and actual admitted-credit accounting remain gates for behavior slices.
No new firmware fields, memory allocation policy or scheduling rules.

## Verification

Focused new suite, explicit expected-red contract mode, existing drain/continuous/runtime
suites, then full native. No firmware build/upload or device claim for test-only changes.
Completion results and any linked-owner limitations are appended after execution.

## Measured closure and behavior (2026-09-27)

`test/test_shared_capture_finalization` uses `test/test_support/capture_storage_harness.inc`:
extracted drain closure plus `TrackCaptureStopCommit.cpp`, `TrackStopTelemetryColdHelpers.cpp`
and `RecordStopLength.cpp`. Its nested `track_harness.inc` links capture input, overdub
lifecycle, deferred maintenance, prepared stop commit and protected undo. The shared include
links `TrackUndo.cpp`, real persistence admission/work queue/deferred save and the drain's
storage closure. Those include lists are the authoritative exact TU inventory. No duplicated
empty-capture reset or stop/persist stand-in is enabled in the new suite; a compile guard
rejects `BYPASS_STOP_UNDO_SAVE`. Existing suites retain their explicit isolated boundaries.

Manager slot selection, hardware MIDI output, clock and in-memory SD are native boundaries.
The fixture selects the Track input slot directly; record uses the real `stopRecordingToStopped`
route while the native global transport predicate stays running. This proves ownership and
publication, not audible continuous playback, DROID gesture timing, SD bytes or all caller routes.
Those remain the later contract tasks. Storage assertions identify queued `LoopPersist` slot
keys; undo assertions identify kind, slot and LoopId. Both intervals' exact two MIDI rows and
768-tick geometry survive retirement and ten extra polls without duplicate publication.

Four baseline rotations run with A's worker unstepped while B starts and stops. OD/OD proves
one A job plus B's accepted latch; actual `processDeferredIdleMaintenance` dispatch retires A,
admits B and retires B without a second press. Record cases characterize the known gaps.
`S4F16_REQUIRE_CONTRACT=1` enables the same fixtures' strict future assertions:

- R/R and R/OD fail because A has zero deferred jobs immediately after record stop.
- OD/R fails because record B does not receive a new interval identity.
- OD/OD reaches retirement but fails its slot-dirty persistence assertion. The real deferred
  worker does not call the persistence request helper; S4f.20 must wire that handoff.

These expected failures are evidence that the fixture detects missing behavior, not passing
firmware acceptance. As S4f.17–S4f.20 land, replace baseline gap characterizations with the
strict assertions; do not delete or weaken the contract checks.

### Bounds

Worker dispatch advances one owner turn. All contracted preparation quanta observed at each
turn are asserted <=32. Scheduler allowance sums the admitted phase credits per newly bound
job plus 128 setup/terminal turns per job and two latch-admission turns. An additional 20,000-turn
watchdog prevents oversized conservative production credit estimates from making a failed test
expensive. Neither allowance is a wall-clock delay or a worst-case execution-time proof.
For these fixed two-row inputs, measured worker turns are R/R=0, R/OD=46, OD/R=46,
OD/OD=93. Admitted estimates are much larger (17,068,284 for one job including allowance;
34,136,566 for two); later bounded-preparation tasks must verify actual per-grain input units.
Native sizeof: CaptureStopJob=984 B; ring including latch=3,984 B. Free pool chunks before/after
worker drain: 490/490, 489/489, 489/488, 488/487 respectively. These are retained-state
measurements, not a device RAM1 budget or a leak claim. No production memory policy changed.

### Closeout

No new architectural decision or exclusion: implements DEC-093's fixture prerequisite.
No firmware behavior change, build, upload, SD operation or acceptance checkoff.
Verification results are recorded below after the complete native run.

- Full `pio test -e native`: **2,490 passed / 7 skipped / 0 failures**, 95.367 s.
  Includes drain re-entry, continuous-capture and runtime-working-set dependent suites.
- Final `pio test -e native -f test_shared_capture_finalization`: **4/4**, 6.503 s,
  after adding explicit retained raw-range/LoopId assertions and the diagnostic watchdog.
- Run that freshly built native program with `S4F16_REQUIRE_CONTRACT=1`: nonzero exit,
  **4 expected failures**, checked by assertion text (deferred job twice, interval once,
  persistence once). This is deliberately not a green test configuration.
- `openspec validate runtime-working-set-ownership --strict`: valid; `git diff --check`: clean.

Next: S4f.17 — common interval and stop admission. S4f.13 — accepted-sequence acceptance
remains open; this native fixture does not substitute for continuously playing device evidence.
