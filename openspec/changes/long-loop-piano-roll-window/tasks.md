## 1. Milestone 1 — bounded window + overview strip (display-only)

- [x] 1.1 Add per-slot detailed window state on `DisplayManager` (`detailedWindowStartTick` default 0, `detailedWindowBars` default 16 capped).
- [x] 1.2 When `loopLengthTicks > 16 * BAR_TICKS`, map only the window tick range onto `pianoRollWidth()`; when loop ≤ 16 bars, keep full-loop rendering and hide/collapse overview strip.
- [x] 1.3 Filter `DisplayNote` list and grid lines to window tick range (wrap-aware) before `drawAllNotes`; draw playhead in detailed layer only when playhead is inside the window.
- [x] 1.4 Implement overview strip: dynamic bar grouping (1/2/4/8/16/32/64), binary **has notes** / **no notes** segments, window box markers, playhead on strip at full-loop position.
- [x] 1.5 Resolve strip vertical placement on hardware (gap below piano roll vs lowest piano-roll row) — M1 uses row 31 strip, detailed roll y=0..30.
- [x] 1.6 Extend `#CAP DISP` with `windowStartTick`, `windowBars`, `windowNoteCount` when loop > 16 bars (`SC_DISP_WINDOW`).
- [x] 1.7 Native tests: tick-range window filter and bar-grouping selection table (`test_display_window_utils`).
- [ ] 1.8 HITL: 32-bar and 64-bar loops — bounded window fields present; no D1 regression (`frameNotes > 0` when `take > 0` during PLAY) — requires device capture; `#CAP DISP` fields ready.

## 2. Milestone 2 — LOOP_EDIT window move and resize

- [ ] 2.1 Gate controls to `NoteEditManager::MAIN_MODE_LOOP_EDIT` only.
- [ ] 2.2 Wire LOOP_EDIT fader 3 to move `detailedWindowStartTick` along loop (bar-quantized; clamp to valid range).
- [ ] 2.3 Wire hold-turn encoder to resize `detailedWindowBars` (1–16 bars; clamp start when needed).
- [ ] 2.4 Sync overview window box immediately when window moves or resizes.
- [ ] 2.5 Document control map in `LoopEditManager` / `MidiMapping` comments.
- [ ] 2.6 HITL or scripted MIDI: on 64-bar loop, move window to bars 16–31 and verify `#CAP DISP` reports matching `windowStartTick` and `windowNoteCount`; resize to 8 bars and verify `windowBars == 8`.

## 3. Verification and closeout

- [x] 3.1 Run `pio test -e native`.
- [ ] 3.2 Run HITL 32/64-bar display gates; store deterministic capture artifacts.
- [x] 3.3 Cross-link from [docs/Plans/long_loop_piano_roll_overview_enhancement.md](../../../docs/Plans/long_loop_piano_roll_overview_enhancement.md) to this change when complete.
- [ ] 3.4 Run `openspec validate long-loop-piano-roll-window` and prepare for `/opsx:apply`.

**Note:** M1 display-only shipped 2026-06-24; M2 controls + HITL sign-off remain open in this change.
