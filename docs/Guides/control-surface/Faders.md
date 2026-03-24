# Faders (control surface)

Four sliders on the reference layout: note select, coarse/fine 16th, note value in **NOTE_EDIT**; loop start, loop length (CC), etc. in **LOOP_EDIT**.

| Slider | NOTE_EDIT (typ.) | LOOP_EDIT (typ.) |
|--------|------------------|------------------|
| F1 | Note selector (pitchbend ch 16) | Loop start |
| F2 | 16th coarse (pitchbend ch 15) | Loop length / end (CC 101) |
| F3 | 16th fine (CC2 ch 15) | — |
| F4 | Note pitch (CC3 ch 15) | — |

Exact channels/CCs: [`include/MidiConfig.h`](../../../include/MidiConfig.h), [`MIDI_CONFIG_GUIDE.md`](../MIDI_CONFIG_GUIDE.md).

**Guides:** [`LOOP_START_EDITING.md`](../LOOP_START_EDITING.md), [`FADER_STATE_SYSTEM.md`](../FADER_STATE_SYSTEM.md), [`MOVE_NOTE_LOGIC.md`](../MOVE_NOTE_LOGIC.md).

Loop editing as **live** performance: adjust boundaries and notes while playback runs — see [How it’s meant to be played](../../../README.md) in the root `README` (intro section).
