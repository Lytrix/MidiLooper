# Deliverable Tracking (Main vs Refinement)

**2026-10-01 — `runtime-working-set-ownership` S4b.7's second slice: the switch's provisioning vehicle is the contract's `SlotProvision` turn, not a reuse of the slot-restore prewarm request (firmware; ITCM back to 391,288, the turn is FLASH +120 B).** The first slice had the contract right and the vehicle wrong: reusing `requestDeferredPlaybackPrewarm` shared one `(track, slot)` record between a restore's prewarm and a switch's provisioning (each could consume the other's), and a switch's provisioning was reachable only through the restore lane's gating. The binding corrections had named the vehicle (Rev 20.3/21.7, Rev 22 §6) — a **`SlotProvision` turn kind**, running both the provisioning and `ensurePlaybackMergedEventsForSlot(targetSlot)`, exempt from the all-stopped guard, `kDeferredIdleOwnerCount` 6, cursor/cycle tests updated — so this slice builds exactly that: `DeferredIdleOwner::SlotProvision = 5`, body `TrackManager::servicePendingSlotProvision()` dispatched **before** the all-stopped guard, eligibility = the pending switch itself (`MidiServiceDrain::slotProvisionNeeded`), and `requestSlotSwitch` marks nothing (no request record, no new state). The restore lane keeps its own provisioning call as the restore's own occupancy need. **Tests:** drain suite re-pinned for six owners (rotation, absent cycle, interleaved session-exit, backlog `two_of_seven`) plus eligibility and visual-cache-independence pins; S4b.6 fixtures drive the real turn. **Red-proved, restored byte-identically:** removing the turn's provisioning fails three fixtures. **Measured coverage gap:** removing the turn's merged-window call leaves all 36 fixtures passing, because that window comes from the loop's **published snapshot root** and a hand-committed slot has none — pinned as a harness tripwire, with the discharge path named (the published-root fixture the demoted-target hold also needs, S4c/F). **Still owed:** the contract's age/bound on the long-loop hold. Focused **36/36**; drain **36/36**; full native **2,652 cases / 2,645 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS, RAM1 `code 391,288 + padding 1,928 / locals 34,880` (floor 840), hex `257adab1…`, not flashed. Recorded as **DEC-103**; record: [ARCHITECTURE-REVIEW.md](../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.7 — the provisioning turn the contract named".
**2026-10-01 — `runtime-working-set-ownership` S4b.6 + S4b.7 PARTIAL: a switch's inbound occupancy has one off-clock owner, the commit binds to it without taking it, and the demoted target shape is measured and owed (firmware; RAM1 +16 B ITCM, no new RAM1 state).** S4b.6's binding now reads `playbackRuntime.slotIfAllocated` (never `trySlot`) and is a no-op without a runtime, with the predicate named beside it (`Track::hasPlaybackOccupancyForSlot`), and the commit branch in `TrackManager::updateAllTracks` keeps the switch **pending** when the inbound occupancy is absent — so the two hot paths (the MIDI edges that request a switch and the clock path that commits it) cannot allocate. S4b.7's owner, `TrackManager::provisionSlotForSwitch`, is idempotent, provisions off-clock, and reports its own `OperationKind::SlotSwitch` (`op=slot_switch`) on refusal; it is reached through the **existing** deferred request owner (`requestSlotSwitch` → `requestDeferredPlaybackPrewarm` → `main.cpp`'s deferred frame provisions then prewarms), so a pending switch needs no new eligibility term and no new state. Fixtures `test_s4b_6_a_switch_to_a_slot_without_occupancy_provisions_off_clock` (one bar) and `…_a_multi_bar_target_…` (8 bars) drive the production press route and the real lane pair. **Red-proved twice, each restored byte-identically:** dropping the occupancy term fails both at *"the switch may not commit before its occupancy exists"*; reverting the binding to `trySlot` fails both at *"the MIDI edge must not provision inline"*. **Owed, measured:** a **demoted** target (> 16 bars) still does not commit after provisioning (`ready=0 occ=1 pending=1`) because the long-loop hold needs a non-empty windowed gather, which is empty without a published root — an architect question or an S4c/F published-root fixture, recorded rather than claimed. Focused suite **36/36**; full native **2,651 cases / 2,644 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS**, RAM1 `code 391,304 / padding 1,912 / locals 34,880` (floor 840), hex sha256 `ceb79f0a…`. Recorded as **DEC-102**; record: [ARCHITECTURE-REVIEW.md](../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.6/S4b.7 — the occupancy the switch binds to, and who provides it".
**2026-10-01 — `runtime-working-set-ownership` S4b.1: the switch lane's acceptance surface exists and the populated-target queued switch is pinned (test-only; no `src/` change).** `HARNESS_LINKS_PLAYBACK_SWITCH_LANE` links the playback hot path, the playback-policy owner and the transport-control owner and lifts the **play branch** out of the suite's fail-closed stub surface — needed because a populated target's switch commits in `TrackManager::updateAllTracks` (DEC-101 records the trade: no passing fixture could have been reaching a removed abort, and the loop-edit manager stays stubbed and fail-closed). Two fixtures that asserted a removed counting stub now assert the real `TrackManager::getMasterLoopLength()`. New `test_s4b_1_a_queued_playback_switch_waits_for_the_predecessor_and_then_commits`: a populated target queues a playback switch, the active slot does not move, the request stays **retained** across three loops while the predecessor finalizes, and it **commits with no further gesture** once the finalizer retires (pending cleared, enabled set replaced) — the "silent dead switch" does not occur on this shape. **Red-proved twice, restored byte-identically:** early commit and never-commit both fail the fixture at the named assertions. Focused suite **34/34**; full native **2,649 cases / 2,642 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS, RAM1 line unchanged. Record: [ARCHITECTURE-REVIEW.md](../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.1 — the switch lane's acceptance surface".

**2026-10-01 — `runtime-working-set-ownership` S4b.1 (a) discharged: the "press dropped" reading was stale, and the denied-path reproducer lands (test-only; no `src/` change).** The gesture already retains the start — `MidiButtonActions::handleToggleRecordForSlot` calls `TrackManager::retainRecordStartForSlot` when `finalizeCaptureAndSelectSlot` returns false, and logs "switch and record start retained (stop not yet admitted)". New `test_s4b_1_a_denied_stop_on_an_uncommitted_switch_retains_the_targets_start` reproduces the denied path (another slot's admitted finalization holds the track's capacity while the predecessor captures live beside it; its stop is denied): the active slot does not move, the target's start is retained, the stop is accepted-and-latched against its own slot, the occupant is untouched, and the retained start executes with no further gesture once the capacity frees. **Red-proved twice, restored byte-identically:** dropping `retainRecordStartForSlot` fails this fixture plus the sibling and the two existing retention fixtures; dropping the denied-stop latch fails it (*"an uncommitted switch must not move the active slot"*) — the latch is what holds the slot, so (a)'s reading was the S4f.5 protection working, not a defect. **Named prerequisite for the rest:** a *populated* target queues a **playback** switch that commits in `TrackManager::updateAllTracks`'s multi-slot-commit lane, which this suite cannot drive — it fails to link for exactly four symbols (`Track::playMidiEvents`, `Track::playMidiEventsForSlot`, `TrackManager::isTrackAudible`, `Track::shouldCommitQueuedPlaybackStart`), so extending the shared harness closure for those four is the prerequisite for the S4b.5/S4b.6/S4b.7 fixtures. Focused suite **33/33**; full native **2,648 cases / 2,641 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS, RAM1 line unchanged. Record: [ARCHITECTURE-REVIEW.md](../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.1 (a) — the 'press dropped' reading was stale".

**2026-10-01 — `runtime-working-set-ownership` S4b.1 check 7's *repeated* switch is covered, and its retention mechanism is measured (test-only; no `src/` change).** New `test_s4b_1_a_repeated_switch_while_a_finalizes_is_bounded_and_executes_once_capacity_frees` drives A overdubbing → A's stop admitted and paused → switch to slot 1 with B recording → **a second switch to slot 2 while A still finalizes**, and asserts: the active slot does not move and the target is not provisioned on the gesture path; the request is retained by the S4f.6 **carrier** (the target's record start stays queued) plus the S4f.1 **latch** (B's stop accepted against B's own slot, no job for it); nothing is stolen (A's live finalizer and lease and B's lease survive) and the pool does not grow (490 → 490); and once A retires the request **executes with no further gesture**, with the steady state neither drifting nor consuming further chunks. **Red-proved twice, restored byte-identically:** provisioning inside the record branch's wait fails the fixture; removing that wait fails it. **Control:** emulating the pre-fix *unscoped* clear leaves it passing and fails two other fixtures, so finding `(b)`'s coverage is attributed to those. Focused suite **32/32**; full native **2,647 cases / 2,640 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS, RAM1 line unchanged. **Still open:** S4b.1 `(a)` and `(d)`/`(e)`; `Omax` is asserted as behaviour, not as a new constant. No new decision (the contract was already logged). Record: [ARCHITECTURE-REVIEW.md](../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.1 — the repeated switch while a predecessor finalizes".

**2026-10-01 — `runtime-working-set-ownership` S4.4 CHECKED: the declared-cap policy is settled as accept-and-schedule, and the breach report now names its holder (firmware, diagnostic-only; RAM1 byte-identical).** S4.4's refuse-vs-queue question is answered by the change's logged contract — **DEC-078 item 1** (acceptance unconditional, execution scheduled) plus `IMPLEMENTATION_CONTRACT.md` §2 (a changed admission envelope returns to the architect; no pre-provisioning) — so the activation proceeds and the interaction layer queues it, while the enforcing caps stay the lease's own. The slice implements that policy's consequences: `Track::reportEnvelopeBreachIfAny` returns the holder slot (`kNoWorkingSetClassHolder` when none) and the row carries `holder=` (`#MEM_DENIED,…,reason=cap_reached,class=<bit>,holder=<slot0|255>,…`), and the `PIO_UNIT_TEST_NATIVE` branch records what was reported, making the policy path host-assertable for the first time. Fixtures: the tightened breach fixture (empty census reports nothing; the check allocates no runtime for the slot it inspects and takes no pool chunks; the breach names slot 0 with class bit 1; the admission proceeds; the production admission reports it) and new `test_s4_4_the_breach_report_names_the_finalizing_holder_instead_of_hiding_it` (the required finalizer-plus-incoming pair is reported with the **finalizing** slot as holder, and is admitted). **Red-proved three times, restored byte-identically:** holder dropped; finalizing holders excluded (which discriminates the two candidate corrections); the census made to allocate. Focused suite **31/31**; full native **2,646 cases / 2,639 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS**, RAM1 line unchanged (`padding 1,928`, floor 840). **Recorded:** the census still counts a stopped capture as a `LiveCapture` holder (its correction is S4b's role transfer), nothing is refused or queued *by this check*, and the new `holder=` field has no device observation yet. Recorded as **DEC-100**; record: [ARCHITECTURE-REVIEW.md](../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4.4 — the declared cap policy: accept-and-schedule, and the report names its holder".

**2026-10-01 — `runtime-working-set-ownership` S4.2 CHECKED: the generation-mismatched release is rejected on the lease path and named as the device row names it (test-only; no `src/` change).** The task's **A-releases / C-reuses / late-A-cleanup** sequence gains its two missing halves: `test_s4_2_a_retired_owners_late_release_cannot_release_a_successors_working_set` (A admits through the production record-arm admission → A retires through the track-level sweep → C reuses the same runtime object with its own identity → A's late release must answer `StaleGeneration`, *not* the silent `NotHeld`, with C's lease/roles/generation untouched and the successor's identity releasing as the control) and `test_s4_2_the_release_outcomes_are_named_as_the_device_row_names_them` (the emitter's `released` / `refused_busy` / `stale_generation` / `not_held` literals — the vocabulary the device reader decodes). **Red-proved twice, restored byte-identically:** removing `releaseSlot`'s identity check fails the new fixture plus three existing ones; renaming the `stale_generation` literal fails the vocabulary fixture. Focused suite **30/30**; full native **2,645 cases / 2,638 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS**, hex byte-identical to the recorded image, RAM1 line unchanged (`padding 1,928`, floor 840). **Recorded:** a device run with a *stale* release has never been observed (the corpus carries only `released` rows), the two identities stay unaliased by design, and the `NotHeld` silence is a sweep rule, so it is device-observable only. Record: [ARCHITECTURE-REVIEW.md](../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4.2 — the generation-mismatched release, rejected and named".

**2026-10-01 — `runtime-working-set-ownership` S4.1's device leg is closed: the working-set lease release row is observed for the slot under test (host tooling + one authorized device campaign; no firmware change).** The obligation S4.1 recorded — a run in which `releaseEligiblePlaybackRuntimeSlots` fires for the slot under test, read by an oracle rather than inferred — is discharged, and the route it fires on is corrected by measurement: it is the **playback-reset/undo path**, not the cross-track or same-track-switch shapes the five 2026-09-27 probe attempts drove (a same-track stop's *retained* route makes `invalidatePlaybackMergedMidiEvents` return early while a deferred stop job is live). `#CAP,3794271032,DIAG,ws_release,0,3,released` in the PASS-verdict base run `captures/session_20260930_142350.log` (hex `577841d6…`), reproduced three times at HEAD (`git_commit f63d5a02`, same hex) each 1.2 ms after the device's own `Button press: Record/Overdub (double)` → `MIDI: Undo (entries=3)` edge, for `Loop 1` = firmware slot 0. New owner `hitl.lease_release` + reader `scripts/hitl_s41_lease_release_read.py` + `scripts/test_lease_release.py` **13/13**; `hitl.retention_probe` now takes the wire form from that owner (43/43). The campaign also found and fixed a host defect: `drain_record_evidence` reconciled every counted family against a trio-only row counter, so task 2.2's latch family raised `KeyError: 'DIAG,latch'` and turned verified base runs into `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`; `test_managed_record_drain` **23/23** with the mutation red-proved. No firmware, behaviour, SD or content change beyond the authorized scoped clear of track 5 slot 1 (the base preset's own target). Recorded as **DEC-099**. Evidence: [EVIDENCE.md](../openspec/changes/runtime-working-set-ownership/evidence/2026-10-01-s41-lease-release/EVIDENCE.md).

**2026-09-30 — HITL MIDI clock: read and drive, with the "no USB MIDI clock out" premise corrected (DEC-097).**
The HITL host layer had recorded the board as emitting no USB MIDI clock, which is why bar pacing fell back to a
serial wall-clock proxy whose tempo source the mandated build never emits (`#CAP,BPM`, `SESSION_CAPTURE_BPM`
defaults to 0). Device measurement: **48.00 Hz = exactly 24 PPQN at 120.00 BPM**, 384/384 pulses per 8 s, jitter
0.72–1.55 ms stdev, `Start` ~0.31 s after a transport press; the host can also drive the clock, and the device
slaves (0 own pulses while driven, own clock resumed 0.53 s after release). The real constraints are narrower:
clock exists only while the device's transport runs, and a bare realtime `Start`/`Continue` without clock parks it
in `CLOCK_EXTERNAL` where it emits none. Shipped `scripts/hitl/midi_clock.py` (phase-locked reader with
`bpm()`/`wait_pulse`/`wait_bars`, absolute-deadline master with a slaved-verdict `echo_reader`),
`scripts/test_midi_clock.py` **10/10**, `scripts/hitl_midi_clock_probe.py` **device PASS**, and
`docs/Guides/HITL_MIDI_CLOCK.md`. Residual: the legacy streamer is not yet pulse-paced.
**2026-09-30 — S4f.17 sub-slice: a deferred overdub stop publishes its interval's undo level.** The sidebar `U:`
(`TrackUndo::undoDepthForLoop`) counted only the record pass while a loop was live — `U:01` after recording plus
overdubs against `U:03` after a reboot — and the base check `undo_redo_after_overdub_stop` logged `0 counts`
against `expected_min: 1`. The deferred overdub stop drive prepared the interval's `OverdubPassAdded` entry only
in the `CompanionHistory`/`UndoCollection` phases, so three correct production routes committed the pass with no
entry: the **clean-stop jump** (`PrepareTransfer` with `SealOutcome::SkippedEmpty`; `stop_phase` trail
`1,6,10,11`, the dominant device route), the **STOPPED destination without verified companion preparation** (its
fallback published no entry by design), and the **superseded causal base**. A late record start quantized up to 3
bars is the same defect reached through timing. Fixed by a handoff at the top of the `AtomicTransfer` phase (once
per job) that prepares the canonical entry from the interval's own session-exit receipt
(`Loop::gatherSessionExitReceiptUndoIdentities`, DEC-068) plus any staged final pass and drives the protected
transaction to `Ready`, so every transfer variant publishes what it prepared; `job.undoPublicationReady` now means
"publishable", and `canPublish(…, kInvalidPassId)` means "this turn commits no pass of its own". Fail-open when the
interval names no identity, fail-closed on a refused `begin`. Fixture
`test_stopped_overdub_stop_publishes_one_undo_entry` (four legs incl. the clean stop); **seven mutations
red-proved**, each restored byte-identically. Full native **2 637 cases / 2 630 succeeded / 7 skipped / 0 failed**;
`teensy41-capture-serial` **SUCCESS**, RAM1 padding **1 928** (floor 840). **Device A/B verified 2026-09-30** on
the clean route with an identical command: pre-fix `captures/s4f20-odundo-07` trail `1,6,10,11` and
`undo_entries` 5→5 with no short-route row; fixed `captures/s4f20-odundo-10-1` the same trail, 5→6 and one
`src=deferred_short_route` row; full-route control `captures/s4f20-odundo-09` 5→6 and `MIDI: Undo (entries=3)`
(the reboot's `U:03`). Residual: that run's scenario report is not a clean PASS (its capture closed while the run
was still finishing). Decision **DEC-095**. [Plan](../Plans/overdub_stop_undo_level_short_route_bugfix.md).
**2026-09-29 — S4f.20 slice: the deferred Record publication queues its own session-exit snapshot.** The
deferred route (S4f.17/S4f.21) mirrored the inline Record stop's undo/display/scratch/persistence side
effects but not `Loop::queueSessionExitSnapshot`, the only enumeration of the committed Record pass into
the pending snapshot box; a mid-bar take therefore published the truncated live-record candidate as a
*current* root, which is the "issue 2" device symptom (Record-pass last bar missing from the display,
returning on an overdub undo). Fixed in `Track::stepDeferredRecordPublication` (mode-correct reason), with
`Loop::queueSessionExitSnapshot` taking a pending session-exit receipt only for an *overdub* exit, so a Record
exit always enumerates its own just-committed pass (DEC-068 receipt identity); decision **DEC-094**. Fixtures:
`test_deferred_record_publication_queues_the_record_session_exit_snapshot` (red-proved by removing the call),
`test_record_session_exit_with_a_truncated_candidate_keeps_the_take_tail` and
`test_record_session_exit_ignores_a_pending_overdub_receipt` (the latter red-proved by restoring the unfiltered
receipt selection). Focused **198/198**; full native **2 636 cases / 2 629 succeeded / 7 skipped / 0 failed**;
`teensy41-capture-serial` **SUCCESS**, RAM1 padding **1,928** (floor 840, unchanged). **Device verified
2026-09-29** on the authoritative base HITL (`STIM-BASE-2-2-01`, reused), three runs: the fixed image
**passes** (`overall_ok=true`) and publishes a **complete** root — `from=snapshot,notes=32`, `srcspan
count=32`, `48` after the first overdub (32 + 16), `DISP frame=48 visual=32`; the fix's mechanism shows as a
`kind=publish,a=1` root right after the record stop, *before* the first overdub. On the **matched**
pre-fix A/B (both 3-bar takes whose notes cross the completed-bar frontier 1536) the same stimulus
reproduces the reported symptom: **26/32** Record notes (omitted 74–79 at ticks 1530–1770 = the frontier
set) and `visual=42` instead of 48. Residuals, all open and *not* this slice's: (a) **28/32 seen once**
(fixed 2-bar run; not reproduced in the fixed 3-bar run, which gave 32/32) — that run's distinguishing
feature is a ~5× divergence of the host's wall-clock tempo proxy; (b) **take length is host stop-press
timing** — the same fixed image produced 2-bar (raw 1891, inside the 384-tick bar grace) and 3-bar (raw
1997) takes, since the length is stamped in the stop *admission* before the publication queue; the
instrument for the residual ~0.5-bar late stop is the host's strict `--require-midi-clock` mode; (c) the
DEC-073 coverage watermark is written without a committed-inventory completeness check, and
`queueSessionExitSnapshot` re-arms a parked box's companion batch.
[Plan](../Plans/record_session_exit_snapshot_queue_deferred_publication_bugfix.md).

**2026-09-29 — S4f.22 device legs: the Record leg passes; capture loss is still under attribution.**
Uploaded the candidate (`teensy41-capture-serial`, RAM1 padding 2,072) and ran the registered
base preset. Attempt **a1** (fresh upload + boot reset) is a **PASS**: integrity COMPLETE,
`issues []`, all five transition checks green, drain complete (RECA 1 / RECS 7 / RID 1
reconciled) and **32 `REVT` rows** present. Repeat attempts on the same boot did not pass:
**a2** `INCONCLUSIVE_CAPTURE` with `RING,wiped,1,15981` and **a3** `FAIL`
(`second_overdub_first_note_missing`). Evidence in `captures/s4f22-step3-base-0{1,2}` and
`captures/s4f22-step4-a{1,2,3}`. Repeatability is **NOT proven**; the remaining ring wipe is
unattributed and the loss totals are now cumulative for the next run. No deployment claim.

**2026-09-29 — evidence reliability before the next device attempt (step 2).**
The in-flight S4f.21/S4f.22 slices were reviewed and checkpointed in six commits.
Three evidence-reliability defects were then fixed, all host-verified: the Record
REVT scan drains while a loop is playing (bounded chunk walk, 32 events/turn; the
full-merge fallback and stored-MIDI verification still require an idle transport);
the ring reserves 32 KiB that ordinary traffic cannot consume, so a take's
`RECA`/`RECS`/`RID` rows cannot be displaced by another take's; and a missing
telemetry row is no longer reported as a firmware contradiction (`EVIDENCE_MISSING`
/ `INCONCLUSIVE_CAPTURE`, with `assertions.evidence_gap_issues`), while a missing
note *event*, a length mismatch and a short span/count/bar set stay contradictions.
Full native **2,628 cases / 2,621 succeeded / 7 skipped**; `teensy41-capture-serial`
**SUCCESS**, RAM1 padding **2,072** (floor 840); both new native fixtures red-proved
by mutation. No upload and no device acceptance is claimed yet.

**2026-09-29 — S4f.21 — Record playback invalidation retarget defect fixed.**
Run 08's two immediate `Record stop retarget` rows led to a native reproduction:
`completeAdmittedRecordStop` launches playback through a cache reset, but
`invalidatePlaybackMergedMidiEvents` treated every pending job as a transport
Stop. The track returned PLAYING while its Record job already targeted STOPPED
and had suppressed MIDI output. The new fixture fails on that destination before
the fix. Ordinary invalidation now preserves a Record job's scratch, destination
and output barrier; explicit Stop and overdub recovery retain their existing paths.
The fixture also asserts an audible barrier state and a live same-slot overdub
successor while Record still finalizes. Focused 156/156; full native 2,619 passed /
7 skipped; capture-serial build PASS, padding 2,072, variables 96,192, locals 34,880.
Not uploaded or device-verified in that slice. Clear authorization for all slots on
all tracks was granted on 2026-09-29; that slice made no device mutation. [Cutover packet](../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md#s4f21--record-playback-invalidation-is-not-transport-stop-2026-09-29).


**2026-09-29 — S4f.22 — same-session Record evidence drain.**
Run 08 counter evidence reports seven RECS enqueues with zero transmissions,
refusals or evictions. The baseline previously waited only for host serial quiet
before stopping its reader. End-of-leg cleanup now runs before MIDI/reader close:
idempotent MIDI Stop, stopped-only fenced replies, repeated bounded trace drains,
selected finalizer-idle check and transmission-delta reconciliation against this
same log. Counter snapshots use their exact fence endpoint; historical tx counts
do not require reboot. Failed retrieval remains incomplete and preserves the
original leg abort. This does not add Stop within the measured sequence or prove
persistence completion. Device-exercised read-only on 2026-09-29:
`captures/s4f22-base-device-17` reports `record_evidence_drain.complete=true` with
RECA 1 / RECS 7 / RID 1 reconciled against that same log. The device loss mechanism
itself remains unproven, and that follow-up uploaded nothing. [Cutover packet](../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md#s4f22--same-session-record-evidence-drain-2026-09-29).


**2026-09-29 — S4f.22 — Record evidence loss accounting, native verified.**
The real capture-ring fixture disproves the previous attribution to critical
reentry/release traffic: those diagnostics are Tier-A, not critical, and cannot
evict a critical Record row. `DebugSessionCapture` now counts RECA/RECS emitter
entries, enqueue successes/refusals, transmissions and evictions alongside RID;
read-only `!CAP_IDENTITY_STATS` returns all three families. Critical append
refusal (including ring allocation failure) latches a safe-flush notice that makes
host `capture_integrity` INCOMPLETE. No retention policy changed. Four real-ring
fixtures pass, including USB backpressure and actual critical-to-critical eviction;
removing the refusal latch fails the allocation-failure fixture. Host integrity
16/16 and forwarding/identity 40/40; full native 2,618 passed / 7 skipped;
capture-serial build PASS, padding 1,992 (floor 840), variables 96,192, locals 34,880.
No upload, SD mutation or device acceptance. The actual device loss mechanism
remains unproven; next compare stopped before/after family snapshots around the
registered baseline. [Execution record](../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md#s4f22--record-evidence-loss-accounting-2026-09-29).


**Current handoff — S4.1 verified; F4 completion-failure policy shipped (2026-09-26).**
**F4 / DEC-091 (firmware):** a persistently failing workspace completion is now **discharged and reported**
instead of re-queued at the queue head, where it blocked every other persistence item and kept
`hasDeferredSaveWork()` true for the rest of the boot — refusing the save-new-set pre-flight and making the
read-only slot-payload observation answer `busy`. The per-slot dirty set stays for the next save trigger.
Focused `test_storage_drain_restore` 15/15, red-proved (with a false green in the fixture's first cut
recorded); full native **2,469 passed / 7 skipped**; capture-serial SUCCESS with `padding 856`
(byte-identical — the named queue API the first cut added cost 256 B of ITCM and was withdrawn). Two
consequences recorded: the blocking drain reports `Failed` in the mixed failed-save/gated-restore shape (the
S2b.7 parked-instance fixture re-premised and pinning it), and body/bundle item failures keep the retry.
**Device evidence owed** (SD fault path, no fault injection). **S4.1 is checked** with its device row owed as
evidence. See
[continuation packet](../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md).


**Previous handoff — S4.1 lease release verified and S4.1 checked (2026-09-26).**
**S4.1 — working-set lease release on a same-track slot switch is verified with no firmware change.** Three
fixtures in `test_runtime_working_set_ownership` (23/23) drive the production switch commit: the retired
slot is released (generation advanced, pre-switch identity consumed) while a live-owner-protected sibling is
untouched; a live finalization refuses with `RefusedBusy` and the lease survives the switch that still
commits; and after retirement the **next** same-track switch releases it (retirement itself releases no
working set — it arms the capacity marker). The live-finalization protection is triple-layered and each
layer was red-proved by staged guard removal. Full native 2,468 passed / 7 skipped; capture-serial SUCCESS
with `padding 856`; `src/` untouched. S4.1's device leg is now an **evidence obligation only**: the
`DIAG,ws_release,<slot>,<generation>,<outcome>` row exists and is Tier-A, so what is owed is a run in which
that path fires plus an oracle that reads it. Next recommended native work: the `FinalizeWorkspace` retry
policy. See
[continuation packet](../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md).


**Previous handoff — S4.1 persistence identity verified; recovery/DMA unchanged (2026-09-26).**
**S4.1 — persistence identity during handoff is verified and closed with no firmware change:** the
deferred save's footer records the workspace **focus** at footer-write time while the interval's slot
remains the **content** identity; four new cases in `test_storage_drain_restore` (paused worker at the
footer, A→B handoff through the production selection route, reload, no-switch control, post-footer handoff,
SD write fault) assert it, and the substitution the packet warned against red-proves as a reverted focus.
Full native 2,465 passed / 7 skipped; capture-serial SUCCESS with `padding 856` (floor 840); `src/`
untouched. **DEC-090**. Next recommended native work: S4.1's remaining working-set **lease** release on a
same-track slot switch, then the `FinalizeWorkspace` retry-policy question found while building the
fixture. See
[continuation packet](../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md).

Recovery/loaded undo is committed in `f29539e3`. Automatic repair and healthy boot
completed on hardware; full device content/Undo/Redo acceptance remains open.
Blocking-SPI display update is user-confirmed. **User parks DMA repair until after
`runtime-working-set-ownership` completes.** Default firmware still enables DMA;
use the preserved comparator profile when an explicitly approved upload must retain
the working display. No timing gate is waived.

## Earlier session checkpoints


**2026-09-26 — blocking-SPI visual comparison PASS (user observed).**
User confirms “Yes, the screen updates now.” Same recovery/renderer source, existing
`MIDILOOPER_DISPLAY_DMA=0` switch only. Device reached load=ok, restore admission
complete, undo hydrated and transport stopped. This isolates the frozen-screen
behavior to the DMA display route; it does not establish the precise DMA defect.
Blocking SPI remains installed as a diagnostic workaround; no real-time timing
acceptance or permanent DMA fix claimed. Serial capture remains open.
Evidence: `captures/display-blocking-diagnostic-20260926/metadata.json` and session log.

**2026-09-26 — recovery firmware uploaded with explicit user authorization.**
`teensy41-capture-serial` upload SUCCESS; SHA256
`4b13749ab9d97b0699fb02f813a3a9a26b7475a60391e19ffa784c21cec93255`
matches the host-verified image. Confirmed standalone read-only reader before upload;
archived and cleared the incident hold for this approved recovery installation.
Device reports runtime metadata repaired, `BOOT,load,ok`, and a settled checkpoint:
`admission_complete=1`, `interactive_ready=1`, `undo_hydrated=1`,
`transport_stopped=1`, `deferred_prewarm_idle=1`, `free_chunks=391`.
Serial held open through verified persistence idle, then closed. No content/clear
stimuli. This establishes successful recovery boot, not per-loop content parity,
hardware Undo/Redo roundtrips or second-boot acceptance; those remain open.
Evidence: active-change workspace-loss `authorized-recovery-upload/`.


**2026-09-26 — automatic boot metadata repair implemented and host-verified.**
DEC-089 — boot metadata repair validates the complete workspace before generating
runtime metadata; preserves source files and corrupt originals; blocks saves after
refusal. Actual boot owner restores the copied 64-slot library and derives 122 undo
actions; healthy reboot retains them. Six isolated cases include 276 interruption
cuts. Full native 2,461 passed / 7 skipped; Python 16; capture-serial SUCCESS with
RAM1 padding 856 (floor 840). No upload/device action; hardware acceptance and the
incident hold remain open. Evidence: active-change
`evidence/2026-09-26-workspace-loss/BOOT-REPAIR.md`.


**2026-09-26 — standalone USB folder reader delivered and exercised:** `/MidiLooper` copied
read-only with the SD card in place: 1,007 entries / 6,837,184 bytes, checksums verified,
one duplicate path preserved separately. `tools/midilooper-reader/README.md` describes the
utility and verification. Reader remains installed; recovery and normal-looper boot are
still held. This is file-copy delivery, not workspace recovery or capacity acceptance.

**2026-09-25 — S1.6c.2 — snapshot construction reuse implemented, device gate open.** Candidate-private pitch/page/membership edits now persist across one construction (DEC-085), with cached tail/count updates and cached-event sort-state reset. Native **2,402 passed / 1 skipped**, focused snapshot **42/42**, target build green; RAM1 unchanged (code391,368 + padding1,848; variables96,160; locals34,912). Image `94b09b51…`. Fresh N=56 census: external payload **7,006,576 → 3,238,256 bytes**, snapshot slabs **151 → 36**; all 64 observed slot records match the earlier census (both lack consistent-view attestation). This saves **3,768,320 bytes (~3.59 MiB)** without deleting content or shrinking the capture ring.

The stress rerun is **INVALID_TARGET_INTERRUPTED**, not acceptance: configured track8/slot4 but the no-clear harness armed selected **track8/slot1**, recorded/overdubbed and persisted `slot:7:0`. Interrupted and explicitly stopped; `STOP_STATUS` confirms STOPPED/pending=0. No clear/delete/rename or cleanup. User reports device85.4BPM, explaining host24-bar timing yielding17devicebars (13,056ticks). No fatal/external refusal, but seven late `snapshot_reserve/heap_low` rows and minimum heap tail12KiB mean overall heap correctness remains open. Host precondition now fails before gestures unless the latest device-time DISP since track selection verifies the configured slot; host tests56/56. No further content runs until the selection fixture is corrected. Role-specific reservations remain unchanged pending their transition/admission contract. Evidence: `captures/snapshot-reuse-preflight/`, `captures/snapshot-reuse-24bar/`, `captures/snapshot-reuse-image/`. Recorded in local runtime checkpoints; capacity acceptance remains open.


**2026-09-25 — S1.6c.1 — known owner lifetime fixes delivered in the working tree:** whole-take release now frees origin/rank side arrays (24,576 bytes of previously retained payload); draw-source compose reuses pending-paint backing, filters slide inclusion in place, and retires obsolete paint before allocating a larger copy. Selected-bridge authority and compose/paint separation preserved. Red→green regressions; focused 301/301, full native **2,399 passed / 1 skipped**; capture-serial SUCCESS, RAM budgets unchanged. Final image `10695e07…`. **Capacity is not fixed:** the same N=56 24-bar HITL (`captures/heap-ownerfix-24bar-02`) still fatals ~47.62 s into RECORD on a 6,096-byte DisplayNote vector reserve, before overdub. S1.6c — owner correction/capacity revalidation remains open. No live heap scan or content-clearing workaround. Board recovered: N=56, chunks403, target empty, zero differences in compared content fields across 64 slots. Edits uncommitted.


**2026-09-25 — S1.6b — original workload rerun:** same effective N=56 24-bar record/24-bar overdub configuration, user track 8 slot 4, no CLEAR, image `a71b50eb…`. `captures/heap-rerun-24bar-01/` reproduces fatal exhaustion ~32.08 s into RECORD: 8192-byte DisplayNote vector growth request, caller `0x6008c919`, external live 8144 KiB / 1176 blocks, reported internal tail 8 KiB. No overdub or record stop reached; harness SERIAL_INCOMPLETE. No heap scan during the workload. Stopped preflight scan complete; no same-boot after-sample because firmware aborted. Recovery returns N=56 / 403 free chunks, target empty, zero differences in 10 compared content fields across 64 slots (not a byte/write guarantee; observation consistent_view=false is not a new stability proof). Exact display container and leak/fragmentation remain unresolved. Evidence: active runtime review packet and `captures/heap-rerun-24bar-01/result-summary.json`. No firmware/script changes in this rerun; OpenSpec gates remain unchanged.


**2026-09-25 — S1.6d — stopped heap diagnostics:** added capture-only `!HEAP_STATS` under MemoryMonitor. No periodic/live allocator inspection or allocation probes. Refuses transport, active tracks/capture/stop work, deferred storage/prewarm, selected external clock, and pulses received within the last second. Raw allocator chunk/gap metrics, unused internal tail, completeness and scan duration are separate. DEC-084 records the explicit stopped/clock-quiet exception. Native **2,394 passed / 1 skipped**, focused diagnostic **5/5**, capture-serial build green (RAM1 variables 96,160; code 391,368; padding 1,848; locals 34,912). Initial stopped readings complete in ~49 ms; final stopped/clock-refusal device gate passed in `captures/stopped-heap-03/` (exact refusal, no scan). S1.6d is checked. Work remains uncommitted. This adds evidence, not a malloc leak verdict or the S1.6c — owner correction; capacity gates remain open.


**2026-09-25 — S1.6 heap correctness:** physical external accounting now excludes internal
fallback, balances tags on address reuse, and reuses saturated-table tombstones. Three native
red regressions → 5/5; full native 2,389 passed / 1 skipped; capture-serial green and flashed,
RAM1 unchanged. Capacity sweeps remain paused. The N=56 canonical 24-bar run still fails in
recording: 5,216-byte request from `DisplayManager::resolveDisplayNotesByDrawSource`, resolved
against pinned ELF, external live8,142KB with no accounting misses. This is not attribution of
the older 8KB request or proof of a leak. S1.6a is complete; S1.6b allocation/lifetime attribution
and S1.6c owner fix remain open, as do S1.5 overdub refusal and S2b.3 workload-peak budget.
See active runtime-working-set-ownership ARCHITECTURE-REVIEW.md § S1.6 and
`captures/heap-attribution-24bar-01/allocation-attribution.json`. Earlier lower-N/reclassify
recommendations are superseded by this heap-correctness priority. Work is uncommitted.


> Project goal and decision log: [Authority/PROJECT_INTENT.md](Authority/PROJECT_INTENT.md). This page is the single shipped-vs-next overview.

## Branch verification — 2026-09-25 (uncommitted; not delivered to dev)

**2026-09-25 — S1.3 residency slope COMPLETE: dense probe reached hard allocation failure.**
The fixed-role two-bar slope remains N=10..56 (46 verified additions). A separate dense
four-bar probe at N=56, user track 8 slot 4, supplied a one-clock note cadence across pitches
0–127. Recording started; at 41.448245 s device time, before record stop, the allocator refused
8,192 bytes with 4 KB internal heap free (`tag=0`) and emitted the fatal exhaustion line.
This is the measured failure point for this library/workload, not a universal smallest N or a
64-bar capacity claim. The untagged allocating caller is not identified. The last sampled free
chunk count was 393: do not classify this as chunk-count exhaustion.

The failed take had no verified persisted payload; the fixture withheld its survival reboot.
Separate crash recovery and a final read-only boot confirm the original 56 loaded slots,
unchanged other observed content fields, matching boot roles, and an empty failed target.
Board is stopped at track 6 slot 3. No clear/delete/rename or existing-slot overwrite.

S1.3 is checked: measured slope plus hard failure/error, alongside the previously completed
synthetic 60-descriptor characterization. S1.5 — 24-bar overdub refusal, S1.4 — undo/pin baseline,
S2b.3 — workload-peak budget and the other device gates remain open. No S3 — lease ownership start.
Host tests **42/42**, OpenSpec/diff checks pass. No firmware edits/build this slice; pinned image
`b289af534577e25b57c52146b27f66c245c97f43a5028fc10a37b9a8a9bbd208` reused.
Evidence: `captures/s13-dense-evidence/pressure-result.json`, `captures/s13-dense-t8s4-01/`,
`captures/s13-dense-recovery-01/result.json`; existing slope in `captures/s13-slope-evidence/extended-slope.json`.
Work remains uncommitted on `feature/runtime-working-set-ownership` (HEAD `1ccff16a`).

<!-- S13_EXTENDED_PROGRESS_BEGIN -->
**Historical checkpoint — superseded by dense-probe completion above: S1.3 extended sweep N=10..56.**
46 two-bar additions passed exact 62-event / 1,536-tick SD/RAM survival before and after reboot.
All boot roles stayed fixed; no other observed content fields changed; 45 repeated intermediate
owner baselines matched exactly. Free chunks 449->403; snapshot telemetry 2,187->4,196 KB;
slab-used 2,491->4,774 KB; tracked live 4,221->6,846 KB. Tracked live/high includes the core
`extmem_malloc` internal fallback and is NOT a physical PSRAM-free oracle.

First named denial occurred at N=55 while recording the 56th slot (user track 8 slot 3):
`snapshot_reserve / heap_low`, 40,960-byte margin + 32,768-byte floor, heap 48 then 44 KB;
17 denial lines, zero wrapper allocation refusals. Publication deferred; the take persisted in
legacy format and survived reboot exactly. This is a temporary publication-admission boundary,
not a hard residency/canonical failure. S1.3 remained open at that earlier checkpoint;
S1.5 — 24-bar overdub refusal and S2b.3 — workload-peak reserve remain OPEN. S3 is not started.

Growth stopped at that boundary. The supervisor initially advanced because survival passed;
the next target (track 8 slot 4) was interrupted during preflight before recording. The host
runner now returns a stop status on named memory denials even if survival succeeds. Final fresh
read-only boot confirms 56 loaded, same observed content/roles, stopped focus track 6 slot 3.
No clearing/deleting/renaming or populated-slot overwrite. User authorization for empty slots
was sufficient; this stop is a measured boundary, not exhausted authorization.

Evidence: `captures/s13-slope-evidence/extended-slope.json`, `extended-episode-peaks.json`,
`captures/s13-empty-anchor-t8s3-01/`, `captures/s13-sweep-closeout-01/`. Host tests 40/40;
OpenSpec validates. Firmware unchanged this slice (SHA256 `b289af53…`, code 391,304 / padding
1,912); prior native evidence 2,384 passed / 1 skipped remains applicable, not newly rerun.
Changes remain uncommitted on `feature/runtime-working-set-ownership`.
<!-- S13_EXTENDED_PROGRESS_END -->

S1.3 now has a controlled two-bar range **10–15 loaded slots**, five successful added-slot
survival checks, and four exact repeated owner baselines. Free chunks 449->444; live telemetry
4,221->4,799 KB with a nonuniform increment at N=12. The approved empty scratch range is
exhausted, not the device capacity. S1.3/S1.5 remain open until a genuine refusal boundary is
measured with further authorized targets. Firmware unchanged in this slice.

The S1.3 one-slot fixture now passes stored-event parity and reboot survival: track 6 slot 3,
62 events / 1,536 ticks unchanged, loaded union 9 -> 10. The capture-gated bounded SD-page
observer is device exercised. Host target decoding now supports legacy and LSV1 files through an explicit Teensy-event ABI
adapter and production semantic parsers; a captured synthetic LSV1 golden fixture pins interoperability. S1.3 and S1.5
remain open; no lease stage starts. See the active architecture-review closeout and the two
`captures/s13-controlled-*` evidence directories.

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
(proposed 2026-09-24; **Packet A — observation-only firmware plus read-only analysis — 1.0–1.5 COMPLETE and
device-verified 2026-09-24**; G2 review two rounds — BLOCKED then corrected) owns it. A per-slot
**observed current state** (`empty` / `populated` / `pending`) is reported
**separately** from a **nullable historical cause** (`catalog omission` / `restore refusal` / `intentional
eviction` / `content loss` / `ambiguous` / `unknown`) — `empty` is a state, never a cause; four distinct
quantities with an explicit
observation **window**, collected through a **bounded on-demand observation** with a declared owner, service
point that runs **independently of normal load admission**, SD-read admission, and an **invalidation/restart** rule covering request-generation change, set-epoch change **and content revision change** (a publication installs content under the same request identity),
independent of the settled census
gates; one fixture inside the approved scratch range with per-slot authorization for any CLEAR and stability
proven by a **complete per-slot manifest comparison**; and it carries the S2b.7 device confirmation as an
independently reportable packet. **Corrected findings:** `n_committed_slots` is a **window-scoped count of
observed `RRQ … committed` completions** with background restores included — *not* the boot playback set, so
the earlier explanation is withdrawn and a count difference does not establish loss; `sets/index.bin` is a
saved-set folder counter (`SetIndex{nextSequence}`); the runs that appeared to shrink report
`reconcile_warnings=0`; `DIAG,lcr,skip,restore` is content-resolution; `Track N loaded state: EMPTY` is
present on stable boots too; and both sweeps' `library_rows` are all-zero. **The corrected measurement
definitions come first — they may explain part of the apparent instability without a firmware fix.**

**`restored-library-stability` Packet A (1.0–1.5) is COMPLETE — observation-only firmware plus read-only
analysis.** Delivered: the corrected measurement definitions with citations; a pure two-output classifier
(observed **current state set** vs a **nullable historical cause**, `empty` never a cause) with 13 native
cases and a 13(+3)-test Python mirror; a **bounded on-demand observation** owned by `StorageManager`,
serviced at the unconditional idle tail in `main` (independent of load admission), requested by
`!LIB_OBSERVE`, emitted as the `LIBOBS` family, with SD-session admission, a 2-slot/2-probe/2-record
per-invocation bound and `(requestGeneration, setEpoch)` **plus a content revision identity** re-checked
before each record; 8 native cases through the real service hook with two mutation proofs. **Native
2,374 / 1 skipped / 0 failures** (after the `LIBOBS` Tier-A/length fix); `teensy41-capture-serial` SUCCESS keeping the approved **12-bank** ITCM
line (`code 391,160 + padding 2,056`, floor 840 met with 1,216 B headroom, locals 34,880); the whole
observation costs **+80 B of ITCM**. **Device-verified read-only** (`captures/s13-libobs-full-02/`):
**payload inventory 36, initial playback-required 8, restore completions in the window 0, committed at the
checkpoint 8**; 36 `populated` / 28 `empty` with **no loss cause anywhere**; the initial-playback set is one
slot per track and **4** of its 8 slots hold in-RAM committed content at the checkpoint
(`committed_at_checkpoint = 8` is a partly different set) **[corrected 2026-09-24, task 2.2 run]**; payloads
on tracks 0–5 only. The read-only re-analysis of the
recorded sweeps (`captures/s13-library-observation.json`) finds **no loss cause** and identifies `N` as
**window-scoped**. The incomplete-window case reports incomplete with all four counts `None`. **Transferred
out of the change after Packet B 2.2:** task **2.3** — the S2b.7-condition fixture, **Packet C** — the S2b.7
drain device confirmation, and **4.2** — the stale drain clause; all three now live, unchecked, in
`runtime-working-set-ownership` S2b.7 (see Packet B below).

**`restored-library-stability` Packet B 2.2 — complete-manifest stability: DEVICE VERIFIED PASS
(2026-09-24).** Reuse first: the five existing `LIBOBS` captures settle only **two** eligible boots
(`full-02`, `incomplete-03`) — `full-01` is the truncated pre-fix format, `incomplete-02` an incomplete
window, and `incomplete-01` a **same-boot** mid-session observation, not a boot — so three consecutive
read-only boots were captured (`captures/s13-libobs-stab-01/02/03`, upload reset before each, only
`!LIB_OBSERVE` sent). All three match the expected manifest recorded from `full-02` **in full**: 0
manifest / lifecycle / settled-count differences over 64/64 slots, observed manifest sha256
`4e451a07d0a94c18…`, five complete-manifest observations agreeing. **Recorded precisely: no differences in
the observed fields; no destructive commands issued** — this does **not** prove unchanged payload bytes or
the absence of a write. **36 / 8 / 8 disposition:** 36 payload-present / 8 in the **pass** representation /
8 initial-playback-required are three populations (the two 8-sets overlap in only 4 slots), and the 28
remaining payload-present slots are **published-root-backed** — distinct from the other 28 payload-absent
slots; the runtime
slope fixture varies the **loaded** population and reports the two representations separately.
**Corrected 2026-09-24 (seventeenth session, on device): that reading is CONTRADICTED — the corrected
per-slot telemetry reports 9 content-bearing slots of 36 verified payloads, and this gate's
`committed_at_checkpoint=8` was a subset only of those. Its repeatability is repeatability of the fields
measured, never of a new payload surviving a reboot.** **Task 2.1 is closed
as not required for the reused fixture** (nothing was cleared). New read-only tooling
`scripts/hitl/library_manifest.py` + `scripts/hitl_library_manifest_stability.py` (tests **14/14**; **15/15**
after the representation comparator case). The
evidence is handed to `runtime-working-set-ownership` (task 4.1), and this change's two requirements are
synced into `openspec/specs/lazy-slot-hydration/spec.md`. **2.3 — drain fixture, Packet C — drain hardware
confirmation and 4.2 — drain-spec correction were TRANSFERRED to `runtime-working-set-ownership`
S2b.7 — drain-driven restore advancement** (recorded there unchecked, independently reportable), so the
library closeout checklist has **no remaining mandatory task**; the change is **ARCHIVED** through the normal
workflow (2026-09-24). **Next runtime work (recorded, not started at handoff): S1.3 — slope-fixture
definition**, which had to explain why 28 verified payloads read as unloaded and why only four
initial-playback slots overlap the pass-backed set — **answered 2026-09-24 (sixteenth session, inferred):
the 28 were read as published-root-backed and resident; CONTRADICTED on device in the seventeenth session**;
repeatability does not establish correct hydration or a usable
increasing-population fixture.

**`runtime-working-set-ownership` S1.3 — slope-fixture definition (2026-09-24, read-only; the "28 unloaded"
premise is FALSIFIED).** All 36 payload slots are restored (8 pass-backed via `Loop::adoptPersistedSnapshot`,
28 read as **published-root-backed** via `Loop::storePublishedSnapshot` — *this per-slot attribution is an
inference and is **CONTRADICTED** by the seventeenth-session corrected telemetry below; the owner totals it
cites reproduce exactly*); the 28 were read as resident — snapshot arena
0 → **2,155 KB / 78 slabs** and `chunks_free` 480 → **453**, flat after settle — and playback reads the root
(`Loop::tryGatherPublishedSnapshotEventsInWindow`). The observation's `committed_at_checkpoint` is therefore
a **pass-backed subset**, not the loaded population (and is not spec-conformant for root-backed slots). The
four required-but-not-pass-backed slots are `(2,1)`/`(4,0)` (root-backed) and `(6,0)`/`(7,0)`
(payload-absent enabled slots). The controlled increasing-population fixture is **defined** (fix one active
role; add one payload per boot inside the approved scratch range; report `N_payload`/`N_pass`/`N_root` and
reservation/payload/root/directory residency separately). **The slope run and the failure point are not yet
measured.** No device run, no firmware edit.

**`runtime-working-set-ownership` S1.3 — content-representation correction and revalidation (2026-09-24,
seventeenth session; firmware + host, native green, device-flashed).** The observation's
`has_committed_content` / `committed_at_checkpoint` reported only the **pass** representation; it now reports
pass-backed and published-root content **separately** (`kp`/`kr`, plus `kc` for currency) with the union as
the loaded count (`k`/`m`), and invalidation includes the **published-root publication identity**. Full
native **2,378 / 1 skipped / 0 failures**; `teensy41-capture-serial` **SUCCESS** (padding 2,056, above the
840 floor) and flashed; host parser/comparator tests **18/18** and **15/15**. **The revalidation then
contradicts the "28 root-backed" reading:** two independent read-only boots report payload **36**,
initial-playback 8, `committed_at_checkpoint` **9** (`pass_backed` 9, `root_backed` 7), with the other 27
payload slots adopting zero passes and installing no root. The **slope attempt is blocked** at its first
step (a saved scratch payload did not survive reboot; no refusal), so **S1.5 — refusal point stays open with
0 added slots measured**, while S1.1's owner census and S2b.3's post-restore budget landed. Full evidence:
[`Runtime/CURRENT_WORK.md`](Runtime/CURRENT_WORK.md) § Now implementing and
[`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ *S1.3 — observability correction*.

The instrumented observable is protected and documented: `LIBOBS` is **Tier-A** (worst-case records ≤ 191
bytes, locked by `test_capture_line_tier` 15/15), the contract is recorded as **DEC-083**, and the behavior
is owned by [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md#the-on-demand-library-observation)
(plus a `BOOT_LOAD.md` key-files row and `.agents/skills/hitl-content-verification` § 9).

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

### 2026-09-26 — offline recovery audit tooling

Host-only `tools/midilooper-reader/audit_backup.py` verifies the copied manifest and
source-derived container checksums/metadata; seven audit tests plus nine reader tests
pass. Current 64 slot files survive checksum checks; full decoding/restore unverified.
See active change evidence `OFFLINE-RECOVERY-AUDIT.md`. No firmware/device action.

### 2026-09-26 — host production decoder audit

All 64 copied slots decode with a documented Teensy event-layout adapter; three
focused native tests pass. Missing runtime metadata/load recovery remains open.
No firmware change or device access. See active-change PRODUCTION-DECODE-AUDIT.md.

### 2026-09-26 — host workspace reconstruction

628-byte runtime bundle built with reusable C++ writer; all 64 slots restore and
7,840 capture events match byte-for-byte. Candidate preserves all original files.
Host verified, not installed; automatic Teensy recovery remains unimplemented.
See active-change evidence WORKSPACE-RECONSTRUCTION.md for defaults and tests.

## 2026-09-26 — host undo reconstruction acceptance

**2026-09-26 — undo reconstructed and host-tested; reload blocks installation.**
Existing C++ content-history owner derives 122 actions (60 record, 11 overdub,
51 boundary changes) across all 60 nonempty loops. Full Undo → Redo preserves
materialized MIDI bytes, loop lengths and published note identities/content on all
60; four empty slots have no actions. Diagnostic bundle: 8,846 bytes. Production
load/hydration fixture retains only 9 actions after reload, so the wrapper exits 2
and the candidate is not safe to install. Original files byte-identical; firmware
unchanged; no device access; hold remains. Original cross-slot chronology, clear
snapshots and old redo branch remain unrecovered. Evidence: active-change
`evidence/2026-09-26-workspace-loss/UNDO-RECONSTRUCTION.md`.

**2026-09-26 — correction: reconstructed undo after reload needs a policy change.**
The prior “next fix” diagnosis missed DEC-057 — snapshot body format and the active
working-set spec: LSV1 load deliberately starts with undo depth zero. The older
content-history reconstruction spec has a newer snapshot delta that changes it.
The 122-action host recovery remains valid; it is not authorization to reverse the
load contract. Proposed change: derive per-slot undo from sufficient saved passes,
redo empty, saved snapshot authoritative, no persisted undo stack. Approval is
required before that normal-load behavior changes. No firmware/device action.
See active-change `evidence/2026-09-26-workspace-loss/UNDO-LOAD-POLICY-REVIEW.md`.

**2026-09-26 — approved load-time undo implemented; host recovery passes.**
DEC-088 — load-time undo reconstruction supersedes the earlier zero-after-load
policy. All 122 actions derive from saved passes on load/reload with empty undo
headers; all 60 nonempty slots pass Undo/Redo, and background commits preserve
runtime Redo. Redo starts empty and is created by Undo. The isolated runner verifies
hydration before/during/after restore. Candidate reconstructed-undo-04 preserves all
65 original files plus a 628-byte bundle (443,431 bytes total). Capture-serial build
passes with RAM1 padding 840, unchanged. No upload/device/card action; standalone
reader and incident hold remain. Hardware recovery is still unverified. Evidence:
active-change `evidence/2026-09-26-workspace-loss/UNDO-LOAD-FIX.md`.

**Verification complete:** full native 2,461 passed / 5 skipped; focused native 68
passed; recovery load/reload 122 actions and all 60 nonempty slot roundtrips;
Python 16 passed; OpenSpec validation passes. Capture-serial firmware SUCCESS,
RAM1 padding 840. No device actions. See UNDO-LOAD-FIX.md and preserved logs.
