# Production live-display identity / transfer regression

## Scope and historical review

User-approved next step under S7.5b.6 — display frame continuity. Branch `feature/unified-rolling-playback-path`, baseline `abaa30975d2cd2650fbce6638ef9e6d3783ad5d7` plus existing dirty repair/host/docs, preserved. The prior trace measures454ms first-overdub drawable-count plateau while accepted ingress and DMA progress; it does not establish individual NoteId-to-completed-frame freshness.

Read display architecture, current draw-source entry, lifecycle, paint phases, source reuse key, Snapshot/preview ownership and existing coverage. Follow DEC-081 — audible playback never gates persistence and DEC-115 — point-of-use projection refusal; existing Track/Loop/DisplayManager owners only. Search terms: draw-source, composed frame, Capture preview, session-exit handoff. No new owner/resource model, migration or architectural decision.

Primary files: new native production fixture, native suite registration and scoped hardware boundary, existing harness display-stub gates, observation-only friend access. Production composition change only if a behavioral RED proves an in-scope blocker. No new device marker, instrumentation or timing limit, no upload/SD action. Real pixel/raster hardware and explicit completion are native I/O boundaries; domain admission, NoteIds, source selection, composition, viewport and paint continuation remain real. Native completion is a controlled transport event, not physical OLED parity.

## Transition oracle fixed before behavior edits

1. Real empty active slot, start Record, accept known-pitch On/Off pairs via Track. Read assigned NoteIds from accepted canonical events, not from display output. Real display Resolve/Draw/Transfer reaches submission and separately controlled completion. Exact independent pitch/start/end/NoteId rows survive; no duplicate identities.
2. Real Record stop queues the deferred job; drive only its actual finalization/publication worker with Snapshot held private. Preserve the last complete Record draw source. Real overdub start accepts a distinct known pair. Its complete preview must become drawable on a fresh display service while the sealed Record base remains intact and Snapshot stays absent/private. No unpublished Snapshot rows selected as committed source.
3. Input after Resolve may not mutate the already-selected Draw/Transfer frame or fabricate completion. It becomes visible on a later Resolve. Busy DMA leaves Transfer pending; completion and next submission are distinct, and an in-flight frame's identities remain immutable.
4. Publish current Snapshot through its owner; next source adoption remains atomic, identities exact and no duplication/blanking. Target/source/window change cannot mix old and new content. Include no-root and stale-existing-root controls as available in the real lifecycle.
5. Test registration and behavioral RED—not build/link errors—before any production repair. Ordinary passing controls remain enforced; a deliberately pending future/known defect oracle must stay explicitly reported, never silently checked as completion.

## Verification and boundaries

- `pio test -e native -f test_live_capture_display_boundary` and related display/Record suites.
- Full native; default `pio run -e teensy41-capture-serial`, unchanged approved RAM1 padding floor840.
- One focused independent review of the nontrivial lifecycle fixture/repair; fix/retest concrete findings only.
- Strict OpenSpec validation, diff check, runtime/task/durable evidence closeout. No parent S7.5b.6 checkoff without full production-window and device acceptance.

## HITL recipe applicability

Native-only slice: no new stimulus, serial row or hardware gesture. Marker tier/contract n.a.; no instrumentation RAM1 impact. Reused future device entry remains canonical base/STIM-BASE-2-2-01, host300BPM, authorized8/4, phase buttons/final realtime Stop, canonical two drains and pinned image, managed360s cap. Any device NoteId/source/frame-completion instrumentation remains a separate recipe/build/tier slice; no physical-latency claim from this native test.

## Evidence

The completion record below supersedes the initial pending state.
## Production failure and narrow repair

New registered suite `test_live_capture_display_boundary` links real Track/Loop admission, NoteId assignment, Record stop/worker/Snapshot publication, production draw-source/viewport resolver, PianoRoll draw and DisplayManager Resolve/Draw/target Transfer-admission branch. Suite-local raster/SSD transport is the hardware boundary: bytes copy into an immutable in-flight frame, submission and controlled completion are separate; observer receipts copy real selected NoteIds, track/slot/source key and Resolve input frontier. Edit/load presentation alternatives fail closed; UI labels are outside the content subject. No copied display-selection algorithm, manually injected view/job or new device marker.

Behavioral RED: accepted overdub pitch70 and its assigned NoteId at local192..240 remain in canonical/preview storage but submitted frame stays at two Record notes (`Expected3 Was2`). `resolveDisplayNotesByDrawSource` held the entire composed frame during no-root session-exit publication, discarding fresh overlays with the candidate base. Repair holds only the complete committed prefix, recomposes accepted overlays in existing pending-paint scratch, then atomically swaps composition; committed source key stays original until complete root handoff. Refresh requires a valid held key matching track/slot/window; otherwise the conservative hold remains. No unpublished Snapshot source is adopted, no extra runtime field/buffer/owner/scheduler/budget.

Fixture correction, not a publisher defect: the initial sparse second pair800..880 legitimately causes `RecordStopLength` content-grace trimming to768; canonical and root then contain one note. The stopped-take oracle now uses1328..1408 and asserts final1536 plus accepted four Record events before stop. Canonical/root parity and both accepted NoteIds are checked after publication. A fresh isolated disable of the repair on this valid two-bar geometry makes exactly the three gap cases fail Expected3 Was2, with three Record/transfer controls succeeding; restored byte-identically. No false root-content defect is left open.

One focused independent review found a real moving-viewport mismatch: old committed window plus new-window overlay could be painted while retaining the old key. Added24-bar/16-bar-window production regression: old continuous Record window starts6128, fresh overdub wraps to first bar. RED at held window6128→0 before correction. `invalidateLiveDisplayCache(true)` now preserves the frame's existing key, and cold `syncDetailedPaintWindow` pins the qualified same-track/slot held interval during the no-root gap. Fresh overlays are composed only within that held interval. Complete root publication permits atomic source/window movement to0, with exact new interval NoteIds. No new state; frame preservation now includes its interval metadata. Review otherwise found scratch/swap lifetime, repeated overlay exclusion, acceptance assertions and native transport boundary sound. Concrete finding fixed/retested locally; no second review/blanket approval claim.

Seven enforced cases: exact Record identities/raster count/submission/completion; input after Resolve frozen frontier then next-frame visibility; busy Transfer pending without overwrite/fabricated completion; accepted OD during Record-root gap; repeated gap frames keeping overlays out of committed prefix; canonical/current-root handoff; moving-window interval coherence. Native controls do not measure physical pixel freshness, scheduler opportunity gaps or external MIDI dispatch queue residence.

## Final verification

- Final seven cases7/7 GREEN (`live-display-final-seven-tests.log`). Isolated valid-take overlay disable:3 scoped failures/3 successes (`live-display-valid-take-control.log`). Initial viewport-pin disable exposed mixed count Expected2 Was3. The strengthened final guard/raster oracle independently re-proves omission of a committed piano-roll row: Expected2 Was1 (`live-display-final-viewport-control.log`). Unconditional key reset gives Expected6128 Was0. All controls restored; final gather anchor `8322`, lifecycle `D02A`; none remains active.
- Full final native **3069 total /3062 succeeded /7 skipped /0 failed**,114.186s, including existing harness consumers (`live-display-final-native.log`). Earlier checkpoints are superseded.
- Default capture SUCCESS, unchanged RAM1 variables96192/code392376/padding840/floor840/locals34880; RAM2163360, EXTRAM11680. Window qualifier stays cold via existing `DISP_COLD_MEM`; no budget rebase. Final built HEX SHA-256 `5ba533387171238e5409eea9716a126a10adb97cef40946c2219b5e5b8c80604`, **NOT uploaded**. Log `live-display-final-build.log`.
- Existing `STIM-BASE-2-2-01@v1` listed/reused for future same300BPM track8/slot4 hardware gate; no canonical stimulus edit/new marker/tier registration or new device action. Latch absent; prior board state remains stopped/drained from the host-repair PASS image, not this new image.
- Strict OpenSpec validation/diff check at checkpoint pass; final closeout updates durable display behavior, runtime status, tracking, design and active task evidence. No new DEC: existing complete-frame/interval ownership contract, no architectural owner/resource transfer. No commit/push/PR.

## Remaining acceptance

S7.5b.6 — display frame continuity and combined timing/content qualification remain unchecked. New image needs pinned hardware verification; native transport completion is not physical OLED parity, numerical freshness deadline is not declared, and Record startup dispatch batching is not repaired by this display change. Long held viewport deliberately waits for complete replacement source; this is coherent frame retention, not a claim that outside-window input is immediately visible. Broader target/source/multi-owner interleavings and complete device NoteId→frame-completion telemetry remain separate acceptance work.

## Hardware-discovered early-overdub ordering — execution packet

Owner-authorized hardware verification flashed `5ba53338…` after pre/post two verified drains and ran the unchanged300BPM8/4 baseline in `captures/live-display-verification-t8s4-01`. Baseline report PASS/overall_ok=true/issues empty and both report/outer shutdown complete2/ok, but **display continuity fails independently**: firstOD32→0→1, count1 plateau426.729ms during4 NoteOns, next-count proxy max373.096ms; recovery1→33. Original pre-display-fix PASS comparison had33→37 plateau454.148ms/max proxy412.592ms. Exact physical pixels remain unmeasured; a lost composed Record base is a concrete failure, not qualification PASS.

Slice scope: reproduce real overdub accepted before the Record worker publishes its pass; extend only the existing DisplayManager hold/window predicate via existing Track per-slot stop-obligation metadata. No playback-view storage read, Snapshot readiness fabrication, new owner/queue/buffer/runtime state, marker or threshold. Existing DEC-081/DEC-115 and complete-frame/interval authority apply. No architectural reassessment trigger or new decision.

Oracle before production mutation: real two-bar Record and completed display seed; real stop admits frozen job; grant closure/window owner only enough to make Record-return audible, with no committed pass and no session-exit Snapshot queue. Accept real overdub. Before its first NoteOn, complete display must still contain both recorded NoteIds; after accepted pair, both base identities plus live identity appear exactly once through separate submission/completion. Then drive actual Record worker publication and assert continuity remains3 while Snapshot is still private, followed by complete-root adoption. Per-slot obligation query must not hold an unrelated slot, and live Record without a stopped obligation remains incremental.

Relevant outcomes: waiting for Record finalization is part of the no-published-source gap even before Snapshot is queued; a complete held frame stays authoritative, accepted preview updates compose independently, and complete root publication releases the hold. Missing/invalid held metadata keeps existing conservative behavior. Native RED→GREEN, focused/full native, capture build/RAM1, one focused review if lifecycle correction requires it, then canonical two drains/upload/unchanged hardware repeat. Preserve failed first-run evidence and distinguish baseline PASS from display verdict.

## Early-source correction and completed hardware qualification

Two added enforced cases reproduce pre-pass-publication base erasure (Expected2 Was0 before firstOD note; Expected3 Was1 after accepted pair), with real closure/window grants and edge adoption, no pass or Snapshot queue. Existing `Track::hasStopInFlightForSlot` is now consulted by cold `displaySourcePublicationPending` alongside the original Loop queue gap. The extra hold is per-slot, excludes live Record and existing published roots, and preserves prior frame/key/interval guards. No playback storage read or new runtime state/API. Nine focused cases pass; full native3071total/3064passed/7skipped/0failed,97.757s; capture build/upload SUCCESS unchanged RAM1 padding840. One focused independent review of this new edge: no scoped findings, no independent command execution claim.

Final flashed hash `6f176b90352435e54510606f2bc55496d3aed7569684712009f04af2d6650465`. USB re-enumeration interruption required owner reconnect/reupload; no content leg opened before verified postflash03 two drains. Run02 interrupted and then drained non-reset; owner identifies its visible blanking as reboot and asks to ignore it. No boot/reboot code change or issue declaration follows; that incomplete run is excluded from qualification.

Completed unchanged300BPM8/4 run03 is baseline PASS/overall_ok true/issues empty; report/outer shutdown both complete2/ok/no latch. FirstOD minimum32 preserved; next-count-progress proxy maximum65.629ms versus original412.592ms, median53.034ms; secondOD maximum67.142ms; Record ordinal maximum64.516ms. MIDI first return32/32 exactgrid/maxgap52.961ms, DMA completions progress without faults. Record initial dispatch batching3.860ms remains separate. Exact per-note pixels/deadline and24-bar/multi-target device gates remain unqualified; parent unchecked.

Causal precision: failed run01's pass publication28.392s precedes OD entry29.065371s and its DISP committed flag is1. The new pre-publication native case exposes a genuine hold gap but is not claimed as the device's exact ordering; scope is the broader no-published-display-source stop obligation. Before/after hardware observation shows preservation/improved progress without per-frame predicate telemetry. [Hardware chronology, measurements and limits](HARDWARE_QUALIFICATION.md).
