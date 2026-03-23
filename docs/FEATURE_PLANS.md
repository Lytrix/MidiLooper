# Feature plans (versioned history)

This file indexes **design, phase, and implementation summaries** in `docs/` next to the **reference guides** (how systems work today), e.g. [LOOP_START_EDITING.md](LOOP_START_EDITING.md), [FADER_STATE_SYSTEM.md](FADER_STATE_SYSTEM.md), [MOVE_NOTE_LOGIC.md](MOVE_NOTE_LOGIC.md).

**Cursor-generated `.plan.md` files** are archived under **[`plans/`](../plans/README.md)** in the repo root. **Phase 3 (multi-loop) requirements:** [plans/phase-3-multi-loop.md](../plans/phase-3-multi-loop.md) — **Scope 1:** 8 loops per track; save/data design still anticipates **more slots** and **large per-loop storage**. Copy from `~/.cursor/plans/` when you add new exports.

## Conventions

- One topic per file. Prefer descriptive names (kebab-case for new files; older files keep their historical names).
- When a plan is superseded, **keep the old file** and add a new one or append a dated section at the top (e.g. `## 2025-03-14 — revision`).
- For full Cursor plan exports, prefer committing copies under `plans/` (see [plans/README.md](../plans/README.md)).

## Index

| Document | Summary |
|----------|---------|
| [jam-bar-step-phases.md](jam-bar-step-phases.md) | Jam state, bar select, HOLD_TWO, `jamTick` — phases and post-fixes |
| [LOOP_START_IMPLEMENTATION_SUMMARY.md](LOOP_START_IMPLEMENTATION_SUMMARY.md) | Loop start editing — implementation phases and issues resolved |
| [REDO_IMPLEMENTATION_SUMMARY.md](REDO_IMPLEMENTATION_SUMMARY.md) | Redo via triple press — implementation summary |
| [FADER_REFACTOR_SUMMARY.md](FADER_REFACTOR_SUMMARY.md) | Fader subsystem refactor summary |
| [MIDI_BUTTON_REFACTOR_SUMMARY.md](MIDI_BUTTON_REFACTOR_SUMMARY.md) | MIDI button architecture refactor summary |
| [MIGRATION_TO_V2_SUMMARY.md](MIGRATION_TO_V2_SUMMARY.md) | Migration to MidiButtonManager V2 |
| [COMPILATION_FIX_SUMMARY.md](COMPILATION_FIX_SUMMARY.md) | Historical compilation fix notes |
| [DRY_REFACTORING_EXAMPLES.md](DRY_REFACTORING_EXAMPLES.md) | DRY refactoring patterns / examples |
| [OPTIMIZATION_ANALYSIS.md](OPTIMIZATION_ANALYSIS.md) | Performance optimization analysis |

Add new rows when you add files.
