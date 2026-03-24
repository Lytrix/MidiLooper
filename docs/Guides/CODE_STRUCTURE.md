# Code structure

How the codebase is organized: **suffix naming** and the **button/fader pipeline**. For MIDI note and channel numbers, see [`include/MidiConfig.h`](../../include/MidiConfig.h) and [`MIDI_CONFIG_GUIDE.md`](MIDI_CONFIG_GUIDE.md).

## Suffix naming

| Suffix | Role | Examples |
|--------|------|----------|
| **Handler** | Receives events and routes or processes them | `MidiHandler` receives all MIDI and dispatches to appropriate modules |
| **Manager** | Owns a domain or coordinates components | `TrackManager`, `ClockManager`, `LoopEditManager` |
| **Processor** | Transforms input (raw → detected events) | `MidiButtonProcessor` detects short/long/double/triple from Note On/Off |
| **Actions** | Executes domain operations | `MidiButtonActions`, `MidiFaderActions` perform record, undo, fader moves |

**Input pipelines** (buttons, faders) use **Manager → Processor + Actions**:

- `MidiButtonManager` coordinates `MidiButtonProcessor` (detection) and `MidiButtonActions` (execution).
- `MidiFaderManager` coordinates `MidiFaderProcessor` and `MidiFaderActions`.

Standalone **Handler** classes (e.g. `BarStepButtonHandler`) process events in one class when the full Manager/Processor/Actions split is not needed.

## Module map (high level)

| Module | Role |
|--------|------|
| `TrackManager` | Selected track, track states, quantization, LED defer |
| `Track` | Loop slots, MIDI events, playback, record/overdub |
| `ClockManager` | Timing, MIDI clock, bar/tick |
| `MidiHandler` | MIDI I/O and routing |
| `MidiLedManager` | LED feedback to controller (e.g. bar/step, track/loop rows) |
| `MidiButtonManager` / `MidiButtonProcessor` / `MidiButtonActions` | Gesture detection and actions |
| `NoteEditManager` / `DisplayManager` | Note edit UI and piano roll |

For deeper implementation history, see **[`docs/Refinements/`](../README.md#refinements)**.
