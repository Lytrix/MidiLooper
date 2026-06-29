# Documentation index

The **story and layout** of the looper start at the **[root `README.md`](../README.md)** (elevator pitch → how to play → reference grid → technical links). **Guides** (current behavior) live in **[`Guides/`](Guides/)**. **Refinements** (implementation logs) live in **[`Refinements/`](Refinements/)** and may not match the code line-for-line today.

**Cursor plan exports** and phased design archives: **[`plans/`](plans/README.md)**.

## Agent context harness (governance layer)

Does **not** change firmware — process and documentation for coding agents.

| Document | Role |
|----------|------|
| [**00-authority/**](00-authority/README.md) | Authority hierarchy: intent → architecture → delivery |
| [runtime/PROJECT_STATE.md](runtime/PROJECT_STATE.md) | Execution context — **load first** |
| [runtime/CURRENT_WORK.md](runtime/CURRENT_WORK.md) | Implementation scope (now / not now) — **required before coding** |
| [runtime/ROADMAP.md](runtime/ROADMAP.md) | Future milestones — informational only |
| [DECISION_LOG.md](DECISION_LOG.md) | Accepted / superseded decisions — **search before new abstractions** |
| [templates/DECISION_REVIEW.md](templates/DECISION_REVIEW.md) | Mandatory historical review before firmware implementation |
| [AGENT_CONTEXT_MAP.md](AGENT_CONTEXT_MAP.md) | Domain → required docs |
| [ARCHITECTURE_REASSESSMENT.md](ARCHITECTURE_REASSESSMENT.md) | When to pause for design review |
| [templates/PREFLIGHT.md](templates/PREFLIGHT.md) | Planning template before implementation |
| [templates/OWNERSHIP_TRANSFER.md](templates/OWNERSHIP_TRANSFER.md) | Ownership move proposal (with removal schedule) |
| [templates/SESSION_CLOSEOUT.md](templates/SESSION_CLOSEOUT.md) | Before closing design-heavy chats |
| [agents/](agents/architect.md) | Architect / builder / reviewer roles |

Cursor rule: `.cursor/rules/Agent-Context-Workflow.mdc`

---

## Guides

| Document | Summary |
|----------|---------|
| [CODE_STRUCTURE.md](Guides/CODE_STRUCTURE.md) | Handler / Manager / Processor / Actions naming and module overview |
| [**LOOP_MIDI_STORAGE_AND_VALIDATION.md**](Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) | **Capture / passes (record, overdub, edit), commitCapturePass, validation tiers, undo stacks, SD v4** — read before touching Loop/Track undo or stop paths |
| [**record_overdub_memory_display_timeline_enhancement.md**](plans/record_overdub_memory_display_timeline_enhancement.md) | **Record/overdub pipeline overview** — Mermaid timelines: capture → external memory pool (external RAM / PSRAM) → playback → OLED → deferred SD |
| [DEFERRED_RUNTIME_PERSISTENCE.md](Guides/DEFERRED_RUNTIME_PERSISTENCE.md) | Central deferred save routing and chunk-bounded SD writer stages |
| [MIDI_CONFIG_GUIDE.md](Guides/MIDI_CONFIG_GUIDE.md) | Remapping channels, notes, and CCs; quick reference tables |
| [**control-surface/**](Guides/control-surface/) | Per-row notes: Scenes, Tracks, Jams, Loops, Bars/16ths, Main controls, Faders, Display |
| [LOOP_START_EDITING.md](Guides/LOOP_START_EDITING.md) | Loop start point editing (live / fader) |
| [FADER_STATE_SYSTEM.md](Guides/FADER_STATE_SYSTEM.md) | Fader state machine and hardware feedback |
| [MOVE_NOTE_LOGIC.md](Guides/MOVE_NOTE_LOGIC.md) | Note movement and overlap resolution |
| [NOTE_WRAPPING_LOGIC.md](Guides/NOTE_WRAPPING_LOGIC.md) | Note and loop wrap-around behavior |
| [jam-bar-step-phases.md](Guides/jam-bar-step-phases.md) | Jam loops: bar/16th buttons, `jamTick`, HOLD_TWO, playback regions |
| [MANUAL_TEST_CAPTURE_SESSION.md](Guides/MANUAL_TEST_CAPTURE_SESSION.md) | Capture session script for known bugs (instrumented `teensy41-capture-serial` build, **DebugSessionCapture** `#CAP` lines) |

Add new topical guides under `Guides/` and extend this table.

### control-surface (row guides)

| Document | Summary |
|----------|---------|
| [Scenes.md](Guides/control-surface/Scenes.md) | Scenes row — roadmap / Phase 3 |
| [Tracks.md](Guides/control-surface/Tracks.md) | Track row gestures (select / mute / solo) |
| [Jams.md](Guides/control-surface/Jams.md) | Jam behavior + Jams row placeholder / capture roadmap |
| [Loops.md](Guides/control-surface/Loops.md) | Loop slot row — record, overdub, hold layer |
| [Bars-and-16ths.md](Guides/control-surface/Bars-and-16ths.md) | Bar / 16th jam and seek |
| [Main-controls.md](Guides/control-surface/Main-controls.md) | REC/PLAY, MUTE/DE, edit mode, NOTELEN, transport |
| [Faders.md](Guides/control-surface/Faders.md) | Slider roles NOTE_EDIT vs LOOP_EDIT |
| [Display.md](Guides/control-surface/Display.md) | OLED / LCD / track column |

---

## Refinements

| Document | Summary |
|----------|---------|
| [LOOP_START_IMPLEMENTATION_SUMMARY.md](Refinements/LOOP_START_IMPLEMENTATION_SUMMARY.md) | Loop start feature — implementation phases and fixes |
| [REDO_IMPLEMENTATION_SUMMARY.md](Refinements/REDO_IMPLEMENTATION_SUMMARY.md) | Redo (e.g. triple-press) — implementation summary |
| [FADER_REFACTOR_SUMMARY.md](Refinements/FADER_REFACTOR_SUMMARY.md) | Fader subsystem refactor |
| [MIDI_BUTTON_REFACTOR_SUMMARY.md](Refinements/MIDI_BUTTON_REFACTOR_SUMMARY.md) | MIDI button pipeline refactor |
| [MIGRATION_TO_V2_SUMMARY.md](Refinements/MIGRATION_TO_V2_SUMMARY.md) | Migration to MidiButtonManager-style layout |
| [COMPILATION_FIX_SUMMARY.md](Refinements/COMPILATION_FIX_SUMMARY.md) | Historical build / compile notes |
| [DRY_REFACTORING_EXAMPLES.md](Refinements/DRY_REFACTORING_EXAMPLES.md) | DRY patterns / examples from refactors |
| [OPTIMIZATION_ANALYSIS.md](Refinements/OPTIMIZATION_ANALYSIS.md) | Performance optimization notes |
| [OPTIMIZATION_EXAMPLE.md](Refinements/OPTIMIZATION_EXAMPLE.md) | Optimization code example |

---

## Also in this folder

| Document | Role |
|----------|------|
| [00-authority/PROJECT_INTENT.md](00-authority/PROJECT_INTENT.md) | **Canonical project goal and decision log** — read this first; conflicts resolve in its favor |
| [FEATURES.md](FEATURES.md) | Full technical feature checklist (also linked from root `README`) |
| [FEATURE_PLANS.md](FEATURE_PLANS.md) | Conventions, **Phase 3 / multi-loop** pointer |
| [plans/README.md](plans/README.md) | Design exports and phase specs (`docs/plans/`) |
| [DELIVERABLE_TRACKING.md](DELIVERABLE_TRACKING.md) | Single overview for main (plans) vs refinements |

When you add new guides, place them in `Guides/` or `Refinements/` and add a row to the matching table above.
