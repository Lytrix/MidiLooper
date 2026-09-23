# Tasks — deferred-slot-restore-liveness

Scope: `docs/Runtime/CURRENT_WORK.md`; motivation and non-goals: `proposal.md`; how: `design.md`.
Every slice follows the [G2 execution-packet workflow](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
and its packet in `design.md` § G2 execution packets. Establish the discriminating production-path
fixture **before** the behavior edit, then record actual native/build/device evidence before checkoff
(DEC-080). A listed fixture or command is not evidence until it has run and its output is recorded here
or in the change's review file.

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

- [ ] A1.2 **[C1 — transport-gated background admission] fixture, red.** Drive a queued background
      restore with the transport running and one track audibly playing; assert the request does not
      reach `committed` (the wrong current behavior) while the queue is non-empty and no job is
      parked/active for it. **Verify:** the case is red on current `src/`, with the failure message
      naming the gate; record the exact assertion output. No `src/` change in this task.

- [ ] A1.3 **[C2 — parked job never re-serviced] fixture, red.** Begin a non-focus background job
      while idle, then set the playing condition and drive turns; assert the job is parked and makes no
      further progress. **Verify:** the case is red and the parked job's identity is unchanged across
      turns; record the probe output.

- [ ] A1.4 **[C3 — background-fill admission is one-shot] fixture, red.** Run boot fill admission to
      completion, drain or block part of the queue, and assert that
      `enqueueRemainingLoopSlotRestoresFromSd` no longer offers the unprocessed payload. **Verify:** the
      case is red and names `bootBackgroundFillAdmissionComplete_` as the stop; record it.

- [ ] A1.5 **[C4 — head-of-line blockage] fixture, red.** Queue a priority-head request whose slot is
      held by a live owner plus a lower-priority startable request; assert that no restore begins and
      the startable request stays queued. **Verify:** the case is red; the assertion distinguishes
      "head blocked" from "queue empty"; record it.

- [ ] A1.6 **[C6 — attempt latch] fixture, red.** Inject an SD read/open failure for one payload, drive
      the restore to its failure path, then make the payload healthy and assert the `(track, slot)`
      never restores again in the same session. **Verify:** the case is red and names
      `loopSlotRestoreAttempted_` as the latch; record the injected failure and the observed terminal.

- [ ] A1.7 **[C5 — drain blocked by slot load] fixture, red.** Owe a deferred save, hold a `LoadLoopJob`
      in flight that cannot progress, and run the blocking drain; assert it aborts with the
      blocked-by-slot-load verdict after the full stuck budget and that CLEAR would be refused.
      **Verify:** the case is red and reproduces the device verdict wording; record the iteration count.

- [ ] A1.8 **Observability, event-shaped (per `design.md` D6).** Add capture-tier lines: one per restore
      terminal with `(track, slot)` and reason, and an on-demand queue/state probe reusing the existing
      `probeActiveLoadLoopJob` shape; add the boot reconcile named outcome (`ok` / `repaired` /
      `failed`) alongside the existing warning. **Verify:** native cases assert the probe's fields for a
      queued, a blocked, an in-flight and a committed request, and assert the reconcile outcome mapping;
      full native suite green; capture-serial build SUCCESS with the RAM1 line recorded. **Do not**
      emit a per-turn state line (capture-ring overflow precedent).

- [ ] A1.9 **Packet A review and mechanism ledger.** Record, for C1–C6, one of: reproduced by fixture
      `<case>`, or withdrawn with the observation that falsified it; record the TBD-1 conclusion and
      whether any mechanism is a *different* root cause than `design.md` states. **Verify:** the
      ledger is written into the change review, and every Packet B/C task below is either unblocked by
      a reproduced mechanism or explicitly re-scoped. Do not check A1.9 while any C-fixture is
      unreviewed.

- [ ] A1.10 **Packet A gate.** Full native suite green (count recorded); `teensy41-capture-serial`
      build SUCCESS with the post-link RAM1 line recorded; Packet A red/green status table complete.
      **Verify:** the exact commands and outputs are recorded; no behavior change is claimed by this
      packet.

## 2. Packet B — background restore liveness (D2, D3, D5)

- [ ] A2.1 **Terminal record for a restore request (D1).** Introduce the per-`(track, slot)` terminal
      (`committed` / `unnecessary` / `refused` + reason), updated at every path that currently pops or
      marks attempted, stored beside the existing restore arrays outside ITCM. **Verify:** native cases
      assert exactly one terminal per request and that a request never re-enters the queue after a
      terminal; full native suite green; RAM1 line recorded and unchanged except as recorded.

- [ ] A2.2 **Background-restore admission reason (D2).** Add the bounded background-restore service
      slice so a queued background request advances while playback continues, serviced after MIDI
      service with its own declared budget; keep recording deferred and keep the focus/High path
      unchanged. **Verify:** A1.2 and A1.3 flip from red to green; a new case asserts the per-turn work
      stays within the declared budget; the capture-serial build's `overflow_free` standing gate is
      re-checked on device in task A2.6.

- [ ] A2.3 **Re-arm the background fill (C3).** Make the remaining-payload offer idempotent so an
      unprocessed payload is re-offered after the one-shot admission completes, without re-probing the
      whole SD set per turn. **Verify:** A1.4 flips red→green; a case asserts the offer does not repeat
      unboundedly (bounded per turn) and does not re-offer a terminal request.

- [ ] A2.4 **Startability-aware queue service (D3).** Select the first startable request in priority
      order, leaving a blocked entry queued with its reason; keep the S2b.5 `queuedRestoreMayBegin`
      semantics and the existing done-tests. **Verify:** A1.5 flips red→green; a case asserts priority
      order is preserved among startable requests and that a blocked entry is still served once its
      owner retires.

- [ ] A2.5 **Bounded retry then named refusal (D5).** Replace the permanent attempt latch with a
      declared bounded attempt count per power cycle, ending in a `refused` terminal that names the
      reason; keep `unnecessary` for absent/invalid payloads. **Verify:** A1.6 flips red→green; cases
      assert retry-then-refuse for a persistent failure and retry-then-commit for a transient one; no
      retry storm (attempts bounded per turn and per power cycle).

- [ ] A2.6 **Packet B device gate.** Build and flash `teensy41-capture-serial` (ask the owner first —
      never flash unprompted) and run the canonical base stimulus on the authorized scratch slot, then
      a reboot. **Verify:** the restore terminal lines appear for the boot payloads; no payload is left
      without a terminal; `overflow_free=True`; MIDI gap within the standing bound; the slot that was
      `EMPTY` before now restores. Record the capture path and the exact lines. If the device is not
      available, this task stays open and Packet B is not closed.

- [ ] A2.7 **Packet B gate.** Full native suite green; the A1.2–A1.6 fixtures green in the default
      gate (not behind an env flag, unless the flag is justified and recorded); capture-serial RAM1
      line recorded; A2.6 evidence recorded. **Verify:** no oracle was weakened; the slice's commit
      boundary is one buildable commit.

## 3. Packet C — drain reconciliation (D4)

- [ ] A3.1 **Drain progress awareness (D4).** Teach the blocking drain to distinguish "waiting on a
      restore that is progressing" from "waiting on a restore that cannot progress", and to terminate
      as soon as the restore reaches a terminal. **Verify:** A1.7 flips red→green; a case asserts the
      drain completes once the restore terminates, without cancelling it; a case asserts the
      cannot-progress wait reports the named blocked-by-restore reason rather than `Stuck`.

- [ ] A3.2 **Preserve the DEC-081 and capture behavior.** Assert in cases that audible playback is
      still not a persistence gate, that a recording track still parks the multi-frame writers, and
      that the mid-pass one-chunk flush still runs during a take. **Verify:** the named regression cases
      pass; no change to `isCaptureRecordingForPersistence` semantics.

- [ ] A3.3 **Packet C device gate.** With a deferred save owed and a restore in flight, press CLEAR on
      the device. **Verify:** CLEAR completes; **0** `Clear aborted` / `Persistence drain stuck`;
      either the drain completed after the restore terminal or the log names the blocked-by-restore
      reason. Record the capture path. If cancellation turns out to be necessary, STOP and return to
      design for the `DEC` (proposal TBD-3) rather than implementing it here.

- [ ] A3.4 **Packet C gate.** Full native suite green; A3.1/A3.2 cases green; RAM1 line recorded;
      A3.3 evidence recorded.

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
      the unit, background restore is admitted while playing, the drain reconciles rather than
      cancels) with its device evidence, and record O1/O2 if they were resolved by measurement.
      **Verify:** the index row, the Next-ID bump, and the entry body are present and consistent.

- [ ] A5.5 **Archive readiness.** All gates above are taken or explicitly recorded as unrun with the
      reason; then archive per `openspec-archive-change`. **Verify:** `openspec validate` passes and the
      main spec reflects the shipped behavior.
