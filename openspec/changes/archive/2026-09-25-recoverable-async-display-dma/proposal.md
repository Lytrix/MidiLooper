## Why

The SSD1322 display currently sends its 8,192-byte framebuffer through a CPU-driven, blocking SPI loop at the driver's fixed 20 MHz bus setting. The bundled DMA path is presently bypassed by `SSD1322_API::display()`, and its available DMA send routine also waits for completion, so it cannot reduce display-induced service gaps in MIDI and transport work.

This change defines a recoverable asynchronous display transfer path: it must improve main-loop availability without allowing a failed DMA transaction to corrupt a later SPI frame or permanently disable DMA.  It is a display-polish item within the current persistence/overlay/display scope described by [CURRENT_WORK.md](../../../docs/Runtime/CURRENT_WORK.md) and the SSD1322 delivery surface recorded in [DELIVERABLE_TRACKING.md](../../../docs/DELIVERABLE_TRACKING.md).

## What Changes

- Add an asynchronous SSD1322 framebuffer-transfer capability for Teensy 4.1 that uses SPI DMA when the transport is healthy.
- Add a public, reusable DMA-SPI abort/reset operation to the bundled `TsyDMASPI` wrapper, so its owner can safely recover a timed-out transaction without manipulating LPSPI registers directly.
- Make `DisplayManager` coalesce redraw requests while an OLED transfer is in progress, preserving the newest completed framebuffer for the next transfer.
- Add a bounded degraded-mode path: reset a failed DMA transaction, send a blocking SPI resynchronization frame, and retry DMA later; stop retrying for the boot after repeated recovery failures.
- Add observable transfer/recovery diagnostics and hardware validation scenarios that can disprove correctness or responsiveness claims.

## Capabilities

### New Capabilities

- `recoverable-display-dma-transport`: SSD1322 framebuffer transfer scheduling, DMA recovery, fallback, retry, and observability on Teensy 4.1.

### Modified Capabilities

- None.

## Impact

- Affected firmware: `DisplayManager`, the local `SSD1322_OLED` API/wrapper, and bundled `TsyDMASPI` Teensy 4.x code.
- The display’s public drawing API remains unchanged; display transfer becomes an internal scheduled operation.
- The transfer buffer remains DMA-capable, aligned memory and requires explicit cache maintenance on IMXRT1062.
- No changes to loop, pass, Capture, persistence, MIDI protocol, or UI content semantics are proposed.

## Open Product Decisions

- The exact retry cooldown and permanent-degraded-mode threshold are TBD. The design will require them to be bounded and logged; initial values must be justified by hardware evidence rather than assumed.
- The acceptance performance threshold is TBD pending a complete MIDI scenario capture. The comparison will use the same firmware scenario and the driver's 20 MHz display bus.

## Non-goals

- Raising the configured SPI clock, changing OLED visuals, partial-dirty-region rendering, or double-buffering the renderer.
- Changing MIDI/transport scheduling policy or allocating dynamic memory on hot paths.
- Uploading firmware or treating a successful compile as hardware proof.
