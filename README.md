[![License: PolyForm Noncommercial 1.0.0](https://img.shields.io/badge/License-PolyForm--Noncommercial%201.0.0-lightgrey.svg)](https://polyformproject.org/licenses/noncommercial/1.0.0/)

# Midi Looper

![midilooper.jpg](Images/midilooper.jpg)

## Midi Looper

A **multi-track MIDI looper** for **Teensy 4.1** with piano-roll editing, tight timing, and controller-first workflows. It is built for **live performance**: you layer loops, nail subdivisions, and bend arrangement **while the music runs**—not only in a stopped “setup” mode.

**Branch note:** This working branch targets a **full MIDI grid + faders** (reference layout below). A **minimal** encoder + few-buttons setup is a future goal; the architecture stays **MIDI-driven** so any controller can match [`include/MidiConfig.h`](include/MidiConfig.h).

---

## How it’s meant to be played

### Gesture-first

The looper assumes **intent from how you press**, not from hunting through modes: **short**, **long**, **double**, **triple**, and **hold** (including **hold → second press** for two-step gestures). The **primary** action on a row is usually a **short** press; stronger or rarer actions use **long** / **double** so you are not forced through “arm this mode first” for every move.

### Jam loops into a song

**Loops** are **immediate**—punch in, overdub, slice, retrigger. **Notes** are **detailed**—timing, length, velocity—on the **piano roll** and faders. **Jam regions** (bars and 16ths in **LOOP_EDIT**) let you **zoom** playback into a phrase, practice it, or perform inside a subset of the full loop. The aim is **flow**: building a song feels like **playing**, not like operating a spreadsheet.

### Jams vs Loops (why two ideas)

**Loops** means **per-track loop slots**: eight buffers per track for record, overdub, clear, and undo—see [**Loops**](docs/Guides/control-surface/Loops.md). Folding **everything** you do in a **jam** (regions, switches, transposition, tick logic) into that **same** state machine would get **heavy** fast.

A dedicated **Jams** concept (including a **Jams** row on the **target** hardware map) is where **jam-era performance** and capture can live **next to** slot **Loops**, not inside every slot’s FSM. Today, **live** jam behavior is already in the **Bars** and **16ths** rows; the **Jams** row itself is **roadmap**—see [**Jams**](docs/Guides/control-surface/Jams.md).

---

## Reference control layout

**Defaults** match the **reference** patch [`droid/midilooper_v1.ini`](droid/midilooper_v1.ini). **Remap** with [`include/MidiConfig.h`](include/MidiConfig.h), [`src/Utils/MidiButtonConfig.cpp`](src/Utils/MidiButtonConfig.cpp), and [**MIDI_CONFIG_GUIDE**](docs/Guides/MIDI_CONFIG_GUIDE.md).

Grid is **8 columns** × rows (plural labels). **No note numbers here**—only roles. Exact notes/CCs: **MIDI guide** + header.

| Row | Role | Detail |
|-----|------|--------|
| **Scenes** | Scenes / snapshots (roadmap) | [**Scenes**](docs/Guides/control-surface/Scenes.md) |
| **Tracks** | Select / mute / solo per track | [**Tracks**](docs/Guides/control-surface/Tracks.md) |
| **Jams** | Jam capture & performance row (target; see note above) | [**Jams**](docs/Guides/control-surface/Jams.md) |
| **Loops** | Eight loop slots on the selected track | [**Loops**](docs/Guides/control-surface/Loops.md) |
| **Bars** | Bar 1–8 (jam / seek with **16ths**) | [**Bars and 16ths**](docs/Guides/control-surface/Bars-and-16ths.md) |
| **16ths** | Sixteenth steps + playhead LEDs (from Teensy) | [**Bars and 16ths**](docs/Guides/control-surface/Bars-and-16ths.md) |
| **Main controls** | REC/PLAY, MUTE/DE, edit mode, NOTELEN, transport strip | [**Main controls**](docs/Guides/control-surface/Main-controls.md) |

**Faders:** [**Faders**](docs/Guides/control-surface/Faders.md)  
**Display:** [**Display**](docs/Guides/control-surface/Display.md)

**Channel 16 quick map** (legacy one-screen reference): see [**MIDI_CONFIG_GUIDE** § Config summary](docs/Guides/MIDI_CONFIG_GUIDE.md#config-summary-default-droid-mapping).

**Hardware:** Teensy 4.1, SSD1322 256×64 (or 16×2 LCD), 6N137 MIDI-in; optional **DROID** M4 + 2× B32—[Droid](https://shop.dermannmitdermaschine.de/pages/droid-universal-cv-processor). MIDI circuit: [PJRC MIDI library](https://www.pjrc.com/teensy/td_libs_MIDI.html).

**Deep dives (guides):** [Loop start / length editing](docs/Guides/LOOP_START_EDITING.md), [Jam phases](docs/Guides/jam-bar-step-phases.md), [Note move / wrap](docs/Guides/MOVE_NOTE_LOGIC.md), [Fader state](docs/Guides/FADER_STATE_SYSTEM.md).

---

## Technical features

**Read the numbers first:** [`include/MidiConfig.h`](include/MidiConfig.h) — channels, notes, CCs, LED bases.

**Remap any controller:** [`docs/Guides/MIDI_CONFIG_GUIDE.md`](docs/Guides/MIDI_CONFIG_GUIDE.md) — checklist, tables, `MidiButtonConfig.cpp`, and matching `droid/midilooper_v1.ini` if you use it.

**Full capability list:** [`docs/FEATURES.md`](docs/FEATURES.md).

**Code layout (Handler / Manager / Processor / Actions):** [`docs/Guides/CODE_STRUCTURE.md`](docs/Guides/CODE_STRUCTURE.md).

**Build:** [PlatformIO](https://platformio.org/) — `platformio.ini`, board **Teensy 4.1**.

**Documentation index:** [`docs/README.md`](docs/README.md) — guides, refinements, archived plans.

**Feature conventions / Phase 3 pointer:** [`docs/FEATURE_PLANS.md`](docs/FEATURE_PLANS.md). Exported design plans: [`docs/plans/README.md`](docs/plans/README.md).

---

## Credits and inspiration

Inspiration: [4×8 — minimal 4-track looper](https://iestyn-lewis.github.io/4by8/).

**Development tooling:** Early exploration used ChatGPT; most of the current structure and refactors were done with **Cursor** (Claude and similar models)—useful for navigating large C++ modules and keeping docs in sync with behavior.

**Chat logs (historical):**  
[MidiLooper (V1)](https://chatgpt.com/share/680a4839-6720-800b-ae73-9aff16f6e41f) · [MidiLooperV2](https://chatgpt.com/share/680e999a-c860-800b-a079-9862a59f1e89) · [MidiLooperV3](https://chatgpt.com/share/680e98f9-2a64-800b-abb2-4e1bd359c90f)

SSD1322 circular DMA bring-up was prototyped with Cursor against existing driver code.

---

## Examples

- [`examples/ButtonConfiguration40Example.cpp`](examples/ButtonConfiguration40Example.cpp) — MIDI button mapping example  
- [`droid/midilooper_v1.ini`](droid/midilooper_v1.ini) — Reference DROID patch  

---

© 2025 Lytrix (Eelke Jager)  
Licensed under the [PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/).  
Private use for further development with attribution is allowed. For commercial use or distribution, contact the author for a separate license.
