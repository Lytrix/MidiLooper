## Why

During edit-baseline HITL and manual testing, three UX/storage symptoms block trust in the edit overlap fix and make failures hard to diagnose on hardware: (1) while **ARM → RECORD** the piano roll shows a **growing loop length** but **no captured notes** until record stops; (2) after note **move / pitch / length** edits, the **sidebar LEN field and piano roll** lag behind the committed MIDI store; (3) after the long-M0 overlap scenario, **fader 1 reselect** can **stretch P0** (pitch 60 @ bar 1 beat 3) toward **loop end**, consistent with **length-edit fader routing** still active or a stale **fader 2 END** move being applied to the wrong note.

These are separate from the overlap LIFO pairing fix shipped in `NoteMovementUtils` (see `note-move-pitch-overlap-flaky`). They affect what the user sees on the OLED during record/edit and whether HITL step gates match on-device behavior.

## What Changes

- **Live record display:** Ensure `capturePreview` / live open-note tails appear on the piano roll **during** RECORD (transport running or stopped-with-growing capture), not only after STOP.
- **Note-edit display refresh:** After edit mutations (`move`, pitch, length), invalidate display-facing caches and refresh **DNTE / LEN / bracket** within bounded latency (no multi-second stale length).
- **Length-edit mode lifecycle:** Harden `lengthEditingMode` exit: toggling length mode off must guarantee fader 2/3 map to **START** (position edit); fader feedback after fader 1 select must not apply an unintended **LENGTH EDIT** to a non-selected neighbor note (e.g. P0).
- **HITL transport policy (document):** Edit baseline intentionally **stops transport** before edit session; record phase may run **with or without** transport — clarify expected OLED behavior for each (not a firmware defect if edit runs stopped by design).
- **Regression tests:** Native/display-unit hooks where feasible; HITL serial gates for length-mode-off + P0 gate after fader 1 reselect post-overlap.

## Capabilities

### New Capabilities

- `live-record-display`: Piano roll shows capture notes and growing loop length during RECORD before stop.
- `note-edit-display-refresh`: Edit sidebar and piano roll reflect store changes within bounded time after move/pitch/length.
- `note-edit-length-mode`: Length-edit mode state, fader routing, and safe exit when reselecting notes.

### Modified Capabilities

- *(none)* — no requirement changes to archived `timeline-epochs` / `multi-loop-slots` storage contracts; display-only and edit-UI routing.

## Impact

- **Firmware:** `DisplayManager.cpp` (`resolveDisplayNotes`, `resolveDisplayLoopLength`, live capture path), `Loop.cpp` (`capturePreview`, `captureDisplayRevision`), `NoteEditManager.cpp` (`lengthEditingMode`, `sendCoarseFaderPosition`, `toggleLengthEditingMode`, fader feedback scheduling), `Track.cpp` (cache invalidation after capture events).
- **HITL:** `scripts/host_midi_automation_edit_baseline.py` — optional transport-during-edit flag (TBD), length-mode serial gates, display-settle timing.
- **Related changes:** `note-move-pitch-overlap-flaky` (overlap store — patch landed); `m8-edit` (NoteEditSession / edit RAM — active).
- **Brownfield:** `docs/Guides/MOVE_NOTE_LOGIC.md`, `docs/DELIVERABLE_TRACKING.md`.

## Non-Goals

- Jam / multi-loop slot display beyond selected slot record focus.
- D13 arrangement capture.
- Rewriting the full fader state machine (only length-mode leak + display sync).

## Open Decisions (TBD)

- Should edit baseline HITL run **transport playing** during edit for visual parity with performance use, or stay **STOPPED** for deterministic serial?
- Minimum acceptable **display refresh latency** after edit (e.g. ≤200 ms vs next `DisplayManager::update()` frame)?
