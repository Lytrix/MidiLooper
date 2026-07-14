# Faders (control surface)

**Applies to:** v3 (`dev`) — DROID 4 motorized faders (NOTE_EDIT / LOOP_EDIT roles).

Four sliders on the reference layout: note select, coarse/fine 16th, note value in **NOTE_EDIT**; loop start, loop length (CC), etc. in **LOOP_EDIT**.

| Slider | NOTE_EDIT (typ.) | LOOP_EDIT (typ.) |
|--------|------------------|------------------|
| F1 | Note selector (pitchbend ch 16) | Loop start |
| F2 | 16th coarse (pitchbend ch 15) | Loop length / end (CC 101) |
| F3 | 16th fine (CC2 ch 15) | — |
| F4 | Note pitch (CC3 ch 15) | — |

Exact channels/CCs: [`include/MidiConfig.h`](../../../include/MidiConfig.h), [`MIDI_CONFIG_GUIDE.md`](../MIDI_CONFIG_GUIDE.md).

## NOTE_EDIT motor sync (DROID)

When motorized faders are connected (DROID patch), firmware keeps physical faders aligned with edit state using **two independent debounced queues**:

| You move | Display / selection | Motors that follow (after ~300 ms idle) |
|----------|---------------------|----------------------------------------|
| **F1** (Select kind) | `EditorSelection` updates immediately | F2 + F3 + F4 parallel burst |
| **F2 / F3 / F4** (Move / Length / Pitch) | `EditorSelection` + bracket from moving note | F1 bracket burst only |

While editing geometry (Move / Length / Pitch), **F2/F3/F4** drive the moving note; F1 motor follows after ~300 ms idle. **User F1** can still change note selection (commits geometry, returns to Select kind). Motor echo after geometry F1 outbound is ignored for 1500 ms — see [**DROID_MOTORFADER_PITCHBEND.md**](../DROID_MOTORFADER_PITCHBEND.md).

**Guides:** [`LOOP_START_EDITING.md`](../LOOP_START_EDITING.md), [`FADER_STATE_SYSTEM.md`](../FADER_STATE_SYSTEM.md) (§ NOTE_EDIT motor feedback), [`MOVE_NOTE_LOGIC.md`](../MOVE_NOTE_LOGIC.md).

Loop editing as **live** performance: adjust boundaries and notes while playback runs — see [How it’s meant to be played](../../../README.md) in the root `README` (intro section).
