> **Runtime scope (2026-11-08):** the reference below describes legacy v3, built by `teensy41-capture-serial`. For the current fixed-memory runtime (`teensy41-engine`), use the [engine guide](../FIXED_MEMORY_ENGINE.md). In particular, engine slot double clears, slot long opens length, and the legacy jam/overlay gestures are unavailable.

# Display (control surface)

**Applies to:** v3 (`dev`) — SSD1322 piano roll + track strip on current firmware.

- **256×64 OLED:** piano roll, cursors, brackets, track column.
- **16×2 LCD:** essential status (when enabled).

**Track column (OLED):** One letter per track (`-`, `P`, `O`, `R`, `A`, …). **`M`** if user-muted or hidden by solo on another row; **selected** track still shows real machine state under solo for editing.

## Boot title submission

`DisplayManager::beginBootOled` initializes the panel and framebuffer dimensions without
submitting a blank frame. The immediately following `drawBootScreen` clears/draws the
complete title and owns the first transfer, so an in-flight blank DMA frame cannot drop
the title request. DMA stays enabled; `update` polls completion during the existing boot
hold. Normal `setup` clearing and transport fallback/retry are unchanged.
[Regression/build and authorized upload evidence; owner confirmed title and no hang on this boot](../../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-11-boot-title-first-frame/EXECUTION.md).

## Long-loop bounded window

When a loop is **longer than 16 bars**, the detailed piano roll shows at most a **16-bar window** plus a full-width **overview strip** below the roll (window box + playhead on the full loop).

| Mode | Window behavior |
|------|-----------------|
| Playback / record / overdub | Window auto-follows the playhead |
| NOTE_EDIT | Window **frozen** at the last position |
| Hold **play/stop** (note 40) | Temporary auto-follow (even in NOTE_EDIT) |
| Long-press **play/stop** | One-shot center on current playhead |

Capture builds emit `#CAP DISP` fields `windowStartTick`, `windowBars`, `windowNoteCount` for HITL verification (`long_loop_display_window` scenario).

See [`DisplayManager.cpp`](../../../src/DisplayManager.cpp) (`drawPianoRoll`, `drawOverviewStrip`).
