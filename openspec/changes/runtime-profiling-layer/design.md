# Design — runtime profiling layer (Level 1)

See [proposal.md](proposal.md) for motivation and
[specs/runtime-profiling/spec.md](specs/runtime-profiling/spec.md) for the normative contract. This
document records the technical choices, their evidence, and the execution contract per slice.

## Context

Constraints that shape the approach (all verified in this tree):

- **RAM1 is a hard gate.** `scripts/ram1_budget.json` pins `teensy41-capture-serial` at
  `ram1_variables 96,192`, ITCM `393,216` (code ≈ `392,120` + padding), `padding_floor 840`.
  `check_ram1_budget.py` runs post-link, and a handful of new string literals can snap
  `.rodata`/DTCM by exactly +4,096 B (DEC-216, DEC-225, `hitl_authoring_traps.md` §2). Therefore:
  no new DTCM data, and every new literal goes to a carved FLASH TU.
- **RAM2 (`DMAMEM`, OCRAM2) is the established place for diagnostic state**: capture-ring state,
  `disp_src`, the overdub-start refusal counters, `PersistenceWorkItemPersist` slice logs,
  `LoadLoopJob`. `EXTMEM` (PSRAM) is used by `HotPathTelemetry` — correct for five rare records,
  wrong for a per-call table.
- **A cycle source already exists**:
  `ClockPhysicalReferencePlatform::readCycleCounterForObservation()` reads `ARM_DWT_CYCCNT` under
  `SESSION_CAPTURE` and `cpuClockHzForObservation()` reports the rate only when `DEMCR.TRCENA` and
  `DWT_CTRL.CYCCNTENA` are set. It issues `dsb/isb` barriers on purpose (physical clock
  observation), so it is not a per-call duration primitive.
- **The counter can lie after a wrap.** At 600 MHz a 32-bit cycle counter wraps every 7.158 s, and
  a wrap-safe subtraction cannot distinguish 10 ms from 7.168 s. Nothing about a scope's *ceiling*
  changes that: the ceiling is a reporting threshold, not evidence of the operation's real
  duration.
- **A one-off timer already solved this problem once**: `Track::prewarmPlaybackForSlot` carries seven
  `micros()` brackets and `DebugSessionCapture::capturePrewarmSplit` (carved `StopDiagRows.cpp`) emits
  one direct row after the call. DEC-230 read `views=270,842 us` of `total=273,314 us` from it.
- **Tier-A is decided by two lists.** `isTierATextLine` (`DebugSessionCapture.cpp`) calls
  `CaptureLineTier::isTierALine` (`CaptureLineTier.h`) and then adds prefixes registered in the
  `.cpp`. The header's literals are instantiated per including TU and were measured to snap DTCM by
  4,096 B, so new families belong in the `.cpp` list; `test_capture_ring` asks the real predicate and
  `test_capture_line_tier` pins the header. `check_marker_contract.py` parses both.
- **Decisive rows go direct-first, ring-fallback** (DEC-228 K1), and read-only stats commands are not
  transport-gated (DEC-228 K4). Profile dumps are the opposite risk — they are *large*, so they are
  batched rather than made maximally direct.
- **`HotPathTelemetry` is a separate, accepted facility** (five named metrics in `EXTMEM` behind
  `PERF_TELEMETRY`, plus `snapshotStats`, plus its `PERF,…` summary text). It is not this change's
  subject.

## Goals / Non-Goals

**Goals**

- One generic timing table and one thresholded ring, usable by any declared owner without a new
  build per investigation, and **truthful across a counter wrap**.
- Zero cost and zero bytes in builds that do not profile.
- Dumps that cannot become the next main-loop stall.
- A vertical slice that is verifiable end to end: runtime → bounded dump → host decoder → device
  proof that reproduces DEC-230's known attribution before anything else is wired.

**Non-Goals**

- Level 2 compiler instrumentation (`-finstrument-functions`), address symbolization, laboratory
  image. Separate change.
- Consolidating `HotPathTelemetry` into the profiler's table (see D1).
- Full call-chain reconstruction; Level 1 carries the immediate enclosing scope and `seq`.
- Any behavior change, bound, deferral or re-ownership of a measured operation.
- ISR-entered scopes; per-call streaming; formatted logging on the measured path.
- Retiring `loop_rem`, `persist_save,step`, the 5 s telemetry family or `capturePrewarmSplit`.

## Decisions

### D1 — The profiler is additive; `HotPathTelemetry` consolidation is deferred

`HotPathTelemetry` keeps its five metrics, its `EXTMEM` storage, its `PERF_TELEMETRY` gating and its
`PERF,` summary text exactly as they are. The profiler owns a table for **its own** identities only.

*Why not extend it now:* it would make this change simultaneously responsible for an accepted
telemetry system, an exact `PERF,` reproduction, reserved identities and two compile configurations —
all before the new instrument has survived one real investigation. The decision ladder's "extend the
existing owner" applies to the *concern being added* (aggregate timing exists); it does not require
merging two independent facilities in the first slice. Consolidation becomes its own change after the
profiler is proven, and is recorded as a non-goal here.

*Alternatives:* (a) migrate the five metrics now — rejected, unbounded equivalence risk for no
investigative gain; (b) a second table for the same identities — not applicable while the facilities
stay separate.

### D2 — Two primitives: short (DWT only) and long (DWT + coarse monotonic bracket)

- **Short** reads `ARM_DWT_CYCCNT` at entry and exit (inline, no barriers, ~1–2 cycles each) and
  records the cycle delta. Estimated total ~20–35 cycles ≈ 35–60 ns per call at 600 MHz.
- **Long** additionally reads `micros()` at entry and exit. When the cycle delta is inside one
  counter period it reports the cycle duration; otherwise it reports the wall-clock duration and
  marks the record coarse-sourced. Estimated ~60–90 cycles ≈ 100–150 ns.

Both use the same table and ring; only the sampling differs. The boot contract is read once from
`ClockPhysicalReferencePlatform::cpuClockHzForObservation()`; `0` means the timing source is
unavailable and **every** measurement — short and long — is refused, not fabricated.

The layer never programs `DEMCR`/`DWT_CTRL`. Whether the Teensy core leaves the counter enabled is
**UNKNOWN** until the boot row proves it. The long primitive's wall clock exists only to disambiguate
a cycle-counter wrap; it is not an alternate timing source when the cycle counter is missing.

### D3 — Interval validity is a property of the site, not of the ceiling

A short scope is permitted **only** where an independent guarantee establishes that the operation
cannot span one counter period. The guarantee is recorded beside the site and must be checkable
against the operation's own code or bound (a declared grain, a bounded page count, a fixed frame).
"The ceiling is below 7.16 s" is explicitly **not** a guarantee and is rejected in review.

A long scope has a weaker but still explicit contract: two 32-bit `micros()` samples cannot detect a
full wrap, so every long site must record the structural reason its operation cannot reach one
`micros()` period (≈ 71.6 min). For these sites the reason is the same and trivial — a synchronous
main-loop call with no unbounded wait, no user input, no network and no sleep — but it is stated
beside the site rather than assumed. A site that cannot state it needs a 64-bit monotonic clock,
which this capability does not provide.

Sites with no such guarantee use the long primitive: record admission, prewarm composition, the
idle-owner turn, and anything touching SD. A pathological multi-second call there is reported as a
wall-clock duration instead of aliasing to a small number.

The remaining validity rules are what a runtime can actually detect:

- `elapsed == 0` is a valid recorded measurement (a sub-cycle call).
- An unavailable timing source refuses the measurement and increments `refused`, for both variants.
- An out-of-order exit increments `orderFaults` and **poisons the nesting state**: no further
  measurement is recorded until an explicit reset, so a corrupted stack can never fabricate a parent
  or a self time. Live scopes still unwind; once depth returns to zero the reset is permitted.

The declared ceiling keeps a build-time sanity assert (below one counter period) but that assert is
**not** presented as interval validity; it only rejects nonsense declarations.

*Alternative:* make every scope long — rejected; the short primitive is the cheap default for
structurally bounded work and its guarantee is a stronger statement than a wall clock could make.

### D4 — Aggregate record, depth stack and self time

One 48-byte record per identity in `DMAMEM`:

```text
calls u32 | maxCycles u32 | maxUs u32 | lastCycles u32 | lastUs u32
overCeiling u32 | refused u32
inclusiveCycles u64 | selfCycles u64 | orderFaults u16 | lastResult u8 | flags u8
```

The **cycles** fields are exact only inside one counter period; the **microseconds** fields carry the
wall-clock duration that the long primitive reports when the cycle delta spans a period. Both are
always filled (an exact measurement converts cycles → µs for the µs fields), so a dump never has to
guess a unit, and a coarse value larger than `UINT32_MAX` cycles is never truncated.
`flags` bit 0 records that at least one included measurement was coarse-sourced.

Identity capacity is **64** for the first image (envelope 128, pinned by `static_assert`); the table
is 3 KB. Nesting uses a **16-frame** fixed stack in `DMAMEM` (`id u16, pad u16, childNormalizedCycles
u64` = 16 B per frame, **256 B**). On exit: `self = elapsed − frame.childNormalizedCycles`, the
frame's inclusive is added to the parent's `childNormalizedCycles`, and both totals are recorded.
Depth overflow records inclusive only and increments a
`depthOverflow` counter — it never corrupts the stack.

A coarse-sourced measurement contributes its wall-clock duration **converted to cycles only for the
u64 inclusive/self totals** (a 71-minute bound fits comfortably in u64); the u32 maxima keep the
saturated cycle value and the true microsecond value side by side.

### D5 — Declared ceilings are report-only classes, not new bounds

Five classes, `PROFILE_CLASS_CLOCK/HOT/FRAME/DEFERRED/COLD` = 50 µs / 250 µs / 2 ms / 5 ms / 25 ms, or
an existing named budget (`MidiServiceDrain::kOwnerWorkBudgetUs` for the idle-owner turn, so the
profiler ceiling cannot drift from the enforced one). A class decides ring entry only. No owner's
bound class changes and `ExecutionBounds.md` gains no row. This follows DEC-043 point 4 and DEC-214
("naming is not a bound"); a new `DEC-###` records the class table as report-only.

### D6 — 24-byte ring record with a monotonic sequence and both duration units

```text
seq u32 | timestampUs u32 | cycles u32 | micros u32 | id u16 | parentId u16 |
track u8 | slot u8 | result u8 | flags u8
```

`cycles` is the exact duration inside one counter period; `micros` is the wall-clock duration. Exactly
one is meaningful per record — the coarse flag says which — so a 10-second pathological call is
reported as 10,000,000 µs instead of a truncated cycle count. `flags` bit 0 marks a coarse-sourced
record.

Capacity **256** = 6 KB, overwrite-oldest, `dropped` monotonic. `seq` makes ordering and overwrites
unambiguous across long captures and `micros()` wrap; the host can detect the overwrite boundary
from the gap alone. `parentId` is the **immediate enclosing profiled scope** — when the parent did
not itself breach, no grandparent is available and none is claimed.

Total added RAM2: **≈ 9.4 KB** (table 3 KB + ring 6 KB + stack 256 B + counters). No allocation,
ever; the whole structure is a static object in one TU.

### D7 — Result metadata is optional and set at any point before exit

The scope API is:

```cpp
{
  PROFILE_SCOPE_SHORT(ProfileId::X);          // or PROFILE_SCOPE_LONG(...)
  // ... work; the scope's identity is known at construction ...
  if (failed) { scope.setResult(static_cast<uint8_t>(Result::NoCapacity)); return false; }
}
```

`setResult()` may be called at any point before the scope destructs; the destructor reads it. There is
also a reference form for sites whose result is a single variable
(`PROFILE_SCOPE_LONG_RESULT(ProfileId::X, result)`), which reads that variable at exit. Duration is
recorded correctly whether or not a result was ever supplied; a scope with no result reports "none".

No cross-domain outcome enum is introduced: each site passes its own existing enum value. This keeps
the profiler from becoming the place where outcome semantics drift (G2 §3).

### D8 — Bounded commands at depth zero, with a cursor, a sequence horizon and a terminator

`!PROFILE_STATS`, `!PROFILE_RING`, `!PROFILE_RESET`, added beside `!CAP_IDENTITY_STATS` in
`StorageManagerHitlSerial.cpp`.

- **Every profile command runs only at profiler depth 0.** A command received while any scope is
  active is refused with a short `profile_cmd,refused,depth=<n>` row and changes nothing; the host
  retries later. This is stricter than suppressing the record path and removes the contradiction
  where an enclosing scope could breach *because of* the dump.
- **Stats** emits at most **16** rows per request; **ring** at most **32**. Each ends with a
  terminator row carrying `count`, `dropped`, `resume` and, for the ring, the pinned
  `range_start`/`range_end` sequence horizon.
- **Ring horizon:** the first batch of a ring dump session pins `endSeq = latest seq` and
  `startSeq = max(oldest available, endSeq − capacity + 1)`. Only that range is drained; breaches
  arriving mid-session belong to the next session and are never interleaved or duplicated. If part
  of the pinned range is overwritten before it is read, the terminator reports the sequence gap
  explicitly instead of skipping it.
- **Stats horizon:** the identity set is compile-time fixed, so a cursor over ids is stable; the
  values may still move between batches, and the dump states that it is a multi-batch snapshot.
- Each dump has its own persisted cursor. A request with `resume=1` means "ask again"; a new command
  of the same kind continues, and a reset clears both cursors.
- **Reset** clears the table, ring, overwrite count and both cursors and nothing else; it is subject
  to the same depth-0 gate.
- Rows are direct-first/ring-fallback (DEC-228 K1) and not transport-gated (DEC-228 K4), but the
  batch bound — not the transport state — is what keeps them from stalling the loop.

Rows:

```text
#CAP,<us>,DIAG,profile_boot,source=<0|1>,cpu_hz=,samples=,min_cycles=,avg_cycles=,min_us_x100=,avg_us_x100=,ids=,ring=,depth=
#CAP,<us>,DIAG,profile_stat,<id>,<name>,calls=,avg_us=,avg_self_us=,max_us=,last_us=,over=,ref=,order=,last_result=
#CAP,<us>,DIAG,profile_stat,end,count=,resume=,multi_batch=<0|1>
#CAP,<us>,DIAG,profile_ring,<seq>,t_us=,id=,name=,parent=,dur_us=,trk=,slot=,result=,src=
#CAP,<us>,DIAG,profile_ring,end,count=,dropped=,resume=,range_start=,range_end=,gap=
#CAP,<us>,DIAG,profile_reset,<ids>
#CAP,<us>,DIAG,profile_cmd,refused,depth=<n>
```

Cycles are converted to microseconds **at dump time only**. The boot row is the only unsolicited
profile row. It is emitted once per boot **including `source=0`**, so a capture always shows whether
the profiler was armed.

### D9 — Registration in the classifier source list; the header list is not grown

`DIAG,profile_` is added to `isTierATextLine` in `DebugSessionCapture.cpp`. `CaptureLineTier::
isTierALine` in the header is deliberately unchanged: its literals are instantiated per including TU
and one more was measured to snap DTCM by 4,096 B. Because `isTierATextLine` consults the header list
first and then the `.cpp` list, a `.cpp`-only prefix is Tier-A for the ring — the predicate that
matters.

Pinned three ways: `test_capture_ring` (the real predicate and the 47-byte head read),
`test_capture_line_tier` (the header list's unchanged behaviour), and
`scripts/check_marker_contract.py --audit`, which parses both lists and reports FIT.

Names and row formats live in `src/Utils/ProfileDiagRows.cpp` with one linker line
(`*ProfileDiagRows.cpp.o(.rodata.str* .rodata.*.str*)`), following the `StopDiagRows.cpp` precedent.

*Alternative:* numeric-only IDs symbolized by a host script — rejected; it moves a firmware-debug
step into a second tool. The numeric ID still leads every row, so a script can still parse it.

### D10 — Build gating is a dedicated environment, and absence is total

`RUNTIME_PROFILE_SCOPES=1` is defined only by the new
`teensy41-capture-serial-profile` env, which extends `teensy41-capture-serial`. With the flag
undefined, the scope macros expand to nothing, the table/ring TU compiles to nothing, and the
commands are not registered. The default and `-no-grain` images must link to the same RAM1, ITCM and
boot behavior as before — a gate, not a claim.

### D11 — Instrument the work body, never the poll entry

Several candidate owners are called every loop and usually return without work
(`stepPlaybackViewMaintenance`, `processDeferredIdleMaintenance`, `DisplayManager::update`,
`processDeferredSaveState`, the load step, `startOverdubbing`'s preflight). A scope around the poll
would report millions of near-zero calls and hide the one expensive body; it would also pay the
scope cost on every idle turn.

Rule: the scope goes **after the cheap no-work/admission gate**, on the body that does the work, and
the identity is named for that body (`PlaybackViewService_grain`, `IdleOwner_unit`,
`Display_frame_build`, `SaveWorkItem_step`, `LoadStep`, `OverdubStart_body`). A site that cannot be
split this way is not instrumented in this change.

### D12 — Proposed sites, variant and ceiling

| Identity (proposed) | Entry point | Variant | Ceiling | Independent guarantee / why long |
|---|---|---|---|---|
| `RecordStart_edge` | `Track::startRecording` body | long | COLD 25 ms | composes allocation + prewarm; unbounded composition |
| `PlaybackPrewarm_slot` | `Track::prewarmPlaybackForSlot` body | long | COLD 25 ms | composes seven reserves; no bound |
| `PlaybackPrewarm_views` | `LoopPlaybackRuntime::prewarmViews` body | long | COLD 25 ms | pool allocation loops, unbounded count |
| `PlaybackWindow_build` | `Track::ensurePlaybackMergedMidiEventsBuilt` body | long | FRAME 2 ms | window-shaped but **content-sized**: no hard event cap, so no short guarantee |
| `PlaybackViewService_grain` | `Track::stepPlaybackViewMaintenance` admitted grain | short | DEFERRED 5 ms | one declared grain, ≤ 64 inspected units (row 27) |
| `SnapshotPublish_step` | `Loop::stepPendingSnapshotPublish` admitted step | short | DEFERRED 5 ms | one bar + ≤ 8 membership pages of 16 refs (row 21) |
| `SaveWorkItem_step` | deferred-save work-item step | long | DEFERRED 5 ms | SD write |
| `IdleOwner_unit` | `Track::processDeferredIdleMaintenance` admitted unit body | long | `kOwnerWorkBudgetUs` | composes several children |
| `DisplayBase_gather` | `DisplayManager::buildCommittedDisplayBase` | long | FRAME 2 ms | window-capped at 16 bars but **content-sized** inside it; no hard row cap |
| `Display_frame_build` | `DisplayManager::update` frame body after the phase/dirty gate | short | FRAME 2 ms | fixed frame size, no I/O (row 4) |
| `LoadStep` | deferred `LoadLoopJob` step | long | DEFERRED 5 ms | SD read |
| `OverdubStart_body` | `Track::startOverdubbing` body after the refusal admission | long | COLD 25 ms | composes prewarm/allocation |

**Three short scopes** with a structural guarantee (a declared unit/page/frame cap), **nine long
scopes**. The table is the review artifact for D3: a site that cannot state a *hard* bound that keeps
it below 7.16 s moves to the long column, and every long site also states its `< 71.6 min` reason
(D3), which for this table is uniformly "synchronous main-loop call, no unbounded wait".

`PlaybackWindow_build` and `DisplayBase_gather` were proposed as short in the previous revision on a
window argument alone. A window bounds the *span*, not the row/event count inside it, so neither has
a hard 7.16 s guarantee and both are long here.

### D13 — Native testability at the observation boundary only

The record path reads both clocks through one tiny shim pair (`profileCyclesNow()`,
`profileMicrosNow()`); native fixtures advance fake clocks, exactly as the ingress-cost fixtures do.
Row formatting is tested through a text sink (the `DebugSessionCapture` pattern). No production
branch is added for tests; no state machine is copied.

## Execution contract (G2 packets)

Each slice is independently buildable and reviewable. Evidence lives in `evidence/<date>-<slice>.md`
and is distinguished as specified / fixture / native / device.

### Slice 1 — runtime core, short and long primitives, aggregate and ring

- **Owner / entry:** `RuntimeProfiler` (`include/Utils/RuntimeProfiler.h`,
  `src/Utils/RuntimeProfiler.cpp`). No production caller.
- **Preconditions:** profile env exists; `DECISION_REVIEW` and the report-only `DEC-###` are
  appended; `CURRENT_WORK.md` names the change.
- **Mutation:** identity enum + capacity asserts; `DMAMEM` table/ring/stack; both primitives; refusal
  and order-fault guards; boot calibration (128–256 samples, min + average); macros that compile to
  nothing without the flag.
- **Postconditions:** with the flag on and no sites, the table is zero, the boot row reports source
  availability and calibration, and a synthetic identity records one call per scope.
- **Must preserve:** default/no-grain link figures; `HotPathTelemetry`'s output (untouched by
  construction).
- **Non-goals:** any production site, any serial command.
- **Oracle:** short-exact vs long-coarse on a forced multi-period operation (fake clock), inclusive/self
  split, order fault, depth overflow, ceiling entry, ring overwrite + `seq` gap, refusal counters.
- **Validation:** `test_runtime_profiler`; target build of the profile env SUCCESS at RAM1 `96,192`;
  default env link figures unchanged.

### Slice 2 — bounded commands, Tier-A registration, host decoder

- **Owner / entry:** `StorageManagerHitlSerial.cpp` command table +
  `DebugSessionCapture::isTierATextLine` + `ProfileDiagRows.cpp` + `scripts/profile_report.py`.
- **Preconditions:** slice 1 landed; a synthetic identity exists for fixtures.
- **Mutation:** three commands with independent cursors, batch maxima (16/32), terminators with
  `resume`, a pinned ring sequence horizon, and the depth-zero gate for every command; Tier-A
  registration; carved formats; the decoder.
- **Postconditions:** repeated requests drain a full table/ring in batches; a command issued inside an
  instrumented scope is refused with its depth and changes nothing; a breach arriving mid-dump is
  excluded from the pinned range.
- **Must preserve:** every existing serial command, `!CAP_IDENTITY_STATS`, `!TRACE_DRAIN`.
- **Oracle:** a table of N identities drains in ⌈N/16⌉ requests with a correct `resume` sequence and
  no row loss; a ring with M records drains in ⌈M/32⌉ within its pinned range; a command at depth > 0
  leaves the table, ring and cursors byte-identical.
- **Validation:** `test_runtime_profiler` row/sink cases; `test_capture_ring` and
  `test_capture_line_tier` boundaries; `check_marker_contract.py --audit` exit 0;
  `scripts/test_profile_report.py` (complete, evicted, missing-rows, `seq` gap); target build.

### Slice 3 — the DEC-230 vertical proof

- **Owner / entry:** `Track::startRecording`, `Track::prewarmPlaybackForSlot`,
  `LoopPlaybackRuntime::prewarmViews` — three identities, nothing else.
- **Preconditions:** slices 1–2 landed; the `midifile-chain-*` rig and a 20-bar take.
- **Mutation:** three long scopes; `prewarmViews` passes its existing `bool` via the optional result.
- **Postconditions:** the first Record admission produces over-ceiling ring records for
  `RecordStart_edge`, `PlaybackPrewarm_slot` and `PlaybackPrewarm_views`; the restamp produces none.
- **Must preserve:** `capturePrewarmSplit` and its numbers (equivalence proof only); `okmask`
  semantics; the Record edge's return path.
- **Oracle:** the ring must attribute the first admission to `PlaybackPrewarm_views` at the same
  order as DEC-230's `views=270,842 us` (within the ring's microsecond rounding), and the restamp must
  be clean for all three; `midi_gap`/`late_on` in the record window must not regress beyond the DEC-230
  sample.
- **Non-goals:** the DEC-230 repair; any other site.
- **Validation:** focused native suites for the three owners unchanged; profile-env build; one device
  run; evidence records ring rows, split rows and the 5 s telemetry side by side.

### Slice 4 — the remaining nine bodies, coverage and overhead A/B

- **Owner / entry:** `PlaybackWindow_build`, `PlaybackViewService_grain`, `SnapshotPublish_step`,
  `SaveWorkItem_step`, `IdleOwner_unit`, `DisplayBase_gather`, `Display_frame_build`, `LoadStep`,
  `OverdubStart_body`.
- **Preconditions:** slice 3 accepted on device.
- **Mutation:** nine scopes per D11/D12; no control-flow edits.
- **Postconditions:** the aggregate dump shows at least one identity per declared domain after a run
  that records, overdubs, plays, saves and loads.
- **Overhead proof:** paired device runs on the profile image and the pre-change default image with
  the same stimulus. The boot calibration gives min/average cycles per scope; the 5 s `midi_gap`/
  `late_on` maxima and `win_gap_overrun` count must not worsen beyond the recorded tolerance
  (proposed: no new overrun, maxima within the run-to-run spread of the baseline pair). A site that
  breaks the tolerance is removed and recorded.
- **Validation:** full native suite; both builds; two device runs; closeout docs.

## Risks / Trade-offs

- **Probe effect changes what it measures.** → No serial/alloc on the path; calibration reports
  min+average over 128–256 samples; paired A/B with a pre-declared tolerance; a site whose overhead
  approaches its class is removed by decision.
- **A short scope can still alias after a wrap.** → Short scopes require a recorded structural
  guarantee; everything without one uses the long primitive. This is a review gate, and the long
  primitive is the fallback rather than a promise that the short one is always safe.
- **A dump becomes the next stall.** → Batch maxima 16/32, independent cursors, a pinned ring
  sequence horizon, and a depth-0 gate so a dump never runs inside a live scope. The dump's own cost
  is not profiled, so it cannot create the breaches it reports.
- **Reset under a live scope corrupts nesting.** → All three commands are refused at depth > 0 with a
  named reason and change nothing.
- **A long scope aliases after 71 minutes.** → Every long site records the structural reason its
  operation cannot reach one `micros()` period; a site that cannot state it needs a 64-bit clock,
  which this capability does not provide.
- **Duplicate access paths: `HotPathTelemetry` and the profiler.** → Deliberate and bounded: separate
  facilities, separate gating, no shared identities, and consolidation is an explicit follow-up
  change rather than an unstated merge.
- **`parentId` is mistaken for a call tree.** → The dump prints it as "immediate enclosing profiled
  scope" only; the decoder has no chain view and the spec forbids claiming one.
- **RAM1 snap from new literals.** → Names/formats in a carved FLASH TU; classifier prefix in the
  `.cpp` list only; `check_ram1_budget.py` gates every build; RAM2-only data.
- **Ring owned by ordinary Tier-A traffic (DEC-218 lesson).** → Only over-ceiling calls enter the
  ring; no per-call or per-turn row; dumps are on demand; the boot row is once per boot.
- **Scope creep into the DEC-230 repair.** → `prewarmViews` stays unbounded here; slice 3 is
  measurement only and the repair consumes its evidence.

## Migration / rollback

- No data migration, no SD format change, no persistent state.
- Rollback is removing `-D RUNTIME_PROFILE_SCOPES=1`: the layer compiles out, the commands disappear
  and the default images are unaffected.
- If a site proves too hot or its guarantee fails review, delete that one scope; identities are
  append-only, so removing one does not renumber the rest.
- If the long primitive's coarse path proves unusable on the target, its sites revert to short scopes
  only when each has a guarantee; otherwise the site is dropped (recorded) rather than aliased.

## Open questions

- Exact ring capacity (256 → 6 KB) and batch maxima (16/32) — tunable after the first two real
  captures without changing the contract.
- Whether the calibration should also report a nested parent+child pair to expose stack cost;
  deferred until the first calibration numbers exist.
