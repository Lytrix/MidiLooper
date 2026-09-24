# Deliverable Tracking (Main vs Refinement)

> Project goal and decision log: [Authority/PROJECT_INTENT.md](Authority/PROJECT_INTENT.md). This page is the single shipped-vs-next overview.

## Working state — 2026-09-24 (branch `feature/runtime-working-set-ownership`, nothing new on `dev`)

**`deferred-slot-restore-liveness` is CLOSED and ARCHIVED with a reduced, accurately described scope; its
runtime measurements moved back to `runtime-working-set-ownership`.** Delivered: restore **preparation**
advances under active transport (`waiting_capture` for recording, `waiting_commit_gate` / `waiting_job_capacity`
for the global commit gate); requests carry identity `(track, slot, requestGeneration, setEpoch)` with named
waits distinct from four terminals and pre-publication stale-identity rejection; the census checkpoint is
finished and device-verified; content playback passes (11/11 cycles `inventory_equal` and `exact_order`); and
CLEAR's **permitted completion branch** is verified (shape (b): completion through workspace-save priority
while a started restore remained commit-gated). **Explicitly NOT delivered — deferred to
`runtime-working-set-ownership` S2b.7 and bound by `resumable-midi-boundary-playback-maintenance` task 3.3:**
drain-driven restore advancement and active-transport commit safety. The set-change epoch residual was
deferred to `runtime-working-set-ownership` S2b.8 and **landed there 2026-09-24** (see below). Evidence is in
the change's `REVIEW.md`; the runtime status is in `PROJECT_STATE.md` / `CURRENT_WORK.md`.

**Landed 2026-09-24 in `runtime-working-set-ownership` — S2b.8 external resets (native only).**
Undo-clear and redo-clear are decided to **be external resets** and now advance the restore-request
generation, so an in-flight restore job cannot publish a parsed payload over content the user just restored or
re-cleared. `noteRestoreSetEpoch` was corrected to record `superseded` under each request's **original**
identity before advancing the epoch and to emit its `RRQ` transition (two real defects: the record was
re-stamped with the new epoch and read as *current*-but-terminal, and the reason was never emitted); the dead
non-emitting `invalidateAllRestoreRequestsAsSuperseded` was removed. Recorded, not faked: **no runtime
set-change entry point exists**, so none was invented (the set boundary is boot's full record reset), and
"a different owner committing that slot" already terminates as `unnecessary` plus pre-publication rejection.
Evidence: full native **2,348 / 1 skipped / 0 failures**; `test_storage_manager_load` 35/35,
`test_drain_reentry_callback_safety` 8/8; capture-serial SUCCESS `code 391,080 + padding 2,136`; both fixes
proven discriminating by removal. **No device run** — nothing hardware-observable.

**S2b.7 DELIVERED: the blocking drain now SERVICES the restore it waits on.** It was measured first — the drain
named `slot_load_active` and advanced nothing (progress 1 → 1, phase 1 → 1) — and then implemented:
`StorageManager::serviceDeferredRestoreForDrain()` runs the **same** selection/admission + bounded step the
deferred-load frame uses, called once per drain iteration while a restore holds the SD session, with a
termination clause that reports the named wait as soon as the restore is gated instead of spending the drain
budget. No new owner, loader, queue or admission policy; the commit gate is untouched
(`resumable-midi-boundary-playback-maintenance` **task 3.3** still owns the bound). **Behavior:** another track
playing → progress **1 → 8**, `TemporarilyBlocked` / **`waiting_commit_gate`**, nothing published, request
preserved; a focused slot with the gate admitting → the drain commits the restore and discharges the save.
Full native **2,352 / 1 skipped / 0 failures**; `test_storage_drain_restore` 10/10; disabling the service step
fails exactly the three advancement assertions. **Device: NOT RUN** — the claims are native.

**No currently eligible native work remains in `runtime-working-set-ownership`.** What is left there is
**device-gated** on a stable restored library (S1.1/S1.3/S1.4/S1.5/S2b.3/S2b.5, the content PASS, and the S2b
gate) or is a **later stage** (S3–S7, phases B–F) whose start the change's own execution order places after the
S2b device gate. **The restored-library instability is no longer unowned:** `restored-library-stability`
(proposed 2026-09-24, planning complete, read-only Packet A startable immediately) owns it, pre-registers the
four-class attribution (catalog omission / restore refusal / intentional eviction / content loss), requires a
per-slot inventory that separates the payload set from the boot playback set, defines one fixture inside the
approved scratch range with per-slot authorization for any CLEAR, and carries the S2b.7 device confirmation.
Its first read-only findings already correct the previous attribution: `sets/index.bin` is a saved-set folder
counter (`SetIndex{nextSequence}`), the shrinking boots report `reconcile_warnings=0`, `DIAG,lcr,skip,restore`
is content-resolution, `Track N loaded state: EMPTY` is present on stable boots too, and both sweeps'
`library_rows` are all-zero — so no per-slot inventory was ever captured.

**`runtime-working-set-ownership` native scope (P0/P1) is COMPLETE, plus S2b.8 and S2b.7 delivered.** Full native
**2,352 / 1 skipped / 0 failures** (2026-09-24; 2,344 before this session). Landed and green: S0
verification, the S1.2/S1.3-clause-2/S1.5-clause-3 fixtures, the accepted-stop latch, boot/load admission
removal (S2b.1–S2b.3), S4.0/S2b.4 liveness + release, S4f.5's stop consumers, S3.0 placement pins + the
post-link RAM1 budget check, B01–B06, **S2b.8 external resets**, and **S2b.7 drain-side restore service**.
**Remaining and device-gated:** S1.1's
per-owner census, S1.3's device slope, S1.4's undo/root baseline, S1.5's device refusal point, S2b.3's
post-restore baseline, S2b.5's device gate, the content PASS, D0.2's B07 allocation census (not measurable
natively), and phases B–E.

**DEC-081 is device-verified** on its discriminating case (five tracks audibly playing + a genuinely owed
save → the clear's blocking drain completed in <833 ms, 0 `Clear aborted` / `drain stuck`), and the
persist-as-you-record mid-pass flush is device-verified (13 `PERS,mid_pass` rows, including inside live
`OVERDUBBING`/`PLAYING` windows, `overflow_free=True`). **Process gap, recorded not hidden:** both landed on
plan docs + code with **no OpenSpec change**; `deferred-slot-restore-liveness` covers only the
**restore**-liveness half, and **no change yet covers the save/immediacy half**.

## Workflow update — 2026-09-23 (working tree, no firmware delivery)

**SD temp-write truncation fix (bugfix, not shipped).** A pre-existing persistence defect found by the
`runtime-working-set-ownership` device session is fixed on `feature/runtime-working-set-ownership`:
Teensy's `SD.open(path, FILE_WRITE)` appends and `FILE_WRITE_BEGIN` does not truncate, so a temp left by a
failed write corrupted `sets/index.bin` and the boot catalog stopped reconciling. Fixed via
`CurrentSetStorage::openForRewrite` at the 16 from-scratch write sites plus a self-healing
`reconcileSetIndexOnSd`, with the native SD stub corrected and a red-proven regression test. Native
2,289 / 1 skipped / 0 failures; device shows **0** `could not reconcile` warnings across the two
post-fix boots that actually reset the board (against one on every pre-fix boot). Plan:
[`Plans/sd_temp_write_truncation_refinement.md`](Plans/sd_temp_write_truncation_refinement.md).
**Verified on device (2026-09-23):** 0 `could not reconcile` on every post-fix boot that reset the board,
a recorded loop body persists (`loop_persist=4252 sliced=True ok=True`) and its slot restores across a
reboot (`CAPTURE_CONTENT,begin,track=5,slot=0,length=3072`, 190 rows), with pre-existing content faithful
(`length=13824`, 1,964 rows). **Not shipped:** it is not on `dev`. **Still open** under
`runtime-working-set-ownership`: the content PASS (needs a clean managed capture) and the S1.3 slope sweep.
**Correction (same day, later evidence):** the later RWSO-24CAP device session showed the **restored set is
still unstable across boots** (`Track 6 loaded state: EMPTY`, a census that changes between boots), so this
fix repaired reconcile and the save path but did **not** make the library faithful — the remaining blocker is
the restore-liveness defect, whose change was **archived 2026-09-24** as
[`2026-09-24-deferred-slot-restore-liveness`](../openspec/changes/archive/2026-09-24-deferred-slot-restore-liveness/proposal.md)
with a reduced scope (the restored-library instability itself was **not** removed).
The Round 7 "the catalog IS stable" claim in the plan is scoped to *a recorded loop's body persists and its
own slot restores across one reboot*; it does not extend to *the library accumulates across successive
boots*, and it is the latter that the slope sweep needs.

## Workflow update — 2026-09-23 (working tree, no firmware delivery)

DEC-080 strengthens [implementation readiness](Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
with fixture-first execution packets and evidence-backed checkoff. The runtime-working-set-ownership
[phase review](../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) specifies acceptance
for upcoming slices. The change's **native measurement stage (S1) has since landed its fixtures** — the 1-vs-4
and synthetic 60-descriptor residence laws, the dense-24-bar library capacity law, and the dense-seal
diagnosis (all in the default native gate) — plus the first boot/admission behaviors (S2b.1/S2b.2 and the
accepted-stop latch). **Nothing here is shipped firmware:** the change is on
`feature/runtime-working-set-ownership`, not `dev`, and its device gates are unrun. This is not a shipped
runtime feature.

## Shipped vs Next (updated Sep 2026)

**Shipped** (in firmware on `dev`):

- **Memory: the slab arena was measured, not reclaimed; the pool, the ring and the two-overdub display were fixed (Sep 2026):** four landed changes on this thread. (1) **Slab census** (`c0aadc0f`): `SnapshotSlabCensus` registers live slabs in a doubly-linked list inside the 32 KB slab header (no RAM1 cost) and reports live/used/unused plus `pin1`/`pin24` in the `[Memory] ext` tick line, next to the historical cumulative `snap=` so the two cannot be conflated. It falsified the premise of the planned "reclaim ~3 MB": 16,798 nodes of `sum(sizeof)` 2,205,184 B in 78 slabs at 131.3 B average is **99.5 % packed** on device, so the arena is live content, not stranded slack — the reclamation work was re-aimed instead of built. (2) **RC-MEM-2 whole-take reservation** (`5ba7d109`, `8c93e4e6`): a 24-bar loop can never publish a whole-take view (`coverage.lengthTicks == loopLengthTicks` is required), so the two bridges, their bases and the prepared pair are reserved only when `Loop::fullTakePlaybackViewAdmissible()`, and released when the loop is known long — ~558 KB of dead reservation per armed long-loop slot, with `wholeTakeReserved` making the refusal explicit rather than incidental. (3) **RC-MEM-5 lazy chunk pool** (`d85be061`): `initPool` reserves nothing; the 512-chunk array is a lazily opened block table (64 chunks/block), so a 104–143-chunk peak backs 2–3 blocks instead of all 8. Boot pool `live` fell **3,837 -> 1,913 KB** and the reported regression was fixed on device (`captures/rcemem5-lazy-pool`): **`Overdubbing started` twice** where the fixed-pool legs had zero starts against four `op=overdub_start` denials, `op=overdub_start` denials **0**, `snapshot_reserve` denials 129 -> **6**, **0 `FATAL`**, heap holding 80 KB through both overdubs. (4) **RC-MEM-7 tier-aware ring degradation** (`69f01080`): Tier-A text displaced by newer Tier-A text is reported as `RING,tier_a_evicted,<count>` and degraded rather than treated as session-invalidating, with only critical loss fatal; the harness raises an advisory, so long runs no longer abort on ring capacity (measured `capture_tier_a_evicted:92655` over ~162 s, which sizes the instrumentation change). (5) **5.12 second-overdub display** (`4e97feb2`, `8c93e4e6` lineage): a closed overdub's rows were carried only by the sealed capture handoff, which `openOverdubSession` cleared, so starting the second overdub erased the first from the OLED until a resolve ran. A **display-only tier** (`retiredSealedDisplayNotes_`) retired at session close, unioned inside `applyDrawSourceOverlays` (the production counterpart of the host mirror, last in every branch, after the assign that would otherwise clobber it) and gated on `retiredSealedDisplayNeededForFrame()` — **identity, not span**, because exact-span coverage can never hold for a row the clean-adopt rule replaced. Native `test_loop_take_survival` 140/140 including all four historical guards (`120321`, occupy-hide, `214900`, od2); full native 2,262 / 1 skipped / 0 failures; RAM1 unchanged. Device gate owed. Across (2)–(5): full native 2,262 / 1 skipped / 0 failures, `teensy41-capture-serial` SUCCESS with RAM1 unchanged.

- **Committed passes always reach the audibility edge, and the display follows an undo/redo (OpenSpec `immutable-loop-snapshot-passes` 5.8 + 5.8a, successor 5.9/5.11, Sep 2026):** the published root is rebuilt from every active committed pass after an undo or redo of a record/overdub entry (`Loop::requestCommittedPassesRepublish`, normal session-exit lane, previous root kept audible until the replacement lands), and a queued session-exit publication is now startable on an in-session idle turn (`pendingSnapshotSessionExitQueued()` in the snapshot-idle eligibility clause) instead of waiting for a wrap journal or an in-flight construction. This closed the reported 'the overdub wrap's second pass is shown on the display but not audible'. Device: [`content-verify-65-playback`](../captures/content-verify-65-playback/content-review.json) `result: PASS`, `storage_matches_effective: true` (384 + 157 + 35 == 576), all five cycles `inventory_equal: true` / `exact_order: true` / 0 missing / 0 extra, `max_bar_refs: 72`, no `companion path stalled`; firmware `f82ee9b9…` from `31933183`. **5.8a (the display half):** once playback stopped emitting the disabled pass, the OLED still painted it, because the sealed capture handoff is released only when `sealedCapturePreviewIsCovered()` finds each row in the committed cache — an undo removes those rows, so coverage could never hold again and `applyDrawSourceOverlays` unioned them into every frame. `Loop::setCapturePassState` now clears the handoff when a pass becomes `Disabled`. Device: [`display-undo-03`](../captures/display-undo-03/session_20260918_024515.log) (managed `base`, DEC-071 shape + recipe undo/redo) `Result: PASS` — **`DISP across undo/redo: before=194 after_undo=179 after_redo=193`**, against the buggy `194 -> 193 -> 193`; firmware `47e05029…` from `9eda0984`. The base runner now rejects a frame that keeps the undone overdub (`_verify_undo_redo_display`). Native 2,240 / 1 skipped / 0 failures; RAM1 locals 6,400. **Publisher correctness (DEC-073, successor 5.9, Sep 2026):** the remaining loss was the publisher stamping a candidate built for an older committed-pass state with the current `playbackRevision` — through the session-exit publish *and* the undo/redo captured-root selection — so the LSV1 body save could gather an undo-shaped 48-row root while `PassesResolve` was 57. A published root now carries `coveredPassRevision` and `Loop::publishedSnapshotIsCurrent()` is `coveredPassRevision == committedPassRevision()`; the box advances its watermark only where it enumerates, wrap candidates keep their `targetRevision`, and undo/redo coverage travels with the retained root (`UndoEntry::before/afterPublishedCoverage`). Two native reproducers in `test_loop_take_survival` were red before and are green after; full native **2,246 / 1 skipped / 0 failures**; `teensy41-capture-serial` RAM1 variables 91,968 / code 425,560 / padding 424 / locals free 6,336. Device (firmware hex `f2f1d8bd…`, DEC-071 multi-pass shape + recipe undo/redo): [`rc1-coalescing-reboot-playback`](../captures/rc1-coalescing-reboot-playback/content-review.json) is a fresh boot (`boot_reset,crash=0` at 3.0 s) with **`result: PASS`, `storage_matches_effective: true`, 384 + 154 + 38 + 24 = 600 effective, all five cycles `inventory_equal: true` / `exact_order: true`, 0 missing, 0 extra, `max_bar_refs=84`** — the 24-row second-overdub pass is audible after reboot. **Persistence admission (DEC-074, RC2, Sep 2026):** the LSV1 body now admits only a *settled* root (`hasSettledPublishedSnapshotForSave`: coverage-current and no publication pending); the per-loop format is resolved once at the write's `Header` and pinned, so a committed-state change mid-write can no longer leave an LSV1 preamble without its snapshot tail; and an unsettled loop writes the passes-authoritative **legacy** body (LSV1's readers reject a missing `LoopSnapshot` section, so a rootless LSV1 body is not expressible). A bounded settle re-admission (128 writer turns, nothing written while waiting) lets a transient pending republish finish first. Two native fixtures in `test_storage_loop_io` (the admission one red on the pre-RC2 predicate); suite 47/47, full native 2,248 / 1 skipped / 0 failures, RAM1 locals free 6,336. Device (firmware `86fd8f07…`, `captures/rc2-settle-device-c` + `rc2-settle-reboot-playback`): the save took the settled LSV1 path (`tail_gather_begin`, `LoopSnapshotTail` 1,389 steps, `q=0` publications), 0 `RING,overflow`, 0 `FATAL`, post-reboot `storage_matches_effective: true`; that session's 4 unexplained wrap rows are a 240-vs-120 BPM setup artifact (17-bar record for an 8-bar request), not a content finding. The acceptance env's `SESSION_CAPTURE_LCR=0` gates the `DIAG,lcr,*` family that overflowed the ring twice before. Load-time reconcile was explicitly rejected by the owner.
- **Adversarial reentry checkpoint set complete (OpenSpec 5.5, Sep 2026):** the deferred-stop reentry gate is proven independent of where the predecessor pauses. All five checkpoints run in the default suite: `CanonicalOrder` (during canonical collection), `CanonicalRewrite` (after the raw-range stage, while the canonical copy is rewritten), `CompanionHistory` (after pending-change application), the staged-pass-ownership turn (the D8 boundary, expressed as "the turn the job first owns its staged pass" because `PrepareTransfer` dispatches before `CanonicalTail` and its grain also stages and takes the receipt), and `AtomicTransfer` (publication/retirement). The `REENTRY_RED` gate is gone and the phase checkpoints assert **equality** so a checkpoint cannot silently run past its state. Native: `test_continuous_capture` 74/74, `ONLY_COMPANION=1` 1/1, full native **2,239 / 1 skipped / 0 failures**, RAM1 locals **6,400** (test-only). Remaining in that change: the 5.9/5.11 audibility proof and 6.2 device runs.
- **The undo stack keeps its protected publication capacity (successor 8.3, Sep 2026):** `TrackUndo::eraseUndoEntriesForSlot` move-assigned a filtered rebuild and so dropped the `prewarmProtectedUndo` reserve; `TrackUndoPublication::begin` then rejected at `cursor >= capacity`, silently committing a stop's pass without its GUS entry (depth stuck at 2, next undo fell through to the first overdub; device diagnostic `cap=4`). Fix `433fea68` re-reserves `kUndoStackProtectedCapacity` before the move. New native fixture fails without it; full native **2,244 / 1 skipped / 0 failures**, RAM1 locals 6,400. Device [`gus-sanity-01`](../captures/gus-sanity-01/session_20260918_091003.log) (canonical `base`, two overdub sessions + undo/redo) `Result: PASS`: `MIDI: Undo (entries=3)`, `publish_begin cap=32`, `passIds_n=2,src=deferred`, `DISP across undo/redo: 56 → 48 → 57`.
- **The deferred stop marks the committed pass's display bars (successor 8.2, Sep 2026):** the deferred terminal commit only called `notifyCommittedContentChanged` → `markPassDerivedStale`, which leaves `dirtyBars` empty after a `slice_clean`; `rebuildVisualCacheIdleSlice` then cleared the stale flag as `slice_nodirty` without rebuilding, so the OLED kept pre-commit notes after an overdub stop until the next wrap marked bars (reported: "the second overdub disappeared on the display after overdub stop to playing, reappeared at loop wrap"; `gus-undo-04`: 176 → 144 at 74.3 s). Fix `a91cb719` marks the committed pass + prepared companion ids in every deferred terminal commit. New native fixture fails without it; full native **2,242 / 1 skipped / 0 failures**, RAM1 locals 6,400. Device [`gus-undo-06`](../captures/gus-undo-06/session_20260918_084854.log) (managed `base`, two overdub sessions): the frame holds 176 from the second stop through the undo, `DISP across undo/redo: 176 → 144 → 176`.
- **One GUS entry carries the whole overdub session (successor 8.1, DEC-068 follow-up, Sep 2026):** the deferred stop's `captureSessionExitTail` swaps the interval's wrap identities into its own session-exit receipt at admission, but the deferred undo collection still read the live session lists at its later `UndoCollection` phase — so the entry held only the final pass and one undo removed a partial overdub. The collection now sources the interval's receipt (`SessionUndoCollection` receipt pointers + bound `receiptIdentity`; `beginCollectOverdubStopUndoPasses` takes the receipt ring), and the tail capture now runs only once deferred admission is committed (a denied admission used to leave the synchronous fallback with an already-emptied session). New native fixture `test_deferred_stop_undo_entry_carries_every_session_pass` fails without the fix; `test_continuous_capture` 75/75, full native **2,241 / 1 skipped / 0 failures**, RAM1 locals 6,400. Device [`gus-undo-03`](../captures/gus-undo-03/session_20260918_083306.log) (managed `base`, firmware `7708d605…` from `c380c030`) **`Result: PASS`**: `MIDI: Undo (entries=2)`, `DIAG,gus,…passIds_n=2,src=deferred`, `DISP across undo/redo: 194 → 128 → 192`, `stored overdub span 64/63 count_ok/span_ok/bars_ok`.
- **Interval-owned staged capture pass (DEC-072, Sep 2026):** a deferred stop job owns its staged capture pass from its own `PrepareTransfer` grain (`CaptureStopJob::pendingPass` / `ownsPendingPass`, `Loop::takePendingCapturePass`), the prepared companion history binds that receipt by address, and the job's `AtomicTransfer` consumes it directly (`Loop::transferCapturePass`); every deferred reader (undo identity, bridge-refresh, companion verification, stop telemetry and chunk/event accounting) resolves the receipt through the job with no fallback to `Loop::pendingCapturePass_`. `beginCapture` keeps its unconditional producer-local clear. This retired the OpenSpec 5.5 `CompanionHistory` reproducer (a successor's capture start used to discard the predecessor's pass and wedge its finalization resources). Native: `ONLY_COMPANION=1` green, `REENTRY_RED=1` `test_continuous_capture` 57/57, full native **2221 cases, 1 skipped, 0 failures**, RAM1 locals **6400**. Device [`successor-ownership-06`](../captures/successor-ownership-06/session_20260917_230432.log) (managed `base`, 8-bar record / 2-bar overdub, temporary `d8-ownership` stimulus variant): `Result: PASS`, `overlap=True denied_stop=True retirement_ok=true` — A retires inside B's live interval and B then stages and transfers its own pass, no `AlreadyPending`.
- **Reentry session ownership (DEC-068, Sep 2026):** each capture interval owns its session-exit tail, captured at its own stop admission and released with its own pass id; a successor opens a fresh session and a retiring job never ends the successor's session or hands off its identities. Native `test_continuous_capture` 46/46 (six DEC-068 cases), full native **2209 cases, 1 skipped, 0 failures**. Device [`successor-reentry-27`](../captures/successor-reentry-27/session_20260917_163416.log): first clean reentry `PASS` (`overlap_ok`/`denied_stop_ok`/`retirement_ok`).
- **Successor-reentry finalization admission is stop-directed (DEC-067, Sep 2026):** overdub START keeps only the live staging role, the stop-descriptor ring and its own memory admission; the canonical copy target, active transfer admission and session-exit receipt slot are checked by `beginDeferredOverdubStop`, and a stop denied for them is refused atomically instead of falling into the legacy synchronous stop. Prompt markers `Reentry window open slot=<n>` / `Reentry released` (plus `DIAG,reentry_window_open` / `DIAG,canonical_release`) drive the HITL probe. Native `test_continuous_capture` 40/40 (three new DEC-067 cases), full native **2203 cases, 1 skipped, 0 failures**, RAM1 locals **6464**. Device [`successor-reentry-25`](../captures/successor-reentry-25/session_20260917_160819.log) (managed `base`, 8-bar record): B START 583 ms after A's stop and 2,209 ms before A's finalization released, B's stop refused, then admitted after release. Declaration: [`CONTINUOUS-CAPTURE-OWNERSHIP.md`](../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md).
- **First-record playback launches from the prepared bar-1 window (Sep 2026):** the record-stop handoff's bound window is preserved across the launch's cache resets and adopted as the audible source while the first root publishes; adoption selects the cursor with the source. Device [`first-record-launch-04`](../captures/first-record-launch-04/session_20260917_144808.log): first note 93 ms after the stop edge, then the 16th grid, against 620 ms and a catch-up burst before. Open: S2b/S3 (retained-extent/closure and post-bar-1 continuation).
- **Immutable OpenSpec 6.4 verification gate (Sep 2026):** post-6.3 + RC-P1 LSV1 tail closeout sign-off on device [`164947`](../captures/session_20260912_164947.log) (`loop_snapshot_tail,done` → `payload_done` → `SAVE,completed`, 25-bar OD3). RAM1 **6848**; native **1707/1707** (RC-P1 close `325d6d6a`); HITL `base` re-run waived (6.3.7 anchor [`030210`](../captures/session_20260912_030210.log)). Next: **6.5** archive.
- **Immutable OpenSpec 6.3 compatibility machinery removal (Sep 2026):** C1-a gather bridges retired (6.3.1); post-record visual continuity (6.3.2); `passesMaterializedStore_` / `gatherCommittedEvents*` / capture construction replay / LCR `Phase::Materialize` removed (6.3.3–6.3.6); gap predicate retained for visual hold-only (6.3.7). Native **1699/1705**; RAM1 **6880**; HITL `base` PASS [`030210`](../captures/session_20260912_030210.log). Plan: [`immutable_loop_snapshot_6_3_compatibility_machinery_removal_refinement.md`](Plans/immutable_loop_snapshot_6_3_compatibility_machinery_removal_refinement.md).
- **Immutable OpenSpec 5.7 persist work-queue stall hardening (Sep 2026):** recover orphaned `Writing` entries when `PersistenceWorkItemJob` is inactive; `#CAP PERS,work,...,work_queue_stall,recover`; native **13/13**; host stall warning in `parse_persistence_buildup.py`. RAM1 **6880**; native **1828/1833**. HITL post-fix sign-off pending. Plan: [`immutable_loop_snapshot_5_7_persistence_work_queue_stall_hardening_refinement.md`](Plans/immutable_loop_snapshot_5_7_persistence_work_queue_stall_hardening_refinement.md).
- **Immutable OpenSpec 6.2 compatibility consumer retirement (Sep 2026):** B1–B8 normal-consumer compat fallbacks retired (`_6_2_BLOCKER_SITES` empty); narrow C1-a bridges at playback/visual/edit/idle; B7 LCR device-gate construction-only; B8 `copyCommittedPassReplayEvents` in capture. Native **1825/1830**; RAM1 **6880**; HITL core [`002259`](../captures/session_20260912_002259.log); persist verifier open → **5.7**. Plan: [`immutable_loop_snapshot_6_2_compatibility_retirement_refinement.md`](Plans/immutable_loop_snapshot_6_2_compatibility_retirement_refinement.md).
- **Immutable OpenSpec 5.5 persist ownership (Sep 2026):** deferred save execute stays on `StorageManager::processDeferredSaveState`; HITL `base` [`200657`](../captures/session_20260911_200657.log) sliced `LoopPersist` (`drain=0`, `MO=600`). Plan: [`immutable_loop_snapshot_5_5_deferred_persistence_ownership_refinement.md`](Plans/immutable_loop_snapshot_5_5_deferred_persistence_ownership_refinement.md).
- **Immutable OpenSpec 5.6 large-loop persist bounds (Sep 2026):** LoopPersist phase progress, 100 ms slice caps, active snapshot during PLAYING save, stop-path admit-only, timing-critical call-site gates; HITL `base` PASS [`203803`](../captures/session_20260911_203803.log) (`windows=5`, report [`203843`](../captures/host_midi_automation_baseline_20260911_203843.json)); native **1823/1**. Plan: [`immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md`](Plans/immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md).
- **Immutable OpenSpec 6.2.2 B3/B4 C1-a bridge (Sep 2026):** generic B3/B4 visual/display compat retired; session-exit gather only via `isPlaybackSessionExitPublicationGap`; Overlay-reject and pass-only misses use `PassesResolve` / hold gates. Host gates PASS; RAM1 locals **6880**; native **1824/1829**. HITL `base` core baseline PASS [`000210`](../captures/session_20260912_000210.log); persist verifier **open**. Plan: [`immutable_loop_snapshot_6_2_compatibility_retirement_refinement.md`](Plans/immutable_loop_snapshot_6_2_compatibility_retirement_refinement.md).
- **Immutable OpenSpec 6.2.1b B1 C1-a bridge (Sep 2026):** generic B1 compat retired; session-exit gather only via `isPlaybackSessionExitPublicationGap`; other misses hold empty. HITL `base` PASS [`231439`](../captures/session_20260911_231439.log); RAM1 locals **6880**; native **1824/1829**. Plan: [`immutable_loop_snapshot_6_2_compatibility_retirement_refinement.md`](Plans/immutable_loop_snapshot_6_2_compatibility_retirement_refinement.md).
- **Immutable OpenSpec 6.1 compatibility consumer inventory (Sep 2026):** hot-path forbid gate + B1–B8 fallback site inventory; host `scripts/test_compatibility_consumer_inventory.py` (7 tests PASS); no firmware change. Plan: [`immutable_loop_snapshot_6_1_compatibility_consumer_inventory_refinement.md`](Plans/immutable_loop_snapshot_6_1_compatibility_consumer_inventory_refinement.md).
- **Overdub snapshot bar-crossing membership — Stage 2 RC-A + Stage 4 + Stage 5 complete (Sep 2026):** hand-off preserves live-record open notes; all consumer containment layers unwound (6→5→3→1→2). Final HITL [`114744`](../captures/session_20260911_114744.log) (`from=snapshot notes=32`, id=27 bar-cross). Latent RC-B separate. Plan: [`overdub_snapshot_bar_crossing_membership_bugfix.md`](Plans/overdub_snapshot_bar_crossing_membership_bugfix.md).
- **Overdub start bar-crossing snapshot omit — consumer Stages 1–6 unwound (Sep 2026):** containment shipped then removed after membership RC-A. Final gate [`114744`](../captures/session_20260911_114744.log). Plan: [`overdub_start_snapshot_omits_bar_crossing_record_note_bugfix.md`](Plans/overdub_start_snapshot_omits_bar_crossing_record_note_bugfix.md).

- 8 tracks × 8 loop slots: per-slot record, overdub, clear, mute, quantized switching, multi-slot hold layering (`Track`, `TrackManager`, `SlotStateMachine`)
- Undo/redo per slot (overdub, clear, loop start) — memory-aware depth (`PREFERRED_UNDO_DEPTH` 99, `MIN_UNDO_DEPTH` 8, `ABSOLUTE_MAX_UNDO_ENTRIES` 512; `trimGlobalUndoStackForMemory` in `PassReclaim` / `TrackUndo`). Redo branch preserved after full undo until a new pass pushes.
- 192 PPQN internal clock, 24 PPQN MIDI sync with internal fallback (`ClockManager`, `ClockSourceStateMachine`)
- Jam regions via Bars/16ths buttons (`BarStepButtonHandler`, jam state on `Track`)
- Piano-roll note editing: select, start, length, pitch, move, wrap (`EditManager`, `EditStates/`, `NoteEditManager`)
- Loop start/length fader editing (`LoopEditManager`)
- SSD1322 OLED piano roll + track strip (`DisplayManager`); controller LED feedback (`MidiLedManager`)
- SD persistence v4 (passes via `StorageLoopIo`) with deferred runtime save (`StorageManager::requestDeferredSaveState` / `processDeferredSaveState`); PSRAM chunk pool + PSRAM-first length-scaling allocators (`LoopEventStore`, `PsramFirstAllocator`)
- **StorageSession persistence refactor (DEC-012, Jun 2026):** job RAM on `storageSession`; FSM split `WorkspaceSave` / `RevisionCommit` / `RevisionLoad` + `Overlay.cpp`; revision-load request vocabulary; normative specs `revision-load`, `storage-session-jobs`, `storage-session-layout`
- DROID USB host MIDI buttons/faders (`MidiHandler`, `MidiButtonManager`, `MidiFaderManager`)
- **Commit-centered lazy slot load (DEC-026/027, Jul 2026):** Phase A archived `2026-07-18-unified-commit-lazy-slot-load` (gates [`224607`](../captures/session_20260718_224607.log), [`230145`](../captures/session_20260718_230145.log)). **Phase B DeferredJobScheduler** archived `2026-07-19-deferred-job-scheduler` (gates [`231510`](../captures/session_20260718_231510.log), [`022107`](../captures/session_20260719_022107.log)); normative `openspec/specs/deferred-job-scheduler/`.
- **NoteEditCurrentState ownership (DEC-029, Aug 2026):** NOTE_EDIT editable geometry keyed by `NoteId`; `EditSession.store` is projection only. OpenSpec archived `2026-08-08-note-edit-current-state`; normative `openspec/specs/note-edit-current-state/`; HITL [`112202`](../captures/session_20260808_112202.log), [`115120`](../captures/session_20260808_115120.log), [`032118`](../captures/session_20260808_032118.log).
- **Sticky overlap participation (DEC-030, Aug 2026):** `NoteEditOverlapParticipationType` on current state; `ParticipatingNotePhase` removed; §12 R1–R5 orthogonal representation complete. Plan: [`note_edit_resolver_authority_contracts_refinement.md`](Plans/note_edit_resolver_authority_contracts_refinement.md).
- **Playing move/length playback audition (Aug 2026):** `refreshPlaybackPreview` forwarded through move/length geometry paths (`15c5750`). HITL [`113626`](../captures/session_20260808_113626.log), [`115120`](../captures/session_20260808_115120.log). Bugfix: [`note_edit_playing_move_audition_bugfix.md`](Plans/note_edit_playing_move_audition_bugfix.md).
- **Overdub pass overlap resolution (DEC-031/032 G2, Aug 2026):** `overdubSourceView` + shared geometry Add/Shorten/Hide; dual-seal companions + STK2 undo. OpenSpec archived `2026-08-12-overdub-pass-overlap-resolution`; normative `openspec/specs/overdub-pass-overlap-resolution/`. Device PASS [`010000`](../captures/session_20260812_010000.log); baseline [`183525`](../captures/session_20260811_183525.log). Firmware on `feature/overdub-pass-overlap-resolution` pending merge to `dev`.

- **Loop content-only history (DEC-035 Layer A, Aug 2026):** content-only persist; load-time GUS fill; empty GUS headers; `LoopPersist`-only overdub stop. OpenSpec archived `2026-08-14-loop-content-history-persistence`; normative `openspec/specs/loop-content-history/`. Device PASS [`030147`](../captures/session_20260814_030147.log), [`032227`](../captures/session_20260814_032227.log). Plan: [`loop_layer_history_persistence_architecture.md`](Plans/loop_layer_history_persistence_architecture.md).

- **LoopContentResolution parallel prototype (DEC-037, Aug 2026):** native `LoopContentResolution` + device gate suites; 6C/6D closed [`205928`](../captures/session_20260815_205928.log) / [`210508`](../captures/session_20260815_210508.log). PR [#35](https://github.com/Lytrix/MidiLooper/pull/35) merged to `dev` (`be0e05f`). OpenSpec change `loop-content-resolution` pending archive. Plan: [`loop_event_sourced_resolution_architecture.md`](Plans/loop_event_sourced_resolution_architecture.md).

- **NOTE_EDIT exit persist parked mover (Sep 2026):** close commits parked Visible `currentSpan` ≠ `committedSpan` and does not drop `applyOwnedEditPassRows` when canonical is empty. Same owner as empty-step deselect (`commitAllPendingNoteEditActions`). Guide: [`MOVE_NOTE_LOGIC.md`](Guides/MOVE_NOTE_LOGIC.md) § Commit / macro seal. Compose `02` [`225149`](../captures/host_midi_hitl_note_edit_overdub_compose_20260909_225149.json) **13/13**. Plan: [`note_edit_exit_persist_selected_mover_bugfix.md`](Plans/note_edit_exit_persist_selected_mover_bugfix.md).

- **NOTE_EDIT overdub-exit piano-roll revert (Sep 2026):** RC1 retain unpublished `visualCache` over this stale gather. RC2 non-selected `stopPlaying` does not write `liveDisplayNotes`. Device PASS [`223532`](../captures/session_20260909_223532.log) `frame,65` / first STOPPED `2304/65`. Plan: [`note_edit_overdub_exit_piano_roll_revert_bugfix.md`](Plans/note_edit_overdub_exit_piano_roll_revert_bugfix.md).

- **Capture pending same-pitch NoteOn reject (Sep 2026):** `Track::noteOn` drops a second On while `{note, channel}` is pending so the first On is not orphaned to loop end. Device PASS [`212509`](../captures/session_20260909_212509.log) compose `10/10`. Plan: [`capture_pending_same_pitch_note_on_reject_bugfix.md`](Plans/capture_pending_same_pitch_note_on_reject_bugfix.md).

- **NOTE_EDIT overdub select/pitch hang (Sep 2026):** paint-cache reuse on fingerprint/select; ValueOnly motors while overdubbing. Device PASS [`152705`](../captures/session_20260909_152705.log). Plan: [`note_edit_overdub_select_pitch_hang_bugfix.md`](Plans/note_edit_overdub_select_pitch_hang_bugfix.md).

- **NOTE_EDIT control-plane motors (Sep 2026):** RC7 ValueOnly notegate held `kMotorNoteDurationMs`; RC8 F1 outbound uses `selectPitchbendFromTick` (not neighborhood slot index). User accepted [`234815`](../captures/session_20260909_234815.log) late-loop F1. Compose `02` **14/14**. Plan: [`note_edit_control_plane_record_display_stall_bugfix.md`](Plans/note_edit_control_plane_record_display_stall_bugfix.md).

- **Compose last-bar beat-4 Shorten HITL (Sep 2026):** `STIM-NE-OD-COMPOSE-03` v1 / preset `note_edit_overdub_compose_last_bar_shorten`. Device PASS [`133714`](../captures/session_20260909_133714.log). Plan: [`note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md`](Plans/note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md).

- **Overdub companion admission + STOPPED first-paint (§36, Aug 2026):** clock-path gather copy-only; source-span prewarm after `boot_commit`; STOPPED undo/switch keep `visualCache.notes` and budget idle gather. Device PASS [`165107`](../captures/session_20260821_165107.log), [`183229`](../captures/session_20260821_183229.log). Plan: [`runtime_scheduling_owner_boundary_inventory_investigation.md`](Plans/runtime_scheduling_owner_boundary_inventory_investigation.md) §35–§36. Later: wrap-append bound, 5 s idle attribution (§37).

**Next (persistence/runtime):** **`feature/overdub-participant-discovery`** — leftover identity HITL **PASS** [`111819`](../captures/session_20260819_111819.log); missing-open identity validity filter native **PASS** 1374/1374 HITL `n=1 a=2` open ([`overdub_occupy_missing_open_identity_bugfix.md`](Plans/overdub_occupy_missing_open_identity_bugfix.md)); occupy clock same-tick Off-before-On HITL **PASS** [`001021`](../captures/session_20260819_001021.log) ([`overdub_occupy_clock_same_tick_off_before_on_bugfix.md`](Plans/overdub_occupy_clock_same_tick_off_before_on_bugfix.md); `n=0 a=1` = 0, `n=1 a=0` = 0); parent catch-up pin [`235314`](../captures/session_20260818_235314.log) was 4; pin [`233247`](../captures/session_20260818_233247.log) was 11; interval catch-up in tree; USB `playMidiEvents` catch-up HITL **FAIL** [`214856`](../captures/session_20260818_214856.log) reverted; wrap-S HITL **PASS** [`185831`](../captures/session_20260818_185831.log); loop-head storage-0 HITL **PASS** [`203948`](../captures/session_20260818_203948.log) ([`overdub_loop_head_playback_ledger_bugfix.md`](Plans/overdub_loop_head_playback_ledger_bugfix.md)). Occupy lookup of `Entry.noteId` shipped. Stage 1 `from=span` HITL **parked**. Do not wait STOPPED for 64-bar `lcr,mat`. Phase 4 1-bar HITL **PASS** [`123803`](../captures/session_20260818_123803.log). Loop length during overdub queued. **NOTE_EDIT hydrate** queued. **Playback gather** Stage 1 hooks. Overlay track A parked.

**Not in firmware** (docs may suggest otherwise):

- Phase 3 jam/arrangement **capture** (recording jam performance into a new slot), Scenes row, Jams row capture — spec exists ([plans/phase-3-multi-loop.md](Plans/phase-3-multi-loop.md)), slots infrastructure shipped, capture itself not built
- Encoder + 4-button GPIO base module — `ButtonManager` exists but is never called from `main.cpp` (see intent decision 1)
- DROID LFO pulse feedback, CC value editing, Fader3 quantization %, Fader4 pitch transpose, EEPROM config
- 16×2 LCD — driver present, pins disabled in `Globals.h`
- Interactive SD slot hydrate while PLAYING — **PASS** [`224607`](../captures/session_20260718_224607.log) / A.7 [`230145`](../captures/session_20260718_230145.log); OpenSpec archived `2026-07-18-unified-commit-lazy-slot-load`
- `DERIVED_READY` off Commit critical path (optional polish)

---

This repo uses two different “deliverable” styles in Markdown:

1. **Main deliverables (plan slices)** are exported by Cursor into `Plans/*.plan.md`. These files usually include a `todos:` block with slice IDs and `status:` values.
2. **Bug fix / refinement deliverables** — historical summaries in [`Plans/archive/refinements/`](Plans/archive/refinements/) (see [`Refinements/README.md`](Refinements/README.md)); active work in `Plans/`. State tracked manually or via OpenSpec.

This file is meant to be the **single overview** you consult to avoid losing track between the “big” plan slices and the smaller implementation summaries.

---

## Semantic Map (use these names consistently)

The goal here is to map the concepts you listed to the codebase’s existing subsystem names (so items line up with modules, functions, and existing docs).

| Your concept | Canonical subsystem name | Key entry points (proven in code/docs) |
|---|---|---|
| `Piano roll Display` | `DisplayManager` | `DisplayManager` (SSD1322 UI renderer); `DisplayManager::drawPianoRoll(...)` |
| `Note/Loop information display` | `DisplayManager` | `DisplayManager::drawAllNotes(...)`, `DisplayManager::drawNoteInfo(...)`, `DisplayManager::drawBracket(...)`, `DisplayManager::drawInfoArea(...)` |
| `Track State/Select display` | `DisplayManager` + `TrackManager` | `DisplayManager::drawTrackStatus(...)` (track letters); `TrackManager::getTrackState(...)` provides the `TrackState` used for rendering |
| `Midi Button` | `MidiButtonManager` (+ `MidiButtonProcessor` / `MidiButtonActions`) | `MidiButtonManager` delegates to `MidiButtonProcessor` (gesture detection) and `MidiButtonActions` (executing edit/record/undo operations); button configuration lives in `Utils/MidiButtonConfig.*` |
| `Midi Leds` | `MidiLedManager` | `MidiLedManager::updateLeds(...)`, `MidiLedManager::updateTrackSelectLeds(...)`, `MidiLedManager::updateCurrentTick(...)`, `MidiLedManager::clearAllLeds()` |
| `Tight Midi clocking` | `ClockManager` | `ClockManager::onMidiClockPulse()`, `ClockManager::onMidiStart()`, `ClockManager::onMidiStop()`, and `ClockManager::updateInternalClock()` |
| `Loop wrapping` | `NoteUtils` + `NoteMovementUtils` | Wrapping/reconstruction uses `NoteUtils::reconstructNotes(...)` (note-on discard vs note-off wrap); movement overlap/wrap logic is in `NoteMovementUtils` (e.g. `wrapPosition(...)`, `calculateNoteLength(...)`, overlap detection) |
| `Note/Loop cached note reconstruction (complex cache + update system)` | `NoteUtils::CachedNoteList` (via `Track::getCachedNotes*`) | Cache + invalidation uses `NoteUtils::CachedNoteList::getNotes(...)` (hash of MIDI events + loopLength); accessors are `Track::getCachedNotes()` and `Track::getCachedNotesForSlot()`; caches can be invalidated with `Track::invalidateCaches()` |
| `Note selecting, editing` | `NoteEditManager` + `EditManager` + `EditStates` | `NoteEditManager` routes MIDI note/fader inputs; movement + identity is managed by `EditManager`; individual edit behaviors live in `src/EditStates/*` (select/start/length/pitch) |
| `Undo system for Note edits/Loop edits` | `TrackUndo` | `TrackUndo` provides capture-pass undo (`pushRecordPassAdded` / `pushOverdubPassAdded`), **NoteEditPassClosed**, `undoOverdub(...)`, `redoOverdub(...)`, plus clear and loop-start undo/redo via `pushLoopStartSnapshot(...)` / `undoLoopStart(...)` / `redoLoopStart(...)` |
| `1 Redo` | `TrackUndo` | Redo is part of `TrackUndo`: `redoOverdub(...)` and `redoClearTrack(...)` |
| `Storage Load/Save logic for Loops, State machine` | `StorageManager` + `LooperStateManager` | Runtime persistence: `requestDeferredSaveState` → `processDeferredSaveState` (chunk-bounded SD v4 writer); load: `loadState(...)`; maintenance drain: `saveState(...)`; guide: `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`; global state + overlays in `LooperStateManager` |
| `MidiClock, Ports, Midi In/Out` | `MidiHandler` (+ `ClockManager`) | MIDI IO routing is `MidiHandler` (`handleMidiInput()`, `handleMidiMessage(...)`); clock pulses route into `ClockManager` via `clockManager.onMidiClockPulse()` |

---

## Deliverables: how to track them (minimal rules)

### Main deliverables (Cursor plan slices)
- Source files: `Plans/*.plan.md`
- In each plan file, use the `todos:` entries as the slice/status list (the `id` is your stable “deliverable key”, and `status:` is `pending` / `completed`).
- When you complete a slice in the code, update the corresponding `status:` in the plan file (and then reflect it in this overview if you maintain an external table).

### Bug fix / refinement deliverables
- Source files: `Plans/archive/refinements/*.md` (historical); active `Plans/*_refinement.md`
- Update the refinement file directly when the implementation is done.
- Use this overview to link which refinement docs are “done” for which deliverable area.

---

## Main Deliverables (MVP first)

This table is your “main vs sub deliverables” view. For most MVP items we only track **what** you want and the **code areas** it touches; when there is already a well-documented plan with slice IDs (Phase 3 jam recording), we include the detailed slice mapping.

### Deliverable Overview
| Deliverable | Canonical code areas (from this repo) | Spec / plan docs | State |
|---|---|---|---|
| MVP: Display + Button workflow to load/save sessions | `DisplayManager`, `MidiButtonActions` (session triggers), `StorageManager` + `LooperStateManager` | Needs a plan doc export for the actual UX flow | Needs spec (not found as an existing slice doc) |
| MVP: Memory expansion (PSRAM/OSRAM) to increase undo + loop capacity (>= 11000 notes) | `TrackUndo`, `Utils/MemoryPool` / `PooledMidiEventVector`, `Track`/`Loop` MIDI event storage | No plan export exists (referenced `mvp_psram_memory_expansion.plan.md` was never exported) — `ExtMemAllocator` spillover strategy: internal RAM first, PSRAM fallback, no-PSRAM graceful degradation | Implemented: `include/Utils/ExtMemAllocator.h`, `MemoryPool.h`, `Loop.h`, `StorageManager.cpp`, `main.cpp`; unit tests in `test/test_extmem_allocator/` |
| MVP: Display CC values + edit them using the faders | `DisplayManager` (info rendering), `NoteEditManager::handleMidiCC`, `MidiFaderManager` / `MidiFaderActions` | (Related concept) `Plans/phase-3-multi-loop.md` “cycle MIDI category note vs CC value” goal; no dedicated CC-editor slice doc found | Needs spec (UI + fader mapping still to be defined) |
| Main: Phase 3 jam recording (record loop start / loop selection into other Loops as a live jam) | `Track` jam state (`jamStartTick`, `jamLength`, `jamTick`, `jamPlaybackActive`), `ClockManager`, `TrackManager` (multi-loop capture plumbing), `DisplayManager`, `TrackUndo` | `Plans/phase-3-multi-loop.md` + `Plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md` (D13/D14/D15) | Slot infrastructure shipped; jam **capture** (D13–D15) and Scenes not implemented; detailed slice mapping below |
| Phase 3 support: slot play/record feedback via Droid LFO pulse (BPM-synced) | `MidiLedManager`, `MidiButtonConfig`/`MidiButtonActions` (slot armed/record transitions), `MidiConfig::LfoPulse` constants (note 70 + slot CC) | `Plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md` | Pending: repo has `MidiConfig::LfoPulse` definitions, but no wiring found for note 70 + CC slot gating into any active LFO/LED lane in `src/` |
| Future: Pitch transpose on `Fader4` in Loop mode (and record it into the loop) | `MidiFaderManager` / `MidiFaderActions` (fader routing), `NoteEditManager` edit plumbing, `Track`/`Loop` recording path | Roadmap/placeholder: `Guides/control-surface/Jams.md` “Pitch transposing” | Future (no slice doc found) |
| Future: `Fader3` controls quantization percentage of notes to 16ths | `MidiFaderProcessor` (fader input), `NoteEditManager` + edit states, quantization helpers (`Track::quantizeStart`, tick/16th logic) | No plan doc export found for “quantization percentage” | Future (needs spec) |
### Phase 3 jam recording slice mapping (only detailed one)
From `Plans/multi-loop_leds_and_droid_lfo_3a62f325.plan.md`:
- `D13` = **Arrangement mode: capture** (jam-era capture into target slot)
- `D14` = **Arrangement mode: playback** (resolve phase 2.3; tests)
- `D15` = **Scenes** (optional capture/persistence of active loop index)
