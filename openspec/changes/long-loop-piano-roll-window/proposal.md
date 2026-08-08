## Why

Loops longer than 16 bars compress the full loop onto the piano-roll width in `DisplayManager::drawPianoRoll`, making notes unreadable and forcing the display path to work with more note data than the 32-row framebuffer can usefully show. Display scaling was deferred from `record-stop-64-bar-crash` (archived) because the RAM2 crash fix (`long-record-memory-headroom`, **shipped**) had to land first.

Brownfield plan: [docs/Plans/long_loop_piano_roll_overview_enhancement.md](../../../docs/Plans/long_loop_piano_roll_overview_enhancement.md).

## What Changes

- **M1 (display-only):** When loop length exceeds 16 bars, cap the detailed piano-roll view at 16 bars (default window at loop start, tick 0) and add a full-width **overview strip** with binary grouped note presence, a **window box** marking the detailed region, and a playhead indicator on the strip when the playhead is outside the detailed window.
- **M2 (LOOP_EDIT navigation):** In **LOOP_EDIT** mode, let the user move the detailed window along the loop and resize it from 1 to 16 bars (fader / encoder — control map TBD before M2).
- Extend `#CAP DISP` (or a companion `#CAP OVW` line) so HITL can verify bounded-window behavior on 32/64-bar loops without full-frame dumps.
- Add native tests for tick-range window filtering and overview bar-grouping selection.

Delivery is phased in one change: M1 display-only first, then M2 LOOP_EDIT controls.

## Capabilities

### New Capabilities
- `long-loop-piano-roll-window`: Long loops render a bounded detailed piano-roll window (max 16 bars), a full-loop overview strip with window box, and LOOP_EDIT navigation to move/resize the window.

### Related Capabilities (referenced, not redefined here)
- `long-record-memory-headroom`: prerequisite — **shipped**; restores reliable 48/64-bar record without crash.
- `edit-record-display-length-mode`: D1 live-record display gates must not regress when the window filter is added.

## Impact

- **Apply order:** after `long-record-memory-headroom` (shipped; 48/64-bar HITL passed).
- Affected firmware: `DisplayManager.cpp` / `DisplayManager.h` (window state, filter, overview strip), `LoopEditManager.cpp` (M2 controls), `DebugSessionCapture.h` (DISP/OVW markers).
- Affected verification: `scripts/host_midi_automation_baseline.py` or dedicated `scripts/test_long_loop_display_serial_verify.py`; native window-filter tests.
- Carries forward display scope from archived `record-stop-64-bar-crash` §6 (`openspec/changes/archive/2026-06-22-record-stop-64-bar-crash/PARKED.md`).
- Non-goals: loop storage/capture/persistence changes, NOTE_EDIT piano-roll changes, jam full-arrangement navigation, overview note-density/velocity shading in v1.
