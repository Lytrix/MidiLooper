# Faders

The fixed-memory engine (`teensy41-capture-serial`). All channels below are one-based. The reference [DROID patch](../../../droid/midilooper_v1.ini) selects bank 1 for NOTE_EDIT, bank 0 for LOOP_EDIT and bank 2 for the GROOVE window (the firmware sends the Program Change).

## NOTE_EDIT

- **F1:** select a 16th/note, pitch bend channel 16.
- **F2:** coarse note start or end, pitch bend channel 14.
- **F3:** fine timing, CC 2 channel 15.
- **F4:** pitch, CC 3 channel 15; while NOTELEN is held, velocity (zero mutes).

NOTELEN short changes start/end mode, or creates at an empty bracket. Motors follow selection and edit state; settle and echo guards prevent a motor landing from becoming an edit. Repeated adjustments of one selected note coalesce into one session undo step. Leaving NOTE_EDIT keeps the session as one slot undo step.

## LOOP_EDIT

- **F1:** rotate loop start in 16ths, pitch bend channel 16; applied at rest relative to the visit's starting content.
- **F2:** loop length, CC 101 channel 16 (pitch bend channel 14 also accepted). CC 0–127 selects the nearest of 25 notches: 1/16, 1/8, 1/4, 1/2, 3/4 bar; 1–16 bars; 32, 64, 96, 128 bars.
- **F3:** fine tempo, CC 100 channel 16, 0.1 BPM steps around centre 64.
- **F4:** whole BPM, pitch bend channel 15, 20–300 BPM. A coarse move recentres F3. Local tempo editing is inactive on external clock.

For a notched DROID F2, scale its notch number to 0–1 (divide by 24), and its start value back to a notch (multiply by 24), as in the current patch. Sending the raw notch number into a normalized CC output saturates it.

On LOOP_EDIT entry the firmware parks all four motors, sending values and the patch's clear triggers: F1/F2 notes 100/101 on channel 16; F3/F4 notes 1/2 on channel 15. Length/start changes coalesce into one undo step per visit while the previous change remains current.

## GROOVE window

- **F1 / F2:** column A's groove template and amount; **F3 / F4:** column B's — CC 20–23 on channel 16.

On opening the window the firmware moves all four motors to A's and B's values (a value on CC 20–23, then the patch's clear note 102–105 on channel 16); their travel, and the first report as the bank opens, are not moves. A value changed rebuilds only the column heard; the columns are saved with the slot when the visit changed them. See the [UI / UX guide](../ENGINE_UI_UX_GUIDE.md) for the window itself.

F2 also works while the loop-length popup is open. See the [engine guide](../FIXED_MEMORY_ENGINE.md) for popup controls, loop-window behavior and the current feature boundary.

Sources: [ControlSurface.h](../../../include/ContentEngine/ControlSurface/ControlSurface.h) (with `ControlSurfaceLoopEdit.h` and `ControlSurfaceGroove.h`), [ControlSurfaceNoteEdit.h](../../../include/ContentEngine/ControlSurface/ControlSurfaceNoteEdit.h). The older [motorfader](../DROID_MOTORFADER_PITCHBEND.md) and [fader-state](../FADER_STATE_SYSTEM.md) guides describe the legacy implementation.
