# Tasks — slot-selection-focus

**Note:** Branch may already contain partial hooks (`beforeSelectedSlotChange`, `loopForNoteEditFocus`). Tasks §1–5 **consolidate/refactor** toward this spec — do not duplicate parallel paths.

## 0. OpenSpec and docs

- [x] 0.1 `proposal.md`, `design.md`, delta specs, `tasks.md`
- [x] 0.2 UIP cross-link row in `unified-interval-projection/proposal.md` (Relationship table only — no UIP tasks)
- [x] 0.3 Update `docs/runtime/CURRENT_WORK.md` — parallel track, not blocking UIP 5.5 HITL
- [x] 0.4 Sync `docs/plans/slot_selection_orchestration_refinement.md` link to this change folder
- [x] 0.5 Review amendments — default `SyncPlayback::Yes`, playing+edit `No`+queue, revision footer-then-fallback

## 1. TrackManager orchestrator

- [x] 1.1 Add `SyncPlayback` enum (default **`Yes`**) and `getSelectedLoop` / `getSelectedLoopIndex` to `TrackManager`
- [x] 1.2 Enhance `setSelectedSlotIndex(track, slot, syncPlayback = SyncPlayback::Yes)` — Departure → Transition → Arrival on **selected track only**
- [x] 1.3 Arrival: call `DisplayManager::invalidateForSlotChange`, `forceLedUpdate`, `requestDeferredSaveState`
- [x] 1.4 Pass `previousSlot` into `onSelectedSlotChanged`
- [x] 1.5 Background track: transition only (no edit/display arrival)

## 2. Caller playback policy

- [x] 2.1 Update `MidiButtonActions` — **playing** paths (incl. edit mode): explicit `SyncPlayback::No` + `requestSlotSwitch`; stopped/immediate: default `Yes` or omit third arg
- [x] 2.2 Remove redundant `setActiveLoopIndex` after orchestrator when sync covered
- [x] 2.3 Audit other `setActiveLoopIndex` call sites — system paths only (quantize commit, SD load, capture finalize)
- [x] 2.4 Update [`docs/Guides/control-surface/Loops.md`](../../docs/Guides/control-surface/Loops.md) — orchestrator entry, default Yes, playing explicit No+queue, departure before active commit

## 3. EditManager focus lifecycle

- [x] 3.1 Add `commitEditSessionOnDepart` + `reenterEditSessionForFocusChange` (Loop / Note / ControlChange stub)
- [x] 3.2 Refactor `beforeSelectedTrackChange` / `onTrackChanged` to shared internals
- [x] 3.3 Refactor `beforeSelectedSlotChange` / `onSelectedSlotChanged` to shared internals (consolidate partial branch work)
- [x] 3.4 Add `LoopEditManager::reopenLoopEditSession` (parallel to `reopenNoteEditSession`)

## 4. DisplayManager

- [x] 4.1 Add `invalidateForSlotChange(track, previousSlot, newSlot)` — cache invalidation only, no sync draw; during playback also **`centerDetailedWindowOnPlayhead`** for the new slot (UIP D25 playhead alignment)
- [x] 4.2 Wire from TrackManager arrival phase (not EditManager); runs even when `editSession.active == false`

## 5. Route consumers

- [x] 5.1 Replace `loopForNoteEditFocus` / `selectedSlotIndexForTrack` with `getSelectedLoop` (prefer const)
- [x] 5.2 Replace `loopForLoopEdit` / `selectedSlotForTrack` with `getSelectedLoop`
- [x] 5.3 Fix `NoteEditManager` display slot to use `getSelectedSlotIndex`

## 6. SD persistence

- [x] 6.1 Footer write: after `activeLoopIndex[]`, write `kFooterSelectedSlotExtensionToken` (`0x534C4F54`), then `selectedSlotIndex[]`, then undo token
- [x] 6.2 Read: peek after `activeLoopIndex[]` — `'SLOT'` → read selected array; else legacy (selected = active)
- [x] 6.3 Boot load + revision footer path: restore both via `applyLoadedTransportFooter` without edit hooks
- [x] 6.4 Revision default transport (`writeDefaultRevisionLoadTransportBody`): first occupied slot for **both** indices per track

## 7. Native tests

- [x] 7.1 Add `test/test_slot_switch_edit_sessions/` + register in `platformio.ini` `[env:native]`
- [x] 7.2 Note edit + Loop edit slot switch + ControlChange stub
- [x] 7.3 Default `SyncPlayback::Yes` vs explicit `No` + queue policy — manual/HITL gate (TrackManager not in native env)
- [x] 7.4 Background-track transition only (no arrival hooks) — manual gate
- [x] 7.5 Repeated slot 1↔2 stress — no pass mutation, no stale cache / leaked bindings
- [x] 7.6 Footer round-trip both index arrays
- [x] 7.7 Run `pio test -e native`

## 8. Verification

- [ ] 8.1 Manual: NOTE_EDIT slot 1↔2 while **stopped** — piano roll matches (default Yes)
- [ ] 8.2 Manual: NOTE_EDIT / LOOP_EDIT slot 1↔2 while **playing** — UI focus immediate, active at grid, edit pass committed on depart
- [ ] 8.3 Manual: multi-slot play focus-only — display selected, audio active until grid
- [ ] 8.4 Reboot: both indices restored independently after deferred save
- [ ] 8.5 Revision load with footer extension restores both; default revision transport uses first occupied
