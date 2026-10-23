# I5 — checkpoint arbitration and physical-deadline guard

Status: bounded passive building blocks implemented and locally verified. User requested continuation after native input mechanics. References [timing draft](TIMING-CONTRACT.md), [native continuation evidence](CONTINUATION-EXECUTION.md). No target continuation activation, physical reference producer, measured grain budget, main execution loop, source-progress implementation, device/SD/Git or parent checkoff.

## Contract and closure

Owner: existing main/deferred-owner policy, not a new scheduler. Add a caller-owned checkpoint cursor containing only the owed ordinary owner and a pending flag; normal selection reuses MidiServiceDrain::selectDeferredIdleOwner and preserves its Snapshot backlog/session-exit behavior. Insert at most one Window turn before a non-Window normal turn when a qualified urgent offer is permitted. The saved normal turn is mandatory next, even if urgency remains. Physical refusal consumes no normal cursor or owed-turn state. Cross-track round-robin and within-track earliest-slot selection are unchanged.

Physical guard is a passive reference record: independent-reference qualification, ClockManager epoch/anchor logical tick/adoption tick, fixed extended64bit physical anchor/lead and producer-era identity/correlation uncertainty. It is not a second clock/content authority or a live arrival observer. Missing/unqualified reference, missing established selected-owner grain/return cost, stale/zero logical epoch or backwards/passed logical position, stale/zero physical era, backwards/expired physical time, counter-end overflow and insufficient budget refuse by a local named outcome. No automatic zero margin or restamping from delayed Clock delivery. Full physical counter wrap cannot be represented by implicitly widening micros32. Grain+return+uncertainty must end strictly before the boundary; physically expired time refuses even if logical ticks remain behind.

Primary files: new Utils/PlaybackPhysicalDeadline.h, MidiServiceDrain.h policy extension, existing service-policy and real-input native suites. No src/main.cpp or ClockManager change; inline building blocks are not activated by merely compiling them. DEC-123 — checkpoint arbitration and deadline admission records the bounded state/decision boundary, no device timing value or readiness approval.

## Fixture-first transition oracle

1. Default reference/cost rejects extra work, preserves both normal cursor and any owed normal turn.
2. Qualified mock time/bounds permit one inserted Window then the saved normal; changing urgency or Snapshot booleans cannot overtake/replace that saved turn. A physical refusal between them leaves the normal owed until a fresh admissible reference is supplied.
3. Compare accepted ordinary-owner subsequence elementwise against the existing production selector across all start owners, Snapshot-extra/backlog/session-exit combinations and changing urgency. At most one extra per selected normal, each base cycle bounded in selection turns. This is a fairness count, not WCET.
4. Physical cases: exact boundary, insufficient remainder, large cost/margin sum, unavailable/zero cost, mismatched/zero epoch, backwards logical tick, passed logical deadline, physical lateness with lagging virtual tick, extended time crossing micros32 wrap, full-wrap expiry, backwards time, unknown/recycled physical era and extended-counter end/overflow. Fixed illustrative values test arithmetic boundaries, not adopted deployment budgets.
5. Real-input fixture uses actual ClockManager epoch/rebase and actual Track playbackWindowServiceOffer. Missing live running request remains a genuine non-offer; do not manufacture urgency or successful owner work to claim integration. Guard/selection observation is not proof of executed source/window readiness.

## Validation

Implement fixtures before helpers; existing headers compile/link seams may be included in the slice. Establish behavioral pre-fix evidence where existing selector permits urgency/deadline failures; missing new symbols are not behavioralRED. Focused policy and native-qualified real-input suites plus fullnative/defaultcapture/RAM1, exact header ABI pricing and one focused review. Removal controls must falsify oldest-owner/deadline observations and restore byte-identically. Strict spec/link/diff checks; no numerical service or physical producer claim.

## Readiness boundary

These blocks can close admission/selection mechanics only. Real target integration needs a provenance-qualified independent physical reference and enforced cost bounds, concrete grain execution/route/lifetime interleavings and admitted-workload proof; repeated input-budget refusal is a named service failure, not non-starvation qualification. Existing native continuation remains target-compile-rejected. No HITL in this slice and no hidden target timing change.

## Completion

Complete for passive guard/selection mechanics only. All parent tasks remain open; progress remains 29/60. No target timing/readiness qualification.

### Concrete refinement before review

Per-owner cost table and preview copies preserve cursor/debt on every refusal. A full-micros32-wrap regression was added against the initial32bit helper and failed ExpectedPhysicalDeadlinePassed WasAllowed (9othercasespassed); this exposed a real expiry-resurrection API defect. The final helper requires PhysicalClockStamp64bit microseconds+producer era, refuses era/backwards/counter-end invalidity, and the same fullwrap expectation passes. No new physical producer or deployment time bound was inferred from this change.

### Implementation and verification

`PlaybackPhysicalDeadline::admit` consumes a caller-qualified immutable reference and the selected owner's established indivisible grain plus return costs. `selectInputCheckpointOwner` previews copies, charges that owner, and commits both cursors only on Allowed. An inserted PlaybackViewMaintenance turn leaves the ordinary owner owed; changed urgency/backlog/session-exit flags cannot replace it. Refused previews report no insertion. Existing normal rotation, holder policy and target input behavior remain unchanged.

- Final focused command: `pio test -e native -f test_checkpoint_deadline -f test_midi_service_drain -f test_midi_input_clock_integration` — **69/69 passed** (13 checkpoint, 42 service, 14 ordinary input).
- `PLATFORMIO_BUILD_FLAGS='-DUNIFIED_INPUT_CONTINUATION_ORACLE' pio test -e native -f test_midi_input_clock_integration` — **16/16 passed**. Actual ClockManager epoch/rebase and actual Track live non-offer are observed; physical reference/costs are explicitly mocked. No executed owner/readiness or physical-clock health claim.
- Final `pio test -e native` — **3,134 total / 3,127 passed / 7 skipped / 0 failed**, all **116** executed suites PASSED, 109.286s. Pre-review run: 3,125 passed/7 skipped; two review-suggested tests account for the final increase.
- `pio run -e teensy41-capture-serial` — **SUCCESS**, 73.301s. RAM1 variables96,192/code392,216/padding1,000/locals34,880, unchanged; approved padding floor840/margin160. RAM2 variables163,360/EXTRAM11,680 unchanged. Helpers have no resident instances or new Track/Loop/runtime/MidiHandler fields. No image upload.
- One focused source review found no blocking defects. Its two non-blocking coverage omissions were closed: expensive/unestablished inserted Window cannot use cheap ordinary-owner cost; newly asserted Snapshot flags cannot replace saved LoadAndDisplay. Both registered and passing; implementation unchanged/no repeat review.

### Actual ABI price

Compiler object-symbol sizes on Cortex-M7 and native executable report the same size/alignment: PhysicalClockStamp16/8; PlaybackPhysicalDeadline48/8; PhysicalCheckpointCost12/4; InputCheckpointCursor2/1; InputCheckpointDecision4/1; CheckpointCostTable84/4. These are individual type prices, not a placed aggregate, stack peak, RAM1 embedding or timing budget. Scratch probe `i5-checkpoint-price.cpp` includes the actual headers; ARM flags reuse the prior default-target compiler command. No size pins rebased; draft checkpoint16B is not the actual two-byte owed-owner cursor.

### Removal controls and preservation

Disabling saved-normal enforcement caused **5 assertion failures / 6 passes**, plus harness ERRORED/exit1: missing selected-owner-cost refusal, replacement of Persistence by Window, ordinary-subsequence/fairness failure and invalid-debt safety. Disabling physical expiry and current-era validation caused **6 assertion failures / 5 passes**, plus harness ERRORED/exit1: exact/past deadlines, counter end, full-wrap resurrection and recycled-era acceptance. Both controls restored byte-identically before final checks. Initial full-wrap regression remains independent pre-fix RED evidence, not a missing-symbol compile failure.

SHA256 after restoration: MidiServiceDrain.h `c6c897fe21922b06b1134a3ae25c3b4a6b27add3c0a89be77b6f544ab0978243`; PlaybackPhysicalDeadline.h `cdd87efbe7027d831d70c16b6771ae6bbfdd57bc56543eb9edc33a1e358685b6`. Main/ClockManager match pre-slice hashes `a10aad16a6ef848b6b9cd0554e181d49f735bc63aeccc446cf2f77ba5607087a` / `db88bd1bc93bb5a496fedce7812c82d37c08cc23ef070f6cad4c77e759c93f0a`; pre-existing main changes preserved. No target execution wiring, physical-reference producer, bound measurement, target continuation activation, clock-delay policy, source-progress state, HITL/SD or Git delivery.

### Session closeout and remaining dependencies

DEC-123 — checkpoint arbitration and deadline admission plus its extended-time refinement already record the decision; verification changes no decision/exclusion. Runtime, active tasks/design, delivery tracking and latest iterative-plan handoff updated. Strict OpenSpec/link/diff checks recorded at final closeout. Parent 3.15a–f — one-reader production qualification, 3.10 — fair deadline service and 3.7b — legacy emitter retirement stay unchecked. Default rollingOFF/legacy emitters remain.

Next dependency-closed target design must supply provenance/correlation-qualified physical time (including era lifecycle and counter exhaustion), enforced per-owner indivisible grain/return bounds, real main/route execution, live running-Capture request/initial anchor, useful-prefix mutation/source reuse and stop/lifetime closure. Refusal preserving debt is not eventual progress or a 28-tick readiness proof. No measurement image/HITL before that contract closes.

Logs in session scratch: `i5-checkpoint-full-wrap-red.log`, `i5-checkpoint-price-{target,native}.log`, `i5-checkpoint-{normal,physical}-control.log`, `i5-checkpoint-final-{focused,qualified,native,build}.log`, `i5-checkpoint-reviewed-{focused,native}.log`, `i5-checkpoint-precontrols.sha256`. No commit/push/PR/archive or device action.

Final closeout checks: `openspec validate windowed-playback-generation --strict` valid; `git diff --check` clean; all nine current checkpoint packet/runtime/task/design/delivery local links resolve; task checkbox count29/60 unchanged. Parsed final native summary confirms116/116 executed suites PASSED/zeroFAILED-ERRORED. Final four precontrol SHA256 checks pass. Default ELF symbol inspection finds no selectInputCheckpointOwner, PlaybackPhysicalDeadline::admit, pollQualifiedInput or qualificationInput execution symbols. No upload or device action; build evidence remains valid after test/doc-only review additions.
