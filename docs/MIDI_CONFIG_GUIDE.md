# MIDI configuration guide

How to remap channels, notes, and CCs for your own controller. The looper is **controller-agnostic**; DROID is the reference target, but any MIDI controller can be used if you update the config to match what your hardware sends.

---

## Config locations

| What | File | Role |
|------|------|------|
| Channels, LED notes, CC numbers | `include/MidiConfig.h` | Central constants used by handlers |
| Button mappings (note + channel → action) | `src/Utils/MidiButtonConfig.cpp` | `loadConfiguration()` |
| Bar/16th button range | `include/MidiConfig.h` → `BarStepButton` | Notes 0–15, 17–24 on channel 16 (or override) |
| DROID patch (if using DROID) | `droid/midilooper_v1.ini` | Must send/receive on same channels and notes as Teensy |

---

## Quick remap checklist

1. **Pick your channels** — Avoid overlap between:
   - Buttons (e.g. 16 for main/bar, 2 for tracks)
   - Faders (15, 16)
   - LED feedback out (3) and transport LED in (4)
   - Recorded MIDI (channels outside 13–16 are recorded as loop data)

2. **Update `MidiConfig.h`** — Change `Channels::*`, `Led::*`, `Fader::*`, `BarStepButton::*`, `LoopEdit::*` to match your controller’s output.

3. **Update `MidiButtonConfig.cpp`** — In `loadConfiguration()`, change each `ButtonConfig(note, channel, ...)` to your controller’s note and channel. Notes are 0–127; channels are 1–16 in config.

4. **Update DROID ini** (if applicable) — All `[midiout]` and `[midiin]` blocks must use the same channels and note numbers. See the ini header and section comments.

---

## Config summary (default DROID mapping)

### Main transport (channel 16)

| Note | Action |
|------|--------|
| 36 | Record/Overdub (short), Undo (double), Redo (triple), Clear (long) |
| 37 | Track switch (short), Undo clear (double), Redo clear (triple), Mute (long) |
| 38 | Edit mode (short), Delete note (double), Exit edit (long) |
| 3 | Length edit toggle |
| 39 | Global transport (short), Reset to loop start (double) |

### Bar and 16th buttons (channel 16)

| Notes | Role |
|-------|------|
| 0–15 | 16th step select |
| 17–24 | Bar select |

### Track row (channel 2 or 16)

| Notes | Role |
|-------|------|
| 48–63 (ch2) | Track 1–16: select (short), mute (long), solo (double) |
| 60–67 (ch16) | Track 1–8 in multi-loop plan — see `plans/` |

### Faders

| Channel | Role |
|---------|------|
| 16 | Fader 1 — pitchbend for note/16th select |
| 15 | Fader 2 — pitchbend; Fader 3 — CC 2; Fader 4 — CC 3 (note value) |

### Loop editing

| Channel | CC / message |
|---------|--------------|
| 16 | CC 100 (loop start trigger), CC 101 (loop length, loop end) |
| 16 | Pitchbend for loop start position |

### LED feedback (Teensy → controller)

| Channel | Notes | Role |
|---------|-------|------|
| 3 | 0–15 | 16th step content (velocity = brightness) |
| 3 | 16–31 | Current tick indicator |
| 3 | 40–47 | Bar content |
| 4 | 39 | Transport play/stop LED |

Channels 1–4 are excluded from All Notes Off on USB so LED state is preserved.

---

## Changing one mapping

**Example:** Move main transport buttons from channel 16 to channel 5.

1. In `MidiConfig.h`, add or reuse a constant for your transport channel (or change `Channels::SELECT` if 16 is used for transport).
2. In `MidiButtonConfig.cpp`, change the channel in each `ButtonConfig` for notes 36, 37, 38, 39, 3 (e.g. replace `16` with `5`).
3. If using DROID, change the `channel` in the `[midiout]` block that drives B2.29, B2.30, etc., to 5.
4. Ensure channel 5 is in `RECORD_EXCLUDE_MIN`..`RECORD_EXCLUDE_MAX` if you do not want those button presses recorded as MIDI. (Channels 13–16 are excluded by default.)

---

## Record exclusion

Channels 13–16 are not recorded into loops (control-surface traffic). If you move buttons to channels 1–12, their note messages will be recorded. Either keep control on 13–16 or extend `RECORD_EXCLUDE_*` in `MidiConfig.h`.
