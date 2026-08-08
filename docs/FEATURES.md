# Technical features (reference checklist)

**Not the implementation queue.** Shipped capability summary for onboarding and README cross-links. For what to build now, use [`Runtime/CURRENT_WORK.md`](Runtime/CURRENT_WORK.md). For deliverable-level shipped vs next, use [`DELIVERABLE_TRACKING.md`](DELIVERABLE_TRACKING.md). For current behavior, prefer [`Guides/`](Guides/).

**Quick technical path:** Constants and channel/note layout live in [`include/MidiConfig.h`](../include/MidiConfig.h). To remap a controller, follow [`docs/Guides/MIDI_CONFIG_GUIDE.md`](Guides/MIDI_CONFIG_GUIDE.md) and keep [`droid/midilooper_v1.ini`](../droid/midilooper_v1.ini) in sync if you use the reference DROID patch.

---

Multi-track MIDI looper with full undo/redo, auto-save/load, and clear visual feedback—ready for live performance or creative studio work!

- 8 MIDI tracks (`Config::NUM_TRACKS`), each with **8 loop slots** (`Config::MAX_LOOPS_PER_TRACK`)
- 192 PPQN internal clock for live recording
- 24 PPQN MIDI Sync
- 256×64 OLED display (a 16×2 LCD driver exists but its pins are disabled in `Globals.h`)
- Memory-aware undo per slot (`PREFERRED_UNDO_DEPTH` 99 when reserves are healthy; trims under chunk/heap pressure; `ABSOLUTE_MAX_UNDO_ENTRIES` 512), covering overdub, clear, and loop-start edits. Redo branch survives full undo until a new pass commits.
- Track clear Undo (restore last cleared track)
- Overdub Undo (revert last overdub layer)
- **Loop Start Point Editing** (dynamic loop start position control with fader) — [`LOOP_START_EDITING.md`](Guides/LOOP_START_EDITING.md)
- **Loop Length Editing** (1–128 bars with CC control and automatic fader feedback)
- **Note Length Editing Mode** (toggle between position and length editing for notes)
- Automatic saving after each edit (record, overdub, undo, clear) and full recall of loops on SD card when powering up
- Reload last used state and loops on startup
- Robust state machine for all track transitions
- Visual feedback for actions and states on both displays
- 256×64 OLED display with piano roll visualization
- SD card storage for loop data
- Comprehensive undo/redo system with multiple history stacks
- Real-time MIDI playback with precise timing
- Multi-layer overdubbing with separate undo states
- Track muting and solo functionality
- Automatic loop synchronization
- Motorized fader support with feedback prevention
- Dedicated edit modes for different operations
- Full wrap-around support for notes crossing loop boundaries
- **Controller LED feedback** — 16th step content, current tick indicator, 8 bar LEDs, loop row (50–57), track row (60–67); updates on loop start/length change — see [`MIDI_CONFIG_GUIDE.md`](Guides/MIDI_CONFIG_GUIDE.md)
- **Track row & loop slot row (Ch. 16)** — per-track select / double-mute / long-solo; per-slot quantized record, overdub, clear, slot undo/redo, layered hold — [`control-surface/Tracks.md`](Guides/control-surface/Tracks.md), [`control-surface/Loops.md`](Guides/control-surface/Loops.md)
- **OLED track column** — letters per track (`-`, `P`, `O`, `R`, `A`, …); **`M`** if muted or solo-hidden; selected row shows real state under solo ([`DisplayManager::drawTrackStatus`](../src/DisplayManager.cpp)); [`control-surface/Display.md`](Guides/control-surface/Display.md)

## Note editor

- Piano-roll note editor integrated into the looper UI
- Encoder/fader-driven movement of note start positions, with wrap-around support
- Overlap resolution and direction-aware restoration — [`MOVE_NOTE_LOGIC.md`](Guides/MOVE_NOTE_LOGIC.md), [`NOTE_WRAPPING_LOGIC.md`](Guides/NOTE_WRAPPING_LOGIC.md)
- Host-side unit tests in `test/`

## Jam loops (bar / 16th)

- Sub-regions of the full loop in **LOOP_EDIT** — [`jam-bar-step-phases.md`](Guides/jam-bar-step-phases.md), [`control-surface/Jams.md`](Guides/control-surface/Jams.md), [`control-surface/Bars-and-16ths.md`](Guides/control-surface/Bars-and-16ths.md)

## Build

- **PlatformIO** project (`platformio.ini`), target **Teensy 4.1**
