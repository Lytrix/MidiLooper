# Deliverable Tracking (Main vs Refinement)

This repo uses two different “deliverable” styles in Markdown:

1. **Main deliverables (plan slices)** are exported by Cursor into `plans/*.plan.md`. These files usually include a `todos:` block with slice IDs and `status:` values.
2. **Bug fix / refinement deliverables** are summarized in `Refinements/*.md` (typically `*_SUMMARY.md`), and their state is tracked manually.

This file is meant to be the **single overview** you consult to avoid losing track between the “big” plan slices and the smaller implementation summaries.

---

## Semantic Map (use these names consistently)

The goal here is to map the concepts you listed to the codebase’s existing subsystem names (so items line up with modules, functions, and existing docs).

| Your concept | Canonical subsystem name | Key entry points (proven in code/docs) |
|---|---|---|
| `Piano roll Display` | `DisplayManager` | `DisplayManager` (SSD1322 UI renderer); `DisplayManager::drawPianoRoll(...)` |
| `Note/Loop information display` | `DisplayManager` | `DisplayManager::drawAllNotes(...)`, `DisplayManager::drawNoteInfo(...)`, `DisplayManager::drawBracket(...)`, `DisplayManager::drawInfoArea(...)` |
| `Track State/Select display` | `DisplayManager` + `TrackManager` | `DisplayManager::drawTrackStatus(...)` (track letters); `TrackManager::getTrackState(...)` provides the `TrackState` used for rendering |
| `Midi Button` | `MidiButtonManager` (+ `MidiButtonProcessor` / `MidiButtonActions`) | `MidiButtonManager` delegates to `MidiButtonProcessor` (gesture detection) and `MidiButtonActions` (executing edit/record/undo operations); button configuration lives in `Utils/MidiButtonConfig.*` |
| `Midi Leds` | `MidiLedManager` | `MidiLedManager::updateLeds(...)`, `MidiLedManager::updateTrackSelectLeds(...)`, `MidiLedManager::updateCurrentTick(...)`, `MidiLedManager::clearAllLeds()` |
| `Tight Midi clocking` | `ClockManager` | `ClockManager::onMidiClockPulse()`, `ClockManager::onMidiStart()`, `ClockManager::onMidiStop()`, and `ClockManager::updateInternalClock()` |
| `Loop wrapping` | `NoteUtils` + `NoteMovementUtils` | Wrapping/reconstruction uses `NoteUtils::reconstructNotes(...)` (note-on discard vs note-off wrap); movement overlap/wrap logic is in `NoteMovementUtils` (e.g. `wrapPosition(...)`, `calculateNoteLength(...)`, overlap detection) |
| `Note/Loop cached note reconstruction (complex cache + update system)` | `NoteUtils::CachedNoteList` (via `Track::getCachedNotes*`) | Cache + invalidation uses `NoteUtils::CachedNoteList::getNotes(...)` (hash of MIDI events + loopLength); accessors are `Track::getCachedNotes()` and `Track::getCachedNotesForSlot()`; caches can be invalidated with `Track::invalidateCaches()` |
| `Note selecting, editing` | `NoteEditManager` + `EditManager` + `EditStates` | `NoteEditManager` routes MIDI note/fader inputs; movement + identity is managed by `EditManager`; individual edit behaviors live in `src/EditStates/*` (select/start/length/pitch) |
| `Undo system for Note edits/Loop edits` | `TrackUndo` | `TrackUndo` provides `pushUndoSnapshot(...)`, `undoOverdub(...)`, `redoOverdub(...)`, plus clear and loop-start undo/redo via `pushLoopStartSnapshot(...)` / `undoLoopStart(...)` / `redoLoopStart(...)` |
| `1 Redo` | `TrackUndo` | Redo is part of `TrackUndo`: `redoOverdub(...)` and `redoClearTrack(...)` |
| `Storage Load/Save logic for Loops, State machine` | `StorageManager` + `LooperStateManager` | Persistence is `StorageManager::saveState(...)` / `StorageManager::loadState(...)`; global state + overlays are in `LooperStateManager` (see `LooperState` enum + edit/settings overlays) |
| `MidiClock, Ports, Midi In/Out` | `MidiHandler` (+ `ClockManager`) | MIDI IO routing is `MidiHandler` (`handleMidiInput()`, `handleMidiMessage(...)`); clock pulses route into `ClockManager` via `clockManager.onMidiClockPulse()` |

---

## Deliverables: how to track them (minimal rules)

### Main deliverables (Cursor plan slices)
- Source files: `plans/*.plan.md`
- In each plan file, use the `todos:` entries as the slice/status list (the `id` is your stable “deliverable key”, and `status:` is `pending` / `completed`).
- When you complete a slice in the code, update the corresponding `status:` in the plan file (and then reflect it in this overview if you maintain an external table).

### Bug fix / refinement deliverables
- Source files: `Refinements/*.md`
- Update the refinement file directly when the implementation is done.
- Use this overview to link which refinement docs are “done” for which deliverable area.

---

## Main Deliverables (MVP first)

This table is your “main vs sub deliverables” view. For most MVP items we only track **what** you want and the **code areas** it touches; when there is already a well-documented plan with slice IDs (Phase 3 jam recording), we include the detailed slice mapping.

### Deliverable Overview
| Deliverable | Canonical code areas (from this repo) | Spec / plan docs | State |
|---|---|---|---|
| MVP: Display + Button workflow to load/save sessions | `DisplayManager`, `MidiButtonActions` (session triggers), `StorageManager` + `LooperStateManager` | Needs a plan doc export for the actual UX flow | Needs spec (not found as an existing slice doc) |
| MVP: Memory expansion (PSRAM/OSRAM) to increase undo + loop capacity (>= 11000 notes) | `TrackUndo`, `Utils/MemoryPool` / `PooledMidiEventVector`, `Track`/`Loop` MIDI event storage | Needs a plan doc export for the memory allocation + storage strategy | Needs spec (not found as an existing slice doc) |
| MVP: Display CC values + edit them using the faders | `DisplayManager` (info rendering), `NoteEditManager::handleMidiCC`, `MidiFaderManager` / `MidiFaderActions` | (Related concept) `plans/phase-3-multi-loop.md` “cycle MIDI category note vs CC value” goal; no dedicated CC-editor slice doc found | Needs spec (UI + fader mapping still to be defined) |
| Main: Phase 3 jam recording (record loop start / loop selection into other Loops as a live jam) | `Track` jam state (`jamStartTick`, `jamLength`, `jamTick`, `jamPlaybackActive`), `ClockManager`, `TrackManager` (multi-loop capture plumbing), `DisplayManager`, `TrackUndo` | `plans/phase-3-multi-loop.md` (Status: Not implemented) + `plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md` (D13/D14/D15) | Phase 3 not implemented yet; detailed slice mapping below |
| Phase 3 support: slot play/record feedback via Droid LFO pulse (BPM-synced) | `MidiLedManager`, `MidiButtonConfig`/`MidiButtonActions` (slot armed/record transitions), `MidiConfig::LfoPulse` constants (note 70 + slot CC) | `plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md` | Pending: repo has `MidiConfig::LfoPulse` definitions, but no wiring found for note 70 + CC slot gating into any active LFO/LED lane in `src/` |
| Future: Pitch transpose on `Fader4` in Loop mode (and record it into the loop) | `MidiFaderManager` / `MidiFaderActions` (fader routing), `NoteEditManager` edit plumbing, `Track`/`Loop` recording path | Roadmap/placeholder: `Guides/control-surface/Jams.md` “Pitch transposing” | Future (no slice doc found) |
| Future: `Fader3` controls quantization percentage of notes to 16ths | `MidiFaderProcessor` (fader input), `NoteEditManager` + edit states, quantization helpers (`Track::quantizeStart`, tick/16th logic) | No plan doc export found for “quantization percentage” | Future (needs spec) |

### Phase 3 jam recording slice mapping (only detailed one)
From `plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md`:
- `D13` = **Arrangement mode: capture** (jam-era capture into target slot)
- `D14` = **Arrangement mode: playback** (resolve phase 2.3; tests)
- `D15` = **Scenes** (optional capture/persistence of active loop index)
