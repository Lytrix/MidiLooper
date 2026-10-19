# Continuity evidence handoff — task 6.8

**I5 — current attempt4 handoff (2026-10-16): PARTIAL.** [I5 — latest evidence and held Stop blocker](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-HITL-EXECUTION.md#managed-close-repair-and-approved-attempt4-2026-10-16). Managed close safety repaired/94host tests and successful device teardown; initial-deadline919e17e6… now device-evaluated with correct metadata. Growth+empty legs and independent live held no-retrigger/content schedule verified. Held frozen Stop/strict idle failure and matching physical Stop remain open; no full continuity/timing/parentcheckoff. Fullnative3284pass7skip121suites; exact default restored/freshpostflightguard15/bothheaps/two drains/deferredclose/nolatch. Prior host-blocked/NOTuploaded status below is historical.

**I5 — latest approved-loop handoff (2026-10-16): BLOCKED.** [I5 — current execution and stop boundary](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-HITL-EXECUTION.md#approved-loop-closeout-and-next-prerequisite). Three correct-metadata managed attempts exposed initial request deadline0 after publication-before-first-reader; exact production RED→GREEN correction passes507focused/3283native+7skip/23host/builds/one review, retained919e17e6… candidate NOTuploaded. Diagnostic query schema43 is fixed metadata only. Full03 host close hung/no own shutdown report; separate non-reset recovery2drains/no latch, exact default restored/freshpostflight02PASSguard15/bothheaps/two drains. Stop stimulus until teardown diagnosed/bounded. Empty/held/matching Stop and full device/continuity/deadline acceptance remain open;29/60/parents unchecked. Earlier approval-pause/candidate statements below are historical.

**I5 — actual rolling HITL handoff (2026-10-16): PARTIAL.** [I5 — execution, image/provenance pins and remaining tests](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-HITL-EXECUTION.md). Growth leg verifies actual ordered wire/6-row accepted content/NoteIds/window-period/source bindings and corrected frozen Stop; empty reused take stalls, held/matching Stop not device-verified. Real Visual cancellation fixed; idle-shell correction latestnative3281pass7skip/ASan10/buildPASS candidate NOTuploaded. Original stale metadata preserved with upload-chain sidecars and resolver fixed locally. Retry continuation requires approval; exact pretest OFF restored, postflightSTOPPED/guard15/heaps/two drains/nolatch. No full continuity/deadline/defaultactivation or parent checkoff.

**Current extension (2026-10-15): I5 — subsequent live Record coverage** supersedes the first-bar-only limitation below: growing accepted-extent periods hand off at complete full wraps with explicit playback origin; owed later intervals, empty coverage, held identities/no retrigger, late recovery, nonzero origin and subinterval Stop/frozen correction pass through the same owner/reader. [I5 — current verification/gates](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-COVERAGE.md). Fullnative3275pass7skip/focused500/ASan8/three controls/default+enabled buildsPASS/padding1160/952≥840, ABI unchanged. Total raw inspection and actual target return/IRQ/arrival/dense-device gates remain open; no hardware/upload/defaultactivation/continuity or parent checkoff.

**Current increment (2026-10-15): I5 — connected first-bar rolling Record** is functionally verified through real nonzero-era input/ClockManager and actual rolling reader/ledger, including matching Stop and existing mismatching/root-lifetime controls. [I5 — current evidence and remaining gates](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-VERTICAL.md). Fullnative3267pass7skip/ASan5/two controls/default+enabled builds pass; live interval initial[0,768)/period768 only, runtime+64external/Track unchanged. Growing live coverage and target input-return/whole-owner/IRQ/arrival qualification remain open; hardware NOT RUN/no upload. No continuity/all-route/default activation/parent checkoff follows. Earlier missing-running-request statements are historical for this first-bar increment.

What this change hands to the runtime change's continuity acceptance, and what it explicitly does **not** claim. The
authority for every end-to-end threshold lives in the runtime change, not here:
[`runtime-working-set-ownership/specs/…/spec.md`](../runtime-working-set-ownership/specs/runtime-working-set-ownership/spec.md)
— *Continuous runtime operation has one end-to-end acceptance contract* — with **S7.5a — envelope/timing oracle**
([tasks.md § S7.5a](../runtime-working-set-ownership/tasks.md)) **upstream**, and **S7.5b — continuity dependency
closure** and **S7.5 — combined continuity acceptance** downstream

([`design.md`'s proof order](../runtime-working-set-ownership/design.md): *"S7.5a — envelope/timing oracle fixes
justified limits before qualification; the sibling budgets and verifies its work against them"*). S7.5b names **this
task (6.8)** among the evidence it collects.

## 1. No local thresholds

This document states **no** end-to-end timing, gap, capacity or residency limit of its own. Every such number belongs to
S7.5a, whose own entry requires limits to be *"derive[d] before qualifying runs, never fit … after observing a run"*, and
requires unsupported combinations to be stated rather than silently shrunk. The2026-10-05 state had no numeric limits;
**current correction2026-10-11:** DEC-117 — continuity envelope records the choices in
[`DECLARED-LIMITS.md`](../runtime-working-set-ownership/evidence/2026-10-11-s75a-continuity-envelope/DECLARED-LIMITS.md).
S7.5a — envelope/timing oracle remains open because required measurements, resource allocations and qualification
are not complete. This change contributes methods/fixtures/rows, not a competing set of numbers or a qualification claim.

## 2. What must be allocated against the shared budget before the affected paths are enabled

Task 6.8 asks for the window/index/retention/page/closure work to be *allocated and verified against that shared service
and memory budget* before the affected production paths are enabled. The honest state:

| Work | Allocation against S7.5a's budget | Why |
|---|---|---|
| window build/publication (the lane's per-turn work) | **not yet allocated/qualified** | shared numerical choices are recorded, but the lane's cost/bound and resource allocation still need evidence; native sweeps measure `drainTurns = H × (1 + K) × serviceTurns` and density/span/boundary cases, while one declared grain is not yet a device timing guarantee |
| index over the selected snapshot | **not yet allocated** | task 3.6's interval index is unfinished, so there is nothing to charge |
| retention (pinned source) | **not yet allocated** | 3.6a's retained-source figure is recorded but not budgeted against S7.5a |
| paging (the legacy paged path) | **partially** | 5.5's envelope statement and the `S312b` paged-path work bound it within its declared envelope; the shared-budget allocation is still owed |
| closure reconciliation | **partially** | 4.5's fault-injection results are kept **separate** here, as task 6.8 requires |

**The enablement consequence, stated plainly:** this change's rolling path is enabled **only** in the dedicated
qualification env `teensy41-capture-serial-window-qualification` (the shape the firmware's own guard requires), and
**not** in the shared default. Nothing is enabled in normal operation, and the allocation above must exist before anything
else is.

## 3. Evidence this change supplies now

**2026-10-13 — I5 — ingress correspondence prerequisite:** [I5 — ingress execution](evidence/2026-10-13-i5-running-capture-service/INGRESS-EXECUTION.md) / DEC-125 — pinned receiver ingress receipts delivers defaultOFF project-owned generated transport hooks/ordinary/native-retained metadata copies with actual receiver/parser/full-dispatch native controls. Fullnative3154pass7skip118suites/default+ingressSUCCESS; State19168target19192host/continuation5152B and+4096B ordinary batch stack payload explicitly priced, no shared-budget allocation inferred. Installed sources unchanged, numerical RAM1 budget/pins unchanged. No independent producer/era/arrivaldelay/correlation/wholecost/inputhealth/device qualification or740→768 readiness. No timing limit, rolling/default/targetcontinuation activation or parent checkoff; correspondence must not be counted as physical continuity qualification.


**Native transition evidence (controlled ticks, explicit expected MIDI, no sleeps, no copied state machines).** The ten
interleavings task 6.5 inventories are forced on the real request→worker→publication→consumption path; the two that were
gaps are implemented in
[`s65-native-verification-split`](evidence/2026-10-05-s65-native-verification-split/EVIDENCE.md) — **A→B→A view-slot
reuse with the first completion arriving last**, and **cancellation between every preparation grain** — alongside the
admission-boundary fixtures (`s310z`), the `H × K` contention sweep (`s310x`) and the activation audit (`s310y`). Those
are the transition evidence S7.5's *"native forces production-path interleavings"* clause asks for, and they are red-proved
by named mutations.

**Hardware grain/service-gap and input/output timing evidence — partial, and named as partial.** The authorized device
runs (`s64-s65-device-leg-01`, `f14-overrun-tier`) measured, on the qualification image:

* **service under transport**: 50 `win_gap` blocks with `running=1` and a rising playhead phase — the lane *is* dispatched
  while a transport runs — every block at the declared `permitted=64` with **`overruns=0`**;
* **`eligible=0` and `progress=0` in every block**, i.e. **no window work was owed**, so **no** grain duration, **no**
  service gap under load, and **no** receipt or publication was measured. The lane was observed idle-but-served, not
  loaded;
* the leg's target shape was exercised (a 24-bar take, `length=18432`) with the legacy merged-window path active
  (4 996 `playback_build` rows).

**Not measured on hardware, and therefore not offered as evidence:** grain duration and maximum service gaps under load,
sustained multi-track playback with capture/persistence contention, external-clock behaviour and clock transitions,
memory high-water marks with boundary content parity, and route-specific physical MIDI output timing. Task **6.5**'s
hardware half is **NOT RUN** in that sense, and the reason is measured rather than assumed: the leg's gestures owed no
window demand (the next step is a leg that drives a demoted slot switch), and the lane's coverage/overrun rows needed the
F-14 classification and the post-stop drain before they could be read at all.

**The instrument that read was missing (2026-10-05, `rolling-gate-row`) — it changes what the *next* read will be, not
what has been measured.** The reason the hardware half could not be read is now itself addressable: the lane's **demand**
path (`Track::serviceRollingPlaybackWindow`, the off-clock turn `src/main.cpp` drives) returned a typed `RollingOutcome`
to its caller with **no row**, so "owed nothing" and "refused, and by which term" were the same absence (F-16). The new
qualification-only `DIAG,win_gate` row names that exit, the active slot, the inputs that decided it (`ep`/`av`/`al` tell
apart the entry decision's clock stamp, demotion bound and span alignment; `rq` carries the request's ordinal) and is
emitted once per state change. It observes only, is out of every non-qualification image, and changes no
`RollingOutcome`, guard order or return value. **Not measured by it:** everything listed in the paragraph above — a row
is not a duration. It exists so that the next authorized run *measures* rather than infers which term refuses the lane's
demand.

**And it was then run on the device (2026-10-05, authorized): the inference is over, and the answer is not a lane defect.**
`ep=1` in every row (the deadline clock *is* stamped), and the demand dies at its **entry** because the leg's committed
loop is not a whole number of two-bar spans (`len=19200`, `al=0`) while it is demoted — so **this shape gives the lane no
demand at all**, which is why the hardware half has nothing to measure yet. Where the loop *is* aligned the second gate is
named (`x=win_not_servable`, `rt=0`). The device also corrected the row itself (its change key alternated forever through
two observation bits: 809 → 57 rows). The registered verifier, replayed over the run, says the same thing in its own
terms: `window_demand_observed` false, coverage and overrun checks true, receipts 0, `build_complete` 0. **Nothing in
§3's "not measured" list changes** — the lane was never given a demand — and the next leg shape needs a span-aligned
committed length (a leg-shape decision), with the root-currency question still the owner decision on record. See
[`rolling-gate-row/DEVICE-RUN.md`](evidence/2026-10-05-rolling-gate-row/DEVICE-RUN.md).

**And the one controlled switch into an even-length target put the remaining blocker one step earlier in the chain.** With
the target verified by the device's own rows as **24 bars (span-aligned), committed, demoted, inactive and rootless**, the
trace is: switch pending → launch preparation (its republish request **accepted**) → **source publication FAILED** →
never reached: initial view ready/bound, rolling request, preparation, adoption. The failure is not a refusal: the
`Snapshot` owner selects the target in 135 of 136 rows with `p=1, sep=1, elig=1`, and **no publication unit is serviced
anywhere in the 22 s the switch is pending** (the units begin only after the transport stops), after which the switch
reports its bound and stays pending. So the chain's blocker is *serviced publication work under transport*, which is a
different question from the restore-versus-rootless one and is **not** an S7.5 input until the lane is given a demand.
See [`switch-trace/TRACE.md`](evidence/2026-10-05-switch-trace/TRACE.md).

## 4. The assertions S7.5 will judge, and how this change relates to them

S7.5 requires, under sustained simultaneous playback / record-overdub / undo-redo / switching with
persistence-display-load contention: **zero** unintended note-off or retrigger, **zero** capture loss, **zero** MIDI-content
refusal, **zero** MIDI-deadline violation, **zero** coverage fault and **zero** involuntary stop; and a **one-pass-late
edit is measured as audible-action latency, never hidden as immediate execution**.

This change's contribution to each, in its own words:

* **coverage fault** — the lane's own names are `one_pass_late`, `lag_breach` and the *named* `win_gap_overrun`; the
  native fixtures assert the first two are **named rather than silent**, and F-14 (2026-10-05) made the **overrun** row
  survive the timing-critical shed path, which is a prerequisite for S7.5 being able to see a coverage fault under
  transport at all;
* **one-pass-late as latency, never hidden execution** — this is the change's own lag rule (3.6b): the previous revision
  stays authoritative for exactly one traversal, the serving window is byte-for-byte unchanged, no wait and no defer, and
  the second consecutive miss is reported **once** by name. The handoff's contribution is that the rule exists and is
  red-proved natively; whether a given pass is *audible* inside S7.5a's limits is S7.5a's number, not this document's;
* **unintended note interruption / involuntary stop** — the native closure fixtures (4.5) and the stop/switch fixtures
  assert the change's own contribution; their fault-injection results are kept **separate** here, as task 6.8 requires;
* **capacity refusal inside the envelope** — the boundary pair (`s310z`, `s310x`) names the refusal and the retained
  obligation; whether a given density is *in* the envelope is S7.5a's declaration to make.

## 5. Routing

* **Upstream of this handoff:** S7.5a's envelope/timing oracle. Until its numeric limits exist, this change cannot
  allocate against them and **says so** instead of inventing local thresholds.
* **Downstream consumers of this handoff:** **S7.5b** (dependency closure — it collects this task explicitly) and
  **S7.5** (combined continuity acceptance). Neither is a prerequisite for producing this handoff, and **passing this
  subsystem does not certify the whole runtime**.
* **Kept separate:** task 4.5's fault-injection/recovery results, and the safety policies they exercise.

## I5 — serialized live-source retention continuation (2026-10-13)

DEC-127 — semantic live-source retention locally verifies Loop phase/projection/raw
descriptor-at-creation currency, forward live fill, both-builder retained borrow,
release refusal and cancellation before selection/source destruction. Focused418,
fullnative3178passed7skipped119suites/defaultcaptureSUCCESS/padding1720≥840;
controls3/2/1RED restored and one focused review under retained/serialized preconditions.
[I5 — live-source integration evidence](evidence/2026-10-13-i5-running-capture-service/LIVE-SOURCE-INTEGRATION.md).
Internal timer ISR can transition/reset Track; ISR/main read/cancel/adopt synchronization,
running request/anchor/coalescing/adoption/stop, sender-delay/correlation and complete
owner-to-input bounds remain unqualified. Proposed ClockManager two-channel PIT
resource/shared-module/debug-freeze policy needs owner decision before target producer.
No740→768 readiness/shared-budget qualification/parentcheckoff/targetactivation or
device/upload/HITL/SD/Git action;29/60/defaultrollingOFF/legacy retained.

## I5 — ClockManager elapsed-reference continuation (2026-10-13)

DEC-128 — ClockManager PIT elapsed reference resolves the approved two-channel/shared
policy choice with coldPIT0/1/stockFRZ1/Runmodeonly/fixed masked lifetime reads, terminal
era/configuration/overflow/backwards refusal and real MIDI timer-start qualification.
Optional ingress sampler carries receiver observations, not sender arrival or deadlines.
[I5 — physical producer execution](evidence/2026-10-13-i5-running-capture-service/PHYSICAL-PRODUCER-EXECUTION.md)
records ordinary24/ingress28/combinedcontinuation30/fullnative3188passed7skipped119suites,
default+ingressSUCCESS/padding1688-1912≥840/state16BRAM2/linked+32B, controls1/1/1RED restored
and one focused review. Hardware behavior/accuracy/latency/errata/correlation/delay/pacing,
complete owner-input/IRQ bounds and shared sourceISR/main protocol remain unqualified.
No shared-budget/740→768 readiness/parentcheckoff/checkpointactivation/upload/HITL/device/
SD/Git claim;29/60/defaultrollingOFF/targetcontinuationdisabled/receiptsdefaultOFF.
Next is a bounded observable hardware qualification recipe and separately authorized upload.

## I5 — stopped-only PIT hardware qualification (2026-10-14)

[I5 — hardware evidence](evidence/2026-10-13-i5-running-capture-service/PHYSICAL-HARDWARE-QUALIFICATION.md)
records owner-approved request-only instrumentation/managed TEMP preset and DEC-129 —
Run-mode physical-clock debug admission (C_DEBUGEN diagnostic, halt/sleep refusal,
FRZ1/unsupported unseeninterruption continuity unchanged). Full01 PASS,211/211/210s,
Readyera2/natural lowcarry/MIDIch2/highTIFclear/2drains/postflightcomplete/nolatch.
Relativehostrate23.967714–24.058948MHz is predeclared gross sanity, not calibration.
Observed production read11.970–34.090µs/median29.555 is notWCET/owner-cost allocation.
Stopped tick0epoch1 point brackets7–22µs are not moving-clock/due-time/arrival-health
qualification. No parent29/60 checkoff/defaultactivation or envelopebudget assertion.
Movingcorrelation/arrivalage/externalpacing/wholeownerIRQcosts/errata/sourceISR-mainprotocol
and running request/adoption remain open. Uploaded9b50680a…b770c qualified; later cached
clean rebuildf3d02128…d9581 NOTuploaded, no byteidentity claim. Native3196pass7skip119suites/
focused84/ingressnative33/host102/defaultSUCCESS/padding1688≥840; one review defect fixed.

## I5 — moving PIT observations and remaining whole-cost gates (2026-10-14)

[I5 — moving qualification](evidence/2026-10-13-i5-running-capture-service/PHYSICAL-MOTION-QUALIFICATION.md): dedicated managed TEMP/full01 OBSERVED,51matchedReadyera2rows/15movingpoints/internal+USB-device120/300BPM; all3Starts require fresh all-track guard15. Stable per-leg epochs/increasing tick+physical count/read brackets9–31µs; internal residual−408..520.5µs and300BPMhostsend0.005291..23.401125ms are observations only (catch-up, not boundedpacing). No absolute calibration/wire-age/due-time/WCET contribution to shared budget. Exact retained/uploadedHEX96e4160a…d6be24/ELFc801239e…0c75bf;2drains/postflightcomplete/nolatch/no content or SDcleanup, ordinary autosave possible. Native3203pass7skip119suites/host105/defaultSUCCESS/padding1688unchanged/no budgetrebase. [I5 — complete owner/IRQ qualification](evidence/2026-10-13-i5-running-capture-service/OWNER-IRQ-QUALIFICATION.md) retains actual transitive/conditional/shared work and backend/IRQ finite-cost gates. No independent calibrated source, only host USB-device input; sourceISR/mainprotocol and running Capture request/anchor/coalescing/adoption/stop remain. No shared-budget allocation/parentcheckoff/740→768 readiness/defaultactivation/Git;29/60/rollingOFF/targetcontinuationdisabled/receiptsdefaultOFF.

## I5 — native complete-hook mask prerequisite (2026-10-14)

[I5 — ingress-mask qualification](evidence/2026-10-13-i5-running-capture-service/INGRESS-MASK-QUALIFICATION.md) adds native target-rejected observation seam, not a target timing instrument. Actualreceiver and realcounter fixtures assert savedmask0/1, nested ingress/counter/helper pairs, sampler packet/burst frequency, refusal/fault/pressure/FIFO preservation; original DIN unconditionalenable is observed separately. Controls13/10/3RED restored; one focusedreview2oracle gaps fixed/retested; fullnative3207pass7skip119suites/ingress45/receiver18/receipt9/ASan18/host30/defaultSUCCESS/padding1688unchanged. This is a controlled-fixture prerequisite for target hook/masked/receiver duration+overhead measurements, no numeric shared-budget contribution/WCET/arrival/calibration/sourceconcurrency proof. No upload/device/SD/Git/defaultactivation/740→768readiness/checkoff;29/60/rollingOFF/targetcontinuationdisabled/receiptsdefaultOFF.

## I5 — passive ingress-cost arithmetic increment (2026-10-14)

[I5 — cost arithmetic packet](evidence/2026-10-13-i5-running-capture-service/INGRESS-COST-QUALIFICATION.md)
adds no continuity timing threshold or firmware instrumentation. Caller-owned
validation/aggregation separates hook, mask-bracket and probe overhead samples;
full-wrap safety is conditional on enclosing extended-time brackets and an
established whole-interval counter continuity/rate bound. Those producer contracts
and target durations are not established by native arithmetic tests. No measured
WCET, whole receiver/IRQ bound, physical arrival/calibration, source concurrency,
740→768 readiness or parent checkoff follows;29/60/default activation unchanged.

## I5 — gated target ingress-cost observations complete (2026-10-15)

[I5 — target cost evidence](evidence/2026-10-13-i5-running-capture-service/TARGET-INGRESS-COST-EXECUTION.md)
supersedes passive-only next steps. Default-OFF explicit opt-in existing-owner probes/
384B diagnostic payload/strict12row command/registered managed TEMP stopped Clock-only
120/300BPM stimulus implemented. Smoke02/full01 PASS each8complete snapshots/6Readyera2
guard15; full hook deltas239/598/exact1:1:3:1/no new rejection/no saturation. Lifetime
instrumented hook21699..33039/mask5895..15983cycles, not exclusiveIRQ-off/fullIRQ/WCET.
Native3228passed7skipped121suites/host112/ASan8/ON+OFFbuilds/one review repaired; floor840
unchanged. Exact ON HEX61a91281…f26603/ELFcf0ef724…994f73 preserved/verified no-build upload;
OFF HEX4d9aa84c…18a9bd/ELFa7846154…b9d51f restored/verified. Restored02 stopped/idle/guard15/
two drains/postflight/no latch. Boot/storage preflight refusals retained, guards unchanged.
No Start/content/slot/SD-cleanup gestures; automatic persistence occurred. No new decision/
Git/parentcheckoff/defaultactivation;29/60/rollingOFF/targetcontinuationdisabled/receiptsOFF.
Whole-interval continuity remains an assumption. Fullreceiver/IRQ/finiteowner/calibration/
wirearrival/sourceISR-mainprotocol and running request-anchor-coalescing-adoption-stop
remain open; no740→768readiness. Do not repeat resolved PIT/native-mask/passive/probe work.

## I5 — builder-call cancellation boundary locally verified (2026-10-15)

DEC-130 — builder-call cancellation and live-source exclusion fixes reproduced lost
begin cancellation and in-call Fill null dereference through existing builders/runtime.
Executing calls retain state, refuse re-entry, resolve pending discard false/Denied;
runtime reset returns false/busy before destructive work and needs caller retry.
Fixed saved-mask descriptor/currency/raw-one-row reads, not whole-grain masking.
[I5 — source concurrency execution/evidence](evidence/2026-10-13-i5-running-capture-service/SOURCE-CONCURRENCY-EXECUTION.md)
records focused477/ASan9/original4case witness PASS, two negative mask-control failures,
one focused review approval, fullnative3237passed7skipped121passing suites, and final
defaultcaptureSUCCESS after repairing target-only unavailable CMSIS names with the
existing assembly pattern. RAM1padding1496≥840/variables96192/locals34880; no budget
relaxation. Target builder256/runtime7792 unchanged, host400/9976 (+8/+16).
No upload/device/SD/Git/defaultactivation/checkoff; parent29/60 unchanged. Standalone
Fill/direct candidate writers remain serialized; complete Track role/publication/
adoption and running request/anchor/coalescing/stop plus finite owner/IRQ/reference/
wirearrival bounds are the next unresolved boundaries, not another retention/token
prerequisite. Do not treat a false reset as completed or activate target borrowers.

## I5 — exact Track completion authority (2026-10-15)

DEC-131 — exact Track completion authority qualifies destination storage generations
and final Track role/storage/operation receipts, with short saved-mask commit/adoption.
Ready adoption consumes only ready; private completion requires explicit owner publication.
Main bridge worker preserves forward progress through explicit same-source publication.
Four baselineREDs fixed, six new cases/focused468/ASan6/wrongmask2RED, one focused review
approved and coverage gaps added/retested. Fullnative3243passed7skipped121suites and
defaultcaptureSUCCESS/padding1368≥840/margin528/no numerical budget relaxation.
Targetbuilder264/runtime7808 and host408/9992, +16 per external-first runtime.
No upload/device/SD/Git/defaultactivation/parentcheckoff;29/60 unchanged.
[I5 — Track publication evidence and next boundary](evidence/2026-10-13-i5-running-capture-service/TRACK-PUBLICATION-CONCURRENCY.md)
retains acquisition-before-builder-entry and direct Record release/start/bind/seek/projection
as open existing-owner work; no whole-method mask or accepted-Capture rejection to pass.
Running request/anchor/coalescing/stop and finite owner/IRQ/calibration/wirearrival remain
unqualified; no740→768 readiness claim.

## I5 — preparation acquisition/Record exclusion handoff (2026-10-15)

DEC-132 — preparation acquisition and Record exclusion locally verifies the existing
builder selection/commit and reverse destructive-reset/slot-lease transaction, early
Record admission/actual queued success, direct retained writers and required source
retirement. Qualified ready adoption remains independent and bridge metadata receipts
preserve exact completed worker/current storage. No new timing threshold or owner.
Focused479/fullnative3254passed7skipped121suites/ASan12/four scratch controls; one focused
review's two concrete defects fixed/retested, no repeat review. DefaultcaptureSUCCESS
41.065s/padding1160≥840; targetbuilder264/runtime7808/Track384/Loop1008 and host408/9992
unchanged, no sizepin/budget relaxation. [I5 — acquisition final evidence](evidence/2026-10-13-i5-running-capture-service/ACQUISITION-CONCURRENCY.md)
and [I5 — pending hardware recipe](evidence/2026-10-13-i5-running-capture-service/ACQUISITION-HITL-RECIPE.md)
name the required next Record/Stop compatibility HITL. HEXf48aca66…b0068/ELF7cd88398…a3563
NOTuploaded; no device/SD/Git/defaultactivation/checkoff,29/60 unchanged. Running request/
initial anchor/append coalescing/stop and finite owner/IRQ/calibration/wirearrival still
gate source-qualified740→768 readiness; this is not whole continuity acceptance.

## I5 — acquisition/Record compatibility hardware gate PASS (2026-10-15)

The prior handoff's required default-path hardware gate is complete: exact verified
default-OFF HEXf48aca66…b0068/ELF7cd88398…a3563 no-build upload, owner-approved scratch
track8/slots4–5, registered base8/4/channel8/hostUSB300BPM livePASS/overall_oktrue/cap_full/
issuesempty/critical-loss0. Fingerprinted replay02 passes header/must-prove withoutskips;
raw legacy replay limitation retained. Parallel native3254passed7skipped121suites. Final
managedSTOPPED/closedoverlay/guard15/two drains/nolatch, newdefault-OFF image installed,
scratchcontent retained. No firmware/SDcleanup/Git/defaultactivation/parentcheckoff.
[I5 — hardware evidence](evidence/2026-10-13-i5-running-capture-service/ACQUISITION-CONCURRENCY.md#current-session-hardware-compatibility-execution-2026-10-15)
retains early boot-storage refusal, advisory second-overdub lateness and absent deadline
safety proof. No full content-inventory/all-route/740→768/continuity acceptance claim;
running request/anchor/coalescing/stop and finite timing/arrival dependencies remain.
