# Loops (control surface)

**Loops** row: **eight loop slots** for the **selected** track (typical mapping: Channel 16 notes 50–57). Constants: [`include/MidiConfig.h`](../../../include/MidiConfig.h) (`LOOP_SELECT_LED_BASE`, etc.). Wiring: [`MidiButtonConfig.cpp`](../../../src/Utils/MidiButtonConfig.cpp) (`loadConfiguration`), actions: [`MidiButtonActions.cpp`](../../../src/MidiButtonActions.cpp) (`handleToggleRecordForSlot`), hold layering: `MidiButtonManager::updateLoopHoldLayering`.

## Gestures while playing

| Gesture | Empty slot | Non-empty slot |
|---------|------------|----------------|
| **Short (first)** | If clock quantize applies: queue record (reference slot phase if valid, else next bar); else record now | Start overdub |
| **Short (second)** | If queued: punch in now, align loop origin on stop when applicable | Stop overdub |
| **Long** | Select slot, then clear that slot’s loop | Same |
| **Double / Triple** | Slot undo / redo | Same |

**While recording or overdubbing:** short on **another** slot finalizes capture on the current slot, selects the new slot, resumes playback if it has data (`TrackManager::finalizeCaptureAndSelectSlot`).

**While not playing:** short on **empty** → record (immediate when quantize does not apply). Short on **non-empty** → toggle play/stop.

## Hold (layering)

Held past **long-press + 50 ms**: layering arms. While **playing** and held slot **empty**, record can queue from **base** slot phase. **Release** clears layering and queue. See `beginSlotLayerHold` / `endSlotLayerHold`.

## Notes

- Active slot drives primary playback; `heldLayerSlot` can add another slot’s audio while hold is active.
- Queued record while playing can show **armed** in UI (`TRACK_ARMED` when pending and playing/overdubbing).
- Reference slot for queue phase: previously active loop index (or base slot during layer hold), when `loopLengthTicks > 0`.

## vs Jams

Slot **Loops** hold **MIDI loop data**. **Jams** (regions / future **Jams** row capture) stay a separate concern — see **[`Jams.md`](Jams.md)**.
