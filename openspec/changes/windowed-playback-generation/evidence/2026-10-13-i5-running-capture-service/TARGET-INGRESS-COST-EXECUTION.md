# I5 — gated target ingress-cost observations

Status: COMPLETE for conditional instrumented observations; native/host/build/review and managed smoke/full verified; exact default-OFF image restored and stopped/idle reverified (2026-10-15). No parent checkoff or finite owner/IRQ admission.
Baseline branch feature/unified-rolling-playback-path /26125ceb plus preserved dirty tree.
Scope: dependency-ready measurement under3.10 — bounded owner service and3.15a — running
Capture oracle, not checkoff. Parent29/60/defaultrollingOFF/targetcontinuationdisabled.

## G0 — evidence / G1 — owner extension

Existing paths and remaining whole-cost gaps are recorded in
[owner/IRQ disposition](OWNER-IRQ-QUALIFICATION.md),
[native hook/mask qualification](INGRESS-MASK-QUALIFICATION.md) and
[passive arithmetic](INGRESS-COST-QUALIFICATION.md). MidiIngress cold external-first
State owns metadata, packet/direct hooks preserve PRIMASK around observe/queue begin;
ClockManager owns PIT/era/sampler. Generated project-owned copies invoke macros in pinned
actual receivers. Callback span does not equal full IRQ. No src clock/DWT writer found;
pinned core startup sets F_CPU/enables DWT and fault handling can downclock. Existing
PIT producer refuses sampled halt/sleep/config/era faults. Unseen interruptions cannot
be excluded by endpoints. No new producer/resource/queue/owner/persistence representation.

Extend the existing receipt State with three gated128B aggregates (+384B payload),
not Track/Loop fields. Caller-local endpoints/tokens and fixed snapshot copies only;
no hot allocation/logging, no fourth view/timing manager or numerical budget change.
Instrumentation must remain explicitly opted-in. Existing DEC-125 — pinned receiver
ingress receipts and DEC-128 — ClockManager PIT elapsed reference remain controlling.

## G2 — execution contract

### Gate/build/ownership

`MIDI_INGRESS_COST_QUALIFICATION` defaults0; requires receipts plus SESSION_CAPTURE or
native test, refuses unsupported nonnative platforms. Target fixed600MHz image only;
reported CPU/DWT enable/config and physical source endpoints must validate. Default
capture gets a pre-script that is a no-op unless environment variable
`MIDI_INGRESS_COST_QUALIFICATION=1`; only teensy41-capture-serial may opt in. Values unset,
empty or0 mean OFF; other values refuse. ON appends receipts/cost flags and invokes the
existing hash-pinned middleware; OFF does not generate/rewrite/copy receiver sources.
Installed dependencies/source hashes/insertion transforms unchanged. Every enabled/disabled
build must pass the existing numerical RAM1 floor; no budget rebase. No automatic upload.

### Sampling and truthful boundaries

An endpoint reads the installed independent sampler before and after the DWT read;
use floor-us before and ceiling-us after (+1, overflow refuses), matching nonzero era,
nonzero CPU/config. Reuse passive interval validator. Conditional rate bound1.2GHz per
nominal PIT-us second is a gross2x margin for fixed600MHz Run-mode observations, NOT a
calibrated rate or deadline budget. Contract premise: continuous Run-mode/no unseen
halt/reset/clock rewrite/fault for the whole span. Matching endpoints cannot prove it.
This assumption must appear in report limitations and row contractBits; hardware results
are CONDITIONAL OBSERVATIONS only, never established owner timing or WCET.

Four lanes per route (USB-device0/DIN1/USB-host2):
0 HookCall: measured macro wrapper samples immediately around a noninlined actual
  midi_ingress_packet/direct call. Includes the callee's enabled inner probes and their
  first publication; excludes outer wrapper prologue/epilogue/empty-probe work and its
  final publication. It is the instrumented call, not uninstrumented runtime cost.
1 MaskBracket: samples before the original ingress disable and after its restore,
  enclosing the real owner/capacity/refusal/observe/begin path. Sampling/check overhead
  and preemption can inflate this conservative interval. It is not exact exclusive
  IRQ-off cycles; probe sampler reads themselves introduce other preserved masks.
2 Publication: brackets the first saved-mask aggregate update with passive validation.
  Recording this publication measurement requires a second saved-mask update; that
  self-accounting tail is explicitly EXCLUDED and remains additional unmeasured overhead.
3 EmptyProbe: same endpoint-pair machinery with no ingress operation, once per measured
  macro call, published separately. No subtractive correction or WCET inference.

Each measured packet/direct macro produces1hook/1mask/1empty/3publication samples.
Early missing State cannot publish; original payload operation still executes. Foreign
owner/capacity/exhaustion refusals remain measured without fabricating receipt payload.
Direct calls to the original exported hook (not measured macro) produce mask/publication
only. Reset/publish/consume/helpers keep original masks and are not silently timed.
All updates and snapshots serialize through saved-mask scopes; use local start/end tokens,
never a shared start slot. No locking across an unbounded grain. Aggregate lifetime is
cold State lifetime; setup resets diagnostic aggregates with metadata, no serial reset.
Snapshot is read-only/fixed384B and output occurs after restoring mask. Empty min sentinel
UINT32_MAX is meaningful only when count0. Sum/count exhaustion follows passive saturation.

### Serial schema and source policy

New gated strict uint32 command `!INGRESS_COST <id>`; no effect for malformed input.
Existing PIT schemas/commands unchanged. One request emits12rows (route0..2 x lane0..3),
common guard snapshot from existing all-track/storage/pending-start guard. No Start is sent.
Each row has exactly16 comma fields:
`#CAP,time,DIAG,ingress_cost,id,route,lane,cpuHz,guardBits,accepted,rejected,minCycles,maxCycles,sumCycles,saturated,contractBits`
All fields unsigned decimal; time/id/cpuHz/guard/accepted/rejected/min/max are32bit,
sum64bit, route0..2/lane0..3, saturated0/1. contractBits:1 current600MHz/DWTenabled;
2 Ready nonzero physical source;4 conditional Run-mode continuity/rate premise explicitly
assumed (not established by measurement). Require7/guard15 for accepted bench snapshots.
Rows are low-rate, direct USB if room else Tier-A retained ring; format/prefix named PROGMEM
in DebugSessionCapture. No IRQ serial/logging/per-packet marker. Max row <192B verified.
Counters/min/max are LIFETIME aggregates, not isolated leg maxima. Counts/sums may be
compared by before/after differences; no min/max subtraction. Snapshot during traffic
can catch a hook between its masked publication and final wrapper publication, so live
lane counts need not yet match. After pumping stops and callbacks settle, exact per-hook
ratios are the expected quiescent invariant. Missing/duplicate/wrong IDs/rows refuse.

### Native oracle fixed before code

New native hardware-boundary fixture links actual MidiIngressReceipt.cpp and actual
measured macros. Deterministic endpoint counters/physical sampler and shared PRIMASK
fixture only (no copied hook policy). Illustrative clock reads advance100cycles per
endpoint, extended physical reads advance10us, valid600MHz/config1/era9.
For a single measured packet/direct after cold setup: hookcount1/sum500; maskcount1/sum100;
emptycount1/sum100; publicationcount3/sum300. Sampler25calls (24probe +1 actual observe)
for valid owned receipt; original receipt publishes/consumes exactly once/FIFO unchanged.
All3routes and incoming masks0/1 must restore, foreign owner/capacity/sequence refusal
preserves payload/receipt semantics; missing State performs no publication; missing/faulted
sampler or invalid DWT/CPU refuses diagnostic samples without suppressing payload.
Snapshot read-only, invalid route refuses, saturation terminal per lane, nested callbacks
have independent tokens and exact publication counts. Default-off unchanged suites pass.
Missing API pre-fix compile is not behavioral RED; once linked, omit restore/publication
or duplicate hook control must fail a discriminating assertion then restore exactly.

### HITL recipe (before markers/scenario code)

- Criterion: observed instrumented USB-device hook/mask/overhead distributions under
  stopped Clock-only120/300BPM traffic with intact guard15, physical Ready/era continuity,
  exact complete12row snapshots and monotonic counters; no target admission/readiness claim.
- Stimulus list run2026-10-15; reuse managed pit_clock preflight/pit_motion fenced-observation
  patterns and MidiClockMaster.start_pump (Clock-only, NOT start()). New registered TEMP
  `STIM-INGRESS-COST-TEMP-01`, separate `ingress_cost` scenario/preset; canonical stimuli unchanged.
- Input only host USB-device MIDI; no DIN/USB-host fixture. No Start/content/slot/Save/Load/SD
  gestures. Canonical DeviceSession owns two Stops/drains/postflight/latch; ordinary automatic
  transport/source-loss persistence possible, no card byte-immutability claim.
- Marker DIAG,ingress_cost: Tier-A central named PROGMEM prefix,12rows/request, <=1request/s;
  emission DebugSessionCapture::ingressCostImmediate from main serial callback only.
- Tier test both predicates/ring+marker preflight before flash. Previous padding1688/floor840;
  gated owner-state payload384B external-first, cold/probe methods FLASHMEM; remeasure both modes.
- Managed full scenario: preflight stopped/closed overlay/idle+fresh pit_motion guard15;
  baseline12row snapshot, pump120BPM2s (smoke) or5s, stop pump/quiesce/before-after snapshots;
  repeat300BPM; fresh stopped PIT/guard snapshots before/after each leg, no Start.
- Full managed timeout<=360s. Require zero safety/critical-loss issues, complete correlated
  rows, route0 hook increase>0 and quiescent ratios where no overflow, valid stats/sum/count;
  do not equate packets with Clock messages. Other routes report NOT RUN, no coverage inference.
- Exact unique fences/IDs, collect delayed ring rows even after immediate end fence and
  detect duplicate available tail. Retain serial through canonical shutdown and verdict report.
- Preserve actual HEX+ELF before no-build upload, verify both unchanged after upload. No cached
  image identity substituted for evaluated image. Dedicated preset exception applies only after
  native/host/marker/resource/review gates. Never execute command.json.

### Verification and completion

Focused native probe + passive + receipt + actualreceiver + clock/mask neighbors; host
middleware/parser/scenario/tier checks; ASan/UBSan; defaultOFF/ON gate compile checks and
source pins, type/state/stack pricing, both default-env OFF/ON builds under unchanged floor;
fullnative; one focused independent review of this slice only, fix/retest concrete findings;
strict OpenSpec/link/diff closeout. Hardware-only rows/observations recorded separately.

### Local evidence so far (not device evidence)

- New8case probe fixture plus passive/receipt/actualreceiver47/47 PASS; probe/ring/tier65/65 PASS including new command/emitter case. Native fake masks restored for incoming0/1/allroutes. Actual receipt remains sequence1/time1130 under controlled clocks, not any probe timestamp.
- Initial probe run failed because fixture assumed cold setup recycles sequence/generation; production deliberately does not. Corrected fixture precondition through existing native-only setCountersForTests; no production reset or weakened assertion. Then47PASS. Initial5fail+SIGTRAP not claimed as a production RED.
- Standalone ASan/UBSan of final probe fixture8/8 PASS. Scratch-only omit HookCall aggregate update causes6specific failures of8; live source never mutated (SHA256 abd2b640b74f86c2c4473f12db24ada9ed43eb31f293d00ced50bc9467be745a).
- C-compatible gate matrix: default/nativeON/TeensycaptureON compile; missingreceipts/unsupportedplatform/targetnoCapture/target+nativewithoutCapture refuse exact diagnostic. Host source/middleware/opt-in35/35 PASS (includes5new build-gate cases).
- Marker preflight `scripts/check_marker_contract.py 'DIAG,ingress_cost,'`: EMITTED/TIER-A/FIT, namedprefix18/31bytes; ring case asserts prefix boundaries/direct/fallback/uint32parser/maxrow<192B.
- `MIDI_INGRESS_COST_QUALIFICATION=1 pio run -e teensy41-capture-serial`:SUCCESS112.814s. RAM1variables96192/code391304/padding1912/locals34880; RAM2variables163392/EXTRAM11680, floor840unchanged/margin1072. Measured wrappers/snapshot/emitter present in ELF; generated USB contains expected ingress macro; installed transforms unchanged.
- Pre-review enabled image copied to session scratch `i5-target-cost-image/pre-review/`: HEX2aa8a81bc01241b48592c8d7123cdbc26adb4d45c6bb8835749455994f20410e, ELF49711c6d5c7224faf42c2597f1c504f2d3f423d9de6bca99044ee939c6dcaccb. NOTuploaded; not the final reviewed image.
- ARM State19552B/host19576B (+384payload); fixedsnapshot384B/token40B bothABIs. Enabled target compiler frames: costEndpoint72/updateCost56/publishCost88/snapshot24/packet192/direct200/measuredpacket88/measureddirect80/serialsnapshot448B. Individual frames only, no composed stack/WCET claim.
- Independent host implementation reports115tests PASS after unchanged rerun of one timing-sensitive existing midi_clock pacing failure (7.216ms vs3.333ms); no device claim. Newhostscenario/registry/stimulus/read-only whitelist delivered; review pending.
- Full `pio test -e native`:3235total/3228passed/7skipped/121passed suites/0failed,350.812s. Newprobe8/8 and newringcase present. Existing recovery/diagnostic/host-copy skips unchanged.
- OFF `env -u MIDI_INGRESS_COST_QUALIFICATION pio run -e teensy41-capture-serial`:SUCCESS76.913s; variables96192/code391528/padding1688/locals34880/RAM2variables163392/EXTRAM11680; floor840unchanged/margin848. ARMnm confirms no measured wrappers/snapshotCosts/costEndpoint/ingressCostImmediate. OFF artifacts preserved in scratch `i5-target-cost-image/default-off/`: HEX4d9aa84cf40ab51e0f1354d60104440fa23e5e1dcd21430b3b9b26c65418a9bd, ELFa7846154ac6adcecb7ef6a5910fabdbc7093a71e0dd45aaf868942ee15b9d51f, NOTuploaded.
- One focused independent review6c8b120d-b5b8-44f5-a8ae-9f35cdd320ae found a partially delivered twelfth serial row was parsed prematurely. New chunked-delivery regression first failed at invalid_ingress_cost_contract; collector now ignores only trailing unterminated bytes and waits within original deadline. A second test preserves immediate refusal of malformed COMPLETE rows. No broad review restart.
- Final host command `PYTHONPATH=scripts .venv/bin/python -m unittest scripts.test_ingress_cost scripts.test_pit_motion scripts.test_pit_clock scripts.test_hitl_live_observation scripts.test_midi_clock`:112/112 PASS including36newscenario cases. First combined run exposed stale source-string wiring assertion requiring old last callback; updated to assert existing callbacks plus actual gated ingress callback/null branch, not weakened runtime framing. Native ring/ON+OFF compiler checks independently cover dispatch behavior.
- Final diff hygiene and strict OpenSpec validation PASS at2026-10-15T15:33:57Z. No target elapsed measurements, upload/device/SD/Git action yet. Source concurrency/completeIRQ/finiteowner/calibration/wirearrival/readiness remain open. Next fresh managed preflight, exact reviewed ON image preservation/no-build upload, smoke/full Clock-only observations and canonical shutdown; restore retained OFF image as part of evaluation after successful shutdown.

## Managed device evidence / closeout (2026-10-15)

Existing registered `ingress_cost` / `STIM-INGRESS-COST-TEMP-01` entry point ran with
`HITL_MANAGED_RUN_TIMEOUT_S=360`, default capture environment, track8/slot4/channel8
metadata and `--no-boot-reset`. Those metadata fields did not cause slot gestures.
Only stopped Clock traffic120/300BPM and read-only queries; no Start/Record/edit/clear/
Save/Load/SD-cleanup gestures. Canonical shutdown sent two Stops and verified drains.
Automatic boot/transport persistence occurred; no SD byte-immutability claim.

- Fresh managed preflight `captures/i5-ingress-cost-preflight-01`: stopped track7/slot3
  zero-based, closed overlay, physical Ready/era2/guard15; shutdown complete/no latch.
- Final ON build SUCCESS75.200s, identical RAM1variables96192/code391304/padding1912/
  locals34880, unchanged floor840/margin1072. Expected measured wrappers/snapshot/emitter
  verified. Before upload retained HEX+ELF under `captures/i5-ingress-cost-smoke-01/image/upload/`.
  Evaluated HEX `61a912811bad1fe8cc2a70f5964996dc50ebd011af28cd43f847daa710f26603`;
  ELF `cf0ef724624229995d41f00ef6c4dc43c665c00aa211ebd5c1b6be7bb1994f73`.
  ON no-build upload SUCCESS9.575s; both cache artifacts byte-identical after upload,
  and still identical before restoration. Smoke/full metadata pins this HEX (not device attestation).
- `smoke-01` refused `fresh_stopped_status_missing` during boot/load work, before
  Clock pumping or cost snapshots. Retained, not counted as a measurement pass.
- `smoke-02` PASS: two2s Clock-only legs;8complete12row snapshots/6fresh physical guards.
  USB hook deltas96/238; host sends96/240. Exact1:1:3:1 lane ratios; zero new rejections.
- `full-01` PASS: two5s Clock-only legs;8complete12row snapshots/6fresh physical guards.
  All rows CPU600MHz/guard15/contract7/nonsaturated; all guards Ready/era2/stopped.
  USB hook deltas239/598, host sends239/599. Counts are packet hooks, not Clock counts;
  unequal send/hook counts do not establish loss or receiver completion correspondence.
  DIN and USB-host lanes stayed empty: NOT RUN, no physical fixture qualification.

Full-run per-leg USB lane `(count, sumCycles, meanCycles)` in lane order:
-120BPM: hook(239,6560375,27449.268), mask(239,2557582,10701.180),
  publication(717,4190320,5844.240), empty(239,1032429,4319.787).
-300BPM: hook(598,15436750,25813.963), mask(598,5553629,9287.005),
  publication(1794,10033056,5592.562), empty(598,2533912,4237.311).

Final full-run LIFETIME USB min..max cycles: hook21699..33039,
mask5895..15983, publication3975..12293, empty3812..14140. These include prior smoke/
shutdown observations in the same boot; they are not leg extrema. Lifetime accepted
counts1176/1176/3528/1176, rejected1/1/3/1 already present in baseline, unchanged
through measured legs. No rejection reason/timing attribution beyond this evidence.
No overhead subtraction: hook includes inner instrumentation, conservative mask is
not exclusive IRQ-off, publication self-accounting tail remains unmeasured.
Host send intervals full120BPM5.362250..36.089166ms/median20.7086875;
300BPM0.186833..15.524708ms/median8.1717295. Catch-up/host observations, not wire pacing.

### Exact restoration / safety / durable evidence

After full-run canonical shutdown, copied retained OFF HEX+ELF from
`captures/i5-ingress-cost-smoke-01/image/restore/` to cache and uploaded with gate unset,
`-t nobuild -t upload` SUCCESS9.896s. Both hashes unchanged afterward:
HEX `4d9aa84cf40ab51e0f1354d60104440fa23e5e1dcd21430b3b9b26c65418a9bd`,
ELF `a7846154ac6adcecb7ef6a5910fabdbc7093a71e0dd45aaf868942ee15b9d51f`.
The previously verified OFF ELF has no cost-wrapper/snapshot/emitter symbols.
First restored preflight `restored-01` correctly refused guard11 (storage work),
then completed both drains. Fresh `restored-02` PASS: stopped/closed overlay/guard15/
Readyera2, two drains/postflight complete/no latch. Measurement image is NOT left installed.
All five post-upload run shutdown reports complete with2drains; latch absent after final check.

Reports, metadata, invocations, shutdowns, restoration preflight, upload logs and exact
hash manifest retained in [I5 — target device artifacts](target-ingress-cost/SHA256.json).
Raw session logs and both exact images remain under named captures; the manifest
hashes retained reports, not an assertion that cached firmware attests the running device.
Parent29/60/rollingOFF/targetcontinuationdisabled/receiptsdefaultOFF unchanged; no Git action.

Session closeout: existing-owner diagnostic implementation under DEC-125 — pinned receiver
ingress receipts and DEC-128 — ClockManager PIT elapsed reference; no new owner/resource/
decision or changed exclusion, so no new DEC entry. Runtime/design/tasks/guide/tracking/
continuity and owner disposition updated. Native3228passed7skipped121suites, host112,
ASan8, ON+OFF builds and the one repaired focused review remain applicable: no code
changed during hardware evaluation. Finite full receiver/IRQ/owner bounds, calibration,
wire arrival, unseen continuity, source ISR/main synchronization and running request/
anchor/coalescing/adoption/stop remain open. No740→768 readiness or WCET claim.

Final closeout checks at2026-10-15T15:49:18Z: strict OpenSpec validation PASS,
`git diff --check` PASS,22retained evidence-file hashes verified,9target evidence links
resolved,29checked parent tasks unchanged. Restored cache HEX+ELF hashes exact and ARMnm
has no measured wrappers/snapshotCosts/ingressCostImmediate; latch absent. Live receipt
source SHA256 remains abd2b640b74f86c2c4473f12db24ada9ed43eb31f293d00ced50bc9467be745a.
