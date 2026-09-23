# Tasks — deferred-slot-restore-liveness

Scope: `docs/Runtime/CURRENT_WORK.md`; motivation and non-goals: `proposal.md`; how: `design.md`.
Every slice follows the [G2 execution-packet workflow](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
and its packet in `design.md` § G2 execution packets. Establish the discriminating production-path
fixture **before** the behavior edit, then record actual native/build/device evidence before checkoff
(DEC-080). A listed fixture or command is not evidence until it has run and its output is recorded here
or in the change's review file.

**Revision 2 (2026-09-23) — G2 adversarial contract review returned BLOCKED on six findings; all six are
corrected in this file, `proposal.md`, `specs/`, and `design.md`:**

1. **Background admission did not unblock commit.** Fixed: the promise is scoped to **preparation**;
   bounded Commit is named as an external prerequisite
   (`resumable-midi-boundary-playback-maintenance` **task 3.3**, below: *runtime-maintenance 3.3*), a prepare-complete request waits in
   `waiting_commit_gate`, and the acceptance fixtures run through the production admission seam
   (A2.2, A2.7).
2. **The blocking drain had no way to advance a restore.** Fixed: the drain must **service** the active
   and parked job instances (A3.1/A3.2), and the fixture calls the real blocking drain with no external
   advancement (A1.7).
3. **The "red" fixtures asserted the existing bug.** Fixed: every acceptance fixture now asserts the
   **desired** behavior; old-state observations are separate diagnostics; the red set is excluded from
   the initial green gate behind a named flag and promoted per fixture (A1.2–A1.7, A1.10, A1.11).
4. **Request identity and terminal semantics conflicted.** Fixed: identity is
   `(track, slot, requestGeneration, setEpoch)`, **wait** states are separate from **terminal** outcomes,
   a wait never becomes a terminal by elapsed turns, recording deferral stays a wait, and set changes
   yield `superseded` (A2.1, spec delta).
5. **C3 (one-shot enqueue) was not proven defective.** Withdrawn with the code citation (A1.4); its
   implementation task is deleted, not deferred.
6. **Archive readiness permitted unfinished required verification.** Fixed: A5.5 requires every mandatory
   gate taken and green, with exactly one optional gate named (A5.5, A5.6).

**Before any firmware edit:** run [`docs/Templates/DECISION_REVIEW.md`](../../../docs/Templates/DECISION_REVIEW.md),
the preflight, and the transfer protocol in [`docs/Authority/ARCHITECTURE_RULES.md`](../../../docs/Authority/ARCHITECTURE_RULES.md);
search [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md) for DEC-021, DEC-025, DEC-081 and the
`lazy-slot-hydration` lineage. Rerun the SD temp-write lineage
([`docs/Plans/sd_temp_write_truncation_refinement.md`](../../../docs/Plans/sd_temp_write_truncation_refinement.md))
because its landed repair is a prerequisite assumption of Packet D.

**Every task:** no allocation on an action edge; no SD write on the stop hot path; keep new code
FLASH-resident (`STORAGE_PERSIST_MEM` / `FLASHMEM`) and new state outside ITCM, because the RAM1 floor
is enforced post-link by `scripts/check_ram1_budget.py` against `scripts/ram1_budget.json` and the
recorded line is `code 392,328 / padding 888`. Record the RAM1 line for every slice that links new code.
Hold the `lazy-slot-hydration` invariants (boot-playback-first, the `UNLOADED → HEADER_READY →
COMMITTED → DERIVED_READY` lifecycle, priority ordering) plus the modified requirements in
`specs/lazy-slot-hydration/spec.md`.

**Fixture/native/build/device status are recorded separately** (DEC-080 / D0.5 style). A missing or
skipped required case is not a pass; never weaken an oracle or threshold to make a slice green.

## 1. Packet A — characterize and observe (no behavior change)

- [x] A1.1 **Native restore harness increment: link the queue and the load job into one controllable
      suite.** Extend `test/test_storage_manager_load` (or add its sibling) so a test can enqueue,
      inspect and drain restore requests through the production entry points
      (`StorageManager::selectSubmittedLoadJobs`, `stepSubmittedLoadJobs`,
      `enqueueRemainingLoopSlotRestoresFromSd`) with a controllable transport, controllable
      `isRecording` / `isAudiblePlaybackActive` inputs, and a controllable SD payload set.
      **Verify:** the suite builds and stays green, and one smoke case drives a queued request to a
      committed slot through production calls only. Record the case count and the linked production TUs.
      → **DONE 2026-09-23:** `test/test_storage_manager_load/load_and_restore_harness.h` (+ suite) —
      production body-writer payloads, explicit runtime-eligibility controls, bounded production turn
      driver. `pio test -e native -f test_storage_manager_load`: **8 cases / 8 succeeded / 0 failures**;
      smoke case commits its own payload through production calls only. Linked production TUs and the
      measured default-state fact: [`REVIEW.md`](REVIEW.md) § Common packet controls / A1.1.

- [x] A1.2 **[C1 — transport-gated background admission] desired-behavior fixture, red.** Drive a queued
      background restore with the transport running and one track audibly playing, through the production
      admission path (the extracted `hasRealtimeDeferredIdleWork` predicate plus the load-job child
      admission — **not** `stepSubmittedLoadJobs()` directly). Assert the **desired** outcome: while the
      gates admit, the request's preparation advances or it reaches a terminal; and in every case it is
      never left in neither state — within a bounded number of turns it is either served or in a **named
      wait** (`waiting_commit_gate`, `waiting_job_capacity`). **Verify:** the case is **red on current
      `src/`** because the request is neither served nor in a named wait, and it is registered in the
      change's named exclusion set (A1.11) so the default gate stays green. Record the assertion output
      and the exclusion flag. No `src/` change in this task.
      → **DONE 2026-09-23:** `test_c1_transport_gated_background_admission_desired`, green in the default
      gate by exclusion (A1.11), **red under `RESTORE_LIVENESS_RED=1`**: *"neither served nor progressing
      after 64 turns (pending=1, phase=255, jobActive=0)"* — the request stays queued and unexplained,
      which is the defect, not a red test that asserts it. Admission goes through the production
      selection/child-admission calls, not a direct job step. Named waits (`waiting_commit_gate`,
      `waiting_job_capacity`) do not exist yet, so "served or progressing" is Packet A's oracle; A2.2
      replaces it with the named-wait oracle. Shape note: focus is on the **stopped** track while another
      track plays, so the denial is the transport gate, not the focus path. Evidence: [`REVIEW.md`](REVIEW.md).

- [x] A1.3 **[C2 — parked job never re-serviced] desired-behavior fixture, red.** Begin a non-focus
      background job while idle, then set the playing condition and drive turns; assert the **desired**
      behavior: the job receives further service or is reported in a named wait, and it is not parked
      indefinitely without a reason. **Verify:** the case is red on current `src/`; the parked job's
      identity is asserted across turns; record the probe output.
      → **DONE 2026-09-23:** `test_c2_parked_job_is_serviced_or_named_after_transport_starts`, promoted
      with A1.2 under `RESTORE_LIVENESS_RED`; **red**: *"never serviced after 64 turns (parkedEnd=1,
      parkedPhase=1, ownedTurns=64, activelyOwnedTurns=0, pending=0, attempted=0, committed=0)"* — the
      job holds the request for all 64 turns while the active instance never holds it, i.e. the parked
      instance is never advanced. `StorageManager::probeParkedLoadLoopJob` (new, observation-only) makes
      the parked instance measurable: `probeActiveLoadLoopJob` cannot see it, yet it is what keeps
      `deferredSaveBlockedByActiveSlotLoadSd()` true. Packet A's oracle is `served || activelyOwned`;
      Packet B replaces it with the named-wait oracle.

- [ ] A1.4 **[C3 — one-shot background-fill admission] WITHDRAWN (review finding 5).** Do not implement a
      fixture or a behavior change for C3. The boot scan already offers every `(track, slot)` with a
      verified payload, and a blocked request remains in `pendingLoopSlotRestores_`, so no state exists in
      which a request is neither queued, active/parked, nor terminal. Re-arming would duplicate the retry
      policy without addressing starvation. **Verify:** the withdrawal is recorded in the mechanism ledger
      (A1.9) with the code citation; the corresponding implementation task is deleted, not deferred.

- [x] A1.5 **[C4 — head-of-line blockage] desired-behavior fixture, red.** Queue a priority-head request
      whose slot is held by a live owner plus a lower-priority startable request; assert the **desired**
      behavior: the startable request begins and the blocked head stays queued with its reason. **Verify:**
      the case is red on current `src/` (today nothing begins); the assertion distinguishes "head blocked"
      from "queue empty"; record it.
      → **DONE 2026-09-23:** `test_c4_blocked_priority_head_does_not_stall_a_startable_request`, promoted
      under `RESTORE_LIVENESS_RED`; **red**: *"the startable request behind a blocked head was never served
      or owned after 32 turns (served=0, unowned=1, pending=2, committed2=0, attempted2=0, active=0,
      parked=0, headStillQueued=1, headOwned=1)"* — both entries stay queued while the lower-priority slot
      is free, and the assertion separates "head blocked" (`headStillQueued=1`, `headOwned=1`) from "queue
      empty" (`pending=2`). **Mechanism correction to `design.md` C4:** the pop drains *every* done entry
      from the front (`isLoopSlotRestoreAttempted || hasCommittedPasses`), so a **committed** head is
      skipped rather than blocking; the real stall needs a head that fails the done-test **and**
      `noLiveOwnerForSlot`. The fixture builds that pair with a capture-interval owner on an uncommitted
      slot plus the S4f.1 latch. Also fixed a suite defect this surfaced: `setUp` never cleared the
      `LoadLoopJob` instances, so an earlier fixture's parked job survived into the next fixture and owned
      its request; `StorageManagerInternal::clearLoadLoopJob()` now runs in `setUp`.

- [x] A1.6 **[C6 — attempt latch] desired-behavior fixture, red.** Inject a transient SD read/open failure
      for one payload, drive the restore to its failure path, then make the payload healthy; assert the
      **desired** behavior: the request is retried and reaches `committed` (a persistent failure instead
      reaches `refused` after the bounded attempt count). **Verify:** the case is red on current `src/`
      because the slot is latched for the session; record the injected failure and the observed state.
      → **DONE 2026-09-23:** `test_c6_transient_failure_is_retried_not_latched`, promoted under
      `RESTORE_LIVENESS_RED`; **red**: *"a healed payload was never retried after a transient failure
      (committed=0, attempted=1, pending=0, active=0, parked=0)"*. Injection is a production-path
      transient: the slot file is truncated by 3 bytes, and because the save token lives at the END of
      the file the **read** stage comes up short before any parse. The fixture first asserts the failure
      path was actually reached (`attempted=1`) with the payload still uncommitted, then heals the
      payload from a byte snapshot and drives 32 further turns with no external advancement. New harness
      helpers: `snapshotSlotPayloadBytes`, `replaceSlotPayloadBytes`, `truncateSlotPayload`.

- [ ] A1.7 **[C5 — drain cannot advance the restore] desired-behavior fixture, red.** Owe a deferred save,
      hold a `LoadLoopJob` in flight, and call the **real blocking drain** — with **no external
      advancement of the restore between assertions**. Assert the **desired** behavior: the drain
      terminates the restore and completes the save; and that a wait the drain must not break is reported
      as that named reason rather than a generic stuck verdict. **Verify:** the case is red on current
      `src/` (the drain never services the load scheduler); record the iteration count as a **diagnostic**,
      not as the oracle.

- [ ] A1.8 **Observability, event-shaped (per `design.md` D6).** Add capture-tier lines: one per restore
      **state transition** (terminal or named wait) with the request identity and reason, and an on-demand
      queue/state probe reusing the existing `probeActiveLoadLoopJob` shape; add the boot reconcile named
      outcome (`ok` / `repaired` / `failed`) alongside the existing warning. **Verify:** native cases assert
      the probe's fields for a queued, a blocked, an in-flight, a prepare-complete-waiting and a terminal
      request, and assert the reconcile outcome mapping; full native suite green; capture-serial build
      SUCCESS with the RAM1 line recorded. **Do not** emit a per-turn state line (capture-ring overflow
      precedent).

- [ ] A1.9 **Packet A review and mechanism ledger.** Record, for C1, C2, C4, C5, C6, one of: covered by a
      red desired-behavior fixture `<case>`, or withdrawn with the observation that falsified it; record
      C3's withdrawal; record the TBD-1 conclusion and whether any mechanism is a *different* root cause
      than `design.md` states. **Verify:** the ledger is written into the change review, and every Packet
      B/C task below is either unblocked by a reproduced mechanism or explicitly re-scoped. Do not check
      A1.9 while any mechanism is unreviewed.

- [ ] A1.10 **Packet A gate.** Full native suite green **with the named red-acceptance exclusion set in
      place** (count and exclusion list recorded); `teensy41-capture-serial` build SUCCESS with the
      post-link RAM1 line recorded; Packet A red/withdrawn status table complete. **Verify:** the exact
      commands and outputs are recorded; no behavior change is claimed by this packet; no red fixture was
      deleted, generalized, or had a threshold relaxed to make the suite green.

- [x] A1.11 **Red-acceptance coexistence rule (review finding 3).** Define and wire the named exclusion set
      for the red desired-behavior fixtures (a dedicated env flag in the change's own suite, following the
      `WORKING_SET_RED` / `ONLY_COMPANION` / `REENTRY_RED` precedent). Each fixture is promoted into the
      **default gate in the same commit that turns it green**; the flag is deleted when the set is empty.
      **Verify:** the default `pio test -e native` run is green with the flag unset; a run with the flag set
      shows exactly the listed fixtures red; the promotion is checked off per fixture in the slice that
      fixes it.
      → **DONE 2026-09-23:** `RESTORE_LIVENESS_RED` (test-read env var) gates the known-red set; the
      fixture is registered unconditionally, so the flag only decides whether it **runs**. Default gate
      **8 cases / 8 succeeded / 0 failures**; `RESTORE_LIVENESS_RED=1` → **10 cases / 1 failed / 8
      succeeded**, the single failure being the listed red fixture. Promotion bookkeeping: each A1.x
      fixture is recorded here as promoted in the Packet B/C slice that turns it green.

## 2. Packet B — background restore liveness (D2, D3, D5)

- [ ] A2.1 **Request identity and state separation (D1), with validated generational advance.** Introduce
      the request identity `(track, slot, requestGeneration, setEpoch)`, explicit **queued/running** states,
      and the separate **wait** states
      (`waiting_slot_owned`, `waiting_capture`, `waiting_commit_gate`, `waiting_job_capacity`,
      `waiting_retry`) versus the four **terminals** (`committed`, `unnecessary`, `refused`, `superseded`);
      key the record by the full identity, invalidate outstanding requests on a set change as `superseded`,
      and retire or subsume `loopSlotRestoreAttempted_`. **Generation advance SHALL be validated, not blind
      (G2 tightening):** load-failure paths call `resetLoopSlotToEmpty()` internally, so advance the
      generation **only for an external** reset (user CLEAR, set change, a different owner committing that
      `(track, slot)`) and keep the identity for the restore path's **own** failure cleanup (which becomes
      `waiting_retry`). Validate the identity for a **queued**, an **active** and a **parked** job, and
      again **immediately before publication**. Stored beside the existing restore arrays outside ITCM.
      **Verify:** native cases assert running read/parse work and service yields retain identity/progress
      without a fictitious wait or terminal, and a denying gate transitions to its named wait;
      exactly one terminal per identity; a wait never converts to a terminal
      from elapsed turns alone; recording deferral is a wait; a reused `(track, slot)` is a different
      request; a set change yields `superseded`; **the restore path's own failed attempt resets the slot but
      yields `waiting_retry`, not `superseded`**; and a job whose slot owner changed before publication is
      superseded **publishing nothing**; full native suite green; RAM1 line recorded.

- [ ] A2.2 **Background-restore preparation admission (D2), scoped to the global gate.** Add a bounded
      background-restore service slice so a queued background request's **preparation** advances while
      playback continues, serviced after MIDI service with its own declared budget; keep recording
      deferred, keep the focus/High path unchanged, and **do not touch the commit stop-gate**
      (`MidiServiceDrain::admitLoadJobChild`). A prepare-complete request waits in `waiting_commit_gate`;
      **because the gate is global (G2 finding 2), a request whose sibling job is commit-pending waits in
      `waiting_job_capacity`** — this change SHALL NOT claim that queue-head skipping solves that
      scheduler-level blockage. **Verify:** A1.2 and A1.3 flip from red to green **for the
      no-commit-pending case**; a case asserts a prepare-complete request under active transport enters
      `waiting_commit_gate` and applies when the gate admits; **a two-request case asserts that with one
      request commit-pending the other is in the named `waiting_job_capacity` wait and is served once the
      phase clears**; a case asserts the per-turn work stays within the declared budget.

- [ ] A2.3 **Startability-aware queue service (D3).** Select the first startable request in priority
      order, leaving a blocked entry queued with its reason; keep the S2b.5 `queuedRestoreMayBegin`
      semantics and the existing done-tests. **Verify:** A1.5 flips red→green; a case asserts priority
      order is preserved among startable requests and that a blocked entry is still served once its owner
      retires. **Not claimed:** that this resolves the `waiting_job_capacity` blockage, which is a
      scheduler-level gate, not queue ordering.

- [ ] A2.4 **Bounded retry then named outcome (D5).** Replace the permanent attempt latch with a declared
      bounded attempt count per power cycle; a transient failure retries (up to the bound) and a persistent
      one ends in `refused` naming the reason, while absent/invalid payloads stay `unnecessary`.
      **Verify:** A1.6 flips red→green; cases assert retry-then-commit for a transient failure,
      retry-then-refuse for a persistent one, and no retry storm (attempts bounded per turn and per power
      cycle).

- [ ] A2.5 **Packet B device gate.** Build and flash `teensy41-capture-serial` (ask the owner first — never
      flash unprompted), run the canonical base stimulus on the authorized scratch slot, and reboot.
      **Verify:** every boot payload has a **state** (terminal or named wait) reported; no payload is left
      with neither; `overflow_free=True`; MIDI gap within the standing bound; and either the previously
      `EMPTY` slot restores, or the log shows it in a named wait with the reason — the latter is a valid
      outcome only if the reason names the commit gate and **runtime-maintenance 3.3** is still open. Record the capture path and
      the exact lines. If the device is not available, this task stays open and Packet B is not closed.

- [ ] A2.6 **Packet B gate.** Full native suite green; A1.2/A1.3/A1.5/A1.6 green **in the default gate**
      (promoted per A1.11, not left behind the flag); capture-serial RAM1 line recorded; A2.5 evidence
      recorded. **Verify:** no oracle was weakened; the slice's commit boundary is one buildable commit.

- [ ] A2.7 **Commit-bound prerequisite (review finding 1) — record, do not implement.**
      `resumable-midi-boundary-playback-maintenance` **task 3.3** (bounded Commit preparation/cleanup) is
      a named external prerequisite for any acceptance criterion that requires a restore to **apply**
      under active transport. Record the dependency in the change review, keep the affected acceptance
      criteria scoped to terminal-or-named-wait, and do not relax `MidiServiceDrain::admitLoadJobChild`
      here. **Verify:** the prerequisite is named in the change review and in the S1/S2b handoff notes; a
      grep shows this change's acceptance criteria do not claim applying a restore under timing-critical
      transport; no `src/main.cpp` commit-gate change exists in this change's diff.
      *(Referenced elsewhere as **runtime-maintenance 3.3**, to avoid confusion with this change's own
      Packet C tasks.)*

**Revision 3 (2026-09-23) — the re-review returned BLOCKED on three further gaps; all three are corrected
here, in `proposal.md`, `specs/`, and `design.md`:**

1. **CLEAR does not make the whole runtime stopped.** Withdrawn assumption: D4 originally claimed the drain
   is the "STOPPED-policy path because CLEAR stops the selected track".
   `MidiButtonActions::handleClearTrack` stops only the **selected** track, while `timingCriticalTrackActive`
   is computed over **all** tracks (`isRecording() || isAudiblePlaybackActive()`), so another track playing
   leaves the runtime timing-critical. The drain now derives admission from the **same actual eligibility**
   and, where the gate denies, preserves the request and reports the named wait (A3.1, A3.7 shape b).
2. **One waiting commit blocks every other request's preparation.** The gate is **global**:
   `hasLoadLoopJobCommitPending()` is true when either instance is `Committing`/`Cleanup`, and `main.cpp`
   then denies the **whole** load child. The preparation promise is now explicitly conditional and gains the
   `waiting_job_capacity` wait; a two-request fixture covers it, and this change no longer implies that
   queue-head skipping solves it (A2.2, A2.3).
3. **Restore work was absent from the drain's budget and progress accounting.** `SyncDrainBudgetInput` and
   `SyncDrainProgressSnapshot` carry persistence fields only, so a large restore could exhaust
   `maxSliceSteps` or look stalled while progressing. Restore terms are added to both, with a bounded
   fresh-queue admission and named holdoff reporting (A3.3, A3.5).

**Scope consequence, stated once and binding:** this change delivers **diagnosable preparation and named
waits**, plus drain advancement where the gate admits. It does **not** by itself unblock CLEAR during
another track's playback — that requires **runtime-maintenance task 3.3** (bounded commit/cleanup). O5
(narrowing the gate per phase) is a separate, reviewed change. A **named wait is a valid, successful
outcome** of this change, and may last until the external prerequisite is resolved.

**Revision 4 (2026-09-23) — the third review returned BLOCKED on the drain's return behavior at its
callers; corrected here, in `proposal.md`, `specs/`, and `design.md`:**

1. **A named wait still became a failed CLEAR.** `StorageManager::saveState` returns `bool`
   (`include/StorageManager.h:56`) and has exactly two callers — `MidiButtonActions::handleClearTrack`
   (`src/MidiButtonActions.cpp:752`) and `SavedSetIo.cpp:331` — and every `false` is treated as failure
   (`Clear aborted: could not complete deferred save first`). A3.7 could therefore not require both a
   named refusal and zero `Clear aborted`. **Fixed:** the drain gains a three-way **
   completed / temporarily blocked / failed** result (D4 rule 7); `saveState` stays a `Completed`-only
   wrapper; both callers use the detailed entry point; a blocked CLEAR **refuses, names the reason, leaves
   the slot untouched, and leaves the save owed** (A3.4); and A3.7's criterion is **zero *unexplained*
   aborts**, where a named refusal is expected. Retention as an accepted interaction is **O7**, not built.
2. **The finite post-load holdoff could discard the save.** `cleanupLoadLoopJobPublish` arms a **100 ms**
   `postLoadCommitSaveHoldoffUntilMs_` (`LoadLoopJob.cpp:479`), so even the "nothing else sounding" shape
   could restore and then return without saving. **Fixed:** D4 rule 8 names the constant and specifies a
   **bounded wait that services MIDI**, or the named holdoff outcome when the wait would exceed the budget
   (A3.5).
3. **Wording tightened:** the remaining unconditional "never starved" / "served to a terminal" phrasing is
   removed from the scope statements and Packet A, since the detailed contract correctly permits waits that
   last until the external commit prerequisite is resolved.

## 3. Packet C — the blocking drain advances the restore (D4)

- [ ] A3.1 **Drain-side restore service on the real eligibility (D4, G2 finding 1).** Give the blocking
      drain a restore-service step that uses the existing selection/admission
      (`selectSubmittedLoadJobs` + `stepSubmittedLoadJobs`) plus one bounded advance of an admitted job,
      servicing the **active and parked** instances. The step SHALL derive its admission from the **same
      all-tracks runtime eligibility** the normal frame uses (fed into `admitLoadJobChild`), **not** from
      an assumption that CLEAR left the runtime non-timing-critical — `MidiButtonActions::handleClearTrack`
      stops only the **selected** track, and other tracks may still play or record. **Verify:** A1.7 flips
      red→green; a case calls the **real blocking drain** with no external advancement between assertions
      and asserts the save completes **where the gate admits**; a case with a **parked** job holding the
      session asserts the wait clears; and **a case with CLEAR on one track, another track playing, and a
      restore commit pending asserts the drain preserves the request and reports the named commit-gate
      wait instead of admitting an unbounded commit**.

- [ ] A3.2 **Named drain verdict (D4).** Replace the generic `BlockedBySlotLoad` verdict with
      `BlockedByRestore(<request wait reason>)` so the drain reports *why* it could not advance, and keep
      the existing `BlockedByCapture` / `BlockedByPostLoadHoldoff` distinctions (the holdoff is reported by
      name, not spun). **Verify:** cases assert the named reason for capture-active, commit-gate,
      job-capacity and retry waits; no case asserts a generic `Stuck` where a named reason is available.

- [ ] A3.3 **Restore work in the drain budget and progress accounting (D4, G2 finding 3).** Extend
      `SyncDrainBudgetInput` and `SyncDrainProgressSnapshot` / `madeSyncDrainProgress` so that (a) the
      drain's step budget covers active and parked restore work, (b) restore progress counts as progress
      and cannot trip the stuck detector or `maxSliceSteps`, and (c) the drain admits **at most the bounded
      fresh queue work it needs** — a drain must not become an unbounded background fill. **Verify:** a case
      runs a **small pending save behind a multi-slice restore** through the real drain and asserts it
      completes without a stuck report or budget exhaustion; a case asserts restore motion alone registers
      as progress; a case asserts a drain does not drain the whole restore queue; RAM1 line recorded.

- [ ] A3.4 **Caller contract: completed / blocked / failed (D4 rule 7, G2 caller finding).** Give the drain
      a three-way result and expose it without silently changing existing callers: `saveState` keeps its
      `bool` as a thin `Completed`-only wrapper, and both call sites
      (`MidiButtonActions::handleClearTrack`, `saveNewSetInternal()` in `SavedSetIo.cpp` (save-new-set pre-flight)) use the detailed
      entry point. On **temporarily blocked**, CLEAR SHALL refuse the operation, log the named reason,
      leave the slot untouched, and leave the owed save owed; it SHALL NOT report success while persistence
      is pending, and SHALL NOT reset the slot. Retention (an accepted-interaction CLEAR latch) is O7 and
      is **not** implemented here. **Verify:** cases assert a blocked drain yields a named, non-success
      outcome at the real CLEAR caller; **a case asserts a blocked CLEAR does not reset the slot and does
      not clear the obligation**; a case asserts a failed outcome is distinguishable from a blocked one;
      the real `saveNewSetInternal()` caller honors the blocked outcome rather than treating it as failure
      or success, and does not continue saving a new set while the pre-flight drain remains blocked.

- [ ] A3.4a **Drain re-entry and callback safety (D4 rule 9; prerequisite to holdoff polling).**
      Add the StorageManager-owned scoped guard before any shared drain mutation. Nested detailed calls
      return `Blocked(drain_in_progress)` without changing the outer state; the bool wrapper returns false.
      Audit MIDI-dispatched destructive/storage entry points, record their names and conflict checks,
      and reject conflicting callbacks before side effects (including CLEAR's early stop). Preserve
      normal musical input/transport service. Revalidate all-track eligibility and request identity after
      each callback boundary; retain/revalidate the outer caller's original target before resetting it.
      **Fixtures first:** inject a second CLEAR through the real MIDI/controller dispatch during holdoff;
      assert maximum drain depth one, no nested reset/stop, unchanged outer result/budget, and save still
      owed until actual completion. Separately inject transport Start and capture activation; assert the
      next step honors live eligibility, no denied commit/save executes, and the obligation survives a
      named block. Change focus and assert the outer CLEAR is not redirected. Exercise a conflicting
      undo/set-replacement action and assert rejection before target/storage mutation. Exercise Completed,
      Blocked and Failed exits followed by a new drain; assert the guard is always released. Do not replace
      the injected production callbacks with a success stub. **Verify:** named fixture evidence, full
      native suite, capture-serial build and guard placement/RAM1 line recorded; all assertions default-on
      with the implementation.

- [ ] A3.5 **The finite post-load holdoff is waited out, not aborted (D4 rule 8).** Name the holdoff
      constant (`LoadLoopJob.cpp`'s 100 ms `postLoadCommitSaveHoldoffUntilMs_`), and let the drain wait it
      out when `Completed` is otherwise reachable, bounded by the remaining wall-clock budget and
      **servicing MIDI** (`midiHandler.handleMidiInput()` plus the existing yield pattern) during the wait,
      only after A3.4a — drain re-entry and callback safety is implemented and verified;
      if the wait would exceed the budget, return the named holdoff wait. **Verify:** a case where restore
      **cleanup starts the holdoff** asserts the save still completes (not an abort with progress
      discarded) for the "nothing else sounding" shape; a case with the holdoff pushed beyond the budget
      asserts the named holdoff outcome; a case asserts MIDI is serviced during the wait.

- [ ] A3.6 **Preserve the DEC-081 and capture behavior.** Assert in cases that audible playback is still
      not a persistence gate, that a recording track still parks the multi-frame writers, and that the
      mid-pass one-chunk flush still runs during a take. **Verify:** the named regression cases pass; no
      change to `isCaptureRecordingForPersistence` semantics.

- [ ] A3.7 **Packet C device gate.** With a deferred save owed and a restore in flight, press CLEAR on the
      device — in both shapes: (a) nothing else sounding (including a restore whose cleanup starts the
      holdoff), and (b) **another track audibly playing**. **Verify:** shape (a) completes; shape (b)
      either completes or is recorded as the named commit-gate wait **with `runtime-maintenance 3.3` still
      open**. **0 *unexplained* `Clear aborted`**: a named refusal (the reported reason names the gate) is
      an expected outcome and is not counted as an abort; an abort with no named reason, or a reset slot
      after a blocked drain, fails the gate. **0** `Persistence drain stuck` / `drain blocked`. Record the
      capture paths. If cancellation turns out to be necessary, STOP and return to design for the `DEC`
      (proposal TBD-3) rather than implementing it here.

- [ ] A3.8 **Packet C gate.** Full native suite green; A3.1–A3.6 cases, including A3.4a — drain re-entry and callback safety, green; RAM1 line recorded;
      A3.7 evidence recorded for both shapes.

## 4. Packet D — boot catalog reconcile outcome (D6)

- [ ] A4.1 **Named reconcile outcome.** Report `ok` / `repaired` / `failed` for the boot reconcile,
      keeping the self-healing repair intact. **Verify:** native cases assert each outcome; a
      malformed-index case asserts the repair outcome and that a later read succeeds; the existing
      SD temp-write regression tests stay green.

- [ ] A4.2 **Packet D device gate.** One device boot with a known-healthy index and one with a
      deliberately malformed index (only with explicit permission for the SD change; quarantine rules
      apply — never rename or delete card data without same-session permission). **Verify:** the outcome
      line is present and correct in both logs; 0 unexpected `could not reconcile` after repair.
      If the malformed-index case is not permitted, record Packet D's device clause as unrun and keep
      the task open.

- [ ] A4.3 **Packet D gate.** Full native suite green; A4.1 cases green; RAM1 line recorded; A4.2
      status recorded as taken or explicitly unrun.

## 5. Closeout

- [ ] A5.1 **Re-run the unblocked `runtime-working-set-ownership` device legs** (S1.1 per-owner census,
      S1.3 slope sweep, S1.4 undo/root baseline, S1.5 device refusal point, S2b.3 post-restore baseline,
      S2b.5 device gate) using that change's pinned baseline and recipe, now that the restored set is
      stable. **Verify:** each leg's evidence is recorded in that change's
      `ARCHITECTURE-REVIEW.md`; any leg still failing returns as that change's finding, not this one's.
      This task is a handoff, not a second implementation of those legs.

- [ ] A5.2 **Content-verified PASS** for the authorized scratch slot via the
      [`hitl-content-verification`](../../../.agents/skills/hitl-content-verification/SKILL.md)
      pipeline (`dump_and_capture.py` + `verify_playback_content.py`), not a base-run verdict.
      **Verify:** `result: PASS`, `storage_matches_effective: true`, and every cycle
      `inventory_equal: true` / `exact_order: true` with 0 missing and 0 extra. If the two
      unexplained bar-0 rows recur, they are retried, never reinterpreted.

- [ ] A5.3 **Documentation closeout.** Update `docs/Runtime/PROJECT_STATE.md`,
      `docs/Runtime/CURRENT_WORK.md`, `docs/DELIVERABLE_TRACKING.md`, the
      `lazy-slot-hydration` main spec via the archive/sync step, and the affected guide
      (`docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md` or the storage guide) so no durable doc still
      describes the transport-gated restore rule this change removes
      ([`DOCUMENTATION_CLOSEOUT.md`](../../../docs/Authority/DOCUMENTATION_CLOSEOUT.md)).

- [ ] A5.4 **Decision log.** Append a `DEC-###` entry for the restore-liveness ruling (the request is
      the unit with identity and named waits, background **preparation** is admitted while playing and
      apply waits on the commit gate, the drain **advances** rather than cancels) with its device
      evidence, and record O1/O2 if they were resolved by measurement.
      **Verify:** the index row, the Next-ID bump, and the entry body are present and consistent.

- [ ] A5.5 **Required-gate completeness (review finding 6).** Every **mandatory** gate in Packets A–D and
      in the `runtime-working-set-ownership` handoff (A5.1) SHALL be **taken and green** before archive.
      **The only optional gate is A4.2's malformed-index device half** (it needs destructive-SD permission):
      it may be recorded unrun with that reason. Everything else is mandatory — including A5.2's content
      PASS, because this change's whole point is that it is reachable once the restored set is stable;
      "recorded as unrun" is not an acceptable archive state for it. If a mandatory gate cannot be taken,
      the change is **not** archive-ready and stays open with the blocking reason recorded. **Verify:** the
      closeout table lists each gate as taken+green, optional+unrun-with-reason, or open; archive proceeds
      only when nothing is in the third state.

- [ ] A5.6 **Archive.** After A5.5 shows no open mandatory gate, archive per `openspec-archive-change`.
      **Verify:** `openspec validate` passes and the main `lazy-slot-hydration` spec reflects the shipped
      behavior.
