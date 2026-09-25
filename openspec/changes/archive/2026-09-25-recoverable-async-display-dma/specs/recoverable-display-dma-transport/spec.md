## Purpose

Defines reliable, observable asynchronous framebuffer transport to the SSD1322
so display refresh work does not unnecessarily block MIDI and transport service.

## ADDED Requirements

### Requirement: Framebuffer transfer is asynchronous when DMA is available

On Teensy 4.1, when the display DMA transport is initialized and healthy, the
firmware SHALL begin a framebuffer transfer without waiting for all framebuffer
bytes to leave the SPI bus. The renderer SHALL retain the transferred frame
unchanged until that transfer completes.

#### Scenario: Accepted DMA frame leaves the renderer available

- **WHEN** a completed framebuffer is submitted while the DMA display transport is idle
- **THEN** the submission returns with the transfer reported in progress before its SPI payload completes
- **AND** the main firmware loop can continue normal MIDI and transport servicing

#### Scenario: Rendering changes during an active transfer

- **WHEN** the renderer completes a newer frame while a prior DMA transfer is in progress
- **THEN** bytes for the active transfer remain those of its submitted frame
- **AND** the newest completed frame is retained for a later transfer

### Requirement: Display refresh coalesces while transport is busy

The firmware SHALL permit no more than one active SSD1322 framebuffer transfer.
While that transfer is active, it SHALL coalesce additional redraw work so that
only the newest completed frame is transferred next.

#### Scenario: Multiple redraws arrive during one transfer

- **WHEN** two or more redraw requests complete before the active transfer completes
- **THEN** the display transport does not start a concurrent or queued stale framebuffer transfer
- **AND** the next accepted transfer contains the most recently completed frame

### Requirement: DMA failure recovers to a safe blocking transfer

If DMA submission fails or an active DMA transfer exceeds its configured
completion bound, the firmware SHALL first return the SPI/DMA transport to an
idle, non-DMA state before it sends a blocking SPI resynchronization frame. It
SHALL NOT begin blocking SPI traffic while a timed-out DMA transaction can still
drive the bus.

#### Scenario: Active DMA transfer times out

- **WHEN** a submitted DMA framebuffer does not complete within its configured bound
- **THEN** the firmware resets the DMA and SPI transaction state and releases the display bus
- **AND** it sends one complete blocking SPI resynchronization frame after recovery
- **AND** a later display refresh remains possible

#### Scenario: DMA submission is rejected while idle

- **WHEN** the DMA transport rejects a framebuffer before it becomes active
- **THEN** the firmware sends the frame through the blocking SPI path
- **AND** the display remains eligible for later DMA retry

### Requirement: DMA retry is bounded and observable

After a safe fallback, the firmware SHALL retry DMA on a later display refresh.
It SHALL use a finite retry policy and enter a boot-local blocking-SPI degraded
mode after the configured consecutive-recovery limit is reached. It SHALL emit
diagnostics sufficient to distinguish DMA completion, rejected submission,
timeout recovery, retry, and degraded mode.

#### Scenario: DMA recovers after a transient fault

- **WHEN** a DMA failure is followed by a successful reset and a later healthy DMA submission
- **THEN** the later frame completes through DMA
- **AND** diagnostics record both the recovery and successful retry

#### Scenario: Repeated DMA recovery fails

- **WHEN** consecutive DMA recovery failures reach the configured finite limit
- **THEN** subsequent display frames use blocking SPI for the rest of that boot
- **AND** diagnostics identify the degraded-mode transition and failure count

### Requirement: Transport validation is evidence-based

The change SHALL record a same-scenario hardware baseline and DMA measurement
at the driver's display SPI clock. Validation SHALL confirm complete visual
frames under rapid redraw and dense MIDI traffic, and SHALL exercise the timeout
recovery path without an upload being implied by the build.

#### Scenario: Hardware validation falsifies a transport claim

- **WHEN** the validation run observes a partial frame, stale active-transfer bytes, an unsafe SPI takeover, or a DMA retry that cannot complete
- **THEN** the DMA transport claim is considered unproven
- **AND** the failing capture identifies the applicable transfer or recovery state
