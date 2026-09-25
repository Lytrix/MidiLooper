# Decision review — recoverable-async-display-dma

**Date:** 2026-09-12
**Change:** Recoverable asynchronous SSD1322 DMA transport

## Historical review

### Searched

- [x] [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md)
- [x] Active OpenSpec: this change
- [x] [`openspec/specs/`](../../specs/) for display-adjacent behavior
- [x] [`docs/Plans/`](../../../docs/Plans/) display and runtime scheduling handoffs

**Search terms:** `DisplayManager`, `SSD1322`, `OLED`, `DMA`, `SPI`, `LPSPI`,
`timeout`, `retry`, `midi_gap`, and `ExecutionBounds`.

### Relevant findings

- **DEC-043** requires evidence before describing timing work as bounded. This
  change reports measured CPU submission and wire-time availability rather than
  creating a scheduler or promising a timing bound.
- No decision assigns OLED bus recovery elsewhere or supplies an equivalent DMA
  abort/reset operation.

### Existing reusable pattern

`DisplayManager` owns the existing resolve/draw/transfer phase lifecycle;
`SSD1322_API` owns command/data transport; `TsyDMASPI::MasterBase` owns the DMA
transaction and its hardware state.

### Reuse decision

**YES.** Extend those owners only. The state is transport-local and fixed-size;
it does not become loop, Capture, pass, persistence, or MIDI scheduling state.

### Migration required

**NO.** The retained blocking SPI transfer is the rollback path. No SD, data,
or compatibility migration is needed.
