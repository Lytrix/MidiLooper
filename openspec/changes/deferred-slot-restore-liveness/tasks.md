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

- [ ] A1.1 **Native restore harness increment: link the queue and the load job into one controllable
      suite.** Extend `test/test_storage_manager_load` (or add its sibling) so a test can enqueue,
      inspect and drain restore requests through the production entry points
      (`StorageManager::selectSubmittedLoadJobs`, `stepSubmittedLoadJobs`,
      `enqueueRemainingLoopSlotRestoresFromSd`) with a controllable transport, controllable
      `isRecording` / `isAudiblePlaybackActive` inputs, and a controllable SD payload set.
      **Verify:** the suite builds and stays green, and one smoke case drives a queued request to a
      committed slot through production calls only. Record the case count and the linked production TUs.

- [ ] A1.2 **[C1 — transport-gated background admission] desired-behavior fixture, red.** Drive a queued
      background restore with the transport running and one track audibly playing, through the production
      admission path (the extracted `hasRealtimeDeferredIdleWork` predicate plus the load-job child
      admission — **not** `stepSubmittedLoadJobs()` directly). Assert the **desired** outcome: the request
      is served — its preparation advances and it reaches a terminal or a named wait (including
      `waiting_commit_gate`) within a bounded number of turns. **Verify:** the case is **red on current
      `src/`** because the request is neither served nor in a named wait, and it is registered in the
      change's named exclusion set (A1.11) so the default gate stays green. Record the assertion output
      and the exclusion flag. No `src/` change in this task.

- [ ] A1.3 **[C2 — parked job never re-serviced] desired-behavior fixture, red.** Begin a non-focus
      background job while idle, then set the playing condition and drive turns; assert the **desired**
      behavior: the job receives further service or is reported in a named wait, and it is not parked
      indefinitely without a reason. **Verify:** the case is red on current `src/`; the parked job's
      identity is asserted across turns; record the probe output.

- [ ] A1.4 **[C3 — one-shot background-fill admission] WITHDRAWN (review finding 5).** Do not implement a
      fixture or a behavior change for C3. The boot scan already offers every `(track, slot)` with a
      verified payload, and a blocked request remains in `pendingLoopSlotRestores_`, so no state exists in
      which a request is neither queued, active/parked, nor terminal. Re-arming would duplicate the retry
      policy without addressing starvation. **Verify:** the withdrawal is recorded in the mechanism ledger
      (A1.9) with the code citation; the corresponding implementation task is deleted, not deferred.

- [ ] A1.5 **[C4 — head-of-line blockage] desired-behavior fixture, red.** Queue a priority-head request
      whose slot is held by a live owner plus a lower-priority startable request; assert the **desired**
      behavior: the startable request begins and the blocked head stays queued with its reason. **Verify:**
      the case is red on current `src/` (today nothing begins); the assertion distinguishes "head blocked"
      from "queue empty"; record it.

- [ ] A1.6 **[C6 — attempt latch] desired-behavior fixture, red.** Inject a transient SD read/open failure
      for one payload, drive the restore to its failure path, then make the payload healthy; assert the
      **desired** behavior: the request is retried and reaches `committed` (a persistent failure instead
      reaches `refused` after the bounded attempt count). **Verify:** the case is red on current `src/`
      because the slot is latched for the session; record the injected failure and the observed state.

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

- [ ] A1.11 **Red-acceptance coexistence rule (review finding 3).** Define and wire the named exclusion set
      for the red desired-behavior fixtures (a dedicated env flag in the change's own suite, following the
      `WORKING_SET_RED` / `ONLY_COMPANION` / `REENTRY_RED` precedent). Each fixture is promoted into the
      **default gate in the same commit that turns it green**; the flag is deleted when the set is empty.
      **Verify:** the default `pio test -e native` run is green with the flag unset; a run with the flag set
      shows exactly the listed fixtures red; the promotion is checked off per fixture in the slice that
      fixes it.

## 2. Packet B — background restore liveness (D2, D3, D5)

- [ ] A2.1 **Request identity and state separation (D1).** Introduce the request identity
      `(track, slot, requestGeneration, setEpoch)` and the separate **wait** states
      (`waiting_slot_owned`, `waiting_capture`, `waiting_commit_gate`, `waiting_retry`) versus the four
      **terminals** (`committed`, `unnecessary`, `refused`, `superseded`); key the record by the full
      identity, advance `requestGeneration` on clear/reset/commit-by-another-owner, and invalidate
      outstanding requests on a set change as `superseded`. Stored beside the existing restore arrays
      outside ITCM; the existing `loopSlotRestoreAttempted_` latch is retired or subsumed. **Verify:** native
      cases assert exactly one terminal per identity; that a wait never converts to a terminal from elapsed
      turns alone; that recording deferral is a wait; that a reused `(track, slot)` is a different request;
      and that a set change yields `superseded` (not `refused`); full native suite green; RAM1 line recorded.

- [ ] A2.2 **Background-restore preparation admission (D2).** Add a bounded background-restore service
      slice so a queued background request's **preparation** advances while playback continues, serviced
      after MIDI service with its own declared budget; keep recording deferred, keep the focus/High path
      unchanged, and **do not touch the commit stop-gate** (`MidiServiceDrain::admitLoadJobChild`) — a
      prepare-complete request waits in `waiting_commit_gate`. **Verify:** A1.2 and A1.3 flip from red to
      green; a case asserts a prepare-complete request under active transport enters `waiting_commit_gate`
      and applies when the gate admits; a case asserts the per-turn work stays within the declared budget.

- [ ] A2.3 **Startability-aware queue service (D3).** Select the first startable request in priority
      order, leaving a blocked entry queued with its reason; keep the S2b.5 `queuedRestoreMayBegin`
      semantics and the existing done-tests. **Verify:** A1.5 flips red→green; a case asserts priority
      order is preserved among startable requests and that a blocked entry is still served once its owner
      retires.

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
      A3.3 task.)*

## 3. Packet C — the blocking drain advances the restore (D4)

- [ ] A3.1 **Drain-side restore service (D4).** Give the blocking drain a restore-service step that uses
      the existing selection/admission (`selectSubmittedLoadJobs` + `stepSubmittedLoadJobs`) plus one
      bounded advance of an admitted job, servicing the **active and parked** instances, within a declared
      per-iteration budget; no second loader, queue, or admission policy. **Verify:** A1.7 flips
      red→green; a case calls the **real blocking drain** with no external advancement between assertions
      and asserts the save completes; a case with a **parked** job holding the session asserts the wait
      clears; a case asserts a wait the drain must not break is reported as its named reason.

- [ ] A3.2 **Named drain verdict (D4).** Replace the generic `BlockedBySlotLoad` verdict with
      `BlockedByRestore(<request wait reason>)` so the drain reports *why* it could not advance, and keep
      the existing `BlockedByCapture` / `BlockedByPostLoadHoldoff` distinctions. **Verify:** cases assert
      the named reason for capture-active, commit-gate and retry waits; no case asserts a generic `Stuck`
      where a named reason is available.

- [ ] A3.3 **Preserve the DEC-081 and capture behavior.** Assert in cases that audible playback is still
      not a persistence gate, that a recording track still parks the multi-frame writers, and that the
      mid-pass one-chunk flush still runs during a take. **Verify:** the named regression cases pass; no
      change to `isCaptureRecordingForPersistence` semantics; the drain's slice budget stays derived from
      queued SD work.

- [ ] A3.4 **Packet C device gate.** With a deferred save owed and a restore in flight, press CLEAR on
      the device. **Verify:** CLEAR completes; **0** `Clear aborted` / `Persistence drain stuck`; either
      the drain completed after the restore terminal or the log names the blocked-by-restore reason.
      Record the capture path. If cancellation turns out to be necessary, STOP and return to design for
      the `DEC` (proposal TBD-3) rather than implementing it here.

- [ ] A3.5 **Packet C gate.** Full native suite green; A3.1–A3.3 cases green; RAM1 line recorded;
      A3.4 evidence recorded.

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
