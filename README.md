[![License: PolyForm Noncommercial 1.0.0](https://img.shields.io/badge/License-PolyForm--Noncommercial%201.0.0-lightgrey.svg)](https://polyformproject.org/licenses/noncommercial/1.0.0/)

# Midi Looper Droid

![midilooper_droid.jpg](Images/midilooper_droid.jpg)

## Midi Looper with Droid MotoFader and B24

This is the codebase to run a **multi-track MIDI looper** on a **Teensy 4.1** with live loop jaming manipulations, piano-roll note/cc editing, tight hardware serial timing via minijack midi outs and usb on a 192 PPQN clock with hands on control workflow. It is built for **live performance**: record and layer multiple loop slots per track, remix them by quantized slot switching into new jams **while the music never needs to stop running** to keep into the vibe of the music with undo/redo capabilities.

This branch is aimed at a **8x8 button grid with 4 motorized faders** 
A smaller **encoder-and-4-buttons** rig is a direction for later but was the MVP; underneath this codebase, everything stays **MIDI-addressable**, so another controller can be made into a dedicated controller for this midi looper by updating [`include/MidiConfig.h`](include/MidiConfig.h) to your controller setup.

---

## How it’s meant to be played

### Gesture-first

The looper assumes **intent from how you press**, not from hunting through menus. Every button has the same 5 gestures to get to any function FAST!
**short**
**long**
**double**
**triple**
**hold + second press**

The **primary** action is always a **short** press; extremer edits like delete or undo actions use **long** / **double**.

### Remix loops into a song by jamming

**Loops** are **immediate**—punch in, overdub, slice, retrigger. 
**Notes** are **detailed**—timing, length, velocity—on the **piano roll** and faders. 
**Jam regions** (bars and 16ths in **LOOP_EDIT**) let you **zoom** playback into a new Loop, practice it, or perform inside a subset of the full loop and recording this jam into a new loop. The aim is **flow**: building a song feels like **playing**, not like operating a spreadsheet.

### Jams vs Loops

**Loops** means **per-track loop slots**: eight buffers per track for record, overdub, clear, and undo—see [**Loops**](docs/Guides/control-surface/Loops.md). Slot switching is quantized and each track can run a multi-slot enabled set with per-slot mute/enable states.

**Remixes** of earlier recorded loops is where **jam performance** comes alive. In **Loop edit** the **Bars** and **16ths** buttons can be triggered/looped and recorded into new loops.

---

## Buttons

The current setup is based on Droid using this config file: [`droid/midilooper_v1.ini`](droid/midilooper_v1.ini). To **remap**, edit [`include/MidiConfig.h`](include/MidiConfig.h) and [`src/Utils/MidiButtonConfig.cpp`](src/Utils/MidiButtonConfig.cpp). More info can be found in the [**MIDI config guide**](docs/Guides/MIDI_CONFIG_GUIDE.md) and the [**config summary**](docs/Guides/MIDI_CONFIG_GUIDE.md#config-summary-default-droid-mapping) for Channel 16.

Below is the **cheet sheet** of all available the button gestures.

| Row | Short | Long | Double | Triple | Hold / two-step |
|-----|-------|------|--------|--------|-----------------|
| [**Scenes**](docs/Guides/control-surface/Scenes.md) | — | — | — | — | Roadmap / Phase 3 snapshots |
| [**Tracks**](docs/Guides/control-surface/Tracks.md) | Select that track | Exclusive solo | Mute / unmute | — | — |
| [**Jams**](docs/Guides/control-surface/Jams.md) | — | — | — | — | Target row for jam-era capture |
| [**Loops**](docs/Guides/control-surface/Loops.md) | Select/record slot; when playing, switch slot quantized to next 16th; selected slot toggles mute | If pressed slot is selected and filled: clear slot. If not selected and filled: queue single-slot switch at loop end | Slot undo | Slot redo | Hold one or more slots, release to commit multi-slot enabled set on next 16th |
| [**Bars**](docs/Guides/control-surface/Bars-and-16ths.md) | Seek bar; move single-bar jam | Enter one-bar or two-bar jam | Exit jam | Undo loop start edit | Hold bar A → press bar B for range |
| [**16ths**](docs/Guides/control-surface/Bars-and-16ths.md) | Seek 16th in jam; set / jump region | 16th jam / seek | Exit jam | Undo loop start | Hold 16th A → press 16th B for range |
| [**Main controls**](docs/Guides/control-surface/Main-controls.md) | REC/PLAY, MUTE/DE, Edit, NOTELEN | Clear / mute / exit | Undo overdub / undo clear / delete note | Redo overdub / redo clear | More transport buttons in guide |

## Faders

The four faders sit beside the button grid. Their role changes depending on whether you are in **NOTE_EDIT** or **LOOP_EDIT**.

| Fader | NOTE_EDIT | LOOP_EDIT |
|-------|-----------|-----------|
| **Fader 1** | Note selector | Loop start point |
| **Fader 2** | 16th coarse position | Loop length / loop end |
| **Fader 3** | 16th fine offset | — |
| **Fader 4** | Note pitch | — |

Midi channels, CCs, and pitchbend mappings can be found in [`include/MidiConfig.h`](include/MidiConfig.h) and the [**MIDI config guide**](docs/Guides/MIDI_CONFIG_GUIDE.md). More detail: [**Faders**](docs/Guides/control-surface/Faders.md), [loop start editing](docs/Guides/LOOP_START_EDITING.md), and [fader state](docs/Guides/FADER_STATE_SYSTEM.md).

## Display

The display is built around the **piano roll** with loop and current note played information and **track status strip**. After recording a first loop you often want to edit small parts in detail or shift notes. It also shows what part of the loop is active, selected slot focus, and what you are editing right now. That means the display is not just “status”; it is part of how you shape your groove, ofsett start end boundaries, all while the loop is running.

The **track column** gives a quick status for each track using letters such as `-` Empty, `P(lay)`, `O(verdub)`, `R(ecord)`, `M(uted)`, `S(topped)` and `A(rmed)`.

For more detail: [**Display**](docs/Guides/control-surface/Display.md).

---

## Technical features

**Hardware** is Teensy 4.1, SSD1322 256×64 OLED or 16×2 LCD, 6N137 MIDI in, and in this case a **DROID** M4 + 2× B32—[product page](https://shop.dermannmitdermaschine.de/pages/droid-universal-cv-processor). MIDI wiring follows the [PJRC MIDI library](https://www.pjrc.com/teensy/td_libs_MIDI.html) pattern.

For more details on the logic: [loop start / length](docs/Guides/LOOP_START_EDITING.md), [jam phases](docs/Guides/jam-bar-step-phases.md), [note moves](docs/Guides/MOVE_NOTE_LOGIC.md), [fader state](docs/Guides/FADER_STATE_SYSTEM.md). A compact **Channel 16** listing lives under [**Config summary**](docs/Guides/MIDI_CONFIG_GUIDE.md#config-summary-default-droid-mapping) in the MIDI guide.

State persistence uses storage **version 3** and saves all track/slot loop data, per-slot enabled/muted flags, selected track, active slot per track, and slot-level undo/redo histories.
**Read the numbers first:** [`include/MidiConfig.h`](include/MidiConfig.h) — channels, notes, CCs, LED bases.

**Remap any controller:** [`docs/Guides/MIDI_CONFIG_GUIDE.md`](docs/Guides/MIDI_CONFIG_GUIDE.md) — checklist, tables, `MidiButtonConfig.cpp`, and matching `droid/midilooper_v1.ini` if you want to make your own Droid setup.

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
