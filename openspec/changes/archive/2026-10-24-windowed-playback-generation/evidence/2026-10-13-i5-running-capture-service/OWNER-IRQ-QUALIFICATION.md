# I5 — complete owner/IRQ qualification disposition

2026-10-14, branch `feature/unified-rolling-playback-path`, HEAD `26125ceb`, preserved dirty tree. Read-only trace followed by [I5 — guarded motion observations](PHYSICAL-MOTION-QUALIFICATION.md); no production scheduling/IRQ/driver behavior changed. Existing bound authority is [ExecutionBounds](../../../../../docs/Authority/Architecture/ExecutionBounds.md), DEC-043 — execution-bound classification and DEC-123 — checkpoint arbitration/deadline admission.

## Verdict

**G0 — bounded discovery PASS; G1 — finite complete owner-to-input/IRQ admission NOT READY.** Actual call composition is established, not a complete transitive WCET proof. The 15 moving hardware points and previous stopped production-read distribution do not fill the gaps below. No numerical cost table admitted, checkpoint/rolling activation or 740→768 readiness claimed. No new decision or support-envelope reduction adopted.

## VERIFIED — actual interval composition

`src/main.cpp::loop` invokes a mandatory note-edit competitor then input, followed by DROID outbound, edge-effect drain, clock-source fallback, control-surface/button/fader/bar-step work, display/undo warm, LED output, looper update, track scans, capture flush and owner selection. Input polls around idle/load work are conditional on post-overdub PLAYING/deferred-stop/capture states. Before the universal later poll, motor sync, memory-pressure/reclaim and persistence can stack. After that input, buttons, capture serial commands, census/library/telemetry and periodic accounting precede the next input; framework yield is another dependency. Boot recovery/title/USB enumeration are not silently included in a cheap live envelope.

The seven `MidiServiceDrain::DeferredIdleOwner` lanes are not seven isolated grains:

- Snapshot: wrapper scans tracks/slots; active paths can serve selected plus a different background track. Construction/publication/ref retirement and shared `Track::processDeferredIdleMaintenance` children count.
- VisualCache: canonical-prefix priority or all eligible realtime tracks; non-note REVT inspections are not capped by the emitted note-on counter. Shared prechecks, rebuild/validation/release and wrapper tail count.
- VisualCacheWrapAppend: pending globally suppresses other lanes; append/release plus shared prefix count. One append call is not a proved elapsed maximum.
- Persistence: queued capture chunks bypass ordinary owner/load/post-stop guards in `MidiServiceDrain::serviceDeferredPersistenceTurn`; `StorageManager::processDeferredSaveState` includes catalog/SD/filesystem/commit/allocation children. Checking elapsed time between children cannot bound an indivisible SD operation.
- LoadAndDisplay: prepared-pair service occurs before active child selection; completed paint triggers undo warm outside the display child probe. Stopped paths can stack prewarm, paint, load, snapshot/save/reclaim/title completion.
- SlotProvision: stale launch cancellation across tracks×slots precedes one target. Runtime take/allocation, legacy gather, release and refusal telemetry count.
- PlaybackViewMaintenance: all-holder offer/deadline work precedes the selected worker timing probe; owner offer/epilogue and actual next-input return count. Rolling remains OFF.

`Track::processDeferredIdleMaintenance` has early-return branches but successful ordinary paths can compose stop/accepted interaction, capacity release, canonical replenish, Record geometry/order, preview/display retirement, Snapshot, REVT/source spans and deferred validation. Structural row/page caps are work-count evidence, not elapsed admission.

## VERIFIED — input/IRQ/sampler boundaries

`ClockManager::updateInternalClock` runs from the MIDI IntervalTimer ISR and performs real TrackManager/Track transitions/playback, not merely deferred edge effects. External `ClockManager::onMidiClockPulse` executes synchronously in input dispatch and includes source/BPM/timer changes and all-track update per Clock. Input batch caps do not bound the transitive handler cost. Track tick work can traverse enabled extra slots, rebuild/gather, send closures/all-notes-off and slot commits.

Pinned installed Teensy sources (read-only, prior producer/ingress source pins retained) show default PIT/USB priority128, actual DIN Serial8/LPUART5 priority64, SysTick/PendSV32, assuming no runtime/library rewrite. SysTick and DIN may preempt PIT/USB; equal-priority PIT/USB defer each other. Target NVIC/mask snapshot, IRQ arrivals, tail chaining, cache/DMA/bus/PSRAM interference and nested stack maximum remain unqualified.

Backend elapsed blockers are executable, not speculative: USB-host MIDI `write_packed` retries when both TX buffers are full; USB-device MIDI output waits/yields/timeouts/transfer submission; HardwareSerial TX-full waits/drains/yields. USB device IRQ includes control/reset/queue/hardware waits; USB-host IRQ/task includes topology-wide transfers/timers/enumeration/disconnect waits. Unrestricted host/peripheral failure may have no finite maximum; support/failure/overrun policy is required before admission.

Optional `MidiIngress` packet/direct-DIN hooks hold a saved/restored mask across raw micros plus indirect sampler, metadata and queue begin. The physical producer nests a preserved mask, reads/checks configuration twice plus flags/high/latched-low (20 platform register reads on Ready), wide arithmetic and state publication. Packet/burst frequency differs from per-word/byte consume/publication. Previous standalone DWT production read11.970–34.090µs excludes complete hook/receiver/outer masked duration and cannot be multiplied into WCET. Receipts remain defaultOFF in the motion run; no receiver-IRQ cost measured here.

Existing telemetry records elapsed observations: exit-to-next-entry input gap, input duration, route read/dispatch/task spans, selected owner/child maxima. Probe windows omit stated prefixes/tails or include nested IRQ time. Independently reset/windowed maxima are not simultaneous composable samples or exclusive IRQ costs. The6250µs counter threshold detects, not enforces, return. `getDisplayTickPhase` and some USB backend paths use unconditional interrupt enables; complete caller-mask reachability remains UNKNOWN, not repaired by masking an entire unbounded owner.

## Concrete remaining qualification requirements

1. **Complete ingress sampler/mask measurement:** controlled production `midi_ingress_packet → observe → getPhysicalClockStamp → queue begin` and direct-DIN paths, incoming main/ISR mask, Ready/fault/disabled/pressure branches; reuse generated actual receiver-body/native counter fixtures. Pin full hook and IRQ entry/exit intervals, overhead/frequency, receipts ON/OFF, warm/cold flash/PSRAM and bounded failure evidence before target instrumentation. Current motion image has no such probes.
2. **Whole owner-to-next-real-input fixtures:** max-cardinality multitrack/slot work, shared prefix/tail and conditional poll bypass; capture-chunk persistence bypass; allocation/refusal/retirement, display/undo/load, TX saturation and IRQ bursts. Assert exact work/input order/content independently, with bounded drains and explicit refusal/overrun result. Existing helper unit tests alone are insufficient.
3. **Finite support/failure envelope:** indivisible SD/filesystem/allocator/TX/hardware waits, bounded driver topology/control/reset/disconnect, target priorities/masks/arrival interference and nested stack headroom. Detecting lateness after a blocking child cannot prevent it; an enforced pre-admission/overrun policy must be justified, not fitted to a measured maximum.
4. **Independent timing provenance:** no calibrated reference is available; only host USB-device input. Host send-call pacing is not wire arrival. Receiver completion/late FIFO receipts are not sender time; arrival-delay or complete response bounds cannot be inferred from the musical service clock.
5. **Source concurrency:** real internal ISR can transition/reset Track while main reads/cancels/adopts. Existing retained/serialized native source fixtures do not establish that protocol. Do not mask an unbounded builder/owner to manufacture exclusion.

No whole-cost redesign, unsupported-envelope shrink, default instrumentation enablement, driver fork or budget rebase is implicitly approved by this report. The smallest dependency-ready next measurement is complete ingress-hook/mask cost with pinned real receiver boundaries; it remains separate from finite seven-owner admission and from absolute calibration. The current slice stops at meaningful motion/pacing observations plus these concrete gates, not a blanket PASS.

## Native complete-hook prerequisite delivered (2026-10-14)

[I5 — native ingress mask qualification](INGRESS-MASK-QUALIFICATION.md) closes the native outer-mask evidence gap: target-rejected/defaultOFF seam, actualreceiver samplerfrequency/pressure/refusal/FIFO oracles, actualcounter nestedmask/readcount/fault fallback and receipt/dispatch nestedmask traces. Baseline13RED/restore10RED/frequency3RED restored; receiver18/receipt9/ordinaryclock35/ingress45/retained47/native3207pass7skip119suites/host30/ASan18/defaultbuildsameRAM1. One focusedreview2oracle gaps fixed/retested. This supplies the controllable boundaries for requirement1, not target-duration data or a finite bound. Next is gated completehook/masked/receiver duration+overhead measurement at these boundaries; requirements2–5 and original G1 — finite complete-owner/IRQ admission NOTREADY remain unchanged. No upload/device/SD/defaultactivation/taskcheckoff.

## Passive cost arithmetic prerequisite (2026-10-14)

[I5 — cost arithmetic qualification](INGRESS-COST-QUALIFICATION.md) supplies the
conditional sample-validation/aggregation oracle for requirement1: full-wrap
resurrection refusal, local noncopyable nested tokens, exact-once consumption,
four distinct measurement lanes and terminal nonwrapping statistics. No firmware
caller/resident instance, producer, lock, receiver/IRQ probe or target duration
is supplied. Continuous counter/rate relation and enclosing physical-time brackets
must be established by the future producer; endpoint identity alone cannot do so.
Requirement1's target measurements and requirements2–5 remain open; the finite
complete-owner/IRQ admission verdict remains NOT READY. No parent checkoff,
activation, upload/device/SD/Git action or numeric budget change.

## Conditional target ingress observations delivered (2026-10-15)

[I5 — target ingress-cost execution/evidence](TARGET-INGRESS-COST-EXECUTION.md) partially
supplies requirement1 with gated actual USB-device hook/conservative mask/publication/
empty-probe observations, not the full receiver/IRQ span. Smoke02/full01 stopped Clock-only
120/300BPM PASS each8complete12row snapshots/6Readyera2guard15; full hook deltas239/598/
exact1:1:3:1/no new rejection/no saturation. Lifetime hook21699..33039cycles,
mask5895..15983cycles; inner probe overhead included, publication self-accounting tail
excluded, no exclusiveIRQ-off/overhead-subtracted/WCET inference. DIN and USB-host physical
fixtures NOT RUN. Whole-interval continuous Run-mode/gross rate premise remains assumed,
not endpoint-established. Native3228passed7skipped121suites/host112/ASan8/ON+OFFbuilds/
one repaired review; floor840unchanged. Exact enabled image preserved/verified no-build
upload, then exact default-OFF restoration/verified stopped/idle/guard15/two drains/
postflight/no latch. No Start/content/slot/SD-cleanup gestures; automatic persistence occurred.
Requirement1's remaining complete receiver/IRQ/owner composed costs and requirements2–5
are NOT closed. G1 — finite complete-owner/IRQ admission remains NOT READY. SourceISR-main
synchronization/calibration/wirearrival/running request-anchor-adoption also remain;
no parentcheckoff/defaultactivation/Git or740→768readiness.
