# Immediate overdub reentry with continuous queued preparation

> **Reconciliation 2026-09-17 (depth-2 decision; DEC-067/DEC-072).** This plan's
> central mechanism stands, but its **capacity envelope and queue wording are
> superseded** wherever they imply N simultaneously sealed intervals
> (`jobs=[A,B]`, "stop B, start C", "A/B/C/D", "multiple punch cycles can
> accumulate", "third/fourth punch"). The approved capacity is **directional and
> bounded: one finalizing predecessor plus one independently recording successor
> (depth 2 in capture lifecycle, not depth 2 in deferred-stop queueing)**. The
> A/B/C/D walkthrough and the finite-capacity counterexample below are retained as
> the **N-job deferred queueing follow-up**, which needs additional admission roles
> and its own capacity decision before it can be claimed. Read every "queue"
> reference below as "the fixed-capacity descriptor ring whose head is the one
> admitted deferred job". Authoritative statements:
> [`CONTINUOUS-CAPTURE-OWNERSHIP.md`](CONTINUOUS-CAPTURE-OWNERSHIP.md) § *Declared
> capacity (directional)*, `design.md` § capacity, and the spec delta
> `midi-boundary-runtime-maintenance` § *Live capture is independent from deferred
> stop jobs*.


Implementation handoff, 2026-09-16. User-authorized direction; implementation is pending.
Active change: `resumable-midi-boundary-playback-maintenance`, primarily task 2.3,
with affected 2.4–2.5, 3.3–3.4 and task-5 validation obligations. This plan does
not close those tasks or replace normative OpenSpec requirements.

## 1. Outcome and definition of done

When DROID Button A punches out, the current capture interval closes at that
command's musical position. Playback keeps advancing. A subsequent Button A
punch-in MUST start accepting notes at that command's musical position even
while the predecessor interval is normalizing, resolving companions, publishing,
recycling history or handing off snapshots. One finalizing predecessor may coexist
with one independently recording successor (the depth-2 envelope); a second
simultaneously frozen interval is refused explicitly and N-job queueing is the
documented follow-up.

“Immediate” means the existing interpreted Button A action callback, after the
existing press/debounce/double-tap classification. Do not change that classifier
or claim that its existing delay is preparation latency.

Queue the immutable capture intervals and their completion work, not the command
to start capturing later. A test that eventually enters overdub but misses the
notes immediately after punch-in FAILS. Making finalization faster alone FAILS.

Passes and punch boundaries become metadata over the continuous capture/playback
process. Reuse current workers, canonical per-pass semantics and playback windows.
Every completed overdub interval still runs the overdub-stopping lifecycle; that
lifecycle belongs to its pending job and cannot globally disable a newer capture.

## 2. Established evidence and starting point

`captures/session_20260916_104431.log` proves the reuse implementation is on the
device: it contains `prefix_reuse,...,committed_normalization` at 76.071676 s and
81.187250 s. The blocked presses are 75.890 s and 80.991 s; the corresponding
cleanup ends at 76.071720 s and 81.187291 s. The earlier upload failure is therefore
not an explanation for this capture. Do not repeat that diagnosis.

The current blocking chain is explicit:

- `src/MidiButtonActions.cpp`: Button A returns when `hasPendingDeferredOverdubStop()`.
- `src/TrackManager/TrackManagerCaptureQueue.cpp`: `startOverdubbingTrack()` also returns.
- `src/Track/TrackOverdubLifecycle.cpp`: `startOverdubbing()` refuses pending stop and
  otherwise resets the one admission staging object.
- `include/Track.h`: one `pendingStopAdmission_` owns the stop continuation.
- `include/RuntimePlaybackView.h`: one `captureStaging` is shared with the frozen job.
- `include/Loop.h`: one active Capture and one `pendingCapturePass_`, plus shared
  session changes, companion preparation, undo and snapshot handoff state.

These guards protect real ownership. Removing them first corrupts the frozen
capture. A second complete Loop or an alternate companion/playback engine is not
the solution. Two alternating staging buffers alone are also insufficient: the
third rapid punch-out must not reinstate the same dependency.

Already implemented and to preserve:

- `RuntimeCanonicalPassInputs::resumeCurrentAfterAppend` and runtime append reuse.
- PLAYING/OVERDUBBING preparation through existing idle owners.
- `RuntimeCompanionPasses` retaining per-pass normalized output in canonical scratch.
- `adoptCommittedNormalization` and generation/owner-validated pointer-swap handoff.
- Canonical filtered-lane runs, Off-before-On within each run, stable order within
  each kind, existing LIFO and wrap fallback. Do not substitute an older reference.
- Original-Off protection, rolling windows, source handoff cursors and transport epochs.

Start with `git status --short`. The workspace contains substantial uncommitted
work. Do not reset, clean, overwrite or commit unrelated changes. Record the exact
starting diff and firmware revision/hash for later evidence.

## 3. Recommended ownership design

### A. One live ingress, many immutable interval descriptors

Build on the existing pooled `LoopEventStore` and capture admission/reservation
mechanisms. Use an append-only capture event history with explicit range ownership
and punch/wrap markers. This is a runtime ownership adapter around the existing
store, not a new event format, persistence format or semantic resolver.

Each logical capture interval needs a stable descriptor containing at least:

- Track, slot and loop identity; monotonic interval/version id.
- Predecessor logical version id, including predecessors not yet committed.
- Begin/end musical positions, transport epoch and stable input ordering sequence.
- Owned event-range/chunk references and captured end offset; open versus sealed state.
- Capture reservation ownership and boundary-closure obligations.
- Companion/edit intent ownership, undo-session identity and publication receipt state.

These are proposed fields, not claims that existing APIs already provide them.
Reuse current types where their ownership/lifetime semantics match.

The producer exclusively appends to the open interval. Background consumers read
sealed ranges only. Appending to a later interval must not increment a generation
that invalidates an earlier job. Keep content version, storage lifetime and
transport command epoch separate. A global capture counter cannot stand in for all three.

The event pool and range metadata are prewarmed. Punch callbacks append boundary
metadata and switch the active range in bounded work; they must not sort, traverse
notes/passes, clone full stores, allocate vectors or run companion resolution.

### B. Prove the range cut before using it

Audit the current `LoopEventStore` chunk API; do not assume an O(1) move/split API
exists. A range must remain immutable even when a boundary falls inside the last
partially filled chunk. Choose and document one safe implementation:

1. Shared stable chunk storage with immutable end offsets and independently owned
   range references, while the producer writes only beyond the sealed end; or
2. Constant-work tail rotation to a pre-reserved chunk, retaining the previous chunk.

Prefer option 1 if existing pool lifetimes can support it cleanly; otherwise use
option 2 without copying the chunk at the button edge. Neither may release a
chunk while a queued job, canonical pass, undo receipt or selected source references it.
Store range limits in descriptors rather than following mutable producer lengths.
Pool reset, slot unload and pointer relocation must invalidate/read-protect correctly.

Boundary markers should not require another full-size event/staging buffer per
punch. Coalesce empty intervals where semantically safe while preserving button
state, transport and undo semantics. Do not introduce a two-job or N-pass admission
policy. Physical memory is finite: reserve metadata and note closure resources,
reclaim incrementally and make genuine exhaustion explicit without losing already
accepted notes. Do not promise unlimited backlog under indefinitely greater input
than service throughput, or disguise a physical failure as “finalization pending.”

### C. One existing finalization engine processes the queue head

Reuse the existing `PendingStopAdmission` phases and canonical/companion/bridge
workers. The queue holds descriptors and receipts, not duplicate worker graphs.
Only the head job borrows shared staging/scratch. A resumable adapter presents the
head's sealed range to the existing workers. Live input must no longer mutate or
reset that borrowed staging.

Move or explicitly bind the following to a job rather than implicitly looking up
`getActiveLoop().capture` or the newest capture generation:

- Frozen events and stop-time closure reservations.
- Stop tick, destination and transport epoch validation.
- Pending note-change/companion input and prepared transfer receipt.
- Undo session, snapshot receipt, history recycle and cleanup ownership.

Do not merely rename `pendingStopAdmission_` while these child owners remain
singletons tied to live capture. Before removing a guard, list every object it
currently protects and show its new owner and release point.

A successful old-job commit must not call a track-state transition or cleanup
that closes, clears, switches to PLAYING, or resets the newer active overdub.
Queue-head publication changes the committed source; current punch state remains
owned by live ingress. Shared scratch is released only by the matching borrower.

### D. Maintain causal source versions

Successor interval B logically follows A, even if A has not published when B starts.
B must not permanently bind itself to the stale canonical source visible at punch-in.
Capture B's raw accepted input, musical positions and required edit intentions
immediately. Resolve its derived overlap/companion semantics against the completed
predecessor version when B reaches the worker. Do not drop or guess edits while
waiting for A, and do not use an obsolete display/source-span cache as authority.

Audit `TrackCaptureInput.cpp` for eager overlap/pending-change mutations. Keep
bounded note identity/reservation tracking online; defer source-dependent derived
resolution through existing canonical per-pass workers. Preserve live monitoring
and immediate active-capture preview. Any intent that depends on a control value
at event time must snapshot that value; do not read its later value during replay.

Publish jobs in logical order. Reuse validated normalized predecessor results and
existing immutable bridges. Later capture appends cannot invalidate the head's
stable input. Source edits/undo/slot actions must be serialized or version-qualified
against the queue; never silently apply them to whichever pass is current later.

Do not delay the transport, rewind the playhead, replay already-emitted notes or
skip source versions. Existing audible boundary guarantees still apply: queueing
work is not permission to omit newly due notes. Reuse the current delta-bridge
and cursor logic; unresolved audible coverage is a failing integration gate.

### E. Interface sketch and concrete state walkthrough

Names below are illustrative; adapt existing repo nouns rather than introducing
parallel abstractions. Required responsibilities are explicit:

```cpp
// All invoked operations at the action edge must have bounded, reserved storage.
punchIn(slot, actionTick, commandEpoch);   // Opens live interval now.
punchOut(slot, actionTick, commandEpoch);  // Seals range and enqueues its receipt.
appendLiveEvent(event, arrivalPosition);  // Addresses live interval, never queue head.
servicePreparation(quantum);              // One existing owner/child grain.
requestTransportStop(actionTick);         // Stops ingress/emission; retargets work safely.
```

Keep `hasPendingDeferredOverdubStop()` as a query about background work where
needed, but audit every caller. It must no longer mean “live capture cannot open.”
Introduce separate live-capture and worker-busy queries only where current state
predicates cannot express that distinction. Do not globally invert the old query.

Expected sequence with worker service deliberately paused:

**Superseded walkthrough (N-job deferred queueing, not this change's capacity).**
Retained for the follow-up; the approved envelope only performs steps 1-3 and then
services A before B may freeze:

1. Start A: live=A, ring empty; notes append to A.
2. Stop A: live=none, ring=[A]; A enters its stopping lifecycle and owns the
   deferred/frozen role.
3. Start B immediately: live=B, ring=[A]; notes append to B now. B is admitted
   because START needs only the live role.
   -> B's own stop is REFUSED here (deferred/frozen role occupied) and creates no
      descriptor; B stays live and unchanged.
4. Service A to completion: live=B, ring=[]; the deferred/frozen role is free.
5. Stop B: admitted; B enters the stopping lifecycle and transfers exactly once.
   -> the earlier "stop B, start C; live=C, jobs=[A,B]" and "service B against A"
      steps belong to the N-job follow-up, not to this envelope.

Each job must retain its own stop tick. Never substitute the later drain tick.
The latest active interval and its backlog can coexist; a single Track enum cannot
be used as the sole owner of both facts. Keep changes to state handling explicit
and test the real public predicates used by routing, display, MIDI service and save.

### F. Falsification findings and non-negotiable proof obligations

The fixed-queue design is only valid if the following counterexamples are made
impossible. These are architectural tests, not optional edge-case coverage.

**Held-note and state-stomp counterexample:** A records a held C4, punches out,
B punches in and records C4, then A completes while B is live. A completion must
not clear B's `pendingNotes`, change the Track state from OVERDUBBING to PLAYING,
consume B's pending changes, or replace B's preview. Therefore pending notes,
live capture state and preview ownership must be interval-qualified; completion
cleanup must be job-local. If the implementation still calls shared cleanup such
as `pendingNotes.clear()` or unconditionally sets Track state on old-job completion,
the design is falsified.

**Mutable-store counterexample:** `Loop::beginCapture()` currently clears the
single `capture.store`. A queue descriptor that merely stores metadata while a
new capture reuses this store is not an immutable interval. The implementation
must either use stable chunk references with immutable end offsets or rotate the
producer to a pre-reserved tail chunk before the next append. A boundary inside a
partially filled chunk must be covered by a test; copying or detaching an
unbounded chunk list at the Button A edge is not an acceptable substitute.

**Finite-capacity counterexample (restated for the depth-2 envelope):** With
service paused, a predecessor holds the deferred/frozen role. The design must
expose explicit, non-destructive admission failure at the *directional* boundary --
a successor STOP while that role is occupied is refused with no descriptor, and a
START/NoteOn that cannot obtain the live role is denied before mutation. It must
never discard an accepted event, alias two intervals' staging, or report capacity
exhaustion as ordinary finalization latency. The original repeated-A/B/C/D form is
the **N-job deferred queueing follow-up** (see the banner above): it is not a
requirement of this envelope.

**Causal-source counterexample:** B starts while A is unpublished. If B stores
only raw events and later resolves against whichever canonical source is current,
its result can differ from serial A-then-B processing. Every job must retain its
logical predecessor id and source identity; source-dependent companion/overlap
resolution begins only after that predecessor is committed or explicitly reaches
STOPPED recovery.

**Transport-stop counterexample:** Transport Stop between any two finalization
grains must invalidate every queued PLAYING publication. A worker completing later
may recover accepted data under STOPPED policy, but may not resurrect playback,
reopen emission, or apply a stale slot/undo/snapshot receipt.

**Bridge and cursor counterexample:** A selected bridge must remain immutable while
B or C prepares a candidate. Any refresh must use the alternate bridge and a
cycle-qualified cursor; otherwise the implementation can duplicate, omit or delay
an event even though canonical pass contents are correct.

The decisive reproducer therefore runs through the real Button A handler,
TrackManager route and Track input path with worker service paused: A sealed,
B admitted live, B's stop refused while A owns the deferred/frozen role, A's
completion while B stays live, B's own stop admitted afterwards, distinct notes,
same-pitch held notes, and one-grain-at-a-time resume. (The A/B/C-and-possibly-
fourth form is the N-job follow-up.) It must assert interval membership, state/preview ownership, predecessor
ordering, transport-stop precedence, bridge immutability and zero allocation at
each punch boundary.

## 4. State and command rules

- Punch-in: open the successor capture interval and accept its first event in the
  same command ordering as ordinary overdub start; prior jobs remain pending.
- Punch-out: seal that interval, enqueue its mandatory stopping work, leave playback
  advancing. Repeated punch-in/out acts on live punch state, not queue-head state.
- Held notes: each accepted On keeps its reserved terminal-Off obligation. Boundary
  closure, later physical Off, repeated pitch and new-pass continuation must follow
  existing musical semantics exactly, with no orphan/double Off or old-job closure
  applied to a newer note. Do not invent held-note continuation on punch-in.
- Wrap: use the same interval/version mechanism. A wrap is not a second capture or
  finalization engine. Preserve grouping of wraps within the existing undo session.
- Transport Stop: close live capture, increment the controlling command epoch,
  stop emission and invalidate every queued PLAYING publication. Retain accepted
  data for ordered STOPPED recovery. Later transport resume must not resurrect an
  old PLAYING intent merely because its worker finally completes.
- Slot change, clear, load and undo: bind receipts to slot/loop/version identity.
  Preserve current user-visible policy; defer unsafe mutations explicitly, without
  freeing accepted queued data or letting a previous slot mutate the selected slot.
- Display: O means live overdub is accepting notes; PLAYING after punch-out means
  playback continues. Finishing an older job must not overwrite the newer O state.

## 5. Implementation sequence and checkpoints

### Step 0 — contracts and ownership audit, before firmware edits

Read `PROJECT_STATE.md`, `CURRENT_WORK.md`, `docs/Authority/README.md`, architecture
and delivery rules, and the active OpenSpec proposal/design/specs/tasks. Use the
existing decision-review and ownership-transfer templates for this expanded
ownership change. The user's instruction in this plan already authorizes the
behavior; do not ask again just because a template has an approval field.

Add normative scenarios for immediate reentry with one and several pending jobs,
causal predecessor resolution and transport Stop. Keep task 2.3 and task 5.3 open.
Produce an owner/lifetime checklist covering every singleton above. Complete
OpenSpec preflight/review records with real evidence, not generic assurances.

### Step 1 — failing native behavioral reproducer

In existing track-state/capture test fixtures, pause the old job deliberately at
several phases. Punch in again; immediately inject identifiable notes; punch out;
repeat at least three times without servicing any old job. Assert exact interval
membership and punch timestamps. Resume one grain at a time and verify all events
and states. This must fail on the current code before guards are changed.

Cover handler/manager/Track routes, not just a queue container. A test that calls
a hypothetical new API while the real Button A route still returns is insufficient.

### Step 2 — range storage and reservations

Implement the bounded interval cut and ownership adapter around `LoopEventStore`.
Choose and document the safe cut mechanism before wiring the queue: prefer stable
chunk references with immutable end offsets, or use constant-work tail rotation to
a pre-reserved successor chunk when a boundary lands in a partial chunk. The
producer must never append beyond a sealed end. Prewarm interval descriptors,
metadata and spare chunk reservations. Add tests at chunk boundaries, partially
filled chunks, shared-prefix reads, held-note reservations, reclamation, stale
generations and allocation failure. Verify zero allocation at punch edges and add
byte accounting for the declared worst supported simultaneous live/queued/head/
selected owners.

### Step 3 — parameterize the existing worker around its head job

Refactor `beginDeferredOverdubStop()` / `processDeferredOverdubStopGrain()` to obtain
job-owned inputs. Migrate the sealed range, frozen events, stop-time closure
reservations, pending changes, reservations, undo, snapshot, history-recycle and
cleanup receipts with the job. The Track's live interval must never be used as an
implicit worker source. Keep one shared worker pipeline and existing per-turn Q;
only the queue head borrows its scratch. Run all existing stop, companion, bridge,
undo and snapshot tests before changing Button A behavior. Single-job behavior
must remain equivalent.

### Step 4 — enable immediate reentry end to end

Change the Button A, manager and Track guards only after Step 3 proves isolation.
Do not replace them with a pending-start flag. Add separate live-capture and
worker-busy predicates; `hasPendingDeferredOverdubStop()` must remain a background
query, not a live-ingress gate. Bind `TrackCaptureInput.cpp` routing, pending-note
identity, reservations and preview to the live interval. Preserve its active
admission while old cleanup runs. Old-job completion must not clear live notes or
set the Track state to PLAYING. Pass Step 1's multi-punch reproducer and add a
case in which old cleanup completes while the newest interval is recording.

### Step 5 — causal resolution, wrap and publication

Bind each queue head to its predecessor result. Reuse retained normalized passes,
companion workers, rolling windows, delta bridges and original-Off handling.
Make wrap-created work use this path. Add same-pitch and pending-companion tests
whose result differs if B resolves against the pre-A source. Compare against a
serial canonical reference that processes the same boundary/event sequence.
Add undo/redo and snapshot-receipt tests covering multiple queued sessions.

### Step 6 — epoch and lifecycle integration

Exercise transport Stop before/after every worker phase, while a newer interval
is open, and during metadata/receipt handoff. Verify no stale PLAYING publication.
Exercise clear/load/slot switch according to the existing policy and protect chunk
ownership across all paths. Keep first-record bar-1 preparation and launch unchanged.

### Step 7 — bounded telemetry and full verification

Record compact transition/aggregate evidence, not another per-event logging flood:
command action tick, live interval id, queue-head id/phase, predecessor id, accepted
first-note tick, queue depth/high-water, reuse/restart counts, and commit/cleanup tick.
Correlate with existing MIDI deadline/emission and grain start/end device-clock data.
State whether each timestamp is ingress, service, publication or emission.

Run focused suites after each slice. At final integration, run `pio test -e native`,
strict OpenSpec validation, `git diff --check`, and `pio run -e teensy41-capture-serial`.
Run PlatformIO test/build/upload sequentially because they share build/cache state.
Check RAM1 and actual runtime prewarm success; static build success does not prove
external pool or fallback-heap headroom. The previous build had 6,176 bytes of RAM1
local headroom. Do not spend another ITCM bank accidentally.

Upload when device access and session authorization permit it. Capture an explicit
build identifier. Do not clear the user's manual material to set up a test unless
that scratch location/action is authorized. Perform focused HITL below before
claiming the interaction is fixed. Keep the full task-5.3 matrix and reservations
open until their separate evidence passes; do not remove compatibility paths early.

## 6. Required acceptance cases

1. Punch out A; with A paused, punch in B and send On/Off immediately. Both events
   belong to B at their original ticks. No eventual-start substitution.
2. Repeat for C and D before A drains when the declared capacity permits. No hidden
   two-buffer limit or ignored presses; when capacity is full, a new interval is
   rejected explicitly and all previously accepted intervals remain intact.
3. Old cleanup completes while D is open: D's state, data, reservation, pending
   notes and preview remain intact. O stays O; queue depth may shrink independently.
4. B uses the same pitch/identity-relevant edits as A. Serial-reference parity proves
   B resolves against A's result and canonical per-pass semantics are preserved.
5. Held notes cross punch/wrap boundaries; original audible Off is retained exactly
   once and later physical Off cannot terminate an unrelated new note.
6. Equal-tick lane runs, separated equal ticks, LIFO and wrap pairing regressions pass.
7. Empty rapid punches, dense input, long loops and many short intervals exercise
   metadata reuse without materializing a full-loop event array at each boundary.
8. Transport Stop at each queue phase preserves accepted data and blocks stale
   PLAYING publication, including after later transport restart.
9. Failed/stale worker publication cannot clear active capture, shared pending-note
   state, or recycle chunks still owned by a queued job.
10. Overflow-free HITL: record, then at least four rapid overdubs without transport
    Stop, sending distinct notes immediately after each punch-in. Include wrap and
    same-pitch cases, then transport-Stop interruption. Observe display and actual
    MIDI output, not only button/state logs.
11. Compare input-to-capture timing separately from background completion latency.
    Check audible deadlines separately again; short grains or low stop latency do
    not by themselves establish a real-time deadline pass.

## 7. Stop conditions and mistakes to avoid

Do not mark complete if only tests of helpers pass, if a handler still rejects
pending work, if B's early notes are lost, if queue depth is silently capped by
full staging buffers, or if playback/snapshot state overwrites live punch state.
Do not remove source/epoch/ledger checks, raise Q or the provisional 6,240 estimate
to mask throughput, bypass mandatory overdub-stopping, or process the entire backlog
inside a MIDI callback. Do not flatten passes and apply companions globally.

If an ownership API cannot be safely adapted, leave the failing behavioral test
and record the concrete missing lifetime proof. Do not substitute “start later”
or claim that faster finalization satisfies immediate capture. Preserve existing
work; no broad rewrite, unsolicited commit, archive or compatibility removal.

## 8. Suggested prompt for the implementing model

Read this complete plan and the active OpenSpec context, then implement its steps
in order. The required outcome is immediate capture on every accepted punch-in,
even with earlier stop jobs pending; queueing a future start is not sufficient.
Begin with a failing native test through the actual button/manager/Track flow.
Reuse existing storage, companion and playback workers. Preserve canonical
per-pass semantics and all accepted notes. Work in reviewable slices, run focused
tests, then full native/build checks. Update OpenSpec evidence without closing
HITL gates prematurely. Do not reset or commit the existing dirty worktree.
Report exactly which firmware is on the device and which gates remain unverified.
