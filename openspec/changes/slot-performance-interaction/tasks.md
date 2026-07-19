# Tasks — slot-performance-interaction

**Prerequisite:** Read [`design.md`](design.md), [`proposal.md`](proposal.md), [`docs/plans/slot_playback_window_interaction_architecture.md`](../../../docs/plans/slot_playback_window_interaction_architecture.md).

## −1. Rename merge cache (first)

- [x] −1.1 Rename `include/PlaybackWindow.h` struct → **`PlaybackMergedMidiEvents`**
- [x] −1.2 Rename `Track::invalidatePlaybackWindow` → **`invalidatePlaybackMergedMidiEvents`**; update call sites (`Track.cpp`, etc.)
- [x] −1.3 Rename `LoopPlaybackRuntime::primaryWindow` → `mergedMidiEvents`
- [x] −1.4 Update `ensurePlaybackWindowBuilt` → `ensurePlaybackMergedMidiEventsBuilt`
- [x] −1.5 `pio test -e native` after rename-only pass

## 0. OpenSpec and docs

- [x] 0.1 Architecture plan [`slot_playback_window_interaction_architecture.md`](../../../docs/plans/slot_playback_window_interaction_architecture.md)
- [ ] 0.2 `openspec validate slot-performance-interaction --strict`
- [ ] 0.3 Update [`docs/Guides/control-surface/Loops.md`](../../../docs/Guides/control-surface/Loops.md)
- [ ] 0.4 Cross-link in [`docs/plans/openspec_integration_overview.md`](../../../docs/plans/openspec_integration_overview.md)

## 1. SlotActionQueue (Phase 0)

- [ ] 1.1 `SlotActionType` + pending action + **per-action `SlotQuantization`** on `SlotStateMachine` / `TrackManager`
- [ ] 1.2 `enqueueSlotAction` + `shouldCommitSlotAction` (`LoopEnd` vs `NextGrid`)
- [ ] 1.3 `commitSlotAction` — Launch / Restart / Mute / Unmute
- [ ] 1.4 `Track::silenceAudibleNotes()` — `sendAllNotesOff` + clear `ActiveNoteLedger`
- [ ] 1.5 Wire commit in `TrackManager::updateAllTracks`

## 2. LOOP_EDIT resync (Phase 0)

- [ ] 2.1 `LoopEditManager::applyLoopStartTick` — silence + queue restart when playing
- [ ] 2.2 `applyLoopLength*` — same when playing
- [ ] 2.3 `BarStepButtonHandler` HOLD_ONE 16th — resync hook
- [ ] 2.4 `Track::setLoopStartTick` — note-offs when playing

## 3. Gesture remap (Phase 1)

- [ ] 3.1 Strip arm/record/overdub/undo/redo from `handleToggleRecordForSlot` — empty slot = select only
- [ ] 3.2 `MidiButtonConfig`: slot short=performance, double=restart/launch, triple=overlay; remove `OVERDUB_FOR_SLOT`, `REDO_FOR_SLOT`, `CLEAR_TRACK_FOR_SLOT`
- [ ] 3.3 Short → `LoopEnd`; double → `NextGrid`
- [ ] 3.4 Long press → `LoopTriggerSequenceManager` dispatch (gate full behaviour on UIP Phase 7); remove `beginSlotLayerHold` on loop slots in `MidiButtonManager`
- [ ] 3.5 Triple → overlay entry (gate on `load-save-overlay-display-regression`)
- [ ] 3.6 Very-long (~4s) delete + **global undo** checkpoint (`TrackUndo`)

## 4. LED feedback

- [ ] 4.1 Pending action pulse on **preview** target; phase/current-tick on **playing** slot in `refreshTrackAndLoopSelectLeds` / `MidiLedManager`
- [ ] 4.2 Transient PlaybackWindow braces (`long-loop-piano-roll-window`)
- [ ] 4.3 Display: flashing cursor at `loopStartTick` when preview ≠ playing (`DisplayManager`)
- [ ] 4.4 Display: **dimmed reference overlay** for audible non-selected slots (D23) — tile shorter loops, segment longer loops to `L_play`
- [ ] 4.5 Document legacy **hold layering** playback in `Loops.md` until removed; confirm **no multi-slot overdub** in spec review

## 4b. Split focus (playing / preview / edit commit)

- [x] 4b.1 Formalise `selectedSlotIndex` as preview slot on `SlotStateMachine` (no `previewSlotIndex` field); add `getPlayingSlotIndex` / `getPreviewSlotIndex` / `getPendingSlotIndex` on `TrackManager`
- [x] 4b.2 Preview set on launch queue; playing slot commit unchanged until `commitSlotAction`
- [x] 4b.3 Keep immediate `beforeSelectedSlotChange` / `onSelectedSlotChanged` when preview slot is set (no deferral)
- [x] 4b.4 `DisplayManager` binds piano roll to preview slot; `MidiLedManager` phase to playing slot

## 4c. Boot load all payloads (DEC-021 amendment)

- [x] 4c.1 Queue all SD loop payloads at boot; priority ordering only
- [x] 4c.2 `TrackManager::slotHasLoopContent`; amend DEC-021 in `DECISION_LOG.md`

## 5. PlaybackWindow lifecycle (Phase 3)

- [ ] 5.1 Transient window from bar/step; release → persisted window
- [ ] 5.2 Empty slot short → persist transient window
- [ ] 5.3 Filled slot triple → overlay replace + global undo

## 6. Domain PlaybackWindow struct (Phase 4)

- [ ] 6.1 Introduce domain `PlaybackWindow` / `PlaybackWindowMode` on `Track`
- [ ] 6.2 Migrate `jamStartTick` / `jamLength` fields
- [ ] 6.3 `playMidiEventsForSlot` per-slot projection anchor

## 7. Tests

- [ ] 7.1 `test_slot_action_queue/` — LoopEnd vs NextGrid commit
- [ ] 7.2 Extend `test_interval_projection` — metadata resync
- [ ] 7.3 HITL `slot_performance_mute`, `slot_performance_launch`, `slot_performance_restart`
- [ ] 7.4 `pio test -e native`

## 8. Archive

- [ ] 8.1 Manual slot-selection-focus §8 with new gestures
- [ ] 8.2 `openspec validate --strict`
- [ ] 8.3 Archive; merge deltas
