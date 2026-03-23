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

## 2. Track buttons: short=mute, long=select, double=solo

Per principle 1, the **track row** (ch16 notes 60–67) uses a **mute-primary** mapping:

| Gesture | Action |
|---------|--------|
| **Short** | Mute / unmute track |
| **Long** | Select track |
| **Double** | Solo / unsolo track |

**Rationale:** During performance, muting and unmuting tracks is the most frequent action on the track row. Making it the **short** (default) press reduces friction. Selecting a track for recording or editing is less frequent and fits **long** press. Solo remains **double** as a distinct, deliberate action.

**Implementation:** See [multi-loop_leds_and_droid_lfo_3a62f325.plan.md](../plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md) §4.1. Current codebase may use the inverse (short=select, long=mute); D4 deliverable updates `MidiButtonConfig` to match this mapping.

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
