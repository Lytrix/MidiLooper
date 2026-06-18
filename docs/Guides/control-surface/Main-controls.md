# Main controls (control surface)

**Main controls** strip: record/play, track switch / mute strip, edit mode, NOTELEN, global transport, extended transport — typical Channel 16 notes in the 35–48 range (exact map: [`MIDI_CONFIG_GUIDE.md`](../MIDI_CONFIG_GUIDE.md) § Main controls).

## REC/PLAY (note 36, selected track)

| Press | Role |
|-------|------|
| Single | Record / play / overdub state machine (see below) |
| Double | Undo overdub |
| Triple | Redo overdub |
| Long | Clear track (with data) |

**State machine (single on selected track):**

| From | To (simplified) |
|------|------------------|
| Empty | Recording → stopped recording → playing |
| Playing | Overdubbing ↔ playing |

Per-**slot** record/overdub uses the **Loops** row (50–57), not only REC/PLAY — see **[`Loops.md`](Loops.md)**.

## MUTE/DE (note 37)

Legacy strip: **short** = next track, **long** = mute current, **double/triple** = undo/redo **clear**. Prefer **Tracks** row for direct select/mute/solo — **[`Tracks.md`](Tracks.md)**.

## Edit mode (note 38)

**Short:** enter edit / cycle NOTE_EDIT ↔ LOOP_EDIT. **Long:** exit edit.

Add and delete notes use **NOTELEN** (note 35) double-press — see below.

## NOTELEN (note 35)

| Press | Role |
|-------|------|
| Short | Toggle note **start** vs **end** (length) editing |
| Double | Delete selected note, or create at bracket when empty |

## More

Global transport (e.g. note 39), nudge, loop-edit helpers — [`MidiButtonConfig.cpp`](../../../src/Utils/MidiButtonConfig.cpp), [`MIDI_CONFIG_GUIDE.md`](../MIDI_CONFIG_GUIDE.md).
