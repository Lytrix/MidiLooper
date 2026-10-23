# I5 — native qualification input continuation mechanics

Status: native qualification mechanics implemented and verified; default target behavior unchanged. User continued the qualification-gated continuation implementation after [real-input oracle delivery](REAL-INPUT-EXECUTION.md). **Native qualification only.** No target enablement, physical timing guarantee, upload/device/SD/Git or parent checkoff.

## G0/G1 and historical decision review

Real MidiHandler/ClockManager/TrackManager/Track/reader fixture links; all three consuming polls fail Expected736Was768 under the native continuation oracle. Ordinary controls/fullnative3110pass7skip115suites are pinned. Searched DECISION_LOG for DEC-116 — one rolling path, input FIFO and available DEC numbering; reviewed active timing draft/reassessment and readiness rules. Reuse YES: existing MidiHandler routing/callbacks/setup, existing hardware port reads, actual dispatch-order semantics, existing non-aborting cold allocator pattern. No domain owner transfer or SD/undo change. DEC-122 — native-gated MIDI input continuation records the new retained role/transition boundary, not target timing approval.

## G2 — bounded contract

Owner: MidiHandler only for one fixed retained route batch; ClockManager still handles every dispatched clock and Track remains publisher. Primary files: MidiHandler.cpp/.h, new fixed-state helper, existing real-input native fixture. No main.cpp change.

Preconditions: `MIDI_INPUT_CONTINUATION_QUALIFICATION` is native-only; UNIFIED_INPUT_CONTINUATION_ORACLE selects it in the integration TU. Cold setup provisions one non-aborting fixed state block before polls; absent state falls back to legacy input without claiming qualification ready. Target use of the flag is a compile-time refusal until its independent physical/main integration contract exists. Default target and ordinary native suites retain legacy input.

Mutation: acquire at most one nonempty route batch per poll, cap frozen128/32, same serial route cycle. Copy four-byte messages and read timestamps; USB/DIN two-pass transport-first, host FIFO. Host callbacks capture into the owned batch only during acquisition, using existing encoding/routing otherwise. One poll scans at most16 stored row types and dispatches at most one message. Empty scan segments are legitimate. No full-prefix rescanning, input/clock-edge build, per-poll allocation or extra publisher.

Postconditions: copied messages retained until real handler executes exactly once, then consumption acknowledged. Start/Stop/Continue/epoch changes do not clear the remaining batch. Read-only pending/count/generation observations do not advance input. Reentrant polling while acquisition/dispatch is executing returns waiting with state intact; outer owner changes that condition. Host acquisition capacity violations fail loudly in native qualification, never truncate accepted callbacks. Cold setup is the explicit fresh-session reset, not a runtime cancellation command. No new physical deadline receipt is fabricated.

Preserve: native cold predecessor current/token/reader/ledger and all actual clock semantics; unchanged cross-route ordering and host callback PitchBend conversion; current/ready and source ownership; defaultOFF/compatibility emitters and all original test registrations.

Non-goals: playback window preparation/service arbitration, source-progress mutation receipt, qualified initial epoch1 publication, physical clock/input arrival or WCET, target buffer placement/activation, adding a global scheduler. The former full timing draft remains conditional.

## Fixture-first oracle and interleavings

Existing three real-input expected REDs are the pre-fix behavioral evidence. After mutation, consumingOff polls return736 and four queued clocks drain to768 exactly once without new messages; shared transport-first/FIFO/cap/boundary controls remain green. Add direct pending/generation observations, multiple input arrivals while old batch owns rows, epoch/transport changes within a retained batch, setup idempotent cold reuse, and generation/capacity fault tests proportionate to this role. Do not impose hardware-inbox ownership on copied rows.

Keep ordinary batch characterizations in unqualified mode only; they intentionally require complete batch advance in one call. Native hardware/time mocks only; Track input/clock/source/reader stay real. Native model is not substituted for production handler.

## Verification recipe

`PLATFORMIO_BUILD_FLAGS='-DUNIFIED_INPUT_CONTINUATION_ORACLE' pio test -e native -f test_midi_input_clock_integration` must turn all previously expected failures GREEN with registered controls present. Ordinary new suite and neighbours/fullnative must stay zero-failure. Controls that suppress a retained row or dispatch/callback ordering must fail independent content/tick assertions, then restore byte-identically. Actual state/header ABI price; default `pio run -e teensy41-capture-serial`/RAM1 no-rebase; strict spec/diff/link checks. One focused independent implementation/oracle review, fix/retest concrete findings only. No HITL or upload in this slice because the flag cannot activate on target.

## Completion

Delivered `Utils/MidiInputContinuation.h` fixed working state and MidiHandler native-gated acquisition/resume/host-capture path. Cold non-aborting prewarm; one retained route batch; frozen128/32cap; USB/DIN transport-first, hostFIFO;16type inspections/atmostone real dispatch per qualified poll; acknowledge after real handler. No main/ClockManager/Track/source/window policy changes. Native observation declarations are consistent across linked native TUs; disabled mode reports no state. Native allocator fault setter affects provisioning only.

### Verified production outcomes

- `PLATFORMIO_BUILD_FLAGS='-DUNIFIED_INPUT_CONTINUATION_ORACLE' pio test -e native -f test_midi_input_clock_integration`: **15/15 succeeded**, exit0. All three predecessor consumingOff return assertions are now GREEN at736. Four copiedClocks pending, pure pending/generation/storage queries preserve736, then bounded drains reach768 once with exact704On/736Off/pairedID/oldcurrenttoken/storage/quietreader/ledger preserved. Native cold publication remains the explicit limited predecessor prerequisite, not runningepoch initial preparation.
- New lifecycle cases: newer DINOff cannot overtake retained USBClocks; realStop changes transport but retains copiedClock; explicit clock rebase changes epoch while the copied row survives and is acknowledged without stopped transport advancement; cold setup reuses storage without recycling generation. Helper tests reject active-batch growth/rebegin, enforce16row scan and frozen33row fixturecap, and make generation exhaustion terminalzero.
- Ordinary unqualified suite **13/13 passed**, including unchanged batch behavior and disabled observations showing no allocated continuation. Isolated `I5_INPUT_PREWARM_REFUSAL_PROFILE` with qualification: **1/1 passed**. Denied cold provisioning retains real legacy On0/Clock8/Off8 acceptance without claiming ready; later cold provisioning succeeds and the next qualifiedClock reaches16 with accepted content intact. Profile is an allocator-boundary stimulus, not a normal suite count or target policy.
- Final shared neighbours **339/339 passed**. Full `pio test -e native`: **3120total /3113succeeded /7skipped /0failed /115PASSED suites**,287.506s. Qualification profile is separately run, not silently excluded from its own acceptance.
- Default `pio run -e teensy41-capture-serial`: **SUCCESS**,77.267s; RAM1 variables96192/code392216/padding1000/locals34880, unchanged floor840/margin160. No rebase. Default ELF contains no pollQualifiedInput/qualifiedInput/qualificationInput symbols. Target compiler explicitly rejects `MIDI_INPUT_CONTINUATION_QUALIFICATION=1` with the native-only diagnostic. No image upload or installed-image claim.
- Actual helper sizeof **1056B/alignment8 on target and host** from real header compiled by ARM Cortex-M7 hard-float and native printers. Native allocation is one1056B state for the global input role; native pointer8B and allocator-fault bool1B are separate scalar payload, excluding allocator/link alignment. No new fields on Track/Loop/runtime/MidiHandler; their size pins unchanged. The target ABI price is not target residency: qualifier state cannot activate there. Prior1064+16B timing-draft record is not the implemented state; no maincheckpoint/physicaldeadline state was added.
- One focused independent implementation/oracle review found no concrete reachable defect within the named native-only slice. Reviewer did not execute tests/builds or approve target timing; no repeat review.

### Discriminating controls and restored state

Host-capture suppression in real MidiHandler reproduces **only hostContinuation Expected736Was768**:16reportedtotal/14succeeded/1failed+harnessERRORED/exit1. Enlarging helper scan quantum16→128 fails **only helperBoundedScanAndFrozenCapacity ExpectedFALSEWasTRUE**, same counts. Both restored byte-identically with cmp: source SHA256ad9818874087eb5947b356ca20234afe94587bfef70c8f9e10d3f47d9d9decfe; helper SHA256a26b4f6519a3d412829b7d556e25e9a4657fa696ea61c2688398cc7b8cca3048. Finalqualification15/15 and neighbours339/339 reran after restoration; default build/fullnative applicability unchanged because restored files exactly match their tested versions. No production fault control left enabled.

### Resolved implementation/fixture issues

First compile helper referenced host members as globals; passing the owning MidiHandler and host interfaces fixes it, not a fake callback. New Stop test incorrectly assumed Stop itself rebases the epoch; production Stop semantics remain unchanged, and the final case explicitly rebases while a Clock is retained. Several line-edit misanchors were corrected from live content before their tests; no malformed edit/build was counted as behavioral evidence. Append mutations execute outside assert, so NDEBUG does not erase message retention. Acquisition/read/library/handler costs are separately unqualified;16scan does not bound a whole poll's WCET.

### Closeout and remaining gates

DEC-122 — native-gated MIDI input continuation, runtime/task/delivery/handoff and qualification behavior docs updated. All parent checkboxes remain open. No target polling/clock delivery changed; default rollingOFF/legacy emitters remain. No physical timestamp/reference, main-owned checkpoint arbitration, source-progress receipt/delta builder, initialepoch1 publication, full deadline bound or hardware parity claim. Next dependency-closed slice must fix target-capable checkpoint/lifetime/physical-reference integration before a measurement image/HITL; this native gate cannot be used to flash a premature timing claim.

Logs in session scratch: i5-continuation-first/linked (initial compile/firstgreen); i5-continuation-qualified/disabled/refusal; i5-continuation-native/default-build; i5-continuation-price-arm/native; i5-continuation-target-refusal; i5-continuation-host-removal-control/scan-control; i5-continuation-qualification-final/neighbours-final. No commit/push/PR/archive, SD mutation, device action or upload.

Finalcloseout: strict OpenSpec valid/local links resolve/diff hygiene clean/progress29/60 unchanged. The obsolete expectedRED failure-label in the now-passing input assertion was changed diagnostically only; qualification15/15 reran afterward. Fullnative/build evidence remains scoped to byte-identically restored implementation files; no threshold, expectation or registration weakened. Unrelated pre-existing dirty main.cpp and other user work retained; this slice does not implement main arbitration or alter ClockManager.
