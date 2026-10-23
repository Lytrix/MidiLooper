# Deadline clock identity — execution packet

Baseline: `d08932c5`, clean tree. Task 3.10 — bounded deadline-ordered service;
DEC-108 — windowed publication, deadline and clock revalidation. User authorized
fixing the full-lap reopening defect and regressions. Existing Track owner only.

## Scope / oracle (fixed before production edits)
Retain absolute time and a clock epoch in each admitted work record. A phase is
not a traversal identity. The request APIs accept a caller-supplied monotonic
64-bit tick and nonzero epoch; an unspecified stamp is unqualified, not epoch 1.
Epoch must change on reset/rebase; tick wrap must be extended or change epoch.
No production request caller exists yet: clock-owner wiring remains owed, and
interference enforcement stays off. No clock manager, capacity or scheduling
policy change, pending-request store, upload, or deadline-envelope qualification.

Tests: a live build's deadline stays expired after an entire unobserved lap and
multiple laps; same-epoch forward time before expiry remains feasible; reset to
an equal phase with a different epoch, backward time, missing identity, changed
loop geometry and timestamp addition overflow fail closed. Actual admission with
enforcement enabled refuses a stale sibling without creating candidate work.
Use existing native harness and stable source buffers, no sleeps or device mocks
as timing evidence. Closed deadline cannot be revived by observation frequency.

Checks: focused ownership and playback-prewarm tests; full `pio test -e native`;
`pio run -e teensy41-capture-serial`; diff whitespace; strict OpenSpec validation.
Size pins only change to compiler-observed ABI figures, never RAM1 budget rebase.
Record red/green results below. Device timing is NOT RUN and not claimed.

## Completion record — native verified, production activation gated

Implemented `PlaybackDeadlineClock { uint64_t tick, uint32_t epoch }` as an explicit
input on committed/preview/low-level request APIs and the interference predicate.
Epoch zero means unqualified. Each active work record retains admission tick,
absolute deadline, epoch and its own loop length. The predicate refuses expiry
(including the exact deadline), epoch mismatch, backwards time and changed loop
length. It computes remaining lead by subtraction only after validation; the
candidate's loop length never reinterprets a sibling's deadline. Timestamp addition
overflow cannot qualify a candidate. The shared contract's absolute-lead helper
uses division to avoid overflow of the turns-times-advance product.

The first red test intentionally demonstrated the old API's ambiguity: the same
phase described both admission and a later lap. It failed at "a full unobserved
lap must not reopen the retained deadline", but could not encode elapsed time in
the old signature. The final test supplies explicit absolute time and covers one,
two and 100 unobserved laps. The enforced request regression proves refusal leaves
no candidate work and preserves the old deadline, then proves the same path can
admit feasible work with a valid stamp. Missing identity and timestamp overflow
are also tested with NO sibling to mask those guards. Extended ticks crossing
UINT32_MAX remain valid before expiry. No intermediate expiry observation is needed.

### Actual verification
- `pio test -e native -f test_runtime_working_set_ownership -f test_playback_prewarm`:
  273/273 passed (83 ownership + 190 prewarm); final ownership rerun 83/83.
- Mutation: remove absolute expiry guard -> full-lap assertion fails, plus enforced
  request reports Expected 11 Was 0. Remove epoch comparison -> equal-phase reset
  assertion fails. Each restored byte-identically; owner TU hash at mutation restore:
  `d8db0e2eaf03016cb09a1738add6887007b1f84efdb9b01fd3a9a6e23a486e6c`.
- `pio test -e native`: 2,722 cases, 2,715 succeeded, 7 skipped, zero failed.
- `pio run -e teensy41-capture-serial`: SUCCESS. Initial build caught the old size
  pin; incomplete-template compiler probe measured 6,680 B (previously 6,656).
  Target pin updated to that measurement; host stays 6,544 B. No RAM1 budget rebase.
  Final RAM1: variables 96,192 / code 390,088 / padding 3,128 / locals 34,880,
  identical to the recorded baseline; 2,288 B above the approved padding floor.
- Historical count correction: `d08932c5` has 81 RUN_TEST registrations, not the
  reported 82. This slice adds two: 83. The full-suite count rises 2,720 -> 2,722.
- Firmware build only: no upload, no HITL and no timing/device acceptance claim.
- `openspec validate windowed-playback-generation --strict`: valid.
- `git diff --check`: clean. Landed as `4a7cef70` (increments 22-23, jointly with
  the timing-envelope slice) after this record was written; the same checks were
  rerun green at that tree.

### Remaining gates / non-claims
Task 3.10 — bounded deadline-ordered service remains OPEN. The clock owner must
supply an atomic, extended monotonic tick/epoch pair (including resets, seeks,
rebases and projection changes) when production requests are wired. No such
request caller currently exists. An omitted stamp fails closed under enforcement;
legacy tests may still prepare with enforcement off, without a qualified promise.
The interference switch stays off, and the service-gap bound, H-weighted admission,
remaining-work accounting, pending-request preservation and device qualification
are NOT solved by this deadline fix. No new owner or scheduling framework, no
content-capacity reduction, no accepted-request retention claim. This implements
DEC-108's existing clock-revalidation/deadline requirement; no decision superseded.
