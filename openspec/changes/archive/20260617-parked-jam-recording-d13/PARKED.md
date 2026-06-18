# Parked — do not apply

**Status:** Deferred (2026-06-17)

## Why parked

This change followed **Phase 3 D13** (arrangement capture — MIDI into a target slot).
The **timeline data model plan** places jam work **after M8** as **JamRecorder +
JamAction** (D3 persisted actions), not slot MIDI capture first.

## Prerequisite sequence

1. **M8 rename** — `m8-rename` (Take / Capture vocabulary)
2. **M8 edit** — `m8-edit` (Edit op-lists + EditSession)
3. **Hardening** — pool metrics, playback-window polish (timeline plan post-M7 todos)
4. **JamRecorder prototype** — new OpenSpec change `jam-recorder` (actions, not D13 MIDI)
5. **M10** — full Jam entity + Scenes + SD
6. **D13 arrangement MIDI capture** — only after R1–R5 locked (`phase-3-multi-loop.md` §9)

## To revive

Do not continue this folder as-is. When ready:

- Resolve R1 (merge model A vs B) and R3–R5 from
  `docs/plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md` §0.2
- `/opsx:propose jam-recorder` per timeline Workstream E, or a new D13 change after M10

Artifacts here are kept for reference only.
