# Design — runtime profiling layer (Level 1)

See [proposal.md](proposal.md) for motivation and prerequisites, and
[specs/runtime-profiling/spec.md](specs/runtime-profiling/spec.md) for the normative contract. This
document records the technical choices, their evidence, and the execution contract per slice.

Rebased 2026-10-31 on HEAD `5ddba83e` (after DEC-231 — off-edge view prewarm, and DEC-238 — display-base
arbitration shipped off). The earlier revision was written against `ebaa6ebe` and assumed the DEC-230
repair had not landed.

## Context

Constraints that shape the approach (all verified in this tree):

- **RAM1 is a hard gate.** `scripts/ram1_budget.json` pins `teensy41-capture-serial` at
  `ram1_variables 96,192`, ITCM `393,216`, `padding_floor 840`. `check_ram1_budget.py` runs post-link
  and **fails a build whose env has no approved baseline**. A handful of new string literals can snap
  DTCM by exactly +4,096 B (DEC-216, DEC-225, `hitl_authoring_traps.md` §2). Therefore: no new DTCM
  data, every new literal goes to a carved FLASH TU, and every profiler literal outside that TU is
  behind the build flag.
- **RAM2 (`DMAMEM`, OCRAM2) is the established place for diagnostic state** — and it is **NOLOAD**
  (`.bss.dma` in the core linker script; see the note in `DisplayManager.cpp`). Static `DMAMEM` objects
  hold whatever the previous image left; the profiler must clear itself at boot.
- **A cycle source already exists and is enabled by the core.** Teensy 4 `startup.c` sets
  `DEMCR.TRCENA` and `DWT_CTRL.CYCCNTENA`; `micros()` reads `ARM_DWT_CYCCNT` and extends it with
  `systick_millis_count`, so it is wrap-safe for 71.6 min but **not independent** of the cycle counter
  being enabled. `ClockPhysicalReferencePlatform::cpuClockHzForObservation()` reports the rate (zero when
  the source is disabled); `readCycleCounterForObservation()` issues `dsb/isb` and is not a per-call
  read.
- **The counter can lie after a wrap.** At 600 MHz, `CYCCNT` wraps every 7.158 s; a wrap-safe
  subtraction cannot distinguish 10 ms from 7.168 s. A ceiling does not change that.
- **The DEC-230 repair has landed (DEC-231).** `Track::startRecording` validates/adopts prepared views
  and otherwise requests preparation and returns `false`; `Track::stepViewPrewarmForSlot(slot, 1, 5000)`
  advances one natural unit from the idle owner (`TrackDeferredMaintenance.cpp`) and from the all-stopped
  path (`main.cpp`). Follow-up 3 measured the edge at 2,353 µs and the largest unit at 17,350 µs. Two
  bespoke rows exist: `capture_prewarm_split` (inside `prewarmPlaybackForSlot`) and `view_prewarm_step`
  (after each unit, in `stepViewPrewarmForSlot`).
- **Tier-A is decided by two lists.** `isTierATextLine` (`DebugSessionCapture.cpp`) calls
  `CaptureLineTier::isTierALine` and then its own prefixes, some already gated
  (`#if MIDI_INGRESS_COST_QUALIFICATION`). `check_marker_contract.py` parses both lists; the ring's head
  read is 47 bytes.
- **Decisive rows go direct-first, ring-fallback** (DEC-228 K1), read-only stats commands are not
  transport-gated (DEC-228 K4). Profile dumps are large, so they are batched.
- **`HotPathTelemetry` is a separate, accepted facility** and is not this change's subject.

## Goals / Non-Goals

**Goals**

- One generic timing table and one thresholded ring, usable by any declared owner without a new build
  per investigation, and **truthful across a counter wrap**.
- Zero cost and zero bytes in builds that do not profile.
- Dumps that cannot become the next main-loop stall.
- A vertical slice verifiable end to end: runtime → bounded dump → host decoder → device proof that
  reproduces the **current** DEC-231 attribution before anything else is wired.

**Non-Goals**

- Level 2 compiler instrumentation; `HotPathTelemetry` consolidation (D1); full call chains.
- Any behavior change, bound, deferral or re-ownership of a measured operation.
- ISR-entered scopes; per-call streaming; formatted logging added by the profiler.
- Retiring `loop_rem`, `persist_save,step`, the 5 s telemetry family, `capture_prewarm_split` or
  `view_prewarm_step` (TBD-2 records the condition only).

## Decisions

### D1 — The profiler is additive; both existing timing facilities stay, retirement is staged

Two timing facilities already exist and both stay untouched in this change:

- `HotPathTelemetry` — metrics in `EXTMEM` behind `PERF_TELEMETRY`, `PERF,` text; overlaps
  `Display_frame_phase` and `OverdubStart_body`.
- `Diagnostics::Timing` — 10 identities, `DIAG_TIMING_RECORD` at 15 sites, u32 µs sum/count/max,
  compiled into every `SESSION_CAPTURE` image and emitted as `DIAG,timing` / `DIAG,timing_max`;
  overlaps `PlaybackWindow_build`, `DisplayBase_gather` and `Display_frame_phase`.

The profiler owns a table for its own identities only and is the **intended successor** of both. Merging
now would make this change responsible for accepted facilities and exact row reproduction across two
configurations before the new instrument has survived one investigation, and removal from the default
image is impossible while the profiler is profile-env-only (RED_TEAM R2). Retirement is staged in
[RETIREMENT.md](RETIREMENT.md); this change only records the same-run equivalence evidence it needs.

### D2 — Two primitives and one source-selection rule

- **Short** reads `ARM_DWT_CYCCNT` inline (no barriers) at entry and exit and records the delta.
  Estimated ~20–35 cycles per call.
- **Long** also reads `micros()` at entry and exit. Source selection is fixed:
  `wallUs = microsExit − microsEntry` (wrap-safe u32); if `wallUs < kCycleTrustUs` the cycle delta is
  reported, otherwise `wallUs` is reported and the record is marked coarse-sourced.
  `kCycleTrustUs` is computed once at boot as one counter period at the boot `cpu_hz` minus a 1 s guard
  (`kCycleTrustGuardUs`): 7,158,278 − 1,000,000 = **6,158,278 µs** at 600 MHz. Estimated ~60–90 cycles
  per call (two `micros()` calls). Both overheads are measured at boot (D14).

The boot contract is read once from `cpuClockHzForObservation()`; `0` means unavailable and **every**
measurement — short and long — is refused. Because `micros()` depends on the same counter, the wall
clock is a wrap disambiguator, never an alternate source. The layer never programs `DEMCR`/`DWT_CTRL`;
on this core `source=0` is not expected and the device leg records `source=1`.

Measured durations include time spent in interrupts that preempt the scope (MIDI, `IntervalTimer`,
DMA completion). This is reported, not corrected: inclusive and self time are wall time on the main
lane, which is what a main-loop stall is.

### D3 — Interval validity is a property of the site, not of the ceiling

A short scope is permitted **only** where an independent guarantee establishes that the operation
cannot span one counter period: a declared grain, a bounded unit count, a fixed frame **with no
blocking I/O**. "The ceiling is below 7.16 s" is not a guarantee and is rejected in review. A step that
is bounded for one sub-operation but can also perform another (for example construction **and**
publication) is long unless the scope is placed around the bounded sub-operation only.

Every long site records the structural reason it cannot reach one `micros()` period (≈ 71.6 min): for
all sites here, "synchronous main-loop call, no unbounded wait, no user input, no network, no sleep".
A site that cannot state it needs a 64-bit monotonic clock, which this capability does not provide.

Runtime-detectable rules:

- **Interrupt context is refused at entry.** The scope reads `IPSR` (via a shim, faked in native);
  non-zero means the operation was reached from an exception handler, so the scope neither pushes a
  frame nor records, and `isrRefused` increments (reported in the stats terminator). Without this, an
  ISR scope would exit LIFO on top of an interrupted main-lane frame and corrupt it silently.
- **Before `initAtBoot()`** the `DMAMEM` state is stale (NOLOAD). A magic word rejects cold-boot
  garbage; because a warm reset preserves the previous boot's magic, every ring/stack index is also
  bounded on use, so an early scope can only touch the profiler's own state, which init then clears.
- `elapsed == 0` is a valid measurement.
- An unavailable timing source refuses the measurement and increments `refused`, for both variants.
- An out-of-order exit increments `orderFaults` and **poisons** the nesting state: no further
  measurement is recorded until an explicit reset. Depth bookkeeping continues while poisoned, so live
  scopes still unwind and depth can return to zero; the global `poisoned` flag is reported in both dump
  terminators. With RAII scopes on one lane this is not expected; it guards a misuse (a scope object
  moved or stored).

The declared ceiling keeps a build-time sanity assert (below `kCycleTrustUs`) that only rejects
nonsense declarations.

### D4 — Aggregate record, depth stack and self time

One 48-byte record per identity in `DMAMEM`, ordered so the 64-bit fields are naturally aligned:

```text
inclusiveCycles u64 | selfCycles u64
calls u32 | maxCycles u32 | maxUs u32 | lastCycles u32 | lastUs u32 | overCeiling u32 | refused u32
orderFaults u16 | lastResult u8 | flags u8
```

`static_assert(sizeof(ProfileStat) == 48)` pins the layout. Cycle fields are exact only inside one
counter period; microsecond fields are always filled (an exact measurement converts cycles → µs), so a
dump never guesses a unit and a coarse value larger than `UINT32_MAX` cycles is never truncated.
`flags` bit 0 = at least one coarse-sourced measurement; `lastResult = 0xFF` = none supplied.

Identity capacity **64** (envelope 128, `static_assert`); table 3,072 B. Nesting uses a **16-frame**
stack (`id u16, pad u16, pad u32, childNormalizedCycles u64` = 16 B, 256 B). On exit:
`self = elapsed − frame.childNormalizedCycles`, and `elapsed` is added to the parent's
`childNormalizedCycles`.

**Depth overflow:** a scope entered at depth 16 is not pushed; `depthOverflow` increments and a live
`overflowDepth` counter tracks nested overflowed scopes. An overflowed scope records inclusive only
(`self = inclusive`). Only the **outermost** overflowed scope (the one that brings `overflowDepth` back
to 0) adds its elapsed to the top frame's child accumulator, so nested overflowed children are never
double-counted. With 13 sites the measured maximum nesting is 4 (`IdleOwner_unit` →
`ViewPrewarm_unit`, `RecordStart_edge` → `PlaybackPrewarm_slot` → `PlaybackPrewarm_views`), so 16 is
headroom, not a working limit.

A coarse measurement contributes its wall-clock duration converted to cycles to the u64 totals (71.6
min fits); the u32 cycle maxima saturate and the µs maxima keep the true value.

### D5 — Declared ceilings are report-only classes, not new bounds

`PROFILE_CLASS_CLOCK/HOT/FRAME/DEFERRED/COLD` = 50 µs / 250 µs / 2 ms / 5 ms / 25 ms, or an existing
named budget (`MidiServiceDrain::kOwnerWorkBudgetUs` = 4,000 µs for the idle-owner unit). A class
decides ring entry only. No owner's bound class changes and `ExecutionBounds.md` gains no row (DEC-043
point 4, DEC-214). The new `DEC-###` records the class table as report-only.

### D6 — 24-byte ring record with a monotonic sequence and both duration units

```text
seq u32 | timestampUs u32 | cycles u32 | micros u32 | id u16 | parentId u16 |
track u8 | slot u8 | result u8 | flags u8
```

Both duration fields are written. `cycles` is exact only when `flags` bit 0 (coarse) is clear; when
set, `cycles` is saturated and `micros` is the reported duration. `track`/`slot` = `0xFF` when the site
supplied none. Capacity **256** = 6,144 B, overwrite-oldest, `dropped` monotonic. `parentId` is the
immediate enclosing profiled scope (`0xFFFF` = none); no grandparent is claimed.

Total RAM2: table 3,072 + ring 6,144 + stack 256 + counters ≈ **9.5 KB**. No allocation, ever.

### D7 — Optional result and context, set at any point before exit

```cpp
{
  PROFILE_SCOPE_LONG(scope, ProfileId::X);   // or PROFILE_SCOPE_SHORT
  scope.setContext(trackIndex, slotIndex);   // optional; ring track/slot
  if (failed) { scope.setResult(static_cast<uint8_t>(Result::NoCapacity)); return false; }
}
```

`setResult()` and `setContext()` may be called any time before destruction. A reference form
(`PROFILE_SCOPE_LONG_RESULT(scope, ProfileId::X, resultVar)`) reads a `bool`/enum variable at exit.
Duration is recorded whether or not either was supplied. With the flag off, the macros expand to a
no-op object whose setters are empty inline functions, so call sites compile unchanged. No
cross-domain outcome enum is introduced.

### D8 — Bounded commands at depth zero, with a cursor, a sequence horizon and a terminator

`!PROFILE_STATS`, `!PROFILE_RING`, `!PROFILE_RESET`, added beside `!CAP_IDENTITY_STATS` in
`StorageManagerHitlSerial.cpp` inside `#if RUNTIME_PROFILE_SCOPES`; the comparison literals live in
`ProfileDiagRows.cpp` behind an accessor so they reach the carved section.

- **Every profile command runs only at profiler depth 0**; otherwise `profile_cmd,refused,depth=<n>`
  and nothing changes.
- **Stats** emits at most **16** rows per request; **ring** at most **32**. Each ends with a terminator
  carrying `count`, `resume`, `poisoned`, and for the ring `dropped`, `range_start`, `range_end`, `gap`.
- **Ring horizon:** the first batch pins `endSeq = latest seq` and
  `startSeq = max(oldest available, endSeq − capacity + 1)`. Only that range drains; later breaches
  belong to the next session. Overwritten parts of the pinned range are reported as `gap`.
- **Stats** is a multi-batch snapshot over a stable id cursor (`multi_batch=1` when more than one batch).
- Each dump has its own persisted cursor; reset clears both. A ring dump **drains**: a later session
  starts after the last seq an earlier one emitted or reported as a gap, so no record is emitted twice.
- **Reset** clears table, ring, `dropped`, `depthOverflow`, `poisoned` and both cursors, nothing else.
  `isrRefused` is kept (it reports a wiring fault, not a measurement) and `seq` stays monotonic.
- Rows are direct-first/ring-fallback (DEC-228 K1), not transport-gated (DEC-228 K4); the batch bound
  keeps them from stalling the loop.

Rows:

```text
#CAP,<us>,DIAG,profile_boot,source=<0|1>,cpu_hz=,samples=,short_min_cycles=,short_avg_cycles=,long_min_cycles=,long_avg_cycles=,ids=,ring=,depth=
#CAP,<us>,DIAG,profile_stat,<id>,<name>,calls=,avg_us=,avg_self_us=,max_us=,last_us=,over=,ref=,order=,coarse=,last_result=
#CAP,<us>,DIAG,profile_stat,end,count=,resume=,multi_batch=<0|1>,poisoned=,depth_overflow=,isr_refused=
#CAP,<us>,DIAG,profile_ring,<seq>,t_us=,id=,name=,parent=,dur_us=,trk=,slot=,result=,src=
#CAP,<us>,DIAG,profile_ring,end,count=,dropped=,resume=,range_start=,range_end=,gap=,poisoned=
#CAP,<us>,DIAG,profile_reset,<ids>
#CAP,<us>,DIAG,profile_cmd,refused,depth=<n>
```

Cycles convert to microseconds at dump time only. The boot row is the only unsolicited profile row
and is emitted once per boot including `source=0`.
It is enqueued on the Tier-A capture ring (`Platform::emitRetainedRow`), never only written directly:
a direct boot-time write is lost when no host reads yet (observed on device in slice 3).

### D9 — Registration in the classifier source list, behind the flag

`DIAG,profile_` is added to `isTierATextLine` as a `PROGMEM` named prefix under
`#if defined(PIO_UNIT_TEST_NATIVE) || RUNTIME_PROFILE_SCOPES`, following `kIngressCostTierPrefix`. The
header list is unchanged. Pinned three ways: `test_capture_ring` (the real predicate and the 47-byte
head read), `test_capture_line_tier` (header unchanged), and `check_marker_contract.py --audit`.

Names, row formats and command strings live in `src/Utils/ProfileDiagRows.cpp` with one linker line
(`*ProfileDiagRows.cpp.o(.rodata.str* .rodata.*.str*)`) beside the `StopDiagRows`/`EnvelopeDistRows`
lines in `linker/imxrt1062_t41_lcr.ld`.

### D10 — Build gating is a dedicated environment, and absence is total

`RUNTIME_PROFILE_SCOPES=1` is defined only by `teensy41-capture-serial-profile`, which extends
`teensy41-capture-serial` and has its own `ram1_budget.json` baseline (initially an alias to the default
env; a separate figure only if it links differently, with a note). With the flag undefined, macros are
no-ops, the record TU compiles to nothing, the Tier-A prefix and commands are absent. The default and
`-no-grain` images must link to the same RAM1, ITCM and boot behavior as the P3 baseline — a gate.

### D11 — Instrument the work body, never the poll entry; the profiler adds no I/O

The scope goes **after the cheap no-work/admission gate**, on the body that does the work, and the
identity names that body. A site that cannot be split this way is not instrumented.

The profiler adds no serial, allocation or formatting. **Existing** session-capture rows that a body
already emits are part of that body's measured cost unless the scope closes before them; each site
states which applies. Where a dedicated row reports the same interval (`view_prewarm_step`), the scope
closes before the row so the two numbers are comparable.

### D12 — Proposed sites, variant and ceiling

| Identity | Entry point (scope placement) | Variant | Ceiling | Guarantee / why long | Existing rows inside |
|---|---|---|---|---|---|
| `RecordStart_edge` | `Track::startRecording` whole body | long | COLD 25 ms | composes `prewarmPlaybackForSlot`; not-prepared early `return false` is timed too | `capture_prewarm_split` (via child) |
| `PlaybackPrewarm_slot` | `Track::prewarmPlaybackForSlot` body | long | COLD 25 ms | composes seven reserves; unbounded at boot/load | `capture_prewarm_split` |
| `PlaybackPrewarm_views` | `LoopPlaybackRuntime::prewarmViews` body | long | COLD 25 ms | monolithic pool allocation (boot/load, overdub start); skipped on the prepared edge | none |
| `ViewPrewarm_unit` | `runtime->stepViewPrewarm(...)` call in `Track::stepViewPrewarmForSlot`, closed before `viewPrewarmStep` | long | DEFERRED 5 ms (= the site's 5,000 µs guard) | one unit, but a unit is one external allocation measured at 17,350 µs and the time guard is secondary (DEC-231) — no hard cap | none (row after scope) |
| `PlaybackWindow_build` | free function `ensurePlaybackMergedMidiEventsBuilt(Track&, …)` body | long | FRAME 2 ms | content-sized window, no hard event cap | none known |
| `PlaybackViewService_grain` | `Track::stepPlaybackViewMaintenance` admitted grain | **short** | DEFERRED 5 ms | one declared grain, ≤ 64 inspected / ≤ 48 order / ≤ 64 retirement units (row 27) | verify in 1.5 |
| `SnapshotPublish_step` | `Loop::stepPendingSnapshotPublish` call in `TrackDeferredMaintenance.cpp` | long | DEFERRED 5 ms | row 21 bounds construction only; the same call can publish (`publishedSnapshotRevision` moves) | qualification rows are outside the call |
| `SaveWorkItem_step` | deferred-save work-item step | long | DEFERRED 5 ms | SD write | `persist_save,step` (state per site) |
| `IdleOwner_unit` | `Track::processDeferredIdleMaintenance` admitted unit body | long | `kOwnerWorkBudgetUs` | composes several children | `idle_turn` (state per site) |
| `DisplayBase_gather` | `DisplayManager::buildCommittedDisplayBase` | long | FRAME 2 ms | 16-bar window, content-sized rows; the shipped path while DEC-238 keeps `DisplayBaseGrain` off | verify in 1.5 |
| `Display_frame_phase` | `DisplayManager::update` phase work after the pending-phase gate | long | FRAME 2 ms | row 4: includes the **blocking** `_display.api.display()` transfer | `disp_src`/paint rows (state per site) |
| `LoadStep` | deferred `LoadLoopJob` step | long | DEFERRED 5 ms | SD read | verify in 1.5 |
| `OverdubStart_body` | `Track::startOverdubbing` body after the refusal admission | long | COLD 25 ms | composes `prewarmViews`/allocation | none known |

**One short scope, twelve long.** The earlier revision listed `Display_frame_build` and
`SnapshotPublish_step` as short; row 4's blocking transfer and the publish path in the same call
remove both guarantees. `PlaybackWindow_build` and `DisplayBase_gather` stay long (a window bounds the
span, not the content). Task 1.5 re-checks every row against the code at the rebased HEAD.

### D13 — Native testability at the observation boundary only

The record path reads both clocks through `profileCyclesNow()` / `profileMicrosNow()`; native fixtures
advance fake clocks, as the ingress-cost fixtures do. `DMAMEM` already expands to nothing in
`test/test_support/firmware/Arduino.h`; the boot-clear test therefore **pre-fills** the static storage
with a non-zero pattern before calling `initAtBoot()`, so host zero-initialization cannot hide a
missing clear. Row formatting is tested through a text sink. No production branch is added for tests.

### D14 — Boot initialization and calibration

`RuntimeProfiler::initAtBoot()` is called from `setup()` **before** the first instrumented owner can run
(boot load and boot prewarm run in `setup`); the exact call position is a P6 prerequisite. It clears
table, ring, stack and counters, reads `cpu_hz`, then calibrates **both** primitives: 128–256 empty
short scopes and the same number of empty long scopes into a reserved calibration identity, with no
serial inside the loop. It records min and average cycles per variant, clears the calibration
identity, and emits the boot row once.

## Execution contract (G2 packets)

Each slice is independently buildable and reviewable. Evidence lives in `evidence/<date>-<slice>.md`
and is distinguished as specified / fixture / native / device. Group 0 prerequisites (proposal P1–P8)
precede slice 1.

### Slice 1 — runtime core, short and long primitives, aggregate and ring

- **Owner / entry:** `RuntimeProfiler` (`include/Utils/RuntimeProfiler.h`,
  `src/Utils/RuntimeProfiler.cpp`). No production caller other than `initAtBoot()`.
- **Preconditions:** P1–P6 complete.
- **Mutation:** identity enum + capacity/layout asserts; `DMAMEM` table/ring/stack; both primitives and
  the D2 selection rule; refusal, order-fault/poison and overflow guards; boot clear and two-variant
  calibration; macros that compile to no-ops without the flag.
- **Postconditions:** with the flag on and no sites, the table is zero after boot even when storage was
  pre-filled, the boot row reports source and both calibrations, and a synthetic identity records one
  call per scope.
- **Must preserve:** default/no-grain link figures (P3); `HotPathTelemetry` untouched.
- **Oracle:** short-exact vs long-coarse at the `kCycleTrustUs` boundary (just below / at / above),
  inclusive/self split, order fault + poison + reset, depth overflow without double counting, ceiling
  entry, ring overwrite + `seq` gap, refusal counters, boot clear over a pre-filled pattern.
- **Validation:** `test_runtime_profiler`; profile-env build at RAM1 `96,192`; default env figures equal
  P3.

### Slice 2 — bounded commands, Tier-A registration, host decoder

- **Owner / entry:** `StorageManagerHitlSerial.cpp` command table + `isTierATextLine` +
  `ProfileDiagRows.cpp` + `scripts/profile_report.py`.
- **Preconditions:** slice 1 landed; P5 linker line present.
- **Mutation:** three commands with independent cursors, batch maxima, terminators, a pinned ring
  horizon and the depth-zero gate; gated Tier-A registration; carved formats; the decoder.
- **Postconditions:** repeated requests drain a full table/ring in batches; a command at depth > 0 is
  refused and changes nothing; a mid-dump breach is excluded from the pinned range.
- **Must preserve:** every existing serial command, `!CAP_IDENTITY_STATS`, `!TRACE_DRAIN`; default image.
- **Oracle:** N identities drain in ⌈N/16⌉ requests with a correct `resume` sequence; M ring records
  drain in ⌈M/32⌉ within the pinned range; a refused command leaves table, ring and cursors
  byte-identical.
- **Validation:** `test_runtime_profiler` row/sink cases; `test_capture_ring`, `test_capture_line_tier`;
  `check_marker_contract.py --audit` exit 0; `scripts/test_profile_report.py`; both target builds.

### Slice 3 — the DEC-231 vertical proof

- **Owner / entry:** `Track::startRecording`, `Track::prewarmPlaybackForSlot`,
  `LoopPlaybackRuntime::prewarmViews`, `Track::stepViewPrewarmForSlot` — four identities.
- **Preconditions:** slices 1–2 landed; P7 stimulus and P8 same-day reference capture.
- **Mutation:** four long scopes; `prewarmViews` and `stepViewPrewarm` pass their existing `bool` via
  `setResult`; `setContext(track, slot)` where the indices are in scope.
- **Postconditions / oracle** (against P8, not against older images):
  - `ViewPrewarm_unit`: ring records for the units above 5 ms; per-unit `dur_us` is **≤** the same
    run's `view_prewarm_step us=` (the row brackets a superset of the scope); measured offset 32–42 µs
    (the row's bracket also covers its own argument evaluation), so later comparisons use [0, 50] µs;
    max ≈ the P8 largest unit; `parentId` = `IdleOwner_unit` only once slice 4 lands, otherwise none.
  - `PlaybackPrewarm_slot` self time includes `capture_prewarm_split`'s own emission (RED_TEAM R5);
    the evidence records it rather than subtracting it.
  - Retirement evidence (RETIREMENT.md stage 0): the same run records `capture_prewarm_split`'s
    `views`/`total` beside `PlaybackPrewarm_views`/`PlaybackPrewarm_slot`.
  - `RecordStart_edge`: prepared edge under COLD (no ring record), aggregate `last_us` ≈ P8 edge;
    a not-prepared edge appears as a short call with the not-prepared result, not as a breach.
  - `PlaybackPrewarm_views`: aggregate call count does **not** move across a prepared Record edge
    (skip path); it does move on boot/load.
  - `capture_prewarm_split` in the same run still reports `views=0` at the edge.
  - `midi_gap`/`late_on` in the record window within the P8 spread; no new `win_gap_overrun`.
- **Must preserve:** both existing rows and their numbers; `okmask` semantics; return paths.
- **Non-goals:** repairing the 13–17 ms units; any other site.
- **Validation:** focused native suites for the four owners unchanged; profile-env build; one device run.

### Slice 4 — the remaining nine bodies, coverage and overhead A/B

- **Owner / entry:** `PlaybackWindow_build`, `PlaybackViewService_grain`, `SnapshotPublish_step`,
  `SaveWorkItem_step`, `IdleOwner_unit`, `DisplayBase_gather`, `Display_frame_phase`, `LoadStep`,
  `OverdubStart_body`.
- **Preconditions:** slice 3 accepted on device.
- **Mutation:** nine scopes per D11/D12; no control-flow edits.
- **Postconditions:** the aggregate dump shows at least one identity per declared domain after a run
  that records, overdubs, plays, saves and loads; `ViewPrewarm_unit` now reports `IdleOwner_unit` as
  parent when stepped from the idle owner and none from the all-stopped path.
- **Overhead proof:** paired device runs on the profile image and the P3 pre-change default image with
  the same stimulus. `midi_gap`/`late_on` maxima and `win_gap_overrun` count must not worsen beyond the
  pre-declared tolerance (no new overrun; maxima within the baseline pair's run-to-run spread). A site
  that breaks the tolerance is removed and recorded. Each site's evidence lists the legacy timers
  already present in it (`HotPathTelemetry`, `Diagnostics::Timing`, session-capture rows) so a removal
  decision cites the profiler's own calibrated cost, not the whole delta (RED_TEAM R7).
- **Retirement evidence:** the same runs record `DIAG,timing`/`timing_max`, `idle_turn` and
  `persist_save,step` beside their identities (RETIREMENT.md stage 0).
- **Validation:** full native suite; both builds; two device runs; closeout docs.

## Risks / Trade-offs

- **Probe effect.** → No profiler I/O on the path; both primitives calibrated; paired A/B.
- **A short scope can still alias after a wrap.** → Only one short scope remains, with a row-27
  guarantee checked in 1.5.
- **A dump becomes the next stall.** → Batch maxima, cursors, pinned horizon, depth-0 gate.
- **Stale RAM2 at boot.** → `initAtBoot()` before any owner; pre-filled native fixture.
- **Default image drift from ungated literals.** → Flag-gated prefix and commands; P3 baseline gate.
- **Existing diag rows inflate a measured body.** → Declared per site (D11/D12); the slice-3
  equivalence site closes before its row.
- **Interrupt time is included.** → Stated in the contract; the A/B compares like with like.
- **An instrumented owner reached from an ISR.** → `IPSR` check at entry refuses and counts it.
- **Three timers on one body (`HotPathTelemetry`, `Diagnostics::Timing`, the profiler).** → Named in
  D1; A/B attributes only calibrated profiler cost; staged retirement in RETIREMENT.md.
- **`parentId` read as a call tree.** → Printed as immediate enclosing scope only.
- **Ring owned by ordinary Tier-A traffic (DEC-218 lesson).** → Only over-ceiling calls enter the
  ring; dumps on demand; boot row once.
- **Scope creep into the preparation repair.** → Slice 3 is measurement only.

## Migration / rollback

- No data migration, no SD format change, no persistent state.
- Rollback is removing `-D RUNTIME_PROFILE_SCOPES=1`; the default images are unaffected.
- A site that proves too hot or fails review is deleted; identities are append-only.

## Open questions

- Ring capacity and batch maxima — tunable after the first two captures (TBD-3).
- Whether calibration should also report a nested parent+child pair; deferred until first numbers.

## Pre-implementation review (2026-10-31, before slice 1)

Per `.cursor/rules/Plan-Pre-Implementation-Review.mdc`, against the code at `fec18994`. Site-level
detail: [evidence/DECISION_REVIEW.md § Site table review](evidence/DECISION_REVIEW.md).

### Ready

- Scope authorized (`CURRENT_WORK.md`, 2026-10-31); DEC-239 appended; decision review filled.
- Baselines recorded before any firmware edit ([evidence/PREFLIGHT.md](evidence/PREFLIGHT.md)): default
  `2c7bb5d3…`, no-grain `22d5638f…`, both variables 96,192 / code 391,928 / padding 1,288.
- Profile env, its `ram1_budget.json` alias and the `ProfileDiagRows.cpp` linker rule exist.
- Boot hook: immediately after `emitBootResetCapture(...)` in `setup()`, before
  `trackManager.prewarmPlaybackRuntime()`.
- HITL: owner rig reused unchanged; dumps sent from the capture session after the rig exits.

### Resolved (user / code)

| Topic | Decision |
|---|---|
| Runtime per-ID arming | Follow-up change (owner, 2026-10-31) |
| `PlaybackWindow_build` owner | Free function `ensurePlaybackMergedMidiEventsBuilt(Track&, …)`, not a `Track` member |
| Command dispatch vs depth gate | `processHitlSerialCommands()` is called from `loop()` outside every scope |
| Tier-A prefix form | `PROGMEM` named prefix in `isTierATextLine` (flash-resident, parsed by `check_marker_contract.py`), not an accessor function — the checker does not resolve accessor prefixes (it reports `DIAG,idle_turn,` as not Tier-A today) |
| `Display_frame_phase` placement | The phase `switch` in `DisplayManager::update`, only when the phase is not `Idle` |
| `IdleOwner_unit` placement | Whole `Track::processDeferredIdleMaintenance` body; its no-work gate is the caller's admission |

### Open items the implementer pins at session start

1. **ITCM headroom is 448 B above the floor.** Scope objects are `FLASHMEM`-backed: slice 1 made the
   constructor, destructor and platform reads out-of-line `FLASHMEM` functions (no inline code at a site
   beyond the calls), so calibration measures that cost. Measure the profile env after
   slice 1 *and* after each slice-3/4 site; a site in an ITCM function that costs padding is moved to
   a `FLASHMEM` wrapper or dropped.
2. **Native has no `DMAMEM`/`FLASHMEM`/`ARM_DWT_CYCCNT`.** Clock and IPSR shims are the only
   platform seam; the native test owns fake values (D13).
3. ~~`kCycleTrustUs` source~~ — **resolved in slice 1:** computed at `initAtBoot()` from `cpu_hz` minus a
   1 s guard (6,158,278 µs at 600 MHz); pinned by the boundary fixture.
4. **Slice-3/4 exact line placement** for `PlaybackViewService_grain`, `SaveWorkItem_step`,
   `OverdubStart_body` and `LoadStep` is pinned in their execution packets with the rule recorded in
   the site review.
5. **Tooling gap seen in passing:** `check_marker_contract.py` misreports accessor-function prefixes
   (`DIAG,idle_turn,`, `DIAG,stop_tail,`). Not this change's scope; noted for a host-tooling fix.
