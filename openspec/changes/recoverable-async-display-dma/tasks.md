## 1. Baseline and implementation preflight

- [x] 1.1 Complete `docs/Templates/DECISION_REVIEW.md` and the project preflight for this change, including searches for display, DMA, SPI, LPSPI, and existing timeout/retry decisions; verify the result identifies `DisplayManager` and SSD1322 as the extended owners.
- [x] 1.2 Capture a repeatable blocking-SPI baseline at the driver's 20 MHz display clock during a rapid-redraw, dense-MIDI scenario; record frame payload/submission timing and MIDI-service gap evidence in the change notes. The 2026-09-12 pre-DMA capture has 91 transfer samples: 4,495–4,670 µs (median 4,496 µs); it is transport-cost evidence only because the record/overdub assertions remained inconclusive.
- [ ] 1.3 Derive and record the initial DMA completion bound, retry delay, and consecutive-failure limit from the baseline and a targeted fault run; verify each is finite and covers one full-frame transfer with measured margin.

## 2. Recoverable DMA-SPI wrapper

- [ ] 2.1 Add a public `abortAndReset()` operation to `lib/SSD1322_OLED/lib/TsyDMASPI/TsyDMASPI/MasterBase.h` and the Teensy 4.x master implementation; verify it disables both DMA paths, clears transaction state and LPSPI DMA/FIFO state, and leaves the wrapper idle without reinitializing channel lifetime.
- [ ] 2.2 Preserve normal wrapper completion behavior and caller-owned-CS semantics after adding recovery; verify by compiling the Teensy environment and exercising both normal completion and an injected abort path.
- [ ] 2.3 Add a small deterministic test seam or host-testable state helper for rejected submission, timeout recovery, successful retry, and finite degraded mode; verify the relevant native test covers all four transitions.

## 3. SSD1322 asynchronous transport and recovery

- [ ] 3.1 Initialize the bundled DMA-SPI wrapper for the SSD1322 with the existing SPI settings and caller-owned CS; add fixed, aligned DMA snapshot-buffer validation and cache maintenance; verify the firmware build succeeds and a submitted frame never DMA-reads the mutable renderer framebuffer.
- [ ] 3.2 Replace the blocking DMA wait in `SSD1322_API` with non-blocking start and poll operations that hold CS for exactly one active frame; verify a healthy start returns before the 8,192-byte wire transfer completes and completion raises CS once.
- [ ] 3.3 Implement the rejected-submission and timeout recovery paths: recover the wrapper before blocking SPI on timeout, send a full resynchronization frame, retry after the configured delay, and enter boot-local SPI degraded mode after the limit; verify diagnostics distinguish each state and a transient injected failure returns to DMA.

## 4. Display transfer integration

- [ ] 4.1 Update `DisplayManager` transfer-phase state so a busy OLED transfer is polled on later updates and completed redraws coalesce to one newest pending frame; verify a focused native test or deterministic harness proves no concurrent/stale transfer is started.
- [ ] 4.2 Preserve existing rendering, boot-screen, and load/save overlay behavior while the transport is busy; verify a firmware build and manual on-device smoke flow show a complete first frame, normal overlay refresh, and no partial-row artifact.

## 5. Verification and closeout

- [ ] 5.1 Run `pio test -e native` and `pio run -e teensy41-capture-serial`; verify results are recorded, with failures triaged before proceeding.
- [ ] 5.2 With explicit user confirmation before upload, run the rapid-redraw/dense-MIDI HITL baseline and DMA comparison, then force queue rejection and timeout recovery; verify complete frames, safe SPI fallback, DMA retry, bounded degraded mode, and captured MIDI-service-gap evidence.
- [ ] 5.3 Compare the same-scenario baseline and DMA captures, document whether the reduced blocking claim is supported or falsified, and update the change tasks plus runtime/delivery documentation before requesting archive.
