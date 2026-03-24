# Display (control surface)

- **256×64 OLED:** piano roll, cursors, brackets, track column.
- **16×2 LCD:** essential status (when enabled).

**Track column (OLED):** One letter per track (`-`, `P`, `O`, `R`, `A`, …). **`M`** if user-muted or hidden by solo on another row; **selected** track still shows real machine state under solo for editing.

See [`DisplayManager.cpp`](../../../src/DisplayManager.cpp) (`drawTrackStatus`, etc.).
