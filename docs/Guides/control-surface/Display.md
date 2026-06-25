# Display (control surface)

- **256×64 OLED:** piano roll, cursors, brackets, track column.
- **16×2 LCD:** essential status (when enabled).

**Track column (OLED):** One letter per track (`-`, `P`, `O`, `R`, `A`, …). **`M`** if user-muted or hidden by solo on another row; **selected** track still shows real machine state under solo for editing.

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
