# Task 3.10 — the rolling chain's admission boundary on the playing path

Execution packet, written **before** the fixtures. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md). Slice type: **verification on the
playing production chain** (tests may enable the interference budget; no production line changes, no gate flip, no
constant moved).

Owner's steer for this step: *"Take option 1, but narrow it to the actual rolling path -- not another general arithmetic
sweep. Don't enable normal operation yet."* and *"Stop there: record the resulting capability statement and the next
required contract decision. Don't launch another optimization series."*

## Identity and scope

- **Why this slice:** the `H x K` contention sweep proves *bounded completion for a request that is given sufficient lead*
  -- and it deliberately drives the service composition with the transport stopped and the rolling selector out of the
  picture. Meeting one long-lead promise is a different claim from the **playing** chain repeating it, and from what
  happens to a shape the admission must refuse **while playback is audible**. This slice closes that gap and nothing
  more.
- **Chain under test (all production symbols, transport playing):** `Track::serviceRollingPlaybackWindow` (selector,
  stamped) → the bounded worker's grains through `Track::stepPlaybackViewMaintenance` → `Track::resolveRollingPlaybackWindow`
  (boundary validation + adoption) → the existing emitter/ledger, with `src/main.cpp`'s lane turn (`rollingTestTick`'s
  shape: service, step, `TrackManager::updateAllTracks`) as the driver.
- **Enforcement:** `sInterferenceBudgetEnforced` is turned on **inside the fixtures only** and restored, so the enforced
  path is exercised where the acceptance asks for it. Production keeps it off; `PLAYBACK_WINDOW_ROLLING_ENABLED` keeps its
  default.
- **Acceptance (owner's list, verbatim in intent):**
  1. one admissible shape completes and emits **exactly**, across **repeated handoffs**;
  2. a **just-outside** shape gets the **named refusal**, keeps its **original obligation**, and playback **continues
     through fallback**;
  3. the budget charges **actual qualification, preparation and retirement work**, not only an already-admitted builder;
  4. capacity **2,048**, span **≤ 2 bars** and every timing constant stay unchanged, and the statement is about
     admissible **workload/lead** -- not a reduced "largest supported window";
  5. **intermittent immutable coverage** is distinguished from **continuous service**.
- **Primary files:** `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp` (fixtures +
  registrations) and this directory. No production line.
- **Non-goals:** enabling normal operation; a new sweep/optimization series; device work; changing any constant, capacity,
  gate or span; extending the packet's claim to contiguous service.
- **Commit boundary:** test-only, independently buildable.

## Oracle fixed before behavior edits

| Case | Initial state | Ordered actions | Expected observation |
|---|---|---|---|
| **R1** admissible shape, repeated handoffs, enforcement on | the S4b.7 demoted 24-bar target playing with its launch bound (the rolling slice's own setup); `sInterferenceBudgetEnforced = true` | stamped `serviceRollingPlaybackWindow`, then drive the lane tick by tick past **three** interval boundaries (`rollingTestTick`: service → step → `updateAllTracks`) | the selector answers `Requested` once per episode (**≥ 3** handoffs), each adapter is validated and adopted, the emitted trace is **exact** (`2 x bars + 1` events for `bars + 48` ticks, channel 8), no promise is crossed (`one_pass_late == 0`), no `TrackInterferenceBudgetExceeded` demand is retained, and the intermediate fallback intervals are audible |
| **R2** just-outside shape, enforcement on | the same playing target, but the interval the selector targets carries a **dense** root the derived cost cannot prepare inside the lead the selector granted | stamped `serviceRollingPlaybackWindow`, then drive the lane for a few turns, then keep driving past the boundary | the selector's demand is **accepted as a request** and the worker's admission refuses it **by name** (`CannotCompleteBeforeHandoff`), the demand keeps its **original deadline** and its covered interval, the frame goes `Blocked`, `resolveRollingPlaybackWindow` answers `Rejected`, **no adoption happens** (the served view does not change, no rolling `build_complete`), and the emitter keeps producing the loop's own events through the fallback |
| **R3** the charge is real work, not only the admitted builder | one slot on the playing track owning its **coverage source** (qualification/retirement), the flag **off** | read the per-track budget `playbackViewOutstandingGrains(<other slot>)`; ask the production admission (`canCompleteWithinLead`) for a candidate with a short and with a long lead, with and without that charge | the budget is **non-zero** while the slot owns its source and equals the derived snapshot bound (not an admitted-work record); the short lead is refused **because of** the charge and the long lead admitted; so the charge covers qualification/preparation/retirement, not only an admitted builder |
| **R2-control** the same drive without any rolling request | the same playing target and the same dense root, flag off | drive the same ticks | the emitted trace equals R2's driven range: the refusal changed nothing audible (fallback continuity) |

- **Counterexamples that must fail these fixtures:** making the selector's demand bypass the worker's charge pricing;
  publishing/adopting a candidate whose promise cannot be kept; dropping the demand instead of retaining it on refusal;
  charging zero for a slot that owns its coverage source.
- **Preserved invariants:** capacity, span, quanta, gap, `H` and margin unchanged; nothing is published by a refused
  demand; the enforcement flag is restored by every fixture; the fallback remains the served source while a demand is
  refused.

## Controls and coverage

- **Reused seams:** `assert_s4b_7_a_demoted_target_through_the_turn` (the playing demoted target with a bound launch),
  `rollingTestTick`, `MidiSendCapture`, `PlaybackPathRows`, `publishNotesInBarsRootForSlot` (the dense root),
  `TrackCaptureStopTestAccess::runtimeIfAllocated`, and the `sInterferenceBudgetEnforced` flag.
- **Production paths exercised:** selector → worker → boundary resolution → emitter, plus the mutable fallback and the
  per-track budget. No stub, no copied state machine, no fabricated publication.
- **Numeric limits:** every limit read from the declaration or from production records; the only literals are measured
  trace counts and the fixtures' own tick values.
- **Untestable-before-link:** `src/main.cpp` itself is not native-linkable (the fixtures drive the same calls the lane
  makes, which is the suite's established limit).

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership`; broad: `pio test -e native`.
- Build: `pio run -e teensy41-capture-serial` (RAM1 line must be unchanged); `git diff --check`;
  `openspec validate windowed-playback-generation --strict`.
- Device: **NOT RUN** -- 6.4/6.5 own that, and this slice does not enable normal operation.
- Evidence path: `evidence/2026-10-05-s310z-rolling-admission-boundary/`.

## Completion record

- [x] Fixtures implemented; oracle above honoured (three fixtures in the ownership suite)
- [x] Mutations red-prove: **MR2** fails R3 as intended and restores byte-identically; MR1/MR1p/MR3/MR5 are recorded with
      the reason each did not produce a clean red-proof (see `EVIDENCE.md`) rather than claimed as passes
- [x] Native verification: focused **140/140**; full **2794 / 2787 succeeded / 7 skipped / 0 failed**
- [x] Build/RAM1 verification: `teensy41-capture-serial` **SUCCESS**, `variables 96,192` / `padding 1,640` (no re-base)
- [x] Device verification: NOT RUN (not claimed)
- [x] **Capability statement recorded** (workload/lead terms; explicitly intermittent, not continuous)
- [x] **Next required contract decision named** (the frozen/pending refusal asymmetry; activation; the F-1 defect)
- [x] Task checkoff: none on its own
