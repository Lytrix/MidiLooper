# Project intent

This is the canonical "why" of the project. When a feature idea, refactor, or doc conflicts with this page, this page wins — or this page gets deliberately updated first.

Authority index: [README.md](README.md).

## The essence

**A song creation device that feels like playing an instrument, not operating software.**

The reference feeling is Cubase on an Atari: you sat down, ideas went in immediately, and the machine sort of disappeared. Only notes and parts to arrange nothing more. One hand on the device, attention on the music.

## The workflow it serves

Everything in the firmware exists to serve this loop:

1. **Capture** — record MIDI the moment the idea happens; the music never stops.
2. **Layer** — overdub and build loops fast.
3. **Refine** — surgical timing/note edits to fine-tune the groove (piano roll, faders) never stopping the music.
4. **Remix** — rearrange and retrigger parts in realtime; capture this jam itself as a new way of music creation.
5. **Song** — massage the captured vibe with the parts at hand and tools to fix small mistakes on the fly or fine tune grooves into a finished structure.

Steps 1, 2, and 4 are the identity of the device. Step 3 exists *in service of the groove* — the editor is a tool inside the instrument, not the product. Step 5 is where Phase 3 (jam/arrangement capture) is headed.

## The core question this project answers

**What is the smallest control surface that makes full song creation feel like playing?**

The working hypothesis: **one encoder + 4 buttons** is enough for the entire core workflow. MIDI extension surfaces (DROID grid, iPad port) exist to *test* richer UX layouts — they are experiments for discovering what feels pleasant, never requirements for core capability.

**The mechanism that makes this possible: gesture-first control.** Every control is multiplexed through the same five gestures — **short**, **long**, **double**, **triple**, **hold + second press** — so one button carries many functions without menus. The primary action is always a short press; extremer actions (delete, undo) use long/double. Intent comes from *how* you press, not from hunting through screens. (Inspired by the [4×8 minimal looper](https://iestyn-lewis.github.io/4by8/), which drives a full workflow from 2 buttons this way.)

## Litmus tests for any new feature or refactor

Ask these before building. A "no" means redesign it or drop it:

1. **One hand:** can it be done single-handed on the base module (encoder + 4 buttons)?
2. **Music keeps running:** does it work without stopping the transport?
3. **Less distraction, not more:** does it remove attention from the device, or demand it?
4. **Serves the loop:** which of the five workflow steps does it serve? If none, it doesn't belong.
5. **Gesture-first:** can it map onto the five existing gestures on an existing control? A new feature that needs a new button, screen, or menu before a gesture mapping has been tried is suspect.

## What this is not

- **Not a DAW replacement.** Deep menus, 2 hand operations and multiple configuration screens are failure modes.
- **Not a DROID instrument.** The DROID is the first extension surface; the firmware must never require it.
- **Not an editor first.** The piano roll is rich, but if editing grows at the expense of capture/remix flow, that is drift (it has happened: `NoteEditManager` is the largest module in the codebase but was needed to solve the complexity of selecting through changing datamodel updates).
- **Not a feature collection.** A smaller device that flows beats a bigger one that doesn't.

## Key decisions (verified Jun 2026)

1. **Base configuration is the encoder + 4 GPIO buttons module.** It existed physically but was left behind during DROID development (`ButtonManager` exists in `src/` but is never called from `main.cpp`; its pins are still defined in `include/Globals.h` under `Buttons`).
2. **The base module alone must cover the full core workflow** — record, overdub, undo, track/slot navigation, basic edits. MIDI surfaces add convenience, never capability.
3. **Priority order: reliability first, then Phase 3 jam/arrangement capture.** Reliability means fixing known bugs *and* a verification regime (`pio test -e native` plus structured manual hardware tests) so changes stop breaking things.
4. **One surface-agnostic action layer, in both directions.** GPIO and MIDI controls map onto the same actions (input), and faders/LEDs/display share one feedback path (output). Today actions live only in `MidiButtonActions` and fader feedback is coupled to `MidiFaderManager`.
5. **Portability target (fall iteration): Randomwaves Drumboy Pro** — an open-source (MIT) STM32 Cortex-M7 groovebox, C++, with MIDI I/O, SD card, 5.3-inch LCD, pads, and 8 encoders. Refactoring must separate a **platform-independent core** (track/loop/slot logic, clock math, undo, note editing, gesture-to-action mapping) from a **thin platform layer** (MIDI transport, storage, display, controls, timers, PSRAM allocation — all Teensy/Arduino-specific today: `USBHost_t36`, `EXTMEM`, Arduino `SD`/MIDI, SSD1322 driver). Whatever compiles for `pio test -e native` is the platform-independent surface; growing that surface is the measure of progress.

## Open decisions (do not assume; verify at a checkpoint)

- Whether the base-module revival comes before or after jam capture.
- How the looper lives on the Drumboy Pro (replace its firmware vs. integrate alongside its engine) — a fall decision; the only commitment now is the core/platform split.

## Current status

See [DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md) for the single shipped-vs-next overview. Historical claims in older docs (`Refinements/`, `Plans/`) may lag the code; this page and the tracking page are the references.
