# I5 — builder-call cancellation and atomic live-source reads

## Scope / implementation contract (2026-10-15)
Existing owner-authorized running-Capture firmware/native work in CURRENT_WORK applies;
branch feature/unified-rolling-playback-path /26125ceb plus preserved dirty tree.
Extends DEC-127 — semantic live-source retention; no ownership transfer, storage/undo
model, clock dispatch/timing policy, new queue/view or default activation.

**Boundary:** RuntimePlaybackViewBuild public mutation calls own their private Fill,
Order and candidate state until the call finishes. Cancellation during a call becomes
an idempotent pending discard; it must neither clear that executing state nor allow
same-address re-entry. End-of-call resolution reports Denied/false, detaches the borrow
and invalidates only its unpublished destination. A runtime reset during either builder
call requests cancellation and returns its existing false/busy outcome **before**
retiring sources or resetting destination/ledger metadata. It is not a completed reset;
caller must retry its existing obligation. Preserve-destination requests only preserve
when all pending requests agree; published views are never reset by builder discard.

Short saved/restored PRIMASK sections cover call admission/completion/discard metadata,
live descriptor creation/validation/currency and each raw cursor validation+one-row-copy
(or charged chunk transition). Source filtering, private fill/order loops and whole
runtime reset/retirement stay outside the mask. All source owners remain physically
retained by the caller/Track. Standalone Fill and arbitrary direct candidate writes
retain their serialized-owner preconditions. This is not an all-owner adoption protocol.

**Preserved:** accepted Capture rows/identities, published current storage/generation,
ledger, source retention and incoming mask; no allocation, Clock reorder or lost reset
success claim. Rejected re-entry consumes no new build/source. A stale source at call
completion is rejected before Complete is reported. The final publication/adoption
consumer must still atomically qualify its complete source/role/epoch; that running
consumer is not activated here.

**Non-goals/external gates:** running request/anchor/coalescing/adoption/stop closure,
all Track metadata writers, calibrated clock/wire-arrival and finite owner/IRQ bounds.
No target borrower activation, whole-grain masking, upload/HITL/SD/Git or parent checkoff.
No new marker or serial-observable scenario; hardware timing is explicitly NOT RUN.

## G0 / historical review
Read-only source-concurrency discovery and scratch ASan witness completed before code.
Relevant DEC-126 — raw-prefix stability and DEC-127 — semantic live-source retention
explicitly qualify serialized/retained callers, not synchronization. Existing builder,
runtime reset false/refusal, three views and native prewarm/real-clock fixtures reused.
Search terms: live-source, sourceCursor, cancel, reset, ISR, ownership, concurrency.
No formal hard trigger found: fields/helpers remain on existing owners; no Track ISR
relocation or tick timing change. No new owner or parallel state manager.

Actual ISR trace: IntervalTimer -> updateInternalClock -> updateAllTracks; pending Record
activation can seal/replace source and active overdub wrap can clear/reload/sort producer.
Heavy stop finalization stays deferred; slot changes do not destroy LoopPool shells.
Tokens/checks alone do not exclude a validation-to-copy interruption. Native query
callback injects cancellation at a controlled boundary, not a fully driven timer run.

## Transition oracle fixed before behavior edits
- Normal: live Record On100/Off200, prewarmed unpublished destination. Begin -> Fill;
  stepFill(8) -> Order with exact two rows; order -> Complete, never published by builder.
- Before entry: actual runtime cancellation then step -> Denied, detached/no source writes.
- During begin/fill/order/complete query, for viewBuild and bridgeWindowBuild: actual
  owner cancellation requests discard; call refuses/denies, never dereferences cleared
  state, no live borrow afterward, source bytes and sibling published view unchanged.
- Runtime reset during a call: false/busy, no destructive reset while executing; call
  ends Denied/detached; explicit later reset can complete. No claim that false means reset.
- Nested begin/step on same builder during source callback: refused without replacing
  candidate/source. Repeated cancellation and mixed preserve requests remain terminal.
- Source append/destructive rewrite during a controlled mask boundary: descriptor/row
  access occurs under saved mask, stale source rejects; accepted content remains owned.
- Incoming mask0/1 and nested source/current/call masks restore exact incoming value;
  private multirow Fill/Order runs outside the mask. Test-only mask model is not a
  target IRQ-duration proof.

## Fixtures / controls / verification
Existing test_playback_prewarm includes actual Fill/Build/Order/runtime bodies. Reuse
its live-source factory/callback interface; add native-only hardware mask adapter
(no copied domain state machine or target hook). Include both runtime builders and
recovery/published-view/accepted-content assertions. Production-path clock fixture
remains neighboring coverage, not proof of every intra-Track ISR schedule.

Baseline scratch witness unchanged source: normal and before-entry PASS; actual runtime
cancel/reset during Fill generation query exit134, UBSan null member + ASan SEGV in
RuntimePlaybackViewFill::step cleanup after discard cleared destination. Scratch
witness SHA256 ecc10f2f19a8d4de465a3311e056571c52a37c9977262dbb41a7bfa12df7f372.
Neighbor baseline453/453 PASS in10.309s; details in session scratch
`i5-source-concurrency-g0.md` and `i5-source-preemption-*.log`.

Required: focused prewarm/ownership/real-clock/source-stability suites; scratch sanitizer
control+cancel/reset retest; native mask/refusal/re-entry/stale/recovery coverage;
full `pio test -e native`; `pio run -e teensy41-capture-serial`; measured ARM/native
ABI and existing RAM1 numerical floor; one focused lifecycle review; strict OpenSpec
validation and diff hygiene. No device action needed/claimed for this local boundary.

## Completion record
Status: locally verified boundary complete; NOT device/all-Track concurrency qualified.

- Contract/oracle unchanged. DEC-130 — builder-call cancellation and live-source exclusion
  records the existing-owner transition; accepted Capture/current/ledger invariants hold
  through both builders' interruption matrix. Parent29/60 remains unchanged.
- Registered baseline `pio test -e native -f test_playback_prewarm`: lost-begin cancellation
  assertion followed by Fill SIGSEGV/ERRORED, exit1. Initial test-only ledger query typo
  failed compilation and was corrected before this genuine RED; not counted as evidence.
- Final focused command with `-f test_playback_prewarm -f test_runtime_working_set_ownership
  -f test_midi_input_clock_integration -f test_capture_source_stability`:477/477 PASS,
  final25.104s. Includes9new registered cases and both-builder interruption matrices.
- Original scratch four-case ASan/UBSan witness: normal/before/cancel/reset all exit0;
  formerly crashing cases return Denied with accepted source retained, no publication.
  Scratch9case sanitizer runner:9/9 PASS, no sanitizer diagnostics.
- Scratch wrong-mask restore-to-zero control:2specific failures in incoming-mask and
  row-transaction tests; live header unchanged during control. No live mutation needed.
- `pio test -e native`:3244cases,3237passed/7skipped/0failed,121passing suites,340.813s.
  Full run preceded target-only intrinsic spelling fix; native branch unchanged.
  Final focused477 rerun after that fix also PASS. Logs in session scratch:
  `i5-source-call-full-native.log`, `i5-source-call-focused-final.log`.
- Initial target build FAILED48.887s: Teensy Arduino core does not declare CMSIS
  `__get_PRIMASK`/`__set_PRIMASK`/`__DMB`. Replaced target-only spellings with existing
  project PRIMASK/DMB assembly pattern; no native or state-machine change.
  Final `pio run -e teensy41-capture-serial`:SUCCESS30.627s. RAM1 variables96192,
  ITCM393216(code391720+padding1496), locals34880; approved floor840 unchanged,
  margin656B. RAM2variables163392/EXTRAM11680. No budget relaxation.
- Compiler-measured ARM source128/Fill168/Build256/Loop1008/runtime7792 unchanged;
  host source168/Fill240/Build400/Loop1600/runtime9976, Build+8/runtime+16B.
  Executing/pending flags grouped into existing target padding; no new target resident
  payload, hot allocation or Track field. Conditional stack/IRQ timing not qualified.
- Built, NOT uploaded: HEX SHA256
  `c91d3a238f1fe70b6d13539d952aae22631f00b1aae89398fcb293ccee95caab`;
  ELF SHA256 `ebeb576841d2c32d3dc695431dd6783e537ed0adf15bf6ea64a2764995c9680e`.
  Prior restored default-OFF image remains installed; no device/SD/reset actions.
- One focused lifecycle review APPROVE/no concrete in-scope defect under explicit
  retained-source/serialized-external-writer contract. Subsequent target intrinsic
  compatibility fix reused existing assembly and was built/retested, no review loop.
- Strict OpenSpec validation and `git diff --check`:PASS. Runtime/design/tasks/handoff/
  guide/decision/execution-bounds/tracking closeout updated; no task checkbox changed.
- Device verification NOT RUN; no new serial marker/stimulus or target activation.
  All-Track role/publication/adoption, standalone Fill/direct writer serialization,
  running request/anchor/coalescing/stop and finite owner/IRQ/calibration/wirearrival
  remain open. RollingOFF/targetcontinuationdisabled/receiptsdefaultOFF, no740→768readiness.

Session closeout: owner unchanged, no authority conflict or hard reassessment trigger;
alternatives and exclusions in DEC-130 — builder-call cancellation and live-source
exclusion. No commits/push/PR/archive or user-data migration. This boundary is done;
continue with complete Track publication/adoption protocol, not another token/retention
prerequisite or target activation based on these local tests.
