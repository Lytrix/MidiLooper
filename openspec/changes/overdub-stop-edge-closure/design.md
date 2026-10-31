## Context

See [proposal](proposal.md) and [behavior delta](specs/capture-state-guards/spec.md). Baseline is
`ebaa6ebe066569d612c853a8a1cfb76f39a36e4b` on `feature/unified-rolling-playback-path`, with the pre-existing
dirty tree retained. This session changes planning/runtime documentation only. Firmware, fixtures,
target build, upload and device acceptance are **NOT RUN**.

### Scope traced — G0 evidence

**VERIFIED current execution:** MIDI Record action calls `Track::stopOverdubbing`; a successful
`beginDeferredOverdubStopForSlot` freezes/swaps capture into an existing FIFO job and publishes Playing.
On capacity refusal, `latchAcceptedStop` retains identity/phase but leaves Overdubbing. Deferred
`serviceAcceptedInteractionTurn` waits for the FIFO to drain, then `driveAcceptedInteraction` retries
using the current clock. The latter does not use the stored phase for `job.phaseTick`.

**VERIFIED owners/lifetimes:** Track owns interval, latch and jobs; Loop owns producer capture/session;
the existing externally allocated `CaptureStopJobs` owns job previews, pending changes/notes, undo
baseline and stable custody receipts after handoff. Each runtime owns one canonical finalizer.
`CaptureStopJobs::kCapacity` is 8, custody capacity is 9, staging-role count is 10. These are existing
finite limits, not evidence that unlimited successive Starts can execute.

**VERIFIED publication/undo/persistence:** the worker performs reserved closure, canonical ordering,
prepared pass transfer, atomic publication and deferred cleanup; job-owned receipts prevent successor
reset from replacing the predecessor's pass. Session-exit identity and undo baseline move at binding.
Persistence's capture-active predicate includes stop obligations. No writer or undo-format change is proposed.

**VERIFIED alternate entry paths:** manager/direct overdub Start; manager slot switching;
`TrackManager::stopAllTracks` dispatch into `stopOverdubbingToStopped`; live NoteOn/NoteOff and
`recordMidiEvents`; orphan reservation settlement; deferred transfer-horizon replenishment;
display's live and job-owned preview composition. Track switching alone does not stop another track's take.

**VERIFIED timing/resources:** no second canonical working set is needed. The relevant classification is
[ExecutionBounds](../../../docs/Authority/Architecture/ExecutionBounds.md) rows 28–31: existing stop
admission/worker units and accepted-interaction scheduling. Classification must describe the new wait
path in the implementation commit, without promoting empirical timing to a guarantee. Stop remains
allocation-free; new continuation metadata, if needed, belongs in the existing external ring, never
by-value in the DTCM Track array. No working-set scaling with loop length is authorized.

**VERIFIED existing validation:** `test_shared_capture_finalization` includes real stop/worker/undo/storage
owners, controlled clock, canonical-output occupancy, bounded drains, stale identity, successor rotation
and queued-stop fixtures. Existing stale-latch fixtures intentionally expect a live producer after refusal;
retain their identity assertions while updating only the deliberately changed closure semantics.

**CONFLICT, explicitly amended:** DEC-067's keep-recording denial rule and DEC-078's boundary rederivation
and no stopped-interval retention are incompatible with immediate input closure. DEC-233 records the
owner-approved amendment; no canonical-finalizer cardinality change is authorized.

**ASSUMPTION:** the code defect can contribute to user-visible late closure. **UNKNOWN:** attribution of
the reported already-closed-note visual extension; latest cited capture instead has a correct Playing edge.
No device result establishes capacity-refusal behavior. No tempo, track-count or product-envelope expansion
is claimed: `ClockManager::setBpmFloat`, `Config` and `activeLoopIndex` remain the authorities.

**G0 verdict: PASS for the bounded stop-edge contract below**, not approval of the original capacity-only patch.

## Goals / Non-Goals

Goals are the behavior delta. Design-level non-goals: no second finalizer, generic queue framework,
new stored absolute-clock deadline, whole-loop copy, eager canonical allocation on the press,
normalization bypass, or changed overlap/ledger policy. Already-closed notes must not be rewritten as
open tails. The previously withdrawn blanket display-tail freeze is not reinstated.

## Decisions

### 1. Input closure and finalization are distinct transitions on the same owner

Keep Track as owner. A retained matching overdub Stop denotes an input-closed interval even before
job admission. Stamp the first phase and destination once. Move track state to Playing on the press
(Stopped if transport has stopped), without clearing producer capture, reservations, pending changes,
pending held notes, preview or session-exit claim. State-gated input must reject new rows, including
late physical releases and CC/input routing through `recordMidiEvents`.

Until job binding, retain the same interval identity. Binding advances the identity exactly once, as
the existing admitted route does. A closed-latch retry is explicitly eligible even when the track is
not Overdubbing; it bypasses only the already-completed input-state transition, never resource or
identity preflight. Do not bounce Playing back through a live Overdubbing state for admission.

The stored phase is the musical boundary, independent of later transport absolute-tick reset. Pass it
to job closure as `job.phaseTick`; keep current absolute ticks as scheduling/diagnostic values only.
Repeat Stop cannot restamp it. LoopId/mode/slot/interval mismatch explicitly supersedes the request
without touching replacement content.

### 2. Reuse the existing FIFO for closed intervals; serialize only the canonical worker

Physical cut admission still proves free descriptor, staging, custody/raw range, session-exit
destination, transfer reservation and finite work credits before freezing. These resources protect
content and must not be bypassed. A canonical output reservation is not needed to own the stopped
input: remove it from descriptor/cut admission and require it at **all** worker-binding sites.

In particular, the current eager `setCaptureStopOwner` in `beginDeferredOverdubStopForSlot` must not
bypass `bindCaptureStopWorker`'s canonical readiness check. Bind only a ready worker. The existing
deferred worker turn replenishes/reserves empty, unowned canonical output before binding; populated
output or another owner is left byte-for-byte intact and remains an observable wait.

Keep the same FIFO bound and staging/custody counts. If genuine physical cut capacity is exhausted,
the input-closed producer remains the owner; no successor may reset it. This does not permit a busy
canonical worker to masquerade as capture-capacity exhaustion.

### 3. Successor Start runs before predecessor completion, but never resets retained input

At the **beginning** of `Track::startOverdubbing`, before allocation/prewarm, staging reset or
session-exit claim, attempt the matching retained Stop's physical handoff. Manager and direct callers
must share this guard. Once the handoff succeeds, continue Start immediately using the now-clean live
role even while the predecessor is queued or finalizing. Do not wait for publication/cleanup.

For a true physical-capacity wait, retain the distinct successor intent alongside the Stop as bounded
continuation metadata in the existing external interaction record. It is not an additional interval
or canonical worker. Bind Stop first, then transfer that intent into the existing Start carrier using
the post-binding interval identity; consume only the Stop request. Allow the Start carrier's retry
while FIFO jobs remain, subject to the existing cheap Start preflight and wake conditions. A later
Stop/transport Stop explicitly supersedes an unexecuted Start. Repeated Start does not stack or
overwrite the Stop. Record entry must likewise not reset the retained predecessor before handoff.

`hasLiveOwnerOfReservedSessionExitObligation` and orphan settlement must count the matching closed
Stop as a live owner; state Playing is not evidence that its Reserved claim is orphaned. No orphan
cleanup may release it before its own handoff.

### 4. Keep recovery, switching and transport paths reachable

- `replenishLiveOverdubTransferHorizon` remains eligible for a matching closed retained overdub, not
  only `isOverdubbing()`. Keep the no-admitted-job relocation guard. Ordinary maintenance must restore
  a lapsed horizon after predecessors retire; no new Start or fixture-only reserve may be required.
- `finalizeCaptureAndSelectSlot` checks the slot-qualified unbound Stop before recording/overdubbing
  state branches. It may move `activeLoopIndex` after this interval binds, never merely because another
  slot has an admitted predecessor. Existing manager switch intent stays with its owner.
- Transport dispatch includes retained overdub Stops. `stopOverdubbingToStopped` retargets rather than
  consumes the retained obligation; it preserves the original phase. Closure ownership must survive
  the subsequent `sendAllNotesOff` (which currently clears `pendingNotes`). Silence output/ledger as
  usual, but preserve retained held-note capture state until job handoff or completed closure.
- A stopped-destination job may complete without resurrecting Playing. A live successor's state,
  storage, preview and session claim are never changed by predecessor retirement.

### 5. Display reads the stopped interval as a closed source

Expose the matching input-closed producer through the existing Track display-source boundary, with
its original closure phase and identity. Compose its preview/pending transforms as a stopped source
until job preview takes over; do not clear the preview to end tail growth, read a borrowed predecessor
as the producer, or freeze every live preview. A successor's open tails remain playhead-driven.

### Alternatives rejected

- State-only change: breaks retry eligibility, orphan ownership, slot-switch gating and transport closure.
- Keep recording until canonical release: preserves the defect's late input window.
- Wait for predecessor publication before every Start: contradicts the explicit successor requirement.
- Allocate another canonical finalizer or grow FIFO without measurement: unnecessary for existing overlap
  roles and changes the memory envelope rather than separating the transitions.
- Change the host's button scheduling: hides the firmware edge contract and is outside this slice.

## Risks / Trade-offs

- Finite physical storage can still serialize a Start → retain it distinctly, report holder, test recovery;
  never silently turn canonical contention into a blanket Start barrier.
- Making the track Playing changes downstream state-based gates → explicitly cover orphan release,
  slot switching, transfer replenishment, direct Record entry, transport Stop and persistence admission.
- An unbound occupied canonical output has a separate owner → never clear it to force a pass; test both
  empty reservation recovery and populated-output non-destruction.
- Frozen preview wrap geometry → test open and already-closed notes across wrap with Shorten/Hide;
  do not claim the unrelated observed visual defect fixed without its own evidence.
- RAM1 alignment → keep code cold and metadata external; target budget guard must pass unchanged.

## Architecture reassessment and historical review

Trigger: deliberately superseding logged Stop transition/lifetime clauses. User explicitly approved
the separate scope and amendment, then required successor overlap. Extend existing owners and roles;
no ownership transfer or schema migration. Authority: PROJECT_INTENT's immediate layering workflow,
ARCHITECTURE_RULES' Track/Loop ownership and role-bounded resources, DEC-233, then this delta.

Historical searches: DECISION_LOG for DEC-067/068/069/070/072/078/079/191/204; the active
runtime-working-set-ownership interaction/finalizer delta; `openspec/specs/capture-state-guards`;
`docs/Plans/*handoff*.md` for overdub Stop/finalization. Reuse decision: **YES**, existing FIFO,
custody, staging, worker bind and native harness. Keep DEC-204's real-orphan repair but extend its
ownership predicate; preserve DEC-072's job-owned pass isolation. No SD or undo-format migration.

Independent review: [REVIEW.md](evidence/REVIEW.md). Its initial **BLOCKED** findings are incorporated
in decisions 2–5. Implementation must prove each listed recovery/control; this document does not
claim a follow-up review PASS or native/device evidence.

## Execution packet — one stop-edge implementation slice

**Owner / files:** Track entry → existing maintenance/worker → existing commit/retirement. Primary
`include/Track.h`, `src/Track/TrackOverdubLifecycle.cpp`, manager capture/transport entry files,
necessary display-source read boundary, held-note clearing boundary, `ExecutionBounds.md`, and
`test/test_shared_capture_finalization/test_shared_capture_finalization.cpp`. Unrelated dirty hunks
are excluded. Any production file added to the slice must implement one of decisions 1–5, not cleanup.

**Preconditions:** existing prewarmed capture roles/FIFO/custodies and production-path harness;
owner-approved behavioral amendment. **Mutation/postconditions:** decisions 1–5 and the spec delta.
**Must preserve:** committed generation until actual publication, one finalizer, exact NoteIds/events,
one seal/pass publication/undo unit, session claim ownership, successor input, persistence gating.
**Commit boundary:** one independently buildable firmware/fixture/doc slice on the working branch,
staged by hunk; no auto-PR or unrelated host edits. No commit requested in this planning session.

### Oracle fixed before behavior edits

1. **Capacity-wait fixture:** seed actual committed base; open A with one closed note and one held note;
   withhold physical/canonical resources independently. Stop at phase P. Assert Playing immediately,
   unchanged raw rows/reservations/claim, immutable identity and P; later NoteOn/Off/CC add nothing.
   Repeated Stop preserves P. Empty canonical-reservation wait queues A without binding the worker;
   ordinary deferred turns replenish it. Populated output control leaves owner/content unchanged.
2. **Successor leg of capacity fixture:** Start B via manager and direct Track routes before A completes.
   With real cut exhaustion verify no producer reset and distinct retained Start. Release the specific
   holder; ordinary turns bind A and start B while A is pending. Distinct B input stays B-owned; service
   A to completion while B is live; B remains Overdubbing with exact unchanged rows/reservations.
   Stop B on its own edge; bounded drain proves A/B exact content and one publication each.
3. **Admitted-stop control:** same input trace with resources free; one immediate cut, phase P, one seal,
   publication and undo unit. Extra worker turns do not duplicate content; successor remains admissible.
4. **Necessary transition controls:** transport Stop before handoff both with/without a queued predecessor;
   lapsed-transfer-horizon recovery on normal maintenance; slot switch blocked until this Stop binds;
   changed LoopId/mode supersession; repeated requests and pending Start supersession; stopped preview
   wrap with closed note and held note plus pending Shorten/Hide. These supplement, not replace, the two
   requested principal fixtures.

Use existing clock/admission/resource-observation seams, not a copied FSM or success-returning domain
stub. Record each fixture's pre-fix discriminating failure. Freeze the expected event/NoteId/closure
trace independently. Bound drains to the harness's declared-budget + 20,000 turns and absolute
30,000-turn cap; failure prints interval/slot/phase/owner counts. No sleeps.

### Verification recipe and completion record

- Focused: `pio test -e native -f test_shared_capture_finalization` plus affected display-boundary and
  state/slot suites. Required principal fixtures must be registered and present in actual results.
- Broad/device: start `pio test -e native` in background alongside
  `pio run -e teensy41-capture-serial`; collect totals, skips and errored suites, not exit code alone.
  Target RAM1 budget must pass without rebasing. Upload only the successful image, retain exact hash
  and build/diff evidence, verify fresh `boot_reset,crash=0` and `BOOT,load,ok` independently.
- Reuse registered baseline/accepted-sequence stimulus for safe content checks; use the existing
  20-bar `scripts/hitl_midifile_chain.py` as the owner-requested musical reproduction, unchanged.
  Native forced-busy fixtures prove the exact logical interleaving; a chain not exercising refusal
  cannot substitute for them. Any content-changing device run must have an authorized target; no
  implicit permission to clear or overwrite the existing library/slot. Recipe and authorized target
  must be pinned before execution; no captured `command.json` execution.
- Device oracle: correlate each Stop/Start with state/input closure and job identity; require no
  live-state rollback, successor opening before predecessor retirement, no duplicate commit, no
  post-P A rows, and exact final content. Absence requires capture-loss accounting. The already-closed
  note visual report remains separately unproven if not reproduced.
- Evidence destination: `evidence/EXECUTION.md` with exact commands, logs, counts and hash.

**Current evidence:** contract/spec only. Fixtures NOT IMPLEMENTED; focused/native/build/device NOT RUN.
Initial independent review BLOCKED (corrections specified above, not yet verified). No task acceptance,
shipment, timing improvement or board-image change is claimed.
