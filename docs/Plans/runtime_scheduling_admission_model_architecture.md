# Runtime Scheduling — Timing Telemetry Contract and Admission Prerequisites

**Status:** Contract stable. S0 telemetry **shipped** (observation only); S0b–S0e **attributed**; RC-K1–K3 / RC-L1 **device-verified**. Owner-Boundary Gate **not complete**. Interval reservation **not authorized**.  
**Date:** 2026-08-13  
**Decision:** Do not implement interval reservation or add extra `handleMidiInput()` call sites until the Owner-Boundary Gate has removed or bounded pathological owners and the resulting MIDI Input Gap and path durations are measured.  
**Parent:** [`realtime_incremental_work_capture_overdub_architecture.md`](realtime_incremental_work_capture_overdub_architecture.md)  
**Roadmap:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](runtime_scheduling_owner_boundary_admission_refinement.md)  
**Investigation log:** [`archive/refinements/runtime_scheduling_timing_envelope_investigation.md`](archive/refinements/runtime_scheduling_timing_envelope_investigation.md)  
**Evidence:** [`115913`](../../captures/session_20260812_115913.log) · [`122003`](../../captures/session_20260812_122003.log) · [`104104`](../../captures/session_20260812_104104.log) · [`225803`](../../captures/session_20260812_225803.log) · [`112104`](../../captures/session_20260813_112104.log)

This document is the **stable contract**. Chronological capture narrative lives in the investigation log. Implementation stages, design gates, and commit slices live in the roadmap. Neither this file nor the investigation log authorizes firmware.

---

## 1. Decision summary

The architectural goal is valid:

> **No lower-priority operation may cause the gap between timing-critical MIDI Input handling opportunities to exceed an evidence-based real-time bound.**

That bound exists to protect **MIDI deadline correctness**: every required note-on, note-off, and MIDI clock is emitted on or before its scheduled deadline.

```text
lateness_us = sendMidiEvent() − scheduled_deadline
late_event_count == 0
max_lateness_us <= 0
```

Score on, off, and clock independently. MIDI Input Gap and `handleMidiInput()` duration diagnose **contention** (why a deadline was lost). They do **not** prove events were on time: a small gap can still miss a send, and a large gap can still deliver every event on time.

Per-event lateness is the same measurement specified for playback gather Stage 1 ([`playback_gather_lcr_consume_enhancement.md`](playback_gather_lcr_consume_enhancement.md)). One hook owner (`RuntimeTimingTelemetry` / `#CAP` next to `sendMidiEvent`). Do not invent a second telemetry path. Those hooks do **not** authorize interval reservation, extra `handleMidiInput()` call sites, or playback-horizon firmware from this contract.

The previously proposed stateless:

```text
RuntimeWorkBudget::beginServiceInterval()
RuntimeWorkBudget::admit(WorkClass)
RuntimeWorkBudget::exhausted()
```

model is **not sufficient** to guarantee that contract.

Reasons:

- `admit()` has no operation cost, reservation, or enforcement mechanism.
- Owner-level gating does not bound nested work.
- Several operations contain allocation, full scans, callbacks, or variable-cost work.
- `MidiHandler::handleMidiInput()` itself can be substantial and is part of measured timing telemetry, not a zero-cost handling point.
- Clock dispatch synchronously enters `TrackManager::updateAllTracks()`.
- Internal-clock ISR execution is not serialized with main-loop work by a shared budget.
- Transport transitions can synchronously perform substantial work.
- USB-host output can introduce blocking and callback re-entry.
- The proposed 2 ms target / 5 ms ceiling is not established by measurement.

**Therefore:**

Do not implement `RuntimeWorkBudget` interval reservation, do not add extra `handleMidiInput()` call sites, and do not introduce a **new** global scheduler until the Owner-Boundary Gate exits.

S0 identifies pathological owners; it does not make them admissible. Before interval reservation, each operation must either be removed from the timing-critical path or converted into a bounded, resumable unit with an enforced upper bound.

This is **not** a prohibition on the shipped [`DeferredJobScheduler`](../../openspec/specs/deferred-job-scheduler/) (DEC-027). That owner remains the non-realtime load-frame coordinator. Interval reservation, if introduced later, coordinates existing owners at `main.cpp::loop()` and does not replace `DeferredJobScheduler`, `StorageManager`, `Track`, `Loop`, or `DisplayManager`.

---

## 2. Vocabulary

Do not collapse these into one “admission” noun.

| Term | Meaning | Not this |
|------|---------|----------|
| **Persist admission** | Queue persistence intent: `StorageManager::admitLoopPersist`, `admitLoopUndoHistory`, `requestDeferredSaveState`, `PersistenceWorkQueue::admitWork` ([NAMING.md](../Authority/NAMING.md) `request*` / `admit*`; DEC-018 heap-gated **dispatch**) | CPU-time reservation |
| **Runtime admission** | Future coordinator of which bounded runtime units may execute between `handleMidiInput()` entries | Persist `admit*`; a “MIDI service” owner |
| **Interval reservation** | Future shared remaining-µs check at a `handleMidiInput()` entry before running one bounded owner unit | Persist `admit*` |
| **Deferred / scheduled work** | Resumable owner jobs drained later (`processDeferredSaveState`, `DeferredJobScheduler::runFrame`) | Synchronous algorithms |
| **MIDI Input Gap (MIG)** | Wall-clock time between consecutive `handleMidiInput()` entries (`DIAG,midi_gap`; historical captures used `DIAG,msi`) | Time spent *inside* `handleMidiInput()`; proof that MIDI was on time |
| **`handleMidiInput()` duration** | Time spent inside `handleMidiInput()` (`DIAG,midi_input`; historical captures used `DIAG,midisvc`) | MIDI Input Gap |
| **MIDI deadline lateness** | `lateness_us` per required note-on, note-off, and MIDI clock at `sendMidiEvent()` | MIDI Input Gap; `clockrate` half-tempo proxy |
| **Timing-critical** | Architectural property of MIDI Input | An owner name |

---

## 3. Architectural objective

The system uses a cooperative single-threaded `loop()` with timing-critical MIDI processing interleaved with display, persistence, maintenance, reclaim, and control-surface work.

> Loop length may increase total background work and completion time, but must not increase the maximum real-time blocking interval of one timing-critical scheduling opportunity.

This is stronger than a local budget, a display bailout, an extra MIDI drain, or a passing 100-bar HITL.

### 3.1 Work-quantum principle

The amount of work performed in one timing-critical scheduling slice must depend on the configured work quantum, not on total loop length.

### 3.2 Protected `handleMidiInput()` entry principle

The scheduling interval is the MIDI Input Gap before the next timing-critical `handleMidiInput()` entry, not one `loop()` iteration. At each `handleMidiInput()` entry the runtime establishes the next deadline, reserves one complete bounded owner unit, executes it, and returns to the next `handleMidiInput()` entry.

Order:

1. timing safety;
2. timing-critical MIDI Input;
3. interval-reservation fit;
4. fairness among units that fit;
5. throughput.

Fairness must never cause an inadmissible unit to run.

### 3.3 Owner-Boundary Gate

Mandatory between S0 investigation and interval reservation:

```text
S0 investigation
      ↓
Owner-Boundary Gate
      ├─ remove/reduce synchronous pathological work
      ├─ convert remaining work to resumable units
      ├─ establish enforced unit bounds
      └─ measure the resulting owner path durations
      ↓
Interval reservation (OpenSpec + DEC required)
```

`Loop::establishOverdubSourceView` `begin_capture` is timing-critical-path-forbidden while it can synchronously occupy the CPU for seconds. Giving it a larger reservation is not an acceptable remedy. Transition-path behavior is a **design session** — see the roadmap.

Each unit is classified as:

- **structurally bounded:** code enforces a fixed maximum amount of work and storage;
- **empirically bounded:** measurements provide an engineering limit, but code does not enforce it;
- **unbounded or unknown:** scans, drains, allocates, sorts, or loops on a content-dependent condition without an enforced cap.

Only structurally bounded units, or empirically bounded units with an explicit overrun guard and failure policy, may be reserved as timing guarantees.

---

## 4. Scheduling layers (existing owners)

Interval reservation is a future coordinator at the existing integration point. It is **not** a domain owner (invariant O).

```text
main.cpp::loop()                 runtime service-order / scheduling integration
    ├─ MidiHandler::handleMidiInput     protected MIDI Input handling point
    ├─ ClockManager / TrackManager      clock + track tick
    ├─ Track / Loop                     capture, passes, source view
    ├─ DisplayManager                   projection / paint
    ├─ DeferredJobScheduler::runFrame   non-realtime load frames (DEC-027)
    ├─ StorageManager                   persist admission + deferred save FSM
    └─ owner budgets                    PersistenceBudget, LoadLoopBudget,
                                        display resolve bailout (local only)
```

Local budgets remain necessary. They do not form a collective MIDI Input Gap contract.

Do **not** create a new Manager for scheduling. Do not move Clock into an ISR as part of this work. Do not treat persist `admit*` as interval reservation.

---

## 5. Current scheduling model

```text
handleMidiInput()
    ↓
state / buttons / faders / control surface / LEDs
    ↓
capture flush
    ↓
idle maintenance × tracks
    ↓
load + display
    ↓
handleMidiInput()          ← RECORD/OVERDUB only (RC-C C)
    ↓
fader motor sync
    ↓
memory monitor / reclaim
    ↓
persistence
    ↓
handleMidiInput()
    ↓
HITL serial
```

Verify exact call ordering against [`src/main.cpp`](../../src/main.cpp). Additional owners consume the same MIDI Input Gap and must appear in any inventory: `processDroidUsbHostOutbound` (twice), `maybeUpdateDisplayForNoteEditSelection`, `looper.update()`, advisory reclaim inside `runDeferredLoadAndDisplayFrame`, and `midiButtonManager.update()` after the third `handleMidiInput()` call.

Clock property ([`ClockManager::onMidiClockPulse`](../../src/ClockManager.cpp)): one processed Clock message advances tick by 8 with no resync. Unprocessed Clock bytes are permanently lost; sustained 50% loss presents as half tempo.

---

## 6. Timing-critical paths

### 6.1 MIDI Input

MIDI Input is timing-critical. `MidiHandler` owns I/O ingress and dispatch. `MidiHandler::handleMidiInput()` may process USB-device, DIN, and USB-host input, MIDI dispatch, Clock dispatch, channel messages, transport actions, and button-triggered actions. Its duration is part of measured timing telemetry. There is no “MIDI service” owner.

### 6.2 Clock dispatch

```text
MIDI Clock → ClockManager::onMidiClockPulse()
         → currentTick += TICKS_PER_CLOCK
         → TrackManager::updateAllTracks(currentTick)
```

`updateAllTracks()` is synchronous and content-dependent (playback emission, cache construction, slot commits, note output, stop processing).

### 6.3 Internal clock (ISR)

`ClockManager::updateInternalClock()` can invoke `TrackManager::updateAllTracks()` from `IntervalTimer` when the device is the internal master. External-clock slave returns early. A future interval-reservation contract must account for ISR overlap with main-loop work; it must not claim serialization the runtime does not provide.

### 6.4 Transport transitions

RECORDING → `stopRecording()` → PLAYING and OVERDUBBING → `stopOverdubbing()` → PLAYING are measured, not assumed free. Alternate paths: `stopRecordingToStopped`, `stopOverdubbingToStopped`, STOPPED_RECORDING → OVERDUBBING, NOTE_EDIT fold via `handleNoteEditFold`.

`startOverdubbing()` → `Loop::markDisplayCachesStale()` (`dirtyBars.assign(totalBars, 1)`) and `establishOverdubSourceView` make PLAYING → OVERDUBBING not constant-cost.

### 6.5 Button and callback re-entry

`MidiButtonProcessor::processPendingPresses()` can trigger record/overdub stop, undo, redo, load, and save from MIDI dispatch. Loop-edit immediate seek in `BarStepButtonHandler` can call `ClockManager::setCurrentTick()` → `updateAllTracks()`. PLAYING bar-step uses `queueBarPlaybackStart` rather than a synchronous full tick update. USB-host callbacks do not share the USB/DIN batch reorder contract.

---

## 7. Background work inventory

| Owner / operation | Current characteristic | Timing concern |
|-------------------|------------------------|----------------|
| `processDeferredIdleMaintenance()` × tracks | Per-track local budgets | Budgets multiply |
| `rebuildVisualCacheIdleSlice()` | Bar-quantized | Cost contains loop/cache terms |
| `materializeEditViewFromPasses()` | Potential full-loop | Unbounded when started |
| `ensureVisualCacheBuilt()` | Potential full-loop | Unbounded |
| `validateAndCleanupMidiEvents()` | Full-loop merge | Unbounded once started |
| display update | Frame-sized | Full frame not capped |
| display resolve | 5000 µs measured bailout | Bailout does not make computation bounded |
| load jobs | Local µs budgets | Included in the shared MIDI Input Gap |
| persistence | Incremental slices | Local budget; payload can still be workspace-wide |
| fader motor sync / LED refresh | No shared cap | Potentially unbounded |
| HITL serial | Drains available bytes | Unbounded |
| derived-cache / disabled-pass reclaim | Can scan all tracks/slots/passes | Potentially unbounded |
| `establishOverdubSourceView` | Full gather + reconstruct | Synchronous on overdub entry |
| `processDroidUsbHostOutbound` | Pacing + callback re-entry | Blocking |

`PerformanceMonitor::beginLoop` / `endLoop` are commented out in `main.cpp`.

---

## 8. Why local budgets are insufficient

A local budget such as display 5000 µs, persistence 300 µs, load 1200 µs does **not** establish `total interval ≤ 5000 µs` because the operations are sequentially additive. Eight track maintenance calls multiply a local budget. A bounded unit can trigger an unbounded nested operation.

**Local budgets are necessary but not sufficient for collective timing safety.**

---

## 9. Non-preemptible operation rule

Interval reservation cannot make an already-running operation safe. If an operation can take 20 µs sometimes and 20 ms in the worst case, then `admit(operation)` does not establish a 2 ms MIDI Input Gap ceiling.

The operation must first be inherently bounded, made resumable, assigned a measured/static worst-case cost, or excluded from timing-critical intervals.

> No operation may be reserved solely because its caller has remaining budget. Its own worst-case execution cost must also be known or structurally bounded.

`begin_capture` is not an admissible unit while `Loop::establishOverdubSourceView` can perform multi-second synchronous source-view construction.

---

## 10. Display, visual cache, and overdub mutation

`Loop::rebuildVisualCacheIdleSlice()` is resumable, but a “4-bar slice” can include a full chunk scan and a full display-note vector erase-remove. Slice cost must be proportional to the declared quantum and the data it actually processes.

`LCD::DISPLAY_UPDATE_INTERVAL = 30 ms` controls paint opportunity, not computation. `Diagnostics::kDisplayResolveBudgetMicros = 5000` is a measured bailout. Never rebuild everything synchronously and rely on a timeout.

**Absolute full-rebuild invariant:** no synchronous full-loop visual reconstruction on a timing-critical RECORD or OVERDUB path.

**Overdub delta-proportional invariant:** an overdub mutation must not reconstruct the pre-existing loop. Test with the approximately 100-bar + two-overdub scenario.

Display completeness under a bounded resolve is a **product policy** (stale frame vs resumable resolve vs always-complete). It is not settled by this contract; the roadmap owns the design gate.

---

## 11. Persistence, reclaim, serial, control surface

Persistence already has incremental infrastructure: sealed chunks, `PersistenceQueue`, mid-pass persistence, local budgets, deferred work. Normal STOP is cooperative. STOP must not become a catch-up point for loop-sized work.

**Outside this architecture change:** Clear / SavedSet SyncDrain, Stage 5b, crash-recovery redesign, new persistence owner, parallel persistence FSM.

`reclaimUnreferencedDisabledPasses()` is not resumable. Pressure must become bounded reclaim unit → continuation → next opportunity.

`processHitlSerialCommands()` drains available serial data. Future timing-safe implementation must count-bound, time-bound, or exclude during timing-critical capture.

`ControlSurfaceManager::processDeferredFaderMotorSync()` and `TrackManager::updateMidiLedsDeferred()` do not participate in a shared real-time contract.

---

## 12. Proposed collective scheduling contract (design target — deferred)

```text
handleMidiInput() duration + all reserved non-MIDI Input cost + nested non-preemptible cost
        ≤ evidence-based MIDI Input Gap ceiling
```

The numerical ceiling is not selected before measurement. 2 ms / 5 ms are **withdrawn** as an established contract.

| Clause | Statement |
|--------|-----------|
| C1 MIDI priority | Timing-critical MIDI processing has highest priority |
| C2 Bounded interval | Evidence-based ceiling from measured MIDI Input Gap and path durations |
| C3 Collective reservation | Interval reservation with a structurally bounded or explicitly enforced per-unit cost; not persist `admit(WorkClass)` |
| C4 Quantum proportionality | Reserved unit costs O(quantum) |
| C5 Pending is normal | Denied work stays pending; denial never loses work |
| C6 Reuse derived state | Existing materialized state usable while background work incomplete |
| C7 Symmetric capture | RECORD, OVERDUB, post-record PLAYING same policy; extra PLAYING drain is mitigation only |
| C8 STOP cooperative | STOP does not become catch-up point |
| C9 Fairness | Equal-priority classes must not starve indefinitely; fairness never overrides timing safety or reservation fit |
| C10 Transport transitions | Part of measured timing telemetry; not assumed free |
| C11 Callback/re-entry | Nested callbacks and playback re-entry in cost model |
| C12 Evidence-based ceiling | Final MIDI Input Gap ceiling from device evidence |
| C13 Dual execution context | ISR `updateInternalClock` overlap is part of the cost model |
| C14 Observation non-loss | Tier-A DIAG must egress or the timing telemetry is invalid |
| C15 MIDI deadline correctness | Lateness hooks exist: `late_event_count == 0` and `max_lateness_us <= 0` for note-on, note-off, and clock (`DIAG,late_*`). A passing MIDI Input Gap with late sends is not a pass |

If interval reservation is required later, minimum shared state is: interval deadline/remaining budget and generation; per-class fairness cursor; reserved unit cost; re-entry/ISR depth. Owner continuation cursors remain owner state.

Conceptual API: `canRun(unitCost) → reserve(unitCost) → run bounded unit` — not `admit(class) → do arbitrary work`.

Allocation (`assign`, `resize`, `push_back`, sort) is part of unit cost. O(loop), O(chunks), O(visual notes), O(all passes) are forbidden as one non-preemptible timing-critical unit.

---

## 13. S0 telemetry (shipped, observation only)

`RuntimeTimingTelemetry` emits Tier-A `DIAG` windows every 5 s from `main.cpp::loop()`. No scheduling decision reads these values. Native: `test_runtime_timing_telemetry`. `overCount` uses an observational 5000 µs soft ceiling for counting only.

| Metric | Definition | DIAG tag |
|--------|------------|----------|
| **MIDI Input Gap (MIG)** | Time between consecutive `handleMidiInput()` entries | `midi_gap` (historical captures: `msi`) |
| **`handleMidiInput()` duration** | Time spent inside `handleMidiInput()` | `midi_input` (historical captures: `midisvc`) |

Playback gather Stage 1 hooks exist on this owner: 5 s `DIAG,late_on` / `late_off` / `late_clk` plus `DIAG,late_event` / `DIAG,playback_build` one-shots. C15 still needs a device window (`late_event_count == 0`, `max_lateness_us <= 0`). Until Stage 3 scores those lines, `clockrate` is not the musical pass criterion.

S0b–S0e attributed PLAYING/OVERDUB `handleMidiInput()` duration (`midisvc` in those captures) as `usbdev` → `usbdisp` → `usbnote` → `notechg`. RC-K1–K3 moved reconstruct off the note-off path (`noterecon` 0 on that path after [`225803`](../../captures/session_20260812_225803.log)). `DIAG,noterecon` remains a probe; after RC-K3 it reads 0 on the production note-off path and must not be treated as an active cost owner.

Instrumentation must not alter dispatch order, add extra `handleMidiInput()` call sites, alter display cadence, change persist admission, or change transport behaviour.

---

## 14. Evidence and status index

Chronological narrative: [investigation log](archive/refinements/runtime_scheduling_timing_envelope_investigation.md).

| Item | Status | Evidence / notes |
|------|--------|------------------|
| S0 probes | **Shipped** | `RuntimeTimingTelemetry` |
| S0b `usbdev` | **Attributed** | [`193645`](../../captures/session_20260812_193645.log) |
| S0c `usbdisp` | **Attributed** | [`195240`](../../captures/session_20260812_195240.log) |
| S0d `usbnote` | **Attributed** | [`200452`](../../captures/session_20260812_200452.log) |
| S0e `notechg` | **Attributed** | [`204221`](../../captures/session_20260812_204221.log) — `noterecon` 177 ms + `notepair` 98 ms |
| RC-K1–K3 / RC-L1 | **Device-verified** | [`223033`](../../captures/session_20260812_223033.log), [`225803`](../../captures/session_20260812_225803.log) |
| RC-S0a Tier-A parse | **Fixed** | `CaptureLineTier::isTierALine` |
| RC-S0b pool walk | **Fixed** | runtime `logStatus` no longer walks PSRAM |
| RC-S0c Tier-A transmit | **Open** | RECORD windows and overflow still drop DIAG timing lines |
| RC-D OLED cadence | **Shipped** | [`155132`](../../captures/session_20260812_155132.log) |
| RC-E/F/G display | **Shipped** (residuals remain) | see investigation log §31g |
| RC-J STOPPED persistence stall | **Open** — Owner-Boundary, not S0b | Distinct from PLAYING dump |
| PLAYING overdub-stop dump | **Open** — deferred `LoopUndoHistory` bundle | [`105505`](../../captures/session_20260813_105505.log), [`112104`](../../captures/session_20260813_112104.log) |
| `begin_capture` | **Open** — sibling source-view plan | 77–83 ms clean ([`225803`](../../captures/session_20260812_225803.log)); multi-second later entries ([`105505`](../../captures/session_20260813_105505.log), [`112104`](../../captures/session_20260813_112104.log)) |
| Interval reservation | **Not authorized** | Requires Owner-Boundary Gate + OpenSpec + DEC |

S0 exit is **partial**: MIDI-path attribution is complete; telemetry trust (Tier-A delivery, remainder coverage, post-stop windows) and an evidence-based MIDI Input Gap ceiling are not.

---

## 15. Relationship to RC-C

RC-C remains narrow. Shipped: A (MIDI ordering), B (incremental display), C (optional extra MIDI drain during RECORD/OVERDUB only).

[`122003`](../../captures/session_20260812_122003.log) proved that a second visual-cache slice with no `handleMidiInput()` call between slices lost Clock after record stop (PLAYING has fewer `handleMidiInput()` call sites than RECORD/OVERDUB). Dual-slice was reverted. Extra PLAYING drain is a **mitigation candidate**, not proof of C2.

---

## 16. What this architecture does not propose

This document does **not** authorize: moving MIDI Clock to an ISR; timestamping USB arrival; changing MIDI transport semantics; changing capture/Loop/Track/persistence **ownership** without the ownership-transfer protocol; a new global scheduler; replacing `DeferredJobScheduler`; parallel execution; Stage 5b SyncDrain redesign; Clear/SavedSet persistence redesign; note-edit geometry changes; broad display architecture replacement; widening `timingCriticalTrackActive` as a symptom patch.

---

## 17. Core architectural invariants

| ID | Invariant |
|----|-----------|
| A | MIDI priority — timing-critical MIDI processing has highest priority |
| B | Bounded non-preemptible work — no reserved operation with unknown or loop-sized worst-case cost |
| C | Collective reservation — sum of reserved work fits the measured protected interval |
| D | Reservation — reserve the complete next unit before execution |
| E | Pending work — denied work remains pending; denial never loses work |
| F | Resumability — work that cannot fit remains resumable |
| G | No full-loop catch-up — dirty state never forces synchronous full-loop reconstruction |
| H | Loop-length independence — longer loop increases completion time, not max blocking interval of a bounded unit |
| I | Fairness — never overrides timing safety or reservation fit |
| J | Transport transitions — part of measured timing telemetry |
| K | Callback/re-entry accounting — nested callbacks in cost model |
| L | Evidence-based ceiling — final MIDI Input Gap ceiling from device evidence |
| M | Protected `handleMidiInput()` entry — reservation protects the next MIDI Input handling opportunity, not a `loop()` iteration |
| N | Bound classification — structural bounds are guarantees; measurements alone are tuning evidence |
| O | Owner boundary — interval reservation coordinates existing owners and never becomes a domain owner |
| P | Dual execution context — ISR overlap is accounted for, not assumed serialized |
| Q | Observation non-loss — Tier-A timing telemetry must survive or measurement is invalid |
| R | MIDI deadline correctness — MIG protects the opportunity; `lateness_us` proves the send. Do not treat `midi_gap` as the musical pass criterion |

---

## 18. Immediate next action

**Do not implement interval reservation.** Follow [`runtime_scheduling_owner_boundary_admission_refinement.md`](runtime_scheduling_owner_boundary_admission_refinement.md): observation integrity, targeted closeout, then the Owner-Boundary Gate (source-view transition design, scoped persistence / DEC-024 Phase 2, remaining owner inventory).

Historical labels S1–S8 are superseded. Do not treat them as an authorized sequence.

---

## Appendix A — Codebase audit findings (B1–B16)

| ID | Finding | Owner / path |
|----|---------|--------------|
| B1 | Persist-style `admit(WorkClass)` cannot guarantee collective reservation without cost/reservation/enforcement | Proposed `RuntimeWorkBudget` |
| B2 | 8× track maintenance multiplies budgets | `main.cpp` |
| B3 | MIDI Input Gap excludes `handleMidiInput()` duration in the old design | `MidiHandler.cpp`, `ClockManager.cpp` — S0 now measures both |
| B4 | `updateInternalClock()` ISR can call `updateAllTracks()` concurrently with main loop | `ClockManager.cpp` |
| B5 | `updateAllTracks()` unbounded by content | `TrackManagerTransportTick.cpp` |
| B6 | `stopRecording()` exceeds any 2 ms / 5 ms ceiling | `TrackCaptureStopCommit.cpp` |
| B7 | `stopOverdubbing()` adds flush, display snapshot; verification now deferred | `TrackOverdubLifecycle.cpp` |
| B8 | `startOverdubbing()` → `markDisplayCachesStale()` O(loop bars) | `TrackOverdubLifecycle.cpp` |
| B9 | `rebuildVisualCacheIdleSlice()` O(C) + O(V) | `LoopVisualCache.cpp` |
| B10 | Whole-loop paths in idle maintenance when fully idle | `TrackDeferredMaintenance.cpp` |
| B11 | Critical reclaim not resumable during capture | `main.cpp` |
| B12 | Full display frame not capped by resolve bailout | `DisplayManager` |
| B13 | HITL serial unbounded drain | `StorageManagerHitlSerial.cpp` |
| B14 | Loop-edit bar-step immediate seek → `setCurrentTick()` → `updateAllTracks()`; PLAYING uses queued bar start | `BarStepButtonHandler.cpp` |
| B15 | USB-host pacing blocks; callbacks re-enter | `MidiHandler.cpp` |
| B16 | Stateless budget insufficient; minimum state in §12 | Interval-reservation design |

### Already satisfied

`Track` / `Loop` / `StorageManager` / `DisplayManager` ownership; deferred full validation on stop; persistence FSM and load budgets; REVT/visual-cache resumability; USB/DIN transport ordering (RC-C A); `DeferredJobScheduler::runFrame` for load frames (DEC-027).

### Implementation details (not blocking observation)

USB/DIN batch bounded but copies + two scans; USB-host no batch reorder; `recordMidiEvents()` backward scans; `rebuildPlaybackOrder()` full sort; `processPendingPresses()` 2048 scan; semantic `admitLoopPersist` is persist admission, not interval reservation.
