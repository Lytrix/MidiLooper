# Record presentation ownership — native execution continuation

## Scope and contract

Working branch: `feature/unified-rolling-playback-path`, HEAD `11faa90f` plus the existing accumulated dirty worktree. This record covers the owner-approved task **3.15g — wrapped short-loop Record finalization**, specifically the post-`RecordPublished` presentation lifecycle and its fixture migration. It does not approve all-route cutover, remove compatibility emitters, relax deadline admission, change the default rolling flag, or change the RAM1 budget.

Track remains the owner. The sealed extent lives in the slot's `CaptureStopPresentation` from admission, at a stable address used as the Loop borrow key. Non-empty Record publication activates the presentation reader owner, releases the finalizer, and pops the interaction FIFO synchronously. The presentation retirement service subsequently owns root-publication, reader-takeover and extent-release duties. Empty results promote no presentation and finish retirement through the ring's existing RecordPublished dispatch.

`CaptureStopPresentation::generation` is the **interval-derived presentation generation**, derived only by `presentationGenerationFor(interval)`. It is neither `CaptureRange::generation()` (range lifecycle identity) nor `frozenCaptureContentGeneration` (content identity). The stamping sites and qualifier use that one derivation. No new independent validity counter is introduced.

## Transition oracle and production paths

- Published private-root return: real Record input and stop admission → closure/staging/normalization/pass transfer/publication → empty ring, active presentation and retained extent → qualified frozen source → off-clock window preparation → actual E2 edge adoption → exact MIDI/ledger observations.
- Default compatibility versus rolling qualification: the default build intentionally uses **Committed-origin copied frozen coverage** with no Capture token on the view. The rolling-enabled build uses **Capture-origin coverage with the presentation's frozen content token**. The same source adapter still distinguishes range generation from content identity. Do not infer a token propagation bug from a default-profile Committed view.
- Successor overdub: the published Record base remains readable and an already-prepared successor remains adoptable at its boundary during OVERDUBBING. Ordinary fixed/bridge eligibility remains PLAYING-only; no preparation runs on the clock edge.
- Refreshed live window: a complete live Capture view with stale final geometry or content token does not prove the first frozen interval is already served. The off-clock preparer recovers that current interval instead of skipping to bar two.
- Empty take: `test_record_normalization_filtered_empty_resets_geometry` was RED because RecordPublished dispatch refused while `ownsRawRange()` was true, preventing the retirement step that releases it. Removing that circular dispatch gate makes the existing production-path test GREEN; no build-cancellation workaround is retained.
- Root-first missing view: construct a truly missing reader through `releaseFirstRecordingBarReturnWindow()` rather than leaving an invalid retained lease. Request a positioned, admissible long-loop next-wrap interval through `requestCommittedPlaybackWindow()`. The existing retry owner must advance a matching retained/in-flight request instead of re-requesting forever. The first complete root view becomes current through the normal owner, emits the independent expected pair, then presentation ownership/extent retire.
- Stopped/background lifecycle: `hasRealtimeDeferredIdleWork()` includes active presentation retirement after the ring job is gone. Lifecycle fixtures drive Snapshot and deferred owners through presentation discharge, not just FIFO release. A running reader is stopped explicitly only in lifecycle/inventory fixtures; reader-continuity fixtures keep playback running.

Existing seams are the real Track/Loop/storage closure in `capture_storage_harness.inc`, the explicit native deadline clock, the existing owner selector, and the MIDI send capture. The rolling wrapper now forwards Unity's C-linkage setup/teardown hooks to the shared harness, so cases reset independently. Clock state also resets per case.

## Shape 3 — post-retirement identity readers

Production identity readers were enumerated in `TrackPlaybackWindowBuild.cpp`, `TrackStateFacade.cpp`, `TrackPlaybackHotPath.cpp`, and `TrackOverdubLifecycle.cpp`:

- E2 adoption, frozen-source qualification, rolling-source retention and holder selection recognize the matching live Record finalizer before publication and the runtime presentation owner afterward. Root-return preparation remains publication-qualified.
- Background Snapshot servicing follows the active published presentation with a live owner.
- The compatibility onward reader requires a retained extent and matching Loop owner/id; revision-transition fallback additionally qualifies the presentation.
- The retirement service clears the runtime presentation owner with the record's matching generation **before** setting `retirementActive=false` and zeroing owner/LoopId/interval/content token/generation.
- Descriptor reservation/release completion reads the range's own lifecycle state; it does not use zeroed presentation identity to qualify content.

Thus zeroed identity is the dormant descriptor state, not evidence of premature release. Fixtures assert ownership discharge, source release/re-arm, and a working successor admission. No retirement hold was added to preserve obsolete job observations.

## Disproved diagnostic conclusions

Earlier chat claims that a Committed candidate proved a frozen-preparation precedence defect were incorrect: the default suite had rolling disabled. The registered private-root preparation regression **passes unchanged** in the rolling-enabled wrapper. Probe rows from other tests were also previously mistaken for the registered RED's state. No dispatcher reorder or committed-candidate supersession from those hypotheses is retained. Temporary PUBPROBE, RETIREPROBE, READY/EMIT probe functions and branch-id instrumentation have been removed.

## Verification record

Actual focused commands after the continuation:

- `pio test -e native -f test_shared_capture_finalization`: **86/86 PASS**, exit 0. The runner's earlier extra errored pseudo-case is not an additional Unity test.
- `pio test -e native -f test_rolling_record_retirement`: **8/8 PASS**, exit 0. Includes private-root preparation, all five recovery variants, stopped unadopted-successor retirement and retained Committed demand controls.
- `git diff --check`: PASS at the focused checkpoint.

Outstanding verification (do not check off parent tasks from this record):

- Final full native after the empty-successor repair: GREEN, 3,304 PASS / 7 SKIP / 0 FAIL; 122 suites PASS / 4 SKIP / 0 ERRORED, exit 0, 209.483 s. Focused five suites 479/479.
- Latest corrected default and qualification firmware/resource gates PASS: default code 392,072 + padding 1,144; qualification code 392,360 + padding 856, both above unchanged 840 floor.
- Focused independent lifecycle review: completed once; all four concrete findings repaired and discriminated as recorded below.
- Strict OpenSpec validation and `git diff --check`: PASS after the empty-successor repair.
- Corrected-candidate hardware rerun reaches both overdubs and undo/redo but overall remains INCONCLUSIVE on one playback/persistence witness issue, detailed below. The exact historical default is restored with healthy fresh postflight.

The native-only checkpoint above preceded device activity; the later sections record all uploads, stimuli and restores.

## Focused review repair execution packet

Scope: task **3.15g — wrapped short-loop Record finalization**, extending the existing Track presentation driver and live-finalizer reader path. HEAD is now `26125ceb`; intervening host boot-handoff commits are preserved. No new owner, storage format, deadline waiver, retirement hold, or budget change is authorized.

Historical review: searched Record/presentation and **DEC-136 — private committed-root preparation**, **DEC-140 — bounded unadopted successor**, **DEC-141 — matched terminal**. Reuse the existing bounded release and holder/qualification owners. No formal reassessment trigger for these corrective extensions.

Transition oracles:
- Running takeover: finish preparation/source cleanup, adopt the matching current-root successor at its real edge, then ordinary deferred dispatch (without forcing a window turn) must release the presentation and allow a non-empty successor stop.
- Dense release: a real admitted Record spanning at least three chunks remains active/extent-owned through partial one-chunk releases; completion discharges the borrow, re-arms the descriptor and permits a second admission.
- Before publication: real admission and closure only, with pass publication withheld; production offers/selection must prepare bar two, and playback must adopt/emit the independent accepted pair while the finalizer still owns the source.
- Stopped successor: drain the presentation rather than FIFO; assert extent release/re-arm, owner removal and successor cleanup. Disabling retirement must fail this anchor.

Independent review completed once: four concrete findings above, no broad repeat review. Prior full-native run was NOT GREEN (four errored suites); qualification resource gate also failed (`792 < 840` padding). Previous focused GREEN did not prove these four contracts. Results of repairs will be appended after execution.

## Review repairs and corrected fixtures — 2026-10-19

1. `serviceCaptureStopPresentations` now has an ordinary deferred wakeup after actual matching current-root reader takeover; it yields before takeover so accepted interactions can progress. The running native anchor finishes source cleanup before the real adoption edge, then invokes no extra window grain: presentation/borrow/extent discharge and a nonempty successor stop must succeed.
2. `stepDeferredRecordRetirement` reuses `retireJobCaptureSourceGrain` and returns Pending on partial release, retaining extent/active state. Finalizer clearing and teardown occur only after release completion. A real take spanning five chunks crosses publication, publishes its root, observes partial releases, discharges its borrow, re-arms and admits a second nonempty stop.
3. `hasRecordReaderOwnerForSlot` recognizes the matching live Record finalizer or active published presentation. Frozen qualifier, holder/preparer, E2 and serving retention use this contract. The new real-admission case withholds all finalization after closure, recovers/adopts the first frozen interval, prepares bar two through production offers/selection, emits exactly the accepted note pair, and still has no committed Record pass.
4. The stopped-unadopted-successor anchor drains active presentation ownership, not FIFO occupancy, and asserts released/re-armed extent, no borrow/bound reader, and successor cleanup. Removed the wrapper's fallback that forced a window turn when no holder was selected.

Initial REDs: real pre-publication preparation unavailable; partial release lost the production wakeup/extent. Negative control disabling `serviceCaptureStopPresentations` fails exactly the running takeover, dense release and stopped-successor anchors with FIFO already empty. Restored byte-identically (`ae78bbae…` at that checkpoint); later one-line slot-specific cold-admission repair changes this hash intentionally.

Continuous-capture fixture dereferences now read the slot descriptor before admission, not an unset job back-pointer. RWO and MIDI/clock corrected-geometry fixtures first recover/adopt the actually missing first interval at its boundary before requesting bar two; their exact MIDI/NoteId/ledger oracles remain unchanged. Competing-track dispatch services the selected production holder rather than asserting only track zero can be eligible. The restore-queue fixture builds its immutable SD payload before creating its live capture owner.

Full-native capacity coverage exposed a real regression: reserving eight tails per Track consumed 64 chunks at boot. Stable descriptors now remain dormant after boot/load, cold capture prewarm reserves the requested slot only, and loaded overdub start admits only its missing tail without revisiting predecessor-owned scratch. The guard is slot-specific (`runtime.captureStopOwner`), so another slot's finalizer cannot leave the incoming descriptor unreserved. Dense-library admission still enforces its original free-pool floor; the assertion explicitly counts the newly cold-admitted one-tail cost.

Focused latest checkpoint: rolling retirement 10/10, shared finalization 86/86, continuous capture 107/107, RWO 214/214, MIDI/clock integration 61/61, storage load 35/35. Commands/logs are under the session scratch directory; final full-suite/build results will supersede intermediate totals.

Safety preflight `captures/i5-presentation-review-preflight-01/health.json`: PASS, two canonical drains, STOPPED, pending=0, queue=0/4, both heap observations complete. No content stimulus, upload or reset in that preflight. The exact historical default restore is retained (`08f43e450ee327546b844af95a8b9ac57be46fcb8e84ad982ddda10e357fc964`). Hardware content evaluation is still NOT RUN on the final repair candidate.

## Final local gates and first device attempt — 2026-10-19

Full native before device: exit 0, 3,303 PASS / 7 SKIP / 0 FAIL, 122 suites PASS / 4 SKIP / 0 ERRORED, 174.792 s. Both final builds pass: default 392,072 + padding 1,144; qualification 392,360 + 856, unchanged 840 floor. Strict OpenSpec validation and diff check PASS. Qualification HEX `38044ef364788b77e80179a542b4872d6e36fbcc14f4b7d9edaa88a02c743560`, ELF `fbf47c4d4b88423ee6a04995725b1c01fca91105948107e98429d1f1e2214fc6`, pinned in configured cache and unchanged across no-build upload.

Registered `base` / `STIM-BASE-2-2-01@v1`, track 8 / slot 4, fresh boot: `captures/i5-presentation-review-base-01`, **INCONCLUSIVE_CAPTURE / SETUP_UNCONFIRMED**, exit 3. Record raw 1,881 → final 1,536, first overdub reaches OVERDUB_STOPPING → PLAYING in 15 µs, no parked FIFO, undo/redo gestures execute. **Not accepted:** second overdub start is concretely refused twice (`#MEM_DENIED,op=overdub_start,reason=prewarm_failed`, at 59.052 s and 64.243 s), despite 416 free chunks / 144 KB heap. Missing second phase/first-note and persistence-progress observations remain failures; no PASS or task checkoff. Routine shed 1, no critical reject/eviction/overflow. Shutdown complete with two drains, stopped pending=0/queue=0/4 and both heaps complete (`after-base-01` health).

Exact historical default `08f43e45…` restored no-build with hash verified; fresh postflight `restored-01` initially refused heap for boot storage work, then no-reset settled `restored-02` PASS (two drains, STOPPED/pending=0/queue=0/4/both heaps complete). No SD cleanup or other-slot action.

Corrective packet: a short nonempty successor round-trip remains GREEN natively and does not reproduce the device refusal. The existing wrapped Record + frozen reader + empty accepted successor shape, strengthened to assert retained extent ownership, **REDs** on "an empty successor stop must not clear the Record presentation's sealed extent obligation". The empty successor was assigned the same slot descriptor, then `clearOwnedRawRange()` erased the prior Record's obligation, leaving a sealed-but-falsely-unowned range that later `reserveTail()` refuses. Narrow repair: an empty overdub stop references no range descriptor (`presentation=nullptr`); existing legacy empty-producer cleanup remains, prior presentation ownership stays intact. No new hold, admission relaxation, marker or field. Relevant tests/full native/builds and device rerun are owed after this repair.

Empty-successor correction checkpoint: focused five suites 479/479 GREEN; final full native running. Default/qualification rebuilds SUCCESS with RAM1 lines unchanged (1,144/856 B padding). Strict validation/diff PASS. Corrected qualification HEX `a2313c1d59005d01f72f385929d7237f08861a82629908b8bfac9320a806dfb3`, ELF `4383dedc0135bbb18185a0e84393a3cf49abdeb55fc1eacbf9885c30bc0316c0`; default HEX `3ce7606e671521a1195d4d130e5909dde895cb272865518b4811d03fd39768c6`. Source lifecycle SHA-256 `d64b42d02a98e23ba74c0cf14018c411d4a0db282ea367c271ac58117e7df078`. These supersede the first device attempt's image for future acceptance, not its recorded result.

## Corrected device run and final closeout — 2026-10-19

Final full-native log `full-native-empty-successor-final.log`: exit0,3,304 PASS/7 SKIP/0 FAIL,122 suites PASS/4 SKIP/0 ERRORED,209.483s. Source SHA-256 matches `d64b42d0…`. Both corrected builds/resource gates and strict/diff checks pass as pinned above. No second broad review; concrete first-review and device findings repaired/retested.

Same authoritative registered base entry point/target/fresh boot, corrected qualification HEX `a2313c1d…` (metadata exactly matches no-build upload): `captures/i5-presentation-review-base-02/host_midi_automation_baseline_20261019_014352.json`. **Functional successor criterion reached:** PLAYING→OVERDUBBING twice, both stops reach destination, accepted phase inventories Record32/32, overdub16/16, second8/8 with zero open/out-of-range/sequence errors, undo1/redo1. Record raw1,884→final1,536; first-note latency7/4clocks≤12; slowest Record finalize572µs. No second-start prewarm refusal, no parked stop (pending0,phase0,queue0/4), persistence progress complete, sliced/nonblocking, recorded slice bounds pass; routine shed2/critical reject0/Tier-A eviction0/overflow0.

**Overall verdict remains `INCONCLUSIVE_CAPTURE / EVIDENCE_MISSING`, `overall_ok=false`; process exit0 is not a PASS.** Sole issue: `persistence_active_snapshot_unavailable_during_save`. Verifier `summarize_deferred_persist_large_loop_bounds` counts a playback snapshot receipt or MO within ±500ms of each complete playback-active persistence window;6/9 windows qualify. `unmatched-persistence-windows.json` records the three unmatched windows (device µs):
- key `slot:7:4`:43,464,366–43,540,146;
- key `slot:7:5`:44,017,427–44,094,643;
- key `slot:7:6`:44,564,908–44,649,096.
Previous qualifying witness42,800,068; next45,606,106. These are neighboring saved-slot keys, not additional content stimulus. Missing witnesses do not alone prove unavailable roots, silence correctness or deadline safety. No verifier/envelope/limit was changed, and no blind rerun was spent to seek a favorable witness alignment. Task3.15g and broader parent gates remain unchecked pending this discrimination/acceptance.

Safety: corrected run shutdown complete/two verified drains; `after-base-02` health PASS, STOPPED/pending0/queue0/4/both heaps complete. Exact pretest default `08f43e450ee327546b844af95a8b9ac57be46fcb8e84ad982ddda10e357fc964` restored no-build (13.616s), hash verified. Fresh managed final postflight `captures/i5-presentation-review-restored-final/health.json` PASS with boot-owned capture, STOPPED, tick0, pending0,queue0/4,complete internal/external heaps,two canonical drains,no incomplete-shutdown latch. Authorized scratch8/4 content retained. No SD quarantine/delete/rename/cleanup, unrelated-slot stimulus, default rolling activation, archive, commit or PR.

Session closeout: decision/history/design/task/runtime/tracking updated; no overlap-resolve/ledger/order algorithm changed in these repairs, so their evaluation guides need no new mechanism entry. Remaining blocker is the named playback/persistence evidence gap, not native/build/resource failure or lack of upload authorization.

## The post-publication gap, measured on the real witness image (2026-10-19)

`captures/i5-playback-witness-base-05` runs the pinned qualification image and reproduces the remaining gate failure.
This section names what serves playback between the wrapped take's `RecordPublished` and the publication of its
snapshot, because that gap is the acceptance blocker.

**Measurement.** Record stop at 41.994 s (`RECS,stop,3,356,0,1892,1536,0` — the take wrapped into a 1,536-tick loop,
so the playhead resumes in bar 1 at phase 356). The track goes `STOPPED_RECORDING → PLAYING` at 41.995 s, and the
first loop note-on arrives at **45.072 s**: 3.078 s of silence with the transport running. The same measurement on
the default image (`i5-playback-witness-base-04`) is **0.002 s**, and on the previous presentation candidate
(`a2313c1d…`) 3.203 s, so the gap belongs to the rolling qualification lane.

**Why nothing serves it.** Every turn resolves a source in this order: the live-record window, the frozen record-return
window, the rolling window, then the fixed source (`selectedFullLoopBridge` / `publishedFullLoopView`), then the
merged committed fallback. In the gap each door is shut, and each has its own row:

- the frozen return window is prepared for the **second** bar (`win_frozen_readiness … adopt_wait,e2_boundary,`
  with `start=768,length=768,loop=1536` and mask `76` = ready exists + complete + loop match, while
  `ready.coverage.startPhase == phase` and "current view is this frozen job" are clear). The E2 edge only adopts a
  successor whose interval starts at the playhead's phase, and the playhead is at 356 in bar 1, so it waits for a
  boundary it cannot reach until the bar turns — 2 s later;
- the window lane is excluded and **cancels the frame**: `win_gate … x=win_track_not_playing` with
  `pl=1, ed=0, od=1` — the excluding term is `hasPendingDeferredOverdubStop()` (the Record job still pending in the
  turn after the stop), and the exit runs `cancelRollingPlaybackWindow()`, so the frozen handoff has no `Serving`
  frame to extend when the job does retire;
- the fixed source needs a compiled full-loop bridge or a published full-loop view for the **current** revision, and
  neither exists for a revision whose root is not published (`root_rev=0`);
- the merged fallback is committed-only by construction — `ensurePlaybackMergedMidiEventsBuilt` gathers
  `tryGatherPublishedSnapshotEventsInWindow` and clears the set on a miss
  (`GatherMissHoldEmpty`); with `root_rev=0` the gather finds nothing, and the `lcr,src` row taken when the overdub
  starts reads `from=cache,ev=0,notes=0,root_rev=0,cache_rev=1` — the effective-content view is empty too.

The snapshot of the recorded take then constructs in bounded units from 43.6 s (`snap_gate … cu=40,62,22,16,48,…`) and
publishes at 44.733 s (`hp=1, rv=1`), after which the fixed/merged route has content again.

**A second, independent symptom in the same window:** the merged fallback rebuilds on nearly every turn instead of
holding its miss. `DIAG,track_rebuild` counts 468 rebuilds in the 5 s bucket covering the stop (default image: 594;
previous candidate: 996), i.e. one full-loop gather per turn throughout the silence. The reuse guard compares
`builtFromRevision`/`builtFromSnapshotIdentity` with the live identity, and while the root is unpublished neither
stabilises. That is exactly the window in which the SD saves and the sliced snapshot construction need the CPU.

**What the contract says, and what is missing.** The rolling gate's own comment states the intent — *"the completed
build publishes as the `ready` successor, the resolver adopts it at its boundary and the slot is served from then on
(the merged fallback covers the traversals in between)"* — and the same block notes that *"the raw first bar remains
the compatibility source until the exact completed successor is eligible"*. Both statements presume a served source
for bar 1 during the gap. With the snapshot publication now deferred (its own bounded construction), no owner provides
it: the frozen Capture window is bar 2 only, the merged fallback is committed-only, and the fixed path needs the root.

**The narrowest fact in the chain** (`src/Loop/LoopSnapshotPlayback.cpp`): `tryGatherPublishedSnapshotEventsInWindow`
returns `false` outright when `publishedSnapshot()` is null — the slot was empty before the record, so there is no root
at all — and in the stale-root branch every unpublished pass it overlays is filtered by
`if (!isRecordPassId(passes, passId))`. The committed-only merged fallback therefore cannot surface a **record** pass
even when a stale root exists; a freshly recorded take has no route through it until its own snapshot publishes. That
is why the gap is structural rather than a missing retry, and why repair (b) reads "fold the immutably published
take", not "retry the gather".

**Two candidate repairs, neither applied.** (a) Let the retained window cover the interval the playhead is *in*
(or the whole loop) rather than only the successor interval, so the E2 edge can adopt at once — costs window storage
and changes the interval contract the DEC-136 hold is asserted over. (b) Let the merged fallback fold a take that is
already immutably published (`RecordPublished`, DEC-142) instead of treating every capture row as live — this
contradicts the "committed-only (224719)" rule as written, but that rule predates immutable publication. Both change
which owner serves a newly published take, so both need a decision before code; RAM1 qualification padding is exactly
at the 840 B floor, so any ITCM growth must be paid for first.

No firmware line was changed for this section, no verifier or window was touched, and task 3.15g stays open. The
capture and the pinned image remain the reproducer.
