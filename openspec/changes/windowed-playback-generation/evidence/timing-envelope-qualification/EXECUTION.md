# Task 3.10 — timing-envelope qualification

## Identity and scope
Baseline d08932c5 plus retained uncommitted absolute-deadline correction.
Authority: DEC-108 — windowed playback, §13; DEC-043 — execution bounds;
Architecture/Playback.md and ExecutionBounds.md; task 3.10 — bounded service.
Historical search: deadline, clock epoch, service gap, DEC-108, DEC-043.
Reuse ClockManager and main's existing PlaybackViewMaintenance dispatch. No new
scheduler, consumer activation, capacity reduction, SD migration or RAM1 rebase.
Device permission: upload capture firmware; overwrite track 8 slots 4/5 only.

## Oracle fixed before edits
- Clock advance crosses 32-bit wrap without epoch change; any explicit assignment
  (including same-value, forward seek, reset followed by catching up) changes epoch.
  Exhausted identity fails closed; atomic reads restore the caller's interrupt mask.
- Sample every dispatch, including no eligible holder; idle intervals cannot be
  called pending-work starvation. Record all-dispatch and eligible-predecessor maxima
  separately, epoch discontinuities, eligibility and actual worker outcomes.
- A reset hidden between samples is excluded by epoch even when tick increases.
  Real delayed service while work is eligible remains in the measured maximum.
- Preserve existing worker behavior and all non-scratch slot content.

## Controls and qualification criteria
Use pure clock/sampler helpers in native fixtures; production wiring in ClockManager
and main; no copied scheduler. Measurement is phase ticks per lane opportunity,
not worker duration, and the other deferred owners are already included in it.
Hmax=8 tracks; same-track interference is bounded work, not a slot multiplier.
Qualifying admission needs derived request work, protected sibling deadlines,
retained pending obligations, a permitted service-gap bound and overrun policy.
Observed maxima alone are not guarantees. Existing device runs do not prove
committed-window work: shared record/canonical builders use the same worker.
The ordinary seven-owner rotation may insert extra Snapshot turns (up to thirteen
iterations with session-exit drain), and iteration spacing is not a time bound.

## Verification recipe
`pio test -e native -f test_clock_transport_master`
`pio test -e native -f test_runtime_working_set_ownership`
`pio test -e native`; `pio run -e teensy41-capture-serial`; `git diff --check`.
HITL: registered base stimulus via host_midi_hitl.py, track 8 slots 4/5;
internal and external-clock controls, image identity and loss-accounted rows.
An admitted committed-window contention leg is required before claiming its envelope.

## Completion
Measurement/admission slice verified; timing envelope NOT QUALIFIED. See results below.

## Gated admission correction oracle (before edit)
Enforced admission must charge candidate-derived grains and Hmax=8, not the legacy
7-grain/4-turn declaration. Fixture: 64 source events cost at most 20 grains;
100-bar lead is insufficient at provisional Delta=768 and H=8; 200-bar lead fits.
Disabling either derivation or H weighting must make the refusal fixture fail.
This is gated-path correctness, not permission to enable the provisional Delta.

Fixture correction: two-bar half-open gather returns 63 (not 64) events; both cost 20 grains. Initial assertion failed on this exact inventory; admission thresholds unchanged.

## Implementation and verification results
- ClockManager owns a 64-bit tick extension. All real tick writes use advance or
  rebase; explicit seek/reset/silent assignment (even same-value or forward), clock
  source transition and PPQN change invalidate identity. Atomic reads/writes restore
  PRIMASK. Stop alone pauses tick; it does not forgive a deadline. Tempo/projection
  revalidation at a future request/handoff remains part of that caller's obligation;
  this does NOT claim that caller exists.
- Sampler moved from conditional worker calls to every main-loop lane dispatch.
  Capture-only fixed DMAMEM counters; first row, every 256 dispatches, and worker
  terminal rows. Aggregation is outside the timed selection/worker span. No capacity,
  quanta, scheduler, RAM1 budget or live activation changed.
- Review found missing explicit initialization of NOLOAD sampler state. Fixed with
  resetPlaybackLaneMeasurement at setup and a reset regression fixture. Clock state
  is explicitly initialized by ClockManager's constructor. One review pass only.
- Enforced admission now uses candidate-derived grains and Hmax=8 before candidate
  and sibling feasibility checks. It still uses the provisional gap declaration,
  so sInterferenceBudgetEnforced stays false in production. No live capability
  reduction is hidden behind this correction.
- Focused ownership: 84/84. Earlier combined clock/ownership 90/90 (before adding
  the candidate-cost case). Full final native: 2,727 total / 2,720 passed / 7 skipped /
  0 failed (`pio test -e native`, timing-native-last.log). An earlier 240-second
  shell timeout interrupted a run without an assertion failure; complete reruns pass.
- Four mutation checks failed at the intended assertion and restored byte-identically:
  reset epoch removed (Expected Not-Equal); eligible gate removed (Expected 0 Was
  8000); candidate derivation removed and H reduced to four (both Expected 8 Was 0,
  candidate must charge derived work across all eight tracks).
- Build: `pio run -e teensy41-capture-serial` SUCCESS. RAM1 variables 96,192,
  code 390,408 + padding 2,808, locals 34,880; padding floor 840 unchanged.
- Final uploaded image: `7c2a137717da5b7100625241626f62d005e7262cc38a6f60b7e670d339501e95`.
  Base commit d08932c5d77523c880f38d18c8341aab3b14018f, dirty tree (metadata records it).
  Prior post-initialization image 387a910a... passed both controls; the final image
  includes the gated admission correction and is NOT silently substituted for it.

## Device evidence (latest image)
Entry point: `host_midi_hitl.py run --preset base --track 8 --loop-slot 4
--midi-channel 8 --out-dir <capture>`. First run adds `--build-upload`; external
control adds `--clock-source host --tempo-bpm 300 --boot-reset`; internal retry adds
`--boot-reset`. Canonical STIM-BASE-2-2-01@v1 reused unchanged. Only authorized
track 8 slots 4/5 were targets. Baseline's normal stopped/drain shutdown completed.

| Capture | Baseline verdict | Rows | Max dispatch gap (ticks) | Max eligible-predecessor gap (ticks) | Max selected worker (us) | Eligible/progress/denied |
|---|---|---:|---:|---:|---:|---|
| timing-envelope-qualified-clock-internal | INCONCLUSIVE_CAPTURE | 92 | 112 | 16 | 320 | 254/254/0 |
| timing-envelope-qualified-clock-external300 | PASS | 96 | 168 | 32 | 249 | 239/239/0 |
| timing-envelope-qualified-clock-internal-retry | INCONCLUSIVE_CAPTURE | 95 | 149 | 20 | 288 | 162/162/0 |

All three latest-image runs report COMPLETE capture, no Tier-A eviction, no ring
overflow, critical rejection or wipe, and zero saturated gaps. Diagnostic-only
shed is recorded in measurements.json, not hidden. Two internal-clock attempts
are INCONCLUSIVE_CAPTURE / EVIDENCE_MISSING with the same
`persistence_active_snapshot_unavailable_during_save` issue; neither is a pass.
The 300 BPM baseline is PASS, with advisory first-note timing warnings retained in
its report. Hardware was run alongside native tests on the host for the first two
latest-image captures; the internal retry ran alone. These observations are not
isolated WCET measurements and must not be called such.

The old thousands-of-ticks `win_gap` observations could span no-eligible-work time.
Their attribution to pending-work starvation is withdrawn, not replaced by a claim
that starvation is impossible. Current rows include idle dispatches. An eligible
predecessor is deliberately NOT a continuous pending-request identity: other
owners also use the builder. 9 denials appeared on the earlier lane-external300
capture; the latest controls have their own counts and do not erase that finding.

## Row schema and limits
`DIAG,win_gap,maxDeltaTicks,opportunities,phase,running,discontinuities,
eligiblePredecessorMax,eligibleTurns,progressTurns,deniedTurns,maxWorkerUs,epoch,
saturatedGaps,2`.
Maxima/counts are per reporting block; opportunities are cumulative. The running
flag and epoch describe the last sample; a block may span states/epochs. Deltas
are accumulated only between running samples in the SAME qualified epoch, with
no backward tick. Do not filter entire blocks by the final running flag. A late
window that begins between dispatches still needs its own admission-to-first-service
measurement. No unreported final partial block is included in these summaries.

## Qualification verdict and remaining blockers
**NOT QUALIFIED. Task 3.10 stays open; no supported deadline envelope or normal
continuity guarantee is claimed.** The verified conditional work bound is
`lane opportunities <= 8 * (candidate grains + bounded sibling work)` for a closed
backlog; the seven-owner factor must not be multiplied again. Replenishing work
requires the protected admission checks, not a fixed sibling-slot multiplier.

1. No production caller requests committed/preview windows. Baseline eligible work
   comes from shared record/canonical builders, so these runs do not exercise the
   admitted immutable-window request -> completion -> handoff path or all eight
   admitted holders. A dedicated retained-source window profile is still required.
2. No retained pending-request/retry path with original deadline, source identity
   and projection revalidation exists. The new clock accessor alone does not supply
   that path. Refusal remains a named result, not a retained obligation.
3. No permitted service-gap limit and operational overrun policy is wired into the
   live admission/publication path. Empirical maxima cannot be promoted to guarantees;
   containment after a miss is not passing normal-operation continuity.
4. Latest-image internal-clock baseline twice lacks the required persistence evidence.
   That must be resolved or supplied before a complete baseline acceptance claim.

Closing only measurement and gated-admission correctness is not closing the user's
requested timing qualification. No task checkbox or capacity promise was changed.
