# Documentation index

**Guides** (current behavior) live in **[`Guides/`](Guides/)**. **Refinements** (implementation logs) live in **[`Refinements/`](Refinements/)**. They may not match the code line-for-line today.

**Cursor plan exports** and phased design archives: **[`plans/`](../plans/README.md)** in the repo root.

---

## Guides

| Document | Summary |
|----------|---------|
| [DESIGN_PRINCIPLES.md](Guides/DESIGN_PRINCIPLES.md) | Gesture-first UX; track row vs loop slot buttons; minimal-button goals; MIDI-controllable design |
| [MIDI_CONFIG_GUIDE.md](Guides/MIDI_CONFIG_GUIDE.md) | Remapping channels, notes, and CCs; where config lives; quick reference |
| [LOOP_START_EDITING.md](Guides/LOOP_START_EDITING.md) | Loop start point editing (live / fader) |
| [FADER_STATE_SYSTEM.md](Guides/FADER_STATE_SYSTEM.md) | Fader state machine and hardware feedback |
| [MOVE_NOTE_LOGIC.md](Guides/MOVE_NOTE_LOGIC.md) | Note movement and overlap resolution |
| [NOTE_WRAPPING_LOGIC.md](Guides/NOTE_WRAPPING_LOGIC.md) | Note and loop wrap-around behavior |
| [jam-bar-step-phases.md](Guides/jam-bar-step-phases.md) | Jam loops: bar/16th buttons, `jamTick`, HOLD_TWO, playback regions |
| [MANUAL_TEST_BAR_STEP_BUTTONS.md](Guides/MANUAL_TEST_BAR_STEP_BUTTONS.md) | Manual test notes for bar/step controls |

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
| [FEATURE_PLANS.md](FEATURE_PLANS.md) | Conventions, **Phase 3 / multi-loop** pointer, link to [`plans/`](../plans/README.md) |

When you add files, place them in `Guides/` or `Refinements/` and add a row to the matching table above.
