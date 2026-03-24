[![License: PolyForm Noncommercial 1.0.0](https://img.shields.io/badge/License-PolyForm--Noncommercial%201.0.0-lightgrey.svg)](https://polyformproject.org/licenses/noncommercial/1.0.0/)

# Midi Looper

![midilooper.jpg](Images/midilooper.jpg)

This is a **multi-track MIDI looper** for **Teensy 4.1** with piano-roll editing, solid timing, and a controller-first layout. It is meant for **live work**: you build layers, nudge subdivisions, and reshape the arrangement **while audio is moving**, instead of living in a permanent “stopped, configuring” head space.

This branch is aimed at a **full MIDI grid plus faders** (the reference layout below). A smaller **encoder-and-a-few-buttons** rig is a direction for later; underneath, everything stays **MIDI-addressable**, so another controller can follow the same map by matching [`include/MidiConfig.h`](include/MidiConfig.h).

---

## How it’s meant to be played

**Gesture-first:** the box reads **how you press** more than **which mode you declared**. Short presses carry the everyday action on a row; long, double, and triple presses (and **hold**, including **hold then a second press**) carry the rarer or heavier moves, so you are not forced to step through a separate “arm” mode before every gesture.

**Loops, notes, and jams:** loop recording stays **fast and direct**—punch, overdub, retrigger. Note work lives in the **piano roll** and faders when you want **fine** timing, length, and velocity. In **LOOP_EDIT**, **bars** and **16ths** define **jam regions** so you can loop a phrase, drill it, or perform inside part of the full loop. The point is **flow**: finishing a sketch should feel closer to **playing** than to **operating a spreadsheet**.

**Why “Jams” and “Loops” are separate ideas:** **[Loops](docs/Guides/control-surface/Loops.md)** here means the **eight loop slots per track**—record, overdub, clear, undo, with a dedicated state machine. Jam-style performance (regions, tick feel, switching, transposition ideas) is a lot to fold into **that same** slot logic without turning every feature into a special case. A dedicated **[Jams](docs/Guides/control-surface/Jams.md)** surface—row on the **target** map—is the place jam-centric **capture** can live **next to** slot loops. **Today**, live jam behavior already comes from the **Bars** and **16ths** rows; the dedicated **Jams** row is **roadmap** until firmware and the patch catch up.

---

## Reference control layout (cheat sheet)

Defaults line up with [`droid/midilooper_v1.ini`](droid/midilooper_v1.ini). To **remap**, edit [`include/MidiConfig.h`](include/MidiConfig.h), [`src/Utils/MidiButtonConfig.cpp`](src/Utils/MidiButtonConfig.cpp), and the [**MIDI config guide**](docs/Guides/MIDI_CONFIG_GUIDE.md). This table is **roles and gestures only**; exact notes and CCs are in the guide, `MidiConfig.h`, and the [**config summary**](docs/Guides/MIDI_CONFIG_GUIDE.md#config-summary-default-droid-mapping) for Channel 16.

Think of it as the **one-screen** feel of the grid. Edge cases, **HOLD_TWO** timing, and seek rules are in the linked guides—start with [**jam-bar-step-phases**](docs/Guides/jam-bar-step-phases.md).

| Row | Short | Long | Double | Triple | Hold / two-step |
|-----|-------|------|--------|--------|-----------------|
| [**Scenes**](docs/Guides/control-surface/Scenes.md) | — | — | — | — | Roadmap |
| [**Tracks**](docs/Guides/control-surface/Tracks.md) | Select track | Solo¹ | Mute | — | — |
| [**Jams**](docs/Guides/control-surface/Jams.md) | — | — | — | — | See note² |
| [**Loops**](docs/Guides/control-surface/Loops.md) | Slot gestures³ | Clear slot | Slot undo | Slot redo | Layer⁴ |
| [**Bars**](docs/Guides/control-surface/Bars-and-16ths.md) | Seek bar⁵ | Jam entry⁵ | Exit jam | Undo⁵ | Range⁵ |
| [**16ths**](docs/Guides/control-surface/Bars-and-16ths.md) | Seek⁵ | Jam⁵ | Exit jam | Undo⁵ | Range⁵ |
| [**Main controls**](docs/Guides/control-surface/Main-controls.md) | Strip short⁶ | Strip long⁶ | Strip dbl⁶ | Strip tpl⁶ | More in guide⁶ |
| [**Faders**](docs/Guides/control-surface/Faders.md) | — | — | — | — | Move⁷ |
| [**Display**](docs/Guides/control-surface/Display.md) | — | — | — | — | See note⁸ |

**Notes**  
¹ **Tracks:** long = exclusive solo; long again on the **same** track clears solo.  
² **Jams:** live jam today is [**Bars / 16ths**](docs/Guides/control-surface/Bars-and-16ths.md); future **Jams** row in [**Jams**](docs/Guides/control-surface/Jams.md).  
³ **Loops:** short is slot-aware record / play / overdub—depends on transport and slot data; [**Loops**](docs/Guides/control-surface/Loops.md).  
⁴ **Loops:** hold arms layer / queue; second short can punch in—[**Loops**](docs/Guides/control-surface/Loops.md).  
⁵ **Bars / 16ths:** requires **LOOP_EDIT**; double exits jam; triple undoes loop start edit; full timing in [**jam-bar-step-phases**](docs/Guides/jam-bar-step-phases.md).  
⁶ **Main controls:** REC/PLAY, MUTE/DE, Edit, NOTELEN, transport—[**Main-controls**](docs/Guides/control-surface/Main-controls.md). “dbl” = double, “tpl” = triple.  
⁷ **Faders:** continuous; NOTE_EDIT vs LOOP_EDIT roles in [**Faders**](docs/Guides/control-surface/Faders.md).  
⁸ **Display:** OLED piano roll and track column; [**Display**](docs/Guides/control-surface/Display.md).

**Hardware** is Teensy 4.1, SSD1322 256×64 OLED or 16×2 LCD, 6N137 MIDI in, and optionally a **DROID** M4 + 2× B32—[product page](https://shop.dermannmitdermaschine.de/pages/droid-universal-cv-processor). MIDI wiring follows the [PJRC MIDI library](https://www.pjrc.com/teensy/td_libs_MIDI.html) pattern.

For more depth: [loop start / length](docs/Guides/LOOP_START_EDITING.md), [jam phases](docs/Guides/jam-bar-step-phases.md), [note moves](docs/Guides/MOVE_NOTE_LOGIC.md), [fader state](docs/Guides/FADER_STATE_SYSTEM.md). A compact **Channel 16** listing lives under [**Config summary**](docs/Guides/MIDI_CONFIG_GUIDE.md#config-summary-default-droid-mapping) in the MIDI guide.

---

## Technical features

If you are here to **read numbers** or **repoint a controller**, open [`include/MidiConfig.h`](include/MidiConfig.h) first, then [`docs/Guides/MIDI_CONFIG_GUIDE.md`](docs/Guides/MIDI_CONFIG_GUIDE.md) for the remap checklist, tables, and `MidiButtonConfig.cpp`. Keep [`droid/midilooper_v1.ini`](droid/midilooper_v1.ini) aligned if you rely on that reference patch.

The full capability checklist is in [`docs/FEATURES.md`](docs/FEATURES.md). **Handler / Manager / Processor / Actions** naming and a small module map are in [`docs/Guides/CODE_STRUCTURE.md`](docs/Guides/CODE_STRUCTURE.md).

Build with [PlatformIO](https://platformio.org/) using `platformio.ini` and board **Teensy 4.1**. The rest of the docs tree starts at [`docs/README.md`](docs/README.md). Phase and export conventions live in [`docs/FEATURE_PLANS.md`](docs/FEATURE_PLANS.md) and [`docs/plans/README.md`](docs/plans/README.md).

---

## Credits and inspiration

The original spark was [4×8](https://iestyn-lewis.github.io/4by8/), a very small four-track looper with two buttons and four digits—proof that a loop can stay simple on the surface.

Early experiments used ChatGPT; most of the current C++ structure and refactors were done in **Cursor** with Claude-class models, which helped a lot when tracing large modules and keeping documentation honest against the code. Historical chat exports: [MidiLooper (V1)](https://chatgpt.com/share/680a4839-6720-800b-ae73-9aff16f6e41f) · [MidiLooperV2](https://chatgpt.com/share/680e999a-c860-800b-a079-9862a59f1e89) · [MidiLooperV3](https://chatgpt.com/share/680e98f9-2a64-800b-abb2-4e1bd359c90f). The SSD1322 circular DMA path was iterated with Cursor on top of existing driver code.

---

## Examples

- [`examples/ButtonConfiguration40Example.cpp`](examples/ButtonConfiguration40Example.cpp) — MIDI button mapping example  
- [`droid/midilooper_v1.ini`](droid/midilooper_v1.ini) — reference DROID patch  

---

© 2025 Lytrix (Eelke Jager)  
Licensed under the [PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/).  
Private use for further development with attribution is allowed. For commercial use or distribution, contact the author for a separate license.
