## 1. Decision and baseline gates

Implementation handoff for immediate overdub reentry while older jobs are pending:
[CONTINUOUS-CAPTURE-IMPLEMENTATION-PLAN.md](CONTINUOUS-CAPTURE-IMPLEMENTATION-PLAN.md).
This is planned work; no task or HITL gate is closed by the handoff.

- [x] 1.1 Complete `docs/Templates/DECISION_REVIEW.md`, the OpenSpec preflight, and `docs/Templates/OWNERSHIP_TRANSFER.md` for moving derived playback-view ownership and audible-source admission from `Loop` direct order storage to `LoopPlaybackRuntime`; add and validate the `playback-runtime-prewarm` delta; verify the reassessment, canonical-commit compatibility, migration, removal trigger, user approval, and required decision-log entry are resolved before firmware edits.
- [x] 1.2 Capture an overflow-free active-transport baseline that separates each `playbackOrderDirty` cause and every `load_frame` child; map the `appendCaptureEvent` → `sealCapture` → `commitPendingCapturePass` and `maybeCommitOverdubWrap` state transitions. Replace direct `OVERDUBBING → PLAYING` window detection with `OVERDUBBING → OVERDUB_STOPPING → PLAYING|STOPPED` segments plus subsequent PLAYING when present. Verify each active segment can be isolated and prove that the existing in-place incremental seal helper cannot be used as an immutable successor bridge.
- [x] 1.3 Add the affected `ExecutionBounds.md` rows and a capture-gate note for order-build and load-frame grains; add a per-owner long-loop `CoverageServiceContract` row and a `StopAdmissionContract` row that name verified phase-advance/denied-turn or stop-finalization bounds, required grains, lead, and fail-safe action. Verify each row names its owner, interval entry point, declared quantum, continuation, overrun policy, and bound class without assigning a policy ceiling from observed cadence.

## 2. Resumable playback-order successor

**2026-09-17 successor reentry unguarding — split out and re-scoped (preflight).** The
final slice of 2.3 opens as its own change,
`successor-capture-reentry-unguarding`, which removes the three admission guards in
the order Track → TrackManager → Button A. Its preflight resolved the public
acceptance list for task 2.3:

- **Made green by that change:** `test_track_reentry_does_not_depend_on_manager_guard`
  and `test_manager_reentry_with_a_held_same_pitch_note`.
- **Re-scoped, renamed and made green by that change:**
  `test_button_a_three_intervals_accept_notes_with_service_paused` →
  `test_button_a_successor_stop_denied_until_deferred_role_available`. The original
  test pressed Button A three times with no worker turns and asserted C captures
  "while A and B remain queued", which needs **two simultaneously frozen jobs** — more
  than the two `CaptureAdmissionStaging` roles and one `captureStopOwner` support.
- **Deferred to a follow-up capacity change:** the three-deep (N-job deferred
  queueing) scenario itself, with its additional admission roles, per-job drain order
  and PSRAM admission measurement. It is a documented follow-up, not a skipped red
  test.

Declared capacity for the unguarding change is **directional**: a successor *start*
requires the live role to be free, a successor *stop* requires the deferred/frozen
role to be free. Depth 2 in capture lifecycle (one finalizing predecessor plus one
recording successor), **not** depth 2 in deferred-stop queueing. Recording the split
here so task 2.3's checkbox closes on two public reentry cases plus device evidence,
with the third named as deferred rather than silently dropped.

**2026-09-16 causal predecessor/source identity — increment B, superseded rule
(partial 2.3):** the user approved option (A). The implemented shape is narrower than
the audit's first sketch because the implementation falsified its premise: the
canonical pipeline binds its owner identity *by address* to the live Loop scalars and
its ordering phases need one coherent live epoch, so preparation must keep resolving
live (measurement confirmed the live revision and root are unchanged during a normal
finalization). The recorded `causalBase` is therefore the **supersession reference**:
`causalBaseCurrent` is checked at the start of `CompanionBridge` (before any bridge is
built) and at the start of `AtomicTransfer`, and when it is false the interval retires
its ranked credit (`retireSupersededPreparation()`) and commits through
`TrackPreparedStopCommit::commitSupersededCausalBase` — the staged accepted capture
pass is committed, no bridge is selected or published
(`causal_base_superseded`), and a prepared protected undo entry is published in the
same turn. The unpublished horizon returns to `Prewarmed` through the new
allocation-free `Loop::restoreSupersededPreparedCompanionHistory(owner)` (the recycle
receipt guard cannot admit it by design), with discard-and-reprewarm as the fallback.
New assertions in `test_deferred_stop_records_and_pins_its_causal_predecessor_source`
cover the whole rule. Recorded deviation: the superseded route publishes the capture
pass only, relying on the newer root to already carry the companion rows. Full native
2,191 cases — 2,186 passed, 1 skipped, the same three intentional public-reentry
failures; capture-serial builds with 6,752 bytes RAM1 headroom. Remaining in 2.3:
successor-capture unguarding in a separate change (the three reentry cases).

**2026-09-16 causal predecessor/source identity — increment A (partial 2.3):** the audit
found a deferred stop re-derives its predecessor source live at every consumer turn
(admission's `baseSource` is used for credit sizing and discarded; the worker recomputes
`baseSource`/`targetSource`/`frozenSource`/`currentSource`; the atomic commit recomputes
`base`), so a successor's publication could change what a predecessor resolves against.
`CaptureStopJob` now records `causalBase`, pins its root with `causalBaseSnapshot`, and
sets `causalBaseRecorded`; admission resolves and pins it before the raw-range ownership
transfer and denies `causal_base_unavailable` when a non-null published identity cannot
be pinned, and retirement clears all three. Increment A records only — the consumers
still read live state — so it changes no behavior; increment B switches them over and
decides the epoch-qualified publication rule (including STOPPED recovery). New native
`test_deferred_stop_records_and_pins_its_causal_predecessor_source`; full native 2,191
cases — 2,186 passed, 1 skipped, the same three intentional public-reentry failures;
capture-serial builds with 6,752 bytes RAM1 headroom.

**2026-09-16 prepared companion history interval qualification (partial 2.3):** the
Loop-owned prepared companion history is now qualified by the admitted interval that
owns it. The identity is `PendingStopAdmission::intervalToken()` (frozenGeneration +
stopTick), a value rather than an object address, so a copy of an admission made for
credit probing or retargeting still identifies the same interval while a reused ring
descriptor gets a fresh token; zero means the live session path.
`preparedCompanionHistoryOwnedBy()` treats Cold/Prewarmed as receipt-free and
owner-agnostic, and every in-flight consumer (`step`, `ready`, `ids`, `history`,
`captureVerified`, `canStep`, `matches`, `commit*`,
`begin/stepRecyclePreparedCompanionHistory`, `discard`,
`beginCollectOverdubStopUndoPasses`) requires the token, which `begin` records and
recycle completion/discard clear. The deferred stop worker, the stop-commit helpers
and the undo-collection receipt pass `stop.intervalToken()`; defaults keep the live
path source-compatible. New native
`test_prepared_companion_history_is_job_qualified`; the two Track-level fixtures in
`test_playback_prewarm` now begin the preparation with the admission that consumes
it, and their stale unqualified consumers were qualified (the qualification is what
makes those calls correctly answer "not mine"). Full native 2,190 cases — 2,185
passed, 1 skipped, the same three intentional public-reentry failures;
capture-serial builds with 6,752 bytes RAM1 headroom. Remaining in the
session/preview/undo/snapshot receipt slice: nothing (closed); remaining in 2.3:
causal predecessor/source identity per job, then successor-capture unguarding in a
separate change.

**2026-09-16 session-exit handoff receipt ring (partial 2.3):** the session-exit
`SessionExitSnapshotHandoff` receipt is now a Loop-owned FIFO ring of
`kSessionExitReceiptCapacity = 4` (external-memory-first, cold-prewarmed by
`prewarmSessionExitSnapshotHandoff()`), replacing the single-slot unique pointer.
`canHandoffSessionExitSnapshot()` is a **free-slot** gate; "one receipt per open
session" moved inside `handoffSessionExitSnapshot()` behind `sessionExitHandedOff_`
(set by the handoff, cleared by `openOverdubSession`). A successor session is no
longer serialized behind an unconsumed publisher turn, while a repeat handoff for
the same session is still refused. The idle publisher consumes the oldest receipt
one per turn (retaining it when construction is disallowed), and playback's
stale-root overlay folds every pending receipt rather than one. Tests: new
`test_session_exit_receipt_ring_is_fifo_and_not_serialized`; the handoff-contract
assertions in `test_playback_prewarm` and `test_loop_take_survival` were updated to
the ring contract. `test_playback_prewarm` 165/165; full native 2,189 cases —
2,184 passed, 1 skipped, the same three intentional public-reentry failures;
capture-serial builds with 6,752 bytes RAM1 headroom. No new boot allocation, so the
increment A/B device memory check (build `91e6b38c…`) still covers this slice. Still
open in 2.3: causal predecessor/source identity per job, prepared-companion history
qualification, then successor-capture unguarding in a separate change.

**2026-09-16 deferred protected undo publication wired (increment B):** the normal
deferred overdub stop now records an `OverdubPassAdded` entry. `TrackUndoPublication`
is externally allocated and cold-prewarmed by `prewarmProtectedUndo()` together with
the undo stack's own entry capacity (`Config::ABSOLUTE_MAX_UNDO_ENTRIES`, 512) —
that capacity is a publication precondition because `begin` rejects a transaction
whose cursor reached capacity, and its device PSRAM admission must be measured.
`stepDeferredUndoPublication` runs `begin` + bounded `stepPrepare` inside the
existing `UndoCollection` phase (no new phase). The atomic transfer proves
`canPublish(...)` and only then calls `advanceAtomicTransferWithUndo`, else the plain
commit with the transaction restored. `stepProtectedUndoDrain` runs one bounded
quantum per DeferredCleanup turn (`stepReleaseRedo` → `beginTrimAfterReclaim`/
`stepTrim` → `finish`, plus a `beginRestore`/`stepRestore` + `recoverPreparedEntry`
path), so `protectedState()` is Idle before the job retires. Known deviations: the
DEC-063 disabled-pass reclaim continuation is still unwired (trim pressure measured
without it), and STOPPED/empty routes publish no protected entry because they skip
companion preparation. Tests now assert the transaction returns to Idle and exactly
one `OverdubPassAdded` entry is published; `test_continuous_capture` 30 pass with the
same three reentry failures; full native 2,188 cases — 2,183 passed, one skipped, the
same three; capture-serial builds with 6,752 bytes RAM1 headroom.

**2026-09-16 deferred undo collection with a job-owned prepared entry (increment A):**
new `PendingStopPhase::UndoCollection` on the prepared PLAYING route (between
`CompanionHistory` and `CompanionBridge`, because the collection needs the current
prepared companion receipt); STOPPED/empty routes skip companion preparation and
never enter it, and `retireSkippedCompanionPreparation` retires the estimate. New
dedicated `remainingUndoCollectionGrains`/`undoCollectionQuantum` +
`consumeUndoCollectionGrain()`, included in `finalizationComplete()` and sized from
`Loop::requiredSessionStopUndoCollectionGrains(...)`, so a saturated estimate denies
admission rather than stalling the phase. `Track::stepDeferredUndoCollection` runs
the resumable collection in bounded grains and builds the entry via
`TrackUndo::prepareOverdubSessionEntry` from the **job's** slot and baseline; the
collection's private outputs are pre-reserved in `prewarmCaptureStopJobs` (the API
never grows them). `Loop::reclaimUnreferencedDisabledEditPasses` moved to flash to
hold 13 ITCM banks. New native
`test_deferred_stop_prepares_job_owned_undo_entry`; `test_continuous_capture` 30 pass
with the same three reentry failures; full native 2,188 cases — 2,183 passed, one
skipped, the same three; capture-serial builds with 6,752 bytes RAM1 headroom.
Increment B (next): externally allocate `TrackUndoPublication`, begin/stepPrepare it
from the prepared entry, flip the atomic transfer to `commitWithUndo` /
`commitStoppedWithUndo`, and add the bounded `ReleaseRedo → AwaitReclaim → Trim →
Complete` drain in DeferredCleanup.

**2026-09-16 deferred-stop receipt wiring — audit (decision required):** the tworemaining receipt items need a design decision before implementation. (1) The
normal deferred overdub stop publishes **no** `OverdubPassAdded` entry:
`pushOverdubPassAdded` is only reached via `Track::finalizeCommitSideEffects` from
the fault fallback `commitCaptureForStop`, while the deferred path calls
`TrackPreparedStopCommit::commit` with `undo = nullptr` (`commitWithUndo` /
`commitStoppedWithUndo` have no callers). So the job-owned undo baseline landed
above is correct preparation but currently unreachable in production. Wiring it
means running the prewarmed-but-unused resumable collection
(`begin/stepCollectOverdubSessionUndoPasses`, `requiredSessionUndoCollectionGrains`)
plus `prepareOverdubSessionEntry` with the job's baseline/slot and
`TrackUndoPublication::begin`/`stepPrepare`/`publishAtCommit` at the atomic commit —
new bounded phases, admission counters and contract rows before `AtomicTransfer`.
(2) The session-exit receipt **cannot** be job-owned: it is retained past the job
and consumed later by the idle publisher while playback reads it for the stale-root
overlay, so it must be a **Loop-owned multi-slot ring**; `canHandoffSessionExitSnapshot()`
should test a free slot rather than an unconsumed singleton. Open questions: ring
size, and what the stale-root overlay does with more than one pending receipt. See
CONTINUOUS-CAPTURE-OWNERSHIP.md ("Deferred-stop receipt wiring — audit and decision
required"). No code changed for these two items.

**2026-09-16 job-owned overdub-session undo baseline (partial 2.3):**
`CaptureStopJob` now owns `ImmutableLoopSnapshotRef beforePublished` /
`hasBeforePublished`. Admission moves the live `Track::overdubSessionBeforePublished_`
into the job and clears the live member, so a successor session's
`beginOverdubSession` records its own baseline instead of overwriting the one this
interval's `OverdubPassAdded` entry must record. `TrackUndo::pushOverdubPassAdded`
prefers the head job's owned baseline, then the live session's, then a fresh
capture; an unconsumed baseline is retired with the job. New native
`test_stop_job_owns_overdub_session_undo_baseline`; `test_continuous_capture` 29 pass
with the same three reentry failures; full native 2,187 cases — 2,182 passed, one
skipped, the same three; capture-serial builds with 6,752 bytes RAM1 headroom. Still
open in this slice: the session-exit snapshot handoff receipt and the
prepared-companion history.

**2026-09-16 job-owned capture preview (partial 2.3):** `CaptureStopJob` now owns
`CapturePreview preview`, transferred from the live session at admission by
constant work (`CapturePreview::swapWith`, per-vector swap, no copy/allocation) with
a job-identified read borrow. `Loop::capturePreviewView()` (out of line in flash, so
40+ display call sites do not inline the branch into ITCM) is the single readable
accessor; writers keep the `capturePreview` member. 45 read-only display/Edit sites
migrated. `TrackPreparedStopCommit::completeDeferredCleanup` now takes the job's
owned preview, so the deferred seal retains this interval's rows rather than a
successor's. Per-descriptor `reserve`/`prewarm` capacity calls were dropped — the
transfers are allocation-free regardless and they only added ~640 bytes of new ITCM
template code (14th bank). New native `test_stop_job_owns_capture_preview` and
`test_capture_preview_borrow_release_is_owner_identified`;
`test_continuous_capture` 28 pass with the same three reentry failures; full native
2,181 pass / 1 skipped / the same three; capture-serial builds with 6,752 bytes RAM1
headroom. Still open in this slice: the overdub session/undo before-snapshot, the
session-exit snapshot handoff receipt, and the prepared-companion history.

**2026-09-16 job-owned pending note changes (partial 2.3):** first piece of the
session/preview/undo receipt slice. `CaptureStopJob` now owns
`PendingNoteChangeVec pendingChanges`; admission swaps the live Add/Shorten/Hide
set into the job in constant work and leaves the live session a prewarmed empty
set, so a successor interval is independent and neither the swap nor the next
interval allocates. `Loop::pendingNoteChanges()` is the single readable accessor
returning the job's owned set while a job-identified borrow is bound (writers use
the live member directly), and the borrow is released with the job. The worker
resolves the canonical tail against `job.pendingChanges`. New native
`test_stop_job_owns_pending_note_changes` and
`test_pending_change_borrow_release_is_owner_identified`; the existing
shorten-and-complete stop test now exercises the job-owned path.
`test_continuous_capture` 26 pass with the same three reentry failures; full native
2,179 pass / 1 skipped / the same three; capture-serial builds with 6,752 bytes
RAM1 headroom. Still open in this slice: `capturePreview` ownership across the stop,
the overdub session/undo before-snapshot, the session-exit snapshot handoff receipt,
and the prepared-companion history.

**2026-09-16 sealed raw-range ownership wired (partial 2.3):** the approved
ownership transfer is implemented. `CaptureStopJob` owns
`LoopEventStore::CaptureRange rawRange`; `beginDeferredOverdubStop` proves
`canSealCaptureRange` **before any mutation**, seals the producer at punch-out and
binds a job-identified read borrow on `Loop`. A denied admission performs no seal
and no transfer; an empty capture owns no extent and skips the seal. `Loop`'s read
source exposes producer and finalizing sources separately; ordering is scratch-only
so the borrowed range is never mutated; closure NoteId resolves from the finalizing
extent first. `Track::retireJobCaptureSourceGrain` retires the owned extent in
grains, drops the borrow only while it still identifies that job, and re-arms the
descriptor's reserved destination. `beginCapture` preserves a cut's successor
directory via `resetCaptureExtentPreservingReservedStorage`. DisplayManager is
unchanged; the successor-capture guard is still in place deliberately. Native:
five new regressions (seal transfer + successor capture, read-source row parity and
sealed-extent immutability, closure identity from the sealed extent, owner-identified
borrow release, denial performs no transfer) plus three migrated ownership
assertions; `test_continuous_capture` 24 pass with the same three reentry failures;
full native 2,177 pass / 1 skipped / the same three; capture-serial builds with
6,752 bytes RAM1 headroom (new Loop/LoopEventStore/Track functions are FLASHMEM and
`Loop::beginCapture`/`discardCapture` moved to flash to hold 13 ITCM banks). Device
boot-prewarm **PASS** on build `41b30d18…`
(`captures/task23-sealed-range-prewarm/result.json`): PSRAM free 5,142,912 ->
3,856,320 B (160,824 B/slot, unchanged from the dual-staging build), internal heap
352,256 -> 311,296 B, pool chunks 512 -> 416 — the +4 chunks/slot are exactly the
four `CaptureRange::reserveTail()` destinations per track; all slots `ready=1`,
zero warnings, 416 free chunks. Remaining: per-job session/preview/undo/snapshot
receipts, causal predecessor/source identity, causal successor capture, and
unguarding the three public reentry cases.

**2026-09-16 FIFO/queue-head binding (partial 2.3):** the stop-descriptor ring is
now the explicit admission authority. `Track::captureStopJobTail()` is the
admission target (null when the ring is full) and `captureStopJobHead()` is the
worker's stable descriptor (null when empty); `beginDeferredOverdubStop` admits
into the tail and reports a distinct `queue_full` stop-stage outcome instead of
treating exhaustion as latency, and `processDeferredOverdubStopGrain` binds the
head once for the whole grain instead of re-resolving it mid-step. `!STOP_STATUS`
reports `queue=<depth>/<capacity>`. Behaviour is unchanged while one job is
admitted (tail == head); the binding makes a later multi-job admission
well-defined. New native `test_stop_ring_admits_at_tail_and_binds_worker_to_head`
and `test_full_stop_ring_denies_admission_and_preserves_accepted_capture`; full
native 2,173 pass / 1 skipped / the same three reentry failures;
capture-serial builds with 6,752 bytes RAM1 headroom.

**Sealed raw-range wiring — audit complete, ownership decision required.** The
audit found that `Loop::capture.store` is still read during the deferred stop by
the Loop display-window merge helpers (which also mutate it to sort it) and by
`Track::appendCaptureNoteOffAtPhase` for physical-closure note identity. Sealing
at punch-out without a Loop-internal read fallback therefore empties the
live-capture overlay for the whole finalization window and breaks closure
identity; `Loop::beginCapture` also frees a pre-reserved tail today. Recommendation
and rejected alternative are recorded in CONTINUOUS-CAPTURE-OWNERSHIP.md
("Sealed raw-range wiring — audit and decision required"). Because this moves
capture-interval ownership between `Loop` and the admitted Track job, the
`ARCHITECTURE_RULES.md` ownership-transfer protocol applies. **Status 2026-09-16:
approved and implemented — see the sealed raw-range ownership checkpoint above.**

**2026-09-16 dual capture-staging roles (partial 2.3):** the runtime's single
`RuntimePlaybackViewSet::captureStaging` is split into two independently
prewarmed roles in the same owner: a *live* ingress staging and a *frozen*
stop-job staging. Live ingress (`recordMidiEvents` admission, `noteOn`
reservation, the cross-wrap continuation, `startOverdubbing` reset) writes only
the live role; the one admitted stop job borrows the frozen role. Punch-out
freezes the live role and exchanges the two indexes in constant work
(`LoopPlaybackRuntime::exchangeCaptureStagingRoles`); job cleanup resets only its
own retired frozen role. Staging is no longer shared between live input and the
frozen stop, so a successor interval owns a physically separate ingress buffer.
No new owner/module, no canonical/persistence/SD change, no action-edge copy.
Cost is one extra 2,048-event staging buffer per fully-prewarmed runtime slot
(external-first), so device PSRAM admission still must be re-measured before the
reentry guards are removed. Native: `test_playback_prewarm` 164/164 and
`test_continuous_capture` 18 pass with the same three intentional public-reentry
failures; full native 2,171 pass / 1 skipped / the same three failures;
capture-serial builds with 6,752 bytes RAM1 local headroom. Device boot prewarm
admission **PASS** (`captures/task23-staging-prewarm/result.json`, build
`fb4ab3a9…`): PSRAM free 5,143,968 -> 3,859,680 B across the 8 boot slots
(160,536 B/slot, matching two stagings/slot, no internal-heap fallback), all slots
`ready=1` with 8 credits, no warnings, 46.1 % of the 8.38 MB pool still free. That
device run used a capture-only extra boot line (`DIAG,capture_prewarm,phase=…`,
SESSION_CAPTURE-gated, no behavior change). FIFO/queue-head binding, sealed
raw-range wiring, causal predecessor/source, per-job session/preview/undo/snapshot
receipts and the three reentry cases remain open. Task 2.3 stays unchecked.

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
The provisional 6,240-grain baseline remains unchanged. Task 2.3 and hardware
acceptance remain open.



**2026-09-16 repeated bridge refresh blocks the third overdub:**
`captures/session_20260916_000904.log` confirms another overdub starts at
61.028 s. Its stop at 69.606 s remains pending. The retained trace contains
23 completed bridges immediately followed by refresh, from 72.194397 through
75.511650 s, at intervals of 144,372–158,456 us. Compilation captures a live
ledger/cycle/phase; admission checks exact ledger equality and the retained
Off horizon. Normal emission advances these while compilation runs, and
`refreshFrozenCompanionBridge` resets and recompiles the whole bridge. Repeated
ready/refresh is therefore not bounded progress toward admission. The first
stop finishes; the second stop does not, so later Button A presses correctly
retain the pending transaction. Ring overflow prevents a complete timing verdict.

Required correction: separate stable canonical mapping/ordering from bounded
current-boundary closure reconciliation, preserving progress under continuous
NoteOn/Off emission. Do not remove ledger validation, pause playback, wait for
silence, or increase the quantum as a substitute for a progress proof. Add a
moving-ledger regression with multiple consecutive source changes and verify
original Offs exactly once before treating another upload as a fix. This is
diagnosis and an implementation requirement, not implemented or device-verified.
Task 2.3 remains open.



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
capacity from an occupied snapshot receipt. Device reentry remains unverified;
task 2.3 and HITL/deadline gates remain open.



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
acceptance. Manual device confirmation and task 2.3 remain open.



Production integration checkpoint (2026-09-15): owned selected/candidate source
bases now survive stop cleanup, the existing bridge stream feeds shared rolling
window fill/order, and ordinary PLAYING consumes those cycle-qualified views.
The prepared bridge cursor preserves continuity during late refill. Current
serviced-phase admission refreshes stale ledger preparation; second-source
compilation retains original pending Offs. Full native validation reports 2,123 passed and one skipped; capture firmware
build passes with 6,176 bytes RAM1 local headroom;
manual two-overdub and deadline/reservation validation remain open. This is not
completion of task 2.3 or permission to remove compatibility under 5.4.


Earlier checkpoint, superseded by the integration above — initial-window closure foundation (2026-09-15): the shared bridge compiler can
project a completed closure context into an initial partial window, then retain
its Off through successor windows. This is native-tested preparation infrastructure,
not a completed firmware fix. Runtime initial-context production, current-phase
selection and successor servicing are still absent; `partial_open_ledger` remains
in place and task 2.3 remains open.


Manual reentry correction (2026-09-15): companion-history setup now binds current
canonical history after checking prewarmed capacity, allowing metadata appended
by wrap publication before setup. In-flight source guards remain unchanged.
The regression fails before the fix and passes afterward, including a second
history preparation after recycle (148 native tests pass); firmware build passes
with 6,176 bytes RAM1 headroom. Manual DROID confirmation remains pending; this
does not close task 2.3 or the HITL gates.


Latest cycle checkpoint (2026-09-15): preparation/publication anchors now use
the running runtime cycle; ordinary/overdub wraps advance it without a selected
bridge. The native held-closure fixture covers cycle 3 as well as cycle 0
(147 prewarm tests pass). Uploaded two eight-second late-note overdubs both
complete normal cleanup; host tests pass (27), RAM1 headroom is 6,176 bytes.
The frozen stop-phase anchor remains to be replaced through current-phase/rolling
integration. Task 2.3 and deadline gates remain open. See
[cycle evidence](FOCUSED-STOP-HITL.md#running-cycle-anchors-2026-09-15).

Latest joint-publication checkpoint (2026-09-15): removed the populated PLAYING
canonical-only fallback after joint-preflight denial. The exposed Add-only
target mismatch is fixed: reserved preparation derives +1/+2 from completed
companion rows, matching canonical publication. Native production-dispatch and
Add-only regressions pass (147 prewarm tests). Uploaded two-populated-overdub
HITL completes both normal cleanups without atomic-preflight denials. Earlier
cleanup-only passes did not prove joint publication. Task 2.3 remains open for
current-phase/rolling handoff, protected undo and bounded cleanup; 4.1/5.3 remain
open. See [joint-publication evidence](FOCUSED-STOP-HITL.md#joint-publication-and-add-only-revision-2026-09-15).

Latest second-stop checkpoint (2026-09-15): bridge publication now uses the
existing lifecycle to promote its ready canonical base to current. Cleanup
retires the old window receipt without retiring that audible base. This resolves
the second-stop CanonicalPrefix/window denial: the strengthened two-populated-pass
HITL verifies both normal cleanups before transport Stop. Native playback-prewarm
and take-survival: 147 + 124 passed; host probe/transition: 27 passed. RAM1 headroom
is 6,176 bytes after placing the infrequent handoff in flash. Task 2.3 remains
open for its wider handoff/undo/cleanup obligations; no deadline gate closes.
See [passing checkpoint](FOCUSED-STOP-HITL.md#second-stop-ready-window-ownership-2026-09-15).

Earlier cleanup checkpoint (2026-09-15): normal PLAYING cleanup now preserves
the selected bridge, cursor and ledger, and retires both completed bridge and
canonical-tail workers. Production-helper native regressions pass (147 cases);
host probe/transition tests pass (27). Capture firmware was uploaded. The
stricter two-pass device probe observes only one completed normal cleanup:
the second remains in CanonicalPrefix during observation and completes after
transport Stop. Visible PLAYING twice is insufficient; the probe now requires
every normal cleanup. See [cleanup evidence](FOCUSED-STOP-HITL.md#normal-stop-cleanup-continuation-2026-09-15).
Task 2.3 stays open; next isolate prefix/window owner readiness on the second pass.

Latest Button A checkpoint (2026-09-15): the live DROID investigation found an
empty frozen tail stuck at atomic transfer with one unused canonical setup
credit. Skipped companion work now retires its credits after closure/order
completion, allowing both normal and transport-retargeted empty stops to finish.
Uploaded firmware passes two empty overdubs and two eight-second wrapped
overdubs on track 5 / slot 1. Final status is STOPPED with no pending stop.
Native playback-prewarm regressions: 147 passed; host probe/transition tests:
26 passed. See [focused evidence](FOCUSED-STOP-HITL.md#droid-button-a-empty-tail-deadlock-2026-09-15).

The immediate PLAYING state edge is distinct from canonical/audible commit.
The 17/20 µs state measurements do not prove emission deadlines or DROID feedback
latency. Task 2.3 remains open for the full atomic handoff/cleanup requirements;
task 4.1 must retain service/emission correlation through background finalization;
5.3, the real-time reservation for 6,240 grains, and 5.4 remain open. Restored-slot
prewarm and the wider implementation/matrix tasks retain their existing gates.

Implementation note (2026-09-15): successor bridge preparation now carries
canonical row ranks, cycle masks, and owned held-note closures across contiguous
coverage windows. The closure is emitted once at its due phase and cannot repeat
in steady cycles. This remains an implementation checkpoint only: production
Track dispatch, rolling refill, and HITL are still open.

2026-09-15 update: the missing reserved session-exit identity handoff remains
repaired; the forced four-bar wrap fixture now reaches `OVERDUB_STOPPING` and
passes transport-Stop silence after the output-barrier repair. Task 2.3 remains
open for its protected cleanup/undo, bounded publication, bridge, and full
admission/parity requirements. Full timing and layered gates remain open. See
`FOCUSED-STOP-HITL.md` for the distinct verdicts and evidence.

- [x] 2.1 Add fixed, external-memory-first playback views containing events, order, source/projection identity, cycle-qualified `EmissionCursor`, cursor anchor, ledger-boundary summary, and explicit long-loop coverage. Prewarm three view buffers (current, ready successor, successor-after-next build), two independent bridge buffers (selected/candidate) sized across the current/ready horizon, generation-matched capture-source admission staging, and `CaptureNoteReservation`/stop-finalization credits. Verify firmware-size/RAM1 budgets under simultaneous selected bridge, candidate bridge, source-change successor build, and accepted open-note reservations; published-view immutability during a paused build; coverage wrap capacity; ledger-summary equivalence; and no allocation on an active playback tick.
- [x] 2.2 Implement phase/index fill and bottom-up merge ordering as independently bounded, resumable grains, including canonical equal-phase Off-before-On ordering. At every handoff, retain an incomplete successor as pending and keep the retired current view idle; only after that successor becomes ready may the retired buffer begin its successor-after-next build, without overwriting current, ready, or pending coverage. Add both unchanged-source and source-change coverage-service continuations, with the latter proving the new-source handoff and each following refill from the latest permitted predecessor completion point. Verify each contract against maximum phase advance and denied turns; if either cannot be proven, reject active windowed selection or the source mutation and exercise the full-loop/STOPPED-only fallback. Verify native parity against empty, long, wrapped, equal-tick, non-note, repeated-`NoteId`, denied-refill, three-buffer rotation, source-change preemption near handoff, source preparation completing midway through each of two successive coverage windows, and coverage-fault fixtures.
- [ ] 2.3 Implement continuous queued capture in reviewable slices: (a) add a failing reproducer through Button A, TrackManager, Track and MIDI input for service-paused A/B/C reentry; (b) choose and implement immutable range ownership using stable end offsets or pre-reserved tail rotation, including partial-chunk and release proofs; (c) add a fixed-capacity live-plus-queued interval ring with distinct job-owned ranges, reservations, stop/undo/snapshot receipts and explicit exhaustion behavior; (d) parameterize the existing worker around the FIFO head and prove old-job cleanup cannot clear live pending notes, preview, state or storage; (e) preserve generation-matched NoteOff/wrap/bridge/stop reservations and copy-on-write canonical preparation; (f) bind successor jobs to causal predecessor/source identity and epoch-qualified publication, including STOPPED recovery; (g) add selected-bridge/cursor immutability and same-pitch held-note coverage. On accepted NoteOn, reserve its matching NoteOff, wrap continuation/synthetic closure, staging, bridge, and stop-finalization credits before capture mutation; a matching Off consumes that reservation and cannot be rejected under ordinary pressure. Reject an unreservable NoteOn before capture, pending-note, preview or ledger state; its later unmatched Off bypasses capture mutation. Do not queue commands, clone Loop/Track, add a second worker graph, sort/gather/detach unbounded storage at the action edge, or silently drop accepted input. Verify phase-zero overdub-wrap admission, accepted-On/rejected-On pressure, accepted-On/matching-Off pressure, wrap-held closure, append-and-stop with no intervening grain, bounded deferred-stop latency, Transport Stop between finalization grains, stale pending builder, selected-bridge immutability, second-source candidate coalescing, capture preservation after fault stop, and promotion without replay, omission, or delay. Keep the checkbox open until all slices and public-route/device evidence pass.
- [ ] 2.4 Change active `playCommittedLoopMidi()` order handling from synchronous full rebuild to request/consume behavior while retaining the complete in-place builder as a STOPPED-state oracle; verify instrumented native tests fail if active transport invokes `std::sort`, vector growth, full gather, or full reanchor.
- [ ] 2.5 Differential-test the published/ready/build/bridge path against the complete oracle across record stop, phase-zero overdub-wrap admission, an append just before wrap, pre-seal candidate invalidation/rebuild, rejected-On/later-Off, accepted-On/matching-Off under staging pressure, wrap-held NoteOn closure, append-and-stop without an intervening grain, bounded deferred-stop completion, promotion before wrap, long-window handoff, maximum phase advance, denied refill, three-buffer rotation, source-change preemption near handoff, source preparation completing midway through each of two successive coverage windows, second-source bridge replacement, coverage fault, same-tick NoteOff/NoteOn, a source change touching an open note, layered slots, slot switch, and first/second revision/source changes. Verify byte-identical MIDI traces with no missing, duplicate, delayed, or reordered event for every accepted canonical commit, and an explicit deferred-stop trace for normal stop admission, except the specified fail-safe stop trace after an injected contract violation.

## 3. Resumable load/display-frame orchestration

- [x] 3.1 Add fixed continuation state to the `main.cpp` load/display owner that selects exactly one expensive active-transport child per eligible turn; verify display phases retain their continuation, admitted due display frames preempt load slices while load progress stays saved, denied work stays pending without restart, and no display phase/load job/prewarm/boot admission stacking occurs.
- [x] 3.2 Retain the existing resolve/draw/transfer display state machine and DMA polling, but make its one admitted phase an explicitly selected load/display child; verify a pending transfer coalesces newer paint requests and the incomplete frame is never published.
- [ ] 3.3 Keep `DeferredJobScheduler::runFrame(budgetUs)` as the existing load-job child, but expose its next phase and isolate its turn from other active-transport children; split Commit preparation and cleanup into continuations, measure the complete atomic Commit body, and deny active admission for any Commit without a content-independent bound. Verify focus-load behavior, demote-on-focus, bounded-Commit native coverage, and STOPPED-only fallback coverage.
- [ ] 3.4 Replace synchronous first-commit playback prewarm in a timing-critical load frame with bounded prewarm grains; verify a load Commit cannot expose an unprepared audible slot, prewarm resumes without an active-path allocation, and Commit cleanup cannot run hidden inside an active `runFrame` grain.
- [x] 3.5 Preserve post-overdub and `OVERDUB_STOPPING` MIDI polls and persistence non-stacking around the selected child; verify native `MidiServiceDrain` policy tests cover one-child turns, denial, deferred save behavior, and transport-Stop precedence over a pending stop continuation.

- [x] 3.6 Fix PLAYING display starvation: require actual pending restore work for active load admission/retention, prioritize admitted due display frames between load slices, and preserve STOPPED housekeeping. Validate sustained refresh with a retained load backlog, one-child turns, and load resumption in the native scheduler regression.

## 4. Attribution and capture gates

- [ ] 4.1 Extend capture-only telemetry with owner, grain kind, input quantum, capture/staging/frozen generation, NoteReservation allocation/closure/rejection, stop-admission state, command epoch, requested destination, and latency, effective/pending canonical source generation, cycle-qualified emission cursor, ledger-summary result, coverage interval/service-contract state including source-change horizon, bridge state, load-job phase, duration, and complete/yield/denied/discarded outcome. Separate immediate USB/serial playback deadline scoring from deferred DROID USB-host feedback, and retain bounded queue age/depth/overflow telemetry for the latter. Verify `test_runtime_timing_telemetry` and `test_capture_line_tier` retain parent `track_order`/`load_frame` compatibility and recognize every new line.
- [x] 4.2 Add a host-side timing report that scores RECORDING, PLAYING, OVERDUBBING, and `OVERDUB_STOPPING` intervals; segment `OVERDUBBING → OVERDUB_STOPPING → PLAYING|STOPPED` without relying on the old direct transition; reject `RING,overflow` inside every scored interval; and assert no extra MIDI service call sites. Verify fixtures reject mixed stopping/post-overdub windows and attribute each retained outlier to a single grain.

## 5. Verification and closeout

Latest evidence (2026-09-15): [track-specific emission correlation](FOCUSED-STOP-HITL.md#track-specific-emission-evidence-2026-09-15-0939).
Target-track NoteOn/NoteOff records now correlate with stop state edges and
service timestamps without trace loss. The dense run nevertheless reports
231 ms accumulated deadline lateness that was already present at stop entry.
Correlation is complete for this checkpoint; deadline safety and the reservation
are not accepted. Restored-slot prewarm integration remains open under 3.4.

Evidence update (2026-09-15): [fresh normal stop and correlated interruption](FOCUSED-STOP-HITL.md#correlated-transport-stop-rerun-2026-09-15-0922).
The focused interruption passes stop precedence/silence with zero trace drops;
state edges and grain-start service snapshots are retained on the device clock.
Those earlier short stop windows contained no note emissions, and the broad baseline
remains inconclusive. No task checkbox or real-time reservation is closed.

Evidence update (2026-09-14): see [focused stop timing](FOCUSED-STOP-HITL.md#timing-relative-to-midi-boundaries-2026-09-14).
The normal stopping sample is 9.291 ms (0.446 nominal MIDI clock intervals at
120 BPM); nearby wrap work is 31.690 ms and reported clock lateness is 25.774 ms.
These duration comparisons do not verify boundary alignment, per-grain slack,
or the 6,240-grain real-time reservation. Tasks 4.1 and 5.3 remain open for
complete deadline attribution and the broader transport-Stop matrix;
no task completion or acceptance threshold changes follow from this sample.

- [x] 5.1 Run the focused native suites for playback ordering, MIDI service drain, telemetry, capture tiers, slot load, and new continuation fixtures; verify `pio test -e native` passes.
- [x] 5.2 Build the capture-serial firmware and verify RAM1/firmware-size limits remain within the project gate.
- [ ] 5.3 Run overflow-free HITL record, overdub, wrap, canonical-versus-pending-audible admission, normal `OVERDUB_STOPPING`, transport Stop during `OVERDUB_STOPPING`, short/long loop coverage handoff, coverage-contract fault, source-change bridge, open-note bridge change, second source change, slot switch, bounded-Commit and excluded-Commit load, and post-overdub PLAYING scenarios with DMA display enabled. Score stopping and subsequent PLAYING separately; verify no NoteOn, NoteOff, or clock deadline lateness is attributable to an order-build or load-frame grain and the 4b.3 post-overdub MIDI-boundary criterion is met.
- [ ] 5.4 Remove the direct active-transport full order rebuild and compound load-frame compatibility paths only after the previous gates pass; verify STOPPED-only throughput remains separately reported and update `CURRENT_WORK.md`, `PROJECT_STATE.md`, the 4b.3 evidence, and the decision log before archive.

Evidence update (2026-09-15): [pre-stop emission burst attribution](FOCUSED-STOP-HITL.md#pre-stop-emission-burst-attribution-2026-09-15) identifies a 258-message target-track burst after snapshot publication, with 233.444 ms nested send time before stop. Rebuild telemetry now retains occurrence time and previous/current playback phases to distinguish source-admission catch-up from stop-grain blocking. Cursor cause and full prepared-source integration remain unverified; no acceptance gate is closed.

Device follow-up (2026-09-15 09:52): [fresh-origin catch-up confirmed](FOCUSED-STOP-HITL.md#device-confirmed-fresh-origin-catch-up-2026-09-15-0952): first snapshot reaches phase 375 with previous phase UINT32_MAX; nested send max is 144.102 ms. This pins the next 2.4/3.4 work to prepared-source admission plus initial ledger/cursor anchoring. Normal stop completes, but no in-stop notes were captured, so the emission checkpoint remains open.


User correction (2026-09-15): deferred first-record launch is rejected. Tasks 2.3–2.5 and 3.4 must integrate first-record input with rolling one-bar playback preparation, establish and retain bar 1 from recording start, and select it at the existing final-bar return boundary. Add one-bar late-input/no-intervening-grain, held-note, unknown-final-length, multi-bar refill, and snapshot-replacement parity fixtures. Task 4.1 must correlate record start, window readiness/generation, final-bar boundary, selection, and first emission. Task 5.3 must verify this on-device without delayed launch, global rewind, skipped notes, or a catch-up burst. The rejected proposal is historical only; no new first-record rejection policy or unmeasured reservation is accepted.

Implementation continuation (2026-09-15): first-record setup now installs an
empty raw bar-1 view through the shared playback lifecycle before RECORDING is
entered, and record stop binds its final loop length before the normal audible
handoff. The shared fill worker accepts a borrowed source descriptor for the
chunked canonical capture store. During recording, one bounded fill/order grain
per VisualCache owner turn refreshes the retained bar; capture-display
generation changes cancel and restart the unpublished build without mutating the
audible view. Cache reset preserves the retained raw view. Native focused
coverage/prewarm tests pass (132/132); both Teensy targets build. Deferred
main-loop maintenance is explicitly placed in FLASH so the capture-serial
image stays below the flexRAM bank boundary with 6,496 bytes of RAM1 local
headroom. HITL upload and tasks 2.3–2.5/3.3–3.4 acceptance remain open.

HITL update (2026-09-15): the memory-safe capture firmware was uploaded. The
track 5 / slot 1 transport-Stop interruption rerun passed focused stop
precedence and silence verification, while the broader host baseline remained
inconclusive by design because the injected Stop aborts overdub verification.
A clean normal-stop rerun remained inconclusive after missing the expected
overdub-stop transition confirmation. No HITL acceptance checkbox closes.


Implementation increment (2026-09-15): added `RuntimeRecordingWindow`, a private
prewarmed one-bar order with constant-work append/unlink, lazy-epoch reset,
stable tick/kind buckets, and immutable sealing. Native regressions cover a
last-tick Off arriving after Ons without a worker turn, retained bar 1 across
later-bar reuse, capacity/removal/reuse, and randomized parity against stable
tick/Off-before-On ordering. The focused playback-prewarm suite passes 122 tests.
This is not yet wired to Track capture or playback. Closure/capacity reservations,
canonical final geometry, source/ledger admission and coverage integration remain
required; task checkboxes and HITL gates remain unchanged.

Build validation for this increment: production and capture-serial both pass; capture RAM1 local allowance remains 6,496 bytes. The new component is not selected by production playback, so this build result does not measure its eventual runtime memory reservation or boundary cost. No firmware upload or HITL run was performed for this isolated ordering increment.


Recording-window continuation (2026-09-15): per-slot runtime now owns its private
return window. Playback-cache resets retain it; full slot reset retires it.
Same-window NoteOn admission reserves a physical terminal Off row before exposing
the On, preserving closure at full capacity. Handles bind owner and epoch, including
cold re-prewarm, and wrong/duplicate/stale closure attempts leave reservations intact.
Canonical short-pair removal consumes the reserved closure without emitting either
row. A window with unresolved local reservations cannot seal. These APIs are not yet
called by Track first-record input or selected as an audible source. Cross-bar/loop-end
closure transfer, geometry, full accepted-input capacity, canonical publication and
boundary activation remain open; no new recording rejection policy is introduced.

Validation of recording-window ownership/closures: 127 focused native tests pass, strict OpenSpec validation passes, and both final firmware builds pass. Capture RAM1 code is 425,816 bytes with a 6,496-byte local allowance. No upload/HITL was performed: Track first-record ingestion and audible selection still do not use this window. The verified first-record catch-up defect is not yet fixed.


Recording closure increment (2026-09-15): added reserved destination rows for
cross-bar terminal Offs and explicit final-loop-end closure into the bar-1
phase-zero bucket while retaining canonical raw ticks. Wrong/stale/foreign credits
and invalid geometry preserve both private windows; short-pair filtering consumes
both obligations. Focused native suite: 133 tests pass. Track first-record ingestion,
rolling obligation propagation, full accepted-input capacity, finalization work
bounds and atomic audible selection remain open. No checkbox or HITL gate closes.

Cross-bar/loop-end closure validation: all 133 focused native tests and both firmware builds pass; capture RAM1 local allowance remains 6,496 bytes. Strict OpenSpec validation and diff whitespace checks pass. No upload or HITL was performed; these private-window methods are not yet wired to first-record input or audible playback.


Prepared recording stream increment (2026-09-15): a borrowed sealed-window
stream now exercises the existing MIDI cursor advancement path. Differential
validation against RuntimePlaybackOrderBuilder covers a 514-event bar with
repeated ticks, stable Off/On/other ordering, a raw loop-end Off at phase zero,
and a mid-bar handoff after the serviced phase without historical replay. Both
layered and active committed emission policies match the canonical trace.
Retired/reused storage is rejected before emission; incompatible final geometry
is denied. The focused native suite passes all 135 tests.

Selection validates geometry and seeks in O(retained events + ticks per bar);
it belongs before activation, not inside the final boundary. Playback traversal
can inspect up to three buckets per bar tick across a window; a sparse seek can
spend that full bound in one call. No device timing reservation is established.
The adapter is not yet selected by Track. Full accepted-input capacity, capture
wiring, rolling readiness and atomic boundary activation remain open, as does
the observed first-record catch-up defect. No task checkbox or HITL gate closes.

Capacity integration finding: LoopEventStoreConfig provides 512 shared chunks
of 256 events (131,072 physical event rows before other owners/reserves). First
record append uses that pool rather than a fixed per-bar admission limit. The
private window and shared playback cursor currently use 16-bit row positions,
so merely increasing a proposed 2,048-event window reservation cannot establish
coverage of all previously accepted capture input. Resolve storage/index capacity
against pool admission before enabling this path; do not introduce silent input
rejection or late launch as a fallback.

Prepared-stream build validation: production and capture-serial both pass;
capture RAM1 local allowance remains 6,496 bytes. Strict OpenSpec validation and
whitespace checks pass. No firmware upload or HITL was performed for this
component; unused stream code does not establish its eventual active memory or
timing cost.


Continuation (2026-09-15): replaced the prepared stream's full retained-event
geometry scan with the append/closure invariant described in design.md. Final
length is bound per return-window epoch before filtering, and conflicting
closures preserve reservations. Added a 32-bit logical stream/cursor overload
of the shared MIDI walker; existing callers retain the narrow overload.
All 137 focused native tests pass, including a 131,072-event synthetic source
that crosses the 16-bit limit and recovers from invalid-entry denial without
replay. Physical recording-window capacity, capture wiring, sparse seek cost
and atomic activation remain unresolved; no task or HITL gate closes.

Validation of geometry binding and wide cursor: 137 playback-prewarm tests plus
154 tests across playback-cursor, MIDI-output and pending-note-change suites
pass (291 total). Both production and capture-serial builds pass. Capture RAM1
code is 425,880 bytes; the local allowance remains 6,496 bytes, with 104 bytes
of padding before the next ITCM bank. OpenSpec strict validation and whitespace
checks pass. No upload/HITL; physical recording storage and Track selection
remain unchanged, so the observed first-record timing defect is still open.


Physical recording storage continuation (2026-09-15): dedicated prewarmed rows
replace borrowed 16-bit playback-sort arrays. Row/bucket links, closure and
incoming credits, and the recording stream now use 32-bit positions. Actual
131,072-row storage traversal and full-capacity terminal closure pass, including
removal across the old index limit and an incoming credit above it. All 139
focused native tests pass. Cold allocation is overflow checked; no active
append/reuse growth is introduced. Device-wide shared memory admission, Track
input wiring, rolling readiness and atomic boundary activation remain open.
The physical index limit is resolved; this does not yet fix the first-record
catch-up defect or close any task/HITL gate.

Physical-row validation: both firmware builds pass; capture RAM1 code remains
425,880 bytes and local allowance 6,496 bytes. Strict OpenSpec validation and
whitespace checks pass. No firmware upload or HITL was performed.

Device reservation constraint: the existing capture
`captures/resumable-stop-rebuild-phase-device-hitl/session_20260915_095242.log`
reports `psram chip=8 MB pool=8185 KB`. Multiple full-pool event copies for return
and rolling windows cannot fit alongside the canonical pool and other runtime
owners. The wide-row fixture proves representable extent, not available device
capacity. Resolve shared canonical event references and their immutable lifetime
(or equivalent storage sharing) before enabling Track ingestion; do not turn this
memory shortfall into a new capture rejection policy or delayed launch.


User DRY correction: removed RuntimeRecordingWindow, its separate stream,
per-slot member, and prototype-only tests. Their historical validation notes
above are superseded, not shared-path acceptance evidence. Reuse the existing
RuntimePlaybackView/Fill/Build, order builder, lifecycle and bridges for recording.
The shared fill/build now supports explicit PlaybackCoverage selection, charging
all scanned rows to the quantum and denying unpublished candidate overflow.
All 122 focused native tests pass, including raw loop-end Off preservation,
modular coverage, a 65,538-row source, and overflow/retry. Existing AllSource
callers retain their default behavior. First-record source/lifecycle integration,
late input, closures, capacity and HITL remain open. No task checkbox closes.

DRY refactor validation: both firmware builds pass. Capture RAM1 code is 425,816
bytes; local allowance remains 6,496 bytes. Strict OpenSpec validation and
whitespace checks pass. No firmware upload/HITL. Recording-specific code is gone;
shared-path first-record integration and its behavior/timing proof remain open.


Shared lifecycle implementation: retained return coverage is a role in the
existing three-view lifecycle, not a separate store or stream. Pending and
already-complete successor paths preserve it while the other buffers rotate.
The return operation detaches other coverage without clearing event/ledger
arrays; owner preflight and in-flight worker discard remain required. Full
runtime reset invalidates retention. The shared runtime coverage-build wrapper
now uses PlaybackCoverage fills. All 125 focused tests pass, including ten-bar
successor rotation followed by return and a new successor, reset invalidation,
and filtering at the runtime owner. These are shared-owner methods; Track
recording/deferred-maintenance does not yet invoke them. Source identity, cache
refresh, late-input/closure handling and first-record activation remain open.

Shared builder cancellation correction: discard now detaches both borrowed fill
and ordering continuations and invalidates only unpublished candidate readiness.
Previously the fill worker retained its destination pointer, preventing a new
build after mid-fill cancellation. Regression covers cancellation during fill
and order, rebuilding into a different view, and preserving already published
coverage. All 126 focused native tests pass.

Final shared-owner validation: 126 focused native tests, both firmware builds,
strict OpenSpec validation and whitespace checks pass. No firmware upload/HITL.
Track recording input and first-record return activation remain unconnected;
this increment does not claim the observed first-record timing defect is fixed.


Raw preparation continuation: added RawCoverage to the shared fill worker and
RawPlaybackTick to the existing order builder. They exclude later raw ticks
without provisional modulo and preserve audible stable Off-before-On ordering.
Completed raw coverage binds final length without an event/order walk. Shared
lifecycle Preparation mode rotates unbound windows; default audible handoff and
retained-return activation reject them until binding. All 129 focused tests pass,
including eight-bar preparation rotation and parity against projected playback.
Track::startRecording now sets the recording origin before pre-roll ingestion.
Live capture feeding, terminal closure updates, capacity, cache/commit ownership
and record-stop activation remain open. No task or HITL gate closes.

Raw-preparation validation: 129 focused native tests, both firmware builds,
strict OpenSpec validation and whitespace checks pass. No firmware upload or
HITL. The recording-origin correction is implemented in Track, but raw window
feeding/activation is still not connected to live capture; the first-record
snapshot-delay/catch-up defect remains open.


Playback display scheduling fix (2026-09-15): permission to restore no longer
keeps an empty load child selected during PLAYING. Admitted due display frames
also preempt retained load slices without resetting StorageManager progress.
The regression models 1,000 owner turns with a permanent load backlog: 100
complete three-phase display frames and 700 separate load slices. All 161 tests
across `test_midi_service_drain`, `test_display_window_utils`, and
`test_load_loop_selection_policy` pass. The production `teensy41` build passes
using an isolated temporary build directory after the shared cache build failed
with missing object files. No upload or device verification was performed.
Task 5.3 remains open and must include sustained PLAYING refresh with empty and
deferred background-load queues, plus RECORDING/OVERDUBBING transitions and
MIDI timing checks. This fix does not close the other playback or Commit gates.

### 2026-09-15 stop liveness and display regression

device `!TRACE_FENCE` answered
while the interface appeared frozen: main-loop liveness was retained. A stopped-only
`!STOP_STATUS` query now exposes the selected track's stop and history worker phases.
`captures/normal-stop-status-20260915/stopped-status.log` proves a stall in
CompanionHistory, with canonical tail complete. Recording start prewarmed resources
before resetting their canonical generation; the reset now precedes prewarm, and
snapshot cleanup retains the reserved companion-ID destination. The next device
report (`captures/normal-stop-resources-fix-20260915/stopped-status.log`) proves
matching generations but history Trim parked with zero canonical estimates.
Preparation/recycle counters now saturate as estimates rather than denying further
valid worker turns. Completed workers retire unused estimates without padding turns.
The display owner reuses its sliced paint continuation during OVERDUB_STOPPING.
Native prewarm/edit suites passed 259 tests after resource fixes; final continuation
prewarm/survival suites passed 259 tests (edit suite also passed 124). Capture firmware
built/uploaded. The next device run advanced through history to CompanionBridge
(phase 8), with history Ready and bridge Idle; normal stop still fails. Evidence:
`captures/normal-stop-continuous-display-20260915/stopped-status.log`. The current
full-loop bridge requirement and transport-stop retarget need further investigation.
The user-observed rerun also failed normal-stop verification:
`captures/normal-stop-display-user-verify-20260915/result.json`. Transport Stop was
sent for cleanup; completion was not established. Screen responsiveness still needs
the user observation. No task 2.3, timing reservation, or HITL acceptance gate closes.

### 2026-09-15 first-record frame gaps

**2026-09-15 first-record display regression:** user confirmed the screen now
continues updating in OVERDUB_STOPPING, but reported multi-second frame gaps during
the first two recording bars. A recording-only mode in the existing stop probe
isolates that interval from the stalled stop trace flood. Before the scheduler
fix, stopped background tracks rebuilt 56/353/1429-row display caches during the
recording owner turns: idle maintenance reached 281,346 us, a sampled display frame
538,356 us, and the window MIDI-service gap 281,883 us. The realtime dispatcher now
services realtime tracks only; pending stopped-track wrap work cannot block the
active display. The after-run sampled frames were 14,260–15,942 us, with no reported
long idle-maintenance span. Both captures are overflow-free. The after window still
has a 180,003 us MIDI-service maximum around recording entry; this is not deadline
acceptance. Evidence: `captures/record-only-background-fix-20260915/comparison.json`
and both linked session logs. Capture build/upload succeeded; six existing host
probe tests pass. The recording sequence completed and transport ended STOPPED.
CompanionBridge stop liveness remains open; no OpenSpec acceptance gate closes.
A stopped-only `!TRACE_DRAIN` diagnostic retrieves retained logs on explicit request.

### 2026-09-15 companion bridge and stopped recovery

**2026-09-15 companion-bridge checkpoint:** user confirmed first-record display
responsiveness after the background-cache dispatch correction. The remaining
normal-stop stall is now explicitly attributed on device:
`ODUB,stop,bridge_base,...,missing_full_loop_base`. A first recording retains a
one-bar raw playback view; the compatibility bridge mapper, selection preflight,
and hot-path consumer still require a full-loop view with canonical provenance.
Do not fix this by synchronously materializing a full-loop base or weakening
provenance/coverage checks. Task 2.3 must integrate coverage-aware canonical
successor preparation with the existing window lifecycle and task 2.4 consumer,
including current-phase cursor/ledger and subsequent-window handoffs.

Transport-Stop recovery from CompanionBridge is repaired: completed history skips
audible bridge work and calls the existing stopped canonical/companion commit
body, shared with its undo-aware entry point. Frozen-command/owner checks and
history cleanup remain enforced. Native stop/prewarm and take-survival suites:
259 tests passed, including no-published-base recovery and stale-command rejection.
Capture build/upload passed. Device `captures/bridge-stopped-recovery-20260915/`
shows OVERDUB_STOPPING -> STOPPED, retained correlated state/grain evidence, no
trace drops and no ring overflow. The normal-stop verifier still fails, correctly;
this is recovery evidence, not normal-stop, silence-matrix, or realtime acceptance.
The device was left STOPPED. No task checkbox closes.

### 2026-09-15 canonical window worker implementation

**2026-09-15 canonical window implementation (partial task 2.3):** the existing
RuntimePlaybackViewFill and RuntimePlaybackViewBuild now accept typed canonical
coverage-scoped sources, copy events and pass provenance together, and keep fill
and order on separate bounded turns. Whole-source length need not fit the view;
only retained rows consume destination capacity. The existing companion bridge
mapper now filters the prepared per-pass result to that same coverage and checks
the borrowed base's coverage, publication, storage and provenance receipt on every
turn. The bridge compiler accepts a matching partial coverage with an empty ledger;
open-note obligations remain denied until carried across subsequent window rotation.
No raw/projected source is silently accepted as canonical provenance.

Native verification covers nonwrapped, wrapped and empty windows, one-entry quanta,
source larger than destination, provenance surviving worker reuse, per-pass MIDI
parity, changed coverage and unsupported held-note closure rejection. Final focused
prewarm suite: 138 passed; edit suite: 124 passed. Capture firmware build passed.
This is shared-worker implementation, NOT completed Track integration: stop-owner
base-window preparation, current-phase admission, closure-carrying rotation and the
ordinary PLAYING consumer must be connected together before enabling normal stop.
The new pieces were not uploaded; the device retains the verified display fix and
transport-Stop recovery. Normal-stop device verification and task 2.3 stay open.

**2026-09-15 stop-owner canonical window preparation (partial task 2.3):**
CanonicalPrefix now prepares a one-bar canonical base when the current view has
no usable full canonical provenance. It merges the normalized prefix through the
existing RuntimeCompanionPasses worker, fills/orders a spare view through the shared
RuntimePlaybackViewBuild, and publishes an immutable ready view without changing
audible current. Fill, order and publication resume on separate turns. The pass
worker is released before frozen-tail processing; copied origins survive its reuse.
A busy shared builder stays pending, and source/storage changes cancel only the
owned preparation. Both initial-ready and post-record no-ready lifecycle shapes
are covered. No new event buffers or active allocations were introduced.

Native prewarm/take-survival: 265 passed. Capture build passes with 6,176 bytes of
RAM1 local headroom. This does not fix normal-stop liveness yet: the bridge selector
and PLAYING consumer still require the full-loop compatibility base. Current-phase
admission and held-note closure carry through subsequent window rotation remain
required before enabling the new ready base. No upload, HITL verdict or task closure.

**2026-09-15 ready-window bridge integration (partial task 2.3):** frozen
companion bridge preparation now borrows the exact prepared canonical ready
window, falling back to the existing full current base when needed. Mapping and
compilation no longer re-resolve through the audible-current index. Admission
checks that the preparation anchor lies inside this base coverage. Retirement,
provenance loss and coverage changes invalidate the borrowed base, including
completed candidates. Physical views now advance a storage generation on reuse;
mappers, bridge compilation, selection and stream cursors reject stale indexes
even when source identity and coverage repeat. The stopped diagnostic identifies
`prepared_window` separately from a missing canonical base.

The integrated native fixture covers prefix -> ready window -> frozen tail ->
bridge compilation -> the existing read-only MIDI cursor, with nonwrapped,
wrapped and empty coverage and equal-phase Off-before-On output. It verifies that
retiring the unrelated old current does not invalidate the ready base, while
retiring/reusing the borrowed base does. Audible Track selection remains guarded:
held-note closure carry, current-phase admission and following-window playback
are not complete, so this is not a normal-stop or HITL pass. No firmware upload.
Native prewarm/take-survival: 265 passed; capture firmware build passed with
6,176 bytes RAM1 local headroom. Strict OpenSpec validation and diff checks pass.

**2026-09-15 bridge-owned held-note closure state (partial task 2.3):** the
existing bridge compiler now stores its affected-open-note flags, closure time
and next-On phase in the bridge's fixed ledger-sized storage. When the existing
closure walk finds the original Off, it also copies that event into the bridge.
The copied closure survives compiler discard and base-window reuse. The old
base index remains only for the current compiled stream; it is not a transferable
closure receipt. No alternate pairing or closure-resolution algorithm was added.
Bridge reset retires closure metadata by count without clearing every payload;
compilation rejects a changed ledger count. Unresolved or incomplete receipts
are not exposed by the closure accessor.

Native prewarm/take-survival: 266 passed, including the unchanged cross-wrap MIDI
trace, all 128 ledger slots, base reuse, bridge reuse and changed-ledger rejection.
Capture build passes with 6,176 bytes RAM1 local headroom. Successor inheritance,
closures outside the initial window, current-phase selection and rolling playback
consumption remain required; the partial-window held-note guard stays in place.
This is ownership preparation, not completed cross-window playback or a normal-stop
fix. No upload or HITL acceptance; task 2.3 remains open.

**2026-09-15 successor closure integration (partial tasks 2.3–2.4):** the
existing RuntimePlaybackBridgeBuild now has a same-source successor entry point.
It checks contiguous coverage/cycle boundaries and a stable selected predecessor,
then inherits owned closures in bounded grains. A copied Off is inserted only in
the window containing its original due phase and only in its original cycle.
Steady insertion ordering compacts already-read indexes so closure-only Offs cannot
repeat in later loops; the existing MIDI cursor consumes the resulting streams.

Typed shared view fills now retain each row's complete canonical merge rank.
Compiled bridges retain canonical cycle masks across the complete rank horizon,
so later windows inherit gating decisions without reconstructing them from NoteIds.
Masks shift at cycle advance and survive windows containing no events. Replaced
old base rows cannot overwrite the effective canonical row's mask. Both bridge and
view buffer generations protect builders and cursors against reuse. All new slabs
are prewarmed; the existing 6,240-grain reference-case estimate remains unchanged.

Native prewarm/take-survival: 270 passed. Fixtures verify original Off emission,
no repeated closure in steady cycles, carry through two windows at quanta 1/32,
shortening the revised Off before the old closure, stale predecessor/source denial,
and canonical ranks surviving source-worker reuse. Capture firmware build passed:
RAM1 local headroom remains 6,176 bytes. This is shared compiler/cursor integration,
not completed production Track dispatch or a device normal-stop verdict.

Still required before enabling rolling Track selection: initial partial-window
closure context for already-open notes (its guard remains); coverage-qualified
cursor consumption across wrapped windows; atomic current-phase view/bridge handoff;
ordinary successor refill; and normal-stop cleanup without silence/reanchor.
No upload, task checkbox closure or HITL acceptance is claimed.

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


**2026-09-16 continuous-capture foundation:** Added the production-route native
reproducer (Button A, TrackManager, Track, MidiHandler input). The single-interval
control passes; queued A/B/C, manager-held-note and direct Track reentry currently
fail as expected. Added exclusive pre-reserved LoopEventStore range rotation and
bounded release with partial/full chunk, pool-exhaustion, stale receipt and stale
rewrite tests. Focused range/persistence/store/prewarm: 209 tests pass. No Track caller uses
the new cut yet; no reentry guard is removed. See
[ownership/migration checkpoint](CONTINUOUS-CAPTURE-OWNERSHIP.md).
Task 2.3 remains open for the ring, complete reservations, FIFO worker binding,
causal resolution, job-local cleanup, undo/snapshot receipts and device evidence.

Final foundation build: capture-serial passes, RAM1 locals 6,176 bytes. The bounded
rewrite worker is placed in flash to retain the existing RAM1 bank allocation.
No upload; device timing and the full queue integration remain open.


**2026-09-16 worker-owner integration:** Track stop admission now binds its
Loop/slot, original closure phase, geometry and interval; it owns stopped pending
notes separately from live input. Existing empty/paired/held-note worker flows
complete in native tests. Job-note retirement and Track-state completion are
isolated from newer ingress. The full FIFO and independent Loop/runtime session
owners remain unfinished; all three public reentry regressions still fail.
No guards removed and task 2.3 remains unchecked.

Integration slice checks: 266 affected tests pass, three reentry requirements
remain failing; capture-serial builds with 5,600 bytes RAM1 local headroom.
Strict validation and diff checks pass. No upload or device timing claim.


**2026-09-16 reserved canonical-output integration (partial 2.3):** production
stop now copies Q=32 ordered rows into independently reserved chunks, releases
one raw chunk per subsequent turn, and transfers through an explicit-source
Capture receipt. Full-pool transfer and oversize/empty admission preflight are
covered. Maximum 2,048-row copying, stale output/generation, filtered pairs and
transport Stop during partial copy have native regressions. Reentry guards remain:
the three public-route requirements still fail. FIFO and live/head staging,
causal source, preview/session/undo/snapshot and generic-reset lifetimes remain
open. See CONTINUOUS-CAPTURE-OWNERSHIP.md; no completion checkbox changes.


**2026-09-16 reset-owner checkpoint (partial 2.3):** an admitted job now pins
runtime staging/output through cleanup. Cache reset requests STOPPED recovery and
yields instead of discarding accepted input; Track invalidation defers its source
generation change. Native partial-copy and every-stop-phase reset fixtures pass.
Prewarm diagnostics support the next intermediate hardware memory/timing check,
needed before expanding FIFO reservations given the prior device PSRAM exhaustion.
This is not immediate-reentry acceptance; the same three requirements remain red.
See FOCUSED-STOP-HITL.md and CONTINUOUS-CAPTURE-OWNERSHIP.md. No checkbox closes.


**2026-09-16 focused hardware evidence:** uploaded build and authorized track 5 /
slot 1: two normal stop cleanups, focused interruption silence and three restart
content cycles pass. Dense attempt rebooted/disconnected and is inconclusive;
full memory/timing and FIFO/reentry gates remain open. Host pending-job verifier
updated with 24 passing tests. No completion checkbox changed; see FOCUSED-STOP-HITL.md.


**2026-09-16 overlap reboot follow-up:** user confirmed spontaneous reboot at
record-pass overlap resolution. New native overlap/Shorten stop case passes;
three reentry requirements still fail. Fixed crash-report consumption so future
startup diagnostics preserve the crash flag and SD report; build/upload pass.
Human overlap reproduction pending. No task checkbox closes.


**Automated dense rerun:** same recipe reproduced spontaneous reboot, now with
crash=1. No dense gate acceptance; fault details not retained on serial. See
FOCUSED-STOP-HITL.md. No manual reproduction is required for this automated test.

**2026-09-16 dense reboot fix verified:** matching fault report recovered into
crash-log-retrieval.log: stack-boundary DACCVIOL in _svfprintf_r. Overlap diagnostic
scratch moved to RAM2; local frame reduced 1,372 -> 76 bytes. Two dense automated
runs now complete without reboot; adjusted Stop-window run passes focused silence
(18.9 s observation). Broad baseline/content/timing checks remain inconclusive;
no FIFO/reentry/task-2.3 completion. Firmware hash and details are in active
FOCUSED-STOP-HITL.md. All serial/MIDI runners are closed.

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

**2026-09-16 latest checkpoint (14:29):** descriptor build a9a7217f uploaded on
retry; dense hardware test then lost USB during initial recording, before overdub.
No reboot marker or fault cause retained; serial port absent. Native full run has
2,159 pass / one skipped / three known reentry failures; final focused guard run
has 14 pass / the same three failures. Stored dense content verification on the
prior fixture passed its scoped input comparison; audible timing and restored
playback remain open. Device reconnection is required to continue hardware
investigation. No capture reader is running. See active FOCUSED-STOP-HITL.md.
