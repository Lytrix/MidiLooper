# Preflight — recoverable-async-display-dma

**Mode:** Lightweight — existing-owner extension; no formal reassessment trigger
or ownership transfer.

## Problem

The OLED full-frame transfer is CPU-blocking despite a dormant DMA path. The
change makes that physical transfer recoverable and asynchronous while retaining
the current blocking SPI frame as a safe fallback. It serves the project intent
that music continues while the display updates.

## Domain

Display platform layer: `DisplayManager`, `SSD1322_API`, and the bundled
Teensy-specific `TsyDMASPI` wrapper.

## Historical review

### Searched

- [x] [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md)
- [x] [`docs/Plans/`](../../../docs/Plans/) display, runtime-scheduling, and
  long-record display-stall handoffs
- [x] This active OpenSpec and archived specifications

**Search terms:** `DisplayManager`, `SSD1322`, `OLED`, `DMA`, `SPI`, `LPSPI`,
`timeout`, `retry`, `ExecutionBounds`, and `midi_gap`.

### Relevant findings

- **DEC-043:** a timing claim needs a declared bound or evidence; the DMA change
  therefore measures submission and on-wire availability and does not claim a
  hard frame-rate guarantee.
- Current runtime guidance identifies display work as part of the active
  persistence/overlay/display-polish scope; it forbids a new interval scheduler
  or reservation.
- Existing display refinements retain `DisplayManager` as the display owner and
  use phased resolve/draw/transfer work. No DMA/SPI recovery owner or equivalent
  wrapper abort method exists.

## Existing owner and reuse decision

| Scope | Owner | Extension |
|---|---|---|
| Frame scheduling and redraw coalescing | `DisplayManager` | Poll transport before admitting the next transfer phase |
| SSD1322 commands, CS/DC, framebuffer transport | `SSD1322_API` | Start, poll, and recover one physical frame |
| DMA transaction/peripheral reset | `TsyDMASPI::MasterBase` | Public wrapper-owned `abortAndReset()` |

**Reuse possible:** YES. The change extends existing owners. No new manager,
scheduler, persistent state owner, or compatibility layer is introduced.

## Loaded docs

- [x] `docs/Runtime/PROJECT_STATE.md`
- [x] `docs/Runtime/CURRENT_WORK.md`
- [x] `docs/DECISION_LOG.md`
- [x] `docs/Authority/PROJECT_INTENT.md`
- [x] `docs/Authority/ARCHITECTURE_RULES.md`
- [x] `docs/Authority/DELIVERY_RULES.md`
- [x] `openspec/changes/recoverable-async-display-dma/{proposal,specs,design,tasks}.md`
- [x] `docs/Templates/PREFLIGHT.md`
- [x] `docs/Templates/DECISION_REVIEW.md`

## Ownership, persistence, and migration

| Item | Result |
|---|---|
| Ownership transfer | No |
| Persistence / SD format | None |
| Undo / Capture / passes semantics | None |
| Migration | Firmware-only; disable DMA selection to roll back |
| Architecture review required | No |

## Affected files

- `lib/SSD1322_OLED/lib/TsyDMASPI/TsyDMASPI/MasterBase.h`
- `lib/SSD1322_OLED/lib/TsyDMASPI/TsyDMASPI/MasterT40.h`
- `lib/SSD1322_OLED/include/SSD1322_API.h`
- `lib/SSD1322_OLED/src/SSD1322_API.cpp`
- `lib/SSD1322_OLED/src/SSD1322.cpp`
- `src/DisplayManager.cpp` and `include/DisplayManager.h`
- focused transport-state test/helper files

## Context summary

- The display payload is 8,192 bytes at a fixed 20 MHz SPI driver setting; an
  idle capture reports roughly 4.5 ms blocking transfer time.
- `DisplayManager` already splits frame work into resolve, draw, and transfer.
- `SSD1322_API::display()` currently bypasses DMA and calls blocking SPI.
- The available DMA path blocks on completion and must be replaced, not enabled.
- The DMA snapshot buffer is already aligned `DMAMEM`; source and snapshot cache
  maintenance are mandatory on IMXRT1062.
- The wrapper must own peripheral abort/reset; the display layer must not reach
  into LPSPI registers.
- Firmware builds and native tests are required; device upload and HITL need
  explicit user confirmation.

## Blocking-SPI baseline

[`host_midi_automation_serial_20260912_234010.log`](../../../captures/host_midi_automation_serial_20260912_234010.log)
is the pre-DMA comparison capture. It contains 91 `DFRAME` rows during the
HITL control/MIDI activity: transfer time is 4,495–4,670 µs (median 4,496 µs)
at the 20 MHz display setting. The scenario did not complete its record/overdub
assertions, so it is a transport-cost baseline only; a successful same-flow
capture remains required for end-to-end MIDI validation.
