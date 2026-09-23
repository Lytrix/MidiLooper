# Implementation handoff corrections — 2026-09-23

This is the current contract for the five targeted implementation-review findings. It supersedes
conflicting earlier revision text in this change only. DEC-079 records the clarification to DEC-078;
no firmware has been implemented or validated by this documentation update.

## Execution discipline (DEC-080)

All remaining steps use [ARCHITECTURE-REVIEW.md](ARCHITECTURE-REVIEW.md)'s P0–P8 packets and the shared
G2 execution-packet workflow. The review names existing production-path test seams, stepwise acceptance
oracles and evidence criteria. It does not implement fixtures or turn future dependencies into available code.
Contract choices below remain authoritative; a test is never weakened to accommodate a contradictory patch.

## 1. Executable first slice: boot/load canonical admission

Owner: `Track` owns accepted stop state and canonical reservation; `TrackManager` owns transport epoch;
`StorageManager` consults slot liveness before removing a restore request.
Primary files: `src/TrackManager.cpp`, `src/Track/TrackPlaybackWindowBuild.cpp`,
`src/Track/TrackOverdubLifecycle.cpp`, `src/Track/TrackDeferredMaintenance.cpp`,
`include/Track.h`, `include/TrackPlaybackRuntime.h`, `src/StorageManager/LoadLoopJob.cpp`,
and the transport-epoch callers named by S4f.4.

Preconditions: S0/S1 evidence is recorded; the native red scaling fixture is an expected-failure
characterization, not an unexplained failing suite. Use the current supported resident library.
The 60-long-slot library and windowed playback are not prerequisites.

Mutation / commit boundary: **one buildable boot/admission slice**, containing S2b.0–S2b.6 and only
these prerequisites pulled forward from later stages:

- S3.0 size/placement pins for fields actually introduced here, before adding those fields.
- S4f.0 slot-parameterized stop admission; the Stop variant of S4f.1 and S4f.2; transport epoch and
  retargeting from S4f.4; stop-related in-flight consumers from S4f.5. Stop acceptance, deferred
  retry, epoch handling and boot-reservation removal ship together. No temporary second latch.
- S4.0's `noLiveOwnerForSlot(track, slot)` for **existing** ring entries, pending commit, prepared
  handover, job-owned roots and the Stop latch. The ring scan is over its live window. Add the
  reconstruction term when S5 introduces that owner; do not call an API that does not exist yet.
- Existing track-level start/switch guards must include the Stop latch until S4f expands those
  interactions. Do not move the active slot underneath an unadmitted stop in this intermediate slice.

The later tasks extend these same helpers; they do not create them again. S2b.4 releases only the
canonical reservation after its existing owner is gone and its store is empty; it does not introduce
recyclable runtime leases, so the S3/S4 generation pool is not a prerequisite for this release.

Postconditions: inactive boot/load performs no canonical-copy reservation; a loaded-slot overdub
can start without that reservation and one Stop request is retained, retried and completed once
capacity returns. A pending restore stays queued while its slot is owned. Transport Stop cannot
later complete as PLAYING. All other runtime residency and publication authorities remain current.

Must preserve: sealed capture and job ownership, MIDI continuity, no allocation on the action edge,
no full validate on stop, no destructive load, unchanged RAM1 bank count.
Non-goals: general start/switch acceptance, reusable lease pool, cold undo, window sizing,
root decoupling, canonical SD eviction, and the 60-long-slot acceptance.

Validation: native full suite with the red characterization explicitly expected; existing prewarm
fixtures; loaded-slot overdub → one stop → capacity recovery; transport stop during the wait;
restore blocked then resumed; canonical-credit refcount census. Build capture-serial and record RAM1.
Device gate: use the S1 resident-library baseline with room for canonical content, demonstrate removal
of the per-slot **reservation** slope and a 24-bar overdub when the measured capacity permits it.
Separate canonical payload, reservation and derived bytes; total pool live is not expected flat yet.
A synthetic 60-descriptor fixture covers boot admission independently of S6's content residency.

## 2. Budget cardinalities and transfer overlap

P/C/D/I are incremental bytes **per holder**, never device totals. Budget:

`fixed + N*P + Cmax*C + Dmax*D + Imax*I + Omax*O + canonical + roots + undo + visual + standby + reserve <= PSRAM`

Charge shared backing once, report which holders share it, and include transient construction peaks.
Until phase F, `roots` includes the whole-loop playback/checkpoint roots actually retained.
Until S6, `canonical` includes all resident event bodies; neither term is hidden inside reserve.

Use source-derived maxima, not a device-wide singleton assumption:

- `Cmax = Config::NUM_TRACKS` (8): one live capture per track.
- For the current admission guard (`hasPendingDeferredOverdubStop()` rejects a second admitted stop
  on that track), `Dmax = Config::NUM_TRACKS` (8). This counts independent retained jobs on eight
  tracks; scheduling only one grain at a time does not reduce residence.
- If a later task relaxes that per-track admission guard, first recompute Dmax from the maximum
  admitted jobs and slot resources. The conservative descriptor ceiling is
  `NUM_TRACKS * min(MAX_LOOPS_PER_TRACK, CaptureStopJobs::kCapacity)` (32 today), **not** evidence
  that 32 finalizers are provisioned or admissible. This change does not add stopped-interval queuing.
- `Imax = 1` for the selected interactive holder. Deferred undo work and its pinned roots remain
  charged under `undo` for their entire lifetime, including after selection moves.
- At most one inbound switch preparation per track; `Omax = NUM_TRACKS` (8) extra inbound holders.
  An outbound live/finalizing role is already counted in C/D and is never released to satisfy O.
  Replacing a queued target retires its unpublished preparation before provisioning another target
  on that track. O is the measured incremental inbound occupancy not already charged as P/C/D/I.

S1 measures sizes and simultaneous holder counts; S2.3 reports the conservative supported-envelope
budget and representative lower-concurrency cases separately. If the conservative sum does not fit,
report that result: do not shrink cardinalities silently, claim a proved N, or pre-provision extra
finalizers. A changed admission envelope returns to the architect. Size measurement may proceed
before window implementation; length-independent playback proof waits for phase F.

Validation: simultaneous live capture/finalization on separate tracks, switching on multiple tracks,
and target replacement during preparation. A single-track trace cannot prove the device budget.

## 3. Runtime-loss recovery retains the existing job

Owner before/during/after a resource wait: the **same live `CaptureStopJob` ring entry** owns `rawRange`,
its interval/slot identity, staged pass receipt, undo baseline, preview and causal source. No new retry
queue, no orphan sealed range, no re-admission through `beginDeferredOverdubStop`.

`regenerateFrozenCaptureStaging(job)` returns pending/complete/fault to the existing deferred stop
worker. On allocation refusal, retain the entry and all ownership receipts, mark regeneration pending,
and retry one declared bounded regeneration grain on eligible deferred turns. Liveness includes
this entry; load and release cannot destroy its Loop. Missing runtime does not mean missing content.
Persist the refill cursor in the job's external state; no full-range refill in one service turn.

Restore the frozen role from `job.rawRange`, its saved generation and reservation count, then rebind
runtime consumers. Never freeze/exchange the live successor's staging. Before publication, derived
scratch may be rebuilt from job-owned inputs without redoing admission. A completed commit/transfer
receipt is never replayed: after commit, resume cleanup from that receipt, not raw-pass publication.
If required authoritative input is unavailable, enter the named diagnostic fault while keeping the
job and content pinned; a timeout is not cancellation and no empty pass is published.

`accepted_stop_park_terminal` reports stalled progress once per episode; it does not dequeue the job.
Retirement is legal only after normal completion has consumed or released every job-owned resource,
or an explicit session reset follows the existing destruction rules. The phrase “never stalls holding
an owner” means no silent, non-retried early-out; it does not permit abandoning ownership on OOM.

Validation: inject missing runtime before preparation and after the commit receipt; hold allocation
unavailable across repeated turns, then restore capacity. Assert unchanged ring identity/extent and
refcounts during the wait, successor staging untouched, one publication/undo entry, and eventual
normal retirement. Fault injection with missing authoritative input must preserve ownership and
report the fault, never publish empty content or recycle the entry.

## 4. Playback dependency boundary

`windowed-playback-generation` is a **planned, uncreated sibling**, not an available implementation
contract. This change retains S4c only as contract/acceptance definition. Changing publication units,
window currency, playback root consumers or LSV1 checkpoint authority requires that sibling's
proposal/design/spec/tasks, DEC/reassessment and validation before Apply. No placeholder implementation.

S2.1/S2.5/S2.6 playback behavior and S4d playback standby integration wait for that contract; S2 size
accounting and cold-admission work do not. S5 may demote inactive roots but reactivation must prepare
whatever the **current** playback authority requires off-clock, including a whole-loop root until F.
Charge it honestly; do not claim length independence from cold demotion alone. S5's current-root
compatibility ends when F replaces the consumers and its parity gates pass.

S6 retains its independent canonical-residency gate. Final S7 scaling/continuity and archive require
both S6 and the sibling playback gate; early slices have their own narrower gates.

## 4a. Transport timing validity (architect decision 2026-09-23, `design.md` Revision 24)

The `transportEpoch` field is **removed** and the stored-epoch requirement is withdrawn. Timing validity
is **live re-derivation + identity validation + consumption**, not a compared counter:

- the destination is re-derived from live transport state, and the boundary tick from the live clock —
  never from a stored absolute tick;
- the drive validates `(kind, slot, interval)` before acting, so a superseded request does not bind later
  content;
- history is covered by **consumption**: an accepted stop is consumed by the admission that binds it, by
  the transport-stop entry that retargets or completes it, or by a synchronous completion.

S4f.4 — transport timing validity closes only when four paused-worker transition tests pass: Transport
Stop → Start; seek/reset/MIDI Start; load/replacement; repeated retries (exactly one commit). If a test
reveals missing history detection, add a **consumed generation check for that specific gap** — not a
general epoch.

## 5. Interaction composition and supersession

`PendingInteractionRequest` owns the live interval's stop obligation. Its interval/slot identity
cannot be overwritten by a later request. **stop > switch > start is execution order, not permission
to discard different accepted actions.** A switch's prerequisite stop uses the same obligation.

Keep destination requests in their existing owners: `SlotStateMachine` retains the pending switch
and replacement policy; `TrackManager` retains `pendingRecord` / `pendingRecordSlot` for a pending
start (with an externally held start-mode discriminator for record versus overdub). These are
bounded, latest-target requests, not a queue of sealed intervals. The interval latch refers to them;
clearing a successfully admitted stop clears only the stop obligation. Commit a switch only after
that stop binds to the old slot, then execute a start only after its target is active and provisioned.
A start waiting without a live interval is owned by the manager, not assigned a fictitious interval.

The manager's in-flight query includes all three carriers, including during the no-live-interval gap.
Deferred eligibility includes pending switch/start work; exactly-once consumption occurs only at each
operation's commit point. Start mode/identity is covered by S3.0 external placement pins.

Explicit transitions (same track unless stated):

- Stop(A), then Switch(B): retain Stop(A) and B; admit A's stop, then commit B once.
- Stop(A), Switch(B), Start(B): additionally retain Start(B); execute in that order without re-press.
- Stop(A), Start(A): finish/admit A's stop, then start a **new interval** on A; never erase the stop
  in `startOverdubbing`'s reset block before its admission.
- Another Stop before that pending Start executes: the newer Stop **supersedes the unexecuted Start**,
  reported explicitly; preserve the earlier stop and pending switch. Do not manufacture a zero-length
  interval. If Start already committed, Stop targets the new interval instead.
- Switch(B), then Switch(C) before either commits: C supersedes B; retire B's unpublished preparation.
  A start explicitly bound to B remains on B (it is not silently retargeted); cancel that start as
  superseded by the changed target and report it. A subsequent Start(C) is accepted normally.
- Duplicate same operation/identity: no-op. A distinct operation is not a duplicate just because the
  interval matches. Transport Stop cancels unexecuted starts, retargets the live stop to Stopped,
  and leaves selection requests to the existing stopped-selection commit path; seek/MIDI Start
  invalidate timing coordinates via epoch without resurrecting a canceled start.
- Different tracks retain independent requests and jobs; one track's stop never clears another's.

Validation: cover each transition with the worker paused, then drain; assert execution or explicit
supersession for every request, correct target/interval, exactly-once commits, and no stale target.
These semantics clarify DEC-078's explicit-supersession exception; they do not promise an unbounded
history of every button press or add a queue of stopped intervals.
