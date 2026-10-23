# I5 — private committed-root preparation before Capture-ready adoption (G2 packet)

Task: `windowed-playback-generation` 3.15b–c — root-current next-wrap handoff. Decision: **DEC-136 — private
committed-root preparation before Capture-ready adoption**.

## Scope

The held frozen mismatch Stop (real accepted pair, Stop at raw 3352, final loop 1536 = 2 bars) must retire its
bound `CaptureStopJob` while the take keeps playing. S7.5b.5 releases the job only when a **root-backed** window
(Committed origin + the current root's snapshot identity) has taken over the physical reader. That successor is
unreachable today because the ordinary rolling admission cannot express the demand on this geometry at all:

```
canCompleteWithinLead needs  leadTicks + requiredGrains() x (denied+1) x advance
                        =  768       + 5 x 8 x 64                        = 3 328 ticks
loop length                                                                 1 536 ticks
```

so *no* legal lead admits it (and even the smallest coverage demand needs 1 792). The refusal — the recorded
3.15b–c root-current `CannotCompleteBeforeHandoff` RED — discards the qualified candidate, leaves the frozen
reader serving, and strands the job. Full record:
[I5 — falsification and the proven blocker](ROLLING-RECORD-HITL-EXECUTION.md#the-consumed-work-record-fix-is-falsified-the-blocker-is-the-declared-envelope-2026-10-16).

**In scope:** one narrow, position-free preparation route for exactly this obligation (DEC-136 items 1–5).
**Out of scope:** the declared envelope and its constants, `canCompleteWithinLead` itself, any other demand's
admission, default rolling activation, a new owner/queue/view/emitter/resident field/scheduler, Track or RAM1
layout, the device image and SD contents, attempt 5 (spent only after this gate is GREEN).

## Eligibility (structural, not a bar count)

`Track::rootReturnSuccessorIsOwedForSlot(slot, request, clock)` — all four required:

| Term | Why it is required |
|---|---|
| a bound Record `CaptureStopJob` for this slot whose frozen source qualifies (`qualifiedFrozenRecordCaptureSourceForSlot`) | the obligation is *this job's* return window; without the job there is nothing to release |
| the demand's source is the **current published root** (revision, generation, published snapshot identity, `publishedSnapshotIsCurrent()`) | a root-backed successor is the only thing that satisfies S7.5b.5 |
| an owed or served **complete, non-raw Capture window built from this job's frozen token** for this loop length | the reader the successor must take over is the frozen one |
| the requested coverage is a valid window of this loop that **contains the playhead** | adoption is only useful for the imminent interval |

`Track::ordinaryAdmissionCannotServeDemand(contract, loopLengthTicks)` =
`!contract.canCompleteWithinLead(loopLengthTicks)`, evaluated against the demand's **own derived cost**. The
waiver (the lead term) applies only where this is true; otherwise the mark is cleared and the ordinary path —
including its `CannotCompleteBeforeHandoff` refusal and retention — runs unchanged.

## Transition oracle

1. Accepted held pair 728/920 → Stop 3352 → final geometry 1536; root published; frozen Capture window adopted
   at the 768 boundary (the served reader).
2. The root-return demand for the interval containing the playhead is classified owed-and-impossible
   (DEC-136 items 1 and 3), is marked position-free, and its build runs **privately** in the existing build
   role: the served window stays byte-identical (storage generation, source identity, coverage) until adoption.
3. The completed window publishes as the ready successor; the existing boundary adoption
   (`ready.coverage.startPhase == phase`, coverage contains the phase) takes it over.
4. S7.5b.5 then reads `currentRootWindowServed` and releases the frozen source exactly once; the job leaves the
   queue (`hasLiveCaptureStopJobForSlot` false) — the registered reproducer's final assertion.

## Fixtures (RED first)

| Fixture | Asserts |
|---|---|
| `rollingRecordHeldMismatchStopWithDeferredOwners` (registered, intentionally RED) | the held mismatch Stop discharges its Record job; the frozen reader serves until the root successor is adopted |
| `privateRootReturnDemandIsOwedOnlyForTheBoundFrozenReader` | the classification rule on real state: owed during the held stop; **not** owed before the Stop (no bound job); **not** owed once the source is released |
| `ordinaryAdmissionCannotServeShortLoopRootReturnWork` | the impossibility rule as a pure contract: true for the 2-bar held geometry's own cost, **false** for the same cost on a 6-bar loop, **false** for an undeclared/no-cost contract |

Containment evidence rides inside the reproducer (per-turn assertions that the served window is untouched while
the private build runs, and that the private demand carries no fabricated deadline).

## Verification plan

Focused `test_midi_input_clock_interaction` suite, neighbouring rolling/ownership suites, full native
(`pio test -e native`), `teensy41-capture-serial` and `teensy41-capture-serial-window-qualification` builds
against the unchanged RAM1 line, `git diff --check`, `openspec validate --strict`. No device action; attempt 5 is
spent only after this gate is GREEN.

## Result — implemented and native-verified (2026-10-16)

**The route.** `Track::rootReturnSuccessorIsOwedForSlot` (structural classification, DEC-136 item 1) and
`Track::ordinaryAdmissionCannotServeDemand` (the envelope's own impossibility, item 3) are the whole guard.
`stepCommittedPlaybackRetry` marks a demand that is owed-and-impossible as
`PlaybackViewPendingReason::PrivateRootReturnPreparation`, **clears its position** (no fabricated deadline) and
waives only the lead term; `committedPlaybackRetryCanProgress` tolerates the position-free owed demand so it can
start qualification; and `LoopPlaybackRuntime::beginQualifiedCoverageBuild` honours the mark (item 2). Every
other requirement stands: the candidate's identity/currency/row-count/read-units checks, the single successor
role, the sibling promise (`playbackViewAdmissionKeepsAdmittedDeadlines`, still enforced), the atomic boundary
adoption, and S7.5b.5's release edge. One interception was corrected on the way (item 5): the Record-retirement
branch no longer takes the turn while a coverage build needs service, so the admitted build is served by the
existing grain tail instead of being retired as an orphaned request — the same convention that branch's own
S7.5b.5 preparation already uses.

**Observed transition (registered reproducer, `captures`-free native run).** Tick 768: the frozen Capture reader
is adopted (cur→2, ready→none) and the qualified root candidate is admitted **privately** (`prep=Building`,
`active=1`, no position, no `T-RETRY2` refusal — the ordinary path's `CannotCompleteBeforeHandoff` no longer
fires). Tick 776: the private build completes and publishes as the **ready** successor. Tick 1536: the existing
E2 boundary adoption takes it over (`cur=0`, source origin `Committed`, `snapshotIdentity` = the published root)
— `currentRootWindowServed` — and S7.5b.5 releases the frozen source, so the `CaptureStopJob` leaves the queue.
The frozen reader is byte-identical throughout (`storageGeneration` unchanged) and the private build carries no
deadline/epoch.

**Fixtures and gates.** `rollingRecordHeldMismatchStopWithDeferredOwners` is GREEN (was the intentionally RED
registered reproducer); two new contract fixtures pass — `privateRootReturnDemandIsOwedOnlyForTheBoundFrozenReader`
and `ordinaryAdmissionCannotServeShortLoopRootReturnWork` (two-bar cost impossible, six-bar cost admissible,
undeclared contract grants nothing). Focused suite **57/57**; full native `pio test -e native`
**3 294 cases / 3 287 succeeded / 7 skipped / 0 failed** (169 s). Builds: `teensy41-capture-serial` SUCCESS
(`variables 96,192 / code 392,136 + padding 1,080`, floor 840) and
`teensy41-capture-serial-window-qualification` SUCCESS (`code 392,328 + padding 888`, floor 840). The first
rolling build **failed** the RAM1 floor (`padding 760`) because the classification's reader test was written as a
lambda whose body was emitted into ITCM; it is now a `TRACK_COLD_MEM` helper, which restored the floor (the same
remedy the RAM1 rule records for this TU). `git diff --check` clean,
`openspec validate windowed-playback-generation --strict` valid, host tooling suite
`scripts/test_accepted_sequence.py` OK (194 tests; no file under `scripts/` was touched).

**Device gate — NOT run.** `pio device list` and `system_profiler SPUSBDataType` show **no Teensy on USB**
(only the debug console and Bluetooth ports), so attempt 5 could not be spent: there is nothing to upload to and
nothing to drive. The default OFF image and the SD contents are untouched; no upload, no SD operation, no Git
delivery, no checkoff. Attempt 5 remains available for the next session with the board attached; the local gate
it was waiting on is now GREEN. Task 3.15b–c's remaining device/content/timing gates stay open, and DEC-136's
device half is explicitly unproven.

### Attempt 5 — device leg attempted, blocked by the board leaving USB (2026-10-16)

The board came back, so attempt 5 was started. **It could not be completed: the Teensy left the USB bus during the
evaluation-image upload and did not return.**

* **Image built and pinned (isolated root).** `PLATFORMIO_BUILD_DIR=<scratch>/i5-dec136-qualification-root pio run
  -e teensy41-capture-serial-window-qualification` SUCCESS (79.2 s), RAM1 line unchanged
  (`variables 96,192 / code 392,328 + padding 888`, floor 840). HEX `f40fca838b4bf3e91d3206e3b1a55be2c7c06331febffeaf7535757f904b1833`,
  ELF `4dea1e820b930df8a1007cc1883d325e8996456d02e46b9b5d94f57d85c4d0f0`. This is the first image that contains
  DEC-136 (rolling enabled).
* **No-build upload reported SUCCESS** (14.4 s, cached build, the isolated root's hex). Immediately afterwards the
  board was gone from the bus: `/dev/cu.usbmodem*` absent, `system_profiler SPUSBDataType` shows **no USB device at
  all** (only host controllers), and a repeat upload answers *"No Teensy boards were found on any USB ports of your
  computer. Please press the PROGRAM MODE BUTTON on your Teensy to upload your sketch."* (`upload-after-vanish.log`
  retained in session scratch). Bounded waits totalling ~35 s did not bring it back.
* **Consequence.** No managed `rolling_record` run was started, so **attempt 5 is still unspent** and no device
  evidence was produced. The device needs a physical re-plug/power-cycle (or the PROGRAM button) before the
  upload/run/restore sequence can resume.
* **Restore target pinned and copied to this session's scratch** (the previous session's retained images are still
  present): the default OFF image **HEX `f48aca664f60883378c427c8458f72602d4b943380f84e38bf340233fc3b0068`**
  (`<scratch>/i5-dec136-restore/firmware.hex`), so the exact pretest default can be re-flashed no-build once the
  board answers.
* **Honest risk, stated not diagnosed.** The only difference against the image that previously booted on this board
  (the attempt-4 qualification image `919e17e6…`) is DEC-136 plus the diagnostic-tag removal, and the flash reported
  success before the vanish. So two explanations remain open and neither is claimed: a host/loader-side event (the
  board simply needs re-plugging), or a boot fault in this new image. **If the board boots `f40fca83…` normally
  after a re-plug, the event was host-side; if it vanishes again on a second upload of that exact image, treat the
  image as a boot-fault suspect and bisect against `919e17e6…` before spending attempt 5.** Nothing about the local
  gates changes: focused 57/57, full native 3 294 / 3 287 succeeded / 7 skipped / 0 failed, both builds inside the
  RAM1 floor.

* **Resume kit (this session's scratch, all three images verified):**
  `i5-dec136-qualification-root/teensy41-capture-serial-window-qualification/firmware.hex` = the DEC-136 rolling
  image `f40fca83…`; `i5-dec136-bisect/attempt4-known-boots.hex` = the known-booting attempt-4 image `919e17e6…`;
  `i5-dec136-restore/firmware.hex` = the default OFF image `f48aca66…`. Resume order: re-plug the board → upload
  `f40fca83…` (if it boots, run the managed `rolling_record` preset as attempt 5) → restore `f48aca66…` with a
  fresh managed postflight. Keep `919e17e6…` in reserve for the bisect only.

### Attempt 5 on the board — the route runs and its leg passes, but the *next* take's live growth stalls (2026-10-16)

The board came back and attempt 5 ran. **The DEC-136 route does execute on the device — the growth leg passes — but the
preset stalls in its *empty* leg's live observation, so the held leg was never reached.** Control runs on the
attempt-4 image establish that the recipe itself is sound. Details, all from host-side capture only:

* **Boot/loader facts (resolving the earlier vanish).** A second no-build flash of the *same* HEX `f40fca83…` brought
  the board up in 3 s, so the vanish was host/loader-side, not a boot fault in the image. The reliable no-build flash
  path in this workspace is PlatformIO's `-t nobuild -t upload` with the pinned hex in the env's build dir (log shows
  `Uploading …/firmware.hex`, no `Compiling`/`Linking`). `teensy_loader_cli` **cannot** be used here: it fails with
  `error reading intel hex file … HEX parse error line 65553` on *every* hex this project produces, including a fresh
  one, so it is a tool limitation, not a corrupt artifact. A plain `pio run -t upload` on a foreign hex is also unsafe:
  it relinked and overwrote the pinned file (observed once, hash changed to a fresh build).

* **DEC-136 image `f40fca83…` (rolling enabled), three managed runs**
  (`captures/i5-dec136-held-attempt5-01…-05`): run 1 (`--boot-reset`) stopped in *preparation*
  (`ClearStatus.REFUSED` while the boot sweep was still running — the clear was attempted ~5 s after boot);
  runs 2 and 4 (`--no-boot-reset`) and run 5 (`--boot-reset`) all show **growth PASS with
  `stop_outcome = corrected_frozen_handoff`** and then **`RuntimeError: rolling_observation_condition_timeout` in the
  empty leg's *live* observation** (`observe.until(recording and framePeriod == 2304 …)`, 20 s). The failing leg's own
  observations are unambiguous: `recording=1, rows=2, framePeriod=0` for the whole window — the device accepted the
  fixture's notes but **never grew the live rolling frame**, so the condition cannot hold. The gate-refusal
  distribution is unchanged from the control (`win_track_not_playing` 34/38, `win_window_ineligible` 10/10,
  `win_not_servable` 4/4) — my patch adds no new refusal term.

* **Control on the attempt-4 image `919e17e6…`, two managed runs**
  (`captures/i5-dec136-bisect-attempt4image-02/-03`, same recipe, `--boot-reset`): **growth PASS and empty PASS**
  (`corrected_frozen_handoff` both) and then the **held leg FAIL** with
  `stop_handoff_unproven: ['rolling_snapshot_unqualified_source' ×4]` — i.e. the documented attempt-4 held-Stop
  symptom reproduces on this harness, and the control's observation table shows the same fixtures growing
  `768 → 1536 → 2304` normally. (One earlier control attempt failed at `boot_handoff_unproven` because it attached
  after the boot banner; the handoff gate needs a fresh boot inside the capture, which `--boot-reset` provides.)

* **Reading, and what it means.** The only source delta between the two images is DEC-136 plus the diagnostic-tag
  removal, and the asymmetry is control-empty-PASS 2/2 vs DEC-136-empty-stall 3/3 with identical recipes, fixtures and
  harness. The route therefore appears to leave state that breaks the **next take's** live window growth — the symptom
  shape (notes accepted, no window period, no new gate term) is what a role/source held by an unreleased successor
  would produce after the slot clear. **This is a hypothesis, not a diagnosis:** the sequence that exercises it
  (root-return Stop → slot clear → next take's live growth) is exactly what my native fixtures do *not* cover, because
  they test one take at a time. The next step is a native cross-take fixture (Stop through the route, clear the slot,
  then require the following take's live frames to grow) to reproduce it locally before touching the board again.
  **Nothing in this section closes or contradicts the local result** (focused 57/57, full native 3 294 / 3 287 /
  7 skipped / 0 failed, both builds inside the RAM1 floor): the defect, if confirmed, is a cross-take release gap in
  the new route, not a flaw in the classification/waiver itself.

* **Device restored to the exact pretest default.** HEX `f48aca664f60883378c427c8458f72602d4b943380f84e38bf340233fc3b0068`
  flashed no-build (verified hash, no compile/link, port back in 7 s); read-only postflight
  `captures/i5-dec136-restored-postflight-01` reports `post_restore_free_chunks=397`, `admission=1 interactive=1
  undo=1 stopped=1 deferred_prewarm_idle=1`, readiness attested, `reconcile_warnings=0`, walk flat `480 → 480`,
  `S2b.3 free=397 vs required 16 → met=True`. The probe is attach-only, so `boot_banner=False` is expected: the boot was
  the flash itself. No SD write, clear, or other destructive operation was performed; the authorized scratch slots
  (track 8 slots 4–5) are the only content touched, by the scenario's own clear/record gestures.

* **Attempt ledger, stated plainly.** Three managed runs were spent on the DEC-136 image (one preparation refusal,
  two growth-PASS-then-empty-stall) and three on the two control images (~all growth legs executed; the **held leg
  never ran on the DEC-136 image**). Attempt 5 is therefore **not a completed content evaluation**: the held Stop
  remains device-unverified, and no claim is made about device-level success of DEC-136.

### Release-edge completion — the early retirement and the discarded ready successor, fixed natively (2026-10-16)

The G0 investigation of the same day traced why a held Stop's bound job releases early and the
private successor's ready view is discarded: **every hold in `Track::stepDeferredRecordRetirement`
predates DEC-136's position-free demand.** While the route is owed, its successor is invisible to
each of them — in flight it is a *coverage-owned* build (not a *frozen-window* build, so the stall
counter never counts it and `releaseFrozenRecordWindow()` answers a coverage-owned build with
"nothing frozen to release"), and once complete it waits **unpublished** in the ready role as a
**Committed**-origin window (the Capture-origin hold does not apply), while the S7.5b.5
served-window holds are keyed on an audible slot and on the served view's own identity — which the
stop-correction transient breaks (the served pre-correction view's geometry no longer matches the
corrected loop). A retirement grain landing in that gap clears `hasBoundRecordReturnWindowForPendingJob()`,
the predicate the route's classification, the E2 boundary adoption and the release edge all read —
stranding the unadopted successor in the single ready role so the slot can never take a root-backed
window again. That is the attempt-5 cross-take stall shape (empty-leg live growth never starting),
recorded there as hypothesis and now closed at its structural root.

**The fix (one bounded hold, in DEC-070's own stall shape).** `stepDeferredRecordRetirement` now
holds the job while the root-return successor is owed **and the reader is not yet root-backed**:

* In flight — the lane's only admitted build that carries **no position at all** (item 2: no
  fabricated deadline) while the frozen reader serves; launch preparations and the cold
  requested-frame route are excluded by their own state.
* Complete but unadopted — an unpublished, non-raw **Committed**-origin ready of the current
  published root, on this loop's geometry, waiting at its boundary.
* The reader-takeover gate uses exactly the terms `currentRootWindowServed` reads below it, so the
  hold and the release edge can never disagree about whether the root arrived; once a current-root
  window serves, an unpublished ready is the slot's next *ordinary* successor and does not hold the
  job (the first implementation missed this and the existing fixtures caught it — below).
* The hold is **counted only while no adoption boundary can come** (the transport stopped). While
  the slot is audible, the boundary is the normal wait and the count stays zero. An expired count
  (unchanged successor, no boundary possible) tears the stuck successor through the build's own
  `discardCoverageBuild()` + `releaseFrozenRecordWindow()` and lets the job retire — so a successor
  nothing can advance still releases the job instead of parking its raw range, its capture-stop
  owner and its ring slot forever.

State: two new externally allocated per-slot runtime fields (`rootReturnHoldKey`,
`rootReturnHoldGrains`) and `kRootReturnStallGrainLimit = 512` in the retirement lane. No RAM1 cost
(`sizeof(Track) == 384` untouched; the default build line is byte-identical); both ABI pins updated
with measured comments — target runtime 7,872 → **7,880 B**, host 10,056 → **10,064 B**.

**The oracle, fixed before the behavior edit:** registered
`rollingRecordHeldStopHoldsOwedRootSuccessorAcrossTransportStop` drives the real held mismatch Stop
(On@728/Off@920, raw 3352 → final 1536), publishes the corrected root, drives the route to its
**complete-but-unadopted** ready successor, then lands the transient the holds do not cover — a
transport stop inside the correction window — and requires: the job still bound (`stopResponsibility`
= 1), the successor keeping the ready role and its storage generation, and finally the job still
**releasing within the bounded budget** (a successor that can never be adopted must not park the
job). It was RED pre-fix exactly at *"a retirement grain in the transport-stop transient must not
retire the bound job while its root successor is unadopted"*, GREEN with the fix.

**The first fix iteration was too broad, and the existing fixtures named both halves.** Holding for
*any* complete unpublished Committed ready of the current root never released the job
(`privateRootReturnDemandDoesNotOutliveItsJobIntoNextTake` FAILED: after adoption the slot's *next
ordinary* successor matches those terms), and a 512-grain budget counted through a *legitimate*
boundary wait — up to one full loop period — and tore a successor mid-wait (the registered
reproducer FAILED: *"the completed root-backed successor must take over the reader"*). Both were
repaired by the reader-takeover gate and the only-while-no-boundary-can-come count, which is why the
hold cannot fire after the root-backed window serves and cannot expire an audible slot's normal wait.

**Verification (all this session):** focused `test_midi_input_clock_integration` **61/61**; full
native **3,298 cases / 7 skipped / 3,291 succeeded / 0 failed** (baseline 3,294 + 3 follow-up cases +
this fixture); builds `teensy41-capture-serial` `variables 96,192 / ITCM 393,216 (code 392,136 +
padding 1,080)` — **byte-identical to the recorded DEC-136 baseline** — and
`teensy41-capture-serial-window-qualification` `code 392,280 + padding 936 ≥ 840` (96 B headroom),
both SUCCESS; mutation control (`false &&` on the hold) kills **exactly** its own case (1 failed /
60 succeeded), restored byte-identically (`cmp`), 61/61 re-verified; `git diff --check` clean;
`openspec validate windowed-playback-generation --strict` valid. No new firmware markers.

**Still open, stated plainly:** the device gate is NOT run — the board is on the exact default
`f48aca66…`, no upload, no SD action, no Git, no checkoff. The attempt-5 empty-leg stall (next take's
live growth never starting) is *not* reproduced by any existing native fixture — the three cross-take
cases pass on both images — so if it recurs on the device it is a *different* residue than this one;
the release-edge gap that plausibly produced it is now closed and bounded. Held Stop remains
device-unverified; attempt 5 stays available as the device leg for this route plus this completion.

### Device leg (attempt 7) — the route completes on hardware; the held-leg oracle's own contract is the remaining red (2026-10-17)

Owner authorized the attempt-5 device leg "for the route plus this completion". Two facts had to be recovered first,
because an undocumented prior session had already spent three runs (recovered from captures only; no journal entry
existed): `captures/i5-dec136-fix-attempt6-01…-03` (23:29–23:35) ran a pre-completion image — HEX `f11444ab…`, git
`26125ceb`+dirty, qualification env — whose source predates the release-edge fix (my edits timestamp 00:04–00:18).
Their matrix: growth PASS + empty PASS (`corrected_frozen_handoff`) — the attempt-5 empty-leg stall did not recur on
that image either — then **held FAIL `stop_responsibility_unproven` ×4**, and their restore+postflight
`i5-dec136-fix-restored-postflight-01` (23:37) left the device on the default. So the completion's device leg was
genuinely unspent, and that pre-completion held signature is the device-side red for the early-release class.

**This leg.** Image built isolated (`$scratch/i5-relfix-qual`): `teensy41-capture-serial-window-qualification`
SUCCESS, RAM1 `code 392,280 + padding 936 ≥ 840`, HEX **`b83c7f6d…`** / ELF `67173a88…` (a fresh isolated rebuild;
RtcTime compile-time data means no byte-identity claim against the earlier shared-cache build of the same tree).
No-build upload SUCCESS (12.2 s, hash verified before and after, no relink).

* **Run `captures/i5-dec136-releasefix-attempt7-01`** (attach ~15 s after the flash): the capture caught the boot
  (`DIAG,boot_reset`, handoff rows + DFRAME — the boot gate passed) and the growth leg's scoped clear was
  **`ClearStatus.REFUSED`** → `scoped_record_preparation_unconfirmed` — the documented early-after-boot storage
  admission refusal (same class as attempt-5 run 1 and the prior session's run 01, which also ran ~seconds after a
  boot). Clean teardown: complete/2 drains/ok, no latch. Not a firmware result.
* **Run `captures/i5-dec136-releasefix-attempt7-02`** (attach on the settled device; the boot gate's own rule —
  no banner in window ⇒ `not-fresh-boot` ⇒ proceed — is what the prior session's passing attach runs used):
  **growth PASS and empty PASS (`corrected_frozen_handoff` both)** — the empty-leg cross-take stall is gone on the
  completion image too — and the **held leg reached its full content verification** (wire inventory exact 1 On/Off
  pair, storage↔effective parity 2/2 rows, snapshot sequence qualified through the take) before
  `stop_handoff_unproven: ['stop_responsibility_unproven' ×4]` ended the leg. Shutdown complete/2 drains/ok, no latch.

**The held leg's observation table is the device proof that the route + completion ran end-to-end.** Stop at tick 3112
(raw 3392 → final 1536); before it the live Capture window served (`cur=2/29`, origin Capture, token = live token,
published, complete, frame Serving). After it:

* **OBS0/OBS1 — job bound, frozen reader serving**: `stopJob=1`, `cur=2/29` (origin Capture, token 20 = the take's
  token, published, complete, rows 1) — but the **rolling frame is Idle** (`phase=0`, `bound=255/0`): the mismatch
  Stop's truncation rewind reanchored the projection and unbound the frame, and the frozen content is served by the
  same view through the reuse branch (`adoptFrozenRecordWindow`'s same-source re-identification), so the
  role/storage change the oracle's `stop_corrected_successor_not_observed` term looks for never materializes here.
* **OBS2 onward — root-backed takeover, then release**: `cur 2/29 → 0/32`, **origin Committed**, published,
  complete, rows 1, ledger opening/closing with playback (the held note's On each wrap), and `stopJob=0` — the
  designed S7.5b.5 release **after** `currentRootWindowServed`, not before it. OBS3–12: committed content serving,
  job stays retired. **No stranding, no early release, no stall** — the attempt-4 permanent stall is gone on hardware.

**Why the registered oracle still fails — a contract that predates the route, not a firmware defect.** The held-leg
`stop_handoff` was written against the attempt-4 world, where the frozen-serving-bound state was terminal (nothing
could ever move past it). On the route image every one of its terms is contradicted by designed behavior: (a) during
the bound phase the truncation rewind leaves the frame Idle, so `qualify`'s Serving/bound-role terms cannot hold —
the same `rolling_snapshot_unqualified_source` shape attempt 4 produced, now transient; (b) the corrected-frozen
"changed role/storage" term is never met because the reuse branch serves the frozen content in the same view; (c)
after the root-backed takeover `stopJob=0` by design, so every later sample raises `stop_responsibility_unproven`.
The growth/empty legs pass the same oracle because their matching-boundary stops do not rewind the projection.

**Not done, deliberately:** the oracle was not edited to accept the route's terminal state — that is a gate change
under the standing no-weakening rule and needs an owner decision (update the held-leg contract to the DEC-136-aware
terminal state with its own discriminating change, or keep it and accept the held leg as not provable by this oracle
on the route image). No firmware was changed by this leg.

**Restore and health.** Device restored to the exact pretest default HEX **`f48aca66…`** (no-build upload SUCCESS
17.3 s, hash verified before and after; the first attempt's scons configuration pass cleaned the foreign hex before
uploading — retry with the scons db present flashed cleanly). Fresh postflight
`captures/i5-dec136-releasefix-restored-postflight-01`: boot banner true, reconcile warnings 0, free chunks 397,
walk flat 480→480, readiness attested, S2b.3 met — matching the prior restored baseline exactly. No SD write or
cleanup, no Git, no default activation, no checkoff; slots touched only the authorized track 8 slot 4.

**Attempt ledger for this leg:** two managed runs (7-01 preparation refusal, 7-02 full matrix through the held leg).
Held Stop is device-verified at the content level by this leg's evidence, and the registered held-leg stop-handoff
oracle was updated under the owner's decision — see § Held-leg contract update below. 29/60.

### Held-leg contract update — the owner decision and the DEC-136-aware Stop oracle (2026-10-17)

The attempt-7 red was a **gate-contract** red, so it went back to the owner rather than being edited in place
(`docs/Authority/IMPLEMENTATION_READINESS.md` — never weaken an assertion to pass). Owner decision: **update the
held-leg contract to the DEC-136-aware terminal state, with its own discriminating change**. Implemented as
[DEC-137](../../../../../docs/DECISION_LOG.md#dec-137--the-held-leg-stop-oracle-is-dec-136-aware-the-frozen-serving-phase-the-root-return-and-the-release-edge);
no firmware, build, upload, SD or device action, and the device stays on the exact default `f48aca66…`.

**What changed.** `scripts/hitl/verify/rolling_record.py::stop_handoff` now reads the **ordered post-stop sample
sequence** instead of one candidate sample. The matching-Stop branch is unchanged. A mismatching Stop is first
scanned for the unchanged `corrected_frozen_handoff` (job bound, corrected frozen content served across the
playhead from a different physical view), which is how the growth and empty legs still pass. Otherwise the leg
passes only with the new `corrected_frozen_root_return`: a frozen serving phase (job bound, published/complete
Capture-origin window carrying **the take's own token** and the frozen geometry; the frame may be reported
unbound because the truncation rewind re-anchors the projection while the reuse branch keeps the same view),
**followed by** a released job serving Committed-origin content at the frozen geometry that is actually being
read (cursor/ledger advancing). `full_recipe_complete` additionally requires the **held** leg to reach that
terminal. `scripts/hitl/scenarios/rolling_record.py` passes the whole sequence, requires the released samples to
serve the take's own accepted content (`root_return_serves_foreign_content`) and counts the root return as a
mismatching Stop outcome.

**Discrimination, measured against the retained device evidence.** Replaying the recorded traces through the new
contract:

| Image (capture) | growth | empty | held |
|---|---|---|---|
| route + completion `b83c7f6d…` (`i5-dec136-releasefix-attempt7-02`) | `corrected_frozen_handoff` | `corrected_frozen_handoff` | **`corrected_frozen_root_return`** |
| pre-completion route `f11444ab…` (`i5-dec136-fix-attempt6-02`, `-03`) | `corrected_frozen_handoff` | `corrected_frozen_handoff` | **`corrected_frozen_root_return`** |
| pre-route stranding `919e17e6…` (`i5-rolling-record-approved-full-04`, `i5-dec136-bisect-attempt4image-02`, `-03`) | `corrected_frozen_handoff` | `corrected_frozen_handoff` | **RED `stop_root_backed_successor_not_observed`** |

The stranding rows are the point: on `919e17e6…` the job stays bound for the whole 5.5 s observation window
(`stopJob=1`, the frozen Capture window serving with the take's token, `boundRole=255/0`) and no Committed
successor ever takes over, so the held leg stays red for the right reason on all three of that image's captures.

**Stated limit (recorded, not implied).** The pre-completion route image and the completion image produce the
**same** held-leg table at the harness's sampling resolution (~two bound samples, then the Committed takeover
with the job released), so this oracle does **not** discriminate the release-edge completion itself. That
discrimination stays with the native reproducer
`rollingRecordHeldStopHoldsOwedRootSuccessorAcrossTransportStop` and its transport-stop fixture; the device leg
proves the route's terminal sequence, not the tick-level hold window.

**Verification (host only).**

* `scripts/test_rolling_record.py` gains `test_stop_handoff_dec136_root_return_and_its_discriminators`: the
  compressed route trace (bound reuse-phase samples followed by released Committed samples) must return
  `corrected_frozen_root_return`, and seven named refusals are pinned (stranding, early release while the frozen
  Capture content serves, release before the frozen phase, unqualified successor, successor geometry not the
  take's, a released window that is not being read, no frozen receipt while bound, no observation, still
  recording). Focused suite **16/16**.
* **Mutation controls** (each mutation kills exactly its own case(s), files restored byte-identically with
  `sha256` verified — verifier `55239da8…` at the mutation run, `cac5c35b…` after the review's docstring correction, which changed no behaviour): dropping the release requirement → the stranding case (and the
  reuse-shape case) fail; accepting a release while Capture content still serves → the early-release case fails;
  dropping the progression term → the non-serving case fails; dropping the held-leg requirement from
  `full_recipe_complete` → the recipe case fails.
* Neighbours: `test_rolling_record` + `test_hitl_import_gate` + `test_hitl_stimulus_contract` **54/54**;
  `test_accepted_sequence` **194/194** (unchanged).
* Full host sweep `unittest discover` over `scripts/test_*.py`: **1 818 tests / 7 failures / 3 errors**, against a
  **measured pre-change baseline of 1 817 / 7 / 3** built by reverting these three files — the failing-test set is
  **identical** and the one extra test is the new case. None of the ten failing tests is in this change's
  modules (their causes: the firmware-source inventory pins in `test_compatibility_consumer_inventory`, the
  legacy verifier's `overdub_step_clocks`, `occupy_paint` pass shorten/hide, a stale `PRESET_SCENARIOS` pin, a
  managed-capture boot timeout, a discovery-path import, and a legacy edit-session anchor).
* Whitespace clean, all three files compile, strict OpenSpec validation re-run after this packet edit.

**What this does *not* change.** The full recipe is still not `PASS`: `full_recipe_complete` also requires the
`matching_physical_reader_preserved` outcome, and **no leg in this recipe produces it** — all three stops are
mismatching (raw `6488/6480/3392` → final `2304/2304/1536`). That "reachable matching physical Stop" coverage is
the separately named open item, untouched by this contract update and by the held leg's now-green terminal.
Held-leg device verdict, task checkoff, the 29/60 progress count, the installed image and the SD contents are all
unchanged. No Git delivery.

### Focused independent review of the contract update (2026-10-17)

One read-only review pass over the three files plus the retained captures (no second review loop). It
independently decoded the recorded rows and confirmed the design: the mismatch classification, the origin
constants against `PlaybackWindowOrigin` (`Committed=0`/`Preview=1`/`Capture=2`), the `captureToken`-zero
contract for non-Capture origins, the index/ordering logic (no off-by-one; `bound`/`released` disjoint), the
degenerate-input behaviour, the per-sample precondition (nothing the scenario legitimately records is
rejected), the cross-file/test agreement with the recorded rows, `root_return_serves_foreign_content` (the held
leg's released endpoints `(728,1)` are in its accepted dump), and that no other module reads the verifier or
the removed `qualified_after` field. Its findings and their dispositions:

1. **The ordered scan labels a leg by what is seen first.** The reviewer showed the recorded *empty* leg
   satisfies both terminals (a corrected-role frozen sample, then released samples) and is labelled
   `corrected_frozen_handoff` by scan order. **Accepted as designed, documentation corrected:** the corrected
   handoff never carried discharge evidence (the pre-route contract required the job still bound there), the
   recorded growth/empty legs are labelled exactly as they were before this change, and the gate's new
   held-leg requirement is the root return. The `stop_handoff` docstring now says this instead of implying
   discharge on both paths.
2. **Two false-negative surfaces, both erring safe.** (a) If the release lands before the first post-stop read,
   no bound sample exists and the leg fails `stop_frozen_serving_not_observed` even though its released phase
   is fully qualified; (b) with exactly one released sample the progression test has nothing to compare and the
   leg fails `stop_root_return_not_serving`. Both are distinct, named refusals (not confusable with the
   stranding term) and both are re-runnable — the scenario reads the earliest available sample after the Stop
   and then ~10–16 samples over 6 s, while the retained route releases after ~1 s with two bound samples
   observed. **Not changed:** accepting either shape without the evidence would weaken the terminal. Recorded
   here as the contract's timing sensitivity: a missed observation reads as "not observed", never as a pass.
3. **The post-window drain injects a realtime transport Stop the contract cannot see.** `drain_record_evidence`
   runs after the 6 s observation window, so a held leg that released its job only *on a transport stop* would
   be refused with the stranding term. The retained route releases on its own ~1 s after the Record stop, so
   the current evidence is unaffected; DEC-136's transport-stop stimulus is the *native* reproducer's
   (`rollingRecordHeldStopHoldsOwedRootSuccessorAcrossTransportStop`), not this leg's. **Not changed** —
   extending the window past the drain is a scenario redesign with its own evidence flow, and it is not needed
   for the attempt-7 verdict. Recorded as a coverage limitation.
4. **The next coverage item's structural obstacle.** `full_recipe_complete` rejects anything but exactly three
   legs, so the obvious way to supply the missing `matching_physical_reader_preserved` coverage — add a
   matching-stop leg — would make the recipe return `False` by construction, and if the *held* leg ever became a
   matching stop no recipe could pass while no report field names which coverage is missing. **Not changed**
   (out of this decision's scope); recorded as the named constraint on the matching-physical-Stop item, which
   must therefore be closed by its own change to the leg set and the recipe predicate together.
5. **Diagnosability note.** The refusal is now one named term rather than the old list of four (which was four
   copies of `stop_responsibility_unproven`), and an aborted leg leaves `functional_legs_ok` /
   `matching_stop_exercised` / `mismatching_stop_exercised` **absent** rather than `False` (visible in the
   retained stranding captures, and pre-existing). **Accepted as-is:** the full per-sample sequence is retained
   in `leg['stop']['observations']`, and the named terms say which phase failed.

No finding changed a verdict. After the docstring correction the retained traces were replayed (identical
results in the table above) and the focused suite re-run green (**16/16**).

### The matching terminal and the matched leg (2026-10-17)

With the held leg green, the recipe's remaining requirement was `matching_physical_reader_preserved`, and the owner
asked for it to be fixed rather than declared out of scope. It turned out to be **two** defects
([DEC-138](../../../../../docs/DECISION_LOG.md#dec-138--the-non-truncating-stop-terminal-is-the-preserved-reader-and-the-recipe-covers-each-stop-class-from-its-own-leg)):

1. **No producing leg.** All 23 recorded device stops across 12 captures are truncating (`raw_length` ≈ 6400 for the
   eight-bar legs and ≈ 3400 for the held leg, against `final_length` 2304/1536), because every leg joins its fixture
   stream *before* pressing Record-stop: the join + read latency (~330 ticks) puts the press past the take's own
   wrap, and `raw_length` is the take's extent at the press.
2. **An unsatisfiable terminal.** The matching branch compared `epoch`, `frameOrigin`, `framePeriod`, `boundRole`,
   `boundStorage`, `currentRole`, `currentStorage`, `cursor` and `ledger` for equality. Measured on the recorded
   stops: `epoch` advances by one **every** time and the positional frame is **cleared**
   (`frameOrigin`/`framePeriod` 0, `boundRole` 255, `boundStorage` 0) until a successor is adopted, while
   `(currentRole, currentStorage)` is unchanged in 8 of 9 stops inspected (the ninth's first post-stop sample was
   already past a rebuild). The equality form therefore could not hold on hardware, whatever the firmware did — the
   recipe was unreachable by construction.

**What changed.**

* **Contract (`scripts/hitl/verify/rolling_record.py`).** The non-truncating terminal now asserts what the device
  guarantees: the era advanced (`after.epoch != before.epoch`), the **same serving view**
  (`currentRole`/`currentStorage`), the take's own frozen content (`period == final`,
  `captureToken == before.liveToken`, Capture origin, published, complete), and a **consistently cleared** frame
  (`boundRole == 0xFF` with zero `boundStorage`/`framePeriod`) or, if the frame is still bound, the ordinary serving
  terms. Cleared/reset fields are no longer compared. Named refusals: `stop_matching_era_not_advanced`,
  `stop_matching_reader_not_preserved`, `stop_matching_frame_inconsistent`, `stop_frozen_geometry_or_token_mismatch`,
  `stop_responsibility_unproven`.
* **A `matched` leg (`scripts/hitl/scenarios/rolling_record.py`, `LEGS`).** Three streamed bars with the growth
  fixture (last note inside bar 3, extent 1832); the leg observes the take's first window and then the **pre-wrap
  third window** of the same cycle (`1950 <= phase <= 2150`) and presses the Record stop **there, before joining the
  stream** — so the take's extent never passes its bar-aligned loop length and the Stop is non-truncating. Its claim
  is the Stop class only: the replay window is a single cycle, so the scheduled inventory and interval rotation stay
  with growth/empty, recorded in the report (`wire_inventory`/`wire_schedule` carry an explicit `scope`, never a
  silent skip).
* **Predicate.** `full_recipe_complete` is leg-attributed and the leg set is membership, not a count: `growth`,
  `empty`, `held`, `matched` must all be present and passing, `held` must reach `corrected_frozen_root_return`,
  `matched` must reach `matching_physical_reader_preserved`, and at least one of `growth`/`empty` must reach
  `corrected_frozen_handoff`; extra legs are allowed and must pass. The scenario records
  `stop_terminals_by_leg`, so a non-PASS names which coverage is missing instead of leaving it implicit.
* **Stimulus.** `STIM-ROLLING-RECORD-TEMP-01` declares the new claim (`matching_physical_stop`) and names the
  non-truncating Stop class in its description; its authorization is unchanged (track 8, slots 4–5).

**Host evidence.**

* **The terminal is now reachable, and still discriminating.** A designed matched trace built from the measured stop
  shape returns `matching_physical_reader_preserved`; the same trace with a truncating receipt is refused
  (`stop_root_backed_successor_not_observed`).
* **The recipe can pass, and cannot be borrowed.** A four-leg recipe assembled from the real attempt-7 evidence
  (`growth`/`empty` `corrected_frozen_handoff`, `held` `corrected_frozen_root_return`) plus a matched leg yields
  `full_recipe_complete == True`; the same recipe without the matched leg yields `False`.
* **Retained traces unchanged:** attempt7-02 and attempt6-03 give growth/empty `corrected_frozen_handoff` + held
  `corrected_frozen_root_return`; the pre-route stranding image `919e17e6…` still stays RED
  (`stop_root_backed_successor_not_observed`).
* **Gates:** focused **18/18**; five mutation controls (era term, reader-identity term, cleared-frame consistency,
  matched-leg requirement, held-leg attribution) each kill exactly their own case(s), files restored byte-identically
  (`sha256`); `test_rolling_record` + `test_hitl_import_gate` + `test_hitl_stimulus_contract` green; full host sweep
  **1 820 / 7 failures / 3 errors** with the **same failing set** as the measured pre-change baseline (the two extra
  tests are the new ones). `openspec validate --strict` valid, `git diff --check` clean.

**What is owed — evidence, not code: one device run.** No board was attached in this session, so the matched leg has
never run. The recipe's `PASS` now depends on it, and the leg's own timing is a **derived hypothesis**, labelled as
such: it assumes the device's `raw_length` at the stop is the take's extent (measured: ~330 ticks past the streamed
extent) and that its `final_length` is the bar-aligned extent of the accepted notes (measured: 2304 for notes ending
in bar 3, 1536 for the held take). If the press lands past the wrap, the receipt is truncating and the leg refuses by
name; if the device rebuilds the reader at a non-truncating stop, it refuses `stop_matching_reader_not_preserved`.
Either outcome is a named answer, and the run's report carries `stop_terminals_by_leg` plus the leg's own observation
sequence. Two of the six attempt budget remains and the qualification image is retained
(`$scratch/i5-relfix-qual/…/firmware.hex`, HEX `b83c7f6d…`); the device stays on the exact default `f48aca66…` and
the SD contents are untouched.

**Device leg executed (2026-10-17, same day; six managed runs, `captures/i5-dec138-matched-attempt8-01…-06`).** The board
was attached, the retained qualification image `b83c7f6d…` was flashed with a no-build upload (14.3 s) and every run
attached on the settled device (`--capture-boot-wait 15 --no-boot-reset`). What the runs established:

| run | result |
|---|---|
| `8-01` | growth leg `rolling_observation_condition_timeout` — **harness defect**: the Stop press had been left only in the `matched` branch, so the other legs waited for a Stop they never sent (no `Button press: Record/Overdub` row on the wire). Fixed: one press site on every leg path, pinned by `test_every_leg_path_presses_the_record_stop_once`. |
| `8-02` | all four legs ran; growth/empty/held `corrected_frozen_handoff`, held `root_return_completed=True`; matched **truncating** (raw 5696 → final 2304). Measured: a host-observed press lands **270–590 ticks** late — more than the slack inside a take. Also showed the held leg serving the frozen content through a **corrected-role view first and still completing the release**, which is why the held requirement became the `root_return_completed` fact. |
| `8-03` | growth/empty/held PASS (held `corrected_frozen_root_return`); matched timed out in its pre-stop observation — but the **grid-scheduled press worked**: the take stopped at take tick **1360 inside a 1536-tick loop** (non-truncating). |
| `8-04` | growth/empty/held PASS; matched stopped at take tick **2080 inside a 2304-tick loop** (non-truncating) yet still classified truncating, because a **growing** take's observed `period` lags (1536 vs final 2304) — the classifier's `before.period == final` can only hold when the loop length is known before the Stop. Hence the **one-bar** matched take now in the tree. |
| `8-05` | matched failed `quiet_replay_current_token_stale` — the quiet-replay currency check ran over post-begin reads of a growing take (harness defect, fixed by scoping the check to the legs whose claims read the served take). |
| `8-06` | growth timed out with **no rolling window at all** (`period 0`, `lastTick=0xFFFFFFFF` on every sample): the device was degraded after six consecutive runs. Stopped. |

**Restore and health.** The exact default image `f48aca66…` was re-flashed (no-build, SUCCESS 11.7 s, hash verified) and a
read-only postflight (`captures/i5-dec138-restored-postflight-01`) shows the boot banner, **reconcile warnings 0**,
`free=397` with **S2b.3 met**, readiness attested and a complete library observation (64 slots, 8 initial playback, 23
committed at the checkpoint). No SD cleanup, no quarantine, no Git delivery; only the authorized track 8 slot 4 was
written.

**Verdict.** The non-truncating Stop **class** is reachable on the device with a grid-scheduled press (two runs), and
three real harness defects were found and fixed by the hardware that no host test had caught. The **recipe has not
passed**: the one-bar design has not yet had a clean run — its two runs were blocked by a harness check (8-05) and by the
degraded device (8-06). Next: one managed run of the one-bar matched leg on a settled device; the six managed content
attempts of this session are spent, so that run needs a fresh approval.
