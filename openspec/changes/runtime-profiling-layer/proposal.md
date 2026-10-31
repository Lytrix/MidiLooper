# Runtime profiling layer (Level 1)

## Why

The last three timing investigations each ended the same way: a one-off
`micros()` instrument, a bespoke capture row, a rebuild and a reflash. DEC-230
is the clearest case — the Record-edge block was found to be **270.8 ms of a
273.3 ms admission inside `prewarmViews`** only after `Track::prewarmPlaybackForSlot`
grew seven `micros()` brackets (fourteen calls) and the `DIAG,capture_prewarm_split`
row. The diagnostics now cost more development time than the defects they locate.

A standard profiler makes "which function owned these 300 ms?" a *dump* instead
of a *build*. It is needed now because DEC-230's repair (bounded, off-edge
`prewarmViews` preparation) is the next named slice, and it needs a before/after
that is not another hand-written timer; every later timing slice inherits the
same instrument.

## What Changes

- **New `RuntimeProfiler` firmware subsystem (Level 1), additive.** Compile-time
  `ProfileId` enum (append-only binary IDs), RAII scopes that time every exit path
  including early `return false`, and `ARM_DWT_CYCCNT` cycle timing instead of
  `micros()`. Entry is the trigger; scope exit is the stop.
- **Two timing primitives, because a cycle counter alone can lie.** A **short**
  scope uses only DWT and is permitted only where an **independent guarantee**
  (recorded at the site) establishes that the operation cannot span one counter
  period. A **long** scope brackets the operation with a coarse monotonic clock
  as well, so a pathological multi-second call is reported instead of aliasing
  after `CYCCNT` wraps; every long site similarly records the structural reason its
  operation cannot reach one `micros()` period (≈ 71.6 min), because two 32-bit
  wall-clock samples cannot detect a full wrap either. A scope's declared ceiling
  is **not** evidence of interval validity; it only decides breach reporting.
  When the cycle counter is unavailable, every scope refuses — the wall clock is a
  wrap disambiguator, not an alternate source.
- **Aggregate statistics in RAM2, one fixed record per ID** (calls, inclusive
  and self cycles, max, last, over-ceiling, rejected measurements, last result).
  Nested scopes use a fixed depth-bounded stack whose accumulator is
  64-bit-normalized, so a coarse multi-second child cannot be truncated in the
  parent's accounting.
- **Thresholded binary trace ring in RAM2.** Only calls above a declared
  per-scope ceiling enter the ring (`seq, timestamp, id, parentId, cycles,
  micros, track, slot, result, flags`, 24 bytes, 256 records ≈ 6 KB). `seq`
  makes ordering and overwrites unambiguous, and the second duration field keeps
  a multi-second wall-clock value from being truncated into cycles. The ring
  carries the **immediate enclosing scope** only; no full call chain is claimed.
  No continuous streaming, no formatting and no serial work on the measured path.
- **Declared budget classes, report-only.** `CLOCK 50 µs`, `HOT 250 µs`,
  `FRAME 2 ms`, `DEFERRED 5 ms`, `COLD 25 ms`, or an existing named constant
  (`MidiServiceDrain::kOwnerWorkBudgetUs`). These decide ring entry only; they are
  **not** new execution bounds and bound nothing (DEC-043 point 4, DEC-214).
- **Bounded dump commands at depth zero, and a host decoder.** `!PROFILE_STATS` and
  `!PROFILE_RING` emit at most one batch per request (16 / 32 rows) with a
  persisted cursor, a per-session ring sequence horizon and a terminator, so a dump
  can never become a main-loop stall; every profile command is refused unless no
  scope is active, and `!PROFILE_RESET` mutates profiler state only.
  `scripts/profile_report.py` reads a capture alone.
- **12 production scopes**, each either the work body after a cheap no-work gate
  or a worker helper — never a high-frequency poll function that usually does
  nothing. **Three** are short scopes with a hard structural cap; the other
  **nine** are long scopes (unbounded composition, content-sized windows, or SD I/O).
- **New build environment** `teensy41-capture-serial-profile`
  (`-D RUNTIME_PROFILE_SCOPES=1`). The default `teensy41-capture-serial` and
  `-no-grain` acceptance images compile the layer out zero-cost and are unchanged.

No firmware behavior, storage format, MIDI path, ownership or lifetime changes.
`HotPathTelemetry` is **not** touched or migrated: the profiler is additive and
existing telemetry keeps its own records and build gating. Not a **BREAKING**
change.

## Capabilities

### New Capabilities

- `runtime-profiling`: what a profile scope is and is not; the short/long
  primitives and their validity contract; compile-time IDs and append-only
  numbering; RAM2 aggregate and trace-ring contents; declared-ceiling ring entry;
  bounded profile commands and their reset/suspension rules; build gating;
  measurement-path rules (no serial, no allocation, no ISR entry); measured
  self-overhead; and the rule that profiling neither bounds nor re-owns the code
  it measures.

### Modified Capabilities

- None. `playback-runtime-prewarm` and `deferred-idle-owner-admission` keep
  their current requirements: the profiler **observes** those owners, it does
  not bound, defer, or re-own them. `HotPathTelemetry` is unchanged.

## Impact

- **Firmware:** new `include/Utils/RuntimeProfiler.h`,
  `src/Utils/RuntimeProfiler.cpp` (aggregate/ring/record path, short and long
  primitives), `src/Utils/ProfileDiagRows.cpp` (carved FLASH TU for names and row
  formats, following `src/Utils/StopDiagRows.cpp`); scope call sites in
  `TrackCaptureInput.cpp`, `TrackPlaybackWindowBuild.cpp`,
  `TrackPlaybackRuntime.cpp`, `TrackDeferredMaintenance.cpp`,
  `Loop/LoopSnapshot*.cpp` (`Loop::stepPendingSnapshotPublish`),
  `StorageManager/DeferredSaveScheduler.cpp`, `DisplayManager.cpp`,
  `DisplayManager/DisplayNoteWindowGather.cpp`, the deferred load step, and
  `TrackOverdubLifecycle.cpp`.
- **Build:** `platformio.ini` adds one env; `scripts/ram1_budget.json` records it
  if its linked figures differ from the default env.
- **Serial / capture:** new Tier-A prefixes registered in `isTierATextLine`
  (`DebugSessionCapture.cpp`) — the list the capture ring actually asks. The
  header's `isTierALine` list is deliberately **not** extended (its literals are
  instantiated in every including TU and were measured to snap DTCM by 4,096 B);
  the relationship is pinned in `test_capture_ring` and audited by
  `scripts/check_marker_contract.py --audit`, which parses both lists.
- **Host tooling:** `scripts/profile_report.py` + `scripts/test_profile_report.py`.
- **Memory:** zero new DTCM `.data`/RODATA by construction — stats, ring and depth
  stack in `DMAMEM` (≈ 9.4 KB), names and formats in a carved FLASH TU. RAM1 must
  stay at the approved `96,192` and ITCM padding at or above the `840` floor.
- **Not affected:** `HotPathTelemetry` and its `PERF,` rows, `Loop`/`Track` sizes,
  the `SlotMeta`/loop body formats, the default and no-grain images, HITL stimuli,
  and every timing bound currently in
  `docs/Authority/Architecture/ExecutionBounds.md`.

## Known behavior (evidence-backed)

- DEC-230, device `midifile-chain-28`, image `79aa5b7f…`: the first Record
  admission is 273.3 ms with `views=270,842 us`; the restamp is 356 us with
  `views=67 us`; `okmask=31` on both.
- `ClockPhysicalReferencePlatform::readCycleCounterForObservation()` already
  reads `ARM_DWT_CYCCNT` behind `SESSION_CAPTURE` and exposes
  `cpuClockHzForObservation()`; its `dsb/isb` barriers make it unsuitable for a
  per-call duration read, which is why the profiler needs a raw inline read.
- `MidiIngressCost` already demonstrates a monotonic-counter validity contract
  (`IngressCounterContract`, `ingressCycleIntervalValid`) — the shape the long
  primitive follows, not its API.
- `DebugSessionCapture::capturePrewarmSplit` already proves the shape of the
  target answer for one function; `stop_tail`/`idle_turn` prove the carved-TU
  pattern that keeps new literals out of DTCM.
- `HotPathTelemetry` keeps five named metrics in `EXTMEM` behind `PERF_TELEMETRY`
  and is a separate, accepted facility; this change leaves it alone.

## Assumptions

- The Teensy 4.1 core leaves `ARM_DWT_CYCCNT` running at 600 MHz on this build
  (`F_CPU_ACTUAL`); the layer reads the existing platform accessor at boot rather
  than programming debug registers itself.
- Twelve scopes are enough to prove the layer cheap and useful before it is
  expanded.

## UNKNOWN

- Whether any of the twelve chosen work bodies is hot enough that the scope's own
  cost is visible in `midi_gap`; resolved by the boot calibration row (minimum and
  average over 128–256 empty scopes) and the paired device A/B, not by assumption.
- The `long` primitive's coarse-clock resolution on the target, and whether the
  per-site `< 71.6 min` guarantee is actually trivial for every chosen site; resolved
  by a native fixture, one device row, and the task 1.5 review before any site relies
  on it.

## Open decisions (flagged, not invented)

- **TBD-1:** final scope set. Twelve sites across ten domains are proposed;
  adding or dropping one is a task-level change, and each short scope must carry
  its own structural guarantee.
- **TBD-2:** whether `DIAG,capture_prewarm_split` is retired after the device
  proof. This change reproduces its numbers and leaves the row in place until the
  DEC-230 repair lands.
- **TBD-3:** ring capacity (256 → 6 KB) and dump batch sizes (16 / 32) after the
  first two real captures.

## Non-goals

- **Level 2** (`-finstrument-functions` whole-image call tracing, address
  symbolization, `no_instrument_function` guards). A separate future change with
  its own laboratory image and perturbation contract.
- **Consolidating `HotPathTelemetry`** into the profiler's table. Deliberately
  deferred: it would make this change also become the canonical owner of an
  accepted telemetry system and force an exact `PERF,` reproduction across two
  compile configurations. Revisit after the profiler has survived real use.
- Full call-chain reconstruction. Level 1 carries the immediate enclosing scope
  and a monotonic `seq`; deeper ancestry needs a different record.
- Bounding, deferring or repairing any measured function — including
  `prewarmViews` (DEC-230's repair is its own slice).
- Instrumenting ISR-entered functions (MIDI clock / `IntervalTimer` paths), and
  instrumenting whole poll functions that usually do no work.
- Per-call streaming, formatted logging or string building on the measured path.
- Replacing `loop_rem`, the 5 s telemetry family, `persist_save,step` or
  `capture_prewarm_split`; those stay until a device-proofed equivalence exists.
- A new UI, a new persistent file format, or any change to `ExecutionBounds.md`
  bound classes.

## Scope authorization

`docs/Runtime/CURRENT_WORK.md`'s newest declarations cover the DEC-229/230
clear/mode/telemetry work and the DEC-230 measurement. This change is a new,
owner-requested observability scope and is **not** authorized by any existing
declaration. Before the first firmware edit, `CURRENT_WORK.md` needs a
one-paragraph `## Now implementing` entry naming this change; that is task 1.1,
and it is a human-visible gate, not an agent assumption.

Related: [DELIVERABLE_TRACKING.md](../../../docs/DELIVERABLE_TRACKING.md) ·
[DEC-230](../../../docs/DECISION_LOG.md) ·
[ExecutionBounds.md](../../../docs/Authority/Architecture/ExecutionBounds.md) ·
[hitl_authoring_traps.md](../../../docs/Agents/hitl_authoring_traps.md).
