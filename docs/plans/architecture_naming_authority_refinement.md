# Architecture naming authority — investigation and naming debt

**Date:** 2026-08-06  
**Status:** Investigation complete; policy shipped in [`docs/00-authority/NAMING.md`](../00-authority/NAMING.md)  
**Branch:** `docs/architecture-naming-authority`

One-time audit supporting the naming authority refinement. **Timeless policy lives in NAMING.md** — this document may age without invalidating the authority doc.

---

## Purpose

Establish a shared architectural vocabulary audit baseline:

- Current terminology distribution across code and docs
- Naming debt roadmap for incremental migration
- Geometry resolution naming analysis
- Documentation cleanup recommendations

---

## Section A — Codebase audit

Counts are approximate substring matches in `src/`, `include/`, and `docs/` (inflated by includes, comments, and compound words).

### Variant audit

| Variant group | src+include | docs | total | Dominant term |
|---------------|------------:|-----:|------:|---------------|
| Pending | 525 | 187 | 712 | Capture queues, slot switch, outbound coalesce |
| Deferred | 653 | 309 | 962 | SD persistence, idle maintenance, undo hydrate |
| Scheduled | 15 | 2 | 17 | Persistence work queue, fader timing |
| Input | 164 | 92 | 256 | MIDI/USB routing |
| Ingress (filename) | 0 | 1 | 1 | `NoteEdit*Ingress.cpp` only |
| Inbound | 0 | 5 | 5 | Docs only |
| Output | 29 | 12 | 41 | MIDI egress |
| Outbound | 203 | 57 | 260 | Note-edit fader motor path |
| Egress | 0 | 0 | 0 | Not used |
| Update | 190 | 154 | 344 | Display/UI refresh |
| Process | 72 | 87 | 159 | Deferred idle, button/fader processors |
| Apply | 93 | 76 | 169 | Edit geometry, pass rows |
| Playback | 421 | 399 | 820 | Runtime machinery |
| Playing | 176 | 49 | 225 | Transport/slot state |
| Snapshot | 372 | 124 | 496 | Undo clones, display probes |
| State | 1470 | 429 | 1899 | Live session owners |
| Pipeline | 35 | 48 | 83 | Geometry + outbound motor flows |
| Controller | 0 | 18 | 18 | Docs/plans only — zero in code |
| Manager | 2510 | 1282 | 3792 | Ubiquitous owner suffix |
| Select | 1333 | 458 | 1791 | Enum values, navigation actions |
| Selection | 363 | 145 | 508 | `EditorSelection`, load policies |
| Geometry | 368 | 141 | 509 | Note-edit domain |
| Position | 89 | 56 | 145 | Motor/MIDI song position |

### Subsystem dominance map

| Subsystem | Winning term | Runner-up | Notes |
|-----------|--------------|-----------|-------|
| SD / persistence | Deferred | Pending (overlay/hydrate) | `processDeferredSaveState`, `DeferredSaveJobStages` |
| Capture / loop commit | Pending | Deferred (idle validate) | `hasPendingCapturePass` |
| MIDI I/O | Input / Output | Outbound (motor path) | Distinct concepts — see NAMING.md |
| Note-edit control surface | Outbound, Geometry, Select | Selection (typed model) | Three parallel “select” concepts |
| Transport / slots | Playing (state) | Playback (runtime) | Not interchangeable |
| Undo / display copies | Snapshot | State (live) | Both preserved |
| Module ownership | Manager | Processor | No Controller in code |

### Manager / Handler / Processor inventory

**Managers (12):** `TrackManager`, `EditManager`, `StorageManager`, `DisplayManager`, `ControlSurfaceManager`, `LoopEditManager`, `ClockManager`, `MidiButtonManager`, `MidiFaderManager`, `MidiLedManager`, `GpioButtonManager`, `LooperStateManager`

**Handlers (2):** `MidiHandler`, `BarStepButtonHandler`

**Processors (2):** `MidiButtonProcessor`, `MidiFaderProcessor`

**Pipeline symbols (not classes):** `runEditSessionGeometryPipeline`, `runEditSessionGeometryPipelineForCausingNote`, `completeOutboundPipelineAtDone`, `isRevisionLoadDisplayPipelineActive`

### Prior naming authority (before this refinement)

| Source | Role |
|--------|------|
| `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc` | De facto domain glossary |
| `ARCHITECTURE_RULES.md` § Naming rules | Suffix table only |
| `CODE_STRUCTURE.md` | Duplicate suffix table |
| Root `README.md` § Loop storage vocabulary | Partial glossary |
| ~15 plan-local glossaries | Feature-specific, fragmented |

---

## Section B — Naming debt audit

Roadmap for incremental convergence. **Do not batch-rename** — adopt when touching each subsystem.

| Current | Preferred | Priority | Approx. usage | Recommended timing |
|---------|-----------|----------|---------------|-------------------|
| `runEditSessionGeometryPipeline` | `runEditSessionGeometryResolution` | **High** | ~15 call sites | Next note-edit geometry refactor |
| `RunEditSessionGeometryPipeline.*` files | `RunEditSessionGeometryResolution.*` | High | 3 files + driver header | Same refactor pass |
| `runEditSessionGeometryPipelineForCausingNote` | `runEditSessionGeometryResolutionForCausingNote` | High | `NoteMovementUtils`, `EditManager` | Same refactor pass |
| `*Ingress.cpp` filenames | `*Input.cpp` | Low | 2 ControlSurface TUs | Next ControlSurface TU work |
| Ingress/egress in ARCHITECTURE_RULES prose | Input / Output / Outbound | Low | Ownership table | Doc cleanup (this pass) |
| Duplicate suffix tables | Single table in NAMING.md | Medium | 2 authority/guide docs | This pass |
| Cursor rule as de facto authority | NAMING.md canonical | High | Agent rules, openspec | This pass |
| `completeOutboundPipelineAtDone` | Evaluate: legitimate async pipeline vs rename | Medium | `ControlSurfaceManager` | Next motor/outbound refactor |
| `flattenActiveCapturePasses` (if any remain) | `mergeActiveCapturePasses` | Medium | Loop/Track | naming_drift Track 1 (separate pass) |
| `EditorSelection` / `SelectNavigation` / `NoteEditKind::Select` | Keep all three; boundaries in NAMING.md | Medium | EditManager, ControlSurface | Doc-only now |
| `LooperStateManager` vs `looperState` global | Document; no rename | Low | `LooperState.h` | Doc-only |
| Plan-local Commit/Publish glossaries | Promote stable terms to NAMING.md | Medium | unified_publish_pipeline_* plans | As terms stabilize |
| `Playing` in docs for runtime machinery | Playback | Low | Plans, comments | Incremental doc edits |
| `Controller` in plans | Manager or Handler | Low | `controller_midi_priority_check_*.plan.md` | When plans touched |
| Serial capture tokens (`#CAP`, etc.) | **Frozen** | — | HITL fixtures | Never rename |

Reference also: [`naming_drift_scoped_edit_pass_handoff.md`](naming_drift_scoped_edit_pass_handoff.md) Track 1 for merge/materialize/memory-tier renames.

**Consolidated index:** [`refactor_priority_backlog.md`](refactor_priority_backlog.md) — cross-cutting P1/P2/P3 backlog linking this audit, hygiene, and CURRENT_WORK.

---

## Section C — Geometry resolution naming

### Finding

`runEditSessionGeometryPipeline()` in [`RunEditSessionGeometryPipeline.cpp`](../../src/RunEditSessionGeometryPipeline.cpp) is a **deterministic synchronous resolution algorithm**, not a runtime pipeline or queue. It runs to completion in one call stack with no asynchronous scheduling.

### Phases (architectural decomposition)

```
Validation     → session state, focus, loop length, note ID preparation
Preparation    → changed notes, evaluation scope, baseline projection
Analysis       → eligible pairs, interaction analysis, grouping
Resolution     → constraint solving, action generation
Commit         → apply actions, refresh caches
```

### Recommended naming

| Current | Preferred |
|---------|-----------|
| `runEditSessionGeometryPipeline` | `runEditSessionGeometryResolution` |
| `runEditSessionGeometryPipelineForCausingNote` | `runEditSessionGeometryResolutionForCausingNote` |
| `RunEditSessionGeometryPipeline.h/.cpp` | `RunEditSessionGeometryResolution.h/.cpp` |
| `RunEditSessionGeometryPipelineDriver.h` | `RunEditSessionGeometryResolutionDriver.h` |

### Future decomposition vocabulary (when subsystem refactored)

- `GeometryPreparation`, `GeometryAnalysis`, `GeometryResolution`, `GeometryCommit`
- Or: `GeometryResolver`, `GeometryResolutionContext`, `GeometryResolutionActions`

### Call sites to migrate (record only — not this pass)

- `src/Utils/NoteMovementUtils.cpp` — move/length/pitch paths
- `src/EditManager.cpp` — create note, geometry apply
- `include/RunEditSessionGeometryPipeline.h`, `RunEditSessionGeometryPipelineDriver.h`
- `docs/Guides/MOVE_NOTE_LOGIC.md`
- `openspec/specs/note-edit-modification-session/spec.md`
- Archived OpenSpec change `2026-08-05-edit-session-action-geometry`

NAMING.md codifies **Resolution vs Pipeline**; this section records the specific rename debt.

---

## Section D — Documentation cleanup recommendations

### Completed in this pass

- Created [`docs/00-authority/NAMING.md`](../00-authority/NAMING.md) as canonical policy
- Slimmed `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc` to pointer + agent enforcement
- Updated authority hierarchy in `docs/00-authority/README.md`
- Added naming-as-architecture section to `ARCHITECTURE_RULES.md`
- Deduplicated suffix table in `CODE_STRUCTURE.md` (link to NAMING.md)
- Added naming review checklist to `docs/agents/reviewer.md`
- Updated `docs/README.md`, `AGENT_CONTEXT_MAP.md`, `openspec/config.yaml`
- Trimmed root `README.md` glossary to summary + link

### Plan glossary index

Plans with local vocabulary — terms stable enough for NAMING.md are marked **superseded** in plan headers:

| Plan | Vocabulary topic | Status |
|------|------------------|--------|
| [`naming_drift_scoped_edit_pass_handoff.md`](naming_drift_scoped_edit_pass_handoff.md) | merge/materialize/memory tiers | Partially shipped; merge/materialize in NAMING.md |
| [`unified_publish_pipeline_commit_terminology_refinement.md`](unified_publish_pipeline_commit_terminology_refinement.md) | Commit vs Publish | Plan-local until promoted |
| [`unified_capture_stop_driver_refinement.md`](unified_capture_stop_driver_refinement.md) | Capture-stop layered vocabulary | Plan-local |
| [`workspace_session_persistence_handoff.md`](workspace_session_persistence_handoff.md) | Persistence request vocabulary | Plan-local |
| [`playback_merged_events_window_refinement.md`](playback_merged_events_window_refinement.md) | PlaybackWindow vs PlaybackMergedMidiEvents | Plan-local |

### Optional future items

- Move global `naming-and-terminology.mdc` into repo for version control
- Promote Commit/Publish stable terms to NAMING.md when unified publish pipeline ships
- Rename geometry pipeline symbols on next note-edit geometry refactor

---

## Success criteria (this refinement)

| Criterion | Status |
|-----------|--------|
| Policy separated from investigation | NAMING.md has no counts; this doc has full audit |
| Ubiquitous language established | NAMING.md concept boundaries |
| Naming is architectural governance | Authority hierarchy updated |
| Review checklist wired | reviewer.md + NAMING.md |
| No firmware renames in this pass | Docs-only |
