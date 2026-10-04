# Task 3.10 — requester/retry integration

## Scope and decision review
Baseline: `25542299` plus the verified, uncommitted completion-clock repair.
User requests live requester/retry integration and timing-envelope qualification.
DEC-108 — windowed publication, especially items 3, 5, 12 and 13, keeps source
lifetime immutable and role-owned; DEC-073 — checkpoint currency forbids treating
an overlay as current. Reuse Track request/worker and pending-demand owners;
no new scheduler, buffer, capacity limit, memory rebase, or fallback removal.

Code inspection found a concrete pre-integration defect: `requestPlaybackWindow`
clears/gathers caller scratch before the low-level admission checks BuildInFlight.
The builder borrows that storage across fill grains. A retry can invalidate the
first request's source even though it is refused. Fix this independently first.

## Transition oracle fixed before implementation
R1: provision a 300-bar slot; publish 400 notes in bars 12–14; request the
window at bar 12 with qualified clock; advance one fill grain. Request a disjoint
empty window at bar 30 using the SAME scratch. Expect BuildInFlight; unchanged
scratch pointer, size and every event; original admitted identity/deadline intact;
new interval retained pending; first build eventually publishes original content.
The busy request has not gathered, so its pending work uses the existing window
capacity as a conservative inspection bound, NOT an observed count. An actual
retry must resolve/count its source again before admission.
R2: a completed deadline-held candidate also refuses before touching scratch.
R3: a real ready successor refuses before touching scratch and remains immutable.
Counterexample: removing the pre-gather guard must fail R1's scratch assertion.

## Verification
Production request -> low-level admission -> real worker/publication through the
existing ownership native harness, using its ClockManager seam. Bounded drain:
4096 grains. Focused `pio test -e native -f test_runtime_working_set_ownership`;
full `pio test -e native`; `pio run -e teensy41-capture-serial`; diff whitespace
and strict OpenSpec checks. Device qualification is NOT supplied by API fixtures.

## Live activation findings / remaining work
- Current snapshot gather allocates an intermediate vector, sorts synchronously,
  and can enter overlay reconstruction. Moving that call into a turn is not a
  fixed grain. Snapshot event membership has paged links; a bounded cursor must
  charge inspected references AND page/bar advances.
- No role-owned stable committed-source adapter exists. Capture staging and
  canonical companion storage already have owners. A deferred snapshot lease
  needs explicit bounded retirement; a raw pointer or extra whole-loop copy is
  not a valid substitute for the approved retained-source contract.
- Playback-only provisioning does not guarantee prewarmed rolling views.
- Pending-only demands are invisible to dispatcher eligibility. Re-admission
  must preserve the original obligation/deadline, not compute a fresh lap.
- Dispatcher global tick is not slot phase: projection origin, jam mapping and
  queued-launch frame require a coherent clock/phase sample.
- Shared active builders include record/canonical work, not exclusively committed
  windows; new requester/worker must respect their ownership.
- Centered windows' next-entry deadline is not necessarily coverage exhaustion.
  Boundary consumption, overlap/wrap cursor and closure settlement remain unwired.
- The declared 64-tick gap is not a guaranteed device envelope. Earlier measured
  dispatch gaps 112/168/149 exceed it; no limit is increased to hide that evidence.

These findings preclude a truthful live timing qualification from the existing
baseline stimulus. Keep activation/fallback-removal gates open and do not upload
an unchanged-path baseline as a committed-window timing proof.

## Completion
Retry-safety prerequisite verified; live integration and timing qualification remain OPEN.

- Baseline red proof: R1 failed `Expected 388 Was 0` on the unmodified requester.
- Focused: 103/103 passed. R1 also reads every published event and requires each
  original event exactly once. R2/R3 preserve completion-held and ready roles.
- Mutation: disabling the pre-gather guard fails both new fixtures with
  `Expected 388 Was 0`; production TU restored byte-identically (SHA-256 in JSON).
- Full native: 2,746 cases, 2,739 passed, 7 skipped, zero failures (196.208 s).
- Capture build: SUCCESS (45.958 s), RAM1 variables 96,192; code 390,536;
  padding 2,680; locals 34,880. Floor 840 unchanged; runtime size pins unchanged.
- Strict OpenSpec validation and `git diff --check`: PASS.
- Initial fixture compile error (wrong event/accessor names) was corrected before
  red proof. Initial guard changed rootless refusal precedence; existing test
  caught `Expected 5 Was 1`; corrected without weakening that test.
- No upload/HITL: the changed API still has no live committed-window caller;
  running the baseline would not exercise this repair or qualify that envelope.
- No commit, no fallback removal, no capacity reduction, no timing-limit rebase.

Verification logs are in session scratch: `requester-red.log`,
`requester-focused.log`, `requester-mutation.log`, `requester-native.log`,
`requester-build.log`. Durable results and hashes: `verification.json`.

## Next implementation boundary
The selected snapshot's bounded inspected-work cursor and window-only pins are now
verified in [SOURCE_CURSOR.md](SOURCE_CURSOR.md). Bind that cursor to an existing
generation-qualified candidate role,
including bounded cancellation/release. Do not acquire an unbudgeted whole-loop
root lease or borrow another owner's capture staging. Then propagate that stable
source through the existing RuntimePlaybackViewSource overload, cold provisioning,
and the single maintenance lane's retained-retry service. Only after these source
and ownership prerequisites may a production requester be enabled for qualification.
The current slice does not implement those continuations and is not task closure.
