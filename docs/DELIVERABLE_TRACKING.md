# Deliverable Tracking (Main vs Refinement)

> Project goal and decision log: [Authority/PROJECT_INTENT.md](Authority/PROJECT_INTENT.md). This page is the single shipped-vs-next overview.

## Shipped vs Next (updated Aug 2026)

**Shipped** (in firmware on `dev`):

- 8 tracks × 8 loop slots: per-slot record, overdub, clear, mute, quantized switching, multi-slot hold layering (`Track`, `TrackManager`, `SlotStateMachine`)
- Undo/redo per slot (overdub, clear, loop start) — memory-aware depth (`PREFERRED_UNDO_DEPTH` 99, `MIN_UNDO_DEPTH` 8, `ABSOLUTE_MAX_UNDO_ENTRIES` 512; `trimGlobalUndoStackForMemory` in `PassReclaim` / `TrackUndo`). Redo branch preserved after full undo until a new pass pushes.
- 192 PPQN internal clock, 24 PPQN MIDI sync with internal fallback (`ClockManager`, `ClockSourceStateMachine`)
- Jam regions via Bars/16ths buttons (`BarStepButtonHandler`, jam state on `Track`)
- Piano-roll note editing: select, start, length, pitch, move, wrap (`EditManager`, `EditStates/`, `NoteEditManager`)
- Loop start/length fader editing (`LoopEditManager`)
- SSD1322 OLED piano roll + track strip (`DisplayManager`); controller LED feedback (`MidiLedManager`)
- SD persistence v4 (passes via `StorageLoopIo`) with deferred runtime save (`StorageManager::requestDeferredSaveState` / `processDeferredSaveState`); PSRAM chunk pool + PSRAM-first length-scaling allocators (`LoopEventStore`, `PsramFirstAllocator`)
- **StorageSession persistence refactor (DEC-012, Jun 2026):** job RAM on `storageSession`; FSM split `WorkspaceSave` / `RevisionCommit` / `RevisionLoad` + `Overlay.cpp`; revision-load request vocabulary; normative specs `revision-load`, `storage-session-jobs`, `storage-session-layout`
- DROID USB host MIDI buttons/faders (`MidiHandler`, `MidiButtonManager`, `MidiFaderManager`)
- **Commit-centered lazy slot load (DEC-026/027, Jul 2026):** Phase A archived `2026-07-18-unified-commit-lazy-slot-load` (gates [`224607`](../captures/session_20260718_224607.log), [`230145`](../captures/session_20260718_230145.log)). **Phase B DeferredJobScheduler** archived `2026-07-19-deferred-job-scheduler` (gates [`231510`](../captures/session_20260718_231510.log), [`022107`](../captures/session_20260719_022107.log)); normative `openspec/specs/deferred-job-scheduler/`.
- **NoteEditCurrentState ownership (DEC-029, Aug 2026):** NOTE_EDIT editable geometry keyed by `NoteId`; `EditSession.store` is projection only. OpenSpec archived `2026-08-08-note-edit-current-state`; normative `openspec/specs/note-edit-current-state/`; HITL [`112202`](../captures/session_20260808_112202.log), [`115120`](../captures/session_20260808_115120.log), [`032118`](../captures/session_20260808_032118.log).
- **Sticky overlap participation (DEC-030, Aug 2026):** `NoteEditOverlapParticipationType` on current state; `ParticipatingNotePhase` removed; §12 R1–R5 orthogonal representation complete. Plan: [`note_edit_resolver_authority_contracts_refinement.md`](Plans/note_edit_resolver_authority_contracts_refinement.md).
- **Playing move/length playback audition (Aug 2026):** `refreshPlaybackPreview` forwarded through move/length geometry paths (`15c5750`). HITL [`113626`](../captures/session_20260808_113626.log), [`115120`](../captures/session_20260808_115120.log). Bugfix: [`note_edit_playing_move_audition_bugfix.md`](Plans/note_edit_playing_move_audition_bugfix.md).
- **Overdub pass overlap resolution (DEC-031/032 G2, Aug 2026):** `overdubSourceView` + shared geometry Add/Shorten/Hide; dual-seal companions + STK2 undo. OpenSpec archived `2026-08-12-overdub-pass-overlap-resolution`; normative `openspec/specs/overdub-pass-overlap-resolution/`. Device PASS [`010000`](../captures/session_20260812_010000.log); baseline [`183525`](../captures/session_20260811_183525.log). Firmware on `feature/overdub-pass-overlap-resolution` pending merge to `dev`.

- **Loop content-only history (DEC-035 Layer A, Aug 2026):** content-only persist; load-time GUS fill; empty GUS headers; `LoopPersist`-only overdub stop. OpenSpec archived `2026-08-14-loop-content-history-persistence`; normative `openspec/specs/loop-content-history/`. Device PASS [`030147`](../captures/session_20260814_030147.log), [`032227`](../captures/session_20260814_032227.log). Plan: [`loop_layer_history_persistence_architecture.md`](Plans/loop_layer_history_persistence_architecture.md).

**Next (persistence/runtime):** DEC-037 **LoopContentResolution prototype** — native Stages 0–8 PASS; Stage 9 through 5.15b (flat A picked). 5.7 still FAIL on `startsByTick` emplace ([`143009`](../captures/session_20260815_143009.log)). Next 5.15c device-gate swap. [`loop_content_resolution_span_boundary_index_refinement.md`](Plans/loop_content_resolution_span_boundary_index_refinement.md). DEC-036 D1 eager flatten withdrawn; **3b device PASS** overdub [`045556`](../captures/session_20260814_045556.log) `begin_capture` 2214 µs; undo [`112909`](../captures/session_20260814_112909.log) 3 ms. Overlay track A parked.

**Not in firmware** (docs may suggest otherwise):

- Phase 3 jam/arrangement **capture** (recording jam performance into a new slot), Scenes row, Jams row capture — spec exists ([plans/phase-3-multi-loop.md](Plans/phase-3-multi-loop.md)), slots infrastructure shipped, capture itself not built
- Encoder + 4-button GPIO base module — `ButtonManager` exists but is never called from `main.cpp` (see intent decision 1)
- DROID LFO pulse feedback, CC value editing, Fader3 quantization %, Fader4 pitch transpose, EEPROM config
- 16×2 LCD — driver present, pins disabled in `Globals.h`
- Interactive SD slot hydrate while PLAYING — **PASS** [`224607`](../captures/session_20260718_224607.log) / A.7 [`230145`](../captures/session_20260718_230145.log); OpenSpec archived `2026-07-18-unified-commit-lazy-slot-load`
- `DERIVED_READY` off Commit critical path (optional polish)

---

This repo uses two different “deliverable” styles in Markdown:

1. **Main deliverables (plan slices)** are exported by Cursor into `Plans/*.plan.md`. These files usually include a `todos:` block with slice IDs and `status:` values.
2. **Bug fix / refinement deliverables** — historical summaries in [`Plans/archive/refinements/`](Plans/archive/refinements/) (see [`Refinements/README.md`](Refinements/README.md)); active work in `Plans/`. State tracked manually or via OpenSpec.

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
| `Undo system for Note edits/Loop edits` | `TrackUndo` | `TrackUndo` provides capture-pass undo (`pushRecordPassAdded` / `pushOverdubPassAdded`), **NoteEditPassClosed**, `undoOverdub(...)`, `redoOverdub(...)`, plus clear and loop-start undo/redo via `pushLoopStartSnapshot(...)` / `undoLoopStart(...)` / `redoLoopStart(...)` |
| `1 Redo` | `TrackUndo` | Redo is part of `TrackUndo`: `redoOverdub(...)` and `redoClearTrack(...)` |
| `Storage Load/Save logic for Loops, State machine` | `StorageManager` + `LooperStateManager` | Runtime persistence: `requestDeferredSaveState` → `processDeferredSaveState` (chunk-bounded SD v4 writer); load: `loadState(...)`; maintenance drain: `saveState(...)`; guide: `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`; global state + overlays in `LooperStateManager` |
| `MidiClock, Ports, Midi In/Out` | `MidiHandler` (+ `ClockManager`) | MIDI IO routing is `MidiHandler` (`handleMidiInput()`, `handleMidiMessage(...)`); clock pulses route into `ClockManager` via `clockManager.onMidiClockPulse()` |

---

## Deliverables: how to track them (minimal rules)

### Main deliverables (Cursor plan slices)
- Source files: `Plans/*.plan.md`
- In each plan file, use the `todos:` entries as the slice/status list (the `id` is your stable “deliverable key”, and `status:` is `pending` / `completed`).
- When you complete a slice in the code, update the corresponding `status:` in the plan file (and then reflect it in this overview if you maintain an external table).

### Bug fix / refinement deliverables
- Source files: `Plans/archive/refinements/*.md` (historical); active `Plans/*_refinement.md`
- Update the refinement file directly when the implementation is done.
- Use this overview to link which refinement docs are “done” for which deliverable area.

---

## Main Deliverables (MVP first)

This table is your “main vs sub deliverables” view. For most MVP items we only track **what** you want and the **code areas** it touches; when there is already a well-documented plan with slice IDs (Phase 3 jam recording), we include the detailed slice mapping.

### Deliverable Overview
| Deliverable | Canonical code areas (from this repo) | Spec / plan docs | State |
|---|---|---|---|
| MVP: Display + Button workflow to load/save sessions | `DisplayManager`, `MidiButtonActions` (session triggers), `StorageManager` + `LooperStateManager` | Needs a plan doc export for the actual UX flow | Needs spec (not found as an existing slice doc) |
| MVP: Memory expansion (PSRAM/OSRAM) to increase undo + loop capacity (>= 11000 notes) | `TrackUndo`, `Utils/MemoryPool` / `PooledMidiEventVector`, `Track`/`Loop` MIDI event storage | No plan export exists (referenced `mvp_psram_memory_expansion.plan.md` was never exported) — `ExtMemAllocator` spillover strategy: internal RAM first, PSRAM fallback, no-PSRAM graceful degradation | Implemented: `include/Utils/ExtMemAllocator.h`, `MemoryPool.h`, `Loop.h`, `StorageManager.cpp`, `main.cpp`; unit tests in `test/test_extmem_allocator/` |
| MVP: Display CC values + edit them using the faders | `DisplayManager` (info rendering), `NoteEditManager::handleMidiCC`, `MidiFaderManager` / `MidiFaderActions` | (Related concept) `Plans/phase-3-multi-loop.md` “cycle MIDI category note vs CC value” goal; no dedicated CC-editor slice doc found | Needs spec (UI + fader mapping still to be defined) |
| Main: Phase 3 jam recording (record loop start / loop selection into other Loops as a live jam) | `Track` jam state (`jamStartTick`, `jamLength`, `jamTick`, `jamPlaybackActive`), `ClockManager`, `TrackManager` (multi-loop capture plumbing), `DisplayManager`, `TrackUndo` | `Plans/phase-3-multi-loop.md` + `Plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md` (D13/D14/D15) | Slot infrastructure shipped; jam **capture** (D13–D15) and Scenes not implemented; detailed slice mapping below |
| Phase 3 support: slot play/record feedback via Droid LFO pulse (BPM-synced) | `MidiLedManager`, `MidiButtonConfig`/`MidiButtonActions` (slot armed/record transitions), `MidiConfig::LfoPulse` constants (note 70 + slot CC) | `Plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md` | Pending: repo has `MidiConfig::LfoPulse` definitions, but no wiring found for note 70 + CC slot gating into any active LFO/LED lane in `src/` |
| Future: Pitch transpose on `Fader4` in Loop mode (and record it into the loop) | `MidiFaderManager` / `MidiFaderActions` (fader routing), `NoteEditManager` edit plumbing, `Track`/`Loop` recording path | Roadmap/placeholder: `Guides/control-surface/Jams.md` “Pitch transposing” | Future (no slice doc found) |
| Future: `Fader3` controls quantization percentage of notes to 16ths | `MidiFaderProcessor` (fader input), `NoteEditManager` + edit states, quantization helpers (`Track::quantizeStart`, tick/16th logic) | No plan doc export found for “quantization percentage” | Future (needs spec) |
### Phase 3 jam recording slice mapping (only detailed one)
From `Plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md`:
- `D13` = **Arrangement mode: capture** (jam-era capture into target slot)
- `D14` = **Arrangement mode: playback** (resolve phase 2.3; tests)
- `D15` = **Scenes** (optional capture/persistence of active loop index)
