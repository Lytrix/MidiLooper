# I5 — physical-reference producer contract draft

Status: **elapsed producer accepted; stopped-only hardware functional qualification PASS, complete physical-deadline contract still unqualified**. Owner-approved PITresource/sharedpolicy under DEC-128 — ClockManager PIT elapsed reference; approved C_DEBUGEN guard correction under DEC-129 — Run-mode physical-clock debug admission. [I5 — producer evidence](PHYSICAL-PRODUCER-EXECUTION.md), [I5 — hardware qualification](PHYSICAL-HARDWARE-QUALIFICATION.md). Earlier draft sections below are historical where superseded. No numerical wholecost budget, checkpoint activation or parent checkoff.

Build on [I5 — passive checkpoint evidence](CHECKPOINT-EXECUTION.md), [I5 — continuation mechanics](CONTINUATION-EXECUTION.md) and [I5 — timing draft](TIMING-CONTRACT.md). Governing ownership/bounds: [Architecture rules](../../../../../docs/Authority/ARCHITECTURE_RULES.md), [Execution bounds](../../../../../docs/Authority/Architecture/ExecutionBounds.md), [Implementation readiness](../../../../../docs/Authority/IMPLEMENTATION_READINESS.md). DEC-123 — checkpoint arbitration and deadline admission remains accepted; this draft does not supersede it or claim its missing producer/cost premises.

## Current decision boundary — 2026-10-14 continuation

DEC-125 — pinned receiver ingress receipts now implements the supported project-owned
generated existing-queue sidecars; the earlier ingress-permission statements below are
draft history, not current blockers. DEC-127 — semantic live-source retention locally
verifies retained/serialized Loop/Track integration, not all-route synchronization.
See [I5 — integration and decision evidence](LIVE-SOURCE-INTEGRATION.md).

User approved and implementation delivered ClockManager-owned PIT0/1 cold reservation,
stock-compatible FRZ1/Run-mode-only policy and bounded masked lifetime reads under
DEC-128 — ClockManager PIT elapsed reference. Initial setup requires allfour channels
disabled; owns pair before MIDI IntervalTimer begin, consumes startup bool, never steals
or disables a foreign timer. State16B RAM2 (+32B linked alignment); no numeric RAM1
budget change. Native/fullnative/default+ingress builds and one focused review verified;
counter image9b50680a…b770c uploaded and stopped-only210s hardware qualified; no absolute calibration/WCET/deadline claim. See [I5 — hardware evidence](PHYSICAL-HARDWARE-QUALIFICATION.md).

NXP RMRev3 chapter53 verifies pair0/1/start/latch/stickyflag semantics at documentation
level; hardware now observes startup/natural lowcarry/highTIFclear/MIDIch2 and stopped
point brackets, relative gross rate and read distribution, not calibration/WCET/errata.
Source rejects current halt/sleep/configuration/overflow/backwards/era faults, not C_DEBUGEN alone; unseen debug/sleep/stop-and-restore remain unsupported.
Explicit invalidation precedes future supported suspend/reconfiguration; intact owned
requalification preserves running count. One MIDI timer/conditional spare is source/build
verified, not permission for later unmediated stock allocation. No checkpoint dispatch,
moving-clock correlation/sender-delay/external-pacing/wholeowner-input bounds or sourceISR/mainprotocol qualification. Progress29/60/defaultrollingOFF/targetcontinuationdisabled/no content or SDcleanup/destructive gestures/Git. Full managed run complete2drains/nolatch; later cachedf3d02128…d9581 NOTuploaded.

## 1. Verified platform evidence

Inspected installed `/Users/eelkejager/.platformio/packages/framework-arduinoteensy`, package version **1.159.0**. File hashes below pin local evidence; this is not an immutable upstream revision claim.

| Source/function | VERIFIED observation | Consequence |
|---|---|---|
| `cores/teensy4/delay.c::micros` | Returns uint32; synchronizes SysTick millisecond/cycle state through exclusive accesses, computes DWT fractional time, clamps fraction to1000us. | Not an independent extended clock; delayed SysTick and full micros32 wrap cannot be solved by casting its result to64bit. |
| `cores/teensy4/EventResponder.cpp::systick_isr` | Records DWT cycle count and increments a32bit millisecond counter. | `millis`/DWT are not a second unlimited hardware elapsed counter. |
| `cores/teensy4/rtc.c::rtc_get/_gettimeofday/rtc_set` | Reads high/low SNVS RTC registers with repeated stability checks; wall time write stops/resets/synchronizes RTC/SRTC. | Wider hardware counter exists, but the existing public readers retry indefinitely and wall-clock reset invalidates monotonic assumptions. |
| `src/RtcTime.cpp::writeHardwareWallClockUnix` and `ensureWallClockValid/raiseWallClockToAtLeast` | Project writes `Teensy3Clock`; getter validity correction or SD timestamp-floor raising can cause writes. | Raw SNVS time is not an isolated monotonic service clock, including under ordinary application clock correction. |
| `cores/teensy4/startup.c` | Sets PIT/GPT peripheral clock to24MHz independent of CPU speed; resets PIT channels at startup. | A dedicated peripheral elapsed counter is a candidate without relying on CPU-speed/DWT conversion. Nominal24MHz is configuration evidence, not oscillator accuracy or measured WCET. |
| `cores/teensy4/imxrt.h` | Exposes PIT64 lifetime high/low registers and channel chaining; GPT counters are32bit. | Hardware-chained PIT is the preferred candidate; register definitions alone do not prove latch/ordering/chaining semantics. |
| `cores/teensy4/IntervalTimer.cpp::beginCycles/pit_isr` | Four PIT channels; takes first disabled channel, writes PIT_MCR=1, uses shared IRQ, dispatches callbacks only for non-null table entries with pending flags. | A chained pair needs explicit boot reservation before the MIDI timer; shared module/debug-freeze configuration and IRQ effects must be included. |
| `cores/teensy4/usb_midi.c::rx_event/usb_midi_read_message` | Six transfer buffers; receive metadata holds count/index/list/availability, read returns a packed MIDI word. No arrival timestamp is retained at that boundary. | Current USB-device dequeue cannot reconstruct transfer age or sender batching age. |
| `libraries/USBHost_t36/midi.cpp::rx_data/read` | Transfers enqueue packed MIDI words; read later invokes message callbacks. No arrival stamp travels with queued words. | Application callback time is dequeue time, not receive-completion time. |
| `cores/teensy4/HardwareSerial.cpp::IRQHandler/read` and `libraries/MIDI/src/midi_Message.h` | UART ISR queues values; parser message contains type/channel/data/validity/length, not ingress time. | DIN byte rate/queue length does not establish the age of an already queued message. |

The preceding repository discovery remains applicable: `ClockManager::onMidiClockPulse` stamps handler-time cadence; telemetry explicitly labels externalarrival as batch read-complete; no production PhysicalClockStamp, CheckpointCostTable or checkpoint execution exists. Existing logical epoch is not sufficient to qualify reference lifetime across Stop or tempo changes. Continue currently calls a timestamp setter which returns its argument without changing the field. These existing behaviors are not repaired by this draft.

## 2. Preferred elapsed-time candidate — provisional

**Proposed owner:** ClockManager, through a thin Teensy platform counter adapter. This measures elapsed time for admission, not a second musical clock, transport owner, publisher, scheduler or content authority. Track/Loop/current-ready ownership and activeLoopIndex remain unchanged.

**Preferred candidate:** reserve a hardware-chained PIT pair at cold setup, before `ClockManager::setup` starts its existing IntervalTimer. Use a free-running wide count and a nonzero, non-recycled producer era. No dependency source edits, SysTick takeover or per-wrap main polling obligation are assumed. The exact pair/lifetime-register semantics and reservation sequence require hardware-reference verification before target code.

**Resource trade-off:** two of four PIT channels would belong to the elapsed-time source; one channel remains for the existing MIDI timer, leaving one spare only after the linked target's other timer users are checked. There is no new reservation today. Refuse source qualification if the pair cannot be owned or expected clock/module configuration is absent. Never steal/reconfigure a live unrelated timer or report source-ready after a failed MIDI timer start.

The installed IntervalTimer changes shared PIT_MCR, including debug-freeze policy. Proposed qualification must explicitly deal with that interaction and debugger/sleep behavior. A counter which can stop without reference invalidation is not independent elapsed time. Changing global peripheral clock configuration to make the candidate pass is not approved.

**Proposed read boundary:** finite coherent hardware reads, with a small declared retry cap (candidate two attempts), no wait loop, allocation, logging or counter writes on the hot read. Unknown/incoherent configuration or an exhausted read attempt produces an unavailable source, never a restamped reference. Numeric read latency and exact register-order primitive remain unqualified. Verify documented hardware latch semantics rather than treating a native register fake as hardware proof.

Counter reset/configuration change invalidates the producer era before reuse. Era exhaustion is terminal zero. Wide-count exhaustion must be rejected, not recycled; whether a sticky high-channel overflow flag can enforce this is part of the unverified register contract. Conversion to microseconds must be overflow-safe and conservative about truncation/quantisation and oscillator uncertainty. No numeric margin is adopted here.

### Alternatives

- Software-extended micros32/millis32: rejected as a readiness substitute without an independently enforced observation/interrupt bound; unobserved complete wraps or missed SysTick service can mint false lead.
- CPU DWT counter alone:32bit and CPU-rate dependent; does not remove rollover/clock-rate obligations.
- Raw RTC/SNVS: not preferred because ordinary wall-clock correction resets the same counter and stock reads retry indefinitely. A future bounded reader plus complete correction invalidation is possible, but not simpler than an isolated elapsed-time resource for this contract.
- GPT32 plus software overflow extension: fewer peripheral channels, but requires an enforced wrap-observation/interrupt bound. It remains an alternative if PIT resource closure fails, not a fallback silently substituted during coding.

## 3. Elapsed time is not deadline-origin provenance

A qualified elapsed counter answers **what time is it now?** It does not answer **when did a queued Clock become physically due?** Keep those proofs separate.

**Proposed provenance boundary:** existing receiver/transport queues own timing metadata for their existing payload; MidiHandler retains matching metadata with its one retained batch. No extra domain/global content queue, duplicate payload queue or second tick authority. A receipt needs queue/route-generation identity and a conservative physical arrival interval. Its lower endpoint must not be a dequeue/dispatch-time upper bound mislabeled as an arrival lower bound.

| Route | Earliest existing boundary worth investigating | Missing premise |
|---|---|---|
| USB-device | Receive-transfer completion before the buffer is exposed to dequeue | Completion/IRQ delay and packet/sender batching uncertainty; metadata lifetime through transfer-buffer reuse; accessible supported hook or explicitly authorized maintained adapter/fork. |
| USB-host | Transfer completion before `rx_queue` publication | Host scheduling/IRQ/completion uncertainty, disconnect/reconnect queue generations, exact correspondence through read/callback and buffer backpressure. |
| DIN | UART FIFO/byte acceptance before software-ring publication, preserving realtime bytes through parsing | UART/FIFO/IRQ delay interval, parser-to-byte association, overflow generation, partial/running-status/SysEx interactions. Byte transmission spacing alone is not queued-message age. |

No current application-level hook supplies these receipts. Adding metadata/hooks or maintaining a project-owned transport fork is **not** approved by read-only dependency permission or this capture. Its ownership, support/removal policy, exact layout, RAM placement and build provenance must be resolved before implementation. Transfer completion itself is not claimed to be sender/wire onset; any such interpretation requires a qualified uncertainty bound.

Missing/unknown ingress provenance, dropped/ambiguous receipt correspondence, reconnect/reset generation mismatch, or unbounded delay leaves the external reference unqualified. Do not infer future physical lead solely from the300BPM tempo clamp, batch caps, queue cardinality or empty queues. External source pacing is not enforced by the BPM estimator; USB can deliver already batched Clocks.

Internal-clock references likewise need an actual timer due-phase mapping and its interrupt/correlation error, not lastInternalTickTime written after track service. No internal reference is approved simply because external ingress is absent.

## 4. Proposed reference lifecycle and correlation

1. Cold source qualification establishes hardware configuration/ownership and physical era. It does not establish transport phase, ingress provenance, measured costs or a ready window.
2. Form a logical/physical receipt only for a running, supported source with a bounded correlation interval. Capture authoritative ClockManager tick/epoch and physical endpoints atomically or through a finite validation/bracketing sequence that preserves the caller's interrupt mask. Excess/unclassified bracket width refuses; do not assume zero-width correlation.
3. Derive a fixed physical deadline conservatively from the earliest legal adoption boundary and qualified source phase/provenance. Converting logical distance using the latest nominal BPM alone is not a proof. A changed later note still does not authorize mid-traversal adoption.
4. Stop, Continue/start, source transition/fallback, tempo/rate change, seek/rebase, setup/reset and producer-configuration change invalidate or explicitly requalify affected promises. A saved reference must not remain usable merely because current tick has not advanced or existing epoch still matches.
5. The existing logical epoch is the preferred owner extension point for invalidating logical promises; precise changes to its invalidation sites and compatibility consequences must be decided before implementation. Physical era denotes physical-producer lifetime, not a substitute transport/slot generation. Do not collapse unrelated identity domains to fit the current passive struct.
6. Retained input still survives invalidation and dispatches exactly once in existing transport-grouping/FIFO order. Reference refusal does not clear the batch, make newer input overtake it, or hold Clocks until window readiness. It suppresses unqualified inserted owner work and reports missing service/readiness honestly.
7. Source/slot/generation currency is revalidated by the existing Track owner at service/publication. Physical reference validity cannot manufacture the missing live running-Capture request, initial anchor, stable useful-prefix progress or stop/lifetime closure.

The passive helper's existing inputs/booleans do not enforce all these lifecycle requirements by themselves. Reuse its arithmetic/preview-commit behavior; do not silently declare new guarantees at its call sites.

## 5. Complete cost and return-to-input boundary

Every established cost pair must describe the **actual admitted execution path**. Child caps are not an owner maximum, and a budget checked between steps is not a bound on an indivisible step.

| Selected opportunity | Required closure before setting established |
|---|---|
| Snapshot | Shared maintenance, receipt assembly/allocation/discard, construction/publication, selected/background aggregation and return span. |
| VisualCache | Canonical-prefix fast path distinct from reconstruction; no transfer of Q32's bound to whole-loop fallback or multi-track work. |
| VisualCacheWrapAppend | Entire append/stale-rebuild body or a separately specified bounded continuation. |
| Persistence | Catalog work, whole SD operation/chunk/stage, admission bypass for queued chunks and return span. |
| LoadAndDisplay | Pre-child prewarm, selected child, blocking transfer/read, post-display undo warming and return span. |
| SlotProvision | Allocation/prewarm/legacy merged preparation, not just fixed descriptor scans. |
| PlaybackViewMaintenance | Offer/bookkeeping plus selected qualification/fill/order/publication/retirement child and return span; existing timer starts after some bookkeeping. |

Interrupt interference and all work before the next real input opportunity belong in the wall-time bound, not outside its measurement window. Avoid double-charging by naming a non-overlapping grain/return partition, while admitting against their complete sum plus uncertainty. Conditional input polls, motor sync, pressure/reclaim and queued-chunk persistence must not disappear from that partition.

An empirical bound needs its enforced overrun guard and failure policy under [Execution bounds](../../../../../docs/Authority/Architecture/ExecutionBounds.md). A breach observed after an indivisible operation cannot undo elapsed time; it faults the timing claim and must not be presented as successful deadline protection. Until complete bounds/envelope are established, owner costs remain unavailable. No existing300/3000us scheduler setting,15ms load/persist policy, Snapshot hypothesis or optimistic455.73us derived condition is adopted as a checkpoint budget.

## 6. Qualification gates, not implementation tasks

**Native producer mechanics:** future fixtures must link the actual producer/adapter boundary, injecting hardware reads only at the platform boundary. Control incoherent halves, finite retry exhaustion, low rollover, wide exhaustion, reset/configuration/era reuse, source refusal and cold reservation contention. A copied timing FSM or passing mock-time arithmetic is not production acceptance.

**Real logical/input lifecycle:** reuse the actual MidiHandler→ClockManager→Track/reader fixture. Exercise Stop/Continue/start, same-value rebase, source fallback, tempo changes, copied batches and all route receipt generations. Explicitly distinguish source-qualified now from provenance-qualified deadline. Add delayed/missing/ambiguous ingress controls; never delete a queued Clock to fit the oracle.

**Whole-owner execution:** control actual selected-owner/child work, refusal/recovery and mandatory saved-normal turns. Account every admitted grain and return-to-input component. Registered operation-count tests remain useful but do not establish device WCET.

**Resource price:** compile actual proposed state/receipt layouts on native and target; count metadata for each real transport queue plus retained batch, peripheral channels, stack/IRQ access and allocator overhead. No global resident state, field/pin rebase or payload duplication is approved by this document. The previously measured stamp16/deadline48/cost12/cursor2/decision4/table84B and continuation1056B are not the price of a complete producer/ingress design.

**Target qualification:** hardware latch/chaining/clock health/correlation and IRQ/USB/DIN/host delay evidence remain required, followed by enforced owner/return timing and admitted workload qualification. No target measurement image, upload, serial marker or HITL stimulus is authorized here. Default target continuation remains compile-rejected; rollingOFF/legacy remain unchanged.

## 7. Disposition and next decision

Dependency inspection is complete; preferred candidate and failure/lifetime boundaries are proposed. **G1 — architecture is not closed**: hardware read/ownership semantics, ingress hook/support choice and receipt resource price, complete correlation/delay limits, reference invalidation/compatibility and complete cost enforcement remain unresolved. Do not generate implementation tasks that ask a builder to discover these during coding.

Next bounded design work: verify the PIT hardware read/reservation semantics and choose the project-supported ingress observation boundary/metadata lifetime. Keep elapsed-source mechanics, deadline-origin qualification and complete service readiness as distinct acceptance claims. An elapsed-only prototype could be useful later but must not be called an external physical-deadline producer or dependency-closed target integration.

No decision-log entry is added: the user accepted capture, not this timer/transport design. OpenSpec parent checkbox count remains29/60. No firmware/dependency edits, target enabling, device/SD or Git delivery.

## 8. Evidence pin and verification applicability

Read-only commands: bounded Glob/Grep/Read over the authorized installed package; OpenSpec list/spec inventory/status; SHA256 inspection and git diff hygiene. No new code, tests, compiler probes or firmware builds were run in this design/capture step. Earlier native72/72 discovery check and fullnative3127pass7skip/defaultcaptureSUCCESS apply only to unchanged implementations, not this proposed producer.

SHA256 relative to the installed package root:

```text
package.json 67d671aa5e5cbd72b43860d0a2f7b64c9478c5b56eb1e9326e9a33414c5cbf6f
cores/teensy4/delay.c 15db454f188a0e8d36285c43afa6a1b4e9e5663811ec1e6a51d5a35bf31a764b
cores/teensy4/rtc.c 6d3b48e07271711842fb272b56bbbadfe2d227e0ef3a26a0fd8382321d4330d5
cores/teensy4/startup.c 39c6e6de4799c11546e7a3ee662ee6ce948aa13e1f57485505a60a09de66ad1a
cores/teensy4/IntervalTimer.cpp a1b02737b226ca80d128bb8e72dec49d7c0318c61c41a77c5a4d5438df962d07
cores/teensy4/imxrt.h faf5cc8372b55db7ba21cb81eebb4a10a4d20d4db8c503878450b349cc486f80
cores/teensy4/usb_midi.c 6e3e1ebb1c677c7f04bf92be06a3dd33e779b518ccded7135a04196b36b476d5
cores/teensy4/HardwareSerial.cpp 54f61046d7f09e834214dbc024b299731e7b771f351483bf94d58ece09ae8571
libraries/USBHost_t36/midi.cpp e6933d23485cfe8d80f4cdb5379f665ab2f501c5143ab8b0b34fdca1a4492ce6
libraries/MIDI/src/midi_Message.h 0b43cbd65e3a0bf1619ae31a228bb9e3fb353cb9f7959835eb3fb76befc465bd
```

Firmware/helper hash checks match the checkpoint precontrol receipts. Documentation strict/link/progress/diff checks are recorded after capture; no physical hardware guarantees follow from them.

Capture closeout: `openspec validate windowed-playback-generation --strict` valid; `git diff --check` clean; seven draft links and all four status links resolve; ten dependency-file hashes match the inspected installation; helper/main/ClockManager precontrol hashes pass. Task count29/60 unchanged. No implementation or numerical timing/source-health verification inferred from these documentation checks.
