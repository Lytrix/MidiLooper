# Shared record/overdub finalization — firmware implementation contract

Status: contract specified; production fixtures, identity/length prerequisites and the
bounded record preparation composition implemented, 2026-09-28. Shared deferred record
finalization and device acceptance remain open.
Authority: DEC-078 — accepted interactions; runtime role/finalization requirements in
[spec delta](specs/runtime-working-set-ownership/spec.md); design Revision 23 mandatory
record-A → overdub-B acceptance. Implementation: S4f.15–S4f.22 in [tasks](tasks.md).

## What was already planned, and what was missing

The proposal budgets one live capture role per track for **record / overdub**, alongside a
finalizing predecessor. The spec's accepted-interaction and resource-rotation requirements
cover both. Design Revision 23 item 6 expressly requires recording a long empty A, stopping
it, then stopping overdub B while A still finalizes. S4f.13 also requires A to finalize while
B captures. This is existing intent, not a new product requirement or a reason to weaken HITL.

The implementation detail did not carry that intent through the record route. The accepted
stop drive calls `beginDeferredOverdubStopForSlot`, and current record-stop calls
`commitCaptureForStop` inline. This contract closes that omission. Separate weaker record
acceptance is rejected. First-bar readiness is a playback prerequisite, not proof that the
canonical stop transaction has already completed.

## G0 — baseline execution and missing dependencies (before implementation)

- `Track::startRecording` in `TrackCaptureInput.cpp` prewarms and prepares a retained raw first
  bar. `Track::serviceDeferredMaintenance` services it in bounded work while recording.
  `LoopPlaybackRuntime::bindFirstRecordingBar` binds raw coverage to final length.
- `Track::recordMidiEvents` reserves complete note lifetimes in `CaptureAdmissionStaging`
  only under `isOverdubbing()`. Record's accepted input goes to the canonical capture store;
  merely calling the overdub admission function would freeze the wrong/empty staging.
- `captureInterval_` advances at `startOverdubbing`; record-start does not currently advance it.
  Shared stop identity must cover both and advance once per actually started capture.
- `Track::stopRecording` and `stopRecordingToStopped` call `prepareRecordStop` then
  `commitCaptureForStop` → `Loop::commitCapturePass` → seal + publish + side effects inline.
  `prepareRecordStop` currently finds the last tick, closes pending notes and remaps events.
  Those content-sized operations cannot be hidden inside a supposedly bounded admission.
- Callers are `TrackManager::stopRecordingTrack`, `finalizeCaptureAndSelectSlot` and
  `stopAllTracks` in `TrackManagerCaptureQueue.cpp` / `TrackManagerTransportTick.cpp`.
  They immediately read length, align/set master geometry, persist, switch or clear held notes.
  Every caller must stop assuming the void call means publication finished.
- The existing Track-owned `CaptureStopJob` has raw-range ownership, slot/LoopId/interval,
  frozen generation, pending pass, preview, pending changes, causal source and undo receipts.
  `TrackOverdubLifecycle.cpp` supplies admission/re-drive and the bounded worker.
  `TrackPreparedStopCommit` supplies joint publication and retirement.
- The worker uses overdub commit reasons, session-exit receipts and
  `TrackUndo::prepareOverdubSessionEntry`. Record instead needs `RecordPassAdded`, before/after
  geometry, slot-history pruning and deferred after-snapshot attachment from
  `TrackUndo::pushRecordPassAdded`. These semantics must survive the migration.
- Record playback return currently occurs after inline publication. Moving publication later
  must preserve the retained view and provide later coverage while the job owns raw input.
  Playback, display and snapshot builders must not borrow a live producer reset by B.

G0 verdict: PASS for defining this targeted extension. The missing work is named above;
no claim that first-bar readiness alone proves the whole implementation. Current code does
not meet this contract. Existing host HITL regressions are independent of firmware acceptance.

## G1 — owners and evolution

Extend existing owners; do not introduce a second finalizer, Manager, arbitration queue,
storage format, undo kind or active-slot authority. The same Track-owned latch, descriptor
ring, admitted stop job, idle worker, protected undo transaction and terminal publication
serve record and overdub. Mode-specific preparation operates inside that lifecycle.

`Track` owns interval identity, accepted stop, geometry receipt and stop job. `Loop` owns
canonical capture/pass construction and transfers its sealed raw extent to that job through
the existing receipt protocol. `LoopPlaybackRuntime` owns the sounding retained/window/bridge
views and their generation-qualified source bindings. `TrackManager` owns pending switch/start,
master length and selected focus. `TrackUndo` / `TrackUndoPublication` own undo visibility and
retirement. `StorageManager` owns dirty/persist scheduling, keyed to the committed interval.

Reassessment: the timing/lifetime change requires this targeted review. Mutable responsibilities
remain with their existing owners; no cross-module ownership takeover, schema or undo behavior
change is authorized. The user requests the shared-finalization contract. Runtime allocation,
MIDI budgets, cardinality and accepted-input guarantees are retained. Failed measured gates
return here with evidence; they do not authorize silently narrowing the acceptance.

## G2 — common state and transition contract

### Identity and receipt

Extend the existing job/latch, without an extra queue. Every admitted record or overdub start
gets one monotonically advancing capture interval identity. A pending start gets none until
it actually starts. Stop captures its mode and `(Track, slot, LoopId, interval)`; admission
also binds frozen generation and source identity. Do not use active/selected slot on re-drive.
No transport epoch is added to the accepted latch: retain design Revision 24's live transport
re-derivation. Existing job publication epochs remain valid for their existing purpose.

Record-specific scalar receipt: capture origin, raw/final length, execution tick, launch
projection/rewind applied flag, baseline geometry, record commit reason and pending pass identity.
Store it in the existing externally allocated job/role; no large arrays in the DTCM Track.
There is no copy of the whole take. A tagged mode/receipt selects preparation, not a second FSM.

### Accept → bind → prepare → publish → retire

1. **Accept:** latch one stop for the live interval. Repeated drive is not another request.
   If shared capacity is occupied, preserve live capture and existing input admission;
   retain switch/start continuations. No empty job, partial seal or slot movement is allowed.
2. **Bind at the earliest valid boundary:** preflight resources and playback readiness,
   compute the record length from maintained scalar summaries and existing `RecordStopLength`
   policy, then atomically move the raw extent, pending-note obligations, preview, geometry
   baseline and required source receipts into the job. Bind its mode/generation. Only after
   successful binding may the manager activate B and admit B's independent live capture.
   When execution waited, derive geometry at execution from the still-live interval, not an
   obsolete pressed-at tick. Consume the accepted latch once on successful binding.
3. **Prepare in bounded turns:** record and overdub share scheduling/capacity/publication.
   Record preparation normalizes its frozen raw source, closes held notes, prepares the
   record pass and record undo. Overdub retains causal-base resolution and companions.
   Preparation failure retains ownership and a named retry/fault state; it never discards
   accepted content or runs the old synchronous full-stop fallback.
4. **Publish once:** atomically publish this job's canonical pass, correct undo entry and
   valid audible-source handoff. Update display/source generations and admit persistence for
   this job's slot. A successor may have changed selection and its own producer in the meantime.
5. **Retire:** bounded cleanup releases only matching receipts/generations after all readers
   release them. B's accepted stop re-drives when the shared resource becomes available.
   Cross-track jobs progress independently; the existing ring is descriptor capacity, not
   permission to accumulate stopped intervals or allocate another full finalizer.

A stop to Stopped uses the same job with no audible launch. Global transport Stop must retarget
both modes and cancel unexecuted starts; it must not erase held-note obligations before their
transfer. A slot-switch's stopped-track destination does not imply stopping global transport.

## Record ingress and bounded stop preparation

Use canonical record input as the authoritative source; do **not** mirror the entire growing
record into overdub's fixed staging at input or add a take-length-dependent allocation.
Maintain the last-event/length summary on accepted append through the existing capture owner,
resetting it with the interval. Transfer the raw `CaptureRange` at binding and consume it in
bounded worker grains through the existing prepared-pass facilities. Extend those facilities
with a record source mode, preserving raw absolute/relative coordinate rules and NoteIds.

Reuse existing pending-note ownership and prewarmed closure capacity. Move/swap obligations
at binding; do not iterate an unbounded map on the input edge. No note becomes accepted unless
its eventual closure and publication remain backed under the existing capture admission
contract. Preserve the existing supported record envelope; using the smaller overdub staging
limit to silently reject previously admitted record input is a failing implementation.

Record's normalized source applies the existing final-length, held-tail and beyond-length
NoteOn policy consistently to the prepared pass **and** playback view. Bound normalization,
ordering, note closure and pass preparation in the existing worker (32-entry preparation
quantum today); a full-store remap, validation, sort or scan is forbidden at stop/launch.
Obligations received after binding belong to the correct live interval or frozen receipt,
never a new interval selected merely by pitch, current slot or current Capture mode.

Empty captures take a mode-specific empty terminal through the same acceptance/identity rules:
no fabricated pass/undo, correct empty geometry, no leaked job/reservation, and pending switch
or start is preserved according to its existing supersession contract.

## First-bar and onward playback contract

`prepareFirstRecordingBar` reserves the role; `serviceFirstRecordingBar` fills it. An empty
prepared role is not proof of current content readiness. Bind only coverage whose generation,
normalization and final length match the frozen record interval, including captures shorter
than one bar and held notes crossing its end. Complete any stop-dependent corrections through
bounded preparation; never pretend stale raw coverage became valid by changing a flag.

Once playback launches, retain that immutable first-bar source until a matching replacement
is ready and handed off through the existing lifecycle/ledger rules. Freeze/rebind in-flight
builders to the job-owned source before resetting the Loop producer for B. Preparation of
later windows must read the same normalized frozen source until canonical publication is
available, and must meet their playhead deadlines independently of terminal job completion.
No global Stop, missing first notes, doubled boundary notes, full-take build or snapshot wait
is a recovery strategy. If readiness is temporarily unavailable before binding, keep the
accepted stop live and schedule the earliest valid boundary under the existing execution
budget; a missed declared deadline is reported and fails the gate, not excused by acceptance.

At the execution boundary apply existing record quantization/auto-align/master-length and
projection/rewind policy once. Record completion must not later rewind the clock a second time
or overwrite B's geometry. TrackManager consumes an explicit identity-qualified geometry-ready
result instead of immediately reading mutable active-loop length after a void stop call.
Preserve global transport continuity and other tracks' MIDI timing; geometry policy changes
beyond this relocation are not part of this contract.

## Undo, persistence, display and alternate paths

Prepare `RecordPassAdded` with job-owned before/after geometry and final pass ID. Extend the
existing protected undo transaction to stage record's slot-history pruning and redo retirement
in bounded work; publish the pruning/new entry with the pass. Before commit a failed preparation
must preserve/restorably retain prior undo ownership. Bind deferred after-snapshot attachment
to the committed entry/interval, never whatever is stack tip after B. Overdub still produces
its existing session-wide undo unit. Undo/redo during either stop uses the existing protected
command continuation. There is no new undo grouping or history loss on a busy finalizer.

Key dirty/persist admission to the job's Track/slot/LoopId and committed revision exactly once.
Remove caller-side duplicate persistence that assumed synchronous record completion. A save,
clear, load or reset sees both accepted stops and admitted record/overdub jobs through the
same in-flight predicate; it must not free a source borrowed by playback or the job. Explicit
supersession uses existing identity-qualified replacement rules and retires every receipt.

Audit all three manager record-stop callers plus direct Track methods, queued start, global
transport stop, subsequent same-slot overdub/record, track switch, note-edit fold, Undo/Redo,
clear/load/save, worker regeneration and stopped recovery. Record-start's current destructive
reset/prewarm cannot run against a still-owned same-slot predecessor; gate it before mutation,
retain the start request and execute it when its required owners are available. A successor
on a provisioned different slot is not serialized behind unrelated predecessor cleanup.

Compatibility names (`hasPendingDeferredOverdubStop` etc.) may remain as thin wrappers during
migration, but must query the common owner for both modes. Their removal trigger is all callers
converted plus source-gate/native proof. No production route may keep synchronous full record
publication as a fallback after this slice is enabled.

## Bounds and resources

Input acceptance and stop handoff: structurally bounded scalar checks and receipt swaps;
no new allocation, full validation or content-size walk. Each deferred unit must itself be
bounded; wall-clock checks between unbounded operations do not satisfy this contract.
Reuse the 32-entry worker quantum and existing admitted budgets; first-bar/window capacity
comes from existing runtime configuration, not a new guessed limit. No extra scheduler or
MIDI service call sites. Cold helpers use existing FLASH placement.

Record the exact bytes added per job/live role and simultaneous owners before production edits:
8 tracks × 8 slots today, one live capture per track, existing per-interval finalizer resources
and sounding-role cap. Resource summaries must include raw/source pins, first-bar and next-window
overlap, held-note closure and undo retirement. Canonical content may grow with notes; additional
derived working state must remain bounded by roles/windows. No whole-take duplicate. Preserve
capture-serial RAM1 padding floor 840 bytes and report baseline/image/config identity plus
all RAM1/PSRAM changes. Native accounting is necessary but not a measured device-memory pass.

## Execution packets and fixed verification oracle

Each implementation slice in tasks uses this contract plus `EXECUTION_PACKET.md`; pin its actual
linked closure and counters before editing behavior. First establish production-path native
fixtures for real record stop, shared worker, first-bar consumer and persistence/undo owners.
Use the existing controllable clock, allocator and in-memory SD. If reusing the drain suite,
replace its off-path record-stop stubs with real owners; if splitting shared harness macros,
split by transport/storage/edit family and run every dependent native suite. No faithful
stand-in may impersonate the state owner being asserted.

Required case matrix and ordered oracles:

- **COMMON-IDENTITY:** start record then overdub (and reverse), assert distinct interval IDs;
  duplicate stop and repeated worker polls publish and retire once. A replacement slot/LoopId
  cannot consume the old obligation.
- **MODE-ROTATION:** R→R, R→OD, OD→R, OD→OD. Hold A's actual worker at a controlled native grain;
  accept A stop, bind its job, activate B, inject independent notes, stop B once while A owns
  capacity. Assert B's latch/no second job, advance A through publication/retirement, then B
  binds and publishes once. Exact expected notes, geometry, undo and persisted identities for both.
  Reintroducing inline record commit or an overdub-only busy predicate must fail the record cases.
- **PLAYBACK-COVERAGE:** stop before, at and after one bar; complete and incomplete first-bar
  builds; held/crossing/retriggered notes; last-tick remap and long/dense captures. Advance beyond
  first bar and across a wrap before finalization completes. Assert exact MIDI event ordering,
  no gaps/duplicates and source identity. Stale first-bar generation must never be selected.
- **BUSY/FAILURE:** capacity busy, allocation failure during preflight, missing readiness,
  terminal preparation fault, lost/regenerated runtime and explicit replacement. Before binding
  no ownership moved; after binding receipt survives; restoring capacity resumes exactly once.
- **TRANSPORT/GEOMETRY:** stop-to-Playing, stop-to-Stopped, slot switch and global Stop in every
  worker phase; internal/external clock and current supported tempo edges. Master length,
  auto-align and rewind apply once to the intended interval; canceled start cannot resurrect.
- **UNDO/STORAGE:** undo/redo before commit, after commit and during cleanup; old slot history,
  empty capture, note-edit interaction; save/load/clear with live receipts. No partial undo/pass
  publication or wrong-slot save; playback and display see a valid matching generation.
- **CAPACITY/BOUNDS:** all supported concurrent tracks, fixed working set with increasing record
  length/density; assert per-grain units, no edge full walk and no per-cold-slot/job-sized allocation.

Bounded fixture scheduler: cap each run using the admitted phase-credit total plus explicit
setup/terminal turns; fail with mode/slot/interval/phase/remaining credits and owning receipts.
Never use an unbounded while-until-complete test. Native pause/fault controls are test-only;
no hardware pause hook or inflated work is permitted to manufacture acceptance.

Verification: focused native cases above, mutation proofs, full `pio test -e native`, successful
`pio run -e teensy41-capture-serial`, floor/accounting check, strict OpenSpec validation. Device
uses the response-driven registered/reused HITL entry point with declared temporary stimulus
variants, identified firmware/display/clock and authorized slots. Test record and overdub legs
plus the design's mixed record-A → overdub-B sequence. Require observed natural overlap, one
press, continuous transport, exact content/persistence/undo, timing gates and proper retirement.
A missed natural window is NOT_EXERCISED; no criterion is waived and no reboot prepares repeats.

Completion: S4f.16 production fixtures are implemented; see [fixture evidence](S4F16-EXECUTION.md).
The user approved connected S4f.17–S4f.21 implementation. Common start identity, typed accepted-stop
identity and maintained record-length metadata are prerequisites in progress/verification; see
[S4f.17 execution evidence](S4F17-EXECUTION.md). The bounded Record preparation composition is now
implemented and native-verified: `RecordPreparationCursor` canonically orders a private prepared
store, applies reserved wrap repair and re-orders, then applies minimum-length decisions and private
compaction in bounded grains. Missing NoteOn identity, a stale preparation generation and a missing
reservation deny by name; the role is not dispatched yet, so no ITCM or RAM1 growth is claimed. The
prepared route now also completes missing NoteOn identities in canonical order (matching `sealCapture`)
when the caller supplies a `NoteIdSource`, and still denies without one. The frozen take is now
staged into that private output one bounded row per unit by `RecordSourceStaging::Cursor`, applying
the existing per-row stop projection under an ephemeral seal; the closure output holds only terminal
Offs and must be merged into the staged store before normalization (see the corrected sequence in the
[role-admission packet](S4F18-ROLE-ADMISSION-PACKET.md#g0-correction-and-slice-sequence-2026-09-28)).
See [composition](S4F18-COMPOSITION-EXECUTION.md), [identity](S4F18-IDENTITY-EXECUTION.md) and
[source-staging](S4F18-SOURCE-STAGING-EXECUTION.md), [closure-merge](S4F18-CLOSURE-MERGE-EXECUTION.md)
and [preparation-role](S4F18-PREPARATION-ROLE-EXECUTION.md) evidence. The per-track
`RecordPreparation::Role` now binds by job identity and drives the composition over the merged store
to `RecordPrepared`, a bounded pass transfer stages the normalized output into the job-owned receipt
at `RecordPassReady`, and the deferred publication atomically commits the pass, pushes the interval's
`RecordPassAdded` undo entry from its own job-owned geometry and admits persistence once at
`RecordPublished`, after which a bounded retirement returns the ring entry. S4f.20 remains unchecked
for the deferred revts validation, `invalidateCaches()` parity and the production Record bind site. Record stop is still inline. Publication, the shared worker continuation and all caller
migration gates are not complete. No device acceptance; S4f.13 and S7 remain open.
