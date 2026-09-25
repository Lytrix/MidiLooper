## Context

See [proposal.md](proposal.md) for motivation and
[recoverable-display-dma-transport/spec.md](specs/recoverable-display-dma-transport/spec.md)
for the behavior contract. The current transfer phase calls `SSD1322_API::display()`
from `DisplayManager::runPaintTransferPhase()`. `display()` deliberately uses the
blocking byte-at-a-time SPI implementation. The dormant DMA path copies the
framebuffer into an aligned `DMAMEM` buffer but waits for `TsyDMASPI0.remained()`
to reach zero, while the wrapper has no public abort operation.

The OLED uses a full 256 × 64, 4-bit framebuffer: 8,192 bytes. The transfer
driver uses a fixed 20 MHz SPI setting (the `SSD1322_CONFIG::SPI_CLOCK` 8 MHz
default is not currently consumed), giving a physical lower bound of about
3.3 ms before command overhead. A post-upload idle capture reports roughly
4.5 ms blocking transfer time; the dense-MIDI baseline remains required.
`DisplayManager` is the existing display owner
under [ARCHITECTURE_RULES.md](../../../docs/Authority/ARCHITECTURE_RULES.md);
this extends its transfer lifecycle and does not change display-content or
transport ownership.

## Goals / Non-Goals

**Goals:**

- Return the display transfer phase promptly once a healthy DMA transfer is queued.
- Preserve frame integrity by transmitting a snapshot buffer rather than the
  renderer's mutable framebuffer.
- Make recovery safe, bounded, diagnosable, and capable of returning to DMA.
- Keep transfer-state storage fixed-size and allocation-free on the hot path.

**Non-Goals:**

- Add a second rendering buffer, modify the paint resolve/draw content pipeline,
  change SPI frequency, or promise a frame-rate target before hardware evidence.
- Run another device scheduler, introduce interval reservation, or alter the
  `main.cpp` update ordering.

## Decisions

### DisplayManager owns scheduling; SSD1322 owns bus transactions

`DisplayManager` will own whether a new render may enter the transfer phase and
whether a completed draw is pending. The SSD1322 API will own start, poll,
completion, fallback, and diagnostic status of a physical framebuffer transfer.
This keeps bus details below the renderer and uses the existing display owner.

Alternative: let `DisplayManager` call `TsyDMASPI0` directly. Rejected because
it duplicates SSD1322 command, CS/DC, cache, and recovery state above the driver.

### Submit one DMA snapshot and poll it on later updates

The SSD1322 API will copy a completed renderer framebuffer into its existing
aligned DMA buffer, flush the relevant cache range, program the SSD1322 RAM
write command/window, lower CS, and queue one DMA transaction. It will then
return. A non-blocking poll will detect completion on subsequent display updates
and raise CS only after the transfer is fully complete.

`DisplayManager` will not redraw into the active DMA buffer. It will retain a
single pending-transfer indication and run a new resolve/draw/transfer cycle
only after polling releases the prior transfer. Multiple invalidations coalesce
through the existing display-paint machinery; no transaction queue is used for
frames.

Alternative: DMA directly from the renderer framebuffer. Rejected because the
next draw can overwrite bytes still being shifted to the panel. Alternative:
queue every completed frame. Rejected because it increases display latency and
requires more buffers.

### Add a wrapper-owned abortAndReset operation

`TsyDMASPI::MasterBase` will expose a public `abortAndReset()` operation. The
Teensy 4.x implementation will atomically disable TX/RX DMA channels, clear DMA
interrupt state, discard queued transaction records, disable LPSPI DMA requests,
flush/reset the peripheral FIFOs and status, and end an active SPI transaction.
Its normal completion path remains unchanged. The operation will respect
`kCallerOwnsCsPin`: SSD1322 retains responsibility for raising CS after the
wrapper has made the bus idle.

The SSD1322 API will initialize the wrapper with caller-owned CS and the existing
SPI settings. On a rejected idle submission it can immediately use blocking SPI.
On timeout it will call `abortAndReset()`, raise CS, issue a blocking full-frame
resynchronization, and mark DMA eligible for a later retry.

Alternative: call `end()` then `begin()` after timeout. Rejected because the
wrapper's current lifetime management deletes DMA channels and does not define
reinitialization while an ISR/transaction is active. Alternative: reset LPSPI
registers in `SSD1322_API`. Rejected because it leaks wrapper implementation
details and cannot serve other wrapper users safely.

### Use a finite health policy with test fault injection

The transport will track consecutive recovery failures in fixed state. A
successful DMA completion resets the count. After a bounded limit, it remains in
blocking SPI for the current boot and emits one degraded-mode diagnostic. A
compile-time or test-only fault-injection seam will force queue rejection and
completion timeout without requiring a physical bus fault, so recovery behavior
can be exercised deterministically.

The initial timeout must be derived from frame transfer time plus measured
margin at the configured clock. The implementation task will establish the
baseline first and record chosen timeout, retry delay, and failure limit with
evidence.

Alternative: retry every frame forever. Rejected because repeated timeouts would
reintroduce unpredictable service gaps.

## Risks / Trade-offs

- [DMA cache or buffer-alignment mistake produces stale or partial rows] → use
  aligned `DMAMEM`, explicit cache flushes, and full-frame visual stress tests.
- [DMA interrupt races recovery] → make abort state changes atomic, disable
  channels before discarding queue records, and test timeout injection.
- [CS/DC changes during active DMA corrupt GRAM] → SSD1322 owns CS/DC and keeps
  CS asserted only from accepted submission to detected completion or reset.
- [Full-frame DMA snapshot copy is itself costly] → measure copy-plus-submit
  separately from wire time; the design claims CPU availability during wire time,
  not zero submission cost.
- [Changed paint cadence leaves a newer completed render unsent] → verify the
  existing phase/invalidation state advances after transfer completion and add a
  targeted host test for coalescing policy where feasible.

## Migration Plan

1. Add wrapper recovery support with a focused, host-testable state/contract
   seam and verify normal completion remains intact.
2. Initialize and expose non-blocking SSD1322 DMA start/poll/recovery APIs while
   retaining blocking SPI as the fallback transport.
3. Integrate transfer busy/pending handling into `DisplayManager` without
   changing rendering semantics.
4. Compile the firmware, run applicable native tests, and capture baseline plus
   DMA/recovery HITL evidence before enabling DMA by default.
5. Roll back by disabling DMA selection at the SSD1322 API boundary; blocking
   SPI remains the retained compatibility path.

## Open Questions

- The exact timeout, retry delay, and failure limit will be selected after the
  baseline capture; this does not alter the required bounded policy.
- A logic-only host test can cover coalescing and health transitions, but the
  final wrapper/peripheral recovery proof requires Teensy hardware.
