# Bars and 16ths (control surface)

**Bars** (typical: Channel 16 notes 17–24) and **16ths** (typical: notes 0–15) are used for **jam loop** entry, seek, and region selection while in **LOOP_EDIT**, and for **LED** playhead/content feedback from the Teensy.

- **Behavior:** **[`../jam-bar-step-phases.md`](../jam-bar-step-phases.md)** — HOLD_ONE, HOLD_TWO, seek, double to exit, triple undo loop start.
- **Numbers:** **[`../MIDI_CONFIG_GUIDE.md`](../MIDI_CONFIG_GUIDE.md)** (`BarStepButton` / channel 16 map).
- **Jam vs grid row “Jams”:** Bar/16th **jam mode** is live today. The dedicated **Jams** **row** on the hardware map is a **target** for future capture (see **[`Jams.md`](Jams.md)**).
