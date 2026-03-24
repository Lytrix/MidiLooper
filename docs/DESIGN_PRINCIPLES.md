# Design principles

Cross-cutting UX and interaction principles for the MIDI looper. Plans and implementation should align with these where applicable.

---

## 1. Gesture-first / mode inference

Prefer inferring intent from **press type** (short / long / double) on context-relevant buttons over requiring explicit mode entry.

- **Default** to the most common action for each button row.
- Use **long** or **double** press for secondary actions so the state machine can infer intent without a separate "loop edit", "note edit", or "mute mode" switch.
- Avoid forcing users into a mode before acting; the gesture on the right button should be enough.

**Example (Loop Edit context):**
- **Short press**: retrigger loop start point in sync.
- **Long press**: loop the selected bar.
- **Double press**: exit loop edit.

---

## 2. Track buttons: short=select, double=mute, long=solo

Per principle 1, the **track row** (Channel 16, notes 60–67 — see `MidiConfig::Led::TRACK_SELECT_LED_BASE` and `MidiButtonConfig::loadConfiguration`) uses a **select-primary** mapping:

| Gesture | Action |
|---------|--------|
| **Short** | Select that track |
| **Double** | Mute / unmute that track |
| **Long** | Solo / unsolo (exclusive solo on that track; long again on the same track clears solo) |

**Rationale:** Selecting a track is the primary action for switching context. Mute as short press was tried but felt irrational; double=mute and long=solo keep those as distinct, deliberate actions.

**Implementation:** `SOLO_TRACK` is handled in `MidiButtonActions::handleSoloTrack` → `TrackManager::toggleSoloTrack`. Playback audibility uses `TrackManager::isTrackAudible` (mute on the `Track` plus solo mask on `TrackManager`).

**Not the same row:** The legacy **MUTE/DE** control (e.g. note 37 on Channel 16) still uses **short = next track**, **long = mute current track**, **double/triple = undo/redo clear** — see README and `MidiButtonConfig` for “Track Switch”.

---

## 2b. Loop slot buttons: staged record flow while playing

Per-slot loop buttons (Channel 16, notes 50–57 — `MidiConfig::Led::LOOP_SELECT_LED_BASE`) use the slot-aware flow in `MidiButtonActions::handleToggleRecordForSlot` and hold layering in `MidiButtonManager::updateLoopHoldLayering`.

| Gesture | Empty slot while track is **playing** | Non-empty slot while track is **playing** |
|---------|----------------------------------------|-------------------------------------------|
| **Short (first press)** | If the **internal clock is running** (`ClockManager::shouldQuantizeRecordStart()`): queue record start aligned to the **reference slot’s loop phase** when that slot has a valid loop length, otherwise on the **next bar**; if the clock is not running, start recording immediately | Start live overdub on this slot |
| **Short (second press)** | If a record was **queued** for this slot: cancel the queue, **immediate punch-in** (start recording now), and set **pickup / loop-origin alignment on next stop** (`Track::setAlignLoopOriginOnNextStop`); does not apply when not queued | Stop overdub |
| **Long** | Select this slot, then **clear this slot’s loop** (active loop only — same path as slot targeted clear) | Same |
| **Double / Triple** | Undo / redo for this slot (active slot set to the button’s slot first) | Same |

**While recording or overdubbing:** A **short press on a different slot** finalizes capture on the current slot, selects the pressed slot, and resumes playback if that slot has data (`TrackManager::finalizeCaptureAndSelectSlot`).

**While not playing:** **Short** on an **empty** slot starts recording (immediate if not using the quantize path). **Short** on a **non-empty** slot toggles play/stop for the track.

**Hold (layering):** If a loop button is held past the configured **long-press time + 50 ms**, layering arms (`beginSlotLayerHold` from `MidiButtonManager::updateLoopHoldLayering`). While **playing** and the **held slot is empty**, a record can be **queued** using loop phase from the **base slot** (the slot that was active when the hold began). **Release** clears layering and any queue for that slot (`endSlotLayerHold`). Long-press **clear** for the same button is a separate gesture handled by the button processor on top of this timing.

Notes:
- One **active** loop slot drives primary playback; held layering can **additionally** play another slot’s events while the hold is active (`heldLayerSlot` in `TrackManager`).
- Queued record while playing + overdub is reflected as **armed** in UI state (`getTrackState` / `TRACK_ARMED` when pending and playing or overdubbing).
- Reference slot for phase is the **previously active** loop index before the press (or the base slot during a layer hold), and only applies when that slot’s `loopLengthTicks > 0` (`refSlotPhaseForQueue` in `MidiButtonActions.cpp`).

---

## 3. Minimal-button operation

The looper must function with a **minimal hardware set** when full controllers (e.g. DROID B32) are not available.

**Target minimal setup:**
- **1 encoder** with switch — cycles through the 4 fader roles (note select, start loop, end loop, note value); one physical encoder serves all four.
- **4 buttons** — rec/play, switch mute track, switch loop/note edit mode, and (eventually) file load/save.

This mode requires reimplementation from the main branch; the DROID-focused UI is the extended configuration. Minimal mode remains the baseline for portability and DIY builds.

---

## 4. MIDI-controllable / controller-agnostic

Every function must be controllable via **MIDI commands** (notes, CCs, program change) so that **any** MIDI controller can drive the looper. DROID is the primary target, but the design must not assume DROID.

**Requirement:** A single, **central** MIDI configuration (e.g. `include/MidiConfig.h` and `src/Utils/MidiButtonConfig.cpp` or equivalent) defines channels, note numbers, and CC numbers. This file must be **documented** enough that users can remap everything for their own controller without digging through the codebase.

**Deliverable:** Clear comments, a config summary table or header block, and a short guide. See [MIDI_CONFIG_GUIDE.md](MIDI_CONFIG_GUIDE.md) and the summary block in `include/MidiConfig.h`.

---

## 5. Loop editing as first-class / live editing

Most time spent on MIDI recording is **post-editing** — in DAWs, that often means mouse clicking and moving events. Those actions matter for any song, but the most engaging workflow is **live editing** while the loop runs.

**Principle:** Loop editing is a **first-class citizen**. It can be done **any time** and **recorded any time**. No need to stop, enter an edit mode, or defer changes to a separate pass. Bar/16th navigation, loop boundaries, note selection, and overdubs are all available during playback, so edits become part of the performance rather than a separate step.

---

## 6. Undo all the way to the start

"The best take was always the previous one." Any edit can be **undone** back to the start of the session. No destructive operation is final — overdubs, clears, and other changes have undo (and redo) so the performer can experiment freely and always retreat to an earlier state.

---

## 7. Loops = punchy; notes = finesse; song = jam and flow

**Loops** are kicked off whenever you want — simple buttons to start, slice, retrigger. They are immediate and performative; no heavy UI.

**Recorded notes** need detail and finesse. Getting the groove right often requires precision — timing, velocity, length. The **piano roll is a primary need**; it is where that editing happens.

**Building a song** should feel like a **jam and flow**. The overall experience stays performative rather than labored. Loops provide the punch; the piano roll provides the precision; the flow between them keeps it fun.
