# Runtime profiling layer (Level 1)

## Why

The last timing investigations each ended the same way: a one-off `micros()` instrument, a bespoke
capture row, a rebuild and a reflash. DEC-230 found the Record-edge block (**270.8 ms of a 273.3 ms
admission inside `prewarmViews`**) only after `Track::prewarmPlaybackForSlot` grew seven `micros()`
brackets and the `DIAG,capture_prewarm_split` row. DEC-231 (the off-edge view-prewarm state machine)
then needed a second bespoke row, `DIAG,view_prewarm_step`, and three follow-ups to locate the next
largest unit (the 17,350 µs first capture staging). The diagnostics now cost more development time
than the defects they locate.

A standard profiler makes "which function owned these milliseconds?" a *dump* instead of a *build*.
It is needed now because the remaining preparation units (13–17 ms single external allocations,
DEC-231 follow-up 3) and the display-base work that DEC-238 shipped off both need before/after
numbers that are not another hand-written timer, and every later timing slice inherits the same
instrument.

## What Changes

- **New `RuntimeProfiler` firmware subsystem (Level 1), additive.** Compile-time `ProfileId` enum
  (append-only binary IDs), RAII scopes that time every exit path including early `return false`,
  and `ARM_DWT_CYCCNT` cycle timing instead of `micros()`. Entry is the trigger; scope exit is the
  stop.
- **Two timing primitives, because a cycle counter alone can lie.** A **short** scope uses only DWT
  and is permitted only where an **independent guarantee** (recorded at the site) establishes that
  the operation cannot span one counter period (7.158 s at 600 MHz). A **long** scope also brackets
  the operation with `micros()` and selects the duration source by a fixed rule (cycles only when the
  wall-clock delta is below one period minus a guard band), so a pathological multi-second call is
  reported instead of aliasing. Every long site records the structural reason its operation cannot
  reach one `micros()` period (≈ 71.6 min). A declared ceiling is **not** evidence of interval
  validity; it only decides breach reporting.
- **Aggregate statistics in RAM2, one fixed record per ID** (calls, inclusive and self cycles,
  max, last, over-ceiling, refused, order faults, last result), explicitly cleared at boot because
  `DMAMEM` is NOLOAD. Nested scopes use a fixed depth-bounded stack with a 64-bit normalized child
  accumulator.
- **Thresholded binary trace ring in RAM2.** Only calls above a declared per-scope ceiling enter the
  ring (`seq, timestamp, cycles, micros, id, parentId, track, slot, result, flags`, 24 bytes,
  256 records = 6 KB). The ring carries the **immediate enclosing scope** only.
- **Declared budget classes, report-only.** `CLOCK 50 µs`, `HOT 250 µs`, `FRAME 2 ms`,
  `DEFERRED 5 ms`, `COLD 25 ms`, or an existing named constant (`MidiServiceDrain::kOwnerWorkBudgetUs`).
  These decide ring entry only and bound nothing (DEC-043 point 4, DEC-214).
- **Bounded dump commands and a host decoder.** `!PROFILE_STATS` and `!PROFILE_RING` emit at most one
  batch per request (16 / 32 rows) with a persisted cursor, a ring sequence horizon and a terminator;
  every profile command is refused unless no scope is active. `scripts/profile_report.py` reads a
  capture alone.
- **13 production scopes**, each on a work body after a cheap no-work gate. **One** is short (the
  rolling playback-view grain, ExecutionBounds row 27); **twelve** are long (unbounded composition,
  single external allocations, content-sized windows, a blocking display transfer, a step that may
  publish, or SD I/O).
- **New build environment** `teensy41-capture-serial-profile` (`-D RUNTIME_PROFILE_SCOPES=1`). Every
  profiler symbol, literal and Tier-A prefix is behind that flag (or `PIO_UNIT_TEST_NATIVE`), so the
  default `teensy41-capture-serial` and `-no-grain` images are byte-for-byte unaffected in RAM1/ITCM.

No firmware behavior, storage format, MIDI path, ownership or lifetime changes. Neither existing timing
facility — `HotPathTelemetry` nor `Diagnostics::Timing` (`DIAG,timing`/`timing_max`) — is touched or
migrated; the profiler is their intended successor and retirement is staged separately
([RETIREMENT.md](RETIREMENT.md)). Adversarial review: [RED_TEAM.md](RED_TEAM.md). Not a **BREAKING**
change.

## Prerequisites (must exist before the first firmware edit)

These are created by task group 0 and each is a hard gate for slice 1. None of them changes firmware
behavior.

| # | Prerequisite | Why it is a gate | Where |
|---|---|---|---|
| P1 | `## Now implementing` scope entry naming this change | No existing declaration authorizes it (AGENTS.md before-coding step 2) | `docs/Runtime/CURRENT_WORK.md` |
| P2 | Filled decision review + one new report-only `DEC-###` | Required before firmware edits; records the validity contract and class table | `evidence/DECISION_REVIEW.md`, `docs/DECISION_LOG.md` |
| P3 | Rebased preflight on HEAD `5ddba83e` with recorded default-env RAM1/ITCM figures, native case count and the pre-change default HEX hash | The "default image unchanged" gate and the slice-4 A/B both need a baseline taken **before** any edit | `evidence/PREFLIGHT.md` |
| P4 | `teensy41-capture-serial-profile` env **and** its `scripts/ram1_budget.json` baseline (alias to the default env until it links differently) | `check_ram1_budget.py` fails a build whose env has no approved baseline | `platformio.ini`, `scripts/ram1_budget.json` |
| P5 | Linker carve line `*ProfileDiagRows.cpp.o(.rodata.str* .rodata.*.str*)` beside the `StopDiagRows`/`EnvelopeDistRows` lines | Without it the first name/format literal can snap DTCM by 4,096 B | `linker/imxrt1062_t41_lcr.ld` |
| P6 | Boot-hook location for `RuntimeProfiler::initAtBoot()` that runs before any instrumented owner (boot load and boot prewarm run in `setup`) | `DMAMEM` is NOLOAD; an uncleared table reports garbage and an unarmed source must be known before the first scope | recorded in `evidence/PREFLIGHT.md` |
| P7 | HITL stimulus for the device legs: list the registry; the chain rig (`scripts/hitl_midifile_chain.py`) is **not** a registered stimulus, so register a temporary variant (`STIM-PROFILE-CHAIN-TEMP-01`) or record an accepted reuse, plus a filled `HITL_TEST_RECIPE.md` | AGENTS.md: a serial-observable firmware change needs a registered or reused stimulus; never hand-rolled MIDI | `scripts/hitl/stimuli/`, `evidence/HITL_TEST_RECIPE.md` |
| P8 | A same-day DEC-231 reference capture on the pre-change image (Record edge, `view_prewarm_step` units, `capture_prewarm_split`) | The slice-3 oracle compares against numbers from the **current** tree, not image `1d864202…` | `evidence/<date>-baseline-device.md` |

## Capabilities

### New Capabilities

- `runtime-profiling`: what a profile scope is and is not; the short/long primitives and their
  validity contract; compile-time IDs and append-only numbering; RAM2 aggregate and trace-ring
  contents; boot clearing; declared-ceiling ring entry; bounded profile commands and their
  reset/suspension rules; build gating; measurement-path rules; measured self-overhead for both
  primitives; and the rule that profiling neither bounds nor re-owns the code it measures.

### Modified Capabilities

- None. `playback-runtime-prewarm` and `deferred-idle-owner-admission` keep their requirements: the
  profiler **observes** those owners. `HotPathTelemetry` is unchanged.

## Impact

- **Firmware:** new `include/Utils/RuntimeProfiler.h`, `src/Utils/RuntimeProfiler.cpp`,
  `src/Utils/ProfileDiagRows.cpp` (carved FLASH TU); scope call sites in `TrackCaptureInput.cpp`,
  `TrackPlaybackWindowBuild.cpp`, `TrackPlaybackRuntime.cpp`, `TrackDeferredMaintenance.cpp`,
  `Loop/LoopSnapshotPublish.cpp`, `StorageManager/DeferredSaveScheduler.cpp`, `DisplayManager.cpp`,
  `DisplayManager/DisplayNoteResolveLiveCapture.cpp`, the deferred load step and
  `TrackOverdubLifecycle.cpp`; one init call in `main.cpp` `setup`.
- **Build:** one env in `platformio.ini`, one `ram1_budget.json` entry, one linker carve line.
- **Serial / capture:** a `DIAG,profile_` prefix in `isTierATextLine` (`DebugSessionCapture.cpp`),
  gated `#if defined(PIO_UNIT_TEST_NATIVE) || RUNTIME_PROFILE_SCOPES` like `kIngressCostTierPrefix`.
  The header's `isTierALine` list is not extended.
- **Host tooling:** `scripts/profile_report.py` + `scripts/test_profile_report.py`.
- **Memory:** zero new DTCM by construction — stats (64 × 48 B), ring (256 × 24 B) and depth stack
  (16 × 16 B) in `DMAMEM`, ≈ 9.5 KB. The profile env must link at the approved RAM1 `96,192` with
  ITCM padding at or above the `840` floor.
- **Not affected:** `HotPathTelemetry` and its `PERF,` rows, `Loop`/`Track` sizes, persisted formats,
  the default and no-grain images, existing HITL stimuli, and every bound in
  `docs/Authority/Architecture/ExecutionBounds.md`.

## Known behavior (evidence-backed, current tree)

- **DEC-231 shipped the DEC-230 repair.** `Track::startRecording` only adopts prepared views; when
  not prepared it requests preparation and returns `false`. `Track::stepViewPrewarmForSlot` advances
  one unit (time guard 5,000 µs, secondary to the unit) from the idle owner
  (`TrackDeferredMaintenance.cpp`) and the all-stopped path (`main.cpp`).
- DEC-231 follow-up 3, image `1d864202…`: Record edge **2,353 µs**, `okmask=31, views=0`; largest
  preparation unit **17,350 µs** (first capture staging); views/bridges 13.0–13.4 ms;
  `canonicalPassInputs` 16,358 µs; zero `win_gap_overrun`.
- `prewarmViews` is still the monolithic cold path for boot/load and `Track::startOverdubbing`
  (`TrackOverdubLifecycle.cpp`), and is skipped by `prewarmPlaybackForSlot` when views are prepared.
- DEC-238: the staged `DisplayBaseGrain` producer ships **off**; `buildCommittedDisplayBase` is the
  shipped display-base path.
- ExecutionBounds row 4: `DisplayManager::update` includes the **blocking** `_display.api.display()`
  transfer. Row 21 bounds `LoopSnapshotConstruction::step` and excludes publication, while
  `Loop::stepPendingSnapshotPublish` can publish in the same call.
- The Teensy 4 core enables `DEMCR.TRCENA` and `DWT_CTRL.CYCCNTENA` in `startup.c`, and `micros()`
  itself reads `ARM_DWT_CYCCNT` extended by the systick millisecond count. `micros()` is therefore
  wrap-safe for 71.6 min but not independent of the cycle counter being enabled.
- `DMAMEM` (`.bss.dma`) is NOLOAD; `DisplayManager.cpp` already documents the consequence.
- `ClockPhysicalReferencePlatform::readCycleCounterForObservation()` issues `dsb/isb` and is not a
  per-call duration read; `cpuClockHzForObservation()` is the boot contract.
- `MidiIngressCost` demonstrates a monotonic-counter validity contract (`IngressCounterContract`,
  `ingressCycleIntervalValid`) — the shape the long primitive follows, not its API.

## Assumptions

- Thirteen scopes are enough to prove the layer cheap and useful before it is expanded.
- Commands are dispatched from the main loop outside every instrumented body, so the depth-zero gate
  is normally satisfied; it is a defensive guard, not an expected path.

## UNKNOWN

- Whether any work body is hot enough that the scope's own cost is visible in `midi_gap`; resolved by
  the boot calibration (both primitives) and the paired device A/B.
- How often the long primitive's coarse path is taken on device; expected never in normal operation.

## Open decisions (flagged, not invented)

- **TBD-1:** final scope set (13 proposed). Adding or dropping one is a task-level change; a short
  scope must carry its own structural guarantee.
- **TBD-2:** retirement of duplicate timing logging. This change records same-run equivalence only;
  removals are the staged follow-up in [RETIREMENT.md](RETIREMENT.md). `view_prewarm_step` keeps its
  `ready=1` event (HITL rigs synchronize on it); `capture_prewarm_split` needs child identities for
  every reserve before it can go.
- **TBD-4:** whether the profiler is promoted into `teensy41-capture-serial` after the slice-4 A/B.
  Without promotion no default-image row can be retired (RED_TEAM R2). Owner decision.
- **TBD-3 (decided 2026-11-01):** keep ring capacity 256 and batches 16 / 32 — the aggregate table is the primary
  instrument; the ring is a recent-breach sample whose overwrite count is reported (evidence, slice 4).

## Non-goals

- **Level 2** (`-finstrument-functions`, address symbolization). Separate future change.
- **Consolidating `HotPathTelemetry`** into the profiler's table. Revisit after real use.
- Full call-chain reconstruction.
- Bounding, deferring or repairing any measured function, including the 13–17 ms preparation units.
- Instrumenting ISR-entered functions or whole poll functions that usually do no work.
- Per-call streaming, formatted logging or string building added by the profiler.
- Replacing `loop_rem`, the 5 s telemetry family, `persist_save,step`, `capture_prewarm_split` or
  `view_prewarm_step`.
- A new UI, a persistent file format, or any change to `ExecutionBounds.md` bound classes.

## Scope authorization

No current `CURRENT_WORK.md` declaration authorizes this change. Prerequisite P1 (task 0.1) adds the
entry; it is a human-visible gate, not an agent assumption.

Related: [DELIVERABLE_TRACKING.md](../../../docs/DELIVERABLE_TRACKING.md) ·
[DEC-230 / DEC-231 / DEC-238](../../../docs/DECISION_LOG.md) ·
[ExecutionBounds.md](../../../docs/Authority/Architecture/ExecutionBounds.md) ·
[hitl_authoring_traps.md](../../../docs/Agents/hitl_authoring_traps.md).
