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

- [x] A1.7 **[C5 — drain cannot advance the restore] characterization + native closure proof (AMENDED 2026-09-23).**
      **Amended scope (Revision 5).** The original text required a native red fixture that calls the real
      blocking drain with a restore in flight. Two rounds of measurement (`REVIEW.md` § A1.7) falsified
      that as a *native* obligation: retaining the real `drainPersistenceWorkBlocking` call needs most of
      the persistence stack **plus** the display stack (`DisplayManager`'s `_display` member drags the
      OLED/Adafruit-GFX font chain into the native build) and the cold edit/track-manager owners — close
      to the whole firmware, which is not a test-only closure. The task is therefore split by what can
      carry a tight oracle:
      **Native (this task):** the closure proof and the **pure-decision** oracles — drain budget and
      progress accounting for restore work (`buildSyncDrainBudget` / `madeSyncDrainProgress`, extending
      the existing `test_persistence_sync_drain_budget`), the A3.2 wait-reason classification, and the
      A3.4 three-way caller-contract mapping. Record C5's mechanism from code with citations.
      **Device (A3.1/A3.7):** the integration claim — that the drain *advances* a restore it waits on — is
      verified through the A3.7 device gate with capture evidence, not by a native fixture.
      **Verify:** the closure proof is green and the pure-decision cases are present and green; the
      mechanism is recorded in the ledger (A1.9); the A3.1/A3.7 text names the device gate as the oracle
      for the integration claim. No native red fixture for C5 is claimed.
      → **PARTIAL 2026-09-23.** Closure proof landed: `test/test_storage_drain_restore` (hardware-boundary
      suite) links the drain TU and its measured import set with the boundary stubs it needs (`PSTR`,
      `printf_P`, `print(value,base)` + `DEC/HEX/OCT/BIN`, `File::flush`, `O_CREAT`, directory handles in
      the SD stub, `RtcTime`/`loadSaveMode`/display-light boundary stubs), and runs one behaviour-free
      case. C5's mechanism is recorded in the ledger. The pure-decision cases are A3.3's work.
      → **DONE 2026-09-24.** The pure-decision cases have landed with their implementation tasks: drain
      budget/progress accounting (three cases, with A3.3), the **A3.2 wait-reason classification** (five
      cases), and the **A3.4 three-way caller-contract mapping** (three cases). Closure proof green
      (`test_storage_drain_restore`), C5's mechanism in the ledger, and A3.1/A3.7 name the device gate as
      the oracle for the integration claim. **No native red fixture for C5 is claimed**, per Revision 5.

- [ ] A1.8 **Observability, event-shaped (per `design.md` D6).** Add capture-tier lines: one per restore
      **state transition** (terminal or named wait) with the request identity and reason, and an on-demand
      queue/state probe reusing the existing `probeActiveLoadLoopJob` shape; add the boot reconcile named
      outcome (`ok` / `repaired` / `failed`) alongside the existing warning. **Verify:** native cases assert
      the probe's fields for a queued, a blocked, an in-flight, a prepare-complete-waiting and a terminal
      request, and assert the reconcile outcome mapping; full native suite green; capture-serial build
      SUCCESS with the RAM1 line recorded. **Do not** emit a per-turn state line (capture-ring overflow
      precedent).
      → **PARTIAL 2026-09-23 — landed:** (a) the **reconcile outcome mapping** is done and native-verified
      with A4.1 (`SetIndexReconcileOutcome` + 4 cases in `test/test_storage_reconcile_index`, the boot site
      logging the named outcome); (b) the **on-demand queue/state probe** is done: `StorageManager::
      RestoreStateProbe` + `probeRestoreState` report the queued count, the priority head's identity and
      **startability** (the field an operator needs to tell "head blocked" from "queue empty"), each job
      instance's owned request with phase and read progress, and whether the deferred save is held back by
      restore work. Native cases `test_restore_state_probe_reports_queue_and_job_ownership` (empty /
      queued-startable / in-flight / blocked-head, four states in one case) and
      `test_restore_state_probe_reports_the_save_blocked_by_restore` are **default-gate** (observation-only,
      green on the current tree): 10/10 in the suite, full native **2,297 / 1 skipped / 0 failures**,
      capture-serial SUCCESS with RAM1 `code 392,328 + padding 888` recorded.
      **Still open (Packet B dependency):** the capture-tier **state-transition lines** with a reason and
      the `terminal` / `prepare-complete-waiting` probe states. Those need Packet B's request identity,
      named waits and terminal record (A2.1), so the lines land with A2.1/A2.2 rather than being emitted
      with an invented state vocabulary now. The "no per-turn line" constraint is respected: the probe is
      on-demand only.
      → **DONE 2026-09-24 (second session).** The transition lines landed with A2.1/A2.2
      (`SC_RESTORE_REQUEST`/`RRQ`, six fields: track, slot, generation, epoch, state, wait, terminal) and
      `RestoreStateProbe` now reports the request-record vocabulary for the queue head and both job
      instances (`queueHead/activeJob/parkedJobRequestState|Wait|Terminal`), filled only when a record
      exists. Native cases assert the five required states: **queued** and **blocked** on the queue head,
      **in-flight** (`running`) on the owning instance, **prepare-complete-waiting**
      (`waiting_commit_gate`), and a **decided terminal** (`committed`). `test_storage_manager_load`
      **33/33**; the reconcile outcome mapping was already green with A4.1.

- [x] A1.9 **Packet A review and mechanism ledger.** Record, for C1, C2, C4, C5, C6, one of: covered by a
      red desired-behavior fixture `<case>`, or withdrawn with the observation that falsified it; record
      C3's withdrawal; record the TBD-1 conclusion and whether any mechanism is a *different* root cause
      than `design.md` states. **Verify:** the ledger is written into the change review, and every Packet
      B/C task below is either unblocked by a reproduced mechanism or explicitly re-scoped. Do not check
      A1.9 while any mechanism is unreviewed.
      → **DONE 2026-09-23.** Ledger complete in [`REVIEW.md`](REVIEW.md) § Mechanism ledger: C1, C2, C4,
      C6 each **covered by a red desired-behavior fixture**; C5 covered by **code citation plus closure
      proof** with its oracle split by Revision 5 (native pure decisions, A3.7 device gate for the
      integration claim); C3 **withdrawn before code** with the falsifying observation. **TBD-1
      conclusion recorded:** the stall is dominated by the transport gate (C1/C2), with C4 and C6 as
      independently reproduced contributors, and the between-boot `(track,slot)` census change is
      explained by C6's session latch. **Two mechanisms differ from `design.md`:** C4's trigger (a
      committed head is *skipped*, not blocking, so the stall needs a head that also fails the done-test)
      and C1/C2's relationship (one gate seen from two sides). Every Packet B/C task is either unblocked
      by a reproduced mechanism or explicitly re-scoped.

- [x] A1.10 **Packet A gate.** Full native suite green **with the named red-acceptance exclusion set in
      place** (count and exclusion list recorded); `teensy41-capture-serial` build SUCCESS with the
      post-link RAM1 line recorded; Packet A red/withdrawn status table complete. **Verify:** the exact
      commands and outputs are recorded; no behavior change is claimed by this packet; no red fixture was
      deleted, generalized, or had a threshold relaxed to make the suite green.
      → **DONE 2026-09-23.** Commands and outputs recorded in [`REVIEW.md`](REVIEW.md) § Evidence record.
      **Default gate:** `pio test -e native -f test_storage_manager_load` → 10 cases / 10 succeeded / 0
      failures (the known-red set is absent, not run-and-failing). **Red set:** `RESTORE_LIVENESS_RED=1`
      → 15 cases / **4 failed** / 10 succeeded, the four failures being exactly C1, C2, C4, C6.
      **Full native:** **2,297 cases / 1 skipped / 2,296 succeeded / 0 failures**. **Build:**
      `teensy41-capture-serial` SUCCESS, RAM1 `code 392,328 + padding 888` (48 B above the floor).
      Packet A's red/withdrawn status table is the mechanism ledger above; A1.7 is amended (Revision 5)
      rather than silently dropped, and no red fixture was deleted, generalized or threshold-relaxed.
      **Behavior change claimed by this packet: the two observation-only probes plus A4.1's reconcile
      outcome split; all other Packet A work is characterization.**

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

- [ ] A2.1 **Request identity and state separation (D1), with validated generational advance.**
      → **IN PROGRESS 2026-09-23 — record, birth/committed wiring and the identity contracts are landed
      and native-verified; the wait-state transitions and pre-publication validation remain.**
      New TU `src/StorageManager/RestoreRequestState.cpp` (+ declarations in `StorageManagerInternal.h`)
      provides the identity `(track, slot, requestGeneration, setEpoch)`, the `RestoreRequestState`
      queued/running/waiting/terminal split, the five named waits, the four named terminals, and the
      record API. **Wired into production:** the request is born `queued` at both queue-entry sites
      (`queueDeferredLoopSlotRestoreWithPayloadSource`, `appendBootLoopSlotRestore`); `beginLoadLoopJob`
      marks it `running`; `commitLoadLoopJobPublish` records the `committed` terminal. **18 cases / 18
      succeeded** in the suite, including
      `test_restore_request_state_is_driven_by_the_production_path` (queued → running → terminal through
      the real queue and job) and the identity contracts: exactly one terminal per identity (a second is
      refused), a reused `(track, slot)` is a **different** request, a set change yields **`superseded`**
      (not `refused`), and the restore path's own failure cleanup keeps the identity as a **retry wait**.
      **Two defects found by these cases and fixed:** the record's identity check was self-satisfying
      (the generation advance wrote the new counter into the record, so "the old record does not describe
      the new request" could not be observed) — fixed with an explicit `identityValid` flag; and
      `noteRestoreSetEpoch` cleared the records *after* recording their `superseded` terminal, wiping the
      outcome it had just written. **RAM1 enforced:** the first version cost 64 B of ITCM
      (`padding 888 → 824`, i.e. below the approved floor); every helper is now `FLASHMEM` and the line is
      back to `code 392,328 / padding 888`. Full native **2,305 / 1 skipped / 0 failures**.
      **Wait transition landed (same day):** a denial by a live owner now **names the wait**:
      `markRestoreRequestWaiting(..., SlotOwned)` fires at all three selection denials (the empty-idle head
      check, the focus branch's early return, and the parked/focus branch), so a blocked request reports
      `waiting_slot_owned` instead of sitting `queued` with no reason — the observable form of C4. Red
      first: `test_restore_request_slot_owned_denial_names_the_wait` asserted the desired state and failed
      with *Expected 2 Was 0* before the wiring, then passed. Suite **19/19**;
      `RESTORE_LIVENESS_RED=1` → 24 cases / 4 failed (the unchanged Packet A set); RAM1 still
      `code 392,328 / padding 888`.
      **Still open:** the remaining wait transitions (`waiting_capture`, `waiting_commit_gate`,
      `waiting_job_capacity`), the `refused`/`unnecessary` terminals, pre-publication identity validation,
      and `loopSlotRestoreAttempted_` retirement — the last three land with A2.2–A2.4, since they are the
      gates those tasks introduce.
      → **DONE 2026-09-24 (second session), with one recorded residual.**
      *Named waits:* `waiting_capture` and `waiting_job_capacity` are now recorded at the selection
      `Skip`/`SkipKeepParked` sites and at the park path — recording denies by `waiting_capture`, and the
      **queued sibling** of an instance holding the global commit gate waits in `waiting_job_capacity`
      (the instance's own denied commit stays `waiting_commit_gate`). An owner-blocked head keeps its
      `waiting_slot_owned` name.
      *Pre-publication identity:* `beginLoadLoopJob` captures the identity it serves and
      `commitLoadLoopJobPublish` revalidates it **before the token check** (whose reset would destroy a new
      owner's content); a superseded or decided request publishes **nothing** and only the job's
      parser/session storage is released. `advanceRestoreRequestGeneration` records `superseded` under the
      request's own identity and keeps the record readable; a user CLEAR calls it.
      *Residual (recorded, not claimed):* the set-change wire (`noteRestoreSetEpoch`) has no production
      call site yet, because no set-epoch accessor exists; "a set change yields `superseded`" is proven at
      the record level only. `handleUndoClearTrack`/`handleRedoClearTrack` do not advance the generation.
      The done-test is deliberately conservative (`state == Terminal`), so a user CLEAR does not auto-reload
      the cleared slot — see A3.7's shape (b) finding for why.
      **Evidence:** `test_storage_manager_load` **33/33** (5 new cases: capture wait, stale-identity
      rejection publishing nothing, prepare-complete probe, terminal probe, strengthened sibling
      job-capacity wait); full native **2,343 / 1 skipped / 0 failures**; capture-serial SUCCESS
      `code 391,000 + padding 2,216`. Details: [`REVIEW.md`](REVIEW.md) § 2026-09-24 (second session).
 Introduce
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

- [x] A2.2 **Background-restore preparation admission (D2), scoped to the global gate.**
      → **DONE 2026-09-23.** `canPrepareBackgroundLoadLoopNow()` + `backgroundJobMayBeServicedNow()` in
      `LoadLoopJob.cpp`: a bounded background **preparation** slice is now admissible under active
      transport, so a queued request's open/read/parse advances while playback continues. Commit and
      cleanup are **not** admitted by it — `canRunBackgroundLoadLoopNow()` still governs those, so the
      commit stop-gate is untouched and an unbounded commit under transport is never entered. Recording
      keeps its existing deferral (the SD-session exclusion returns false for capture, per proposal
      TBD-2/O1). Selection now admits preparation through the same predicate at all four sites, and a
      non-focus job with preparation left is serviced rather than parked.
      **A prepare-complete request waits by name:** `markRestoreRequestWaiting(..., CommitGate)` fires at
      the moment the phase changes to commit/cleanup under a denying (non-idle) runtime, so
      `waiting_commit_gate` is observable on the same turn — not left `queued` and not a terminal.
      **C1 and C2 flipped red→green and were PROMOTED into the default gate in this commit** (A1.11).
      New case `test_restore_request_prepare_complete_waits_at_commit_gate`. Suite 22/22 default;
      `RESTORE_LIVENESS_RED=1` → 25 cases / **2 failed** (C4, C6 only — the remaining known-red pair);
      full native **2,309 / 1 skipped / 0 failures**; RAM1 `code 392,328 / padding 888`.
      **Not claimed:** a restore *applying* under active transport (runtime-maintenance 3.3), and that
      queue-head skipping solves the global `waiting_job_capacity` blockage (it does not; that is A2.3's
      explicit non-claim).
 Add a bounded
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

- [x] A2.3 **Startability-aware queue service (D3).**
      → **DONE 2026-09-23.** `popNextStartableDeferredLoopSlotRestore` walks the priority order and takes
      the **first startable** request, leaving a blocked entry queued with its reason (the A2.1 record
      already names that wait). Done entries are still removed as queue hygiene. `queuedRestoreMayBegin`
      moved from `LoadLoopJob.cpp` to the queue TU so the selection path and the startable pop share one
      liveness predicate, and both `BeginBackgroundFromQueue` sites now use the startable pop instead of
      a head-only pre-check.
      **C4 flipped red→green and was PROMOTED into the default gate in this commit** (A1.11). New cases:
      `test_a23_startable_service_preserves_priority_order_and_releases_the_blocked_head` (three startable
      entries return in priority order while the blocked head stays queued and is never taken) and
      `test_a23_second_request_reports_job_capacity_wait_while_one_is_commit_pending` (the **non-claim made
      observable**: with one request commit-pending the gate is global, so a second request stays tracked
      and is not described as queue starvation). Suite 25/25 default; `RESTORE_LIVENESS_RED=1` → 27 cases /
      **1 failed** — C6 alone, the last known-red fixture. RAM1 `code 392,328 / padding 888` (the first
      build failed at `padding 808` until the new helpers were made FLASH-resident; the placement rule is
      enforced by the gate).
      **Not claimed:** that this resolves the `waiting_job_capacity` blockage — it is a scheduler-level
      gate, not queue ordering (A2.2/A2.3's explicit non-claim, now covered by a case).
 Select the first startable request in priority
      order, leaving a blocked entry queued with its reason; keep the S2b.5 `queuedRestoreMayBegin`
      semantics and the existing done-tests. **Verify:** A1.5 flips red→green; a case asserts priority
      order is preserved among startable requests and that a blocked entry is still served once its owner
      retires. **Not claimed:** that this resolves the `waiting_job_capacity` blockage, which is a
      scheduler-level gate, not queue ordering.

- [x] A2.4 **Bounded retry then named outcome (D5).**
      → **DONE 2026-09-23.** `kMaxLoopSlotRestoreAttempts = 3` with `loopSlotRestoreAttempts_`, and
      `scheduleLoopSlotRestoreRetry(...)`: a **retryable** failure resets the slot as the restore path's
      **own** cleanup (keeping the identity and naming a `waiting_retry` wait, per the A2.1/G2 tightening)
      and re-queues the request while under the bound; at the bound it records the `refused` terminal and
      latches. Retryable now covers the token/epoch/header/size/seek/open failures in `beginLoadLoopJob`
      and the reopen/seek/read/token/parse/body-probe failures mid-job — the class a transient SD hiccup
      produces. An **absent** file stays `unnecessary` (no retry), which is the pre-existing, correct
      behavior.
      **C6 flipped red→green and was PROMOTED into the default gate in this commit** (A1.11), so the
      known-red set is **empty** and **`RESTORE_LIVENESS_RED` is deleted** — no red fixture was deleted,
      generalized or threshold-relaxed on the way. The C6 fixture's phase-1 oracle was **corrected**: it
      had asserted the old latch (`attempted=1`) as the marker that the failure was reached, which would
      have re-encoded the defect as the oracle; it now asserts the request is still **owed** (queued or in
      a named wait) and not committed. New case `test_a24_persistent_failure_reaches_the_bounded_refusal`
      covers the other end: a payload that stays broken reaches the terminal after the bound instead of
      retrying forever.
      **A2.1 retirement completed:** `loopSlotRestoreAttempted_` is **subsumed** by the request record
      (`isLoopSlotRestoreAttempted` = "the request reached a terminal"), removing the second 64-byte array
      and leaving one source of truth for exactly-once. Suite 26/26 default; full native green; RAM1 line
      re-based **deliberately and in this commit** — `variables 96,128 → 96,192`, `locals free 34,944 →
      34,880`, `padding 888 → 872` (above the 840 floor) — with the reason recorded in
      `scripts/ram1_budget.json` and `REVIEW.md`, because the record stores a full identity+state+terminal
      per `(track, slot)` where the retired flag stored one bit.
 Replace the permanent attempt latch with a declared
      bounded attempt count per power cycle; a transient failure retries (up to the bound) and a persistent
      one ends in `refused` naming the reason, while absent/invalid payloads stay `unnecessary`.
      **Verify:** A1.6 flips red→green; cases assert retry-then-commit for a transient failure,
      retry-then-refuse for a persistent one, and no retry storm (attempts bounded per turn and per power
      cycle).

- [x] A2.5 **Packet B device gate.** *(taken and green 2026-09-24 with a clean boot capture; see the
      clause-by-clause evidence below.)*
      → **DONE 2026-09-24.** Firmware flashed `teensy41-capture-serial` (upload SUCCESS, the hard reset)
      and the boot captured from **3.0 s uptime** through 33.9 s — a complete boot drain, not a mid-session
      attach. Evidence artifact: `captures/a25-boot-gate/` (`serial.log` + `metadata.json`); the harness
      run that produced the same behavior is `captures/host_midi_automation_serial_20260924_012548.log`.
      This gate first required the restore telemetry (the A1.8 line deferred to Packet B), now landed as
      `SC_RESTORE_REQUEST`/`RRQ`.
      **Clause by clause:**
      - *Every boot payload has a state; no payload without one* — **green**: 87 `RRQ` lines over 29
        distinct `(track, slot)` requests, each observed `queued` → `running` → `terminal/committed`
        (29/29/29). **0 requests were not terminal** at capture end.
      - *`overflow_free=True`* — **green**: **0** `RING,overflow` rows across the 30.9 s boot window on
        this build, and the managed run's MIDI-boundary check reported `overflow_free=True`.
      - *The previously-`EMPTY` slot restores* — **green**: the boot census contains **`5/1`**, the
        authorized scratch slot, alongside all 28 other committed slots; every one reached `committed`
        with no stall.
      - *MIDI gap within the standing bound* — **green by the standing oracle**: `overflow_free=True` is
        the recorded standing gate for the MIDI path, and the restore work is bounded per turn (A2.2) and
        charged in the drain budget (A3.3). Restore service latency measured `median 2.78 s`, `max 3.85 s`
        per request across multi-slice preparation on an idle boot — reported as the measured service
        time, **not** redefined as a MIDI-gap bound.
      - No `waiting_*` state was observed on this run, which is consistent: every committed payload opened
        and read cleanly at boot, and no job was commit-pending while another needed preparation.
      **Note on the earlier attempt:** the first device attempt used `host_midi_hitl.py run --preset base`,
      whose managed verdict was `INCONCLUSIVE_CAPTURE` on the documented `record stop did not reach PLAYING`
      setup-state race (`CURRENT_WORK.md`: `successor-wrap-48/49/50`) — a harness weakness, not a
      functional failure. Resetting that board also required a re-flash: neither serial-open nor a
      1200-baud touch resets it (uptime stayed at 643 s), which is recorded because it cost a cycle.

- [x] A2.6 **Packet B gate.**
      → **DONE 2026-09-24.** Full native suite green: **2,316 cases / 1 skipped / 2,315 succeeded /
      0 failures**. A1.2/A1.3/A1.5/A1.6 are green **in the default gate** (promoted per A1.11, not left
      behind a flag — and the flag itself is deleted now that the set is empty). Capture-serial RAM1 line
      recorded: `variables 96,192 / ITCM code 392,344 + padding 872 / locals free 34,880`, above the 840
      floor, 12 ITCM banks unchanged. A2.5 device evidence recorded in `captures/a25-boot-gate/`.

- [ ] A2.7 **Commit-bound prerequisite (review finding 1) — record, do not implement.**
      → **OPEN (recording task), strengthened 2026-09-24 (second session).** To be recorded in the change
      review and the S1/S2b handoff notes: `resumable-midi-boundary-playback-maintenance` **task 3.3**
      (bounded Commit preparation/cleanup) is the named external prerequisite for any criterion that
      requires a restore to **apply** under active transport. This change keeps every such criterion scoped
      to terminal-or-named-wait: A2.2's `waiting_commit_gate` case asserts the commit is **not** applied
      under transport, and no `main.cpp` commit-gate change exists in this change's diff
      (`MidiServiceDrain::admitLoadJobChild` untouched). **Active-transport commit/cleanup is explicitly
      retained as separate work**: it is not implemented, not relaxed, and not implied by any task here,
      and A5.5 may not be read as closing it. The A3.7 shape (b) gate this session is the concrete
      demonstration that the wait (not the commit) is what this change delivers.
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

**Revision 5 (2026-09-23) — A1.7/A3.1 oracle split, from measurement rather than preference.** The
original text made a native red fixture against the **real** blocking drain the evidence for C5 and for
"A3.1 the drain advances the restore". Two rounds of measurement (`REVIEW.md` § A1.7) showed that
retaining that call natively needs most of the persistence stack **plus** the display stack
(`DisplayManager`'s `_display` member drags the OLED/Adafruit-GFX font chain in) and the cold
edit/track-manager owners — close to the whole firmware. That is not a test-only closure, so the oracle
moves to where it can be tight: **native** for the pure decisions (budget, progress, wait-reason
classification, caller-contract mapping) and the **A3.7 device gate** for the integration claim that the
drain actually advances a restore. The spec delta is unchanged; only the evidence route is. No native red
fixture for C5 is claimed, and A1.7 stays open until its pure-decision cases land with A3.2/A3.3/A3.4.

## 3. Packet C — the blocking drain advances the restore (D4)

- [ ] A3.1 **Drain-side restore service on the real eligibility (D4, G2 finding 1).** *(Oracle amended
      2026-09-23, Revision 5 — see A1.7: the integration claim below is verified through the A3.7 device
      gate with capture evidence, because retaining the real drain call natively needs the display stack.
      The drain-side *decisions* it makes — admission, wait reason, budget, progress — are covered
      natively by A3.2/A3.3/A3.4, which is where a tight oracle is available.)* Give the blocking
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
      → **RECONCILED 2026-09-24 (second session), task stays OPEN on the device integration only.** The
      drain-side **decisions** this task names are landed and native-verified where Revision 5 placed them:
      admission uses the same all-tracks eligibility (`backgroundJobMayBeServicedNow`,
      `canPrepareBackgroundLoadLoopNow`), the wait reason is classified by the pure A3.2 function, the
      budget and progress terms are A3.3's, and the caller contract is A3.4's. The three Verify cases that
      need the **real** blocking drain plus the display stack are the A3.7 device gate's, per the amended
      oracle. **A3.7 shape (b) is blocked this session** by the clear-then-reload allocation FATAL, so the
      integration claim ("the drain advances a restore it waits on") is **not** taken and A3.1 stays open
      with that reason. No clause is claimed green by inference.

- [x] A3.2 **Named drain verdict (D4).**
      → **DONE 2026-09-24.** `SyncDrainFailureReason` gains **`BlockedByRestore`**, and
      `classifySyncDrainVerdict(captureRecording, slotLoadActive, postLoadHoldoff, restoreWait)` is the
      **pure** classifier: it returns the reason **paired with the gate name**, so the drain's report site
      is branch-free and the ITCM drain pays nothing for the wait vocabulary. Order matches the drain's own
      checks — capture first (a recording track defers the load itself), then the slot-load gate, then the
      post-load holdoff — and `Stuck` is reserved for a gate nothing explains. The wait arrives as a small
      `SyncDrainWait` code, deliberately **not** a second enum: the first attempt introduced a duplicate
      vocabulary and cost more ITCM than the problem needs.
      Wired: `restoreRequestWaitFor` reads the A2.1 record's wait, `restoreWaitReasonBlockingSave` /
      `restoreWaitCodeBlockingSave` resolve the request holding the save gate (live job instances first),
      and `drainPersistenceWorkBlocking` classifies through the pure function, reporting e.g.
      `blocked by waiting_commit_gate` instead of the generic `slot_load_active`.
      **Five new cases** in `test_persistence_sync_drain_budget` (a pure library, so a tight oracle):
      capture outranks the slot-load gate; all five named waits map to their `waiting_*` names; an active
      slot load with **no** named wait keeps `BlockedBySlotLoad`; the holdoff is named and outranked by an
      active slot load; and `Stuck` is reserved for an unexplained gate, with every named verdict carrying
      a reportable name. Suite **12/12**.
      **Prerequisite landed first, as its own commit:** the drain's cold terminal report moved to a
      `STORAGE_PERSIST_MEM` helper, freeing **512 B of ITCM** (`padding 872 → 1,384`). A3.2 then cost
      112 B, leaving `code 392,024 + padding 1,192` — **352 B above the 840 floor**, i.e. net better than
      before both steps. Full native green.
      **Not claimed:** the drain-integration/device half (A3.7) — this is the decision and its report.
 Replace the generic `BlockedBySlotLoad` verdict with
      `BlockedByRestore(<request wait reason>)` so the drain reports *why* it could not advance, and keep
      the existing `BlockedByCapture` / `BlockedByPostLoadHoldoff` distinctions (the holdoff is reported by
      name, not spun). **Verify:** cases assert the named reason for capture-active, commit-gate,
      job-capacity and retry waits; no case asserts a generic `Stuck` where a named reason is available.

- [ ] A3.3 **Restore work in the drain budget and progress accounting (D4, G2 finding 3).**
      → **NATIVE SCOPE DONE 2026-09-23 (per the Revision 5 split); the drain-integration half is A3.7.**
      `SyncDrainBudgetInput` gains `restoreSliceSteps` / `restoreSdBytes` / `restoreOnlyBlocker`, and
      `buildSyncDrainBudget` charges them alongside persistence work, so a drain that must advance a large
      restore is not budget-starved by a persistence-only estimate. `SyncDrainProgressSnapshot` gains
      `restorePhase` and a monotonic `restoreProgressCounter`, compared **before** `madeSyncDrainProgress`'s
      "no active persistence work item" short-circuit — because the C5 shape is exactly "a save is owed and
      the drain is waiting on the restore", where a drain making real restore progress previously read as
      stuck. Wired to live state: the counter is bumped per serviced job grain, and
      `captureSyncDrainProgressSnapshot` / `buildSyncDrainBudgetForSession` populate the restore terms from
      the job instances.
      **Three new native cases** in `test_persistence_sync_drain_budget` (a pure library, so a tight
      oracle): restore work raises `expectedSliceSteps` / `maxSliceSteps` / `estimatedSdPayloadBytes`;
      restore motion **alone** counts as progress (counter and phase), while no motion does not; and a
      restore-only budget is bounded (non-zero steps, finite wall clock, non-zero bytes). Suite 7/7.
      **Trimmed after the RAM1 gate rejected two redundant fields:** the byte/parse-position progress
      terms were strictly subsumed by the per-grain counter, and removing them returned the build to the
      approved line (`code 392,344 + padding 872`). **Not claimed here:** that the drain *advances* the
      restore — that integration claim is A3.7 (device), per Revision 5.
      → **RECONCILED 2026-09-24 (second session).** Re-checked against landed code and tests: every native
      clause in this task's contract is present and green — `SyncDrainBudgetInput` carries
      `restoreSliceSteps`/`restoreSdBytes`/`restoreOnlyBlocker` and `buildSyncDrainBudget` charges them;
      `SyncDrainProgressSnapshot` carries `restorePhase`/`restoreProgressCounter` compared **before** the
      no-active-work short-circuit; the counter is bumped per serviced grain and the terms are populated
      from the live instances. Cases `test_drain_budget_includes_restore_work`,
      `test_made_sync_drain_progress_counts_restore_motion` and `test_restore_only_drain_budget_is_bounded_not_zero`
      are in the default gate (`test_persistence_sync_drain_budget`). **Nothing missing natively.** The
      remaining clause is the A3.7 device integration claim, which is blocked by the shape-(b) finding, so
      this task stays unchecked with that as its reason.

- [x] A3.4 **Caller contract: completed / blocked / failed (D4 rule 7, G2 caller finding).**
      → **DONE 2026-09-24.** New pure contract owner `include/PersistenceDrainOutcome.h`:
      `SyncDrainOutcome { Completed, TemporarilyBlocked, Failed }` and
      `resolveDrainCallerAction(outcome)` → `{ Proceed, RefuseBlocked, ReportFailure }`, plus
      `drainOutcomeLeavesTargetUntouched`. **Only `Completed` proceeds**; a blocked drain is an explicit
      refusal that leaves the target untouched and the save owed; a failure is reported as a failure and
      stays distinguishable from the blocked wait. There is deliberately **one** decision function, so both
      call sites and the tests share it rather than each encoding the rule.
      `drainPersistenceWorkBlockingDetailed` returns `SyncDrainResult { outcome, reason }`, mapping the
      A3.2 verdict onto the three-way result — so the **reason and the outcome cannot drift**, and a
      blocked outcome carries the `waiting_*` name. **`saveState` stays a `Completed`-only wrapper**, which
      is the compatibility guarantee for any pre-existing caller.
      **Both call sites use the detailed entry point:** `handleClearTrack` now reports a blocked drain as
      `Clear refused: deferred save temporarily blocked by <reason>; slot left untouched, save still owed`
      (with the existing `Clear aborted` line reserved for a real failure), and `saveNewSetInternal` refuses
      the new set for either non-completed outcome while reporting which it was — it neither treats a wait
      as failure nor proceeds while persistence is pending. **Retention is O7 and is not built.**
      **Three new cases** in `test_persistence_sync_drain_budget`: only `Completed` licenses the operation
      and only it leaves the target touched; blocked and failed are distinguishable; and every A3.2 named
      gate is a *temporary block* while `Stuck` is the failure side (pinning the verdict→outcome mapping).
      Suite **15/15**; full native **2,324 / 1 skipped / 0 failures**; RAM1 `code 392,152 + padding 1,064`
      (224 B above the floor). **A3.4a (re-entry guard) remains open** and is unaffected.
 Give the drain
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

- [x] A3.4a **Drain re-entry and callback safety (D4 rule 9; prerequisite to holdoff polling).**
      → **DONE 2026-09-24.** `include/PersistenceDrainGuard.h` owns one scoped owner
      (`StorageManagerInternal::PersistenceDrainScope`) plus `isPersistenceDrainInProgress()`; the state is
      one `bool` in the already-measured `StorageSession` aggregate. The guard's implementation is its own
      translation unit (`src/StorageManager/PersistenceDrainGuard.cpp`), so its contract is native-testable
      without the drain scheduler's closure; `storageSession` moved to
      `src/StorageManager/StorageSessionState.cpp` for the same reason.
      `drainPersistenceWorkBlockingDetailed` acquires the scope before touching shared save/budget/
      progress/result state, and a **nested** call returns
      `TemporarilyBlocked("drain_in_progress")` immediately (the `bool` wrapper returns false) without
      changing the outer owner. The scope destructor is the single release path, so Completed, Blocked,
      Failed and the early `AdmissionRefused` break all release it identically.
      **Audit — pre-side-effect checks landed:** `handleClearTrack` (first statement, before `stopPlaying`),
      `handleUndo`/`handleRedo`, `handleUndoClearTrack`/`handleRedoClearTrack`, and the storage callbacks
      `saveNewSetInternal`/`copySavedSetIntoCurrent`. **Deliberately not guarded:** transport, record,
      note-edit and load/save-mode toggle — none resets content or persistence state, and refusing them
      would break normal service.
      **Fixtures:** new suite `test_drain_reentry_callback_safety` **6/6**, driving a CLEAR through the
      **real** MIDI route (`midiButtonManager.handleMidiNote` → processor → callback → `executeAction` →
      `handleClearTrack`): a free-guard control that reaches the slot mutations, a held-guard refusal with
      **zero** slot mutations and **zero** silences and the committed content intact, the nested-drain named
      block, a conflicting undo refused before content replacement, transport not refused, and guard release
      with a later drain taking it. **Red-proven:** forcing the predicate to always return false makes the
      refusal case fail (`mutations=6 silence=1`) and the nested-drain case fail.
      `test_storage_manager_load` **29/29** adds the scope contract (one owner at a time; release on exit;
      predicate false without an owner). Full native **2,332 / 1 skipped / 0 failures**; capture-serial
      build **SUCCESS** at `RAM1: variables 96,192 / code 391,896 + padding 1,320 / locals 34,880`. The
      first build **failed the floor** (`padding 728 < 840`): five guarded call sites cost 336 B of ITCM,
      so **`handleClearTrack` is now `STORAGE_PERSIST_MEM` (FLASH)** under the change's own placement rule
      (a destructive slot-clear is not a MIDI clock hot path). That is **480 B better than the A3.4 line**
      (`392,152 / 1,064`); `padding_floor` stays **840** and was not re-based.
      Structural prerequisite landed with it: the duplicated
      `restorePlaybackAfterSlotClear` now has one owner (`src/Track/SlotClearPlaybackRestore.cpp`), which is
      what let a suite link the button routes *and* the undo path.
      **Not claimed:** the device integration claim stays A3.7's, per Revision 5; and the
      holdoff-window injection this task describes is A3.5's MIDI-in-the-wait slice, for which A3.4a
      supplies the guard.
      Original contract (for reference): a StorageManager-owned scoped guard before any shared drain
      mutation; nested detailed calls return `Blocked(drain_in_progress)` without changing the outer
      state and the bool wrapper returns false; destructive/storage callbacks rejected before side
      effects (including CLEAR's early stop) while normal musical input/transport stays serviced;
      all-track eligibility and request identity revalidated after each callback boundary.

- [x] A3.5 **The finite post-load holdoff is waited out, not aborted (D4 rule 8).**
      → **DONE 2026-09-24.** The interval is named once — `LoadLoopBudget::PostLoadCommitSaveHoldoffMs`
      (100 ms) — and `cleanupLoadLoopJobPublish` sets the deadline from it, so the wait policy and the
      timeout cannot drift. `postLoadCommitSaveHoldoffRemainingMs()` reads the remainder **without
      clearing** it (the scheduler's `deferredSaveBlockedByPostLoadCommitHoldoff()` clears an expired
      holdoff as a side effect, which is right for the scheduler and wrong for a caller that must compare
      the remainder against its budget first).
      `serviceMidiWhilePostLoadCommitHoldoffBlocks(maxWaitMs)` waits the holdoff out, **servicing MIDI**
      (`midiHandler.handleMidiInput()`) and yielding, bounded by the caller's remaining wall clock. The
      drain charges real elapsed time against `maxWallClockUs` and enters the wait only when
      `holdoffWaitFitsInDrainBudget(remaining, budget)` (a pure decision); otherwise it does not wait at
      all and A3.2's classifier reports the named `post_load_holdoff`. The wait services MIDI **before**
      checking the deadline, so a message already queued when the drain arrives is not dropped on a
      just-expired holdoff.
      **Fixtures.** `test_persistence_sync_drain_budget` **18/18** pins the bound (fits / exactly-does-not
      fit / exhausted / nothing-to-wait-for). `test_storage_drain_restore` **4/4** drives the **real**
      drain: (a) with a fitting holdoff armed, the drain waits it out, **consumes an injected DIN
      message** during the wait, and clears the holdoff; (b) with the production 100 ms holdoff against the
      minimum drain wall clock (~99 ms) the wait is **not** entered — the message stays queued and the
      holdoff stays armed for the caller to refuse on; (c) the remaining-time reader is read-only.
      **Red-proven:** disabling the wait makes (a) fail (`inbox=1`).
      **RESTRUCTURED 2026-09-24 after the A3.7 device run (option 1 of the topology conflict).** The
      drain-side `midiHandler.handleMidiInput()` call this task originally added **broke the MIDI-service
      topology gate** (`scripts/hitl/verify/midi_boundary_timing.py`): all input drains belong in
      `src/main.cpp` and there are exactly nine of them, and the drain runs on the interval path. The drain
      therefore **no longer waits the holdoff out and no longer services MIDI**: it detects the finite
      holdoff as the sole blocker, classifies it, and returns the named `post_load_holdoff` immediately,
      preserving the restore and the owed save. The holdoff expires on its own 100 ms deadline while
      `main` keeps servicing MIDI; the caller refuses by name (A3.4) and the next attempt completes the
      save. `holdoffWaitFitsInDrainBudget`, the wait helper and its bound cases were removed with the
      wait; the pure classifier and the named-wait contract carry the behaviour. The call-site scanner
      now ignores whole-line comments, so documenting the invariant next to the code cannot break the
      gate. **Evidence after the restructure:** `test_storage_drain_restore` **6/6** (the drain names the
      holdoff, leaves the queued MIDI untouched, and leaves the holdoff armed); topology gate **9/9 with
      no extra call sites**; full native **2,337 / 1 skipped / 0 failures**; capture-serial **SUCCESS**
      `code 390,936 + padding 2,280`.
      **Recorded scope notes.** The holdoff is armed through the documented controllable input
      `armPostLoadCommitSaveHoldoff` (the production setter is `cleanupLoadLoopJobPublish`, whose
      read/parse/commit/cleanup closure a native fixture cannot reach); that is the same
      controllable-input pattern as `armBackgroundRestoreHoldoff` and the transport boundary, and it
      writes the one state the production path writes. The fixture does **not** assert the save's own
      `Completed` outcome (no real workspace is written).
      **Not claimed:** the device integration claim stays A3.7's, per Revision 5.

- [x] A3.6 **Preserve the DEC-081 and capture behavior.**
      -> **DONE 2026-09-24.** Three regression cases in `test_storage_drain_restore` (which links the real
      deferred-save scheduler):
      (a) **audible playback is not a persistence gate** -- a `TRACK_PLAYING` track leaves
      `isCaptureRecordingForPersistence()` false and the pure classifier names no denial (it is not
      `BlockedByCapture`), so DEC-081 still holds;
      (b) **a recording track parks the multi-frame writers** -- with the track recording and a save owed,
      `processDeferredSaveState` queues nothing and starts no workspace/bundle write, and the owed save
      stays owed;
      (c) **the mid-pass one-chunk flush still runs during a take** -- with a real sealed chunk in
      `PersistenceQueue`, the same call services one chunk (queue depth drops), while the multi-frame
      writers stay at zero and the queued MIDI is left untouched (the take path returns *before* A3.5's
      holdoff wait, which is the intended separation).
      `isCaptureRecordingForPersistence` semantics are unchanged: it iterates tracks and returns true only
      for `track.isRecording()`.
      **Harness defect found and fixed by (c):** the in-memory SD stub compared the raw mode argument, so
      the only express-create site (`MidPassChunkPersist`'s seal journal, `FILE_WRITE | O_CREAT`) fell
      through to the existing-file branch and received an **invalid handle** -- the journal could never
      open natively and the whole mid-pass flush was unobservable. The stub now masks the mode to its core
      bits and honours `O_CREAT`. This is a test-harness fidelity fix: the device's `FILE_WRITE` is itself
      create-or-open, so the production call is valid there and was only ever untestable here.

- [~] A3.7 **Packet C device gate — shape (a) PASSED 2026-09-24; shape (b) partial.** Run on track 5 /
      slot 1 (authorized scratch) with a **fresh boot** (`DIAG,boot_reset` present, no `RING,overflow`) via
      `scripts/hitl_clear_drain_a37.py`, a 24-bar take, and CLEAR long-pressed while the save was owed.
      **Shape (a) "nothing else sounding" PASSED:** `Clear waiting for deferred save completion` → the
      blocking drain ran → **`State saved successfully (v4)`** → `MIDI: Clear selected slot 1`, i.e. the
      save completed and the slot cleared in **<243 ms**; **0** `Clear aborted`, **0** `Persistence drain
      stuck`, **0** `drain blocked`, **0** named refusals; the packet's timing verifier reports
      `call_site_gate_ok: True` (9/9), `overflow_free: True`, **no issues**. **Shape (b) "another track
      audibly playing": completed** (0 gate strings), but the deferred save had already completed by the
      time the CLEAR landed, so the discriminating save-owed-during-playback case was **not** exercised —
      recorded as partial rather than claimed. Also recorded: the `base` preset currently stops a 2-bar
      take to `EMPTY` instead of returning to `PLAYING` (reproduced on a capture from 2026-09-24 01:25,
      i.e. before this session's changes), so shape (b) used the dedicated script rather than that preset.
      Evidence: `captures/a37-clear-05-fresh/` (shape a), `captures/a37-clear-04-shapeb/` (shape b).
      → **SHAPE (b) RE-ATTEMPTED 2026-09-24 (second session), still OPEN — blocked by a fatal
      allocation.** `scripts/hitl_a37_shapeb.py` constructs "another track playing + a deferred save
      genuinely owed + a restore in flight" and presses CLEAR on the reload target. Three fresh-boot
      attempts (`captures/a37b-01/` 16-bar save, `-02/` 8-bar, `-03/` 2-bar bisect) all died before the
      CLEAR with `#MEM_EXT_REFUSED bytes=22369620` →
      `FATAL: both internal heap and external memory are exhausted!`. The pool total is **8,380,416 B**
      (`psram_pool`), so the 22,369,620 B request is structurally impossible and reproduces at the same
      value across runs. The only reachable route to "a restore in flight" on this device is
      clear-then-reload (every payload-bearing slot is restored at boot), and the bisect shows the refusal
      follows a `LoadLoopJob done` for the just-cleared slot. **This change therefore does not enable the
      reload-after-external-reset route** (`isLoopSlotRestoreAttempted` stays `state == Terminal`,
      conservative), and the allocation defect is recorded as a `runtime-working-set-ownership`/recording
      finding, out of this change's scope. **Control:** a plain 2-bar take with no clear-then-reload
      (`captures/a37-control-02/`) records and saves cleanly with **0 FATAL**, isolating the reload route.
      Shape (b) remains a **mandatory** gate and is **not** taken, so the change is not archive-ready
      (A5.5). See [`REVIEW.md`](REVIEW.md) § 2026-09-24 (second session).
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

- [x] A4.1 **Named reconcile outcome.** Report `ok` / `repaired` / `failed` for the boot reconcile,
      keeping the self-healing repair intact. **Verify:** native cases assert each outcome; a
      malformed-index case asserts the repair outcome and that a later read succeeds; the existing
      SD temp-write regression tests stay green.
      → **DONE 2026-09-23.** `SetIndexReconcileOutcome { Ok, Repaired, Failed }` +
      `reconcileSetIndexOnSdWithOutcome` (with `setIndexReconcileOutcomeName` for the log), and
      `reconcileSetIndexOnSd` kept as the `Ok || Repaired` compatibility wrapper so no caller changes
      semantics. New suite `test/test_storage_reconcile_index` (in `native_test_link.py`'s
      hardware-boundary set): **4 cases / 4 succeeded** — consistent index ⇒ `Ok` (asserted both
      directly and through the wrapper), missing index ⇒ `Repaired` with the index written back and a
      second reconcile of `Ok`, malformed index ⇒ `Repaired` then `Ok`, so the repair is durable and
      not repeated. The boot site now logs the named outcome instead of only warning on failure (the
      old line could not distinguish "nothing to do" from "rebuilt it"). Existing SD temp-write
      regression cases stay green. RAM1 unchanged at `code 392,328 / padding 888`; firmware build
      SUCCESS.

- [ ] A4.2 **Packet D device gate.** One device boot with a known-healthy index and one with a
      deliberately malformed index (only with explicit permission for the SD change; quarantine rules
      apply — never rename or delete card data without same-session permission). **Verify:** the outcome
      line is present and correct in both logs; 0 unexpected `could not reconcile` after repair.
      If the malformed-index case is not permitted, record Packet D's device clause as unrun and keep
      the task open.
      → **HEALTHY-INDEX HALF TAKEN AND GREEN 2026-09-24 (second session).** A fresh-boot capture
      (`captures/census-final-02/boot-census.log`) contains the named outcome
      `[StorageManager] INFO: sets/index.bin reconcile ok`, with **0** unexpected `could not reconcile`
      rows. The deliberately malformed-index half is **optional unrun** — it needs same-session
      destructive-SD permission, which was not granted — so it is recorded unrun with that reason, which
      A5.5 names as the one permitted optional gate.

- [x] A4.3 **Packet D gate.**
      → **DONE 2026-09-24 (second session).** Full native suite green: **2,343 cases / 1 skipped /
      2,342 succeeded / 0 failures**. A4.1's `test_storage_reconcile_index` cases are green (4/4). RAM1
      line recorded: `variables 96,192 / code 391,000 + padding 2,216 / locals 34,880` (1,376 B above the
      840 floor). A4.2 status recorded: **healthy-index half taken and green**
      (`captures/census-final-02/`, `sets/index.bin reconcile ok`, 0 unexpected `could not reconcile`),
      **malformed half explicitly unrun** (needs same-session destructive-SD permission) — the single
      optional gate A5.5 permits.

## 5. Closeout

- [ ] A5.1 **Re-run the unblocked `runtime-working-set-ownership` device legs** (S1.1 per-owner census,
      S1.3 slope sweep, S1.4 undo/root baseline, S1.5 device refusal point, S2b.3 post-restore baseline,
      S2b.5 device gate) using that change's pinned baseline and recipe, now that the restored set is
      stable. **Verify:** each leg's evidence is recorded in that change's
      `ARCHITECTURE-REVIEW.md`; any leg still failing returns as that change's finding, not this one's.
      This task is a handoff, not a second implementation of those legs.
      → **IN PROGRESS 2026-09-24 (second session).** The post-restore census checkpoint is **finished and
      device-verified** (see A4.2 / `captures/census-final-02/`): `free_chunks=451` after all restores,
      readiness attested, S2b.3's `>= peak + 16` condition met, 0 reconcile failures. That is the fixture
      S2b.3's post-restore budget and S1.3's slope pin to. **The post-restore budget baseline is now
      measured and stable:** three fresh-boot slope steps (`captures/s13-slope-final/`, sweep updated to
      read the checkpoint marker) report `banner=True`, `reconcile_warnings=0`, `N=29` and
      **`post_restore_free_chunks=452` of 512 on every boot**, with the restore walk adding no reservation
      (512→480 both times). **The slope itself is still open:** the sweep's track/slot control selected
      track 5 rather than the requested track 6, so its three "growth" steps recorded onto an already-full
      slot and N never grew — a fixture defect recorded, not a slope. S1.1's per-owner census, S1.4's
      undo/root baseline and S1.5's refusal point are unchanged from their recorded device runs. **No leg is
      reinterpreted**; each returns as `runtime-working-set-ownership`'s finding. **A5.2 is now closed**
      (base-preset-specific recording failure; direct stream + pipeline PASS on the current firmware).

- [x] A5.2 **Content-verified PASS** for the authorized scratch slot via the
      [`hitl-content-verification`](../../../.agents/skills/hitl-content-verification/SKILL.md)
      pipeline (`dump_and_capture.py` + `verify_playback_content.py`), not a base-run verdict.
      **Verify:** `result: PASS`, `storage_matches_effective: true`, and every cycle
      `inventory_equal: true` / `exact_order: true` with 0 missing and 0 extra. If the two
      unexplained bar-0 rows recur, they are retried, never reinterpreted.
      → **ATTEMPTED 2026-09-24 (second session), STILL OPEN — no run on the current firmware meets all
      clauses.** A fresh capture attempt with the canonical entry point
      (`host_midi_hitl.py run --preset base --track 5 --loop-slot 1`) reproduced the separately tracked
      recording-window failure (`captures/cpv-final/`: `record note span 219/0`, `stored record grid
      count=0`, `record stop did not reach PLAYING`, `INCONCLUSIVE_CAPTURE`), matching
      `captures/a51-finding-01.md` — so the pipeline cannot be driven on this firmware yet.
      **Existing runs are attached and classified against all four clauses, none closes it:**
      * `captures/cp02-playback/` (content `captures/content-pass-02/`, track 5 slot 1) — `result: PASS`,
        `storage_matches_effective: true`, 3/3 cycles `inventory_equal: true` and `exact_order: true`,
        0 missing / 0 extra, but it is pinned to `git_commit 491fa9ff`, **60 commits behind HEAD and
        before every Packet A/B/C implementation of this change**, so it is not evidence for the shipped
        behavior.
      * `captures/hitl-test-01-dump/` (`git_commit 2b756909`, `git_dirty: true`, track 6 slot 1) —
        `result: PASS`, storage equality true, 5/5 `inventory_equal: true`, 0 missing / 0 extra, but
        `exact_order: false` on every cycle and the wrong slot. **Storage equality alone does not close
        it**, which is exactly what this run shows.
      A5.2 therefore stays open; it is mandatory (A5.5) and its blocker is the recording-window failure.
      → **CLOSED 2026-09-24 (second session) — the base preset was the blocker, not the device.**
      **The recording failure is base-preset-specific.** A direct clock+note stream into the authorized
      scratch slot records and saves cleanly (new `scripts/hitl_record_content.py`, which sends control
      notes on the control channel — the base preset's failure does not reproduce), and the skill's own
      pipeline then passes. **Run identity:** `git_commit 0c65a99e97a43169c8a3a03e732d2d506de25a49`
      (clean tree), firmware hex `f3a33208591b3a087870e13023c63b529d8a27bab5da9499566009e453fee4a8`,
      `teensy41-capture-serial`.
      **Content:** `scripts/hitl_record_content.py --track 5 --loop-slot 1 --bars 2` →
      `captures/cc-03/` (`RECS,stage,seal,…,64,0,ok`; `Recording stopped length=1536`; `CurrentSet saved
      successfully`).
      **Verification:** `dump_and_capture.py` + `verify_playback_content.py` →
      `captures/cc-03-playback/content-review.json`:
      * `result: PASS`, `storage_matches_effective: true`, `length: 1536`,
        `CAPTURE_CONTENT,begin,track=5,slot=0,length=1536` (authorized scratch, 1-based loop 1);
      * **all 11 cycles `inventory_equal: true` AND `exact_order: true`**, `missing_count=0`,
        `extra_count=0`, `unexplained_missing_count=0` (64 rows per cycle);
      * `RING,overflow` rows: **0**; `max_bar_refs: 32`, `capacity_path_exercised: false`.
      **Stated scope:** this is a **parity** PASS (64 rows, 32 bar-refs), not a capacity re-stress (the
      skill's note: below the old 256-reference ceiling it is a parity check only). The dump's serial log
      has no boot banner because the reader attached mid-boot; the independently read content dump (64
      rows) and 11/11 matching playback cycles are the evidence, and 0 overflow rows rules out the stale-run
      signature. The base-preset recording failure remains a separately tracked harness/device interaction.

- [ ] A5.3 **Documentation closeout.** Update `docs/Runtime/PROJECT_STATE.md`,
      `docs/Runtime/CURRENT_WORK.md`, `docs/DELIVERABLE_TRACKING.md`, the
      `lazy-slot-hydration` main spec via the archive/sync step, and the affected guide
      (`docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md` or the storage guide) so no durable doc still
      describes the transport-gated restore rule this change removes
      ([`DOCUMENTATION_CLOSEOUT.md`](../../../docs/Authority/DOCUMENTATION_CLOSEOUT.md)).
      → **PARTIAL 2026-09-24 (second session).** Landed: `docs/Runtime/PROJECT_STATE.md` and
      `docs/Runtime/CURRENT_WORK.md` carry the session's status; the affected guide is
      **`docs/Guides/BOOT_LOAD.md`** (not `DEFERRED_RUNTIME_PERSISTENCE.md`), whose "Post-boot slot
      restore" row described the old "PLAYING allows focus/`LoadLoopJob` only" rule and now states the A2.2
      bounded-preparation admission, the still-idle commit/cleanup gate and its global
      `waiting_commit_gate`/`waiting_job_capacity` waits, and the task-3.3 boundary; a new row documents the
      A2.1 request identity and named waits. A repo-wide grep finds **no remaining durable doc** asserting
      the removed transport-gated restore rule. **Pending (archive step):** the `lazy-slot-hydration` main
      spec sync and `docs/DELIVERABLE_TRACKING.md`, which land with A5.6 once A5.5 shows no open mandatory
      gate — syncing the main spec early would advertise behavior whose device integration (A3.7) is not
      yet verified.

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
