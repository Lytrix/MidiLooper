# G2 execution packet — slice 1: owner-admission contract

Per [`IMPLEMENTATION_READINESS.md`](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
and [`EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md). Tasks 1.3/1.4/1.5.

> **Atomicity (design D3).** This slice does not ship alone. `rebuildOverdubSourceView` is load-bearing
> for overlap correctness, so the interdiction of whole-loop work and the bounded windowed replacement
> (slice 2) land in the same commit and the same firmware image. Do not build a flashable image from
> this slice alone.

## Identity and scope

- **Packet ID:** `RIAO-1` (realtime idle-owner admission, slice 1).
- **Approved references:** `proposal.md`; `specs/deferred-idle-owner-admission/spec.md` (all six
  requirements, including the budget/threshold separation and the reserve-before-dispatch rule);
  `specs/overdub-pass-overlap-resolution/spec.md`; `design.md` D1, D2, D4, D6, D7;
  `evidence/DECISION_REVIEW.md`.
- **Governing decisions:** DEC-043 (bound class is gated; local budgets do not replace it; no
  `admit(WorkClass)`), DEC-191 (an expensive child on the owner turn turns the instrument into a
  low-rate loop — the precedent), DEC-181 (windowed resolve primitive), the C3.1/C3.2/C3.5 source-view
  contract.
- **Status:** specified.
- **Owner and entry path:** `main.cpp::loop()` → `runDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredOwner)`
  (FLASHMEM, `noinline`) → `Track::processDeferredIdleMaintenance(...)`. No new owner, no new queue,
  no new scheduler. The rotation (`sDeferredIdleOwnerCursor`) and the one-grain-per-turn discipline are
  unchanged.
- **Primary files:** `include/Utils/MidiServiceDrain.h` (predicate + registry, inline like its
  neighbours), `src/main.cpp` (`runDeferredIdleMaintenance` dispatch + the `idle_maint`/`load_frame`
  spans), `src/Track/TrackDeferredMaintenance.cpp` (child eligibility), `include/Utils/CaptureLineTier.h`
  (deferral literal), `docs/Authority/Architecture/ExecutionBounds.md` (rows 19/12/5/8/9/10 and the
  row-14 note).
- **Existing harness:** `test/test_midi_service_drain` (owner-policy predicates),
  `test/test_live_capture_display_boundary` (idle-lane display behaviour),
  `test/test_continuous_capture` (stop/interaction dispatch), `test/test_runtime_working_set_ownership`
  (deferred stop grains), `test/test_capture_line_tier` + `test/test_capture_ring` (tier delivery).
- **Preconditions already present:** `idle_maint` span + `loop_rem` one-shot; owner rotation; Tier-A
  classifier; `hasRealtimeIdleWork` per-track gate from the 2026-09-15 fix.
- **Prerequisites in this slice:** the child registry, the reservation rule, the deferral row, the
  atomic windowed replacement (slice 2, same commit).
- **External blockers:** none. RAM1 has ~840 B floor headroom; any new static goes to `DMAMEM`.
- **Explicit non-goals:** no `admit(WorkClass)`; no interval reservation; no extra `handleMidiInput()`
  call sites; no change to the playback window builder's grain, the round-robin cursor, or DEC-045's
  overdub visual-cache gate; no persistence restructuring; no change to the stop path.
- **Commit boundary:** one firmware commit containing slices 1+2, independently buildable, with the
  deferral row registered in the same commit (a new literal without the classifier entry is shed
  exactly where it is needed).

## The registry (what a "child" is)

A **child** is one named, dispatchable unit of work on the deferred idle-owner turn. The registry is a
compile-time table of `{ id, kind, declaredMaxUnits, declaredMaxUs, loadBearing }` where `kind` is one
of:

- `Bounded` — has a declared maximum grain and can resume at a finer grain. Admitted only when its
  declared maximum fits the remaining owner work budget.
- `Unbounded` — no declared bound. **Never** dispatched on a timing-critical turn; returns a named
  deferral.
- `CarveOut(reason)` — structurally cannot be reached on a timing-critical turn (e.g. a branch gated
  on `allTracksStopped`). Declared so the enumeration is total; a test asserts the branch still cannot
  be reached.

Slice 1 registers every call site reachable from `runDeferredIdleMaintenance`. The dispatch is
**closed over the registry**: a call site that is not in the table fails the enumeration test, so
"forgot to declare a bound" is a red suite, not a silent overrun.

Children this slice changes (the measured offenders):

| child | before | after |
|---|---|---|
| overdub source-view refresh (whole-loop seed + `NoteId` merge) | runs whole-loop on the turn | **replaced** by the bounded windowed derive (slice 2); admitted through it |
| `prewarmOverdubSourceSpanCache` | runs on the idle lane | `Unbounded` → deferred while timing-critical |
| whole-loop `resolveDisplayNotes(passes, …)` reached from the idle lane (rows 5/8/9/10 helpers) | runs whole-loop | `Unbounded` → deferred |
| `deferredStoredMidiVerification` effective-notes phase | runs whole-loop | `Unbounded` → deferred |
| `SEVT` dump | runs whole-loop | `Unbounded` → deferred |

Children this slice **declares but does not change** (they already carry a quantum): deferred stop
grains (DEC-178 burst cap, one grain/turn), canonical prefix (row 30, Q=32), snapshot units (row 21/22,
one fragment/step), visual-cache bar slice (row 4b, 1–4 bars), playback window (row 27, one declared
grain). These become `Bounded` with their existing quantum as the declared maximum — declaring a bound
the code already enforces, not inventing one.

## Oracle fixed before behavior edits

### Case `RIAO-1.A` — an unbounded child is not dispatched (RED first)

- **State:** one track `OVERDUBBING`, transport running, an unbounded child owed on the turn.
- **Steps:** admit one turn with a fresh owner work budget.
- **Expected after each step:** the child returns `Defer` with a name; zero work units consumed; the
  turn's admitted-work total is 0.
- **Failure counterexample:** the child runs (work units > 0) and the deferral row is absent.
- **Mutation that must red it:** remove the `timingCritical` term from the predicate.

### Case `RIAO-1.B` — reservation rejects a child that cannot fit

- **State:** remaining owner work budget `B`, child declared maximum `M > B`, child is **not** finer
  resumable.
- **Expected:** `Defer`, zero work.
- **Counterexample:** the child is dispatched with the smaller remaining deadline and overshoots `B`.

### Case `RIAO-1.C` — a finer-grained child takes the remaining deadline

- **State:** `B` between the child's fine grain and its declared maximum.
- **Expected:** `Run` with `deadline = B`; the child stops at or before `B` as measured in **work
  units**, and its cursor advances monotonically.
- **Counterexample:** the child runs its full maximum anyway.

### Case `RIAO-1.D` — the aggregate turn is bounded

- **State:** three owed children, declared maxima 8/10/12 ms, budget 25 ms.
- **Expected:** at most two are dispatched (8+10 = 18 ≤ 25; adding 12 would be 30 > 25); the third
  defers by name. Admitted total ≤ budget for every permutation of arrival order.
- **Counterexample:** all three run.

### Case `RIAO-1.E` — deferral is lossless and observable

- **State:** a child with a partially advanced cursor is deferred, then timing-critical ends.
- **Expected:** the next non-timing-critical turn resumes from the same cursor and produces the same
  final output as an uninterrupted run; the deferral row was emitted once for the interval with the
  child name and reason.
- **Counterexample:** the cursor resets, or the deferral is reported as served.

### Case `RIAO-1.F` — coverage currency (shared with slice 2; asserted here at the boundary)

- **State:** established view covering interval `X`; refresh owed for interval `Y`.
- **Expected:** `Y ⊆ coverage` → deferral permitted and recorded as coverage-held; `Y ⊄ coverage` → the
  refresh is **not** deferrable, and the bounded path must serve it (or fail loudly). Never "serve the
  old window as current".
- **Counterexample:** `Y ⊄ coverage` and the old window is returned.

### Case `RIAO-1.G` — the registry is closed

- **State:** every call site reachable from `runDeferredIdleMaintenance`.
- **Expected:** each maps to exactly one registry entry; an artificially added unregistered call site
  fails the enumeration test.
- **Counterexample:** an unregistered child dispatches.

## Controls and coverage

- **Seams reused:** the existing owner-policy header (`MidiServiceDrain`) for the predicate; the
  existing `Track::processDeferredIdleMaintenance` entry for the child side; the existing
  `recordLoopRemainderSpan` / `loop_rem` path for the device claim; `CaptureLineTier` +
  `test_capture_ring` for delivery.
- **Production paths exercised:** the header predicate is called by production `main.cpp` and by the
  fixture (same function, no test-only branch). `Track::processDeferredIdleMaintenance` is already
  driven directly by several suites. **`runDeferredIdleMaintenance` itself is not linkable natively**
  (it lives in `main.cpp`); the dispatch *decision* is therefore extracted into the header so the
  fixture exercises the same code, and the wiring is proven by build + device. This is stated, not
  papered over.
- **Numeric limits:** declared maxima are work units in native (deterministic) and microseconds in the
  device declaration. The device claim for each child is its measured maximum from
  `session_20261029_190227.log` plus the child's own quantum — recorded, not invented.
- **Busy/stale/failure cases:** deferral while a stop job is pending; deferral while the selected track
  is the overdubbing one; resume after transport stops; a child denied by memory admission (must defer,
  not partially adopt).
- **Bounded drain:** every fixture loop is bounded (≤ 64 turns) and prints the last child state on
  failure.
- **Untestable-before-link:** the exact `main.cpp` dispatch ordering. Covered by 5.3/5.5 on device.

## Verification recipe

- Focused: `pio test -e native -f test_midi_service_drain -f test_capture_line_tier -f test_capture_ring`.
- New suite: `test_realtime_idle_owner_admission` (cases A–G), run alone and inside the full suite.
- Broad: `pio test -e native` — expected zero failures beyond the 7 pre-existing skips; counts recorded.
- Build: `pio build -e teensy41-capture-serial` — expected SUCCESS with RAM1 `variables 96,192` and
  ITCM unchanged from `391,992 + 1,224`; any static added must be `DMAMEM`.
- Device: one oracle run (packet `RIAO-2`, tasks 5.2–5.5) — **not** from this slice alone.
- Evidence path: `openspec/changes/realtime-idle-owner-admission-bound/evidence/`.

## KOwnerWorkBudgetUs — declared tunable

- **Initial value: 25 ms** (half the 50 ms alarm).
- **Derived from:** the measured residual = `idle_maint` span − admitted child work. The first device
  run records that residual per turn (task 5.4); the value moves only if the residual justifies it.
- **Not a bound class.** Per DEC-043 point 4 this is a local owner budget: it decides dispatch, it
  does not reclassify a child and does not make an indivisible step bounded.
- **Relationship to the alarm:** 50 ms stays `kLoopRemainderOneShotUs` / `kDeviceGateSliceBudgetUs` and
  remains the acceptance criterion. Admission is expected to make the alarm unreachable in the healthy
  case; the alarm is not the mechanism.

## Completion record

- **Oracle/contract unchanged, or authorized change and supporting evidence:** contract added exactly as
  specified (D1/D2); no threshold, budget or bound-class of an existing child changed.
- **Fixture implemented and discriminating assertion demonstrated:** `test_realtime_idle_owner_admission`
  10 cases, **10/10 PASS** — cases A (unbounded defers + control), B (reservation), C (finer grain), D
  (aggregate turn, all permutations), E (lossless deferral + registry totality), F (coverage rule).
  Task **2.1 complete**. Task **2.2** (production-path turn-cost fixture) **NOT RUN** — the fixture
  above is the dispatch contract, not the turn-cost oracle.
- **Native verification:** `test_realtime_idle_owner_admission` 10/10; `test_midi_service_drain` 42/42
  (no regression from the header addition).
- **Build/RAM1 verification:** `pio run -e teensy41-capture-serial` **SUCCESS**, RAM1
  `variables 96,192`, `code 391,992`, `padding 1,224` — **byte-for-byte unchanged** from the baseline,
  i.e. the `constexpr` registry and predicates cost no RAM1.
- **Device verification:** NOT RUN (blocked: slices 1+2 must land together).
- **Required reviews and durable docs:** `evidence/DECISION_REVIEW.md` done; `ExecutionBounds.md`
  update NOT DONE (deliberately held until the enforcing commit).
- **Task IDs eligible for checkoff:** 2.1. All others open.

## Envelope confirmation (task 1.5)

Recorded from the code, not assumed:

- `Config::NUM_TRACKS = 8`, `Config::MAX_LOOPS_PER_TRACK = 8` (`include/Globals.h`).
- `Config::TICKS_PER_BAR = 768` (`INTERNAL_PPQN * QUARTERS_PER_BAR`).
- Detailed display window: `DisplayWindowUtils::kMaxDetailedWindowBars = 16` bars; the window count for a
  loop is `ceil(loopLengthTicks / (16 * 768))`.
- Touched-coverage tiles: `CoveragePrerequisite::kTileBars = 4`, `kMaxTiles = 64` -> **64 tiles = 256
  bars** of loop. `tileCountFitsEnvelope()` is the named check; above the envelope the caller is required
  to take the whole-loop fallback (design D3). The longest loops exercised by the current suites are 68
  bars (visual cache), 64 bars (JIT-ahead), 40 bars (density/convergence) and 31 bars (derive profile),
  all inside the tile envelope.
- Reservation rule: `min(declaredChildMax, remainingOwnerWorkBudgetUs)`. Registry maxima are 2,000-4,000
  µs against `kOwnerWorkBudgetUs = 4,000`; the source-view child declares `fineGrainUs = 1,000`, so it may
  take the remaining deadline at a finer grain. Unbounded children (maxUs 0) are never dispatched while
  timing-critical.
- The owner turn's own overhead is the residual (`kOwnerWorkResidualUs = 4,000` at the 300 BPM clock
  interval of 8,333 µs); child budget + residual must stay below one clock interval.
