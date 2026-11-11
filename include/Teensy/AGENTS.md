# Teensy — folder notes

Read before editing a file in this folder. Rules: [CODE_HYGIENE.md § 4–5](../../docs/Authority/CODE_HYGIENE.md).
History behind these lines: DEC-298 (comment history, moved) § Teensy and main in the decision log.

## Owns
- The Teensy-only adapters (namespace `Teensy`): `OledScreen` (the SSD1322 panel: frames, windows, status band) and
  `SdCardIo` (`CardIo` over the SD library). The engine never includes them; it builds and is tested natively.

## Invariants
- A frame is drawn over several main-loop passes (`drawStage`: tracks and grid, the roll in `kRollBands` row bands,
  then status and windows) and sent whole by DMA; frames go to the off-screen GRAM half (page flip), so the panel
  never scans a half-written frame. Check a layout with `!ENGINE fbdump` and `scripts/render_display_dump.py`.
- The screen reads the engine only: nothing here composes, allocates or reads pages.
- Store writes use the adapter's own multi-block stream; `ready` never waits for the card to program.
- The adapter writes nothing before the owner approves saving (the engine folder exists).

## Traps
- The first frame after `begin` must go over blocking SPI: a first DMA frame leaves the panel blank.
- `snprintf`'s core stays in RAM1 (the linker script keeps `libc_a-svfprintf.o` in ITCM): from flash the status line
  costs about 4×, screen stage ~90 → ~390 µs. DEC-297 (deep-overdub timing).
- Don't use SdFat's `writeSector` for the store: it stops an open stream and waits 1.6–8.2 ms while the card programs.
- The panel dims as a whole as its summed load rises: keep windows light (no filled bright title bars, no grey
  interiors), and a full raster title flickers.

## Decisions
- DEC-253 / 254 (the store) · DEC-296 (source layout and names) · DEC-297 (deep-overdub timing)
