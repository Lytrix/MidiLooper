# MIDI receiver ingress receipts

Optional receiver metadata on the existing USB-device transfer, Serial8 DIN byte and USB-host word queues. Decision: [DEC-125 — pinned receiver ingress receipts](../DECISION_LOG.md#dec-125--pinned-receiver-ingress-receipts). Verification/resource evidence: [I5 — ingress execution](../../openspec/changes/windowed-playback-generation/evidence/2026-10-13-i5-running-capture-service/INGRESS-EXECUTION.md).

## Build and maintenance

`MIDI_INGRESS_RECEIPTS_ENABLED` defaults to0. The default `teensy41-capture-serial` build compiles the original installed sources and contains no receipt implementation. `teensy41-capture-serial-ingress` enables receipts and `scripts/midi_ingress_build.py`, which validates the pinned Teensyduino1.159.0 source hashes and unique insertion anchors, then writes copies under BUILD_DIR. Installed dependencies remain read-only; original licenses/payload operations are preserved. Unsupported framework/source drift fails the build rather than silently omitting a hook. Updating a pin requires renewed source-boundary tests and review, not changing the expected hash to make a failed build pass.

The enabled profile uses the **same numerical RAM1 budget** as capture. Input-only dispatch helpers are flash-resident in this profile. Compiler stack reports are enabled; individual frames do not prove whole-program stack safety or WCET. No firmware is automatically uploaded.

## Ownership and lifetime

MidiHandler setup cold-provisions one non-aborting external-first metadata store:6 USB transfer cells,64 Serial8 byte cells,400 configured BigBuffer host word cells. A startup-cleared BSS pointer publishes the constructed store. Hooks never allocate. Unsupported owners/capacities or provisioning failure leave payload operation intact without qualified metadata.

Packet begin samples once; exact-slot publication occurs before the existing queue index is published. Dequeue copies the receipt before releasing the slot, never restamping it. USB event offsets distinguish partial packet reads; DIN/host cells track individual byte/word slots. Repeated/skipped USB offset consumption refuses association. Generation/sequence identities exhaust terminally rather than recycle.

Reset, DIN overflow, host disconnect and sampler replacement invalidate receipt currency; already accepted payloads and copied observations are not discarded. MidiHandler copies receipts with ordinary batches and native-qualified retained rows. USB/DIN retain transport-first grouping; host retains FIFO. Native test dispatch observers are absent on target.

## What an observation means

ClockManager setup now installs its bounded Run-mode PIT sampler in the ingress profile under DEC-128 — ClockManager PIT elapsed reference; see [physical elapsed reference](CLOCK_PHYSICAL_REFERENCE.md). Before setup or on source refusal/fault, receipts carry labeled rawmicros32 diagnostics and observed64bit time remains0. No widening makes micros32 independent. Sampling is nonallocating and preserves the enclosing IRQ mask; counter mechanics are locally verified, not device timing/correlation qualification.

USB metadata observes receiver completion, not sender onset. DIN MIDI parser messages carry the observation of the completing byte; partial/running-status/SysEx frames do not acquire a first-byte or whole-frame age proof. Empty-ring direct reads and empty-ring peek insertion are explicitly LateHardwareRead. Host disconnect can leave readable old words whose receipts are no longer current.

`independentObservation()` means only an associated/current counter observation with nonzero era. It is **not** physical arrival, bounded age, deadline admission, input-health or running-Capture readiness. Future reference ownership still owes era lifecycle/correlation, IRQ/FIFO/USB delay bounds and enforced complete owner-to-input execution costs. Target continuation and default rolling playback remain disabled.

## Relevant checks

- `pio test -e native -f test_midi_ingress_receipts`
- `pio test -e native -f test_midi_ingress_transport` (requires pinned framework; generates actual upstream bodies under BUILD_DIR and uses the pinned MIDI parser)
- `scripts/test_midi_ingress_sources.py` and `scripts/test_midi_ingress_build.py`
- Real-input ordinary/native-continuation gate profiles, full native tests and both capture/ingress builds; exact commands/results are in the execution evidence.

No hardware arrival/counter/IRQ-delay qualification or new serial marker/stimulus is delivered here.

## Native complete-hook/mask qualification

`MIDI_INGRESS_MASK_QUALIFICATION=1` is a native-only test seam, default0 and compile-rejected on Teensy/non-native builds. It exposes only the existing IngressMask enter/restore hardware boundary; target PRIMASK instructions and installed/generated receiver code remain unchanged. The actualreceiver fixture and real ClockManager fixture share a native PRIMASK model, separating ingress, physical-counter and original driver operations. Packet/direct incomingmask0/1, refusal/recovery, raw/fault fallback, exact nested counter/receipt/dispatch helpers, packet/burst samplerfrequency and FIFO-pressure preservation are asserted. Original DIN unconditional IRQ-enable behavior is observed separately, not mistaken for hook restore or repaired in the fake.

[I5 — ingress mask qualification evidence](../../openspec/changes/windowed-playback-generation/evidence/2026-10-13-i5-running-capture-service/INGRESS-MASK-QUALIFICATION.md): receiver18/receipt9/ordinaryclock35, optionalingress45/retained47, fullnative3207passed7skipped119suites, host30 and ASan/UBSan18 PASS; one focusedreview2oracle gaps fixed/retested. Absence-of-mask baseline13RED, suppressedrestore10RED and duplicatesampler3RED discriminate the coverage; restored source verified. Default buildSUCCESS/padding1688/variables96192 unchanged/no qualification or receipt symbols in default ELF. No new target instrumentation, serialmarker, upload or device timing qualification: measured completehook/mask/receiver duration and finite owner/IRQ bounds remain subsequent gates.

## Passive ingress-cost arithmetic (not a target probe)

`Utils/MidiIngressCost.h` supplies caller-owned tokens and four independent cycle
aggregates for hook-call, conservative mask-bracket, publication and empty-probe
observations. The header itself does not read DWT/PIT, log, lock or instrument
receivers. Gated target callers described below now establish the named brackets.
The sample validator requires enclosing extended-time brackets, matching nonzero
era/Hz/configuration and an explicit continuous-counter/rate-bound contract over
the whole interval. Endpoint equality cannot prove continuity. The enclosing
duration must preclude a full DWT wrap at that supplied bound; missing/faulted
samples refuse, count/sum exhaustion saturates terminally without wrapped results.
Target callers serialize aggregate access and supply an explicitly assumed continuous
Run-mode/rate contract, not a calibrated or endpoint-proven guarantee. No overhead
subtraction, finite WCET, calibrated rate, wire arrival or readiness claim follows.
See [I5 — cost arithmetic contract and evidence](../../openspec/changes/windowed-playback-generation/evidence/2026-10-13-i5-running-capture-service/INGRESS-COST-QUALIFICATION.md).

## Gated target observations (default OFF)

Set environment variable `MIDI_INGRESS_COST_QUALIFICATION=1` only with
`pio run -e teensy41-capture-serial` to opt into receipts and cost probes; unset,
empty or0 is OFF, other values/environments refuse. Installed sources stay untouched.
Existing MidiIngress State adds384B gated aggregates (ARM19552B/host19576B total);
fixed snapshot384B and caller-local40B tokens, no hot allocation.

Measured macro wrappers bracket the actual packet/direct hook; the HookCall lane
includes enabled inner probes/first publication but excludes wrapper prologue/
epilogue/empty probe/final publication. MaskBracket conservatively encloses the
original disable/callee/restore, including sampling and possible preemption: not
exclusive IRQ-off time. Publication brackets the first protected aggregate update;
its second self-accounting update is excluded/unmeasured. EmptyProbe is separate;
do not subtract it to infer uninstrumented cost or WCET. Saved-mask snapshots/output
restore the mask before serial emission; no per-packet IRQ logging.

Read-only `!INGRESS_COST <uint32>` emits12rows,3routes×4lanes:
`#CAP,time,DIAG,ingress_cost,id,route,lane,cpuHz,guardBits,accepted,rejected,minCycles,maxCycles,sumCycles,saturated,contractBits`
Routes0USB-device/1DIN/2USB-host; lanes0hook/1mask/2publication/3empty.
Bench acceptance requires CPU600MHz/guard15/contract7/nonsaturated. Contract bit4
explicitly ASSUMES whole-interval continuous Run-mode/no unseen halt/reset/clock
rewrite/fault; bits1CPU/DWT+2Ready source cannot establish that continuity.
Aggregates are lifetime; only counts/sums may be differenced between legs, never
min/max. Count0 uses minUINT32_MAX. Stable quiescent macro deltas are1:1:3:1;
in-flight snapshots may differ, and packet count is not MIDI Clock count.

[I5 — target execution and evidence](../../openspec/changes/windowed-playback-generation/evidence/2026-10-13-i5-running-capture-service/TARGET-INGRESS-COST-EXECUTION.md)
records smoke/full stopped Clock-only120/300BPM PASS and exact default-OFF restoration.
Lifetime USB instrumented hook21699..33039cycles/mask5895..15983cycles observed;
DIN/USB-host physical fixtures NOT RUN. No calibration/arrival/full receiverIRQ/
finiteowner/sourceISR-main/readiness qualification. Dedicated TEMP preset retains
canonical serial ownership/two Stops/drains/postflight; no Start/content/slot/SD-cleanup
gestures, ordinary automatic persistence possible.
