# Unified capture stop driver — implementation strategy

> **Vocabulary:** Capture-stop layered terms remain plan-local. Canonical deferral vocabulary: [`docs/00-authority/NAMING.md`](../00-authority/NAMING.md) § Concept boundaries (Deferred, Pending).

**Kind:** refinement  
**Date:** 2026-07-08  
**Status:** Design refinement (incremental migration; supersedes single-shot refactor)

**Related:** [`.cursor/plans/unified_capture-stop_driver_78b2c945.plan.md`](../../.cursor/plans/unified_capture-stop_driver_78b2c945.plan.md), [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md), DEC-020 continuous-runtime-persistence, DEC-023 unified-capture-commit-owner.

**OpenSpec:** [`openspec/changes/unified-capture-commit-owner/`](../../openspec/changes/unified-capture-commit-owner/) (Phase 0 complete).

**Per-phase review:** [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/unified-capture-commit-owner/ARCHITECTURE-REVIEW.md) — architecture + implementation gates for Phases 1–5.

---

## Purpose

After beginning implementation it became apparent that the current refactor attempts to change too many architectural concerns simultaneously. Record start regressions and capture crashes made it difficult to determine whether failures originated from scheduling, ownership, playback, persistence, or the stop pipeline itself.

This document refines the implementation strategy while preserving the long-term architectural goal.

---

## Primary architectural invariant

> Exactly one runtime component owns an in-progress **capture commit** from the moment recording or overdubbing stops until the loop reaches its next stable runtime state.

Everything else in this design derives from this invariant.

That owner is responsible for:

- progressing commit stages
- coordinating persistence
- updating playback state
- updating display state
- emitting telemetry
- handling cancellation
- reporting completion

Whether the request originated from record or overdub is merely contextual information (`CommitReason`).

---

## Core observation

The architectural problem is **not** that record and overdub have different APIs.

The architectural problem is that there is **no single owner** for capture commit progression after the user stops recording or overdubbing.

Current implementation spreads responsibility across:

- synchronous record stop
- deferred overdub stop
- persistence requests
- playback projection
- display updates
- transport transitions
- multiple cancel paths

The objective is therefore **single ownership of capture commit**, not identical code paths or a single “stop driver” that absorbs unrelated runtime logic.

---

## Capture stop vs capture commit

Stopping recording and committing a capture are **related but separate** architectural concepts.

Recommended runtime model:

```text
User requests stop
        │
        ▼
Capture Stop          ← entry paths (record / overdub); mode-specific preparation
        │
        ▼
Mode-specific preparation
        │
        ▼
Capture Commit        ← shared pipeline owner (commitCaptureForStop)
        │
        ▼
Runtime Finalization  ← playback / display / persistence side effects
        │
        ▼
Stable runtime state
```

This reflects existing code boundaries:

- [`Loop::commitCapturePass()`](../../src/Loop.cpp) — seal and publish
- [`Track::finalizeCommitSideEffects()`](../../src/Track.cpp) — persistence, cache defer, REVT

The shared **capture commit pipeline** owns commit and its immediate finalization side effects. It does **not** own the full stop interaction (transport, quantization, freeze initiation, etc.).

### Naming: concept vs function

| Layer | Name |
|-------|------|
| Architectural concept | **capture commit pipeline** (stages: Flush → Seal → Publish → Finalize) |
| Implementation function | **`commitCaptureForStop(...)`** on `Track` — advances pipeline stages |

Uses existing Track verbs (`commit*`, `finalize*`) — not `execute*`. Avoid `commitCapturePipeline()` as a function name (conflates concept and implementation).

---

## Pipeline ownership boundary

### The capture commit pipeline owns

- flushing pending notes into capture
- sealing capture (`commitCapturePass`)
- publishing passes
- `finalizeCommitSideEffects()`
- playback cache invalidation (post-commit)
- display notification (post-commit)
- persistence request (post-commit)
- commit-stage telemetry

### The pipeline intentionally does NOT own

- capture input (`appendCaptureEvent`, MIDI ingest)
- transport control (Start/Stop/Clock)
- record/overdub button behavior
- quantization policy
- loop-length calculation
- freeze initiation (`freezeOverdubCapture`)
- NOTE_EDIT preparation before commit (fold into session)

These remain the responsibility of **capture stop** entry paths (record / overdub). This boundary prevents gradual expansion of the pipeline into unrelated runtime logic.

---

## Preserve genuine differences (capture stop entry paths)

**Record stop** is responsible for:

- creating loop length
- quantization
- first playback projection
- transition from empty slot

**Overdub stop** is responsible for:

- adding an additional capture pass context
- freeze semantics
- preserving existing loop geometry

These complete **before** `commitCaptureForStop()` is invoked.

---

## Migration strategy

Split work into two architectural steps. Separate **behavior extraction** from **behavior migration**.

### Step A — shared capture commit pipeline (Phases 1–2)

```text
Record Stop → record-specific preparation → commitCaptureForStop()
Overdub Stop → overdub-specific preparation → commitCaptureForStop()
```

Entry paths remain independent. Overdub may call the pipeline from deferred FSM stages; record calls it synchronously until Phase 3.

### Step B — unified deferred commit owner (Phase 3+)

Only after Step A is stable, record migrates onto the deferred FSM already used by overdub. Both entry paths enqueue into the same commit owner.

```text
Record Stop ──► queue (existing overdub queue API) ──┐
Overdub Stop ─► queue ──────────────────────────────┤
                                                    ▼
                                         Capture Commit FSM
                                                    │
                                                    ▼
                                         commitCaptureForStop()
```

### Minimize renaming during migration

Keep until Phase 5 (behavior-neutral rename commit):

- `queueOverdubStopCommit()`
- `processDeferredOverdubStop()`
- `OverdubStopCommitStage`

### Reduce implementation risk

Do not combine in one pass: unified FSM, deferred record stop, scheduler changes, playback changes, API renames. Isolate behavior changes per phase for git bisect.

---

## Behavior-preserving extraction invariant (Phases 1–2)

> Phases 1 and 2 are behavior-preserving refactors.

During Phases 1 and 2:

- HITL captures should remain identical
- Telemetry ordering should remain unchanged
- Runtime state transitions should remain unchanged
- No optimization or behavioral simplification
- Only acceptable changes: code organization and ownership of commit logic

**Behavioral changes begin only in Phase 3** when record transitions onto the deferred commit owner.

---

## Relationship to DEC-016 (runtime architecture)

DEC-023 and DEC-016 address **different layers** of the same pipeline. Both are required; neither replaces the other.

Authority: [DEC-016](../DECISION_LOG.md#dec-016-runtime-architecture-four-layer-model), [`RuntimeArchitecture.md`](../00-authority/Architecture/RuntimeArchitecture.md), [`DerivedViews.md`](../00-authority/Architecture/DerivedViews.md). Parallel track: [`next_session_handoff_overdub_uip_architecture.md`](next_session_handoff_overdub_uip_architecture.md).

### Layer map

```text
Capture Storage          passes, capture chunks (authoritative)
        │
        ▼  ◄── DEC-023 owns publish + first invalidation at stop
Derived Representations  mergedEvents, visualCache, capturePreview
        │
        ▼  ◄── DEC-016 owns build policy (chunk merge vs materialize, idle slices)
Interval Projection    projectionCycleStartTick, playbackEventPhase, display window
        │
        ▼
Consumers                playMidiEvents, DisplayManager, LEDs
```

| Concern | DEC-023 (this change) | DEC-016 / runtime handoff |
|---------|----------------------|---------------------------|
| Who runs flush → seal → finalize at stop | **Yes** | No |
| Single post-commit invalidation hook | Phase 2 | Revision chain defines *when* stale |
| `mergeActiveCapturePasses` for playback window | No | **Yes** (Phase A shipped) |
| `projectionCycleStartTick` alignment | Phase 2 unifies hook | UIP Phases 1–4 |
| Display 16-bar window / playhead interval | No | Display + IntervalProjection |
| Live overdub display merge | No | DisplayManager live path |
| Mid-pass chunk persistence during overdub | Orthogonal (DEC-020) | No |

### Overdub chunk / window / tick symptoms

Observed failures (playback window wrong, display showing chunks in the wrong loop region, playhead jumping between subparts) have **two** contributing classes:

1. **Commit boundary corruption** — split stop drivers, stale deferred FSM, `captureAppendFrozen_`, half-published passes, asymmetric invalidation after publish. **DEC-023 is the primary fix.**

2. **Representation + projection correctness** — which chunk refs appear in `primaryWindow.mergedEvents`, when `playbackRevision` rebuild runs, whether display uses `startLoopTick` vs `projectionCycleStartTick`, live overdub merge vs published playback. **DEC-016 / UIP / handoff Phases A–C remain required** if symptoms persist after commit is unified.

Today record and overdub **diverge** after publish:

- Record stop sets `projectionCycleStartTick` inline (with truncation rewind).
- Overdub stop calls `resetPlaybackState` + `invalidatePlaybackWindow` in the deferred FSM complete stage.

Phase 2 SHALL route both through one post-commit hook so consumers see a single revision bump and projection update per published pass.

### What DEC-023 does not fix alone

- Chunk-ref merge content or pass ordering inside `mergeActiveCapturePasses`
- Interval projection math (`IntervalProjection`, slot-switch grid commit)
- Display stale-while-revalidate and 16-bar window interval filtering
- CPU stall from full materialize + deferred save on first PLAYING tick (scheduling — see [`64bar_regression_commit_analysis_enhancement.md`](64bar_regression_commit_analysis_enhancement.md))

### Prerequisite ordering

1. **DEC-023 Phases 1–2** — stabilize commit + unified post-commit invalidation (behavior-preserving).
2. **Verify** overdub stop: one `playbackRevision` bump, projection set in one hook, display snapshot after same commit.
3. **If window/playhead still wrong** — continue DEC-016 handoff / UIP (points to representation or interval, not stop ownership).

### Phase 2 acceptance (projection + window bridge)

Phase 2 is behavior-preserving but SHALL satisfy these **structural** checks (native or serial telemetry):

- [ ] Record stop and overdub stop both call the **same** post-commit helper after `commitCaptureForStop` returns `Published` (single implementation, two entry paths).
- [ ] That helper is the **only** place that combines: `invalidatePlaybackWindow`, playback cache invalidation, display snapshot trigger, and `requestDeferredSaveState` for pass publish.
- [ ] Overdub deferred complete stage no longer duplicates inline `resetPlaybackState` + `invalidatePlaybackWindow` + save request outside the shared helper (record tail duplicate save removed).
- [ ] `playbackRevision` / `invalidatePlaybackCaches` fire once per successful publish, not from both entry path and pipeline.

HITL gate unchanged for Phase 2 (baseline identical). Structural checks prevent reintroducing asymmetric projection hooks that caused overdub chunk / tick misalignment.

### Relationship to slot-performance-interaction (input routing)

**Which button drives capture** is a separate concern from **how commit runs at stop**.

| Layer | Owner | This plan (DEC-023)? |
|-------|--------|----------------------|
| Who may **start** record/overdub | `MidiButtonActions` + future [`slot-performance-interaction`](../../openspec/changes/slot-performance-interaction/) | **No** |
| Who may **stop** capture (any button) | Same input layer → `TrackManager` / `Track` | Entry only |
| **Commit** after stop | `commitCaptureForStop` + deferred owner | **Yes** |

Today **Record button** (`handleToggleRecord`) and **slot buttons** (`handleToggleRecordForSlot`, `OVERDUB_FOR_SLOT`) duplicate capture entry: empty-slot record, queue-at-bar, stop record/overdub, start overdub. That inconsistency lives in the **input layer**, not in `commitCapturePass`.

Planned optimization ([`slot-performance-interaction`](../../openspec/changes/slot-performance-interaction/proposal.md)): **Record button only** for capture/history; slot buttons for performance (launch, mute, restart, overlay) — arm/record/overdub removed from slots.

**What DEC-023 still improves for slots:** any path that **ends** capture must commit the same way:

- Slot press while recording → `stopRecordingTrack`
- Slot press while overdubbing → `stopOverdubbing`
- Switch slot during capture → [`finalizeCaptureAndSelectSlot`](../../src/TrackManager.cpp) (`stopRecordingToStopped` / `stopOverdubbing`)
- Leave queued-arm slot → `cancelPendingRecordArm` (Phase 4: `cancelCaptureCommit` if commit in flight)

So slot vs record **stop** behavior converges; slot vs record **start** behavior waits for `slot-performance-interaction`.

```text
slot-performance-interaction  →  WHO may start/stop capture (input)
DEC-023 commit owner          →  HOW commit runs when any path stops (Track)
DEC-016 / UIP                   →  HOW merged chunks + projection look after commit
```

Recommended order: **DEC-023 Phases 1–2** (unified stop/commit) → **`slot-performance-interaction`** (single capture input driver) → **DEC-016** if window/playhead still wrong.

---

## Implementation phases

| Phase | Scope | Behavior change? | Regression target |
|-------|--------|------------------|-------------------|
| **0** | OpenSpec + docs only | No | N/A |
| **1** | Extract `commitCaptureForStop()` from duplicated commit logic | No | Existing HITL unchanged |
| **2** | Centralize post-finalize side effects in pipeline | No | Record and overdub match baseline |
| **3** | Single capture commit owner; record → deferred FSM | **Yes** | Acceptable record-stop latency; no capture loss |
| **4** | `resetCaptureLifecycle()` replaces scattered cancel paths | Yes (abort semantics) | Arm/record/overdub lifecycle clean |
| **5** | API rename cleanup | No | Native + 64+64 HITL |

**Primary files:** [`src/Track.cpp`](../../src/Track.cpp), [`include/Track.h`](../../include/Track.h).

---

## Success criteria

Architecture is complete when:

- Capture commit has exactly one runtime owner
- Record and overdub share the same commit pipeline (`commitCaptureForStop`)
- Deferred processing is owned by one component
- Capture stop and capture commit remain distinct responsibilities
- Scheduler changes (DEC-020) remain independent of commit ownership
- API cleanup occurs only after behavioral stability (Phase 5)
- 64+64 HITL completes with stable persistence, playback, and display

The strategy favors incremental architectural convergence. Each intermediate step remains testable, understandable, and easy to bisect.

---

## Review additions (2026-07-08)

Architecture review notes that strengthen ownership boundaries without changing the long-term destination:

- Separate **capture stop** from **capture commit**
- Explicit pipeline boundary (owns vs does not own)
- Behavior-preserving Phases 1–2 invariant
- Distinguish pipeline (concept) from `commitCaptureForStop` (function)
- Primary invariant elevated as north star

---

## Naming glossary (lock in Phase 0 OpenSpec)

Review naming **before Phase 1 firmware**, not only at Phase 5 code rename. Phase 0 OpenSpec SHALL include a short glossary delta; Phase 5 applies symbols in code.

### Layered vocabulary

| Layer | Prose / docs | Code (target) | Notes |
|-------|----------------|---------------|--------|
| User action | capture stop | `stopRecording`, `stopOverdubbing` | **Keep** — entry paths, not commit owner |
| Mode prep | (no new type) | stays inline in stop entry | Quantize, freeze, NOTE_EDIT fold |
| Commit owner | capture commit | deferred FSM + `commitCaptureForStop` | North-star invariant |
| Loop seal/publish | (existing) | `Loop::commitCapturePass` | **Do not rename** — one step inside pipeline |
| Post-publish side effects | (existing) | `Track::finalizeCommitSideEffects` | **Do not rename** — called from pipeline |
| Abort in-progress commit | cancel capture commit | `cancelCaptureCommit()` | Phase 4; replaces `cancelDeferredOverdubStop` (keep `cancel*` verb) |
| Lifecycle reset | (prose only) | `resetCaptureCommitSession()` | Prefer over `resetCaptureLifecycle` — scoped to commit session, not full capture input/transport |

### `commitCaptureForStop` vs `commitCapturePass`

Established Track verbs in this area: **`commit`** (`commitQueuedPlaybackStart`, `queueOverdubStopCommit`), **`finalize`** (`finalizeCommitSideEffects`, `finalizeLoopAtStop`), **`process`** (`processDeferredOverdubStop`), **`flush`** (`flushPendingNotesIntoCapture`). No `execute*` usage — do not introduce it.

- **`Loop::commitCapturePass`** — seal + publish one pass (already in specs); one step **inside** the pipeline.
- **`Track::commitCaptureForStop`** — pipeline orchestrator called **after** capture-stop preparation: flush → `commitCapturePass` → `finalizeCommitSideEffects` → (Phase 2) post-commit invalidation/display/persist/telemetry.

`ForStop` ties to `CommitReason::RecordStop` / `OverdubStop` without implying the function owns stop semantics (quantization, freeze, transport). Parallels `commitQueuedPlaybackStart` (commit + contextual qualifier).

Rejected: `executeCaptureCommit`, `commitCapturePipeline()` as function names, `run*`.

### Phase 5 rename targets (behavior-neutral)

Align names with **capture commit**, not capture stop:

| Today | Phase 5 target | Avoid |
|-------|----------------|--------|
| `OverdubStopCommitStage` | `CaptureCommitStage` | `CaptureStopCommitStage` (stop ≠ commit) |
| `queueOverdubStopCommit` | `queueCaptureCommit` | `queueCaptureStopCommit` |
| `processDeferredOverdubStop` | `processDeferredCaptureCommit` | `processDeferredCaptureStop` |
| `cancelDeferredOverdubStop` | `cancelCaptureCommit` | `abortCaptureStopCommit` |

Keep until Phase 5: all `OverdubStop*` symbols above.

### OpenSpec / doc title

Working title `unified-capture-stop-driver` is historical. Phase 0 proposal MAY use **`unified-capture-commit-owner`** to match the primary invariant (commit ownership, not a monolithic stop driver).

### Reuse without rename

- `CommitReason` (`RecordStop`, `OverdubStop`, …) — **keep**; contextual tag for pipeline
- `CommitResult` — **keep**
- `freezeOverdubCapture` — **keep through Phase 4** (overdub-only stop prep); optional Phase 5 → `freezeCaptureAppend` only if behavior unchanged
- Telemetry: `logRecordStopStage` / `logOverdubStopStage` → Phase 2 shared `logCaptureCommitStage(CommitReason, …)` (behavior-preserving ordering)
