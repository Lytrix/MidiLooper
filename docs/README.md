# Documentation index

The **story and layout** of the looper start at the **[root `README.md`](../README.md)** (elevator pitch → how to play → reference grid → technical links).

**Firmware versions:** v1 = `main`, v2 = `midi-faders`, v3 = `dev` (default). See [**BRANCHING.md**](BRANCHING.md). Control-surface guides are **v3 only**.

---

## Four documentation buckets

| Bucket | Location | Role |
|--------|----------|------|
| **Authority** | [`Authority/`](Authority/README.md) | What the system is — intent, architecture, naming, delivery rules |
| **Runtime** | [`Runtime/`](Runtime/CURRENT_WORK.md) | **What to work on now** — [`CURRENT_WORK.md`](Runtime/CURRENT_WORK.md) is the sole implementation queue |
| **Guides** | [`Guides/`](Guides/) | Living behavior and how-to (shipped firmware) |
| **Plans** | [`Plans/`](Plans/README.md) | Proposed or historical design — **not** implementation authority |

**Deliverable tracking:** [`DELIVERABLE_TRACKING.md`](DELIVERABLE_TRACKING.md) records shipped vs next at deliverable level; it does **not** set implementation priority (use `CURRENT_WORK`).

**Historical implementation logs:** [`Refinements/`](Refinements/) (redirect index) → [`Plans/archive/refinements/`](Plans/archive/refinements/) — may not match code line-for-line; prefer Guides for current behavior.

---

## Agent context harness (governance layer)

Does **not** change firmware — process and documentation for coding agents.

| Document | Role |
|----------|------|
| [**Authority/**](Authority/README.md) | Authority hierarchy: intent → architecture → naming → delivery |
| [Authority/NAMING.md](Authority/NAMING.md) | Architectural vocabulary, concept boundaries, migration policy |
| [Authority/WORKFLOW_LIFECYCLE.md](Authority/WORKFLOW_LIFECYCLE.md) | Discovery → decision → GitHub / plan / OpenSpec → verify → docs closeout |
| [Authority/GITHUB_WORK_TRACKING.md](Authority/GITHUB_WORK_TRACKING.md) | GitHub Issues / Project — work inventory (not execution authority) |
| [Authority/DOCUMENTATION_CLOSEOUT.md](Authority/DOCUMENTATION_CLOSEOUT.md) | Docs must match shipped behavior before closing work |
| [Runtime/PROJECT_STATE.md](Runtime/PROJECT_STATE.md) | Execution context — **load first** |
| [Runtime/CURRENT_WORK.md](Runtime/CURRENT_WORK.md) | Implementation scope (now / not now) — **required before coding** |
| [BRANCHING.md](BRANCHING.md) | v1 / v2 / v3 branches, feature workflow, local archive refs |
| [Runtime/ROADMAP.md](Runtime/ROADMAP.md) | Future milestones — informational only |
| [DECISION_LOG.md](DECISION_LOG.md) | Accepted / superseded decisions — **search before new abstractions** |
| [Templates/DECISION_REVIEW.md](Templates/DECISION_REVIEW.md) | Mandatory historical review before firmware implementation |
| [AGENT_CONTEXT_MAP.md](AGENT_CONTEXT_MAP.md) | Domain → required docs |
| [ARCHITECTURE_REASSESSMENT.md](ARCHITECTURE_REASSESSMENT.md) | When to pause for design review |
| [Templates/PREFLIGHT.md](Templates/PREFLIGHT.md) | Planning template before implementation |
| [Templates/OWNERSHIP_TRANSFER.md](Templates/OWNERSHIP_TRANSFER.md) | Ownership move proposal (with removal schedule) |
| [Templates/SESSION_CLOSEOUT.md](Templates/SESSION_CLOSEOUT.md) | Before closing design-heavy chats |
| [agents/](Agents/architect.md) | Architect / builder / reviewer roles |

Cursor rule: `.cursor/rules/Agent-Context-Workflow.mdc`

---

## Guides

| Document | Summary |
|----------|---------|
| [CODE_STRUCTURE.md](Guides/CODE_STRUCTURE.md) | Module map; suffix naming → [NAMING.md](Authority/NAMING.md) |
| [**LOOP_MIDI_STORAGE_AND_VALIDATION.md**](Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) | **Capture / passes (record, overdub, edit), commitCapturePass, validation tiers, undo stacks, SD v4** — read before touching Loop/Track undo or stop paths |
| [OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md](Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md) | Overdub overlap candidate / classify / `resolveConstrainedGeometry` / pending / seal — update when those owners change |
| [OVERDUB_LEDGER_NOTE_EVALUATIONS.md](Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md) | Open ledger identities, catch-up vs clock, wrap exclusion, playback-order — update when those owners change |
| [**record_overdub_memory_display_timeline_enhancement.md**](Plans/record_overdub_memory_display_timeline_enhancement.md) | **Record/overdub pipeline overview** — Mermaid timelines: capture → external memory pool (external RAM / PSRAM) → playback → OLED → deferred SD |
| [DEFERRED_RUNTIME_PERSISTENCE.md](Guides/DEFERRED_RUNTIME_PERSISTENCE.md) | Central deferred save routing and chunk-bounded SD writer stages |
| [MIDI_CONFIG_GUIDE.md](Guides/MIDI_CONFIG_GUIDE.md) | Remapping channels, notes, and CCs; quick reference tables (**v3 / `dev` default DROID mapping**) |
| [**control-surface/**](Guides/control-surface/) | Per-row notes: Scenes, Tracks, Jams, Loops, Bars/16ths, Main controls, Faders, Display (**v3 only**) |
| [LOOP_START_EDITING.md](Guides/LOOP_START_EDITING.md) | Loop start point editing (live / fader) |
| [FADER_STATE_SYSTEM.md](Guides/FADER_STATE_SYSTEM.md) | Fader state machine; § NOTE_EDIT motor feedback (2026) |
| [DROID_MOTORFADER_PITCHBEND.md](Guides/DROID_MOTORFADER_PITCHBEND.md) | DROID motorized fader scale, NOTE_EDIT arm, select/geometry motor sync, HITL probe |
| [HITL_TEST_SCENARIOS.md](Guides/HITL_TEST_SCENARIOS.md) | Hardware-in-the-loop presets and serial verifiers |
| [MOVE_NOTE_LOGIC.md](Guides/MOVE_NOTE_LOGIC.md) | NOTE_EDIT movement and overlap resolution; shared classifier / resolver with overdub — see overlap-resolve catalog |
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

Historical implementation logs — archived 2026-08-08. See [`Refinements/README.md`](Refinements/README.md) or [`Plans/archive/refinements/`](Plans/archive/refinements/).

| Document | Summary |
|----------|---------|
| [LOOP_START_IMPLEMENTATION_SUMMARY.md](Plans/archive/refinements/LOOP_START_IMPLEMENTATION_SUMMARY.md) | Loop start feature — implementation phases and fixes |
| [REDO_IMPLEMENTATION_SUMMARY.md](Plans/archive/refinements/REDO_IMPLEMENTATION_SUMMARY.md) | Redo (e.g. triple-press) — implementation summary |
| [FADER_REFACTOR_SUMMARY.md](Plans/archive/refinements/FADER_REFACTOR_SUMMARY.md) | Fader subsystem refactor |
| [MIDI_BUTTON_REFACTOR_SUMMARY.md](Plans/archive/refinements/MIDI_BUTTON_REFACTOR_SUMMARY.md) | MIDI button pipeline refactor |
| [MIGRATION_TO_V2_SUMMARY.md](Plans/archive/refinements/MIGRATION_TO_V2_SUMMARY.md) | Migration to MidiButtonManager-style layout |
| [COMPILATION_FIX_SUMMARY.md](Plans/archive/refinements/COMPILATION_FIX_SUMMARY.md) | Historical build / compile notes |
| [DRY_REFACTORING_EXAMPLES.md](Plans/archive/refinements/DRY_REFACTORING_EXAMPLES.md) | DRY patterns / examples from refactors |
| [OPTIMIZATION_ANALYSIS.md](Plans/archive/refinements/OPTIMIZATION_ANALYSIS.md) | Performance optimization notes |
| [OPTIMIZATION_EXAMPLE.md](Plans/archive/refinements/OPTIMIZATION_EXAMPLE.md) | Optimization code example |

---

## Root-level docs (roles)

These files sit at `docs/` for discoverability. **Only [`Runtime/CURRENT_WORK.md`](Runtime/CURRENT_WORK.md) sets implementation priority.**

| Document | Role | Not |
|----------|------|-----|
| [Authority/PROJECT_INTENT.md](Authority/PROJECT_INTENT.md) | Canonical **why** — product identity, litmus tests | Implementation queue |
| [Runtime/CURRENT_WORK.md](Runtime/CURRENT_WORK.md) | **What to work on now** | Deliverable history |
| [DELIVERABLE_TRACKING.md](DELIVERABLE_TRACKING.md) | Shipped vs next at **deliverable** level | Second work queue |
| [FEATURES.md](FEATURES.md) | Technical capability **checklist** (onboarding / README) | Authority; verify behavior in Guides |
| [FEATURE_PLANS.md](FEATURE_PLANS.md) | Plan **conventions**; Phase 3 pointer | Implementation queue |
| [PROJECT_INTENT.md](PROJECT_INTENT.md) | Redirect stub → `Authority/PROJECT_INTENT.md` | Duplicate intent source |
| [Plans/README.md](Plans/README.md) | Design history conventions; archive policy | Plan manifest or index |

---

## Also in this folder

| Document | Role |
|----------|------|
| [DECISION_LOG.md](DECISION_LOG.md) | Accepted / superseded decisions — search before new abstractions |
| [ARCHITECTURE_REASSESSMENT.md](ARCHITECTURE_REASSESSMENT.md) | When to pause for design review |
| [AGENT_CONTEXT_MAP.md](AGENT_CONTEXT_MAP.md) | Domain → required docs |

When you add new guides, place them in `Guides/` and add a row to the Guides table above. New implementation summaries belong in `Plans/` (active) or `Plans/archive/refinements/` (historical).
