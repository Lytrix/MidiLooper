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

**Short:** enter edit / cycle NOTE_EDIT ↔ LOOP_EDIT. **Double:** toggle load/save overlay. **Long:** exit edit.

Add and delete notes use **NOTELEN** (note 35) double-press — see below.

## NOTELEN (note 35)

| Press | Role |
|-------|------|
| Short | Toggle note **start** vs **end** (length) editing |
| Double | Delete selected note, or create at bracket when empty |

## Load/save overlay (when open)

| Control | Short | Long | Double |
|---------|-------|------|--------|
| **Edit mode** (38) | **Confirm** focused row (save, load, dirty-prompt choice) | Back / exit (context-dependent) | Enter / exit overlay |
| **Record** (36) | Scroll list **down** | — | — |
| **Track select** (37) | Scroll list **up** | — | — |
| **NOTELEN** (35) | — (suppressed) | — | Toggle **favorite** on focused **Set** row (root list only) |

**GPIO encoder:** rotate = scroll; **short press** = same as Edit **38** short (confirm row).

Normal record / track / NOTELEN behavior resumes when the overlay is closed.

## More

Global transport (e.g. note 39), nudge, loop-edit helpers — [`MidiButtonConfig.cpp`](../../../src/Utils/MidiButtonConfig.cpp), [`MIDI_CONFIG_GUIDE.md`](../MIDI_CONFIG_GUIDE.md).

## Extended transport (channel 16, notes 40–47)

Per-track play/stop and tick nudge. Note **40** is separate from main REC/PLAY (note 36).

| Note | Short | Long | Hold |
|------|-------|------|------|
| **40** Play/Stop | Toggle play/stop | On loops **longer than 16 bars**, center the detailed piano-roll window on the current playhead (does not toggle play) | While held, window and playhead track the current tick — same auto-follow as during playback; overrides NOTE_EDIT window freeze |
| **41** | Set loop start | — | — |
| **42** | Set loop end | — | — |
| **43** | Quantize | — | — |
| **44** | Copy note | Paste note | — |
| **45–48** | Move current tick (± beat / 16th) | — | — |

Long-press threshold for note 40 matches other buttons (default **600 ms**). During NOTE_EDIT the bounded window stays fixed until you long-press or hold play/stop.
