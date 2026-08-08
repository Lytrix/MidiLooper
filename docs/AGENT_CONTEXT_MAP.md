# Agent context map

Deterministic documentation loading per domain. **Before implementation:** detect domain → load required docs → write a short context summary → proceed.

Authority order: [Authority/README.md](Authority/README.md).

Always load first:

- [Runtime/PROJECT_STATE.md](Runtime/PROJECT_STATE.md)
- [Runtime/CURRENT_WORK.md](Runtime/CURRENT_WORK.md) — implementation scope; do not implement outside § Now implementing
- [Runtime/ROADMAP.md](Runtime/ROADMAP.md) — optional; never sole authority for coding
- [DECISION_LOG.md](DECISION_LOG.md) — search for topic; run [Templates/DECISION_REVIEW.md](Templates/DECISION_REVIEW.md) before firmware edits
- [Authority/PROJECT_INTENT.md](Authority/PROJECT_INTENT.md)
- [Authority/ARCHITECTURE_RULES.md](Authority/ARCHITECTURE_RULES.md)

**Runtime orientation (agents — load early for firmware work):** [plans/runtime_process_building_blocks_overview.md](Plans/runtime_process_building_blocks_overview.md) — high-level logic, owners, hard don'ts (not authority; then follow domain sections below).

---

## OpenSpec / timeline governance

**Required**

- [Authority/DELIVERY_RULES.md](Authority/DELIVERY_RULES.md) — commands, locked milestone order, hard guards, verification matrix
- [Runtime/PROJECT_STATE.md](Runtime/PROJECT_STATE.md) — active OpenSpec list
- [Runtime/CURRENT_WORK.md](Runtime/CURRENT_WORK.md) — **implementation scope**
- `openspec/changes/<active-name>/tasks.md` for the change you implement

**Optional**

- [plans/openspec_integration_overview.md](Plans/openspec_integration_overview.md)
- `openspec/specs/` for archived normative behavior
- `.cursor/rules/OpenSpec-Workflow.mdc`

**Avoid**

- Treating `OpenSpec-Workflow.mdc` archive laundry lists as the active work queue — use PROJECT_STATE
- Starting D13 / `jam-recording` without JamRecorder + M10 (parked: `archive/20260617-parked-jam-recording-d13/`)
- Copying [LOOP_MIDI_STORAGE_AND_VALIDATION.md](Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) into new proposal text — cite and link

---

## Recording (capture / record pass)

**Required**

- [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- [plans/record_overdub_memory_display_timeline_enhancement.md](Plans/record_overdub_memory_display_timeline_enhancement.md)
- `openspec/specs/timeline-epochs/` (if present)
- `Track`, `Loop`, `LoopEventStore` headers

**Optional**

- [Guides/HOST_MIDI_AUTOMATION_BASELINE.md](Guides/HOST_MIDI_AUTOMATION_BASELINE.md)
- `.cursor/rules/HITL-Test-Flow.mdc`

**Avoid (unless bisecting history)**

- Pre-passes migration plans in `Refinements/`
- Phase 3 jam capture specs (not implemented)

---

## Playback

**Required**

- [Authority/Architecture/RuntimeArchitecture.md](Authority/Architecture/RuntimeArchitecture.md) — layers and revision chain
- [Authority/Architecture/Playback.md](Authority/Architecture/Playback.md)
- [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) — materialize / merge paths
- `TrackManager`, `LoopPasses` materialize APIs

**Optional**

- `openspec/specs/timeline-passes/`
- [Guides/NOTE_WRAPPING_LOGIC.md](Guides/NOTE_WRAPPING_LOGIC.md)

**Avoid**

- Display-only wrap docs as sole source for playback timing

---

## Persistence (Current workspace / Sets / SD)

**Required**

- [Authority/ARCHITECTURE_RULES.md](Authority/ARCHITECTURE_RULES.md) — StorageManager ownership
- [Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md](Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) — unified RAM + SD model; continuous persistence proposal
- [Guides/DEFERRED_RUNTIME_PERSISTENCE.md](Guides/DEFERRED_RUNTIME_PERSISTENCE.md)
- Active OpenSpec: `openspec/changes/continuous-runtime-persistence/` (DEC-020), `set-revision-persistence/`, `workspace-session-persistence/` (check `tasks.md`)
- [plans/set_revision_persistence_handoff.md](Plans/set_revision_persistence_handoff.md)

**Optional**

- [plans/workspace_session_persistence_handoff.md](Plans/workspace_session_persistence_handoff.md)
- `StorageManager.h`, `SetBrowserOverlayPolicy.h`

**Avoid**

- Legacy SavedSet-only plans superseded by revision model
- UI mock plans without storage spec cross-check

---

## Display

**Required**

- [Authority/Architecture/RuntimeArchitecture.md](Authority/Architecture/RuntimeArchitecture.md)
- [Authority/Architecture/Display.md](Authority/Architecture/Display.md)
- [Authority/Architecture/DerivedViews.md](Authority/Architecture/DerivedViews.md) — representation vs interval
- [Guides/control-surface/Display.md](Guides/control-surface/Display.md)
- [Authority/ARCHITECTURE_RULES.md](Authority/ARCHITECTURE_RULES.md) — DisplayManager ownership
- `DisplayManager` for draw entry points

**Optional**

- `openspec/changes/long-loop-piano-roll-window/`
- `openspec/changes/save-status-display/`
- [plans/record_overdub_memory_display_timeline_enhancement.md](Plans/record_overdub_memory_display_timeline_enhancement.md) (display section)

**Avoid**

- Using `docs/Plans/*display*.plan.md` from 2025 as authority over current `DisplayManager` behavior

---

## Storage schema (loop slots / passes / chunks)

**Required**

- [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- `StorageLoopIo`, `LoopEventStore`
- `openspec/specs/timeline-passes/` when touching edit passes

**Optional**

- [Guides/DEFERRED_RUNTIME_PERSISTENCE.md](Guides/DEFERRED_RUNTIME_PERSISTENCE.md)
- Archived `pool-budget`, `loop-ownership-hardening` specs

**Avoid**

- v2/v3 migration summaries in `Refinements/` as current format truth

---

## Undo

**Required**

- [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) — undo routing
- `TrackUndo.cpp`, `EditManager` session undo
- `openspec/specs/note-edit-session-undo/` (if touching edit undo)

**Optional**

- [plans/reduce_undo_and_lazy_loop_b89758a6.plan.md](Plans/reduce_undo_and_lazy_loop_b89758a6.plan.md) (historical)

**Avoid**

- Assuming global undo depth without reading `PREFERRED_UNDO_DEPTH` / `trimGlobalUndoStackForMemory`

---

## Input (MIDI buttons / GPIO / faders)

**Required**

- [Guides/CODE_STRUCTURE.md](Guides/CODE_STRUCTURE.md)
- [Guides/MIDI_CONFIG_GUIDE.md](Guides/MIDI_CONFIG_GUIDE.md)
- Relevant control-surface row guide under [Guides/control-surface/](Guides/control-surface/)
- NOTE_EDIT motorized faders: [Guides/DROID_MOTORFADER_PITCHBEND.md](Guides/DROID_MOTORFADER_PITCHBEND.md)

**Optional**

- `MidiButtonActions.cpp`, `GpioButtonManager.cpp`
- Row-specific OpenSpec (e.g. `loop-slot-buttons`)

**Avoid**

- Duplicating gesture tables from README without checking `MidiButtonConfig.cpp`

---

## Timing / clock

**Required**

- `ClockManager`
- [Authority/ARCHITECTURE_RULES.md](Authority/ARCHITECTURE_RULES.md)

**Optional**

- [Guides/jam-bar-step-phases.md](Guides/jam-bar-step-phases.md)
- BPM display plans (display-only)

**Avoid**

- DAW sync compensation notes in README as firmware clock bug hypotheses

---

## Note edit

**Required**

- [Guides/MOVE_NOTE_LOGIC.md](Guides/MOVE_NOTE_LOGIC.md) — current geometry / participation / leave-restore / commit workflow
- [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- `EditManager`, `NoteGeometryResolver`, `NoteEditCurrentState`, `ControlSurfaceManager`, `EditStates/`
- [NAMING.md](Authority/NAMING.md)
- `.cursor/rules/HITL-Edit-Test-Flow.mdc`
- DEC-029 / DEC-030 in [DECISION_LOG.md](DECISION_LOG.md)

**Optional**

- [plans/note_edit_resolver_authority_contracts_refinement.md](Plans/note_edit_resolver_authority_contracts_refinement.md) — **complete** (DEC-029/030; §12 R1–R5)
- `openspec/specs/edit-session-action-geometry/`
- `openspec/specs/note-edit-modification-session/`
- `openspec/specs/note-edit-current-state/` — archived change [`2026-08-08-note-edit-current-state`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/)
- Overlap BUG archives under `openspec/changes/archive/`

**Avoid**

- Treating Focus `changedOverlapNoteIds` as authority (removed — DEC-030 / §11 step 5.5)
- Pre-`NoteEditCurrentState` overlap docs that infer session semantics from live store alone
- Imperative `findOverlaps` / restore-first chains as the live geometry engine

---

## Loop edit (start / length / jam region)

**Required**

- [Guides/LOOP_START_EDITING.md](Guides/LOOP_START_EDITING.md)
- [Guides/jam-bar-step-phases.md](Guides/jam-bar-step-phases.md)
- `LoopEditManager`, jam fields on `Track`

**Optional**

- [Guides/control-surface/Bars-and-16ths.md](Guides/control-surface/Bars-and-16ths.md)

---

## Clip / slot management (multi-loop)

**Required**

- [Guides/control-surface/Loops.md](Guides/control-surface/Loops.md)
- `SlotStateMachine`, `TrackManager` slot APIs
- `openspec/specs/multi-loop-slots/` (if present)

**Optional**

- [plans/phase-3-multi-loop.md](Plans/phase-3-multi-loop.md) — roadmap only

**Avoid**

- Treating Phase 3 jam **capture** as shipped

---

## Export / import (Set revision / workspace)

**Required**

- Active OpenSpec under `set-revision-persistence`, `workspace-session-persistence`
- [plans/set_revision_persistence_handoff.md](Plans/set_revision_persistence_handoff.md)
- `RevisionLoadPolicy`, catalog types in `include/`

**Optional**

- [plans/set_revision_persistence_architecture_enhancement.md](Plans/set_revision_persistence_architecture_enhancement.md)

---

## Agent procedure

1. **Detect domain** from user request and touched file paths (`rg` / semantic search).
2. Run [decision ladder](Authority/ARCHITECTURE_RULES.md#progress-bias-and-decision-ladder) — default **implement** unless [formal trigger](ARCHITECTURE_REASSESSMENT.md#formal-triggers).
3. **Load required docs** for that domain (+ PROJECT_STATE, CURRENT_WORK).
4. **Brief context summary** (owner, change, tests) — full [PREFLIGHT](Templates/PREFLIGHT.md) only if triggered.
5. **Implement** — propose path and confidence when alternatives exist; do not stop on vagueness alone.
