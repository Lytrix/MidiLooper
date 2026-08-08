## Why

Multi-loop **slots** and jam **playback** are shipped, but users cannot yet record a
live jam performance (loop switches, bar/16th navigation, composite audible sources)
into a **new slot**. Phase 3 deliverables D13–D15 remain the largest functional gap
after the timeline-epoch refactor (M7). OpenSpec should drive requirements and
scenarios before implementation so capture semantics (especially §2.3 merge model)
are locked with testable acceptance criteria.

## What Changes

- Add **arrangement / jam capture mode** (D13): record loop-button switches and jam
  performance into a designated `recordTargetSlot` while transport runs.
- Define **capture merge model** (R1): flattened MIDI stream at `currentTick` (model A)
  vs structural references (model B) — must be decided in specs before code.
- Implement **browse-while-overdub / browse-while-recording** rules (phase-3 §5):
  view/playback slot may differ from capture target slot.
- Add **arrangement playback** verification (D14): replay captured arrangement per
  locked merge model; native + HITL checklists per phase-3 §6 modes.
- Optional **scenes** (D15): snapshot `activeLoopIndex` per track — out of initial
  apply scope unless explicitly pulled in.

## Capabilities

### New Capabilities

- `jam-recording`: Capture live jam/arrangement performance into a target loop slot
  (D13, D14); includes merge model, slot routing, and test scenarios.

### Modified Capabilities

- `multi-loop-slots`: Slot state machine gains arrangement capture target vs view
  slot distinction and recording modes from phase-3 §6.

## Impact

- **Code:** `Track`, `TrackManager`, `SlotStateMachine`, jam state on `Track`,
  `ClockManager` tick sources (`currentTick` vs `jamTick`), `DisplayManager`,
  `TrackUndo`, `StorageManager` (if capture affects save format).
- **Docs:** Supersedes ad-hoc slices in `docs/Plans/phase-3-multi-loop.md` §8–9 for
  new work; keep `docs/DELIVERABLE_TRACKING.md` in sync on archive.
- **Tests:** New native scenarios for slot routing; HITL baseline extensions for
  capture timing; phase-3 §6 mode checklists.
- **Dependencies:** R1–R5 open decisions in
  `docs/Plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md` §0.2 must be resolved
  in delta specs before `/opsx:apply`.
