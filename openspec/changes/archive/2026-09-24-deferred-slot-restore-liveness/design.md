## Context

See `proposal.md` — Why. This section records only the current state and the constraints that shape
the approach. All code claims below were read from the tree on 2026-09-23 (`feature/runtime-working-set-ownership`,
clean, HEAD `22ae260c`); device claims are cited to the sessions named in
`docs/Runtime/PROJECT_STATE.md` and `openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`
§ Device session — RWSO-24CAP.

**Owners and call paths that matter.**

- `StorageManager` owns the restore queue, the load job, the deferred save, and the persistence drain.
  No ownership moves in this change.
- `LoopSlotRestoreQueue.cpp` — `pendingLoopSlotRestores_` (capacity `NUM_TRACKS * MAX_LOOPS_PER_TRACK`),
  `loopSlotRestoreAttempted_`, `loopSlotPayloadOnSdInRam_`; helpers `appendBootLoopSlotRestore`,
  `queueDeferredLoopSlotRestore`, `popNextDeferredLoopSlotRestore`, `popFocusDeferredLoopSlotRestore`,
  `peekFirstPendingLoopSlotRestore`, `reprioritizeDeferredLoopSlotRestoreEntries`.
- `LoadLoopJob.cpp` — `beginLoadLoopJob`, `ensureActiveLoadLoopJobSelected`, `stepLoadLoopJob`,
  `stepSubmittedLoadJobsImpl`, `parkActiveLoadLoopJob`, `canRunBackgroundLoadLoopNow`,
  `deferredSaveBlockedByActiveSlotLoadSd`, `deferredSaveBlockedByPostLoadCommitHoldoff`.
- `main.cpp` — the boot drain frame: `setBootTitleLoadDrain(bootSlotLoadRefreshPending)` at entry;
  on `bootInteractiveReady()` it clears the flag, calls `enqueueRemainingLoopSlotRestoresFromSd()`,
  and from then on calls `enqueueRemainingLoopSlotRestoresFromSd()` every turn.
- `CurrentSetBootLoad.cpp` — the boot scan builds the boot-playback queue;
  `loadCurrentSetBundleAndActiveLoopSlots` clears the queue, clears the load job and resets
  `loopSlotRestoreAttempted_` — the **only** reset of that array.
- `DeferredSaveScheduler.cpp` / `SyncDrainSaveState.cpp` — the deferred-save gate and the blocking
  drain's gate classification.

**Constraints.**

- `docs/Authority/ARCHITECTURE_RULES.md`: `StorageManager` keeps persistence and load ownership; no new
  `*Manager`; no allocation on the action edge; no SD write on the stop hot path; deferred save via the
  existing admission path, never a full validate on stop.
- `docs/Authority/PROJECT_INTENT.md` §1 and DEC-081: the music never stops. A playing track is never
  stopped or held up for storage work, and audible playback is not a persistence gate.
- `scripts/ram1_budget.json` + `scripts/check_ram1_budget.py` enforce the post-link RAM1 floor
  (`padding 840` in the recorded line). **Any new ITCM code or state is a bank-cross risk**, and
  `code + padding` is already at the FlexRAM boundary — the placement rule for this change is
  FLASH-resident helpers (`STORAGE_PERSIST_MEM` / `FLASHMEM`) and externally allocated state.
- `lazy-slot-hydration` is the normative owner of hydration/deferred-load behavior; its current text
  *permits* the stall this change removes, so the delta modifies it rather than adding a parallel spec.
- HITL: a behavior observable over serial needs a registered or reused stimulus
  (`scripts/hitl/stimuli/`); canonical stimuli are never edited to chase a new condition. The existing
  temporary RWSO stimuli and the canonical base preset are the starting points.

**Read-only root-cause findings (evidence for TBD-1).** These are why the defect is treated as
liveness, with the queue as the spine:

| # | Mechanism (read from code) | Observed symptom |
|---|---|---|
| C1 | `canRunBackgroundLoadLoopNow()` is false while `sequencerRunning` or any track records or is audibly playing; `ensureActiveLoadLoopJobSelected` then starts nothing | `Queuing boot playback loop slot restore 6 pending; first 5/0` and `first 4/0` with `DIAG,lcr,skip,restore`; no `LoadLoopJob done` for those entries |
| C2 | `stepSubmittedLoadJobsImpl` parks a non-focus job when `!backgroundAllowed`, and only ever steps again when the same predicate allows | a job can begin while stopped and be parked for the rest of a playing session |
| C3 | **WITHDRAWN 2026-09-23 (G2 red-team finding 5).** Recorded as "`enqueueRemainingLoopSlotRestoresWithPayloadSource` never re-offers after the one-shot scan". Withdrawn because the boot scan at `CurrentSetBootLoad.cpp` already walks **every** `(track, slot)` and offers each one that has an SD payload, and a request blocked later **stays queued** in `pendingLoopSlotRestores_` — it does not need another offer to exist. No state was produced in which a request is neither queued, active/parked, nor terminal. Re-arming would have duplicated the retry policy, not fixed starvation; the real defects are C1/C2 (never served) and C5/C6 (mis-served). | — (withdrawn; the `DIAG,lcr,skip,restore` symptom is attributed to C1) |
| C4 | `popNextDeferredLoopSlotRestore` takes the priority head and has no track/slot filter, so a head that cannot start blocks every entry behind it | one owned slot can stall the whole background fill |
| C5 | `deferredSaveBlockedByActiveSlotLoadSd()` is true while **either** job instance holds a `SlotLoadSession` or an open SD file, and `drainPersistenceWorkBlocking` advances only the *save* scheduler, so it cannot make the restore progress | `Persistence drain stuck after 33 iterations` → `Clear aborted: could not complete deferred save first` |
| C6 | `markLoopSlotRestoreAttempted` runs on every failure path (missing file, bad token, open/seek/read failure, parse failure), and `loopSlotRestoreAttempted_` is reset only by a full boot load | a transient failure latches that `(track, slot)` `EMPTY` for the power cycle; a later boot restores it, so boots disagree |

C6 remains an **evidence-backed hypothesis**: no log in hand captures the failure path that set the
flag. C4 and C5 are read directly from code. Separating C1–C6 is Packet A's job.

### The commit stop-gate is a hard boundary on this change (G2 finding 1)

`src/main.cpp` `runDeferredLoadAndDisplayFrame` does **not** admit load work freely during timing-critical
playback. It computes

```cpp
const bool activeCommitPending = StorageManager::hasLoadLoopJobCommitPending();
const bool loadJobPending =
    loadJobRequested && MidiServiceDrain::admitLoadJobChild(timingCriticalTrackActive, activeCommitPending);
```

and `MidiServiceDrain::admitLoadJobChild(timingCritical, commitPending)` denies the child while
`timingCritical && commitPending` (the tick domain's "one child per turn" rule). The comment above the
gate states the reason verbatim: *"Publish currently transfers a complete loaded slot and frees parser
storage synchronously. It has no content-independent active-transport bound yet, so leave it pending until
the STOPPED policy admits it."*

That is not this change's gate to remove. It is the **explicit open clause of
`resumable-midi-boundary-playback-maintenance` task 3.3**, which requires splitting Commit preparation
and cleanup into continuations, measuring the complete atomic Commit body, and denying active admission
for any Commit without a content-independent bound; its status records that the unbounded publish and the
ownership-releasing cleanup are deliberately kept pending under active transport, and that the
Commit-body measurement clause is still missing. **This change therefore treats bounded Commit as an
external prerequisite**, not as something it implements:

- **What this change may promise:** a request is never *lost* and never *silently* terminated; it is
  either served, or it is in a **named wait** whose reason and expected clearing condition are reported.
  "Served" means its preparation advanced or its terminal was reached **within the window in which the
  gates admit** — a wait may legitimately last until the commit gate (runtime-maintenance 3.3) or the
  job-capacity phase clears, and that is a valid, observable outcome rather than a failure.
- **What this change must NOT promise:** that a restore physically completes while a track is
  timing-critical. That claim belongs to 3.3. A design that promised it would be falsified by the very
  gate the reviewer cited.
- **Consequence for the spec's liveness requirement:** the obligation is "reaches exactly one terminal
  outcome, or is *waiting on a named gate that the change can name and bound*", not "commits during
  playback". The drain contract (D4) is correspondingly about the drain **advancing** the restore rather
  than waiting on a service that cannot run.


## Goals / Non-Goals

**Goals:**

- Make a queued restore a bounded liveness obligation with an explicit identity, named waits, and
  exactly one terminal outcome per request, observable without a bisect.
- Remove the specific live stalls (C1, C2, C4, C5, C6) in evidence order, with a discriminating fixture
  per stall; C3 is withdrawn and produces no work.
- Make the blocking drain **advance** the restore it waits on — active and parked instances — instead of
  waiting on a service path that cannot run while the drain owns the thread.
- Keep `StorageManager` ownership, the RAM1 budget, and MIDI-path timing unchanged.
- Leave `runtime-working-set-ownership`'s contract untouched; this change only removes its blocker.

**Non-Goals:**

- No new manager, queue framework, or persisted format; no change to the SD catalog write path.
- No change to boot-playback-first commit, the hydration lifecycle, or the priority *order*.
- **Bounding Commit or cleanup under active transport.** That belongs to
  `resumable-midi-boundary-playback-maintenance` task 3.3 and is named as an external prerequisite;
  `MidiServiceDrain::admitLoadJobChild` is not relaxed here.
- No cancellation or supersession of a restore to unblock a save (that would need its own `DEC`).
- No attempt to make restore synchronous, or to restore every SD payload before interactive ready.
- No re-work of `runtime-working-set-ownership` device legs inside this change; they stay with that
  change and are re-run after this one lands.
- Not deciding TBD-2/TBD-3 from the proposal here; they are recorded in Open Questions as deferrable.

## Decisions

### D1 — The unit of correctness is the restore **request**, with explicit identity, waits and terminals

The queue entry becomes a tracked request. Three things are separate and must not be conflated
(G2 finding 4):

**Identity.** A request is `(track, slot, requestGeneration, setEpoch)`. `requestGeneration` starts at 0 and
increments every time `(track, slot)` is cleared, reset, or committed by a *different* owner (a new record
or a load), so a later session reusing the same coordinates is a **different request**. `setEpoch`
identifies the loaded current set, so a set change invalidates outstanding requests. Terminal records are
keyed by the full identity, not by `(track, slot)` — the previous draft's `(track, slot)` key could not
tell two sessions apart, which is exactly the reviewer's finding.

**Lifecycle.** A request is queued, running, waiting, or terminal, with exactly one state at a time.
Reading/parsing and other admitted job phases are running; yielding a runnable slice preserves progress
without inventing a wait reason. A real denying gate transitions the request to a named wait. A terminal
is recorded exactly once when the request completes or is superseded.

**Waits are not terminals.** `waiting` states carry a named reason and are the only states a request may
sit in while work is impossible: `waiting_slot_owned` (a live owner holds the slot), `waiting_capture`
(a track on that slot is recording — the SD-session exclusion), `waiting_commit_gate` (the
actively-admitted STOPPED-only Commit bound described above), and `waiting_retry` (a transient failure
within its bounded attempt count). A wait **never** becomes a terminal by passing an iteration or
stickiness budget.

**Terminals.** Exactly one of: `committed` (payload restored), `unnecessary` (the slot already has
committed content, or the payload is absent/invalid), `refused` (a named, non-retryable reason), or
`superseded` (the set changed, the slot was cleared, or a newer request for the same coordinates replaced
it). `superseded` and `refused` are distinct so a set change is never reported as a failure.

**Identity validation before any supersession (G2 tightening).** A `requestGeneration` advance SHALL NOT be
blind. Load-failure paths call `resetLoopSlotToEmpty()` internally, so "advance the generation on every
reset" would supersede the very request that is meant to retry. The rule:

- every reset/clear site SHALL distinguish an **external** reset (a user CLEAR, a set change, a different
  owner committing that `(track, slot)`) from the **loader's own cleanup** on behalf of a live request;
- the loader's own cleanup keeps the request identity and moves it to `waiting_retry` (or to its terminal
  when the attempt bound is exhausted) — it SHALL NOT read as `superseded`;
- the identity SHALL be validated for a **queued**, an **active**, and a **parked** job, and again
  **immediately before publication**, so a request cannot bind or publish into a slot whose owner changed
  while the job was in flight. A mismatch before publication results in `superseded`, consumes nothing from
  the replacement content, and reports the reason.

*Why:* C4/C5/C6 are all "the request left, or could never leave, the queue without an outcome the user can
see", and recording deferral (the `DIAG,lcr,skip,restore` gate) must stay a **wait**, never a terminal.

*Alternative considered:* keep `(track, slot)` terminal state and treat a later session as a new attempt.
Rejected — it makes exactly-once per request unprovable, because the second session's outcome overwrites
the first's.

*State placement:* compact per-slot record (`state` + `reason` + `requestGeneration`) plus a small
`setEpoch`, stored outside ITCM next to the existing restore arrays. No dynamic allocation; no growth in
the per-turn cost. A ring of recent terminals is **not** required by O3.

### D2 — Background restore preparation is admitted by reason and bounded by the commit stop-gate

Add a background-restore invocation reason with its own bounded per-turn budget, serviced in the existing
deferred frame, so **read/parse preparation** advances while playback continues. Recording keeps its
existing deferral (Open Question O1).

The contract is deliberately bounded by the stop-gate in Context, and — per G2 finding 2 — **the
preparation promise is conditional, not unconditional**:

- preparation (open, read, parse) SHALL be admissible under active transport within its declared budget
  **while no job is commit-pending**;
- **commit and cleanup SHALL NOT be claimed as active-transport operations by this change** — they remain
  gated by `MidiServiceDrain::admitLoadJobChild` until
  `resumable-midi-boundary-playback-maintenance` task 3.3 lands its bounded Commit continuations;
- **the gate is global, not per-job (corrected).** `hasLoadLoopJobCommitPending()` returns true when
  **either** the active **or** the parked instance is in `Committing`/`Cleanup`, and `main.cpp` then denies
  the **entire** load child for that turn. So once one request reaches commit under active transport,
  **no other request's preparation advances** either. This change SHALL therefore define a second named
  wait — `waiting_job_capacity` (another request holds the commit/cleanup phase) — and SHALL NOT claim that
  queue-head skipping solves this scheduler-level blockage, because it does not. A design that narrows the
  gate per-phase is a separately reviewed change (Open Question O5), not something this one infers;
- a request whose preparation is complete SHALL enter `waiting_commit_gate`, not a terminal and not an
  unbounded spin, and SHALL be applied at the first turn the gate admits.

**Scope consequence, stated plainly.** With the global gate unchanged, the deliverable of this change is
**diagnosable preparation and named waits**, plus the drain-side advancement that applies when the gate
admits. It does **not** by itself unblock CLEAR during continued playback of another track; that outcome
depends on runtime-maintenance task 3.3. The change's acceptance criteria are written to that scope, and
A2.7 records the dependency rather than papering over it.

*Why:* C1/C2 are the primary starvation, and DEC-081 established that audible playback must not gate
storage work. The honest scope of that ruling here is *preparation while no job is commit-pending, plus
named waits for apply and for job capacity*, because both the apply and the per-turn child admission are
bounded only by 3.3.

*Alternative considered:* promise full completion during playback by relaxing the commit gate in this
change. Rejected — the gate exists for a measured reason (a synchronous publish with no
content-independent bound), relaxing it would regress the display/MIDI stalls it prevents, and its
resolution is owned by another change with an open task.

*Alternative considered:* narrow `admitLoadJobChild` to deny only the committing job's turn while allowing
another job's read/parse. Not taken here: it changes the tick-domain one-child rule and its MIDI-boundary
reasoning, so it needs its own review (O5).

*Bound:* the slice budget is a declared constant analogous to the mid-pass one-chunk flush; the MIDI
service budget and the `overflow_free` gate are standing acceptance conditions.

### D3 — Queue service skips a non-startable head, preserving the blocked entry

Add a startability-aware selection that walks the priority order and starts the first request that may
begin, leaving a blocked entry queued with its reason. The existing pop helpers keep their
done-test semantics and the S2b.5 `queuedRestoreMayBegin` liveness predicate stays the gate.

*Why:* C4. `popNextDeferredLoopSlotRestore` has no filter and the two `popNext` call sites cannot see
past the head; one live-owned slot stalls everything behind it.

*Alternative considered:* remove the done-test from the pop and re-queue. Rejected — re-queueing is
inert today (`queueDeferredLoopSlotRestore` refuses unless `needsSlotLoad`, which is false once
attempted), which is exactly the S2b.5 trap.

### D4 — The blocking drain **advances** the restore it waits on (G2 finding 2)

Observing progress is not enough, and the earlier draft's D4 was falsified by the code:
`drainPersistenceWorkBlocking` (`SyncDrainSaveState.cpp`) loops on
`StorageManager::processDeferredSaveState(state); yield();` and never invokes the load scheduler
(`StorageManager::processDeferredLoopSlotRestore`, whose only production caller is the main loop's
deferred-load frame). While the drain runs, the main loop is *inside* the drain, so the normal service
path is unavailable — a drain that merely watches a restore cannot cause it to finish, and
`deferredSaveBlockedByActiveSlotLoadSd()` stays true.

**The drain SHALL service the restore directly.** Concretely, the contract is:

1. **Service both job instances.** `deferredSaveBlockedByActiveSlotLoadSd()` is
   `SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile()`, and
   `parkActiveLoadLoopJob`/`swapLoadLoopJobs` moves the `session` pointer, so a **parked** job keeps its
   session alive. The drain's wait can therefore outlive any active job. The service step SHALL consider
   the active **and** parked instances.
2. **Name the service path.** A single drain-side step (a `StorageManager`-owned entry, not a new owner)
   SHALL perform, per iteration and within a declared budget: the same selection/admission the deferred
   frame uses (`selectSubmittedLoadJobs` + `stepSubmittedLoadJobs`) plus one bounded advance of an
   already-admitted job, so preparation, commit and cleanup can all complete while the drain owns the
   thread. It SHALL reuse the existing job state machine and phase gates; it SHALL NOT add a second
   loader, a second queue, or a parallel admission policy.
3. **Admission uses the same actual runtime eligibility in both service paths (G2 finding 1 —
   corrected).** The earlier draft claimed `timingCriticalTrackActive` is false during a blocking drain
   "because CLEAR stops the selected track". **That is wrong and is withdrawn:**
   `MidiButtonActions::handleClearTrack` stops only the **selected** track, while `timingCriticalTrackActive`
   is computed in the main loop over **all** tracks
   (`t.isRecording() || t.isAudiblePlaybackActive()`) and the global clock may still be running. So during a
   CLEAR with another track playing, the real runtime eligibility is still *timing-critical*, and the commit
   gate would still deny — or, if the drain bypassed the gate, it would admit an **unbounded** commit/cleanup
   while another track plays, which is the regression the gate exists to prevent.
   The drain SHALL therefore derive its admission from the **same** predicate the normal path uses
   (`admitLoadJobChild` fed by the all-tracks timing-critical computation and
   `hasLoadLoopJobCommitPending()`), not from an assumption about who stopped. Two consequences:
   - where the gate admits (nothing timing-critical), the drain advances the restore to completion;
   - where the gate denies, the drain **preserves** the request and reports the named wait
     (`waiting_commit_gate`) rather than forcing the commit. Successful CLEAR in that case is
     **runtime-maintenance task 3.3's** outcome, not this change's.
   The drain SHALL NOT invent a content-independent commit bound, and SHALL NOT bypass the gate to make
   CLEAR succeed.
4. **Restore work is charged in the drain's budget and progress accounting (G2 finding 3).** The existing
   budget estimates persistence work only (`SyncDrainBudgetInput`: queued work steps/bytes, mid-pass depth,
   finalize steps) and `SyncDrainProgressSnapshot` carries persistence fields, so a large restore could
   consume the drain's step limit or look stalled while legitimately progressing — and the stuck detector
   would abort a drain that was making real progress. The contract:
   - `SyncDrainBudgetInput` gains the restore term (active/parked restore slice steps and estimated SD
     bytes, and whether the restore is the only reason the drain is waiting), so `maxSliceSteps` covers it;
   - `SyncDrainProgressSnapshot` gains restore progress (phase, bytes read, parse position, or a monotonic
     restore-progress counter) and `madeSyncDrainProgress` compares it, so restore progress counts as
     progress and cannot trip the stuck path;
   - the drain SHALL NOT admit **unlimited fresh queue work** while draining: it starts at most the requests
     it needs to clear the save gate (bounded per iteration), so a drain cannot become an unbounded
     background fill;
   - the existing post-load commit holdoff is honored (`deferredSaveBlockedByPostLoadCommitHoldoff`), with
     the drain reporting `BlockedByPostLoadHoldoff` rather than spinning when the holdoff is what it waits on.
5. **MIDI-service behaviour.** The drain is already a blocking operation that yields; servicing a restore
   inside it SHALL NOT lengthen that beyond the corrected drain budget, and SHALL keep the existing
   `overflow_free` standing gate.
6. **Termination.** If the restore reaches a terminal or a wait the drain cannot clear (`waiting_capture`,
   `waiting_commit_gate` under a denying gate), the drain SHALL report that named reason rather than
   spinning the stuck budget; `BlockedBySlotLoad` becomes `BlockedByRestore(<owning wait reason>)`.
7. **The caller contract: `saveState` is a three-way outcome, and a caller may not treat a wait as
   success (G2 finding — caller/holdoff).** `StorageManager::saveState` returns only `bool`
   (`include/StorageManager.h:56`) and has exactly **two** callers — `MidiButtonActions::handleClearTrack`
   (`src/MidiButtonActions.cpp:752`) and `saveNewSetInternal()` in `SavedSetIo.cpp` (save-new-set pre-flight). Today
   every `false` is treated as failure at both, and CLEAR emits `Clear aborted: could not complete deferred
   save first` and returns. A named wait therefore cannot coexist with "0 `Clear aborted`" unless the
   return value and its callers change. The contract:
   - the drain's result is one of **`Completed`**, **`Blocked(reason)`** (a named, *temporary* wait the
     drain could not clear within its budget), or **`Failed(reason)`** (a real failure);
   - `saveState` keeps its `bool` as a thin wrapper (`Completed` only) so no caller silently gains new
     semantics; a new detailed entry point exposes the three-way result, and both call sites SHALL use it;
   - **a blocked CLEAR is an explicit refusal, not silent success and not a slot reset.** The caller SHALL
     log the named reason, SHALL NOT reset the slot, and SHALL NOT report success while persistence is
     pending. The owed save stays owed and is retried by the normal scheduler;
   - **retention (an accepted-interaction CLEAR latch) is a non-goal of this change**, recorded as O7: the
     project's accepted-interaction rule (DEC-078) is about playback interactions, and extending it to a
     destructive slot reset needs its own decision. This change refuses and reports;
   - therefore **A3.7 — CLEAR device verification's zero-`Clear aborted` criterion is corrected to zero *unexplained* aborts**: a
     named refusal is an expected, non-failing outcome until runtime-maintenance 3.3 lands, and the
     criterion distinguishes the two by reason.
8. **The finite post-load holdoff: bounded wait, or reported — never an abort that discards progress.**
   `cleanupLoadLoopJobPublish` sets `postLoadCommitSaveHoldoffUntilMs_ = millis() + 100u`
   (`LoadLoopJob.cpp:479`), which `deferredSaveBlockedByPostLoadCommitHoldoff()` tests. Without a rule, the
   "nothing else sounding" case can restore successfully and then return **without saving**. The contract:
   - the holdoff is a **finite, named** interval; the drain SHALL wait it out **when `Completed` is
     otherwise reachable**, within a declared holdoff wait budget bounded by the remaining drain wall-clock
     budget (so `maxWallClockUs` still bounds the caller);
   - during that wait the drain SHALL service MIDI (`midiHandler.handleMidiInput()` plus the existing
     `yield()` pattern), again bounded, so the wait cannot starve the MIDI path;
   - if the wait would exceed the drain budget, the drain returns `Blocked(post_load_holdoff)` rather than
     blocking indefinitely, and the caller follows rule 7 (refuse by name);
   - the constant SHALL be named rather than a literal, so the wait policy and the timeout are one
     declaration.


9. **Single-owner drain and callback safety.** MIDI service during a drain is a callback boundary,
   not a passive delay: controller NoteOn/NoteOff messages reach `MidiButtonProcessor` and can execute
   `MidiButtonActions` immediately. Before rule 8 is implemented:
   - `StorageManager` SHALL own one scoped drain-in-progress guard, acquired before changing shared save,
     budget, progress, or result state and released on every exit (Completed, Blocked, Failed). A nested
     detailed drain call returns `Blocked(drain_in_progress)` immediately; the compatibility bool wrapper
     returns false. Neither nested call may overwrite the outer result, budget, or owed-save state.
   - Destructive/storage callback entry points SHALL check that guard **before side effects**, including
     before CLEAR stops a track, undo/redo replaces content, or set/revision operations mutate storage.
     Audit the real MIDI dispatch routes and list the guarded entry points in the execution packet;
     checking only `saveState` is insufficient because an action can mutate before calling it or bypass it.
     Conflicting requests are explicitly refused with `drain_in_progress`, with no new deferred-action
     queue and no silent success. A conflict means resetting/replacing the outer caller's target content,
     changing the set, or starting another operation on the drain-owned storage state.
   - Normal musical input and transport messages remain serviced. After every MIDI service/yield boundary,
     re-read all-track capture/timing eligibility, restore identity and commit admission before another
     restore/save step. Do not cache an idle verdict across a callback. If a new gate denies progress,
     preserve the save/restore obligations and return the appropriate named Blocked result.
   - The outer destructive caller retains its original target identity across the drain and revalidates
     it before mutation. A focus change must not redirect CLEAR to another slot; a changed target/set
     identity produces a named refusal, not a reset of replacement content. Existing restoration of
     playback on refusal must also respect the revalidated target and live transport state.
   - Guard storage has explicit initialization and measured placement under the RAM1 budget; a raw
     uninitialized DMAMEM flag is not an acceptable guard. No new manager or persistence owner is added.

*Why:* C5. The 2026-09-23 classifier named the gate but the gate was a state the caller could not leave;
CLEAR still aborted. Naming a wait was necessary but not sufficient.

*Alternative considered:* have the drain cancel or supersede the in-flight restore. Rejected as the
default because it changes persistence ownership/durability semantics and needs its own `DEC`
(proposal TBD-3). Making the drain service the restore is strictly smaller and preserves ownership.

*Test requirement (from the finding):* the fixture SHALL call the **real blocking drain** and SHALL NOT
advance the restore externally between assertions.

### D5 — A transient restore failure is retryable within the session; the latch is not the fix

Separate "we tried and it failed" from "there is nothing to try". A failed restore is retryable up to a
declared bounded attempt count per power cycle, then reaches the `refused` terminal with its reason;
`unnecessary` is reserved for genuinely absent/invalid payloads.

*Why:* C6. A single transient SD failure currently latches the slot for the whole power cycle and makes
boots disagree.

*Alternative considered:* leave the latch and re-probe more aggressively at boot. Rejected — the latch
is also what stops a permanent retry loop on a genuinely bad file; a bounded attempt count addresses
both.

### D6 — Observability is capture-tier and event-shaped

Emit one capture line per restore terminal (with `(track, slot)` and reason), plus an on-demand
queue/state probe reusing the existing `probeActiveLoadLoopJob` shape. Boot reconcile reports a named
outcome (`ok` / `repaired` / `failed`) instead of only the current warning.

*Why:* every device round in this area has been blocked by having to infer state from absence of
lines. Event-shaped lines are near-zero cost when idle, which satisfies the spec's hot-path clause.

*Alternative considered:* a per-turn state line. Rejected — the `DIAG,stop_grain` history in
`CURRENT_WORK.md` shows per-turn families overflow the capture ring and evict the rows the run is read
for.

### D7 — Acceptance fixtures assert the desired behavior; old-state observations are separate (G2 finding 3)

The earlier draft's Packet A asserted the **present broken state** ("the request does not commit",
"nothing starts"). That is inverted: such a test passes on the broken build and fails after the repair,
which is the opposite of the required red→green direction. The corrected rule:

- **Acceptance fixtures assert the desired behavior.** They are red on the current build (because the
  desired behavior is absent) and green after the fix. Packet A writes them first, in the default gate's
  final state, red-proven.
- **Characterization/diagnostic observations are not acceptance fixtures.** Where the present mechanism
  needs recording (a gate's identity, an iteration count, which reason was reported), it is kept as a
  separately named diagnostic assertion or a recorded observation in the change review — never as the
  acceptance oracle.
- **Coexistence with the initial green-suite gate.** Because the desired behavior is currently absent,
  the fresh acceptance fixtures are red by construction. They therefore start **registered but excluded
  from the default gate** behind an explicit, named env flag in the change's own suite, and each one is
  **promoted into the default gate in the same commit as the fix that turns it green** (the pattern this
  repo already used for `WORKING_SET_RED`, `ONLY_COMPANION` and `REENTRY_RED`). The initial green-suite
  gate is satisfied by the suite being green *with the known-red acceptance set excluded and named*, not
  by weakening or deleting any assertion. A red fixture may not be deleted, generalized, or have its
  threshold relaxed to make a build pass.

*Why:* this is the reviewer's finding 3, and it is the same rule DEC-080 and the `WORKING_SET_RED`
precedent already encode; the earlier draft violated it.

## G2 execution packets (one per slice)

Slices are ordered so every behavior edit is preceded by a discriminating fixture, per DEC-080 and
`docs/Authority/IMPLEMENTATION_READINESS.md`. Each slice is one buildable commit boundary and carries
its own validation. Numeric task ids live in `tasks.md`; the packet ids here are the review sections.

### Packet A — desired-behavior acceptance fixtures + observability (no behavior change)

- **Dependency closure:** `test/test_storage_manager_load` (already links `LoadLoopJob.cpp`,
  `LoopSlotRestoreQueue.cpp`, the in-memory SD card and `test_support/track_harness.inc`), plus the
  `SESSION_CAPTURE` restore-terminal lines and the queue probe.
- **Transition oracle:** for each of C1, C2, C4, C5, C6, one acceptance fixture that drives the
  **production** entry and asserts the **desired** behavior, so it is red today and green after the fix:
  (C1/C2) a queued background request's preparation advances or it is in a named wait, instead of being
  skipped indefinitely by transport;
  (C4) a startable request behind a non-startable head begins;
  (C5) the real blocking drain terminates the restore it waits on and completes;
  (C6) a transient failure is retried rather than latched.
  Where a fixture must run through the scheduler, it SHALL go through the production admission path
  (`runDeferredLoadAndDisplayFrame`'s decision seam, or the extracted `hasRealtimeDeferredIdleWork`
  predicate plus the load-job child admission), not only direct `stepSubmittedLoadJobs()` calls.
- **Diagnostic (not acceptance):** the present behavior of each gate — which reason is reported, the
  stuck-iteration count, the `DIAG,lcr,skip,restore` trigger — recorded as a named diagnostic assertion
  or in the change review, so the fix's effect is measurable without making the diagnostic the oracle.
- **Controllable fixtures:** test-controlled transport state (already a variable in the suite),
  test-controlled `isAudiblePlaybackActive` / `isRecording` inputs, a synthetic queue with a
  deliberately non-startable head, an injected SD read failure, a controllable
  `loopSlotRestoreAttempted_` state, and a controllable commit-gate state.
- **Outcome semantics:** each fixture names which mechanism it discriminates and which **desired**
  outcome it expects; a fixture that cannot separate two mechanisms is reported, not merged.
- **Evidence:** the red run per acceptance fixture (with the exclusion flag named), the full native suite
  green **with that exclusion**, the capture-serial build, and the RAM1 line before any behavior change.
- **Exit criteria:** C1, C2, C4, C5, C6 are each either covered by a red desired-behavior fixture or
  explicitly withdrawn with the observation that falsified it (C3 is already withdrawn by finding 5).

### Packet B — background restore liveness (D2, D3, D5)

- **Prerequisite:** Packet A fixtures for C1, C2, C4 and C6 red.
- **Transition oracle:** with transport running and a track audibly playing, a queued background restore
  is **served while no job is commit-pending** — its preparation advances and it reaches either a terminal
  or a named wait (`waiting_commit_gate`, `waiting_job_capacity`) within a bounded number of production
  turns, and never stalls indefinitely without a reason; with a non-startable head, a lower-priority
  startable request begins and the head keeps its reason; a transient read failure retries and then
  reaches `refused` with that reason after the bounded attempt count. **Not asserted here:** that a
  restore commits during playback (requires runtime-maintenance 3.3), or that a second request's
  preparation advances once another is commit-pending (the gate is global — see finding 2). A dedicated
  case covers the two-request shape and asserts the `waiting_job_capacity` reason rather than progress.
- **Must preserve:** MIDI continuity and the per-turn service budget; recording still defers the load;
  the S2b.5 liveness guard (a finalizing predecessor still blocks a load into *its* slot only);
  exactly-once commit; no new ITCM-resident code.
- **Evidence:** green fixtures, full native, capture-serial build with the RAM1 post-link check, and
  the observed capture lines.
- **Exit criteria:** the spec requirements "Every queued slot restore reaches exactly one terminal
  state or a named wait", "Restore queue progress is independent of queue-head blockage", and
  "Background restore preparation advances while the gates admit" hold under test.

### Packet C — drain advances the restore (D4)

- **Prerequisite:** Packet B green, since the drain step reuses the same selection/admission the deferred
  frame uses. **Also a hard external prerequisite for any "CLEAR completes during another track's
  playback" claim:** runtime-maintenance task 3.3's bounded commit/cleanup (finding 1's corrected form).
  This packet's advancement claim holds only where the real eligibility gate admits.
- **Transition oracle:** calling the **real blocking drain** with a restore in flight terminates the
  restore and completes the save, with **no external advancement of the restore between assertions**;
  both an **active** and a **parked** job instance are serviced; with **another track playing** and a
  restore commit pending, the drain **preserves** the request and reports
  `BlockedByRestore(waiting_commit_gate)` rather than forcing an unbounded commit or aborting;
  a restore in a wait the drain must not break reports its named reason instead of spinning.
- **Accounting oracle (finding 3):** a **small pending save behind a multi-slice restore** completes
  through the real drain without tripping `maxSliceSteps` or the stuck detector; `madeSyncDrainProgress`
  reports progress from restore motion alone; the drain admits at most the bounded fresh queue work it
  needs; and a post-load holdoff is reported as `BlockedByPostLoadHoldoff`.
- **Must preserve:** DEC-081 (audible playback is not a gate), the recording gate, the mid-pass
  one-chunk flush, and the single persist grain's existing (unbounded-by-design) latency.
- **Evidence:** green fixtures plus a device run showing a CLEAR that completes (or is refused with the
  named wait) with a restore in flight, and zero unexplained CLEAR aborts or stuck drains; named refusals are expected outcomes.
- **Exit criteria:** the spec requirement "The blocking persistence drain advances the restore it waits
  on" holds where the gate admits, and the named-wait requirement holds where it does not.

### Packet D — boot reconcile outcome (D6)

- **Prerequisite:** Packet A's observability.
- **Transition oracle:** a malformed index produces the named failure outcome plus the existing
  self-healing repair; a healthy index produces `ok`; the two are distinguishable in the capture.
- **Must preserve:** `reconcileSetIndexOnSd`'s self-healing behavior and the SD temp-write fix.
- **Evidence:** a native fixture over the existing `test_storage_manager_load` / storage suites, the
  capture-serial build, and a device boot showing the outcome line.
- **Exit criteria:** the spec requirement "The boot catalog reconcile outcome is reported" holds.

## Risks / Trade-offs

- **[RAM1 bank cross] The observable/terminal state and the new admission reason add ITCM code and
  `.bss`.** → Every new function is `STORAGE_PERSIST_MEM`/`FLASHMEM`; terminal state lives beside the
  existing restore arrays outside ITCM; the post-link `check_ram1_budget.py` gate is run on the
  capture-serial build in every slice and the floor is never auto-updated.
- **[MIDI starvation] Admitting background restore while playing consumes SD and CPU.** → One
  declared bounded slice per turn, serviced after MIDI service, with the existing `overflow_free`
  gate and the standing MIDI-gap acceptance; the slice is tuned by measurement, not guesswork.
- **[Durability] A save that waits on a restore can delay a CLEAR the user is pressing.** → The drain
  completes as soon as the restore terminates, and the fix aims to make that bounded; if cancellation
  proves necessary it returns as a `DEC`, not as a silent change.
- **[Scope creep into runtime-working-set-ownership] The device legs become re-runnable here.** →
  Non-goal: this change is judged only by its own fixtures plus one CLEAR/restore device run.
- **[C6 fix causing retry storms] A permanently bad payload retried every turn.** → Bounded attempt
  count per power cycle with a named terminal.
- **[False root cause] If Packet A falsifies a mechanism, the follow-on packets are built on sand.** →
  Packet A is explicitly allowed to withdraw a mechanism and re-target (C3 was already withdrawn by
  review finding 5 before any code); Packet B/C tasks are checkable only once their listed Packet A
  fixtures are red.
- **[Over-promising apply] The spec's liveness requirement could be read as "commits during playback".** →
  The requirement is written as *terminal **or** named wait*, and the commit stop-gate is named as an
  external prerequisite (`resumable-midi-boundary-playback-maintenance` 3.3). Any task that would need
  bounded Commit is blocked on 3.3 rather than reimplementing it.
- **[HITL regression risk] The change touches a path with zero native coverage before this change.** →
  Building the native presence *is* Packet A's first task, and no behavior edit is allowed before its
  fixture is red.

## Migration Plan

No data migration and no format change. Behavior changes ship behind their fixtures inside this
change; rollback is reverting the slice commit, because the restore/liveness state is per-boot and
nothing about it is persisted. The only persisted interaction is the existing save path, which this
change does not modify.

## Open Questions

- **O1 (deferrable, TBD-2 restated).** Whether a background restore's **preparation** may also advance
  while a track is *recording*, or keeps deferring. Deferrable because the observed defect is the
  *playing* case, and the recording deferral is separately justified by SD-session ownership; resolving
  it later changes a bound, not the spec or the packet structure.
- **O2 (deferrable).** The exact bounded slice size and attempt count. These are measurements from
  Packet B/C, deliberately not pre-decided.
- **O3 (deferrable).** Whether the request record needs per-attempt history or only the last state.
  Start with the last state plus the attempt count; expand only if a device diagnosis needs it.
- **O4 (resolved by this revision).** Whether the drain needs its own restore-service path or can merely
  observe progress. Resolved: it must **service** both job instances (D4). The only sub-question left
  is the exact drain-side slice budget, which is a measurement (O2), not an architectural choice.
- **O5 (not deferrable — needs its own review before any implementation relies on it).** Whether
  `admitLoadJobChild` should be narrowed to deny only the **committing** job's turn while allowing another
  job's read/parse, replacing the current global deny. This is the only way this change could deliver
  preparation progress *after* a request reaches commit under active transport; it changes the tick-domain
  one-child rule and its MIDI-boundary reasoning, so it requires its own reassessment and fixture rather
  than being inferred here. Until then the `waiting_job_capacity` wait is the honest contract.
- **O6 (decision recorded, not a question).** This change delivers **diagnosable preparation and named
  waits**; it does not by itself unblock CLEAR during another track's playback. That outcome requires
  runtime-maintenance task 3.3 (bounded commit/cleanup). Recorded here so a later reader does not read the
  Packet B/C gates as promising the stronger result.
- **O7 (non-goal; needs its own decision).** Whether a **blocked CLEAR should be retained and executed**
  when the blocker clears (the accepted-interaction pattern of DEC-078), instead of being refused with a
  named reason and needing a second press. DEC-078's rule was written for playback interactions; extending
  it to a destructive slot reset changes clear's user-visible contract and its undo/restore semantics, so
  this change refuses-and-reports and records the alternative. Overtaken when the caller contract is
  revisited with 3.3.
