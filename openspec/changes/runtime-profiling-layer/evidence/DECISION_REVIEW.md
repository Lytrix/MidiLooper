# Historical decision review — runtime-profiling-layer

Filled from [`docs/Templates/DECISION_REVIEW.md`](../../../../docs/Templates/DECISION_REVIEW.md) on
2026-10-31 at HEAD `fec18994` (docs on top of `5ddba83e`). Task 0.2.

## Historical review

### Searched

- [x] `docs/DECISION_LOG.md`
- [x] Active OpenSpec — `openspec/changes/runtime-profiling-layer/`
- [x] `openspec/specs/`
- [x] `docs/Plans/` (non-archive)

**Search terms used:** profiler, DWT, cycle counter, CYCCNT, RAII, HotPathTelemetry,
`Diagnostics::Timing` / `DIAG_TIMING`, span timing, instrumentation, `view_prewarm_step`,
`capture_prewarm_split`.

| Term | DECISION_LOG | openspec/specs | docs/Plans |
|---|---|---|---|
| profiler | 0 | 0 | 1 (mention only) |
| DWT | 1 (DEC-128 alternatives table) | 0 | 1 |
| cycle counter / CYCCNT | 0 | 0 | 1 / 0 |
| RAII | 0 | 0 | 1 |
| HotPathTelemetry | 0 | 0 | 2 |
| Diagnostics::Timing / DIAG_TIMING | 0 | 0 | 3 |
| span timing | 0 | 0 | 0 |
| instrumentation | 7 | 1 (`note-edit-fader-feedback`, unrelated) | 27 |
| view_prewarm_step | 2 (DEC-231) | 0 | 0 |
| capture_prewarm_split | 1 (DEC-230) | 0 | 0 |

### Relevant findings

- **DEC-128 — ClockManager PIT elapsed reference.** Rejected "widen micros32/DWT" as the *clock
  elapsed reference* because casting does not resolve unobserved wraps, SysTick bookkeeping or
  CPU-rate/debug obligations. **Applies in spirit, not in scope:** the profiler is observation-only,
  never a clock reference. It honours the same objections by construction — the short scope needs a
  structural sub-period guarantee, the long scope disambiguates the wrap with the systick-extended
  `micros()`, an unavailable rate refuses, and nothing in the clock path reads profiler output.
- **DEC-043 point 4 — local owner budgets stay** and do not make an indivisible step bounded. The
  profiler's ceiling classes are report-only and add no bound (design D5).
- **DEC-214 — persistence step naming is report-only until the step is split.** Same rule: naming and
  timing a step does not bound it. `SaveWorkItem_step` follows it.
- **DEC-216** (`idle_turn` row, unit > 2 × `kOwnerWorkBudgetUs`), **DEC-230** (`capture_prewarm_split`)
  and **DEC-231** (`view_prewarm_step`, off-edge preparation) are the bespoke timers the profiler is
  meant to supersede; their retirement is staged in [RETIREMENT.md](../RETIREMENT.md), not done here.
- **DEC-228 K1/K4** — decisive rows direct-first/ring-fallback; read-only stats commands not
  transport-gated. The profile commands follow both.
- **DEC-216 / DEC-225** and `hitl_authoring_traps.md` §2 — literal-driven DTCM snaps; drive the carved
  `ProfileDiagRows.cpp` TU and the flag-gated Tier-A prefix.
- No existing decision introduces, rejects or reserves a general-purpose profiler.

### Existing reusable pattern

| Reused owner / pattern | Use |
|---|---|
| `ClockPhysicalReferencePlatform::cpuClockHzForObservation()` | Boot contract: CPU rate, `0` = unavailable. `readCycleCounterForObservation()` is **not** reused per call (it issues `dsb/isb`). |
| `MidiIngressCost` (`IngressCounterContract`, `ingressCycleIntervalValid`) | Shape of the validity contract and the fake-clock native fixtures; not its API. |
| `StopDiagRows.cpp` / `EnvelopeDistRows.cpp` + `linker/imxrt1062_t41_lcr.ld` carve lines | FLASH placement of every new name/format literal. |
| `kIngressCostTierPrefix` gating in `isTierATextLine` | Flag-gated `PROGMEM` Tier-A prefix so the default image gains no literal. |
| `!CAP_IDENTITY_STATS` in `StorageManagerHitlSerial.cpp` | Placement of the three read/reset commands. |
| `DMAMEM` diagnostic state (`DisplayManager`, `LoopContentResolution`) | RAM2 placement and the NOLOAD boot-clear obligation. |

### Reuse decision

**NO** for the timing table itself; **YES** for every surrounding pattern above.

### If NO

**Reason:** two timing facilities exist and neither fits as the extension point without becoming this
change's subject:

- `HotPathTelemetry` — five named metrics in `EXTMEM` behind `PERF_TELEMETRY` with a fixed `PERF,`
  text; no nesting, no ring, no early-exit capture, and PSRAM latency is wrong for a per-call table.
- `Diagnostics::Timing` — ten `micros()`-based identities in DTCM arrays (u32 sum/count/max) emitted
  as `DIAG,timing`/`timing_max` in every `SESSION_CAPTURE` image; no nesting, no self time, no wrap
  contract, no breach ring, and growing it adds DTCM in the default image.

Extending either would make this change responsible for an accepted facility's output format across
two build configurations before the new instrument is proven, and growing `Diagnostics::Timing`
violates the "default image unchanged" gate. The profiler is the **declared successor** of both;
consolidation is the staged follow-up (RETIREMENT.md stages 1–5), so ownership is duplicated only for
a bounded, named period.

**New constraints introduced:** profile ceilings are never execution bounds; a short scope needs a
structural guarantee recorded at the site; profiler output is never read by runtime logic; the default
and `-no-grain` images carry no profiler bytes.

**Migration required:** **NO** — no SD format, no persisted state, no stimulus change. (Row retirement
is a separate change with its own migration of consumers.)

## Site table review (task 1.1)

Checked at `fec18994` on 2026-10-31. "Pin at packet" = exact line placement is fixed in that slice's
execution packet, with the rule stated here.

| Identity | Code found | Variant holds? | Placement rule / note | Legacy timers inside |
|---|---|---|---|---|
| `RecordStart_edge` | `Track::startRecording` (`TrackCaptureInput.cpp`) | long ✓ | whole body; the not-prepared `return false` is a cheap timed exit | `capture_prewarm_split` via child |
| `PlaybackPrewarm_slot` | `Track::prewarmPlaybackForSlot` (`TrackPlaybackWindowBuild.cpp`) | long ✓ | whole body | `capture_prewarm_split` (emitted inside, `SESSION_CAPTURE`) + seven `micros()` brackets |
| `PlaybackPrewarm_views` | `LoopPlaybackRuntime::prewarmViews` (`TrackPlaybackRuntime.cpp`) | long ✓ | whole body; callers: `prewarmPlaybackForSlot` (skipped when prepared), `TrackOverdubLifecycle.cpp` | none |
| `ViewPrewarm_unit` | `runtime->stepViewPrewarm(...)` in `Track::stepViewPrewarmForSlot` | long ✓ | block around the call only, closed before `viewPrewarmStep`; callers: idle owner (`TrackDeferredMaintenance.cpp`), all-stopped path (`main.cpp`), native-only loop in `startRecording` | `view_prewarm_step` **after** the scope |
| `PlaybackWindow_build` | **free function** `ensurePlaybackMergedMidiEventsBuilt(Track&, …)` (`TrackPlaybackWindowBuild.cpp`), not a `Track` member — design table name corrected | long ✓ | whole body; callers include `Track::rebuildPlaybackOrder` (`TrackPlaybackHotPath.cpp`) — the IPSR guard reports any ISR reach | `Diagnostics::Timing` `PlaybackBuild` |
| `PlaybackViewService_grain` | `Track::stepPlaybackViewMaintenance` | **short** ✓ (row 27 grain) | after the turn's admission returns "work"; pin at slice-4 packet | verify at packet |
| `SnapshotPublish_step` | `snapshotLoop.stepPendingSnapshotPublish(...)` in `TrackDeferredMaintenance.cpp` | long ✓ | around the call only; qualification rows sit outside it | `snapshot_idle` (investigation gate) outside |
| `SaveWorkItem_step` | `StorageManager::processDeferredSaveState` (`DeferredSaveScheduler.cpp`), work item active branch | long ✓ (SD) | after the `itemActive`/queue-depth gate; pin at packet | `persist_save,step` |
| `IdleOwner_unit` | `Track::processDeferredIdleMaintenance` | long ✓ | one child unit per call, many early returns; the no-work gate is the caller's admission in `runDeferredIdleMaintenance` (`main.cpp`), so the scope is the whole body | `idle_turn` (caller side) |
| `DisplayBase_gather` | `DisplayManager::buildCommittedDisplayBase` (`DisplayNoteResolveLiveCapture.cpp`) | long ✓ | whole body | `Diagnostics::Timing` `DisplayBuild` |
| `Display_frame_phase` | `DisplayManager::update` phase `switch` | long ✓ | scope the `switch` only when `ran != Idle`; transfer phase is now DMA-polled (`pollDisplayTransfer`), so ExecutionBounds row 4's "blocking" may be stale — long is kept regardless | `HotPathTelemetry` display update + `Diagnostics::Timing` `DisplayUpdateTotal` (outside the switch) |
| `LoadStep` | `stepLoadLoopJob(deadlineUs)` (`LoadLoopJob.cpp`) | long ✓ (SD) | whole body | verify at packet |
| `OverdubStart_body` | `Track::startOverdubbing` (`TrackOverdubLifecycle.cpp`) | long ✓ | after the refusal admission; pin at packet | `HotPathTelemetry` overdub start |

**Dispatch chain (RED_TEAM R8):** `StorageManager::processHitlSerialCommands()` is called directly from
`loop()` in `main.cpp`, after `handleMidiInput()`/`midiButtonManager.update()` and outside every listed
body, so the depth-zero gate is satisfied in normal operation. ✓

**Ceiling-only justifications:** none. No short site relies on its ceiling or window span. ✓
