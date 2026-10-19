# I5 — dedicated rolling Record HITL execution

**Date:** 2026-10-16
**Latest outcome:** PARTIAL / full recipe NOT PASSED after approved attempt4. Managed MIDI-close safety ordering repaired and device-verified; deadline-corrected919e17e6… image now evaluated with correct metadata. Growth and empty-middle legs pass; held live no-retrigger/output/content schedule passes independently, but held frozen Stop handoff fails and subsequent strict idle guard refuses. Matching physical Stop remains unverified. Exact default restored/fresh close-loop-restored02 postflightPASS. Parent tasks unchecked, progress29/60; earlier host-blocked/NOTuploaded statements below are historical.

## Scope and identities

User requested the dedicated HITL after the local growing-Record slice. Branch `feature/unified-rolling-playback-path`, HEAD26125ceb plus preserved intentional dirty tree. Registered `rolling_record` preset/scenario and temporary `STIM-ROLLING-RECORD-TEMP-01@v1`; canonical stimuli unchanged. Actual content gestures affected only authorized track8/slot4 (operator1-based); slot5 remained unused. No other-slot content operation or SD quarantine/delete/rename/cleanup. Automatic normal persistence occurred.

Contract and pre-edit transition oracles: [I5 — HITL recipe](ROLLING-RECORD-HITL-RECIPE.md), [I5 — growing coverage](ROLLING-RECORD-COVERAGE.md). Canonical `host_midi_hitl.py run --preset rolling_record` / managed DeviceSession owns serial and two Stops/drains. No captured command.json execution or bespoke serial owner. One USB MIDI input listener, existing MidiClockMaster and fixture emitter, scoped actions and fresh LOOP_EDIT/target proof.

## Decisive observation and host verification

Added explicit qualification-only `!ROLLING_RECORD_STATUS <uint32>`: fixed saved/restored-mask selected-slot metadata/two ordered endpoint copies; direct output only after guard restoration. No runtime fields, allocation, source walk, periodic emitter or clock/input policy change. Cold functions/literals remain FLASH; Tier-A prefix in `.cpp`, real classification/mask/nonmutation tests and marker contract pass. Initial schema34fields; latest schema38 appends frameEpoch/requestEpoch/requestActive/requestInvalidated. Native and host readers use exact schema, strict request/range/binding/source/coverage checks. Generation0 is valid for initial Track operation; epochs are still checked for equality/currency.

Single focused safety/verification review found5concrete host issues: unproven selection before clear, fixture velocity98 vs assumed100, insufficient Stop physical/geometry proof, incomplete branch coverage accepted as success, and unordered output inventory. Corrected with recognized selection/fresh target/stopped proof before scoped clear, connected real-emitter test, exact ordered inventory/accepted-tick placement, frozen Stop receipts/physical identity/cursor/ledger checks, and a full recipe requiring every leg plus matching and mismatching Stop. No repeat broad review.

The sparse replay cut is in the quiet gap after bar0's completed pair, with cyclic output order and surrounding request/response brackets;32tick observation tolerance is a host-observation acceptance threshold, **not calibrated arrival/latency/WCET**. Held TEMP fixture gate4steps avoids Record-stop projection rewriting its Off into bar0. Actual fixture parameters are recorded per leg. Fullnative/host scheduling and host send times do not establish wire arrival.

## Device attempts retained

- `captures/i5-rolling-record-preflash-01`: read-only managed preflight PASS, selected7/3STOPPED/transport0/closedoverlay/guard15/heapscomplete/two drains/no latch.
- `…full-01`: fresh-boot setup/host timeout after authorized clear; no measured leg PASS and no own shutdown report. `…host-debug-02`: actual Record/44snapshot diagnostic, then host timeout/no own shutdown report. Both retained; non-reset `…timeout-recovery-01/02` each completed2verified persistence-idle Stop/drains/no latch; stricter idle-guard refusal retained. Do not treat the original missing shutdowns as safe completions. Later host runs persist failure before resource close and finish cleanup normally; no proven general backend root cause claimed for those early hangs.
- Initial observed firmware failure:6accepted raw rows/liveToken8/rawMax1832, but empty served token2/period768 through tick7760. Root cause `serviceFirstRecordingBar` on production VisualCache turns discarded valid nonempty rolling private work. Real-input exact production-service interleaving reproduced behavioral RED. Fixed legacy service to leave LiveCapture-owned work alone except genuinely stale borrows. Safe append survives; semantic phase invalidation still cancels. No main scheduling quota or timing-policy change.
- `…growth-03`: setup refused missing retained DISP-slot proof; exited with2drains/no latch. Fresh managed target proof replaces that redundant legacy display guard. `…growth-04`: growth visible but host incorrectly rejected operation generation0; corrected from Track's actual zero initialization. `…full-05`: pre-arm transport refusal after a late-recognized filled-slot toggle; fixed action-recognition/stopped proof. `…full-06`: hardcoded early observation cut missed by reply cadence; changed to quiet cyclic cut, not a source/adoption invariant relaxation. These failed runs remain evidence, not PASS.
- `captures/i5-rolling-record-full-07` and `…full-08`: **growth leg functional PASS**; full run fails on following empty leg. Both have complete2drain shutdowns/no latch. Full08 includes epoch/request diagnostics and explicit record-bars8/overdub-bars0 metadata configuration. Earlier generic record_bars metadata2 was a legacy field, not actual fixture duration (actual8bar stream recorded in fixture counts).

## Verified growth leg, not full matrix

Both full07/full08 observed one source-qualified rolling reader over periods768→1536→2304 and raw windows0/768/1536. Quiet-cycle actual USB output is exactly ordered pairs **62,64,60**, On velocity98/Off0, with no duplicates. Independent committed `record` and `effective` inventories each contain exactly6rows: source60On200/Off296/NoteId1;62On968/Off1064/NoteId2;64On1736/Off1832/NoteId3. Snapshot endpoints match accepted inventory; ledger closes and placement checks pass. Corrected frozen Stop verified (full07 raw6624/final2304). Full07 routine shed9/full08 shed3; each tierA_evict/overflow/crit_rej/wipe0, no stalled Record families.

Following empty take remained silent/current token11 while accepting4rows/liveToken15. Full08 proves clock/frame/request epochs equal9, request active/not invalidated: epoch rejection ruled out. Geometry-accessor interleaving stayed GREEN, so no speculative geometry change was made.

Native role fixture then reproduced the concrete stale-shell defect: previous completed **unpublished idle** copy was transferred into the new ready-shell role without metadata reset; subsequent live work refused that old foreign completion. Corrected exact initial idle→ready transfer to reset only recycled shell metadata. A full Stop-job native setup did not reach the assertion and is not RED evidence; the actual isolated real-builder/Track/reader fixture is the RED→GREEN proof. This correction is **NOT device-verified yet**.

Empty-middle playback, held/no-retrigger and matching physical Stop remain NOT VERIFIED on device. The current general legs may all take mismatch; a dedicated reachable matching Stop producer is still required. Do not relax the full-recipe branch gate or change clock/playback policy to force PASS.

## Provenance caveat and exact images

Original full07/full08 metadata reported stale HEX9079ddc9… because run_metadata preferred `.pio/build` over configured cache. Original metadata is untouched. Each run now has `firmware-upload-provenance.json` and copied upload log, binding the independently verified retained no-build upload chain; this is **not a firmware self-reported hash or retroactive full recipe PASS**. Resolver locally fixed/tested to honor authoritative override/configured build root and report null if its image is absent rather than falling back to stale code. Rerun with correct metadata pin before any acceptance/checkoff. Use an isolated qualification build/artifact root for future concurrent native/HITL work; shared-root different-config builds lost SCons temporary files during this session.

Uploaded images retained in session scratch:
- Initial snapshot `i5-hitl-rolling-image/`: HEXc8cc555ea34d385335cf4107e5d3a06eb91c6fdee3cc04da54e5e417509aaa58; ELF47b0d09eb260c585301f849444da0bd5e8bdd1f59e5f860638fcb43267e55c37.
- Visual-owner fix/full07 `i5-hitl-visual-fixed-image/`: HEX30c20d4877f0bb0eec11616fcbf3e604983d3a53d1eb0aa5a36e276c0b5be01a; ELF66ecf0f4adea8e6f725a32c17aec72cc50c4c9d5da268fe2f458ece94e897dfe.
- Epoch diagnostic/full08 `i5-hitl-epoch-probe-image/`: HEX8150f479a8de2484d02b0ff9b10ecb916a065d1a5356ea35e610a494fd778bc9; ELFe2ccf99d9dfebe798e36dc983c5d81f62b45fc322f776e8a3ef61527943b6a22.
- Latest stale-shell candidate `i5-hitl-final-candidate-image/`: HEXe2aff6897717b81ff03cff3d77867e413980ab86fa0e1316dc73375db1fc0553; ELF5fdb4600ff8a044407e336763b6c8703af70e0cf40ac12e12320b0262a50cc01; **built/retained, NOT uploaded**.

Default and enabled builds pass unchanged RAM1variables96192/locals34880; default code392056/padding1160, enabled code392264/padding952≥840 (112abovefloor); RAM2163392/EXTRAM11680. No numeric budget or Track/runtime layout rebase. One target build failed missing `.sconsign314.dblite`; clean retry succeeded89.762s, no source/budget workaround. Focused505/505, ASan/UBSan10/10 (`detect_leaks=0`), host46checks PASS. Earlier fullnative3280passed7skipped; latest full candidate first attempt had3281passed/no assertion failures but one missing-native-cache-file suite error, so it is not a clean full pass. Isolated full-native retry result is recorded below when available.

## Restoration and stop boundary

Exact pre-test default restored no-build SUCCESS10.512s: HEXf48aca664f60883378c427c8458f72602d4b943380f84e38bf340233fc3b0068 / ELF7cd8839899c684cf956dc9cc6f32235d01a7cb3ae12d39d0a2e2c7937ffa3563; cached hashes rechecked unchanged. First restored postflight's stricter idle refusal retained with2complete drains/no latch. `captures/i5-rolling-record-restored-postflight-02` PASS: selected7/3STOPPED/transport0/closedoverlay, PITReadyera2/guard15, bothheapscomplete,2verified drains/no latch. No stimulus after retry-budget stop; authorized scratch content remains, no cleanup or Git delivery.

Further hardware testing requires explicit approval for another bounded loop. Preferred next step: correct-metadata/isolated-root evaluation of retained stale-shell candidate, then empty/held and dedicated matching Stop coverage, without redoing passed helper/default-OFF compatibility milestones. Full740→768/complete-owner IRQ/wire-arrival/calibration/dense eight-holder qualification remains separate and unproven.

## Final isolated local gate

`pio test -e native` isolated retry **PASS/exit0:3281passed7skipped/0failed-erred,3288total,121passing suites/4skipped suites**,292.356s; log session scratch `i5-hitl-final-candidate-native-02.log`. This supersedes the shared-cache temporary-file error, not the incomplete device matrix. Host46tests also pass after the metadata-authoritative-root and raw≤final matching-Stop oracle corrections. No firmware source changed after this candidate's native/sanitizer/build checks; latest host identity fixes have not been device-rerun. Restored default/current source task status and approval stop remain as above.

## Owner-approved bounded continuation (2026-10-16)

User explicitly approved another loop; scope remains track8/slots4–5, maximum6 managed content attempts, unchanged canonical shutdown/default restoration and no weakened acceptance. Managed approved-preflash01 PASS/guard15/two drains/no latch. Retained e2aff689… candidate staged in isolated `i5-approved-qualification-root`; PlatformIO first initialization removed staged artifacts before upload (no flash), then same retained HEX/ELF recopied and no-build upload02 SUCCESS13.340s. Approved-full01 refused scoped clear during early persistence; no Record evidence,2verified drains. Settled01 passed guard15; approved-full02 reached Record but initial current stayed empty/token2/period768 through7760 despite6accepted rows/token8/max1832, equal clock/frame/requestepoch5 and active/noninvalidated request. Both metadata pins correctly equal e2aff689…; full02 shutdown complete2drains, end heap refused track_work (not full idle proof). Candidate does not clear this production blocker; no fullPASS.

### G0 — bounded discovery and G2 — ownership observation packet

VERIFIED: serviceFirstRecordingBar image implementation differs only by relocated call targets between prior epoch-probe and candidate; independent trace found possible foreign-complete-ready refusal and legacy-first-worker exclusion. Legacy-first real-input fixture passes52/52, so no speculative legacy edit. A test-only real-builder detached-complete-ready setup produces specificRED; production reachability/device state is UNKNOWN, not a proven cause. Existing current-only query cannot distinguish role/worker refusal from absent service. Owners/lifetimes remain existing Track/Loop/runtime with three views; no persistence/undo representation or active-slot authority change.

Included diagnostic-only mutation: append readyRole/buildRole/readyFlags/buildFlags/workerFlags to existing qualification snapshot (43fields), fixed metadata only under existing saved/restored guard, FLASH formatting/direct serial after restore. Final flags per ready/build: complete1/published2/rawCoverage4/source equals pending8. Worker flags: active1/liveCaptureBorrow2. No completion/nextGrain/source-currency callback, source walk, allocation, runtime counter/resident field, scheduler/Clock/input/adoption change or speculative ready cleanup. Native mask/nonmutation and strict host schema/range tests, marker contract and resource gates required before upload. Existing registered TEMP stimulus/actions/managed shutdown reused; authoritative metadata root pin required. Numerical floor840 unchanged, prior enabled padding952; no Git/checkoff/default activation or timing-readiness claim.

### G2 — initial publication/clock admission correction

Diagnostic image HEX6852c23d39b662fa1825ed928dca1b221022ef59775dd4aa89083b8c50c21fe6/ELFe8d8450597ea080a25756f877b6df3b2f5d66cbf1c5ecebe03582f65d24516c8 retained `i5-approved-owner-image`, isolated buildSUCCESS94.157s/padding952 unchanged, no-build uploadSUCCESS11.110s. Schema43 actual flags are complete1/published2/rawCoverage4/source-equals-pending8; worker active1/liveBorrow2; no completion/nextGrain/source-currency callback. Affected265native/23host/markerPASS. Early boot preflight refusal retained/two drains; settledpreflight02PASSguard15. Approved-full03 collected40queries: current empty, complete unpublished ready(source matches pending), inactive live borrow; host hung in session.close and outer420s timeout killed it, no own shutdown result. Separate non-reset timeout-recovery01 has two verified persistence-idle drains/no latch; stricter idle guard refuses, not rewritten as original completion.

Exact real-input production-owner RED: publish initial current, allow one extra main turn before first clock reader, advance accepted192/288pair; next demand deadline expected768/actual0,52othercasesPASS (`i5-approved-zero-deadline-red.log`). Main can recreate initial zero-deadline demand while frameIdle after initial publication; the first reader becomes Serving but demand remains active/zero, ready can complete yet adoption's nonzero-deadline guard cannot progress. Native test previously depended on how many initial owner grains ran before the clock. Device ready observations are consistent, but do not independently expose deadline.

Owner/API: `retainLiveRecordDemand`, existing Track/runtime/current frame. Mutation: while frame not yet Serving and an immutable nonraw Capture current of this operation is already published, do not admit another initial request; first clock establishes frame and subsequent main turn derives ordinary next coverage/deadline from it. Accepted Capture, source retention, three roles/current bytes, existing reader/adoption checks and oldest distinct debt preserved. No new state/queue/view/allocation/Clock or scheduler policy, no persistence/undo migration or bound-class change. Oracle must pass exact deadline768 then actual960/1056 output/NoteId/ledger; existing origin/epoch/later/Stop controls plus focused review/local gates and fullnative before closeout. Commit boundary remains dirty working branch/no Git delivery. Host session-close timeout must be bounded or diagnosed before another content attempt; no reset/latch clearing to force progress.

## Approved-loop closeout and next prerequisite

Three managed content attempts used, no further stimulus after full03 host teardown hang. This is a genuine safety/host-lifecycle blocker, not another request for permission or exhausted six-attempt allocation. `session.close()` did not return; no watchdog message before external420s timeout. Effective timeout environment was unset; source watchdog defaults to360s, but backend cause/GIL/lock location is UNKNOWN (no stack captured). Do not claim a proved backend root cause or fix by simply raising timeouts. Next bounded work is host close-path diagnosis/managed teardown isolation, with failure injection proving canonical two drains still execute; then evaluate latest firmware and remaining empty/held/dedicated reachable matching physical Stop on correct pinned metadata. No blind retry, latch clearing, device reset to force progress or relaxed branch gate.

Locally corrected initial publication/clock admission: focused507/507, exact originalRED→GREEN, full `pio test -e native` PASS3283succeeded7skipped/0failed-erred/3290total/121passing suites/304.754s (`i5-approved-final-full-native.log`). Host23PASS (`i5-approved-owner-observe-host.log`), marker contractPASS. One focused independent review APPROVE/no blocking findings; diagnostic build flags per-bit native coverage is a nonblocking omission, not new behavior. Sanitizers were not rerun for this correction; prior10-case sanitizer result belongs to the earlier candidate only. No speculative foreign-ready cleanup or legacy rewrite; foreign-ready setup/RED retained in scratch, production reachability unproven.

Default buildSUCCESS127.563s/padding1160; enabled corrected buildSUCCESS12.446s/padding952≥840, variables96192/locals34880/RAM2163392/EXTRAM11680 unchanged. Latest corrected candidate retained `i5-approved-zero-deadline-image/`: HEX919e17e629809eaf9963206c323d71546995b5117fd294039d8371122a68792b / ELF49d9bfde6db8a2df62e10d472e4c393980a53e056572117d04b51da3607b9876, **NOT uploaded/device-unverified**. Diagnostic image6852c23d… was uploaded; strict full03 metadata pins it correctly. No full inventory/timing/all-route acceptance follows.

Exact pre-test default HEXf48aca664f60883378c427c8458f72602d4b943380f84e38bf340233fc3b0068 / ELF7cd8839899c684cf956dc9cc6f32235d01a7cb3ae12d39d0a2e2c7937ffa3563 restored no-buildSUCCESS13.417s from verified retained images (`i5-approved-default-restore.log`). First restored strict-idle refusal retained; `captures/i5-rolling-record-approved-restored-postflight-02` PASS:7/3STOPPED/transport0/closedoverlay/PITReadyera2/guard15/bothheapscomplete/2verified persistence-idle drains/no latch. Authorized content8/4 retained; no other-slot content/SDcleanup/Git/defaultactivation/parentcheckoff. Session closeout records actual defect, rejected speculative fixes, verification and exclusions here; runtime/tasks/tracking/recipe/handoff updated.

## G2 — managed MIDI close safety ordering (2026-10-16)

Owner requested continuation and explicitly checking HITL skill. Skill failure-recovery pointer: preserve serial through canonical Stop/drain/Stop/drain and content dump; a host timeout is not a reason to raise the run ceiling. No lingering runner/capture processes found. Idle MIDI-only probes (no sends/serial) passed3input-first and5worker-thread output-first closes; backend root cause remains UNKNOWN, not a reproduced Python lock or device fault. Actual failure checkpoint is session.close before outer DeviceSession shutdown, so safe cleanup ordering is a concrete defect independent of backend cause.

Host-only scope: allow rolling scenario to defer HitlSession close to managed owner. Existing DeviceSession still owns two Stops/drains and its durable shutdown receipt; capture stays alive until afterward. Only after verified shutdown AND capture process stop, execute deferred close with10s faulthandler fatal watchdog and stage/cleanup receipt. Fatal timeout is nonzero with stack evidence, never PASS, and cannot precede required device safety. No arbitrary daemon thread racing port sends, alternate serial owner, reset/latch bypass or firmware mutation. On incomplete canonical shutdown, do not close deferred ports as though safe; preserve failure and existing latch semantics. Failure-injection subprocess must prove drain1/drain2/capture-stop precede a stuck close, pending receipt remains nonpassing and process exits bounded/nonzero; normal and exception tests verify exactly-once close and failure preservation. Reuse current registered TEMP stimulus only after these host tests pass. Remaining approval budget3of6 attempts; content scope8/4–5/default restoration unchanged.

## Managed close repair and approved attempt4 (2026-10-16)

HITL skill explicitly consulted: keep serial through Stop/drain/Stop/drain and content dumps; no timeout increase. Existing failure was before canonical shutdown, so no backend root-cause claim is needed for the ordering repair. `ManagedCaptureSession.defer_midi_close` retains scenario sessions by identity. Rolling finally saves report/snapshot and defers close. Real DeviceSession still owns shutdown; teardown Stop sender and outer final heap helper reuse the retained output, avoiding temporary native port closes before drains. After durable shutdown ok/drains2 and actual capture child exit, deferred close runs with collector/out_port/in_port stage receipts and native faulthandler10s fatal watchdog. A hung native call holding the GIL exits nonzero with stacks and leaves cleanup receipt nonpassing; no daemon port-close race, alternate serial owner, firmware/default/SD-policy change or raised run timeout.

One focused lifecycle review found two additional pre-drain temporary-port closes in Stop sender/final heap helper; repaired/retested through those actual APIs, no broad re-review. Tests fail if teardown opens another output instead of borrowing retained one. Real DeviceSession/two drains run with only I/O evidence boundaries substituted. Exception/incomplete-shutdown/capture-child-still-live/identity-idempotence tests pass; subprocess native `ctypes.PyDLL(None).sleep` holds GIL and watchdog exits bounded/nonzero with stage+stack evidence only after drains/heap/capture-stop. Final affected host94PASS9.106s (`i5-deferred-cleanup-final-host.log`). Original backend close root cause remains UNKNOWN; idle probes3input-first/5worker-output-first passed, not reproductions. Read-only `captures/i5-rolling-record-close-preflight-01` validates default stopped/guard15/two drains/capture-stop/portclose/no latch. Diagnostic safeguards do not turn earlier missing-shutdown attempts into safe completions.

Retained deadline-corrected HEX919e17e629809eaf9963206c323d71546995b5117fd294039d8371122a68792b / ELF49d9bfde6db8a2df62e10d472e4c393980a53e056572117d04b51da3607b9876 verified before no-build uploadSUCCESS11.431s (`i5-deadline-candidate-upload.log`); **now uploaded/evaluated**, superseding prior NOTuploaded status. No new firmware production change or target rebuild in this continuation. Early boot preflight01 refused before content; settled02PASSguard15 and deferredclose complete. Isolated authoritative root pins attempt4 metadata exactly919e17e6…; copied upload log retained in capture. Content operations only8/4; no other-slot content or SD cleanup.

`captures/i5-rolling-record-approved-full-04` verdictFAIL, but decisive progress:
- Growth legPASS: three ordered pairs62/64/60, exact6-row record/effective/NoteIds,768→1536→2304 and all three raw windows, ledger closure and corrected frozen Stop(raw6928/final2304).
- Empty-middle legPASS: window768 has zero rows; ordered64/60 pairs, exact4-row record/effective/NoteIds and corrected frozen Stop(raw6424/final2304).
- Held live pair65/velocity98: accepted On728/Off920/NoteId1, ordered single output On/Off/no retrigger, period1536/window0→768/ledger closure. Independent saved `held-playback-review.json` uses unchanged schedule oracle: expected output1496/2456 within recorded query brackets. This is live-playback-only evidence; original failed report unchanged.
- Held StopFAIL: raw3352/final1536, frozen successor readiness receipt covers[768,1536), but sampled frame remainsIdle/epoch0/current oldLive throughtick2728 after rewind; shared frozen reader never qualifies. General focused real-input case for the same held pair/Stop passes54/54 and does NOT reproduce production reused-take/deferred-maintenance failure. No speculative reader/foreign-ready/source-policy patch. Matching physical Stop remains unexercised; full recipe cannot pass without it.

Attempt4 completed normal canonical2verified persistence-idle drains and capturestop, then collector/out_port/in_port close; `midi-cleanup-result.json` oktrue/complete1of1. Host hang did not recur. Follow-up stopped strict idle guard refused; two persistence drains completed, but persistence idle is not whole-track idle. No further content attempt/reset used to force progress. Four of six approved content attempts used; stopped early on concrete frozen-handoff/idle-health blocker, not missing permission or exhausted budget. Next is a reachable production reused-take/deferred-publication oracle for held mismatch Stop plus dedicated matching Stop producer, preserving branch/physical/source/lifetime checks. No timing/WCET/arrival/all-route or legacy-retirement claim.

Exact defaultf48aca66…/7cd88398… restored from rehashed retained files no-buildSUCCESS11.350s (`i5-close-loop-default-restore.log`). First restored strict-idle refusal retained; `captures/i5-rolling-record-close-loop-restored-02` PASS7/3STOPPED/transport0/closedoverlay/PITReadyera2/guard15/bothheapscomplete/two verified drains/deferred portclosecomplete/no latch. Authorized scratch retained/no SDcleanup/Git/defaultactivation/checkoff. Full native close-loop result recorded below when available; prior production candidate3283pass7skip/buildpadding1160/952≥840 evidence unchanged.

Final close-loop native gate: `pio test -e native` oncePASS/exit0,3284passed7skipped/0failed-erred/3291total/121passing suites,117.504s (`i5-close-loop-final-native.log`). New held-mismatch native case passes54-case suite but does not reproduce failed device reused/deferred handoff; firmware production source and target images unchanged by that test. Attempt4 retained ring snapshot routine shed7, tierA_evict/overflow/crit_rej/wipe0, Record families rej/evict0/all21RECS transmitted. This supports capture integrity, not missing hardware branch acceptance. Strict OpenSpec/diff hygiene checksPASS; current links and image/metadata hashes verified at closeout. No source sanitizer rerun/current sanitizer claim, target timing/WCET or global activation.

## G2 — held Stop current-root return progress correction

Owner continued the current held-Stop scope. Branchfeature/unified-rolling-playback-path/HEAD26125ceb plus preserved dirty tree. Read-only production trace confirms current root publication can overtake frozen ready and its job remains owner until current-root adoption. Actual full deferred-maintenance call initially failed native linking on boot/load/storage status boundaries (`i5-held-deferred-repro-01.log`); no behavioral RED or success stubs claimed. Existing fixture friend routes then drive the actual Stop grain and Record Snapshot publisher that top-level maintenance dispatches, alongside actual input/Clock/TrackManager/rolling-holder/reader. These are controllable production owner operations, not complete main-loop/storage/timing evidence.

Discriminating sequence: real accepted held728/920pair, Stop3352→rewind280/final1536, off-clock real Stop/publication grains until root-current before768, actual frozen adoption768, then continued owner/Clock/Stop/Snapshot turns. SpecificRED: current root published, root qualification AwaitAdmission, source retained, Capture current carries numeric revision1 equal new root revision1; root-return lane reports Idle before advancing qualified work. At nextedge1536 deadline expires, frame becomesIdle; RecordPublished job stays pending. Native terminal assertion and bounded trace retained (`i5-held-deferred-repro-04.log`, `i5-held-root-owner-trace-direct-02.log`). This proves a root-return/retirement deadlock, NOT yet the device's first768 nonadoption cause; no such claim. One-row count is not the selector gate.

Owner/API: Track::stepPlaybackViewMaintenance Record retirement/current-root return branch. Mutation: in rolling-enabled mode, its same-revision Idle shortcut requires Committed origin and exact current-root snapshot identity, not merely same playbackRevision/operation plus coverage containment. Capture current cannot discharge root-backed return progress. Default-OFF shortcut unchanged. Existing qualified build/adoption/deadline/source-lifetime rules still own all acceptance; no forced adoption, deadline refresh, new owner/state/field/queue/view/allocation/mask/SD/undo representation or bound-class change. Must preserve frozen ready/current bytes and job raw range until actual committed-root takeover, then retire exactly once. Legacy retirement/all-route/740→768/WCET/default activation explicitly excluded; no Git delivery.

Validation: specific real-owner RED→GREEN with root-before-edge control, exact output/NoteId/ledger/resource assertions, focused neighboring native and fullnative gates, default/enabled build against unchanged840floor, one focused independent lifecycle review; correct image pins and remaining approved managed HITL only after local proof. Historic decisions reviewed by targeted search: DEC-131 exact completion authority, DEC-133 connected Record and DEC-135 qualification/recycled-shell currency; reuse their existing owners, no reassessment trigger or conflicting ownership policy. Original device held frozen Stop still decides final acceptance, not native optimism.

### Held Stop admission-stall trace (2026-10-16, continuing G2 — held Stop current-root return progress correction)

Bounded fixture traces (`i5-held-root-owner-trace-direct-02.log`, `i5-held-frozen-request-trace.log`): after
root-current publication and frozen adoption at 768, the frozen request stays active with
`coveragePreparation=AwaitAdmission`, source owned, deadline 1536, reason `AwaitingSourceQualification` from
edge 776 through 1480 — `beginQualifiedCoverageBuild` refuses without updating reason on every owner turn. At
edge 1544 the deadline expires: prep/ownership drop, pending re-frozen request reads `DeadlineExpired`, frame
Idle, `RecordPublished` job still bound. The candidate-integrity assertion (expected 2 counted root rows in the
build role immediately after adoption, was 0) shows the counted candidate loses its rows before any admission
attempt could succeed — the silent early-refusal checks in `beginQualifiedCoverageBuild` (candidate
source/count identity) then fail forever. Speculative retirement-lane root-bridge restriction tried and
reverted (RED unchanged); production source matches the attempt4 image again. The resetting owner between
AwaitAdmission and admission (candidate: a stop-grain/finalization/publication `resetMetadata` on the build
role) is the named next diagnostic target. No upload, no attempt5, device on exact defaultf48aca66…; suite
intentionally carries the failing reproducer this turn. No Git delivery.

### Admission-stall probe results and reverted second edit (2026-10-16, same G2 correction)

Drop detectors across the root-publication loop and the adoption advance never observe candidate rows >0:
`views[build].storage.eventCount` is 0 for the whole reproducer while `coverageEventCount` reads 2 — the paused
qualification's count lives in the builder, so the earlier candidate-integrity assertion reflects builder state,
not a lost-rows event. First post-adoption probe (`i5-held-adm-probe.log`, tick 768): candidate source matches
the frozen request (`srcEq=1`), `viewBuild.qualifiedEventCount()=1`, but `coverageSource.complete()=0`,
`readUnits()=0`, `coverageSourceUnits=0` — by the first post-adoption instant the qualified coverage source is
already released while a frozen request with deadline 1536 stays active (`AwaitingSourceQualification`), so
`beginQualifiedCoverageBuild` refuses on `!coverageSource.complete()` every turn until the deadline expires and
the `RecordPublished` job strands — matching the device terminal shape. Roles churn during the loop (drop rows
at turn1: cur1/ready0/build2; turn200: prep0/own0/ready2/build0; turn456: prep2/own1/build0), so the release
happens inside that churn, not at the clock adoption.

Second speculative edit (protect an admitted in-flight build from `!request.active` retirement in
`stepCommittedPlaybackRetry`) did not change the RED — admission is refused upstream of that branch — and was
**reverted**; production source again matches the attempt4 image state. Two edits tried and reverted total; no
production change stands. Named next diagnostic: instrument which call releases the completed coverage source
between qualification completion and the first admission attempt (suspects: re-freeze path in the retirement
lane, `retireSupersededReady`/role rotation interacting with `beginCoverageQualification`, or the stop-grain
canonical phases), then fix only the demonstrated release-vs-admission race. Reproducer + probes remain
registered and intentionally RED; suite not green this turn by design. No upload, no attempt5, device on
exact defaultf48aca66…; no Git delivery.

### Held Stop root-bridge defect demonstrated; two-part fix applied, chain not yet green (2026-10-16)

Pure-flow maintenance-return trace (`i5-held-post-trace.log`, passive replicated owner, no perturbing direct
calls) pins the plateau: post-adoption every production maintenance turn returns `Idle` while the qualified
AwaitAdmission candidate sits intact — the same-revision shortcut at the Record retirement branch treats the
adopted **Capture**-origin view as a "complete same-revision copy bridging the root identity swap" (numeric
playbackRevision/sourceGeneration collide across representations). That starvation was tried as a standalone fix
earlier and correctly re-applied. With the shortcut blocked the next defect surfaced: the retirement lane demands
a **whole-loop window for the 2-bar held take** (`shouldAvoidFullVisualRebuild(1536)` false), which the declared
contract refuses within a one-bar boundary lead (stale binary showed pending `CannotCompleteBeforeHandoff`;
expiry then strands the job — matching the device terminal shape).

Applied production fixes (both demonstrated, rolling-gated, default-OFF unchanged): (1) the root-bridge shortcut
requires Committed origin + exact current-root snapshot identity under `PLAYBACK_WINDOW_ROLLING_ENABLED`; (2) the
Record-return lane requests the same one-bar windows the rolling reader serves (`windowed` forced true under
rolling) instead of an inadmissible whole-loop window. Fixture predicate aligned to the designed retirement edge
(corrected 1536 geometry served from frozen Capture **or** adopted root-backed committed window); obsolete
candidate-integrity diagnostic removed.

New terminal state (`i5-held-fix-trace2.log`): the one-bar root window {0,768} qualifies (1 row) before the
adoption; at the first post-adoption boundary (tick 776, phase inside the adopted frozen bar) the qualified
candidate for the **passed** interval is discharged and the source retired WITHOUT adoption (prep=0/own=0/
frozen=0, current still the frozen Capture view, frame Serving), the plateau goes Idle with nothing owed, and at
1536 a fresh {0,768} request re-stamps with `AwaitingSourceQualification`/deadline 0 — the Stop job stays bound.
Remaining link: stale-interval root requests/candidates must be retired as missed (`record_ready_late` semantics)
and the NEXT interval requested, instead of silent discharge. Reproducer intentionally RED; suite not green; no
gates run this turn; no rebuild/upload/attempt5; device on exact defaultf48aca66…. No Git delivery.

### Release pinpointed to the clock path at the adoption boundary (2026-10-16, same G2 correction)

Entry-state probe on the replicated owner (`i5-held-entry-trace.log`): the first post-adoption maintenance turn at
tick 776 enters with `entryPrep=4 (Retiring), entryRetiring=1, entryFrozen=0` and exits `prep=0/own=0` via the
bounded retirement drain (result GrainServed). The qualified one-bar root candidate's coverage source is therefore
released and the frozen request cleared **on the clock path itself, between the tick-768 adoption and any
maintenance turn** — not by the retirement lane. The subsequent plateau (result Idle, nothing owed) and the
tick-1536 re-request with `AwaitingSourceQualification`/deadline 0 follow mechanically; the Stop job stays bound.

Named next fix target: the clock-path release at/after `adoptPreparedPlaybackWindow` inside the tick-768 dispatch
(suspects: post-adoption hot-path cleanup releasing `coverageSource` once the adopted view's own source is
Capture, or the adoption's completion handling releasing a source that still backs an AwaitAdmission candidate).
The release must not destroy a qualified AwaitAdmission candidate whose frozen request is still inside its
deadline; retiring it is exactly the device's stranded-job shape. Reproducer intentionally RED; no gates this
turn; device on exact default; no Git delivery.

### Release located in the tick dispatch; work-record gate reverted (2026-10-16, same G2 correction)

Post-pulse probe (`i5-held-entry-trace.log`): at tick 776 the coverage source is already Retiring with the frozen
request cleared **immediately after the clock dispatch, before any owner turn** — the release runs inside the
per-tick path (post-adoption hot-path/tick handling), not in `serviceRollingPlaybackWindow`,
`serviceAdmittedDeadlineMisses`, or the maintenance lane. A speculative `admittedOwned` gate on
`CoveragePreparation::Building` in `cancelRollingPlaybackWindow` did not change the RED (the release is a
different call site) and was **reverted**; production remains attempt4 + the two demonstrated fixes only.

Remaining link, precisely located: a clock-dispatch release between the tick-768 adoption and the first owner
turn destroys the qualified AwaitAdmission candidate whose frozen request (deadline 1536) is still inside its
deadline. Next diagnostic is a release-site census on the per-tick path (candidates: hot-path post-adoption
cleanup, tick-path frame/launch reset reaching `retireCoverageSource`). Reproducer intentionally RED; no gates
this turn; no rebuild/upload/attempt5; device on exact defaultf48aca66…. No Git delivery.

### Per-tick release census progress (2026-10-16, same G2 correction)

Ruled out as the tick-dispatch release site: `cancelRollingPlaybackWindow`'s discard paths (frozenOwned/admittedOwned
both false against the post-adoption frame{Serving, deadline0, startPhase0}; the speculative Building gate changed
nothing and was reverted), `rejectRollingFrame` (blocks the frame only, no release), the maintenance lane, the
rolling service's known branches, the misses service expiry (deadline 1536 not reached at 776), the runtime reset
(refused while the stop owner is pending, retires nothing), and the stop-job rewrite path (returns Pending while
the adopted frozen Capture view is the served window). Remaining suspects: the hot path's post-emission handling
and any per-tick deferred/visual-cache poll reaching `retireCoverageSource`/`discardCoverageBuild` with the
RecordPublished job pending. Next: a caller-tag census on the release primitives under the native guard, then the
minimal preservation fix (a qualified AwaitAdmission candidate backed by an in-deadline frozen request must
survive tick-path cleanup). Reproducer intentionally RED; no gates this turn; no rebuild/upload/attempt5; device
on exact defaultf48aca66…. No Git delivery.

### Release primitive named: retireCoverageSource fires once in the adoption dispatch (2026-10-16)

Native-only caller tags on the release primitives (`i5-held-tag-trace.log`): `HELD RETIRE-TAG` fires exactly once —
inside the tick-768 dispatch, immediately after the frozen adoption (intact prep=2 at tick 760, post-release
prep=4/retiring at tick 768, before any owner turn). The completion-handler and launch-cancel release branches did
not fire. The caller is therefore one of the direct `retireCoverageSource`/`discardCoverageBuild` sites reachable
from the per-tick flow; next census step tags the call sites themselves (discardCoverageBuild callers: launch
cancel 4040, retirement lanes 5554/5693, unidentified WindowBuild 2916/4087 regions; direct retire callers:
stepCommittedPlaybackRetry 4891/4918, qualification/admit failures 1835/1872) — one more run names it exactly.
Fix shape unchanged: a qualified AwaitAdmission candidate backed by an in-deadline frozen request must survive
tick-path cleanup. Tags are native-only diagnostic instruments retained for the next census run. Reproducer
intentionally RED; no gates this turn; no rebuild/upload/attempt5; device on exact defaultf48aca66…. No Git
delivery.

### Release fully characterized: zero-slack admission refusal at lead == leadTicks (2026-10-16)

Decision-input probe at the release (`HELD T-RETRY2 lead768 leadTicks768 grains5 outstanding0`): the release is
`stepCommittedPlaybackRetry`'s `CannotCompleteBeforeHandoff` branch — `canCompleteWithinLead(768, 0)` returns false
because the formula `turns <= (availableTicks - leadTicks) / maximumPhaseAdvancePerTurn` allows zero turns when the
remaining lead exactly equals the contract's minimum lead (768 == 768, grains 5, outstanding 0). The root request
was stamped at tick 280 with deadline 1536 and deliberately waited behind the Capture ready
(`rootQualificationWaitsForCaptureReady`); arriving at the boundary with lead == leadTicks is the designed arrival,
and the formula's additional leadTicks reservation makes it inadmissible by construction — retiring the counted
candidate and stranding the RecordPublished job. Growth/empty passed because their requests were admitted with
slack before their boundary. Fix decision next turn (pinned numbers): either the admission formula must not
double-reserve leadTicks beyond the availability check for a demand whose deadline is the arrival boundary, or the
Record-return request must stamp its deadline so the designed wait still admits — choose with the contract owner's
semantics, then RED→GREEN, gates, rebuild, attempt5. Native-only diagnostic tags remain in place for that run;
reproducer intentionally RED; no gates this turn; no rebuild/upload/attempt5; device on exact defaultf48aca66….
No Git delivery.

### Admission-fix attempt narrowed the failing term (2026-10-16, same G2 correction)

Deadline-form admission (`canCompleteWithinDeadline`: grains must fit the available lead; leadTicks gates but is
not re-reserved), scoped to the Record-return site when the adopted Capture current covers the playhead, did NOT
clear the RED — same terminal state. The failing term is therefore the OTHER conjunct:
`playbackViewAdmissionKeepsAdmittedDeadlines` at the same site, which likely protects the **consumed frozen
window's stale `viewBuildWork` record** (deadline/epoch fields of an already-adopted window) as if it were a live
admitted deadline. The deadline-form attempt was reverted per the no-speculative-edits rule; production remains
attempt4 + the two demonstrated fixes. Next fix: admission must not treat a consumed window's work record as a
protected admitted deadline (or the work record must be cleared at adoption), RED→GREEN, then remove the
native-only diagnostic tags (RETIRE-TAG/DISCARD-TAG/T-QUALDENY/T-ADMITFAIL/T-RETRY1/T-RETRY2 probe) before gates.
Reproducer intentionally RED; no gates this turn; no rebuild/upload/attempt5; device on exact defaultf48aca66….
No Git delivery.

### The consumed-work-record fix is falsified; the blocker is the declared envelope (2026-10-16, same G2 correction)

Owner asked to resume at the written-down next step ("null the consumed frozen window's `activeViewBuildWork()` at
adoption... then GREEN → tags → gates → build → upload → attempt 5"). Two things had to be settled before that,
and the first one changed the conclusion.

**The inherited tree did not compile.** The reverted deadline-form attempt left a *duplicated* conjunct in
`Track::stepCommittedPlaybackRetry` (`TrackPlaybackWindowBuild.cpp`, the `CannotCompleteBeforeHandoff` condition):
`canCompleteWithinLead(...)` and `playbackViewAdmissionKeepsAdmittedDeadlines(...)` were each present twice, the
second copy with no `if (`. `pio test -e native -f test_midi_input_clock_integration` failed at
"expected ';' after expression" and 20 cascading errors, so no earlier claim about this tree could be re-checked.
The stray copy was removed, restoring the single two-conjunct condition (HEAD's shape). No other production source
was changed by that repair.

**The written-down premise is falsified by direct measurement.** With the tree compiling, the registered
reproducer `rollingRecordHeldMismatchStopWithDeferredOwners` reproduces its documented RED (54 of 56 cases pass,
this one fails). The T-RETRY2 probe at the refusal (tick 768) reads:

```
HELD T-RETRY2 lead768 leadTicks768 grains5 outstanding0 admitted1 epoch4 ll1536 phase280
HELD RETIRE-TAG
```

`admitted1` is `playbackViewAdmissionKeepsAdmittedDeadlines(...)` returning **true** — that conjunct *permitted*
the admission. The refusal is the **other** conjunct: `canCompleteWithinLead(768, 0)` with `leadTicks = 768` and
`requiredGrains() = 5`. The prior turn read this probe as "the failing term is therefore the OTHER conjunct" only
because the probe's format string carried **no specifier for the `admitted` argument** — the build log shows the
`-Wformat-extra-args` warning on that very line — so the value was never displayed. The instrument was repaired
(native-only; the tag itself is unchanged) and it now reports the value it computes.

The record the fix was aimed at is also already absent at that instant: the refusal happens while
`coveragePreparation == AwaitingAdmission`, and `activeViewBuildWork()` is `runtime->coverageBuildNeedsService() ?
&viewBuildWork : nullptr` — i.e. already **null** for `Counting`/`AwaitAdmission`. The end-state probe agrees:
`buildActive=0 owned=0 pendingJudge=0`. Clearing a record that is not read cannot change a *contract* refusal.

**The proven blocker is the declared service envelope.** `CoverageServiceContract::canCompleteWithinLead` charges
`turns = requiredGrains() × (maximumConsecutiveDeniedTurns + 1)` turns, each permitted to advance
`maximumPhaseAdvancePerTurn` ticks, on top of the one-bar `leadTicks` reserve. For this demand:
`5 grains × (7 + 1) = 40 turns × 64 ticks = 2 560 ticks`, plus `leadTicks = 768` → **3 328 ticks of lead**, while
the held take's loop is **1 536 ticks (2 bars)**. The required lead therefore exceeds an entire loop, so **no
interval of that take** can ever be admitted as a root-backed window. This is not marginal: the smallest possible
coverage demand (`fill + promotion` = 2 grains → 16 turns → 768 + 1 024 = 1 792 ticks) already exceeds 1 536, so
the envelope cannot admit *any* coverage build on a two-bar loop. This is the long-recorded **root-current
next-wrap `CannotCompleteBeforeHandoff`** RED (tasks.md, 3.15b–c, 2026-10-08/09), whose own disposition is
"qualify private useful root work before Capture-ready adoption **or a stronger measured source/window service
contract** … do not bypass admission".

**What happens after the refusal (second, downstream defect).** The refused retry calls
`retireCoverageSource()`, which discards the qualified AwaitingAdmission candidate and coalesces its obligation.
The lane then re-requests the one-bar window exactly at the 1 536 boundary, where `handoffLeadTicks(phase,
coverage.startPhase, loopLength)` is **0** (the playhead sits on the window's own start), so
`retainRefusedPlaybackViewRequest` stamps no position at all. The terminal state is a permanently stalled demand:

```
HELD DIAG pend=1 kind=1 reason=7 epoch=0 deadline=0 admitted=1536 invalid=0 ll=1536 cov=0/768 phase=0
HELD DIAG2 srcRev=1 srcGen=0 srcOrigin=0 srcSnap=1 rootRev=1 snapEq=1 buildActive=0 owned=0 pendingJudge=0 rollPhase=0 rollEpoch=0 rollBound=255
HELD DIAG3 liveJob=1 interval=1 rootCurrent=1 exitQueued=0 servable=1 bound=1 audible=1
```

`committedPlaybackRetryCanProgress` refuses a zero-epoch record for a non-`launch`, non-`cold` demand, so the
unpositioned demand can be neither tried nor re-stamped. The served window stays the frozen **Capture** view, so
the Record job's release condition (`currentRootWindowServed`: Committed origin **and** the current root's
snapshot identity) is never met, `releaseFrozenRecordWindow` is never reached for it, and the job stays bound —
the reproducer's final assertion. (The `DIAG`/`DIAG2`/`DIAG3` rows are test-only instruments added this turn; the
`HELD STATE/ADV/ROWS/MAINT/POST` probes are the prior turn's.)

**Experiments run and reverted (production source carries none of them).** (1) Neutralising the first conjunct at
that site: the refusal moves to `T-RETRY1` at tick 1536 (`DeadlineExpired`) and the job still strands. (2) Treating
an unpositioned retained demand as progressable, in both `committedPlaybackRetryCanProgress` and
`beginQualifiedCoverageBuild`: the qualification starts and then fails on the position/lead checks, is invalidated
(`reason=5 DeadlineInvalidatedEpoch`, `invalid=1`), and the job still strands. Both were reverted; only the
compile repair and the probe's format-string repair stand.

**Conclusion and the decision this needs.** The written-down step cannot advance the RED, and no local fix can:
making this demand admissible requires either a **change to the declared envelope** (a declared-constant/
bound-class change, which the recorded disposition explicitly gates behind a *measured* service contract and an
owner decision — and which needs its own measurement/qualification), or a **new admission/role route for a short
take's root-return window** (private committed-root work before Capture-ready adoption, its own G2
contract/reassessment/DEC). The alternative shapes — serving that window from the raw/retained lane, or exempting
the job-qualified return demand — are design features, not bounded repairs.

**Verification.** Focused suite back to exactly the registered single RED: 54 of 56 cases pass, the held-mismatch
reproducer fails on its final assertion. Full native `pio test -e native`: **3 293 cases / 3 284 succeeded /
7 skipped / 1 failed** (the intentional reproducer) in 181.2 s — the healthy figure the prior turn recorded for
this tree (3 284 passed / 7 skipped) with the tree compiling again. No firmware production behaviour changed.

**Builds (verification of the compile repair; nothing uploaded).** `pio run -e teensy41-capture-serial` SUCCESS —
`variables 96,192 / code 392,056 + padding 1,160` (floor 840), the approved line byte-for-byte, 320 B above the
floor; `pio run -e teensy41-capture-serial-window-qualification` (**rolling enabled**, where the held path and
its diagnostics live) SUCCESS — `variables 96,192 / code 392,264 + padding 952` (floor 840). Both match the lines
the prior turn recorded for this tree, so the repair costs no RAM1 and needs no budget re-base.

**Not done, deliberately.** No upload and no device action: attempt 5 was **not** spent, because no firmware
change exists to evaluate and the prior turn's rule ("no further stimulus until that blocker is addressed") still
holds; the installed default OFF image and the SD contents are untouched. No SD cleanup, no Git delivery, no
parent checkoff, no default activation, no diagnostic-tag removal (the tags are still the reproducer's evidence).
Task 3.15b–c and the recorded root-current admission RED remain open.
