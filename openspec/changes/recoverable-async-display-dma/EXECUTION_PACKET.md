# Execution packet — recoverable-async-display-dma

Authority: [IMPLEMENTATION_READINESS.md#g2-execution-packet](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice).
One section per implementation slice. Template: [EXECUTION_PACKET.md](../../../docs/Templates/EXECUTION_PACKET.md).

## Slice A — activate the recoverable transport

### Identity and scope

- **Packet/task IDs:** a one-packet slice spanning tasks **1.3 — transport bounds derivation** (record only),
  **3.1 — wrapper initialization and snapshot-buffer safety**, and **5.1 — native/build gate**. Contract:
  [spec.md](specs/recoverable-display-dma-transport/spec.md); design decisions "Submit one DMA snapshot and
  poll it on later updates", "Add a wrapper-owned abortAndReset operation", "Use a finite health policy".
- **Status:** activation implemented; native + build evidence recorded below; **device verification NOT RUN**.
- **Owner path:** `DisplayManager::runPaintTransferPhase` → `SSD1322_API::display()` →
  `SSD1322_API_send_buffer_DMA` → `TsyDMASPI0.queue`. Completion: `DisplayManager::update()` →
  `DisplayManager::_display.api.pollDisplayTransfer()`.
- **Primary files:** `lib/SSD1322_OLED/src/SSD1322.cpp` (activation switch, rollback macro),
  `lib/SSD1322_OLED/src/SSD1322_API.cpp` (submit, poll, recovery, blocking full-frame helper),
  `lib/SSD1322_OLED/include/SSD1322_API.h` (bounds, helper declaration).
- **Preconditions already present:** aligned `DMAMEM` snapshot buffer (`SSD1322.cpp`, `aligned(32)`);
  cache flush on the source before the copy and on the snapshot before the transfer; caller-owned CS
  (`kCallerOwnsCsPin = 255`, `MasterBase.h`); `abortAndReset()` (`MasterBase.h:67`) with
  `clearTransaction()` clearing LPSPI `FCR`/`DER`/`CR`/`SR` (`MasterT40.h:102`).
- **Mutation / postconditions:** the SSD1322 transport is initialized for DMA at boot with the driver's
  20 MHz `SPISettings` and caller-owned CS; `display()` returns after a queued transfer; CS is raised only
  on completion or recovery. **Non-goals:** no SPI-clock change, no second renderer framebuffer, no
  display-phase-machine redesign, no new scheduler, no change to render content.
- **Commit boundary:** independently buildable in both directions; rollback is
  `-DMIDILOOPER_DISPLAY_DMA=0` (blocking SPI), no other source edit.

### Oracle fixed before behavior edits

| Case | Initial state | Action | Expected observation |
|---|---|---|---|
| A1 — healthy submit | transport idle, `dmaInitialized_` true | `display()` | exactly one `queue(dmaBuffer, 8192)`; returns before the 8,192-byte wire transfer completes; `dmaActive_` true; CS still low |
| A2 — completion | `dmaActive_`, `remained() == 0` | `pollDisplayTransfer()` | CS raised once; `dmaActive_` false; `dmaRecoveryFailures_ = 0` |
| A3 — timeout recovery | `dmaActive_`, elapsed ≥ `kDmaTimeoutMs` | `pollDisplayTransfer()` | `abortAndReset()`; CS raised; `dmaActive_` false; failures incremented; retry deadline `now + kDmaRetryDelayMs`; **one full-frame blocking write with the GRAM window re-armed** |
| A4 — bounded degraded mode | failures reach `kDmaRecoveryFailureLimit` | next `display()` | blocking SPI path only; transport stays degraded for the boot |
| A5 — busy transport | `dmaActive_` | a second `display()` | no second `queue`; no CS change; newest framebuffer retained for the next transfer |
| A6 — rollback build | compiled `-DMIDILOOPER_DISPLAY_DMA=0` | boot | `beginDMA` is not called; `display()` is blocking-only |

**Counterexample that must fail (the defect this slice fixes, RC-1):** a recovery "resynchronisation"
frame written without re-arming the GRAM window. After a partially-shifted aborted transfer the SSD1322
RAM address counter has advanced, so the pre-fix `SSD1322_API_send_buffer(framebuffer, FRAMEBUFFER_SIZE)`
did not start at row 0. Fixed by routing every blocking full-frame write through
`SSD1322_API::sendFullFrameBlocking()`.

**Evidence limits on A3/A4:** `TsyDMASPI0.queue()` rejects only an invalid size, which a full frame never
is, so the *rejected-submission* transition is not reachable from the production path without the
fault-injection seam that task 2.3 still owes. A3/A4 are therefore specified, not host-verified.

### Controls and coverage

- **Seams:** none exist for this transport. `MemoryMonitor` has a deny-injection seam; the DMA transport
  does not. That is task 2.3's deliverable and is **not** part of this slice.
- **Production paths exercised:** `DisplayManager::update()` → `pollDisplayTransfer()` (device); the boot
  path `SSD1322::begin()` → `beginDMA()` (device).
- **Untestable before link:** `TsyDMASPI0`, `driver_instance` and the whole submit/poll path are
  `__IMXRT1062__`-guarded and hardware-bound, so a `native` test cannot reach them today. This is why
  A1–A5 have no host assertion in this slice; only A6's compile-out is host-checkable.
- **Numeric limits source:** the 2026-09-12 blocking-SPI baseline in [PREFLIGHT.md](PREFLIGHT.md)
  (91 `DFRAME` rows, 4,495–4,670 µs; 3.28 ms of wire time for 8,192 bytes at 20 MHz) against
  `kDmaTimeoutMs = 20`, `kDmaRetryDelayMs = 100`, `kDmaRecoveryFailureLimit = 3`.

### Verification recipe

- Focused: `pio run -e teensy41-capture-serial` — expect SUCCESS and recorded RAM1 figures.
- Rollback: `PLATFORMIO_BUILD_FLAGS=-DMIDILOOPER_DISPLAY_DMA=0 pio run -e teensy41-capture-serial` —
  expect SUCCESS with `beginDMA` compiled out.
- Broad: `pio test -e native` — expect the recorded suite count, no new failures.
- Device-only claim (NOT RUN; requires explicit user confirmation before upload): same-scenario
  rapid-redraw / dense-MIDI baseline vs DMA, `DFRAME` transfer-µs comparison and MIDI-service-gap
  evidence, plus forced queue rejection and timeout recovery (tasks 5.2/5.3).

### Logging surface (added 2026-09-24)

The question "is DMA running, or did it fall back to SPI?" has one answer per layer.

**Transition lines (library, always on):** `SSD1322_API: DMA ready at <hz> Hz` at boot,
`DMA rejected; SPI fallback`, `DMA timeout; resetting transport`,
`DMA recovered; transfers resumed`, `DMA degraded for this boot`. Per-frame
`DMA queued` / `DMA complete` are now gated behind `MIDILOOPER_DISPLAY_DMA_TRACE`
(default 0): they were two serial lines per frame on the paint path (~66 lines/s at a
30 ms interval), which is both an instrumentation-gate violation and a paint-path cost.

**Census (host, `SESSION_CAPTURE` only):** `DisplayManager::reportDisplayDmaTransport()`
emits `#DMA_XFER,<ms>,init=,degraded=,active=,dma_frames=,blocking_frames=,timeouts=,`
`rejections=,recoveries=` at most once a second and only when a counter moved. A rising
`blocking_frames` with a flat `dma_frames` is the temporary-SPI-fallback signature.

**Per-frame indirect:** `#CAP …,DFRAME,…,<transferUs>` under `SESSION_CAPTURE`, every
30th frame. Blocking SPI reads ~4,495 µs; a DMA submission reads tens of µs.

Unrelated: `#DMA_STAGING_BASE` / `#DMA_STAGING_CORRUPT` are the RAM2 overrun canary, not
this transport.

### Completion record

Each entry is the actual command, context and result. NOT RUN is stated explicitly.

- **Activation landed:** `api.beginDMA(config.SPI_CLOCK)` enabled in `SSD1322.cpp`; blocking SPI retained
  as fallback; `-DMIDILOOPER_DISPLAY_DMA=0` compiles the pre-DMA baseline.
- **RC-1 fixed:** `sendFullFrameBlocking()` arms the window; both blocking full-frame callers use it.
- **Build (DMA on):** `pio run -e teensy41-capture-serial` — **SUCCESS** (15.99 s). FLASH code 1,023,640 /
  data 93,076; RAM1 variables 96,160 / code 391,448 / padding 1,768 / **34,912 free for local variables**
  (approved floor `padding >= 840`); RAM2 155,200. Two warnings, both pre-existing and unrelated
  (`DisplayNoteResolveCommitted.cpp:278` unused `holdUntilCurrentSnapshot`;
  `TrackDeferredMaintenance.cpp:1249` unused `avoidFullVisual`).
- **Rollback build (A6):** `PLATFORMIO_BUILD_FLAGS=-DMIDILOOPER_DISPLAY_DMA=0 pio run -e
  teensy41-capture-serial` — **SUCCESS** (77.62 s, full rebuild because the flag changed). FLASH code
  1,023,624; RAM1 variables 96,160 / code 391,432 / padding 1,784 / 34,912 free. Activation therefore
  costs **16 bytes of ITCM code and no RAM**, and the blocking baseline builds from the same tree.
- **Snapshot-source check (3.1):** the only `TsyDMASPI0.queue` call site is `SSD1322_API_send_buffer_DMA`
  (`SSD1322_API.cpp:351`), sourced from `dmaBuffer`; the renderer framebuffer is read only by the
  `memcpy` into that snapshot (`:344`). A submitted frame cannot DMA-read the mutable framebuffer.
- **Native:** `pio test -e native` — **2403 cases, 1 skipped, 2402 succeeded, 0 failures** (exit 0).
  No transport test exists in the suite, so this is a no-regression gate, not transport coverage.
- **Device verification (2026-09-24, `teensy41-capture-serial`, `-t upload`, hex sha256
  `84b0cb0a7561…`):** the board rebooted into the new image (boot banner present). `#DMA_XFER` census
  from ~13.5 s to ~47 s uptime: `init=1, degraded=0, dma_frames 36 → 655, blocking_frames=0,
  timeouts=0, rejections=0, recoveries=0` — the transport ran **entirely on DMA with no fallback frame**
  in the window. `#CAP …,DFRAME,…` transfer time reads **44–53 µs** against the 4,495–4,670 µs blocking
  baseline (~90x), so a healthy start demonstrably returns long before the 3.28 ms wire time.
- **Not verified on device:** the panel's visual completeness / no-partial-row artifact (task 4.2 needs
  a human look at the OLED), and the timeout / rejected-submission recovery transitions (task 2.3's fault
  seam does not exist, so A3/A4 remain specified-only).
- **Deviation from the design's Migration Plan step 4:** activation was enabled by direct user
  instruction *before* the same-scenario baseline/DMA HITL comparison. The `MIDILOOPER_DISPLAY_DMA` flag
  keeps that comparison available from this tree; tasks 5.2/5.3 remain the acceptance gate.
- **Task IDs eligible for checkoff:** **3.1**, **3.2** (non-blocking start measured; CS-once is the single
  `SSD1322_HW_drive_CS_high()` in the completion branch) and **5.1**. **1.3** stays open for the fault
  run; **2.1–2.3, 3.3, 4.1, 4.2, 5.2, 5.3** stay open — their verification needs the fault-injection
  seam and/or hardware.

## Slice B — deterministic transport-health seam (task 2.3)

### Identity and scope

- **Task:** **2.3 — host-testable health seam**, which unblocks **1.3** (targeted fault run), **2.2**
  (injected abort path), **3.3** (recovery transitions) and **5.2** (forced device fault run).
- **Status:** seam implemented and native-verified; device fault runs NOT RUN.
- **Owner path:** `SSD1322_API` decides with `SSD1322_DMA_HEALTH`; the host test drives the same object
  the firmware uses, so no decision is duplicated in test code.
- **Primary files:** new `lib/SSD1322_OLED/include/SSD1322_DMA_HEALTH.h` (dependency-free policy),
  `lib/SSD1322_OLED/include/SSD1322_API.h` + `src/SSD1322_API.cpp` (delegate to it),
  `platformio.ini` (native include path + `lib_ignore`), new
  `test/test_display_dma_health/`.
- **Mutation / postconditions:** `SSD1322_API` keeps only hardware state (`dmaBuffer`, `dmaActive_`,
  `dmaStartedAtMs_`); the retry cooldown, degraded limit and census live in the policy object.
  Behaviour is unchanged — same transitions, same bounds, same log lines.
- **Non-goals:** no bus, ISR or DMA-channel change; no new scheduling; no change to the bounds.

### Oracle fixed before the behavior edits

| Case | Initial state | Action | Expected |
|---|---|---|---|
| B1 uninitialized | `markInitialized(false)` | `canSubmitDma` | false at any time |
| B2 rejected submission | initialized, no failures | `onSubmissionRejected(t)` | `rejections=1`, `failureRun=1`, not degraded, `canSubmitDma(t+delay-1)` false, `canSubmitDma(t+delay)` true |
| B3 timeout recovery | active transfer past the bound | `hasTimedOut` then `onTimeout(t)` | `timeouts=1`, cooldown armed, not degraded |
| B4 successful retry | `failureRun>0` | `onTransferComplete()` | `failureRun=0`, `recoveries=1`, DMA re-admitted |
| B5 finite degraded mode | `failureLimit` consecutive failures | third failure | degraded, `canSubmitDma` false for the rest of the boot |
| B6 partial-run reset | two failures then a success | then two more failures | still not degraded (not three *consecutive*) |
| B7 census separation | — | completions + blocking frames | counted independently |

**Counterexample:** the pre-slice tree had no seam at all, so B2–B5 were unreachable off-device; the
"rejected submission" branch is additionally unreachable through the real wrapper, because
`TsyDMASPI0.queue()` rejects only an invalid size, which a full frame never is.

### Controls and coverage

- **Seam:** `SSD1322_DMA_HEALTH` — a dependency-free policy object. The production `SSD1322_API` calls
  it for every decision, so the fixture exercises the asserted path rather than a copy.
- **Production paths exercised:** the same object instance type the firmware instantiates; the firmware
  build is unchanged in RAM1.
- **Device fault injection (for 5.2, compiled out at 0):** `MIDILOOPER_DISPLAY_DMA_FAULT=1` forces every
  submission to be rejected; `=2` forces an active transfer to never report completion. Both builds
  verified to compile; the device runs themselves are 5.2.
- **Untestable before link:** the wrapper's `abortAndReset()` and the LPSPI register effects still need
  hardware; the seam covers the decision, not the bus.

### Verification recipe

- Focused: `pio test -e native -f test_display_dma_health`.
- Broad: `pio test -e native`.
- Builds: `pio run -e teensy41-capture-serial`, plus `MIDILOOPER_DISPLAY_DMA_FAULT=1` and `=2`.

### Completion record

- **Seam landed:** `SSD1322_DMA_HEALTH` (policy) with `SSD1322_API` delegating; no decision duplicated in
  the test.
- **Focused native:** `pio test -e native -f test_display_dma_health` — **9 cases, 9 succeeded**.
- **Broad native:** `pio test -e native` — **2412 cases, 1 skipped, 2411 succeeded, 0 failures** (exit 0);
  the +9 over the previous 2403 is exactly the new `test_display_dma_health` suite.
- **Builds:** `teensy41-capture-serial` SUCCESS with `RAM1 variables 96,160 / code 391,448 / padding 1,768 /
  locals 34,912` — **byte-identical to the pre-refactor figures**, i.e. the extraction cost no ITCM or
  RAM1. `MIDILOOPER_DISPLAY_DMA_FAULT=1` and `=2` both SUCCESS.
- **Device fault runs:** NOT RUN (task 5.2).
- **Task IDs eligible for checkoff:** **2.3**; **2.1**/**2.2** still need their own verification, and
  **1.3**/**3.3** now have a seam to run against.
