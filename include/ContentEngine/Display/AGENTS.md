# Display — folder notes

Read before editing a file in this folder. Rules: [CODE_HYGIENE.md § 4–5](../../../docs/Authority/CODE_HYGIENE.md).
History behind these lines: DEC-298 (comment history, moved) § Display in the decision log. The screen itself is
`include/Teensy/OledScreen.h`.

## Owns
- `DisplayTiles`: the piano-roll tiles (density and detail), their sweeps as scheduler jobs, and the live overlay of a
  take being recorded. `PianoRollRender`: drawing the note layer from tiles in row bands into the 4-bit framebuffer.
  `StatusView`: the rules behind the status band's fields, tested on the host.

## Invariants
- The draw path reads tiles and the overlay only: it never composes, allocates or reads pages.
- An empty or cleared slot shows nothing; a swept slot is never blank (a density line until its detail tile exists).
- The overlay keeps notes in loop positions, so it draws at any scale: a first take's view grows with the clock.
- The display cursor is shared with the NOTE_EDIT gather: a gather stops the running sweeps.
- A note's first pixel is brighter than its bar, so touching notes on a pitch read as separate; a muted note is dim.
- The preview roll (get a loop) has its own tile slot past the 64 working-set slots.

## Decisions
- DEC-255 (muted note bit) · DEC-271 (audition) · DEC-297 (deep-overdub timing: the stepped bind in the sweeps)
