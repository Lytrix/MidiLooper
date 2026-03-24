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

Defaults line up with [`droid/midilooper_v1.ini`](droid/midilooper_v1.ini). To **remap**, edit [`include/MidiConfig.h`](include/MidiConfig.h), [`src/Utils/MidiButtonConfig.cpp`](src/Utils/MidiButtonConfig.cpp), and the [**MIDI config guide**](docs/Guides/MIDI_CONFIG_GUIDE.md). This table is **roles and gestures only** (no note numbers); the guide and header file carry the exact bytes.

Think of it as the **one-screen** version of how the grid is meant to feel. Full behavior (edge cases, timing windows, **HOLD_TWO**) is in the linked guides, especially [**jam-bar-step-phases**](docs/Guides/jam-bar-step-phases.md).

| Row | Short | Long | Double | Triple | Hold / two-step |
|-----|-------|------|--------|--------|-----------------|
| [**Scenes**](docs/Guides/control-surface/Scenes.md) | — | — | — | — | Roadmap / Phase 3 snapshots (not fully wired yet). |
| [**Tracks**](docs/Guides/control-surface/Tracks.md) | Select that track | Exclusive solo (repeat on same track clears solo) | Mute / unmute | — | — |
| [**Jams**](docs/Guides/control-surface/Jams.md) | — | — | — | — | Target row for jam-era capture; live jams today use **Bars** / **16ths** + this doc’s roadmap. |
| [**Loops**](docs/Guides/control-surface/Loops.md) | Slot-aware record, play, overdub, finalize (playing vs stopped—see guide) | Clear this slot’s loop | Slot undo | Slot redo | Past long-press threshold: **layer hold** (queue from base slot while playing); **short while queued** can punch in immediately. |
| [**Bars**](docs/Guides/control-surface/Bars-and-16ths.md) | Seek bar; outside jam region, move single-bar jam | Hold: enter **one-bar** jam or start **two-bar** range (see jam guide) | Exit jam → full loop | Undo loop start edit | **Hold bar A → press bar B** (timing in jam guide) for multi-bar jam. |
| [**16ths**](docs/Guides/control-surface/Bars-and-16ths.md) | Seek 16th in jam; set / jump region | Hold patterns for 16th jam / seek | Exit jam | Undo loop start (with jam guide) | **Hold 16th A → press 16th B** for 16th-range jam. |
| [**Main controls**](docs/Guides/control-surface/Main-controls.md) | **REC:** play / record / overdub · **MUTE/DE:** next track · **Edit:** NOTE ↔ LOOP · **NOTELEN:** pos ↔ length | **REC:** clear track · **MUTE/DE:** mute · **Edit:** exit | **REC:** undo overdub · **MUTE/DE:** undo clear · **Edit:** delete note | **REC:** redo overdub · **MUTE/DE:** redo clear | More transport buttons in the guide. |
| [**Faders**](docs/Guides/control-surface/Faders.md) | — (continuous) | — | — | — | **Move:** note select, loop start/length, 16th coarse/fine, pitch—roles switch with NOTE_EDIT vs LOOP_EDIT (see guide). |
| [**Display**](docs/Guides/control-surface/Display.md) | — | — | — | — | OLED piano roll + track column (letters / solo / mute); 16×2 summary when used. |

**Hardware** is Teensy 4.1, SSD1322 256×64 OLED (or 16×2 LCD), 6N137 MIDI in, and optionally a **DROID** M4 + 2× B32 ([DROID](https://shop.dermannmitdermaschine.de/pages/droid-universal-cv-processor)). The MIDI input path follows the [PJRC MIDI library](https://www.pjrc.com/teensy/td_libs_MIDI.html) pattern.

For more depth: [loop start / length](docs/Guides/LOOP_START_EDITING.md), [jam phases](docs/Guides/jam-bar-step-phases.md), [note moves](docs/Guides/MOVE_NOTE_LOGIC.md), [fader state](docs/Guides/FADER_STATE_SYSTEM.md). A compact **Channel 16** listing lives under [**Config summary**](docs/Guides/MIDI_CONFIG_GUIDE.md#config-summary-default-droid-mapping) in the MIDI guide.

---

## Technical features

If you are here to **read numbers** or **repoint a controller**, start with [`include/MidiConfig.h`](include/MidiConfig.h), then [`docs/Guides/MIDI_CONFIG_GUIDE.md`](docs/Guides/MIDI_CONFIG_GUIDE.md) (checklist, tables, `MidiButtonConfig.cpp`, and keeping [`droid/midilooper_v1.ini`](droid/midilooper_v1.ini) aligned if you use the reference patch).

The full capability checklist is in [`docs/FEATURES.md`](docs/FEATURES.md). For **Handler / Manager / Processor / Actions** naming and a small module map, see [`docs/Guides/CODE_STRUCTURE.md`](docs/Guides/CODE_STRUCTURE.md).

Build with [PlatformIO](https://platformio.org/) (`platformio.ini`, board **Teensy 4.1**). The rest of the docs tree starts at [`docs/README.md`](docs/README.md); phase and export conventions are in [`docs/FEATURE_PLANS.md`](docs/FEATURE_PLANS.md) and [`docs/plans/README.md`](docs/plans/README.md).

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
