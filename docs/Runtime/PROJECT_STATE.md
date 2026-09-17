# Project state (execution context)

**2026-09-16 task 2.3 causal predecessor/source identity — increment B, approved
superseded rule (resumable-midi-boundary-playback-maintenance).** Option (A) is
implemented and verified. A deferred stop whose recorded `causalBase` is no longer the
live published root retires without publishing a successor window: the staged accepted
capture pass is committed through
`TrackPreparedStopCommit::commitSupersededCausalBase`, no bridge is selected or
published (`causal_base_superseded`), a prepared protected undo entry is published in
the same turn, and the unpublished companion horizon returns to `Prewarmed` through the
new allocation-free `Loop::restoreSupersededPreparedCompanionHistory(owner)` (with
discard-and-reprewarm as fallback). The checks sit at the start of `CompanionBridge`
and `AtomicTransfer`, with `PendingStopAdmission::retireSupersededPreparation()`
retiring the ranked/canonical/undo-collection credits so the job retires instead of
stalling. Implementation finding recorded: resolution must stay live — the canonical
owner identity is compared by address against the live Loop scalars, so the recorded
identity is the supersession reference, not the resolver, and the live revision/root
were measured unchanged during a normal finalization. Full native 2,191 cases — 2,186
passed, 1 skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Recorded deviation: the
superseded route publishes the capture pass only, relying on the newer root to already
carry the companion rows (to be re-verified when successor capture is unguarded).
Remaining in 2.3: successor-capture unguarding, in a separate change. Task 2.3 remains
unchecked.

**2026-09-16 task 2.3 causal predecessor/source identity — increment A
(resumable-midi-boundary-playback-maintenance).** Auditing task 2.3(f) found that a
deferred stop re-derives its predecessor source live at every consumer turn: admission
computes a `PlaybackViewSourceIdentity baseSource` for credit sizing and discards it, the
worker recomputes `baseSource`/`targetSource`/`frozenSource`/`currentSource` when the grain
runs, and the atomic commit recomputes `base` from the live `playbackRevision` and
`publishedSnapshotIdentity()`. A successor publication during finalization could therefore
change what the predecessor resolved against. `CaptureStopJob` now records `causalBase`,
pins its root with `causalBaseSnapshot` (`ImmutableLoopSnapshotRef`) and sets
`causalBaseRecorded`; admission resolves and pins it before the raw-range ownership transfer
and denies `causal_base_unavailable` if a non-null published identity cannot be pinned, and
retirement clears all three. Increment A records only, so behavior is unchanged; increment B
(next) makes the worker and the atomic commit resolve against the recorded identity and
decides the epoch-qualified publication rule including STOPPED recovery. New native
`test_deferred_stop_records_and_pins_its_causal_predecessor_source`; full native 2,191 cases
— 2,186 passed, 1 skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 prepared companion history interval qualification
(resumable-midi-boundary-playback-maintenance).** The Loop-owned prepared companion
history (`preparedCompanionHistory_`) is borrowed worker scratch that outlives one
grain, so a successor interval could previously observe, advance, commit, recycle or
discard a predecessor's in-flight preparation. Every consumer now requires the owning
interval identity, `PendingStopAdmission::intervalToken()` (frozenGeneration +
stopTick) — a value, not an object address, so a copy of an admission made for credit
probing or retargeting still identifies the same interval while a reused ring
descriptor gets a fresh token; zero means the live session path.
`preparedCompanionHistoryOwnedBy()` keeps Cold/Prewarmed receipt-free and
owner-agnostic; `beginPreparedCompanionHistory` records the token and recycle
completion plus `discardPreparedCompanionHistory` clear it. The deferred stop worker,
the stop-commit helpers and `beginCollectOverdubStopUndoPasses` pass
`stop.intervalToken()`; default arguments keep the live path source-compatible. New
native `test_prepared_companion_history_is_job_qualified` covers successor and
live-session refusals, stale discard, receipt ownership and reuse after recycling;
the two Track-level `test_playback_prewarm` fixtures now begin the preparation with
the admission that consumes it, and their stale unqualified consumers were qualified
(rather than deleted) because the qualification is what makes those calls correctly
answer "not mine". This closes the session/preview/undo/snapshot receipt slice. Full
native 2,190 cases — 2,185 passed, 1 skipped, the same three intentional
public-reentry failures; `teensy41-capture-serial` builds with 6,752 bytes RAM1
headroom. Remaining in 2.3: causal predecessor/source identity per job, then
successor-capture unguarding in a separate change. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 session-exit handoff receipt ring
(resumable-midi-boundary-playback-maintenance).** The session-exit
`SessionExitSnapshotHandoff` receipt is now a Loop-owned FIFO ring of
`kSessionExitReceiptCapacity = 4` (external-memory-first, cold-prewarmed by
`Loop::prewarmSessionExitSnapshotHandoff()`), replacing the single-slot unique
pointer. This closes "Finding 2" of the deferred-stop receipt audit: the receipt
outlives the stop job, so it cannot be job-owned. `canHandoffSessionExitSnapshot()`
is now a **free-slot** gate; "one receipt per open session" is enforced inside
`handoffSessionExitSnapshot()` by `sessionExitHandedOff_` (set on handoff, cleared by
`openOverdubSession()`), so a successor session is never serialized behind an
unconsumed publisher turn while a repeat handoff for the same session is refused. The
idle publisher consumes the oldest receipt one per turn (retaining it when
construction is disallowed), and playback's stale-root overlay folds every pending
receipt (passes and companion ids) rather than one. New native
`test_session_exit_receipt_ring_is_fifo_and_not_serialized`; the stale
"unconsumed receipt closes the gate" assertions in `test_playback_prewarm` and
`test_loop_take_survival` were rewritten to the ring contract. Native:
`test_playback_prewarm` 165/165, full suite 2,189 cases — 2,184 passed, 1 skipped, the
same three intentional public-reentry failures; capture-serial builds with 6,752
bytes RAM1 headroom (13 banks, code 425,208). No new boot allocation, so the increment
A/B device memory check (build `91e6b38c…`) still covers this slice. Remaining in 2.3:
prepared-companion history qualification, causal predecessor/source identity, then
successor-capture unguarding in a separate change. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 increments A/B boot abort — ROOT-CAUSED, FIXED, device-verified.**
The memory check found the increment A/B firmware never reached a running sketch
(`a7f10733…` emitted only `boot_reset,crash=1` before the USB port dropped; the
32-entry-prewarm variant `8c03dcfc…` behaved the same, so the undo-stack prewarm was
not the cause). A hardware bisect isolated it to **`0f398715` (increment A)**:
`7a275d1f` boots (21386 bytes, crash=0), `0f398715` and `162df28f` do not (0 bytes).
Root cause: `prewarmCaptureStopJobs` reserved `kRecordsPerBridge` (2048) rows for each
job's `undoPassIds`/`undoCompanionIds`, and both are
`InternalHeapFirstAllocator` vectors — 4 ring entries × 8 tracks × 2 lists × 2048 × 4 B
= **512 KB demanded from a ~340 KB internal heap at boot**. Fixed by bounding that
reserve to `kUndoCollectionReserveRows = 64` (16 KB total); a session longer than that
publishes no prepared undo entry and takes the plain commit. The fixed tip is flashed
and verified: build `91e6b38c…`, 22091 bytes streamed, 0 warnings, prewarm samples
`PSRAM 5,141,952 -> 3,775,872`, `heap 352,256 -> 290,816`, chunks `512 -> 416` — about
79 KB PSRAM and 20 KB internal heap over the reference build
(`captures/task23-undo-prewarm-fix/result.json`). Native: 2188 cases, 2183 passed, one
skipped, the same three reentry failures; build 6752 bytes RAM1 headroom.

**2026-09-16 task 2.3 deferred protected undo publication wired
(resumable-midi-boundary-playback-maintenance):** the normal deferred overdub stop
now records its `OverdubPassAdded` entry. `TrackUndoPublication` is externally
allocated and cold-prewarmed by `prewarmProtectedUndo()`, which also reserves the
undo stack's entry capacity (`Config::ABSOLUTE_MAX_UNDO_ENTRIES`, 512) — a
publication precondition whose device PSRAM admission must be measured.
`stepDeferredUndoPublication` runs `begin` + bounded `stepPrepare` inside the
existing `UndoCollection` phase; the atomic transfer proves `canPublish(...)` then
calls `advanceAtomicTransferWithUndo`, else plain-commits with the transaction
restored. `stepProtectedUndoDrain` drains one bounded quantum per DeferredCleanup
turn (`ReleaseRedo`/`Trim`/`Complete`, plus restore + `recoverPreparedEntry` for an
unpublished transaction), so `protectedState()` is Idle before the job retires.
Tests assert one published `OverdubPassAdded` and an Idle transaction; full native
2,188 cases — 2,183 passed, one skipped, the same three known reentry failures;
capture-serial builds with 6,752 bytes RAM1 headroom. Known deviations: the DEC-063
disabled-pass reclaim continuation is unwired, and STOPPED/empty routes publish no
protected entry. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 deferred undo collection, job-owned prepared entry
(resumable-midi-boundary-playback-maintenance):** a new
`PendingStopPhase::UndoCollection` on the prepared PLAYING route collects the
session's undo identities in bounded grains and builds the `OverdubPassAdded` entry
from the job's own slot and baseline via `TrackUndo::prepareOverdubSessionEntry`.
The collection has its own admission grain class
(`remainingUndoCollectionGrains`/`undoCollectionQuantum`, sized from
`Loop::requiredSessionStopUndoCollectionGrains`, included in
`finalizationComplete()`), so saturation denies admission instead of stalling. The
STOPPED/empty routes skip companion preparation and never enter the phase. Private
outputs are pre-reserved in `prewarmCaptureStopJobs`; `Loop::reclaimUnreferencedDisabledEditPasses`
moved to flash to hold 13 ITCM banks. New native
`test_deferred_stop_prepares_job_owned_undo_entry`; full native 2,188 cases — 2,183
passed, one skipped, the same three known reentry failures; capture-serial builds
with 6,752 bytes RAM1 headroom. Increment B remains: publish through
`TrackUndoPublication` + `commitWithUndo`/`commitStoppedWithUndo` and drain its
`ReleaseRedo → AwaitReclaim → Trim → Complete` phases in DeferredCleanup. Task 2.3
remains unchecked.

**2026-09-16 task 2.3 deferred-stop receipt audit — design decision required
(resumable-midi-boundary-playback-maintenance):** the last two receipt items need a
decision before implementation. The normal deferred overdub stop publishes **no**
`OverdubPassAdded` entry — `pushOverdubPassAdded` runs only from the fault-fallback
`commitCaptureForStop` via `finalizeCommitSideEffects`, while the deferred path calls
`TrackPreparedStopCommit::commit` with `undo = nullptr` — so the job-owned undo
baseline is correct preparation but currently unreachable in production; wiring the
protected undo publication (the prewarmed-but-unused resumable
`*CollectOverdubSessionUndoPasses` machinery plus `TrackUndoPublication`) is what
makes it load-bearing. The session-exit receipt cannot be job-owned because it is
retained past the job and consumed later by the idle publisher while playback reads
it for the stale-root overlay; it needs a Loop-owned multi-slot ring with the gate
testing a free slot. Ring size and multi-pending overlay behaviour are the open
questions. No code changed for these two items. See CONTINUOUS-CAPTURE-OWNERSHIP.md.

**2026-09-16 task 2.3 job-owned overdub-session undo baseline (resumable-midi-boundary-playback-maintenance):**
`CaptureStopJob` now owns the overdub-session undo baseline
(`ImmutableLoopSnapshotRef beforePublished` / `hasBeforePublished`). Admission moves
the live `Track::overdubSessionBeforePublished_` into the job and clears the live
member, so a successor session's `beginOverdubSession` records its own baseline
instead of overwriting the one this interval's `OverdubPassAdded` entry must record;
`TrackUndo::pushOverdubPassAdded` prefers the head job's baseline and an unconsumed
baseline is retired with the job. New native
`test_stop_job_owns_overdub_session_undo_baseline`; `test_continuous_capture` 29 pass
with the same three intentional reentry failures; full native 2,187 cases — 2,182
passed, one skipped, the same three; capture-serial builds with 6,752 bytes RAM1
headroom. Still open in this slice: the session-exit snapshot handoff receipt and the
prepared-companion history. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 job-owned capture preview (resumable-midi-boundary-playback-maintenance):**
`CaptureStopJob` now owns `CapturePreview preview`, transferred from the live session
at admission by constant work (`CapturePreview::swapWith`, per-vector swap, no
copy/allocation) with a job-identified read borrow, so the deferred cleanup seals
this interval's preview rather than a successor's. `Loop::capturePreviewView()` is
the single readable accessor and is out of line in flash (inlining it at 40+ display
call sites cost a RAM1 bank); 45 read-only display/Edit sites were migrated and
writers keep the member. `TrackPreparedStopCommit::completeDeferredCleanup` takes the
job's owned preview. New native `test_stop_job_owns_capture_preview` and
`test_capture_preview_borrow_release_is_owner_identified`; `test_continuous_capture`
28 pass with the same three intentional reentry failures; full native 2,186 cases —
2,181 passed, one skipped, the same three; capture-serial builds with 6,752 bytes
RAM1 headroom (13 ITCM banks). Still open in this slice: the overdub session/undo
before-snapshot, the session-exit snapshot handoff receipt, and the
prepared-companion history. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 job-owned pending note changes (resumable-midi-boundary-playback-maintenance):**
`CaptureStopJob` now owns `PendingNoteChangeVec pendingChanges`; admission swaps the
live Add/Shorten/Hide set into the job in constant work and leaves the live session a
prewarmed empty set, so the worker no longer resolves the canonical tail against the
live session's intents and a successor interval is independent. `Loop::pendingNoteChanges()`
returns the job's owned set while a job-identified borrow is bound (writers use the live
member), and the borrow is released with the job. New native
`test_stop_job_owns_pending_note_changes` and
`test_pending_change_borrow_release_is_owner_identified`; `test_continuous_capture` 26
pass with the same three intentional reentry failures; full native 2,184 cases — 2,179
passed, one skipped, the same three; capture-serial builds with 6,752 bytes RAM1
headroom. Still open in this slice: `capturePreview` ownership across the stop, the
overdub session/undo before-snapshot, the session-exit snapshot handoff receipt, and the
prepared-companion history. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 sealed raw-range ownership wired (resumable-midi-boundary-playback-maintenance):**
The approved ownership transfer is implemented. `CaptureStopJob` owns
`LoopEventStore::CaptureRange rawRange`; `beginDeferredOverdubStop` proves
`canSealCaptureRange` before any mutation, seals the producer extent at punch-out
and binds a job-identified read borrow on `Loop`. A denied admission performs no
seal and no transfer; an empty capture skips the seal. `Loop::captureReadSource()`
exposes producer and finalizing extents separately and ordering is scratch-only, so
the borrowed range is never mutated; closure NoteId resolves from the finalizing
extent first. `Track::retireJobCaptureSourceGrain` retires the owned extent in
grains, drops the borrow only while it still identifies that job, and re-arms the
descriptor's reserved destination. DisplayManager is unchanged; the
successor-capture guard stays deliberately in place. Five new native regressions
plus three migrated ownership assertions; `test_continuous_capture` 24 pass with the
same three intentional reentry failures; full native 2,182 cases — 2,177 passed,
one skipped, the same three; capture-serial builds with 6,752 bytes RAM1 headroom
(new functions are FLASHMEM and `Loop::beginCapture`/`discardCapture` moved to flash
to hold 13 ITCM banks). Device boot prewarm admission **PASS** on this build
(`41b30d18…`, `captures/task23-sealed-range-prewarm/result.json`): PSRAM free
5,142,912 -> 3,856,320 B (160,824 B/slot, unchanged), internal heap 352,256 ->
311,296 B, pool chunks 512 -> 416 (+4/slot = the four range destinations per track),
all slots `ready=1` with 8 credits and zero warnings. Task 2.3 remains
unchecked; per-job session/preview/undo/snapshot receipts, causal predecessor/source
identity and unguarding successor capture remain open.

**2026-09-16 task 2.3 FIFO/queue-head binding (resumable-midi-boundary-playback-maintenance):**
The stop-descriptor ring is now the explicit admission authority —
`Track::captureStopJobTail()` admits (null when full, `queue_full` stop stage),
`captureStopJobHead()` is the worker's stable per-grain descriptor, and
`!STOP_STATUS` reports `queue=depth/capacity`. Behaviour is unchanged at one job
(tail == head). Two new native regressions; full native 2,178 cases — 2,173
passed, one skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom.

**Sealed raw-range wiring is audited but not wired:** during the deferred stop
`Loop::capture.store` is still read (and sorted in place) by the Loop
display-window merge helpers and scanned by `appendCaptureNoteOffAtPhase` for
closure note identity, and `Loop::beginCapture` frees a pre-reserved tail. Sealing
at punch-out therefore needs a Loop-internal producer-or-sealed read fallback; the
recommended design and the rejected lazy-seal alternative are recorded in
CONTINUOUS-CAPTURE-OWNERSHIP.md. It moves capture-interval ownership between `Loop`
and the admitted Track job, so the architecture ownership-transfer protocol
applies — approval required before implementation. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 ingress/staging isolation (resumable-midi-boundary-playback-maintenance):**
`RuntimePlaybackViewSet::captureStaging` is split into two independently prewarmed
roles in the same owner: a *live* ingress staging and a *frozen* stop-job staging.
Live ingress writes only the live role; the one admitted stop job borrows the frozen
role. Punch-out freezes the live role and exchanges the two indexes in constant work
(`LoopPlaybackRuntime::exchangeCaptureStagingRoles`); job cleanup resets only its own
retired frozen role, so old-job cleanup cannot clear or alias a successor interval's
ingress rows. No new owner/module, no canonical/persistence/SD change, no action-edge
copy. New native regressions in `test_playback_prewarm` and `test_continuous_capture`
(production Button A route) pass; full native 2,176 cases — 2,171 passed, one skipped,
the same three intentional public-reentry failures; `teensy41-capture-serial` builds
with 6,752 bytes RAM1 local headroom, RAM2 free 472,928. Added cost is one
2,048-event staging buffer per fully-prewarmed runtime slot (now counted by
`fixedExternalBytes`); boot prewarm admission was measured on device and passes —
build `fb4ab3a9…` reports PSRAM free 5,143,968 → 3,859,680 B across the 8 boot
slots (160,536 B/slot, two stagings per slot, no internal-heap fallback), all
slots `ready=1` with 8 credits, zero warnings, and 46.1 % of the 8.38 MB pool still
free (`captures/task23-staging-prewarm/result.json`; the run used a capture-only
SESSION_CAPTURE boot line). The selected-slot full-prewarm plus dense loaded
content still needs its own footprint check before the guards come off. FIFO/queue-head
binding, sealed raw-range wiring, causal predecessor/source, per-job
session/preview/undo/snapshot receipts and the three reentry cases stay open; task
2.3 remains unchecked. DEC-064 follow-up recorded.

**2026-09-16 dense snapshot representability + fragment transaction (openspec 2.3 / immutable-loop-snapshot-passes 3.7):**
The dense record/overdub pair (`task23-dense-snapshot-cc-playback`, 734 effective
rows) was lossy in publication: bar 2 needs 260 event references, above the fixed
16-page (256-reference) per-bar membership ceiling. Construction failed at
`appendCurrentReference`, and because pass-change progress had already advanced the
fragment was discarded, so a 710-row truncated candidate was published. Two bugs:
representability (valid content exceeds the ceiling) and atomicity (failure consumed
progress despite DEC-047). Fix: explicit `kSnapshotMembershipMaxRefsPerBar = 4096`
design envelope with derived 256-page limit applied to event and note membership,
`uint16_t` page counters, and transactional fragment incorporation (authoritative
progress advances only after successful incorporation; partial candidate discarded on
failure; representability failure parks publication after two consecutive failures
with `DIAG,snapshot_fragment`). DEC-047 amended + ExecutionBounds rows 21/22 updated
2026-09-16. Focused `test_loop_take_survival` 132/132; full native 2166 passed / one
skipped / three known task-2.3 reentry failures; `teensy41-capture-serial` SUCCESS
with RAM1 locals 6752. **Hardware verification PASS for representability:** uploaded
build `5bb90b47bdae299d630be640…`, dense recipe stored 766 effective rows with bar 2
at 270 refs (>256) and four restart cycles played 765 rows with 0 duplicates; the
only absent row is the pre-existing terminal loop-boundary Off (`NoteId=0`). Ordering
parity and that terminal-Off ledger behavior remain separate open items.

**2026-09-16 latest checkpoint (14:29):** descriptor build a9a7217f uploaded on
retry; dense hardware test then lost USB during initial recording, before overdub.
No reboot marker or fault cause retained; serial port absent. Native full run has
2,159 pass / one skipped / three known reentry failures; final focused guard run
has 14 pass / the same three failures. Stored dense content verification on the
prior fixture passed its scoped input comparison; audible timing and restored
playback remain open. Device reconnection is required to continue hardware
investigation. No capture reader is running. See active FOCUSED-STOP-HITL.md.

**2026-09-16 dense verification / FIFO descriptor slice:** stored dense overdub
content matches input (568 events, three chunks, five expected short-pair removals).
Timing is NOT passed: a stop ordering grain crossed a service deadline and two
late clock samples are retained. Restored musical playback produced no target
channel rows in the probes; output policy/readiness remains under investigation.
Stable external Track descriptors replace the singleton stop receipt; sequential
rotation passes, but staging/session/predecessor isolation and three reentry tests
remain open. Full native: 2,159 pass, one skipped, three expected reentry failures;
final focused admission/rotation: 14 pass and the same three failures. Build passes
with 6,752 bytes RAM1 headroom. See active FOCUSED-STOP-HITL.md for evidence and
latest upload status; no task checkbox closes.

**2026-09-16 dense reboot fix verified:** matching fault report recovered into
crash-log-retrieval.log: stack-boundary DACCVIOL in _svfprintf_r. Overlap diagnostic
scratch moved to RAM2; local frame reduced 1,372 -> 76 bytes. Two dense automated
runs now complete without reboot; adjusted Stop-window run passes focused silence
(18.9 s observation). Broad baseline/content/timing checks remain inconclusive;
no FIFO/reentry/task-2.3 completion. Firmware hash and details are in active
FOCUSED-STOP-HITL.md. All serial/MIDI runners are closed.

**2026-09-16 automated dense rerun:** same track-5/slot-1 recipe reproduced the
reboot; corrected boot marker is crash=1. Serial missed the early fault text;
SD crashlog has not been read. Runner/capture finished. The dense gate remains
open; no manual reproduction is required. See task23-dense-crash-report-rerun
and active FOCUSED-STOP-HITL.md.

**2026-09-16 overlap reboot follow-up:** user confirms spontaneous reboot during
record-pass/overdub overlap. Native Shorten/stop regression passes; root cause
remains unresolved. Fixed destructive double-read of Teensy CrashReport: startup
now preserves the crash flag and renders once to Serial and SD. Build/upload pass,
with unchanged 5,600-byte RAM1 headroom. Human overlap reproduction on track 5 /
slot 1 is the next checkpoint; serial capture is in task23-overlap-crash-report.
See active FOCUSED-STOP-HITL.md for firmware hash and evidence limits.


**2026-09-16 device checkpoint (track 5 / slot 1, authorized):** verified firmware
uploaded. Two normal stop cleanups, focused transport-Stop silence, and three
restart content cycles pass. The denser run disconnected/rebooted before its stop
trace drained and is INCONCLUSIVE_CAPTURE; reset cause is unknown. No dense or
full task-5.3 acceptance. Updated host verifier recognizes pending-job completion
after visible PLAYING and passes 24 regression tests. Original reports and a
separate reevaluation are preserved under captures/task23-reset-*. See the active
FOCUSED-STOP-HITL.md checkpoint for exact build identity and limits. FIFO/reentry
implementation remains unfinished; no task checkbox closes.


**2026-09-16 pending-job reset protection / intermediate HITL checkpoint:**
An admitted stop job now retains its runtime staging and canonical-copy output
across cache reset. Reset retargets STOPPED and yields; Track invalidation preserves
the borrowed generation until terminal recovery. Native coverage exercises partial
copy and every stop phase, including held-note closure and receipt release.
A Tier-A prewarm record exposes output credits and pool/heap headroom. Verify
boot/load memory and single-job copy/Stop timing on hardware before expanding the
FIFO reservations: prior eager prewarm exhausted device PSRAM. This checkpoint is
not task-2.3 acceptance; the same three reentry fixtures remain red, and FIFO,
causal-source and preview/session/undo/snapshot migration remain unfinished.
No upload has been performed. Exact results and build identity are in the active
change's CONTINUOUS-CAPTURE-OWNERSHIP.md; the procedure is in FOCUSED-STOP-HITL.md.

**2026-09-16 task 2.3 worker-owner integration:** The existing stop worker now
uses a stopped-interval receipt for Loop/slot/phase/geometry and its own pending
notes. Closures target the saved owner; one-node retirement never clears live
notes, and completion cannot transition a newer Track interval. Production
empty, paired and held-note stops complete in native tests. Public A/B/C reentry
still fails: raw capture, staging, preview and undo/snapshot session migration
and the fixed FIFO remain open. Affected tests: 266 pass, the three known reentry
cases fail. Capture-serial builds with 5,600 bytes RAM1 local headroom; no upload.
Task 2.3 is not complete. See the
[ownership checkpoint](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md).



**2026-09-16 task 2.3: public-route reproducer and raw range foundation:**
The actual Button A → TrackManager → Track route and MidiHandler musical input
now run in a native fixture. First-interval capture/stop passes; A/B/C reentry,
manager same-pitch held-note reentry and direct Track reentry remain intentional
failing requirements. None of the three admission guards has been removed.

LoopEventStore now supports pre-reserved exclusive capture-range rotation,
read receipts, incremental release and stale rewrite/pool-lifetime rejection.
The focused range/persistence/event-store/playback-prewarm run passes 209 tests.
The full native run had 2,136 passes, one skip and only the three expected reentry
failures (before the final persistence-prewarm correction). This API
is not yet wired to Track; it does not fix immediate reentry. Job-owned staging,
closure credits, pending changes, undo/snapshot receipts, causal FIFO resolution,
cleanup isolation and device verification remain open. See the
[ownership checkpoint](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md).
Capture-serial build passes with 6,176 bytes RAM1 local-variable space after
placing the bounded rewrite maintenance worker in flash. Device timing remains
unverified. Task 2.3 stays unchecked; no firmware upload or archive was performed.

**2026-09-16 continuous preparation across punch-in/out:**
Canonical preparation now runs during PLAYING as well as OVERDUBBING, using the
existing VisualCache owner and otherwise-unused wrap-append turns. Existing
record, stop, wrap-append and source-span priority remains. A confirmed newest
pass append with unchanged companion history preserves completed normalized
passes and resumes the first unfinished/new pass; snapshot identity publication
at the same content revision also preserves completed normalization. Unproven
source changes or changed history retain the existing safe invalidation path.

The companion merge now retains final per-pass output, in its original order,
in the canonical owner's existing raw scratch buffer. One extra prewarmed pass
descriptor array is required (16 KiB at 2,048 entries on Teensy); no additional
event buffer or active allocation is introduced. Each existing merge addition
also writes its normalized scratch entry, within the same Q-entry grain.
Successful stop cleanup adopts these results with pointer swaps after matching
source/worker generations, including when the committed companions changed
history. Reselecting current descriptors/history is resumable; completed pass
normalization is not replayed. Selected bridge/base and audible cursor remain
untouched. A scratch-owner receipt prevents another builder from being mistaken
for the retained result. `prefix_reuse` / `prefix_restart` records describe the
low-frequency ownership transitions rather than logging every prework grain.

Task 2.3 and HITL acceptance stay open. This does not claim instant reentry:
frozen-tail/bridge/finalization work remains, and new history during a wrap may
invalidate normalization for all potentially affected passes. The provisional
6,240-grain baseline remains unchanged; added bounded stores need device timing
validation alongside rapid punch-in/out, wraps and original-Off preservation.



Continuous-preparation validation: full native suite 2,130 passed / one skipped;
final focused suite 194 passed, including superseded scratch-owner rejection.
Post-commit adopted per-pass events match fresh canonical replay across the
existing stop variants, including companion edits and the large-capture fixture.
Strict OpenSpec and diff checks pass. Capture-serial build succeeds with RAM1
local headroom 6,176 bytes, unchanged. Upload and one retry failed: the Teensy
loader reported no boards on USB and requested the physical program button;
no `cu.usbmodem` port was present. This firmware is built but NOT uploaded or
HITL-validated. Reconnect/program-button action is needed before retrying.

**2026-09-16 live canonical-base prework and stop estimate correction:**
The `003056` manual capture exposed a small-tail estimate incorrectly used as
candidate capacity. Stop now estimates from the prewarmed capture owner and the
prepared canonical base (or its conservative full capacity when unavailable).
The pass estimate includes record, existing overdubs and the incoming capture.
It no longer infers capacity from a rolling emission window or final tail count.

Live overdub and Stop share `stepCanonicalBase`: prefix normalization followed
by the existing pass merge and view fill/order worker, one child per owner turn.
The live VisualCache turn can prepare the immutable candidate bridge base while
recording the overdub. Paint dirtiness alone does not block this work while
visual reconstruction is disabled; wrap append and source-span work retain
priority. Source/storage changes invalidate prework; current playback and the
selected bridge remain untouched. Stop resumes partial work or reuses a ready
base. Frozen-tail, companion and cleanup work still remains after Stop, so this
is not proof of immediate reentry or a realtime deadline pass. Task 2.3 and the
HITL gates remain open; the provisional 6,240-grain baseline is unchanged.



Validation: full native suite 2,126 passed / one skipped; final focused suite
189 passed after the prefix-owner validation adjustment. Strict OpenSpec and
`git diff --check` pass. Capture-serial build and upload succeeded; serial
`/dev/cu.usbmodem154944801` returned. RAM1 local headroom remains 6,176 bytes.
The device upload finished at 00:44:27 local time on 2026-09-16. Manual reentry
and deadline acceptance remain open.

**2026-09-16 bridge boundary reconciliation implementation:**
`refreshFrozenCompanionBridge` now invokes the shared compiler's
`rebindBoundary` rather than resetting bridge storage. Records, mapping, base
ranks, sorted base order and complete insertion order survive. The existing
merge scratch retains the complete insertion permutation after sorting; steady
compaction no longer destroys the order needed by another boundary binding.
Reconciliation runs only inheritance, affected-record checks, closure resolution
and cycle-index output in the existing entry quantum. No allocation, copy of
canonical events, or sort is introduced during this path. Original carried Offs
are enabled only by a matching live closure and disappear after it closes.

Admission checks the relevant subset of the ledger using fixed pitch/identity
summaries built from all changed records and inherited closures. Identity hash
collisions conservatively request reconciliation; they cannot bypass protection.
Changes to unrelated notes no longer invalidate preparation. Changes to affected
notes, cycle or original-Off deadline retain the closure-only reconciliation
path; no note protection or command-epoch check is removed. These smaller steps
still require device service/reservation validation; no unconditional deadline
or maximum stop-latency claim is made.

Native coverage compares repeated bindings against fresh compilation over six
cycles, changes unrelated notes every preparation turn, verifies unchanged map
and bridge storage generations, and preserves an inherited original Off during
a second source change. Capture refresh telemetry now says `closure_only`.
The provisional 6,240-grain baseline remains unchanged. Full native validation
reports 2,124 passed and one skipped; final focused validation passes 153 tests.
Firmware build and upload retry succeeded with 6,176 bytes RAM1 local headroom;
serial returned. Task 2.3 and hardware acceptance remain open.


**2026-09-16 post-finalization reentry starvation:**
`captures/session_20260916_000034.log` shows the populated bridge ready at
30.256907 s and phase-10 cleanup completed at 30.354501 s. Button A reaches
Live Overdub at 31.709, 35.371 and 37.227 s, then the combined transfer/snapshot
admission guard rejects it. Code inspection and a failing native reproducer
identify `serviceBridgeWindow` discarding a completed but unpublished future
window. It immediately rebuilds that window on subsequent turns, starving the
existing snapshot receipt consumer and leaving its admission slot occupied.
The Complete branch now releases the builder with `discard(&target)`, preserving
the ready/build destination. No snapshot admission policy is relaxed. The native
regression fails before this change and passes after, including repeated idle
yields, immutable future windows and the existing multi-cycle MIDI continuity
fixture (152 focused tests pass). Admission warnings now distinguish transfer
capacity from an occupied snapshot receipt. Full native validation: 2,123 passed, one skipped. Build/upload succeeded
with 6,176 bytes RAM1 local headroom; serial returned. Device reentry remains
unverified; task 2.3 and HITL/deadline gates remain open.


**2026-09-15 manual clean-stop routing regression:**
`captures/session_20260915_235602.log` records wrap commit at 48.314 s, Stop
Overdub at 48.428 s, and `bridge_begin` denial at device 48,438,934 us
(mask 1652 / 0x0674). Later Button A presses reach the finalization-pending
handler. The empty post-wrap candidate bypasses canonical/bridge preparation,
but the AtomicTransfer refresh guard incorrectly required a bridge for every
PLAYING destination and routed it back to CompanionBridge. The guard now
requires an actual pending capture pass. Empty stops proceed to existing
cleanup; populated PLAYING commits retain mandatory bridge preflight and
transport Stop retains precedence. Shared native routing coverage exercises
all three cases. The capture has ring overflow, so it cannot establish deadline
acceptance. Full native suite: 2,123 passed, one skipped. Firmware build/upload pass with
6,176 bytes RAM1 local headroom; serial port re-enumerated. Manual device
confirmation and task 2.3 remain open.


**Current DRY direction:** first recording reuses the existing playback view,
fill/order, coverage lifecycle, bridges and MIDI cursor. The separate recording
window/store/stream prototype was removed. Shared coverage-scoped fill is tested
(122 focused tests); source/lifecycle integration and original-boundary readiness
remain open. Earlier prototype test counts are historical, not acceptance of the
shared recording path. No delayed launch or new recording rejection is authorized.

**2026-09-15 first-record shared-window implementation:** bar 1 is installed as
raw playback coverage before RECORDING, then refreshed from the canonical
chunked capture store by the existing resumable fill/order owner. Source-count
and capture-display generation checks discard stale unpublished work; the
retained view survives a cache reset and binds final length at record stop.
Native focused coverage/prewarm tests pass 132/132 and both firmware builds
pass. Deferred main-loop maintenance is routed to FLASH, and the capture-serial
instrumentation target now passes its RAM1 local gate with 6,496 bytes of local
headroom. No upload or HITL verdict is claimed yet.

The recovered capture image has since been uploaded. Track 5 / slot 1 transport
Stop interruption passed focused precedence and silence verification; the clean
normal-stop rerun was inconclusive because the overdub-stop transition was not
confirmed. Broader HITL and reservation gates remain open.


**2026-09-15 first-record direction corrected by user:** no delayed launch is
acceptable. Prepare rolling one-bar playback windows during recording, with
bar 1 established at record start and retained for the existing final-bar return
boundary. Extend the currently overdub-only staged input path; prove late input,
held-note closure, final-length binding, and subsequent window handoffs without
rewinding transport or replaying historical events. The verified first-record
snapshot delay/catch-up defect remains unfixed. The deferred-launch proposal is
rejected history. See the active `resumable-midi-boundary-playback-maintenance`
design and specs; task and HITL gates remain open.


**2026-09-15 device-clock stop timeline ready:** capture-only telemetry now
retains every stopping grain's frozen identity, requested destination, actual
preceding MIDI service/due/deadline and span, followed by queued service and
NoteOn/Off emission records. Loss is explicit and invalidates the host timing
report. Focused native telemetry/tier: 43 passed; host timing/stop tests: 40
passed. Capture build remains within the RAM1 local gate (6,496 bytes). The
next device checkpoint is normal stop plus timed transport Stop after the
deferred-save setup precondition is confirmed. Task 4.1 remains open.

**2026-09-15 forced wrap Stop PASS:** the approved track 5/slot 1 four-bar
fixture reached `OVERDUB_STOPPING`; MIDI Stop followed 8.689 ms later. A
volatile per-track output barrier now blocks loop output before the Stop
handler's all-track CC123 fan-out and at the final Track emission call. The
rerun sent All Notes Off after 83 us, reached STOPPED after 173.607 ms, and
observed no target NoteOn for 18.9 s. Evidence:
`captures/resumable-handoff-wrap-forced4-barrier-hitl/`. This is focused
single-track silence evidence only; Task 2.3, layered playback and deadline
acceptance remain open. Native focused suites: 242 passed; host tests: 38
passed; capture firmware RAM1 locals: 6,496 bytes.

**2026-09-14 restart omission repaired and HITL PASS:** the prepared-stop path
now hands prewarmed session identities to the idle snapshot owner before close;
the stale-root overlay reads that receipt immediately. Companion publication
updates both note and MIDI-event membership. Three complete restart cycles each
played all 49 expected notes exactly once with matching NoteOffs, after a focused
Stop-interruption PASS. Evidence: `captures/resumable-handoff-admission-hitl/`
and `captures/resumable-handoff-restart-hitl/`. Firmware uploaded; transport stopped.
Native full suite 2,083 passed/1 skipped, final focused suite 124 passed, host tests
38 passed; RAM1 locals 6,496 bytes. Tasks remain 11/19. An earlier wrap-adjacent
run took legacy stop and is inconclusive; reproduce that admission fallback next
with the new capture-only denial diagnostics. Protected cleanup/undo, fully
bounded publisher/overlay work, layered parity and timing acceptance remain open.
See `FOCUSED-STOP-HITL.md` for scope and separate baseline verdicts.

**2026-09-14 restart survival FAIL:** after the focused interruption PASS, three
complete restart cycles emitted only base pitches 48–79; overdub pitches 24–39
and held 83 were absent. Canonical/display evidence still reports 49 notes while
playback's overlay has 32. Prioritize task 2.3's reserved bounded session-exit
snapshot handoff before clearing session identity; the prepared-stop production
path currently omits it. Do not insert the unbounded legacy queue call into an
active grain. Layered HITL is deferred until single-slot survival passes.
Evidence: `captures/resumable-stop-restart-survival/restart-result.json` and
`FOCUSED-STOP-HITL.md`. Transport stopped; firmware unchanged; tasks 11/19.

**2026-09-14 focused interruption PASS:** repaired concrete-output-port fixture
and stale-PLAYING skip; actual MIDI Stop observed 9.253 ms into OVERDUB_STOPPING,
All Notes Off after 70 us, then STOPPED and 9.49 s without resumed target notes.
Evidence: `captures/resumable-stop-hitl-strict-wrapper/interruption-result.json`.
This closes only the single-track interruption/silence checkpoint. Restart
survival, layered playback, broader baseline and full timing acceptance remain
open. Tasks remain 11/19; see `FOCUSED-STOP-HITL.md` for scope and next gates.

**2026-09-14 MIDI deadline correlation checkpoint:** uploaded capture-only retained
send/due/deadline timestamps and per-stop-phase summaries. Native 2,080 passed,
1 skipped. A normal stopping interval measured 97.760 ms; one canonical-order
turn crossed its entry service deadline (248 us duration, 33 us slack). Another
run retained 24.678 ms clock and 26.049 ms NoteOff lateness. This is partial
hardware evidence, not deadline acceptance. Scratch clears succeeded with the
revised host precondition; the timed interruption fixture was subsequently repaired and its focused
checkpoint passed (see the newer entry above). See `FOCUSED-STOP-HITL.md`; tasks remain 11/19 complete.

**2026-09-14 protected stop continuation:** bounded undo publication/restore,
STOPPED-only joint recovery, disabled-pass reclamation and session-close helpers
now have native coverage. Production playback honors the latched STOPPED
emission policy; full joint stop integration remains open (11/19 tasks).
See the change's `BRIDGE-PROGRESS.md` and `FOCUSED-STOP-HITL.md` for the focused
Stop regression checkpoint and explicit remaining integration. Upload and a focused HITL attempt were approved and performed on 2026-09-14.
The initial boot exhaustion is fixed: boot now reserves staging only; full
bridge readiness remains separately guarded. Device startup and normal UI are
confirmed, with 4,401 KB free PSRAM. Native: 2,077 passed, 1 skipped. The exact
managed-log binding is fixed. Normal stopping was observed; the interruption
retest remains INCONCLUSIVE_CAPTURE after a deferred-save clear refusal. See
`FOCUSED-STOP-HITL.md` for evidence and remaining gates.

**Agents: load first** with [CURRENT_WORK.md](CURRENT_WORK.md). Overwrite frequently — **operational only**, no future milestones (those live in [ROADMAP.md](ROADMAP.md)).

Last updated: 2026-09-12 (OpenSpec 6.4 SHIPPED; next 6.5 archive)

---

**2026-09-13 active OpenSpec continuation:**
`resumable-midi-boundary-playback-maintenance` is **11/19 tasks complete**.
DEC-060 records user approval of canonical per-pass companion application for
combined capture preparation. Preserve immutable pass membership and apply rows
independently before the canonical merge; flattened admission is not the oracle.
The bounded per-pass owner now applies and merges immutable input descriptors
with pass/event origins and generation/capacity guards. Per-pass mapping now
validates an explicit published-base provenance table and feeds the ranked bridge
compiler/cursor. Full-loop view fill now constructs and owns the table alongside
events from a completed effective-pass baseline. A generation-checked sealed-pass
reader now copies raw chunks in bounded event/chunk visits without allocation
or partial-result exposure (2026-09-14; event-store tests 34/34). Production canonical pass
lifetime integration remains open. Bounded history preparation
now covers Create/Delete/Length/Pitch/Velocity/NoteRange, including contiguous row
collapse and Create identity reactivation. DEC-061 records the approved stable
Create sort correction. A runtime-owned continuation now connects frozen
sealed-pass descriptors through history application to separate effective-pass
inputs, with a generation-bound companion consumer. A direct LoopPasses overload
now selects canonical passes and filters active history in bounded grains;
a frozen-source entry point now checks live revision/generation/epoch, loop
length and capture state even on completed-result access. The typed companion
consumer now also binds the prepared row owner, rejecting same-generation row
reuse and mismatched capture owners. A frozen-overdub entry now orders capture
with private indexes and normalizes it as the final pass without changing the
shared audible arrays. Companion continuation now preserves historical Delete/
Create membership across the stage boundary through a bounded prefix scan.
End-to-end native bridge traces now cover 24 canonical history/capture variants,
a four-cycle open-note capture trace, and epoch cancellation/rebuild in mapping
and compilation. Ranked mapping/compilation now also feeds runtime admission
with live owner/generation checks retained through final selection preflight.
Executable uint64 bounds now compose ranked preparation, mapping and compilation;
the conservative full-horizon result exceeds the existing stop budget, so
completed sealed prefixes can now be reused without selection/read/history replay.
A runtime-owned prefix continuation now binds live revision/generation/length,
latches denial until the source changes, separates cleanup from restart, and
yields ownership to a frozen tail. The VisualCache owner now schedules at most
one prefix child across all tracks, with Q=32 and a global early return. Stop,
recording and existing active visual work retain priority; eligible overdub
tracks rotate without allocating missing runtime storage. A runtime coordinator now extends the owned prefix
through frozen-tail normalization, row preparation and canonical per-pass
companions in separate grains. It binds all child generations and retains
live source/epoch checks through ranked bridge preflight. Production stop routing,
atomic admission, admitted-tail/service proof and HITL remain open. Ranked-tail
bounds now accept an explicit maximum pass count and published-base event count;
C=2048/P=2/B=2048 with no history/companions still needs 7648 additional grains
at Q=32. Invalid quantum, out-of-staging event counts and overflowing legacy
contract totals now deny before stop admission. The order builder now checks
retained adjacency during fill and bypasses merge/copy only when the exact
comparator proves the input already ordered (64 grains for 2048 rows at Q=32).
Unordered inputs retain the merge path; worst-case bounds and production stop
limits are unchanged. Frozen-tail normalization now imports completed canonical
capture indexes into its private array in Q-bounded steps. Shared-order reuse
invalidates an in-flight import; after copying, audible ordering is independent.
Canonical and audible order bindings now also record that storage generation,
so discarding a later audible order cannot revive an older canonical completion.
Stale bindings deny resume/rewrite/readiness; tail admission uses private sorting
until a current canonical order is explicitly rebuilt.
When canonical order is already complete/current, capture normalization has a
196-grain bound at C=2048,H=0,Q=32 instead of 1604. The initial canonical sort is
still charged. The executable prepared-order aggregate now gives 6240 additional
grains for C=2048/P=2/B=2048,H=R=0,Q=32. The general 7648-grain aggregate and
legacy order/rewrite credits are unchanged. DEC-062 now approves a separate
provisional 6240-grain ranked-preparation allowance for continued integration.
Determine realtime reservations and elapsed stop latency with HITL once the
complete path can run; the old 3072 is not a product latency ceiling.
`PendingStopAdmission` now accepts an optional uint64 ranked-work bound at begin,
validates the 6240 allowance and combined counter capacity before mutation,
and tracks ranked credits separately. Finalization requires all three counters
empty; transport Stop preserves credits and changes epoch/destination. Existing
production callers still request zero ranked credits pending typed tail/bridge
routing; exhausting credits does not replace prepared-source readiness checks.
Reserved tail/bridge step entry points now bind the pending stop's frozen
generation, epoch and admitted quantum, charge one credit before one child,
and deny exhausted/stale reservations without advancing it. Completed readiness
queries consume no credit. Native end-to-end coverage uses these entries;
production stage dispatch, setup charging and atomic publication remain next.
A STOPPED-only typed oracle publisher now owns canonical baseline events and
pass origins together, upgrading flat same-identity views before bridge mapping.
It preserves the existing view on invalid baseline/coverage preflight and needs
no allocation. Track's all-stopped merged-event preparation now invokes canonical
baseline construction using idle prewarmed workers. It publishes typed origins
only when the complete canonical event sequence matches the existing oracle;
otherwise it retains flat publication. Temporary workers are released on success
or denial, without cancelling an already-owned prefix. This is synchronous
STOPPED throughput, never an active grain. Production ranked-stop stage dispatch
and atomic canonical/audible handoff remain open. A joint audible preflight now
requires AtomicTransfer phase, exhausted stop counters, matching command epoch,
PLAYING destination, explicit base/target identities and ready bridge/ledger/
cursor state. It is read-only and does not establish canonical companion-row
publication. Final bridge selection also rejects phase >= loop length instead
of accepting it through modulo coverage. Canonical companion staging is still
required before enabling the combined production commit. Loop-owned companion
history staging now prewarms a separate EditPass vector while STOPPED and copies
history metadata, Create payload events and prepared companion rows in Q-bounded
units. Prospective IDs do not advance nextPassId; canonical rows/revisions stay
unchanged. Source/storage/row-owner/ID changes hide the candidate. Logical discard
retains storage. The history handoff now swaps prebuilt canonical rows and the
snapshot companion-ID list, advances IDs/revision, and retains an immutable ID
receipt for the existing undo owner. Snapshot clear/replacement and append guards
reject stale candidates. Old history stays allocated until STOPPED prewarm.
A joint canonical helper now preflights the reserved capture pass and history,
transfers capture, then publishes history without a stale revision recheck.
It retires capture using scalar writes and preserves the one-revision-per-seal
convention. Track's prepared-stop entry now preflights exact canonical/companion owners,
source identities, stop epoch/credits/destination, cursor and ledger, then commits
canonical data and publishes the bridge without a second fallible selection.
Native fixtures verify matching identities and retained selected-bridge storage.
Loop's prepared-history continuation now verifies every staged sealed capture
event against the raw frozen canonical order in bounded event/chunk visits.
The joint handoff requires this proof; same-count payload/order mismatches and
missing/extra events deny publication. The rewrite cursor also binds its original
store. Stop admission now reserves canonical preparation separately from the 6240
ranked grains, rejects combined counter overflow before mutation, and requires
both counters exhausted for commit. An admitted step checks Q, frozen epoch and
owners before charging exactly one canonical grain; completed queries are free.
STOPPED prewarm now reserves both swap sides. After receipt consumption, a
bounded recycler retains allocations and restores preparation within the remaining
row/snapshot horizon. Two consecutive commits reuse the same buffers; exhausted
capacity or intervening canonical/mailbox changes deny reuse. Stop admission now
reserves post-commit history recycling separately, checks the combined credit
limit, and permits consumption only in DeferredCleanup after preparation finishes.
The joint commit requires and preserves that reservation; transport Stop also
preserves it. The native joint fixture charges setup and recycling while retaining
canonical revision and selected-bridge data. Track's recycling entries now bind
that committed Loop and preparation generation, charge receipt-consumed setup
separately, and enforce the admitted Q for each later grain. Wrong owner or
replacement preparation cannot consume the old reservation. Completed queries
remain free only while canonical/mailbox identity stays current. Loop now offers
bounded session undo-ID collection into caller-reserved private lists, preserving
the legacy cursor prefix and duplicate/order semantics. Session mutations invalidate
paused and completed collection. The maximum 2048-row/2048-companion collection
fits 193 grains at Q=32, separate from ranked preparation and recycling. The
collector now also appends the final capture and prepared companion receipt in
bounded stages, preserving the legacy final-pass inclusion and duplicate rules.
Receipt generation/lifetime and canonical identity guard paused and completed
collection. TrackUndo now prepares a private UndoEntry by setting metadata,
retaining the supplied before-snapshot and swapping the bounded ID outputs;
single-pass entries retain the legacy scalar representation. Preparation consumes
the collection without releasing vector buffers or existing snapshot roots.
Global publication and redo/trim retirement still need bounded integration.
DEC-063's accepted lifetime helpers now drain immutable snapshot graphs, persisted
snapshot/chunk payloads and complete UndoEntry resources in bounded units. A
private-stack trim continuation releases each entry before rechecking physical
pressure and shifts surviving entries one per unit. Shared snapshot readers remain
unchanged in per-grain native checks. Focused suites pass 37/37 and 43/43; full native passes 2069 with 1 skipped.
Firmware passes with RAM1 code 425736; strict OpenSpec and whitespace checks pass. These helpers require exclusive unpublished ownership
beyond a no-fail point; production locking, attachments, commands, reservations and
joint dispatch remain open. See the current BRIDGE-PROGRESS lifetime section.
A native pressure-feedback reproducer now proves that retaining removed roots
can change the surviving undo prefix (10 versus 8 entries in the controlled case).
The [undo publication reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/UNDO-PUBLICATION-REASSESSMENT.md)
records the accepted DEC-063 protected transaction with bounded reclamation.
The latest deferred Undo/Redo request wins per affected Track and executes once
after finalization under the destination policy; Transport Stop stays immediate.
Feedback trimming remains unchanged. Implementation and native proof precede
runnable HITL. Focused pool-budget validation passes **18/18**; this review changed no
firmware behavior. Production dispatch,
receipt/undo sequencing and active snapshot-box refill remain open. This canonical staging work is separate from the
6240 ranked-preparation allowance. A required live storage-generation guard now rejects timeline
reset/revision reuse, disabled-history reclamation and deferred edit appends
that can relocate rows without a playback revision change. The marker advances
before reset releases chunks, reclamation moves rows, or append reallocates
history, and remains bound across frozen-tail handoff. Native lifetime and
scheduler policy coverage do not establish an on-device latency bound.
Empty-history normalization and empty-companion merging now bypass
the apply worker and its extra copies, with corresponding tighter bounds.
Per-pass bridge mapping now uses a bounded sorted origin index and binary
search instead of quadratic scans; its maximum Q=32 bound is 2564 grains.
Production atomic transfer/routing, remaining combined-history/companion parity, duplicate
mergeSequence parity, production lifetime enforcement, capture freezing,
and admission remain open. See the
[Create reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CREATE-HISTORY-REASSESSMENT.md).
See
[scope reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/COMPANION-SCOPE-REASSESSMENT.md).
Task 3.5 STOPPING MIDI-poll/save policy is verified. Bounded bridge compilation
and the production selected-bridge consumer retain cross-wrap open-note
closures in native tests; capture-side creation and the canonical/audible bridge
handoff remain incomplete. Private UndoEntry preparation validation and capture-serial build pass
(RAM1 code 425736, locals 6496). Final full native run: **2061 passed, 1 skipped**
(`/tmp/resumable-undo-entry-native.log`), including combined receipt handoff,
single/multiple-pass representation, buffer/snapshot retention and denial guards;
bounded
Delete and wrap-head selection now match the actual resolver in 1,000
generated differential fixtures, including retention masks and inferred loop
length. Maximum-size completion, cancellation, and restart/capacity guards pass.
DEC-059's revised pairing reference remains the oracle. Length overlap traversal now preserves shortening-before-deletion order in
bounded quanta with held actions; 500 generated cases match the actual Length
resolver when using its existing reconstruction and mutation/order primitives.
Reconstruction wrap queries now match the legacy helpers in bounded two-pass
lookups. Complete bounded pitch reconstruction now matches canonical spans and
projected/deduped note output, using prewarmed storage with stale-source and
partial-output guards. Bounded Delete/Length application and frozen batches now match sequential
reference application while retaining base provenance. Bounded bridge mapping
now preserves final canonical ranks separately from that provenance. The ranked
compiler and cursor consume them with a bounded immutable-base index sort;
combined capture/production integration remains open. Aggregate stop-grain/
latency proof, bridge mapping/publication, and production transfer remain open;
host timing fixtures **22/22**;
capture-serial RAM1 locals **6720**. No upload/HITL. Details and next integration
work: [BRIDGE-PROGRESS.md](../../openspec/changes/resumable-midi-boundary-playback-maintenance/BRIDGE-PROGRESS.md).

## Current branch

**`feature/overdub-wrap-snapshot-publication`** — 3.5 wrap-journal publication
forked from `feature/immutable-loop-snapshot-passes`. Parent:
**`feature/immutable-loop-snapshot-passes`** — forked from
`546169f66658b87c54e6166b79eba74628aa3d1e` for the immutable loop snapshot
architecture proposal. The maintained-LCR continuation from
`feature/overdub-stop-lag-stage-c` is not pursued on this branch. See
[`CURRENT_WORK.md`](CURRENT_WORK.md). Record and overdub `LoopSnapshot`
publication queue at stop and step from idle (`DEC-045`) in the current
firmware. Task `3.5` now queues bounded wrap journals and steps them during
PLAYING/OVERDUBBING after the first root (`DEC-047`). Playback still reads a
stale published window plus unpublished session passes until consumer
migration. OVERDUBBING idle does not run visual reconstruct. NOTE_EDIT close
stays synchronous.
Device PASS [`233308`](../../captures/session_20260825_233308.log): no
`snapshot_idle` after first publish until STOPPED; PLAY4 BPM 119.966–120.049.
Capture-only `snapshot_idle` telemetry exposed the root/queue boundary, and
device capture [`232633`](../../captures/session_20260826_232633.log) verified
that the PLAYING/OVERDUBBING rows before root publication were one in-flight
record construction with no queued successor passes. The final `pending=0`
row published the root; no later PLAYING/OVERDUBBING idle rows occurred.
4b.2c is closed without amending `DEC-045`.
Tasks `3.5`/`3.6` and 4.3 are closed. Remaining-interval yield proof is
parked (`kSnapshotIdleReserveYieldEnabled` is false). OpenSpec 4.4a–4.4c
are closed. 4.5a is closed: overdub stop holds the composed frame and
elevates session-exit Snapshot idle until the successor root is current
([`145633`](../../captures/session_20260908_145633.log)). 4.4d is closed:
NOTE_EDIT dirty paint/select uses the current snapshot as committed base.
4.4e Stage 1 is in tree: `resolveDisplayNotes(*snapshot)` copies
note-span membership. Device
Device PASS [`162207`](../../captures/session_20260908_162207.log)
questions 1–2 (open visual **98**).
Q3 [`170211`](../../captures/session_20260908_170211.log) earned Stage 2.
Stage 2 device PASS [`171444`](../../captures/session_20260908_171444.log):
current-root opens share the span NoteId set. Stage 3 device PASS
[`173531`](../../captures/session_20260908_173531.log): commit rematerialize
uses `copyNoteEditCommitBase`. Close bake PASS
[`174646`](../../captures/session_20260908_174646.log): empty-id exit does
not invent `NoteEditPassClosed`.
4.4f leftover NOTE_EDIT oracles are closed: current-root open skips
`assignMissingNoteIds(loop.midiEvents())`; E: rows undo/redo fallback
copies the opened snapshot root (`tryCopyNoteEditOpenedSnapshotEvents`).
RAM1 locals **6848**; native **1772 succeeded, 1 skipped**.
Device [`191346`](../../captures/session_20260908_191346.log): both
`current=1` opens `only_*=0` (120 then 119).
[`192155`](../../captures/session_20260908_192155.log): `current=0` open
keeps pass rematerialize (`only_store=5`); three `current=1` `root_rev=41`
opens are **141** / `only_*=0`.
Wrap-head vs last-Off pairing RC1–RC3 is accepted (native shipped;
[`185428`](../../captures/session_20260908_185428.log) RC3 analog PASS;
174646 recapture waived).
Close publish: `NoteRange` reuses the current snapshot pitch
([`note_edit_snapshot_note_range_pitch_bugfix.md`](../Plans/note_edit_snapshot_note_range_pitch_bugfix.md)).
4.4g is closed: `validateAndCleanupMidiEvents` copies
`copyIdleValidateEvents` (current-root span-filtered snapshot events).
Overdub analysis current-root gather already skips pass flatten (4.4a).
RAM1 locals **6848**; native **1773 succeeded, 1 skipped**.
Device [`193700`](../../captures/session_20260908_193700.log): second
overdub open `from=snapshot` at `root_rev=7` `play_rev=7`.
[`194039`](../../captures/session_20260908_194039.log): NOTE_EDIT open
`current=1` `root_rev=8` **177** / `only_*=0`.
[`194438`](../../captures/session_20260908_194438.log): Stage 1 idle
validate at 600.015 s on current `root_rev=1`; both overdub opens
`from=snapshot`.
4.4h Stage 1 shipped: `rebuildDisplayNotesInWindow` copies
`tryCopyCurrentSnapshotDisplayNotesInWindow` on a current root.
Device [`201106`](../../captures/session_20260908_201106.log): second
overdub open `from=snapshot` `root_rev=5` `play_rev=5` `notes=114`.
NOTE_EDIT open `current=1` `root_rev=6` **137** / `only_*=0`. `DFRAME`
**137** after second stop through session end.
DEC-055 Stage 3 device PASS
[`221906`](../../captures/session_20260908_221906.log): first Move
`push,e00` then `resolve,9669,1,3,0`; `phase,setup` 303 µs. Stage 2 device PASS
[`220445`](../../captures/session_20260908_220445.log): first Move
`UNDO_WARM,push,e00` (1358 µs, 79 notes, kind=Move); `focus_snap` = 0.
Stage 1 device PASS
[`212850`](../../captures/session_20260908_212850.log). Option B rejected.
Hydrate 1 device functional PASS
[`224533`](../../captures/session_20260908_224533.log): select neighborhood
and first `push,e00` hold. Fast fader-2 / bar-sweep sluggishness is stacked
Move `GEOM_APPLY` on `hostdrain`, not a neighborhood fail. Hydrate 2–3
device functional PASS [`231008`](../../captures/session_20260908_231008.log):
`candidates_hit` then `candidates_miss`; first `push,e00` holds.
Hydrate 4a mover-only seed is in tree
([`232826`](../../captures/session_20260908_232826.log) /
[`233644`](../../captures/session_20260908_233644.log) Move/Pitch).
DEC-056 miss-path device PASS
[`000740`](../../captures/session_20260909_000740.log): close skips
stale-parent publish; overdub source shows `id=93 p=85`. Current-root
close [`000237`](../../captures/session_20260909_000237.log) `root_rev`
4→6. Hydrate 4a empty apply scratch made no-edit close rematerialize
and bake Deletes; [`001857`](../../captures/session_20260909_001857.log)
hung at `EditSelectNoteState::onExit`. Empty-store bake device PASS
[`002450`](../../captures/session_20260909_002450.log)
`NoteEditPass close skip empty-store bake`. Same capture left NOTE_EDIT
open into OVERDUBBING. Compose Stage 1 device PASS
[`112612`](../../captures/session_20260909_112612.log): capture-active
NOTE_EDIT paint always gathers (`includeActiveCapture`) and keys the
paint cache on `captureDisplayRevision`. Open visual **48**; in-edit
`DFRAME` **53 → 63**. Native **1795 succeeded, 1 skipped**. RAM1 locals
**7008**. HITL `--preset note_edit_overdub_compose` / stimulus
`STIM-NE-OD-COMPOSE-02` v1 (promoted TEMP-A). `01` remains paint-only.
Compose Stage 1+2 device PASS. Exit persist parked mover shipped
([`225149`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_225149.json)
**13/13**; [`note_edit_exit_persist_selected_mover_bugfix.md`](../Plans/note_edit_exit_persist_selected_mover_bugfix.md)).
First-note loop-end close commit is RC1
([`192140`](../../captures/session_20260909_192140.log) `ChangeLength` 80–2303;
watch HITL [`132708`](../../captures/session_20260909_132708.log) did not
reproduce the store-pair form;
[`note_edit_record_first_note_loopend_display_bugfix.md`](../Plans/note_edit_record_first_note_loopend_display_bugfix.md)).
`STIM-NE-OD-COMPOSE-03` last-bar beat-4 Shorten HITL device PASS
[`133714`](../../captures/session_20260909_133714.log). After that recipe,
[`134549`](../../captures/session_20260909_134549.log) Shorten `58` `2102–2111`
then LCR `e=2212` / reopen `empty_step`
([`note_edit_overdub_compose_shorten_hold_select_bugfix.md`](../Plans/note_edit_overdub_compose_shorten_hold_select_bugfix.md)).
([`note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md`](../Plans/note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md)).
([`note_edit_overdub_paint_overlap_compose_bugfix.md`](../Plans/note_edit_overdub_paint_overlap_compose_bugfix.md)).
Do not start hydrate 4b–5.
Plans:
[`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md),
[`note_edit_overlay_delta_architecture.md`](../Plans/note_edit_overlay_delta_architecture.md),
[`note_edit_overlay_delta_refinement.md`](../Plans/note_edit_overlay_delta_refinement.md).
DEC-054 Stage 1 admission remains in tree; [`204458`](../../captures/session_20260908_204458.log)
failed the select-cadence gate. Parent 4.4 closed (inventory
2026-09-09). **4.5 SHIPPED** (2026-09-10): `base` / occupy / compose `02`
PASS. **4.6** Stages 1–6 **SHIPPED** / task checked off (derived-rep identity
freshness; §5 undo/load excluded; §6 materialize retirement remains). Stage 6
HITL `base` [`141547`](../../captures/session_20260911_141547.log).
(A→B@R native; RAM1 **7008**; native **1821/1**). Stage 2+ remain.
VisualCache stamp). Persistence and snapshot-identity undo (§5) remain
later. Hydrate 4b–5 and raise `N` stay parked.
The following publication-admission paragraphs retain historical intermediate
captures; their earlier “recapture remains open” wording is superseded by the
closed 3.5/3.6 evidence above.
Latest `teensy41-capture-serial` build restored RAM1 locals to **7008**
without USB-host ownership changes, so task `3.6` recapture is unblocked.
The follow-up admission fix allows queued session-exit snapshot work after
wrap-journal drain and permits the bounded Snapshot owner while tracks are
stopped but the transport clock remains running. Focused native coverage
passes; full native and device recapture remain open. Construction now
persists membership-copy, mutation, append, and event-sort progress at one
fixed-capacity membership page per admitted unit. The capture-serial build
remains at **7008** RAM1 locals; device `T_RT_reserve` recapture remains open.
Capture [`202635`](../../captures/session_20260827_202635.log) confirms journal
drain and eventual `pending=0`, but still records 79.169 ms and 74.453 ms
slices from whole-pass session-exit start. That start now uses a persistent
`CommittedEventRange` cursor and the existing journal caps so one admitted
unit builds at most one bounded pass-change fragment. Capture
[`204136`](../../captures/session_20260827_204136.log) published `root_rev=8`
when wrap journals drained while session-exit passes were still queued;
playback then dropped the first record pass. Wrap completion now waits for
queued passes, construction progress, and companion changes before
`storePublishedSnapshot`. Stage 1 STOPPED admission is closed on
[`221054`](../../captures/session_20260827_221054.log): Snapshot idle ran
after stop, the candidate stayed private, and publication occurred only at
`pending=0` / `root=0x70400ac8` / `root_rev=10`.
Stage 2 child timing is in tree on `snapshot_idle`
(`start`, `pass_change`, `step`, `finish`, `publish`, `probe`, `enqueue`,
`discard`, `emit`). Capture-serial RAM1 locals **6976**; native **1491/1491**.
Capture [`225659`](../../captures/session_20260827_225659.log) names the
224842 record-stop leftover as `discardPendingIfLoopLengthChanged`
(`discard=15262` on the 20.984 ms PLAYING idle). Exclusive leftover after
that probe is ≤ 39 µs. STOPPED session-exit `start`/`pass_change` reaches
19.996 ms. Publication closed at `pending=0` / `root=0x70431844` /
`root_rev=8`. The Stage 2 falsification review is recorded in
[`snapshot_publication_timing_bugfix.md`](../Plans/snapshot_publication_timing_bugfix.md).
Session-exit length handoff device PASS
[`003015`](../../captures/session_20260828_003015.log): record-stop
PLAYING `discard=47`, first root `pending=0` / `root=0x703248cc` /
`root_rev=1`. Non-fit discard remains a content-sized destructor. Device recapture [`004119`](../../captures/session_20260828_004119.log)
names the 21 ms session-exit as `note_lookup` (86.9% of
`pass_change=21050`). Caller-scoped pitch lookup device PASS
[`005527`](../../captures/session_20260828_005527.log): `note_lookup`
max 857 µs, `pass_change` max 3.219 ms.
Wrap-tick derived reconstruction is removed from the wrap clock interval
([`overdub_wrap_derived_rebuild_clock_interval_bugfix.md`](../Plans/overdub_wrap_derived_rebuild_clock_interval_bugfix.md)).
Musical wrap stays on the clock. Rank 6 next-tick full-loop compatibility
gather is replaced by incremental overdub-seal admission when the one-shot
receipt proves completeness
([`playback_merged_compatibility_rebuild_clock_interval_bugfix.md`](../Plans/playback_merged_compatibility_rebuild_clock_interval_bugfix.md)).
`DEC-049` preserves that merged stream across `stopOverdubbing` so the
same incremental admission can run after stop, and admits Snapshot on 2
of every 6 deferred turns while a publication backlog is present. Device
Stage 2 PASS [`152513`](../../captures/session_20260831_152513.log);
Stage 3 drain PASS [`153113`](../../captures/session_20260831_153113.log)
(`root_rev=7` meets `playback_rev=7`). Live-record snapshot start walks
`capture.store` from a persisted index
([`live_record_snapshot_start_bounded_fragment_bugfix.md`](../Plans/live_record_snapshot_start_bounded_fragment_bugfix.md)).
Device [`190654`](../../captures/session_20260831_190654.log) closed the
copy/scan class; remaining `start` growth was `note_lookup`. That path now
skips `findNoteAtPitch` (`openNotes` only)
([`live_record_snapshot_start_skip_parent_note_lookup_bugfix.md`](../Plans/live_record_snapshot_start_skip_parent_note_lookup_bugfix.md)).
Device [`192422`](../../captures/session_20260831_192422.log) closed the
lookup class: `note_lookup=0` on all 32 live-record starts; after-first
`start` 167–233 µs. Remaining `idle_maint` spikes are display VisualCache
(wrap-stale + `from=passes`). [#41](https://github.com/Lytrix/MidiLooper/issues/41)
Stage 1 device PASS
[`202745`](../../captures/session_20260831_202745.log)
([`display_visual_cache_snapshot_reuse_refinement.md`](../Plans/display_visual_cache_snapshot_reuse_refinement.md)):
no wrap `dsz=0` while notes exist; no wrap `from=passes`; first overdub pass
stays on the paint frame (`183,111` → `184,180`; second wrap `249,246`).
Stage 1b device FAIL [`204132`](../../captures/session_20260831_204132.log):
STOPPED DNTE is short; NOTE_EDIT open rebuild then 291 notes / `presence=0`.
Stage 1c device FAIL [`205138`](../../captures/session_20260831_205138.log)
wrap 2 `205→271` then slice `260`. Stage 1d shipped `d35d64a7`. Device FAIL
[`205917`](../../captures/session_20260831_205917.log): idle Overlay fill
left longs after wrap 2. Stage 1e (in tree): idle rejects Overlay and
fills from current committed
([`display_visual_cache_idle_slice_current_fill_bugfix.md`](../Plans/display_visual_cache_idle_slice_current_fill_bugfix.md)).
Native **1518/1518**. RAM1 locals **6944**. Device recapture required.
Stage 1f (in tree): last-resort STOPPED paint rejects Overlay
([`display_visual_cache_stopped_paint_current_fill_bugfix.md`](../Plans/display_visual_cache_stopped_paint_current_fill_bugfix.md)).
Device gate [`213448`](../../captures/session_20260831_213448.log): first
STOPPED `DISP` must equal visualCache. Stage 1g (in tree): wrap-held
spans dirty On/Off bars only
([`display_visual_cache_wrap_dirty_span_bugfix.md`](../Plans/display_visual_cache_wrap_dirty_span_bugfix.md)).
Device [`221925`](../../captures/session_20260831_221925.log): wrap `dcnt`
inconclusive on a 4-bar all-bar overdub; wrap 3 idle did not fill; STOPPED
**128 / 137** with `DNTE` 39 **144+256**.
Device [`222349`](../../captures/session_20260831_222349.log): wrap first
`DISP` drops then `slice_clean` restores; first STOPPED **159 / 31** then
`OverlayRejected` **159 / 166**; B3 held across stop; no per-note dump.
Stage 1h shipped `4238b422`: `Off@0` does not linear-steal a mid-loop On
([`display_visual_cache_held_note_wrap_pair_bugfix.md`](../Plans/display_visual_cache_held_note_wrap_pair_bugfix.md)).
Stage 1i (native **1525/1525**, RAM1 **6944**): wrap/stop first paint
holds dirty cache
([`display_visual_cache_wrap_stop_first_paint_bugfix.md`](../Plans/display_visual_cache_wrap_stop_first_paint_bugfix.md)).
Stage 1j (native **1526/1526**, RAM1 **6944**): playhead tails are
paint-only
([`display_visual_cache_stop_handoff_playhead_tail_bugfix.md`](../Plans/display_visual_cache_stop_handoff_playhead_tail_bugfix.md)).
Device [`232910`](../../captures/session_20260831_232910.log): 1k wraps
2–8 hold; wrap 1 `104→92` after idle `append_resume` 89 is the grow-vs-prefix
hole (fix: compare to composed hold). Dump `reconstruct` 327 / `frame` 412.
First STOPPED **412 / 208**. 1k/1m device PASS [`002145`](../../captures/session_20260901_002145.log).
Occupy same-start hide is in tree
([`overdub_occupy_same_start_shorter_hides_longer_bugfix.md`](../Plans/overdub_occupy_same_start_shorter_hides_longer_bugfix.md)):
reconstruct keeps both `noteId`s; display adopt/idle keep the shortest end.
Native **1533/1533**. RAM1 locals **6848**. Device recapture required
(`002145` `norow` 75/76; pitch 63 frame 96+288). 1l not in that log.
([`display_visual_cache_wrap_compose_hold_bugfix.md`](../Plans/display_visual_cache_wrap_compose_hold_bugfix.md),
[`display_visual_cache_off_zero_tail_pair_bugfix.md`](../Plans/display_visual_cache_off_zero_tail_pair_bugfix.md)).
Stage 2.1 (idle copies current `LoopSnapshotNote` membership) is in tree.
Stage 2.0 load enqueue is disabled after restore/adopt
([`023222`](../../captures/session_20260908_023222.log) rebooted on a loaded
`start` with `ld=1`). Title still exits on restore completion only
([`020111`](../../captures/session_20260908_020111.log)). After `boot_commit`,
VisualCache Stage 2.1 fallback fills from passes until a current root
exists. Session-exit snapshot publication is unchanged. Device stability
PASS [`024731`](../../captures/session_20260908_024731.log)
([`loaded_hydration_stability_bugfix.md`](../Plans/loaded_hydration_stability_bugfix.md)).
Do not freeze an observed unit or the minimum observed `reserve_us` as the
`T_RT_reserve` guarantee. [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)
records the 10,000 µs policy ceiling. Remaining-interval yield proof is
parked after [`115043`](../../captures/session_20260908_115043.log)
(`remaining=0` until STOPPED). `kSnapshotIdleReserveYieldEnabled` is false;
yield helpers stay compiled. OpenSpec `3.5`/`3.6` are closed. OpenSpec `4.3`
is also closed: `LoopContentResolution` now exposes explicit stateless
`LoopSnapshot` query overloads; compatibility pass/index/checkpoint paths
remain until caller migration. Next is `4.4`, which requires a new phase
selection and gate. Construction bounds stay unchanged.
For open 4b.3 attribution, `TrackManager::updateAllTracks` now emits
capture-only per-track `track_update` maxima and over-budget counts alongside
the aggregate `tracks` timing; device recapture is required.
Display paint fallback and visual-cache fill read published snapshot events
when a root exists; compatibility gather remains the no-snapshot fallback.
A nonempty dirty visual-cache prefix does not authorize the paint window
([`235954`](../../captures/session_20260825_235954.log)). Live overdub keeps
the PLAYING committed handoff only while the paint window stays inside
the last gather ([`001048`](../../captures/session_20260826_001048.log),
[`001907`](../../captures/session_20260826_001907.log)). First gather
includes a short loop tail so a 20-bar loop does not omit the last 2 bars
([`002634`](../../captures/session_20260826_002634.log)). Overdub-stop paint
holds the composed frame — device PASS [`004934`](../../captures/session_20260826_004934.log)
(`ODUB,stop,display` 8.8 / 12.2 ms). 4b.0 wrap/hold device PASS
[`092755`](../../captures/session_20260826_092755.log)
(`ODUB,stop,display` 21.4 / **11.1 ms** after wrap). **Now:** prepare
decomposition D1 **device PASS**
[`221655`](../../captures/session_20260826_221655.log) after 1A measure FAIL.
Next is D2 design session on
[`playback_window_prepare_algorithm_decomposition_investigation.md`](../Plans/playback_window_prepare_algorithm_decomposition_investigation.md)
([#38](https://github.com/Lytrix/MidiLooper/issues/38)). Ring B/RC2 is
**closed.** Residual **4b.1b** lives on
[`playback_window_build_owner_boundary_refinement.md`](../Plans/playback_window_build_owner_boundary_refinement.md)
§ *4b.1b* and is parked. Ring A firmware is not authorized. Ring B/RC2 shipped
the five-owner cursor (snapshot, visual-cache, wrap-append, persistence,
load/display) and RC2 coverage: refuse uncovered visual-cache reads; fall back
to committed-pass reconstruction. Device PASS
[`183452`](../../captures/session_20260826_183452.log) / reconfirm
[`185141`](../../captures/session_20260826_185141.log): wrap `from=passes`,
`DISP` 286→466 / 271→437. First-overdub `midi_gap` 222.596→146.802 ms vs
[`175350`](../../captures/session_20260826_175350.log); later steady 24.325 ms
vs 366 ms. Reconstruct timings stay on the owner-boundary plan.
OpenSpec `4b.2` first-take length updates during RECORDING remain in tree;
live-record snapshot append starts now admit at most one completed bar of
new range per start so setup cost stays smaller in realtime windows;
first-take display keeps continuous playhead length (host implement; device
verify open). Ring B now owns the five-way deferred idle turn; `4b.3`
post-stop `usbdisp` /
`load_frame` / `late_on` is now in tree (device verify open);
`4b.4` overdub `DisplayResolveLiveCapture` owner refinement is now in tree
(device verify open). `DEC-046` accepts the round-robin admission and
representation-coverage direction. Ring B/RC2 firmware is **closed**.
RC4 repair and Ring A remain separate.
Slice 1 telemetry is in tree and device-validated by
[`161834`](../../captures/session_20260826_161834.log) and
[`162522`](../../captures/session_20260826_162522.log): the dominant
long-loop rebuild destination is `T0/S6`, with `T4/S0`, `T6/S0`, and `T2/S2`
also observed; `162522` directly pairs the repeated compatibility gathers with
`T0/S6`. The playback rebuild `<count>` is rebuild-call count before one-shot
emission, not distinct-destination multiplicity or an eligible-set count.
RC4 question 3 is closed from code: `adoptPersistedSnapshot` /
`restorePassesSnapshot` do not publish a `LoopSnapshot`. Question 1 is
closed by [`165024`](../../captures/session_20260826_165024.log) and
[`171159`](../../captures/session_20260826_171159.log):
all 16 `cer_heap` rows report `spill=0`, `ext=0`, `clk=0`, and `iclk=0`.
The measured internal-first spill and nested-clock mechanisms are ruled out.
`cer_recon` is in tree to split internal-heap `filt` from PSRAM-first
`span`/`proj` and to record IntervalTimer ISR `itickus`.
[`170149`](../../captures/session_20260826_170149.log) rebooted at first
clock after Record; probe counters are now file-scope volatiles.
[`171159`](../../captures/session_20260826_171159.log) closes question 1:
`filt` stays flat, `span`/`proj` take the 7.9x, `itickus=0` on every row.
The follow-up `cer_snapshot` formatting probe was removed after
[`172305`](../../captures/session_20260826_172305.log) rebooted at record start.
`playback_build` now carries a deferred `path` field (`snapshot`,
`compatibility`, or `note_edit`) so later investigation captures can identify
the representation behind each emitted maximum sample without formatting on the
clock/interval path. The existing one-shot remains an aggregate: `count` is all
rebuild calls before emission, not distinct destinations.
Device capture [`202654`](../../captures/session_20260826_202654.log) validates
the split: 82 emitted samples contain 74 `compatibility` and 8 `snapshot`
paths, with no `note_edit` samples. `T0/S6` remains compatibility-backed
(maximum 325.541 ms), while `T4/S0` uses the published snapshot
(maximum 358 us). RC4 question 5 is closed as that `T0/S6` compatibility
replay; ignore it for snapshot-path sizing. **G1 is satisfied:**
`representativeRebuildWall` = 358 µs; `prefetchTicks` = 272; horizon
unchanged. Ring A firmware is not authorized.
[`205447`](../../captures/session_20260826_205447.log) `T0/S5` rebuild is
334 µs; the 377 ms gap at that switch is `idle_maint`, not the new-slot
rebuild.
[`172802`](../../captures/session_20260826_172802.log) confirms first-clock
recording and continued overdub/stop operation after removal.
Historical checkpoint: task 4.3 was still skipped at this capture.
[`125527`](../../captures/session_20260826_125527.log) kept unpublished
snapshot in-flight through OVERDUBBING/PLAYING with repeated
`snapshot_idle` slices and `midi_gap` spikes before cadence alternation.
[`130244`](../../captures/session_20260826_130244.log) still stacked
unpublished snapshot with long visual idle reconstruct bursts; host
refinement now constrains/gates that visual fallback during contention.
[`131338`](../../captures/session_20260826_131338.log) still showed PLAYING
stalls (`midi_gap` 195 ms, 184 ms) with first-PLAYING stacked
`idle_maint`/snapshot/persist and later `usbdisp`/`load_frame`/`late_on`
windows; host refinement now broadens unpublished visual constraints to
all pending unpublished work and caps each live-record append-start range.
[`131852`](../../captures/session_20260826_131852.log) confirms the parked
issues remain: same-turn `snapshot_idle` + `PERS,work` overlap is still
visible (`snapshot_idle us=26823` then `PERS payload_slice=20806`), large
OVERDUBBING windows remain (`midi_gap=236090`, `usbdisp=143649`,
`load_frame=25638`), STOPPED still hits `midi_gap=430552`
(`usbdisp=538063`, `load_frame=401498`, `idle_maint=337246`), and overdub
display still spikes (`timing_max,DisplayResolveLiveCapture,215619`) while
`ODUB,stop,display` remains 21.9-50.7 ms. In tree: same-turn lane now blocks
deferred save after snapshot on the same loop turn; recapture pending and
4b.1 remains open.
[`132627`](../../captures/session_20260826_132627.log) no longer showed
`PERS,work` while `snapshot_idle` was active (same-turn overlap not observed
in that run), but 4b.3 and 4b.4 owners remained:
post-overdub PLAYING `midi_gap=175230`
(`usbdisp=121603`, `load_frame=14728`, `late_on=184364`), STOPPED
`midi_gap=198666` (`usbdisp=319516`, `load_frame=191331`, `late_on=30491`),
and `timing_max,DisplayResolveLiveCapture,157754`. In tree for 4b.3:
post-overdub drain now polls MIDI during `runDeferredLoadAndDisplayFrame`
between display/load slices; recapture pending.
[`133517`](../../captures/session_20260826_133517.log) still showed
`snapshot_idle`/`PERS,work` interleaving while pending (example:
`snapshot_idle us=7360` then `PERS,work 86`), and OVERDUBBING spikes remained
(`midi_gap=200549` with `usbdisp=129828`, `load_frame=23600`, `late_on=26036`;
`midi_gap=196173` with `usbdisp=68686`, `load_frame=15178`, `late_on=96834`,
`persist_save=32056`). Post-overdub PLAYING windows in that run were in the
26-52 ms class after stop-display lines (`ODUB,stop,display` 19.6-51.4 ms).
In tree for 4b.1: lane arbitration now includes RECORDING pending snapshot
work (not PLAYING/OVERDUBBING only), and persist lane toggles now use
RECORDING/PLAYING/OVERDUBBING pending snapshot detection; recapture pending.
[`134438`](../../captures/session_20260826_134438.log) showed no `PERS,work`
entries (`0` matches) while `snapshot_idle` remained active, so same-turn
snapshot/persist overlap was not observed in that run. 4b.3 remains open:
post-overdub PLAYING still reached `midi_gap=242938`
(`usbdisp=149450`, `load_frame=53120`, `late_on=69979`) after
`ODUB,stop,display=44222`; STOPPED still reached `midi_gap=323973` with
`loop_rem,idle_reconstruct=148417`, `loop_rem,idle_maint=180315`,
`idle_maint=319996`, and `load_frame=38737`. 4b.4 remains open with
`timing_max,DisplayResolveLiveCapture=225601`. In tree for 4b.3:
`MidiHandler::handleMidiInput` now reduces per-call dispatch batch size from
`128` to `32` while `anyPlayingMidiDrainAfterOverdubStop()` is active, so
post-overdub drain turns bound `usbdisp` burst size before the next poll.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS; recapture
pending.
[`135317`](../../captures/session_20260826_135317.log) again showed
`PERS,work=0` while `snapshot_idle` remained active (4b.1 still open pending
repeated recaptures). 4b.3 remains open: first stop cycle held
`ODUB,stop,display=15428`, but a later `midi_gap=181392` was logged after
PLAYING re-entered OVERDUBBING; later cycles were `midi_gap=42033` and
`midi_gap=22132`. 4b.4 remains open with
`timing_max,DisplayResolveLiveCapture=163008`. In tree for 4b.4:
`DisplayManager::rebuildOverdubCommittedDisplayLayer` now skips overdub paint
snapshot gather+reconstruct when `visualCache` already has notes, preferring
held-layer/window-filter branches to bound resolve cost. Verification:
`pio run -e teensy41-capture-serial` PASS with RAM1 locals `4608`, and
`pio test -e native` PASS (`1472/1472`).
[`140201`](../../captures/session_20260826_140201.log) kept `PERS,work=0`
while `snapshot_idle` remained active. 4b.3 improved but remains open:
post-stop PLAYING `midi_gap` reduced to `48361` (from `142534` in `135317`),
but first-bars OVERDUBBING still reached `midi_gap=133468`. 4b.4 improved
strongly in this run with `timing_max,DisplayResolveLiveCapture=7107`
(from `163008`). The same capture showed overdub reopen source drift
(`DIAG,lcr,src,why=open` notes `0`/`165`/`228` versus nearby PLAYING `DISP`
totals `240`/`253`/`308`) and restamp miss evidence
(`seal_publish_span_cache_restamp ... invalidated_missing_id` together with
`capture hot stop verify: non-canonical storage (check=16)`). In tree:
`Loop::commitPendingCapturePass` now invalidates source-span cache on restamp
miss, and `Loop::rebuildOverdubSourceSpanCache` now seeds from clean
`visualCache` when prepared spans miss and no prior cache seed exists.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS;
recapture pending.
[`141034`](../../captures/session_20260826_141034.log) confirms 4b.4 stays
bounded (`timing_max,DisplayResolveLiveCapture=8608`), so 4b.4 is closed.
4b.3 remains open: first-overdub windows still show repeated
`load_frame`+`usbdisp`+`late_on` lag signatures around bars 4-8, and wrap
reopen still showed `DIAG,lcr,src,why=open ... notes=0` after a restamp miss.
In tree: source-cache fallback now also seeds from non-empty dirty
`visualCache` when prepared spans miss during an overdub session.

**Queued-dest visual cache:** `invalidateForSlotChange` leaves a PLAYING queued
destination `visualCache` in place. Firmware in tree; HITL open. Plan:
[`playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md`](../Plans/playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md).

**Boot title RAM1:** `cccbeaa` margin regression fixed — DMAMEM cold globals + `assignMissingNoteIdsToNoteOnsInChunkIds` in `.cpp`. Device PASS [`161855`](../../captures/session_20260816_161855.log). Investigation frozen: [`boot_title_loadloopjob_idle_slice_investigation.md`](../Plans/boot_title_loadloopjob_idle_slice_investigation.md).

**NOTE_EDIT entry while overdubbing:** `openNoteEditSession` calls `stopOverdubbing()` first — live capture commits before rematerialize (quick fix).

**NOTE_EDIT lengthened loop select:** committed-content span for bracket/nav; skip detailed-window filter on selectable inventory. Fixture [`015731`](../../captures/session_20260814_015731.log).

**Overdub + NOTE_EDIT exit bake:** device PASS [`025322`](../../captures/session_20260814_025322.log) — `rows=1 saved=1`, session flats 234 held. Native `dropUnrequestedSessionStoreDeletes`. Stage 3 unblocked.

**OLED first-frame mismatch:** RC3 device PASS [`014553`](../../captures/session_20260814_014553.log). Plan: [`oled_dma_partial_frame_bugfix.md`](../Plans/oled_dma_partial_frame_bugfix.md).

**Loop content-only history (DEC-035):** **Archived** 2026-08-14 — OpenSpec `2026-08-14-loop-content-history-persistence`; normative `openspec/specs/loop-content-history/`. Device PASS [`030147`](../../captures/session_20260814_030147.log) / [`032227`](../../captures/session_20260814_032227.log).

**NOTE_EDIT leave-restore painted span:** RC1 native shipped; device gate open — [`note_edit_overlap_leave_restore_painted_span_bugfix.md`](../Plans/note_edit_overlap_leave_restore_painted_span_bugfix.md). [`200154`](../../captures/session_20260813_200154.log) Restore 5 `720–2255` / select `DNTE` 1535; same-loop [`193838`](../../captures/session_20260813_193838.log) first-select paints length **47**.

**NOTE_EDIT mover wrap-length jump:** RC1 device PASS in [`200154`](../../captures/session_20260813_200154.log) — [`note_edit_mover_wrap_length_jump_bugfix.md`](../Plans/note_edit_mover_wrap_length_jump_bugfix.md). No `2351`/`2975`. Note 14 `2256–2304` stays on the wrap-stub plan.

**NOTE_EDIT note-off pairing LIFO:** native shipped; [`213920`](../../captures/session_20260813_213920.log) multi-overlap device PASS — [`note_edit_note_off_pairing_lifo_bugfix.md`](../Plans/note_edit_note_off_pairing_lifo_bugfix.md). @45.971 seals five overlap rows plus the mover (`canonical=7 pre_commit=7`).

**NOTE_EDIT edit-pass replay row payload:** native shipped; [`211832`](../../captures/session_20260813_211832.log) device PASS — [`note_edit_replay_row_payload_bugfix.md`](../Plans/note_edit_replay_row_payload_bugfix.md). `Length 115 48→287` and `Length 115 1008→1103` seal and hold. Rows replay as stored.

**NOTE_EDIT overlap shorten commit seal:** native shipped; device gate open — [`note_edit_overlap_shorten_commit_seal_bugfix.md`](../Plans/note_edit_overlap_shorten_commit_seal_bugfix.md). Deselect commit dropped the overlap `Length` row on a parked mover; [`204700`](../../captures/session_20260813_204700.log) @166.809 `canonical=1 apply_owned=2` then @166.848 seals `Length 10 960–1247` against note 14's focus. Slice B of the resolver contracts plan **withdrawn** — [`225025`](../../captures/session_20260807_225025.log) `len=287` was the correct truncation.

**NOTE_EDIT Length replay loop-boundary:** native shipped; device gate open — [`note_edit_length_replay_loop_boundary_bugfix.md`](../Plans/note_edit_length_replay_loop_boundary_bugfix.md). Persisted `ChangeLength` 14 `2256–2304` replayed as a wrap; same-pitch notes shortened to 2255. No note ends at 2255 in [`204700`](../../captures/session_20260813_204700.log) / [`205054`](../../captures/session_20260813_205054.log).

**NOTE_EDIT wrap-stub commit:** RC3 native shipped; device gate open — [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](../Plans/note_edit_overlap_action_drop_and_wrap_stub_bugfix.md). RC2 device FAIL: 14 still `2256–2304` when cache paints a non-zero span. RC1 withdrawn (67.824 min-length stay-hidden).

**NOTE_EDIT display unification:** Stages 1–2 and 5–9 shipped — [`note_edit_visual_cache_display_unification_refinement.md`](../Plans/note_edit_visual_cache_display_unification_refinement.md). Device [`192007`](../../captures/session_20260813_192007.log): 45 Hide/Restore `1440–1511` (not 2160); 76 as target still `840–2160`; first NOTE_EDIT `DISP` 59/60; resolve 16.8–52.3 ms. Paint gap and undo-warm stay open.

**Active work:** Occupy clock same-tick Off-before-On HITL **PASS** [`001021`](../../captures/session_20260819_001021.log) ([`overdub_occupy_clock_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_clock_same_tick_off_before_on_bugfix.md)) — `n=0 a=1` = 0; `n=1 a=0` = 0. Parent catch-up pin [`235314`](../../captures/session_20260818_235314.log) was 4. Pin [`233247`](../../captures/session_20260818_233247.log) was 11. Interval catch-up in tree. Merged-capture gather **FROZEN**. USB occupy `playMidiEvents` catch-up HITL **FAIL** [`214856`](../../captures/session_20260818_214856.log) **reverted**. Loop-head storage-0 occupy HITL **PASS** [`203948`](../../captures/session_20260818_203948.log). Wrap-S catch-up HITL **PASS**. Occupy reads `ActiveNoteLedger` at `currentTick` ([DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat)). Occupy lookup: `ledger.noteId(midiChannel, pitch)`. Phase 2 **PASS** [`040236`](../captures/session_20260818_040236.log). PLAYING/STOPPED/MUTED HITL **skipped** ([DEC-040](../DECISION_LOG.md#dec-040-skip-playingstoppedmuted-overdub-participant-hitl)). **Phase 4 in tree**. Phase 3 1-bar occupy **PASS** [`121933`](../captures/session_20260818_121933.log). Stage 1 `from=span` HITL **parked**. Do not shrink `kOverdubSourceWindowBars`. Queued (no firmware): [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md). **NOTE_EDIT hydrate** queued ([`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md)). **Playback gather** Stage 1 hooks landed. Wrap-move persist parked [`201446`](../captures/session_20260816_201446.log).

**Merged to `dev`:** PR [#35](https://github.com/Lytrix/MidiLooper/pull/35) LoopContentResolution prototype (`be0e05f`); PR [#30](https://github.com/Lytrix/MidiLooper/pull/30) overdub overlap; PR [#29](https://github.com/Lytrix/MidiLooper/pull/29) Stage 5a-1/5a-2 + display RC4–RC5.

**Stage 5a-3:** **`pool_alloc` proof abandoned** (2026-08-12) — see [`long_overdub_stage5a3_critical_reclaim_verification_refinement.md`](../Plans/long_overdub_stage5a3_critical_reclaim_verification_refinement.md).

**`chore/firmware-ownership-lifetime-review`** — P0/P1 review **closed** (manual MT gates PASS); Phase 5 + layered **`base`** **parked**.

**`feature/deferred-lazy-load`** — Phase B archived `openspec/changes/archive/2026-07-19-deferred-job-scheduler/`. Specs: `openspec/specs/deferred-job-scheduler/`.

*(Parallel: **`feature/persistence-work-queue`** @ `8b3f93e` — B1–B5 on `dev`.)*

### Firmware lines

| Line | Branch | Role |
|------|--------|------|
| v1 | `main` | Frozen — 16×2, 2-button looper |
| v2 | `midi-faders` | Frozen — iPad/DROID, no PSRAM required |
| v3 | `dev` | Active integration |

See [`docs/BRANCHING.md`](../BRANCHING.md).

## In flight

- **§36 companion admission + STOPPED first-paint:** A+B+C closed. Device PASS [`165107`](../../captures/session_20260821_165107.log) / [`183229`](../../captures/session_20260821_183229.log). Later (parked): bound `applyDeferredVisualCacheWrapAppend` (1.13 s), attribute 5 s STOPPED idle. — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Boot title `boot_commit` display stall:** Device PASS [`161804`](../../captures/session_20260821_161804.log) — USB 11.484 s, first `DISP` visual=2638, `boot_commit=288557` (289 ms). Prior gather was `161148` / `154353` (~70–75 s, visual=0) from `onBootSlotLoadComplete` wiping the idle-built cache. Guide: [`BOOT_LOAD.md`](../Guides/BOOT_LOAD.md).
- **Boot title startup stall hardening:** capture [`161410`](../../captures/session_20260820_161410.log) stops after `DisplayManager: Display buffer displayed` and never reaches boot load logs. `Looper::setup` now uses bounded SD bootstrap retries (`3` attempts, `50 ms` delay) and an explicit empty-runtime fallback when SD stays unavailable, preventing title-screen startup stalls from requiring repeated manual reboots. Native verification pending.
- **Overdub-stop seal lag optimization:** captures [`163904`](../../captures/session_20260820_163904.log), [`164944`](../../captures/session_20260820_164944.log), [`165534`](../../captures/session_20260820_165534.log), [`165956`](../../captures/session_20260820_165956.log), [`170724`](../../captures/session_20260820_170724.log), [`171246`](../../captures/session_20260820_171246.log), [`171909`](../../captures/session_20260820_171909.log), [`172553`](../../captures/session_20260820_172553.log), [`173206`](../../captures/session_20260820_173206.log), and [`174941`](../../captures/session_20260820_174941.log) keep the stop owner on `companion_seal` while `mark_range` remains low. Deterministic follow-up landed: overdub-stop path uses `SC_REC_FLUSH_PENDING_REVTS(8)` in `Track::stopOverdubbing` and in-edit `handleNoteEditFold`, removing variable USB serial flush work from the critical path before display refresh. Telemetry-visibility follow-up landed: `ODUB,stop` lines are Tier-A in `CaptureLineTier` so stop-stage lines remain visible under ring pressure with `flush(8)`. Display-specific follow-ups are reverted per user request. Native **1397/1397** and `teensy41-capture-serial` build pass (`RAM1 code 425084 / locals 4768`); HITL re-measure pending — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Idle-gather edit apply Phase A:** `applyNoteEditPassSequence` now performs strict contiguous `Velocity`/`Pitch` update collapse with barrier-preserving execution; non-collapsing baseline parity is covered by dedicated deterministic and randomized equivalence tests in `test_edit_apply`. Validation gates passed (`pio test -e native`, `pio run -e teensy41-capture-serial`) with RAM1 still within budget (`RAM1 code 424668 / locals 4640`) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start begin-capture long delay:** captures [`165534`](../../captures/session_20260820_165534.log) and [`165956`](../../captures/session_20260820_165956.log) show early-start delays (`Live Overdub -> Overdubbing started` ~2102-2249 ms) and `ODUB,stage,begin_capture` spikes (`2194189 us`, `2248843 us`). Root cause is start-path fallback to full-loop materialize in `Loop::rebuildOverdubSourceSpanCache` when prepared spans are unavailable. Stage now restores overdub-session deterministic fallback (reuse prior cache notes during active sessions; full materialize kept for non-session paths). Native **1397/1397** and `teensy41-capture-serial` build pass (`RAM1 code 425020 / locals 4768`); waiting on HITL re-measure — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start begin-capture long delay (closed):** HITL [`170724`](../../captures/session_20260820_170724.log) confirms the restoration removed the 2.2 s start stalls: `Live Overdub -> Overdubbing started` is now 2-8 ms and `ODUB,stage,begin_capture` is 8198 us / 2150 us. Same run exposes a remaining stop long-tail cycle (`ODUB,stop,seal=310980 us`, `flush=312270 us`, `display=320406 us`, `overlap_hold note_offs=100`), while `DIAG,seal_companion_batch` remains bounded (`rows=5 sealed=5 us=84`). Start regression is fixed; stop-path lag is still open — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start reboot (fault now read; repro conditions narrowed; hang reproduced):** USB drops right after `[TRACK] Overdubbing started`. Present in [`132145`](../../captures/session_20260820_132145.log) and [`143518`](../../captures/session_20260820_143518.log), i.e. before the 2026-08-20 removals, so commits `4af158a`…`6ff4142` are reverted and the tree is byte-identical to `28cf6e3`. **Two failure modes under one symptom, both memory exhaustion.** (a) *Fault:* the 2026-09-17 00:39 session's SD crashlog reads `DACCVIOL` at `Accessed Address: 0xE00` with `Code was executing from 0x6002C97C` = `Loop::tryGatherPublishedSnapshotEventsInWindow`. **The `0xE00 == 128 * sizeof(MidiEvent)` stride argument is withdrawn (2026-09-17, step B):** `sizeof(MidiEvent)` is **20 bytes** on device, measured with the `teensy41-capture-serial` toolchain, so the address does not decode to element 128 of a `MidiEvent` store; 28 is `sizeof(PlaybackBridgeRecord)`, and the faulting object is unidentified. The "snapshot-gather fill through a lost destination base" attribution is therefore **unverified** (the landed gather guard remains useful defence in depth). `ExternalMemoryFirstAllocator` aborts rather than returning null, which is the part that still stands. (b) *Hang — reproduced:* an 8-bar record plus overdub on track 4 slot 1 reached `[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are exhausted!` and went permanently silent with USB still enumerated, leaving **no crash record** (`crashlog.txt` unchanged, next boot `crash=0`) — `abort()` raises no `CrashReport`, which is why this has only ever shown up as a silent hang ([`hang-8bar-track4-slot1-01`](../../captures/hang-8bar-track4-slot1-01/session_20260917_094002.log)). Internal heap was already `free=68 used=393 of 461 KB` at 127.7 s uptime, ~212 KB down from boot, before that record began. User's repro conditions: several tracks already playing loops, then an 8+ bar pass with overdub. Diagnostic landed SESSION_CAPTURE-gated (gather destination guard + `displayManager` RAM2 tail canary, `free for local variables` 6752 → 6720). Native **2192 cases: 2188 passed, 1 skipped, 2 known unguarded reentry failures**; RAM1 locals **6720**; instrumented firmware flashed. **Both boundaries now deny instead of aborting** (overdub via `admitOperationMemory`, record arm via the required prewarm's own result - `Track::startRecording` returns before any mutation, and a missing runtime slot is a denial too). Device [`arm-admission-01`](../../captures/arm-admission-01/session_20260917_113447.log): one `op=companion_history_reserve`, one `op=record_start`, zero `Recording started`, zero `FATAL`, device alive. Native **2193 cases: 2189 passed, 1 skipped, 2 known unguarded reentry failures**. Next (user-directed order): per-buffer lifetime audit of the arm reservation, bounded shared/reusable stop working storage, rerun the loaded-set/8-bar recipe, then the 1334 KB steady staging. **All five are now investigated.** Loaded-set 8-bar rerun [`stepD-arm-delta-01`](../../captures/stepD-arm-delta-01/session_20260917_115207.log): zero `FATAL`, zero `Recording started`, device alive, playback continuing, one `op=companion_history_reserve` + one `op=record_start` denial, **arm delta measured at 1,240 KB**, heap 64-76 KB at the refusal (well above the 32 KB floor). The audit shows the seven prewarms are one concurrently-live working set (not shareable) and that `sizeof(MidiEvent)` is **20**, not 28, which withdraws the `0xE00 == 128 * sizeof(MidiEvent)` crashlog attribution. The 1334 KB is **slot-provisioned boot staging** (~157 KB per content slot) for slots that may never record; the recommended fix is admission-provisioned staging at the arm, which returns ~1.33 MB at boot. Capacity shaping (C) and the staging move (E) change a gated execution bound and need a DEC before implementation. Transport-start identification is done: the transport-start 81920-byte request is the *deferred record arm* (`pendingRecord` released by `handlePendingRecordStart`), so step A already gates it - no separate transport admission gate is added. — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub session index reboot undo:** HITL **PASS** [`003854`](../../captures/session_20260820_003854.log) — reboot `entries=2` `kind=1`; one undo 112→14 — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Cleanup branch telemetry/fader compile-gating:** `chore/cleanup-codebase-tidiness` stage shipped — telemetry-only timing work gated by owning feature macros; `MidiHandler` motor-fader USB-host service helper now feature-gated to `MIDI_USB_FADER_PROBE_PASSTHROUGH`; verification passed (`pio test -e native`, `pio run -e teensy41`, `pio run -e teensy41-capture-serial`) with zero warnings.
- **Overdub-stop handoff flash:** dirty `visualCache` must not overwrite the RC5a composed frame after overdub stop. HITL **PASS** [`001925`](../../captures/session_20260820_001925.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Display window follow readiness:** committed paint filters `visualCache.notes` for the moving 16-bar window plus lookahead; source-view is not display authority. Native **1390/1390**; RAM1 code **425276** / locals **4768**. HITL **PASS** [`000553`](../../captures/session_20260820_000553.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Companion-only projection fast path:** Full companion projection cache is admitted only while `capture.phase == CapturePhase::Overdub`. STOPPED/boot gather uses windowed apply. Mixed companion+non-companion rows keep legacy ordered replay fallback. Stage A on [`165107`](../../captures/session_20260821_165107.log) keeps Overdub `tracks` at 190 ms (was 136 s on [`162928`](../../captures/session_20260821_162928.log)).
- **Overdub participant discovery:** `feature/overdub-participant-discovery` — occupy clock same-tick Off-before-On HITL PASS [`001021`](../../captures/session_20260819_001021.log); parent catch-up pin [`235314`](../../captures/session_20260818_235314.log) was 4 `n=0 a=1`; USB `playMidiEvents` catch-up HITL FAIL [`214856`](../../captures/session_20260818_214856.log) reverted; wrap-S HITL PASS; loop-head storage-0 HITL PASS [`203948`](../../captures/session_20260818_203948.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Codebase consistency:** [#18](https://github.com/Lytrix/MidiLooper/issues/18) **NOW** — Phase 4 TrackManager **shipped** PR #22; next extractions in [`codebase_consistency_phase4_extraction_boundaries_refinement.md`](../Plans/codebase_consistency_phase4_extraction_boundaries_refinement.md)
- **StorageManager TU remaining:** **Merged** PR [#17](https://github.com/Lytrix/MidiLooper/pull/17) → `dev` (2026-08-08); closeout [#16](https://github.com/Lytrix/MidiLooper/issues/16)
- **Firmware ownership lifetime review:** **Closed** 2026-08-06 — [`firmware_ownership_lifetime_review.md`](../Plans/firmware_ownership_lifetime_review.md); layered **`base`** HITL **parked** for dedicated refactor
- **HITL CLI rebuild:** Phase 3 — layered presets locked to `base` + `edit_full`; layered **`base`** device PASS deferred to dedicated HITL refactor
- **Edit-session-action-geometry:** **Archived** 2026-08-05 → `openspec/specs/edit-session-action-geometry/`; Phase 5 HITL matrix parked (`m8_edit_note_edit_hitl_automation_refinement.md`)
- **Note-edit control-surface split:** **complete** on `chore/note-edit-control-surface-split` — Phases 0–8; see [note_edit_control_surface_split_refinement.md](../Plans/note_edit_control_surface_split_refinement.md)
- **Long record display freeze:** queued — [`long_record_onset_display_freeze_bugfix.md`](../Plans/long_record_onset_display_freeze_bugfix.md); evidence [`012342`](../../captures/session_20260812_012342.log)
- **Next product slice:** loop length during overdub queued — [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md)
- **Archived this branch:** OpenSpec [`deferred-job-scheduler`](../../openspec/changes/archive/2026-07-19-deferred-job-scheduler/) Phase B — gates B.1 [`231510`](../../captures/session_20260718_231510.log), B.3/B.4 [`022107`](../../captures/session_20260719_022107.log); specs in `openspec/specs/deferred-job-scheduler/`
- Memory reclaim + boot/display stack — **merged to `dev`** (PR #4)
- **Hygiene (`chore/codebase-hygiene-sprint1`):** safe debt **complete** — see [`codebase_hygiene_technical_debt_review.md`](../Plans/codebase_hygiene_technical_debt_review.md); sprint plans Status Done + [README index](../Plans/README.md#hygiene-sprint-chorecodebase-hygiene-sprint1)

## Shipped on branch (recent)

| Commit / session | Focus | Docs |
|--------|--------|------|
| `3e9253e` | DEC-029 archive + spec sync + Phase 8 closeout | DELIVERABLE_TRACKING, PHASE8_CLOSEOUT |
| `15c5750` | Playing move/length `refreshPlaybackPreview` | note_edit_playing_move_audition_bugfix |
| `7af8671` | DEC-030 §12 R1–R5 orthogonal state | note_edit_resolver_authority_contracts_refinement |
| `5af41c7` | P1 fold wrap unify; NOTELEN exit bracket; HITL serial proxy | firmware_ownership_lifetime_review |
| `85ae7d6` | P1 undo routing docs (session-gated **E:**) | LOOP_MIDI, ARCHITECTURE_RULES |
| `d87d0c6` + [`010126`](../../captures/session_20260718_010126.log) | Windowed display + queued countdown; device gate PASS | boot_load_windowed plan |
| `8b3f93e` | Sync-drain budget, clear-slot restore, post-reboot undo save | persist queue plan |
| `7a89f03` | Retire monolithic deferred save; work queue (B4) | persist queue plan |
| `b3d066f` | `stepPersistenceWorkItem` scheduler hook (B3) | persist queue plan |
| `3143ef0` | `StorageManager::admit*` API (B2) | persist queue plan |
| `736fa33` | NOTE_EDIT move/pitch bracket — mover NoteId lookup | CURRENT_WORK |
| `0f086f4` | Split-focus slot switching + loop-end commit (DEC-025) | Loops.md |
| `f6b496a` | Slot short-press deferred-restore fix | — |
| `346f7ce` | Playhead projection + undo geometry restore | — |
| `b1d3259` | Loop-scoped undo display (DEC-024 Phase 1) | loop_undo plan |
| `50ad01b` | Cold boot + USB Host defer + slot scan hardening | DEC-021/022 |
| `e40f26c` | DEC-020 Phase 4 mid-pass persistence writer | DEC-020 |
| `f0ee520` | DEC-020 Phase 3 cooperative scheduler HITL | DEC-020 |
| *(prior)* | Phase A step 3 + Phase B — one materialize per revision | handoff |
| `d635296` | Partial Phase A — PLAYING defer, REVT bounds; boot + 16-bar smoke | handoff |
| `eea4734` | Serial evidence index (archived captures) | plan |
| `894d2ea` | Runtime architecture docs + DEC-016 | Architecture/ |
| `d05e736` | Display boot/play OLED freeze | — |
| `b1260ce` | Cold buffers → external memory pool | INTERNAL_HEAP guide |

## Active OpenSpec

This branch's active scope is the design-only
[`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/)
change. The maintained-LCR changes below are retained as historical base context
and are not implementation scope here.

| Change | Focus |
|--------|--------|
| **`immutable-loop-snapshot-passes`** | Phase 2 publication complete; 3.5/3.6 and 4.1–**4.6** closed; **5.1**–**5.7** SHIPPED; **6.1**–**6.4** SHIPPED (**6.4** device [`164947`](../captures/session_20260912_164947.log) 2026-09-12); next open: **4b.3**, **6.5**; §**6.6** post-closeout polish non-blocking; [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write); boot sync load still legacy; snapshot nodes pack into 32 KB slabs (`DEC-048`) |
| `loop-content-resolution` | Historical base prototype; maintained-LCR continuation not pursued on this branch |
| `loop-effective-event-source` | Historical base context; no new LCR work on this branch |
| **`hitl-cli-rebuild`** | **Phase 3 in progress** — layered `base` + `edit_full`; 3.2 bridge done; next: 3.3 device PASS |
| **`continuous-runtime-persistence`** | Phase 0–4 shipped; Phase 4 HITL **passed**; Phase 5 recovery **parked** (see firmware ownership review) |
| **`runtime-derived-representation-heap`** | M5 Steps 1–1b–2 shipped (`adoptPersistedSnapshot`); lazy load + 64+64 HITL pending |
| **`slot-selection-focus`** | **Shipped (firmware)** — orchestrator + footer extension; manual §8 pending |
| **`unified-interval-projection`** | Phases 1–5 code shipped; native PASS; **5.5 HITL deferred** (DEC-017); Phase **6.1–6.2** synced with geometry (2026-08-04) |
| `set-revision-persistence` | Set revisions, overlay browser — core shipped; loop picker **4.8–4.10** paused |
| `workspace-session-persistence` | Current workspace session model |
| `load-save-overlay-display-regression` | Overlay display fixes |
| `save-status-display` | Deferred save status on display |
| `long-loop-piano-roll-window` | 16-bar piano-roll window + overview; future `Timeline` projection consumer |
| `note-edit-tick-coordinates-and-audition` | Geometry wrap regression: tick spaces + session-store playback preview |
| `linear-loop-tick-storage` | Canonical linear storage + projection alias — prerequisite for UIP |

## Archived OpenSpec (normative specs in `openspec/specs/`)

| Change | Archived | Normative spec |
|--------|----------|----------------|
| **`loop-content-history-persistence`** | 2026-08-14 | [`loop-content-history/`](../../openspec/specs/loop-content-history/) |
| **`note-edit-current-state`** | 2026-08-08 | [`note-edit-current-state/`](../../openspec/specs/note-edit-current-state/) — [`PHASE8_CLOSEOUT`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md) |
| **`edit-session-action-geometry`** | 2026-08-05 | [`edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/) |
| **`deferred-job-scheduler`** | 2026-07-19 | [`deferred-job-scheduler/`](../../openspec/specs/deferred-job-scheduler/) |

**Side fix (2026-07-03):** `note-edit-tick-coordinates-and-audition` — long-loop pitch edit regression at loop wrap; handoff [note_edit_geometry_wrap_regression_bugfix.md](../Plans/note_edit_geometry_wrap_regression_bugfix.md). HITL pending.

**Archived (2026-07-03):** `note-edit-fader-feedback-regression` → `openspec/changes/archive/2026-07-03-note-edit-fader-feedback-regression/`; normative spec `openspec/specs/note-edit-fader-feedback/`. Phase 12–13 + RC11 §8.3–8.4 deferred.

**Archived (2026-07-03):** `note-edit-stable-note-id` → `openspec/changes/archive/2026-07-03-note-edit-stable-note-id/`; normative spec `openspec/specs/note-edit-stable-note-id/`.

**Handoffs (shipped):** [note_edit_fader_feedback_next_steps_handoff.md](../Plans/archive/handoff/note_edit_fader_feedback_next_steps_handoff.md), [note_edit_stable_note_id_phase_a_handoff.md](../Plans/archive/handoff/note_edit_stable_note_id_phase_a_handoff.md)

Parked (not active): `currentset-savedset-storage-layout`

## Current constraints

- Record/overdub stop: no full validate; deferred SD save only
- Default build env: `teensy41-capture-serial`; upload only after user confirms
- Base module (encoder + 4 GPIO) capable in principle; DROID is extension only
- Governance docs do not change firmware by themselves
- Do not implement from [ROADMAP.md](ROADMAP.md) — scope is [CURRENT_WORK.md](CURRENT_WORK.md) only
- **`edit-session-action-geometry`** — **archived** 2026-08-05; normative `openspec/specs/edit-session-action-geometry/`
- **`loop-content-history-persistence`** — **archived** 2026-08-14; normative `openspec/specs/loop-content-history/`

## Current blockers

- `currentset-savedset-storage-layout` parked until revision model stable — see CURRENT_WORK

## Accepted decisions (summary)

Full log: [DECISION_LOG.md](../DECISION_LOG.md).

- Authority: `docs/Authority/` → OpenSpec → Guides → plans → code
- Gesture-first; five gestures per button
- passes[] per slot (v4+); NoteEditSession for live edit RAM
- Current auto-save; Save appends Set revision without clearing Current
- Overlay confirm: Edit short + encoder short only (DEC-001)
- Persistence RAM model: `StorageSession` on `StorageManager` — request/held/dispatched revision load (DEC-012, **shipped**)

## Current architecture notes

- Persistence owner: `StorageManager` — no parallel save Manager without reassessment (DEC-008)
- **`StorageSession`** (DEC-012): job RAM in `Internal.cpp`; overlay in `Overlay.cpp`; FSM in `WorkspaceSave.cpp`, `RevisionCommit.cpp`, `RevisionLoad.cpp`; orchestrator in `StorageManager.cpp`
- HITL serial line buffer in RAM (FLASHMEM `processHitlSerialCommands` cannot access DMAMEM on IMXRT1062)
- **Capture-serial RAM budget (2026-07-04):** note-edit overlap code in FLASHMEM (`NoteEditMem.h`); deferred `#DBG`/`REVT`/`PERF` via 96 KB PSRAM ring in `DebugSessionCapture` — see [capture_serial_ram1_recovery_extmem_debug_enhancement.md](../Plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md)
- Normative specs: `openspec/specs/revision-load/`, `storage-session-jobs/`, `storage-session-layout/`
- **Next persistence layout:** `transport.bin` / `global.bin` — not started (post DEC-012)
- **Interval projection:** `IntervalProjection` engine owns all wrap math — **`include/Utils/IntervalProjection.h`** + **`src/Utils/IntervalProjection.cpp`**; Phases 1–5.1–5.4 + 5.6 shipped (grep gate on D7 consumers); `Track.projectionCycleStartTick` + queued start at grid; `PlaybackCursor` retired
- `ButtonManager` / GPIO dormant (DEC-005)
- M8 edit + pool-budget archived to `openspec/specs/`
- **Runtime scheduling:** contract [`runtime_scheduling_admission_model_architecture.md`](../Plans/runtime_scheduling_admission_model_architecture.md); roadmap [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md). Interval reservation is **not** authorized. Persist `admit*` and `DeferredJobScheduler` (DEC-027) remain separate.
- **Active linkage:** [`CURRENT_WORK.md`](CURRENT_WORK.md) overdub-lag stages `P1`–`P4` feed the roadmap as pre-contract owner-boundary work (causality + arbitration ordering), not as interval-reservation implementation.


2026-09-16 task-2.3 integration checkpoint: production canonical stop preparation
now uses a separately reserved output Capture instead of mutating raw capture
chunks. Explicit-source transfer/preview APIs and full-pool admission checks are
covered by native tests. The service-paused Button A/manager/Track reentry tests
remain red: FIFO, live/head staging, preview/session/undo/snapshot ownership,
causal sources and generic-reset lifetime isolation are not implemented. Keep
2.3 and HITL open; see the active change's CONTINUOUS-CAPTURE-OWNERSHIP.md for
bounds, evidence and limitations. No firmware upload or device action occurred.
