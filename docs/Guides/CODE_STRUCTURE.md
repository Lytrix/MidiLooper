# Code structure

How the codebase is organized: **suffix naming** and the **button/fader pipeline**. For MIDI note and channel numbers, see [`include/MidiConfig.h`](../../include/MidiConfig.h) and [`MIDI_CONFIG_GUIDE.md`](MIDI_CONFIG_GUIDE.md).

## Suffix naming

Canonical suffix table and verb conventions: **[NAMING.md](../Authority/NAMING.md)** § Module suffix vocabulary and § Verb conventions.

**Input pipelines** (buttons, faders) use **Manager → Processor + Actions**:

- `MidiButtonManager` coordinates `MidiButtonProcessor` (detection) and `MidiButtonActions` (execution).
- `MidiFaderManager` coordinates `MidiFaderProcessor` and `MidiFaderActions`.

Standalone **Handler** classes (e.g. `BarStepButtonHandler`) process events in one class when the full Manager/Processor/Actions split is not needed.

## Module map (high level)

| Module | Role |
|--------|------|
| `TrackManager` | Selected track, track states, quantization, LED defer |
| `Track` | Loop slots, **passes** / **Capture** ([storage & validation guide](LOOP_MIDI_STORAGE_AND_VALIDATION.md)), playback, record/overdub |
| `ClockManager` | Timing, MIDI clock, bar/tick |
| `MidiHandler` | MIDI I/O and routing |
| `MidiLedManager` | LED feedback to controller (e.g. bar/step, track/loop rows) |
| `MidiButtonManager` / `MidiButtonProcessor` / `MidiButtonActions` | Gesture detection and actions |
| `ControlSurfaceManager` / `EditManager` / `EditNoteState` | Note edit UI and piano roll (`EditStates/` overlays); `ControlSurfaceManager` owns NOTE_EDIT fader inbound guards and select/geometry motor-sync queues |
| `DisplayManager` | Piano roll and track status display |

For deeper implementation history, see **[`docs/Refinements/`](../README.md#refinements)**. For loop MIDI memory, validation, and undo behavior, see **[`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](LOOP_MIDI_STORAGE_AND_VALIDATION.md)**. For internal heap vs external memory pool routing, see **[`INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`](INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md)**. For DROID motor fader sync during NOTE_EDIT, see **[`DROID_MOTORFADER_PITCHBEND.md`](DROID_MOTORFADER_PITCHBEND.md)** and **[`FADER_STATE_SYSTEM.md`](FADER_STATE_SYSTEM.md)** (feedback on/off).
