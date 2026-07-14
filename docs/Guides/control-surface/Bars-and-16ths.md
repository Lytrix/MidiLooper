# Bars and 16ths (control surface)

**Applies to:** v3 (`dev`) — DROID bar/16th rows, Channel 16 notes 0–15 and 17–24.

**Bars** (typical: Channel 16 notes 17–24) and **16ths** (typical: notes 0–15) are used for **jam loop** entry, seek, and region selection while in **LOOP_EDIT**, and for **LED** playhead/content feedback from the Teensy.

- **Behavior:** **[`../jam-bar-step-phases.md`](../jam-bar-step-phases.md)** — HOLD_ONE, HOLD_TWO, seek, double to exit, triple undo loop start.
- **Numbers:** **[`../MIDI_CONFIG_GUIDE.md`](../MIDI_CONFIG_GUIDE.md)** (`BarStepButton` / channel 16 map).
- **Jam vs grid row “Jams”:** Bar/16th **jam mode** is live today. The dedicated **Jams** **row** on the hardware map is a **target** for future capture (see **[`Jams.md`](Jams.md)**).

## Queued playback start (while playing, outside LOOP_EDIT)

**Short press** bar or 16th while the selected track is **playing** (and not in LOOP_EDIT jam seek) queues a one-shot **`queuedStartTick`** at the pressed position. Commit happens on the next **16th grid** tick (default grid; configurable per track via **`queuedStartGridTicks`**).

Bar-queued and slot-queued starts are **mutually exclusive** — the last gesture wins. A new slot press replaces any pending bar queue (and vice versa).

On commit: **`projectionCycleStartTick`** = grid commit tick; playback restarts from **`queuedStartTick`** once, then normal wrap resumes.
