# Runtime scheduling — Owner-Boundary Gate and interval-reservation roadmap

**Status:** Active — documentation and gating only; firmware not authorized by this file  
**Date:** 2026-08-13  
**Kind:** refinement  
**Contract:** [`runtime_scheduling_admission_model_architecture.md`](runtime_scheduling_admission_model_architecture.md)  
**Investigation log:** [`archive/refinements/runtime_scheduling_timing_envelope_investigation.md`](archive/refinements/runtime_scheduling_timing_envelope_investigation.md)  
**Parent:** [`realtime_incremental_work_capture_overdub_architecture.md`](realtime_incremental_work_capture_overdub_architecture.md)

This is the **sole actionable roadmap** for runtime scheduling work. Historical S1–S8 labels and the Cursor export `runtime_timing_envelope_8db96333.plan.md` are superseded.

**Do not** implement interval reservation, add extra `handleMidiInput()` call sites, or start OpenSpec firmware for stage A until stages O–R exit and the C-stage contract is written from measured envelopes.

---

## Vocabulary

| Term | Use |
|------|-----|
| **Persist admission** | `StorageManager::admit*` / `PersistenceWorkQueue` — intent queue |
| **Runtime admission** | Future coordinator of bounded units between `handleMidiInput()` entries |
| **Interval reservation** | Future remaining-µs check at a `handleMidiInput()` entry |
| **MIDI Input Gap (MIG)** | Time between consecutive `handleMidiInput()` entries (`DIAG,midi_gap`) |
| **Owner-Boundary Gate** | Bound or remove pathological owners before interval reservation |

See the contract §2 and [NAMING.md](../Authority/NAMING.md) § Timing-critical MIDI Input. Do not say “admission” without persist vs runtime meaning. Do not use “MIDI service” as an owner.

---

## Dependency ladder

```text
O  Observation integrity
T  Targeted closeout (shipped RC evidence)
R  Owner-Boundary Gate
      R1A  source-view transition (design session)
      R1B  persistence payload / undo scope (ownership + wire format)
      R1C  remaining owner inventory
C  Contract (MIDI Input Gap ceiling + reservation semantics)
A  Interval reservation (OpenSpec + DEC + ARCHITECTURE-REVIEW)
P  Proof and closeout
```

R1A and R1B may proceed as **sibling** design sessions. Interval reservation waits for **both** bounded contracts plus R1C. Extra PLAYING MIDI drain (historical S3) is a mitigation only after R1C, not a substitute for bounds.

Each firmware slice: one invariant, one owner, focused native tests, `pio test -e native`, `teensy41-capture-serial` build, local commit. Upload only after user confirmation.

---

## Stage O — Observation integrity

**Goal:** every candidate owner has a timestamped interval, and captures are not missing RECORD / stop / persistence windows.

| ID | Work | Owner | Behavior-preserving? |
|----|------|-------|----------------------|
| O1 | Snapshot persist work type at runtime-bundle **start** (`PERS,bundle` attribution) | `StorageManagerInternal::beginPersistenceWorkItem` / `PersistenceWorkItemJob` | Yes |
| O2 | Close RC-S0c: bounded Tier-A transmit under `SC_CAPTURE_FLUSH(8)` so RECORD and stop windows survive | `DebugSessionCapture::flushCaptureBuffer` | Yes if transmit stays count/time bounded |
| O3 | Keep `DIAG,midi_gap`, `midi_input`, transition stages, `PERS,bundle`, and `begin_capture` in the same capture; treat `DIAG,noterecon` as a post-RC-K3 zero on the note-off path. Historical captures used `DIAG,msi` / `midisvc` | `RuntimeTimingEnvelope` | Yes |
| O4 | Re-run native suite + ≈100-bar RECORD + two OVERDUB baseline after O1–O2 | — | — |

**Exit:** continuous DIAG through RECORD → stop → PLAYING → two OVERDUB; `PERS,bundle` work type matches the item that opened the bundle; remainder spans plus stop/source-view stages cover the 4–16 s class stalls.

**Rollback:** remove emitters / revert capture-transport only.

**Does not authorize:** persist payload narrowing, source-view deferral, interval reservation.

---

## Stage T — Targeted closeout

Shipped bounded-work and display-pressure fixes. Close documentation and remaining device residuals. **Not** interval reservation.

| ID | Work | Status |
|----|------|--------|
| T1 | RC-K1–K3 + RC-L1 note-off / reconstruct | **Device-verified** [`225803`](../../captures/session_20260812_225803.log). Close stale “re-measure pending” references. |
| T2 | Display RC-D/E/F/G residuals, §31d bailout stale-frame class | Named residuals only; do not chase frame-skip as scheduling work |
| T3 | Option B pitch-query | **Withdrawn**. Production overlap is RC-K3. Do not optimize further. |

**Exit:** CURRENT_WORK and sibling plans agree with T1; display residuals listed, not bundled into R or A.

---

## Stage R — Owner-Boundary Gate

### R1A — Overdub source-view entry (state-transition design session)

**Evidence:** `Loop::establishOverdubSourceView` runs inside `beginCapture(Overdub)` on the button path. [`225803`](../../captures/session_20260812_225803.log) first-pass floor 77–83 ms; later entries in [`105505`](../../captures/session_20260813_105505.log) / [`112104`](../../captures/session_20260813_112104.log) reach multi-second `begin_capture`. Sibling: [`realtime_incremental_work_overdub_source_view_refinement.md`](realtime_incremental_work_overdub_source_view_refinement.md).

**Architecture checkpoint**

| Question | Answer until a design choice is recorded |
|----------|------------------------------------------|
| Ownership change? | Extending `Loop::establishOverdubSourceView` / `accumulatePendingNoteChangesForIncomingNote`: **NO**. Making `visualCache` the overlap authority: **YES**. |
| State transition change? | Skipping flatten while still setting `overdubSourceViewEstablished_` at `beginCapture` with equivalent overlap: **NO**. Deferring, slicing across `handleMidiInput()` entries, or precomputing during PLAYING: **YES**. |

**This roadmap does not select** deferred, partial, blocking, or PLAYING-precompute behavior. Option B was withdrawn after [`021304`](../../captures/session_20260813_021304.log) (292 ms per note-off). Option A failed wrap/long-note fixtures on slice-built caches.

**Design session must pin before firmware:**

1. What PLAYING → OVERDUBBING does while source-view construction is incomplete (block overdub, allow capture without Shorten/Hide, partial view).
2. When `overdubSourceViewEstablished_` becomes true.
3. Invalidation on undo, slot switch, edit commit, overdub stop.
4. First-note overlap contract (must match today’s Shorten/Hide set, including wrap).

**Exit:** `begin_capture` is structurally bounded **or** explicitly off the timing-critical path with a recorded transition contract. No multi-second synchronous entry.

**Rollback:** revert the chosen source-view stage only.

---

### R1B — Scoped persistence payloads (ownership + wire format)

**Evidence:** [`112104`](../../captures/session_20260813_112104.log) `PERS,bundle,LoopUndoHistory` 4.25–16.57 s across 1316–1320 slices; MIDI Input Gap (`msi` in that capture) 7–9 s. `beginDeferredRuntimeBundleWrite` + `stepDeferredSaveJobUndoStacks` walks every track’s `GlobalUndoStack`. Persist keys are already scoped; the writer is not. LoadLoopJob PLAYING skip is closed ([`105505`](../../captures/session_20260813_105505.log)). Distinct from RC-J (STOPPED while clock streams). Distinct from R1A.

Keep `StorageManager::processDeferredSaveState` / `stepPersistenceWorkItem` as the single runtime persistence owner (DEC-008). Do not add a Track-level saver or parallel FSM.

#### Decision split (do not conflate)

| Track | What it is | Gate before firmware |
|-------|------------|----------------------|
| **B-narrow** | Payload-scope / wire-format optimization under `StorageManager`: serialize only the touched loop’s undo rows for a `LoopUndoHistory` persist key | Wire-format choice (below) + native fixture + DEC-022 tail-integrity lessons |
| **B-ownership** | DEC-024 Phase 2: move `GlobalUndoStack` from `Track` to `Loop`; API `loop.undo()` / `loop.undoDepth()` | Full [ownership-transfer protocol](../Authority/ARCHITECTURE_RULES.md#ownership-transfer-protocol): reassessment, [`OWNERSHIP_TRANSFER.md`](../Templates/OWNERSHIP_TRANSFER.md), compatibility + removal trigger, user approval, new `DEC-###` |

B-narrow may proceed **without** moving the stack **if** the writer can filter by `LoopId` / slot without a second mutable owner. If filtering requires Loop to own the stack, stop and run B-ownership first.

#### Wire-format choice (required before touching the runtime bundle writer)

Skipping clean sections in a replacement file without copy-forward or loop-owned files **loses** untouched state (DEC-022). Choose one:

1. **Loop-owned undo files** — persist undo per `LoopId`; runtime bundle no longer embeds other loops’ undo.
2. **Copy-forward / rewrite** — rewrite the shared bundle but copy untouched sections so unrelated persisted state survives.

Do not start firmware until this choice is recorded (plan section or DEC).

**Invariant:** an overdub stop persists the touched loop and its touched undo scope under `StorageManager`; it does not walk unrelated tracks, slots, or undo entries. “Active” / “selected” are routing context, not save scope.

**Tests (when authorized):** native — overdub on track 5 / slot X admits `LoopUndoHistory` for that key and serializes only that loop’s undo. Device — `112104`-class capture: bundle `totalUs` and post-stop MIDI Input Gap drop without Clock loss; reload/undo/slot selection intact.

**Does not authorize:** SyncDrain on normal STOP, Phase 5 crash recovery, interval reservation.

---

### R1C — Remaining owner inventory

Classify every remaining path as structurally bounded, empirically bounded with overrun policy, or unbounded/unknown / cold-excluded / included in the `handleMidiInput()` duration envelope.

Required rows (in addition to contract Appendix A):

| Path | Notes |
|------|-------|
| `rebuildVisualCacheIdleSlice` / `removeDisplayNotesOverlappingBars` | O(C)+O(V) |
| `processDeferredIdleMaintenance` × 8 | Budget multiplication |
| `reclaimUnreferencedDisabledPasses` | Not resumable |
| `processHitlSerialCommands` | Unbounded drain — count/time bound or exclude |
| Display frame / OLED transfer | Bailout ≠ bound |
| `processDroidUsbHostOutbound` | Two calls per `loop()` |
| `MidiButtonProcessor::processPendingPresses` | 2048 scan + synchronous actions |
| `rebuildPlaybackOrder` from clock path | Full sort |
| Record/overdub stop, STOPPED variants, NOTE_EDIT fold | In-service transition cost |
| `ClockManager::updateInternalClock` → `updateAllTracks` | ISR overlap |
| USB-host callbacks | No USB/DIN reorder contract |

**Exit:** every path has an owner, a resumable boundary where applicable, a structural bound or explicit empirical overrun policy, and a transport/capture gate.

---

## Stage C — Contract from measured envelopes

Only after R exits.

- Define the MIDI Input Gap ceiling from device evidence across 120–300 BPM. 2 ms / 5 ms remain withdrawn.
- Protected interval = MIDI Input Gap before the next `handleMidiInput()` entry, not one `loop()` iteration.
- Reservation uses the complete bounded cost of the next unit. Overrun: emit diagnostic, stop reserving that class, retain continuation.
- Specify nested `updateAllTracks`, display, persistence, load, reclaim, fader, serial, USB-host, and ISR consumption of the same interval.
- Order: timing safety, timing-critical MIDI Input, reservation fit, fairness among fitting units, throughput.

**Exit:** contract in the architecture document proves collective reservation from declared work units. Still no firmware for interval reservation.

---

## Stage A — Interval reservation (not authorized)

**Formal trigger:** new coordination state at `main.cpp::loop()`. Requires `/opsx:propose` (e.g. `runtime-interval-reservation`), `ARCHITECTURE-REVIEW.md`, user-approved phase, and `DEC-###` **before** firmware. Do not name a new Manager.

1. Shared interval state at the loop integration point.
2. Owner reservation calls around already resumable units, one owner class per commit: persistence → load → display/maintenance → reclaim → remaining transitions.
3. Keep MIDI-first ordering and `handleMidiInput()` call-site count unchanged initially.
4. Reject or defer work that does not fit; never discard pending work.
5. Fairness and re-entry guards before enabling more classes.
6. Do not reserve a unit whose measured cost is not bounded.
7. Extra PLAYING drain (historical S3) only as a labeled mitigation after A2, if O/R evidence shows it helps without masking unbounded work.

Reuse `PersistenceBudget`, `LoadLoopBudget`, `DeferredJobScheduler::runFrame`, `LoadLoopSelectionPolicy`. Interval reservation sits **above** those owners; it does not replace them.

---

## Stage P — Proof and closeout

Native: `pio test -e native`. Firmware: `teensy41-capture-serial`. Device matrix:

- long RECORD (≈100 bar, not 999);
- repeated overdub and overdub-over-overdub;
- overdub stop while PLAYING;
- transition to STOPPED and deferred save drain;
- selected-slot vs active-slot;
- load / reclaim / display concurrent with transport;
- boot / slot-restore variant.

Required evidence: no multi-second PLAYING MIDI Input Gap; no lost external MIDI clock; source-view entry and persistence bundle within measured budgets; deferred work remains pending and completes later; no regression in display completeness, undo, persistence reload, or slot selection.

Closeout: architecture contract, this roadmap, `CURRENT_WORK.md`, `PROJECT_STATE.md`, `DELIVERABLE_TRACKING.md`, `DEC-###`, OpenSpec archive if A shipped.

---

## Mandatory design gates

These are **stops**. Do not pick a default in code to “make it work.”

### G1 — Source-view transition (R1A)

Changing when `establishOverdubSourceView` runs relative to PLAYING → OVERDUBBING is a state-transition design session. Record the four pins in R1A before firmware.

### G2 — Undo ownership (R1B-ownership)

DEC-024 Phase 2 (`GlobalUndoStack` Track → Loop) is an ownership transfer. Complete reassessment, `OWNERSHIP_TRANSFER.md`, compatibility/removal trigger, user approval, and a new DEC. Do not silently move the stack as part of a persistence optimization.

### G3 — Persistence wire format (R1B-narrow)

Choose loop-owned undo files vs copy-forward/rewrite **before** editing `beginDeferredRuntimeBundleWrite` / `stepDeferredSaveJobUndoStacks`. Preserve DEC-022 tail integrity.

### G4 — ISR overlap (C / A)

`ClockManager::updateInternalClock` → `updateAllTracks` from `IntervalTimer` is a second execution context. Interval reservation must state which structures are ISR-safe, which are main-loop-only, and how overlap is reserved. Unresolved at C-stage exit → do not start A.

### G5 — Display freshness

Product policy for live-capture compose when resolve cannot finish: stale last-valid frame, resumable resolve, or always-complete. Each has a different `DisplayManager` owner implication. HITL pass/fail must name the chosen policy.

### G6 — Transport-active STOPPED (RC-J)

Do not widen `timingCriticalTrackActive` as a dump/RC-J patch ([parent capture-critical predicate](realtime_incremental_work_capture_overdub_architecture.md)). Split policies:

- capture-critical (RECORD/OVERDUB);
- transport-active while external clock streams (STOPPED included);
- background persistence allowed.

RC-J, PLAYING dump (`LoopUndoHistory` bundle), and R1A `begin_capture` are three rows, not one flag.

### G7 — Telemetry non-loss

Tier-A DIAG must egress under timing-critical flush or the envelope is invalid (invariant Q). O2 is a prerequisite to claiming S0 exit.

### G8 — Nested re-entry

MIDI dispatch → button actions → stop/start; USB-host callbacks; loop-edit `setCurrentTick`. Nested work consumes the same interval. A-stage must include a re-entry/depth guard.

---

## Existing admission surfaces (do not unify by renaming)

| Surface | NAMING term | Role |
|---------|-------------|------|
| `StorageManager::admit*` / `PersistenceWorkQueue` | Persist admission | Intent queue |
| DEC-018 current-heap dispatch | Persist dispatch gate | When a queued save may run |
| `DeferredJobScheduler::runFrame` | Scheduled / deferred | Load-frame budget (DEC-027) |
| `LoadLoopSelectionPolicy::shouldStepLoadLoopJob` | Load policy | Focus vs PLAYING skip |
| `PersistenceBudget` / `LoadLoopBudget` | Local budget | Per-owner slice cap |
| Future interval reservation | Interval reservation | Shared remaining µs at a `handleMidiInput()` entry |

---

## Historical labels (do not implement from these)

| Old | Disposition |
|-----|-------------|
| S0 / S0b–S0e | Shipped / attributed — see contract §14 |
| S1–S8 | Superseded by O–T–R–C–A–P |
| Priority 0–4 (Cursor plan) | Mapped: P0→O, P1A→R1A, P1B→R1B, P1C→R1C, P2→C, P3→A, P4→P |
| `RuntimeWorkBudget::admit(WorkClass)` | Rejected |

---

## Current execution

Immediate firmware, if any, remains the work named in [`docs/Runtime/CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) (overdub-stop PLAYING dump observation / R1B design). This roadmap does **not** move interval reservation or R1A/R1B firmware into “now implementing.”
