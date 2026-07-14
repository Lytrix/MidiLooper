# Jams (control surface)

**Applies to:** v3 (`dev`) — jam row reserved; jam capture today via Bars/16ths in LOOP_EDIT.

## Live jam behavior (today)

Bar and 16th buttons in **LOOP_EDIT** drive **jam regions**, `jamTick`, HOLD_ONE / HOLD_TWO, seek, and exit gestures. Full behavior: **[`../jam-bar-step-phases.md`](../jam-bar-step-phases.md)**.

## Jams row vs Loops row

**Loops** (notes 50–57 on the reference grid) are **per-track loop slots**: record, overdub, clear, undo/redo, and layering for **MIDI loop buffers**.

**Jams** (dedicated row on the **target** layout) is the planned place for **jam-era performance** and capture **without** folding that state into the same machine as multi-slot **Loops**—so loop-slot record flow stays simpler. Until the **Jams** row is fully wired in firmware and `midilooper_v1.ini`, treat the grid label as **target layout**; live jam use **Bars** + **16ths** as today.

## Placeholder: what to record into Jams (roadmap)

When capture exists, candidates include (**TBD** — not all are implemented yet):

| Signal | Intent |
|--------|--------|
| **Loop switching** | Which loop slot became active, and when, during a jam take |
| **`jamTick` resets** | Jam timeline / phase edits tied to `jamTick` (align vocabulary with [`jam-bar-step-phases.md`](../jam-bar-step-phases.md)) |
| **Pitch transposing** | Live transpose / harmony moves during jam for replay or export |

Track scope and MIDI mapping in **[`include/MidiConfig.h`](../../../include/MidiConfig.h)** and **[`../MIDI_CONFIG_GUIDE.md`](../MIDI_CONFIG_GUIDE.md)** when this ships. Roadmap: **[`../../FEATURE_PLANS.md`](../../FEATURE_PLANS.md)**, **[`../../plans/README.md`](../../plans/README.md)**.
