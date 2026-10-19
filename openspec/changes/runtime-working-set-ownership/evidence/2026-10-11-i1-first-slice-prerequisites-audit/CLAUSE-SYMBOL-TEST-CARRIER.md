# I1 — clause -> symbol -> test -> carrier map (audit-only)

Packet **I1** of `docs/Plans/openspec_iterative_closeout_plan.md` (iteration manifest). Its exit predicate is
"a clause->symbol->test->carrier map per task, plus a characterization run. **No checkbox.**"

Task clauses audited (this file, quoted openings):
`S2b.0 **First-slice prerequisites (same commit boundary).**`,
`S4f.8 Make the waits observable as **scheduling**, not refusal`,
`S4f.9 Prove no new capacity is implied` — all in
[`tasks.md`](../../tasks.md). Contract cited by the clauses:
[`IMPLEMENTATION_CONTRACT.md`](../../IMPLEMENTATION_CONTRACT.md) §1 (executable first slice: boot/load canonical
admission).

**Nothing in this map is a checkoff.** A row says where a clause's work lives and what asserts it; it does not
claim the clause's own acceptance. Native evidence is not promoted to a timing, device or deadline claim.

Revision at audit time: branch `feature/unified-rolling-playback-path` @ `37a63011`, dirty tree with 45
pre-existing unrelated entries (modified/deleted/untracked), all preserved — this audit added only this
evidence folder. Characterization commands and their raw results: [EXECUTION.md](EXECUTION.md).

---

## How to read a row

| column | meaning |
|---|---|
| **clause / term** | the clause's named term, or one prerequisite the clause names |
| **symbol** | the production symbol that carries it, with file (no line numbers as authority) |
| **test** | the fixture/suite that asserts it, by test name |
| **carrier** | the durable artifact that records it |

A missing test or carrier is written as a gap in § Gaps — never inferred from a document that merely describes
the behavior.

---

## S2b.0 — First-slice prerequisites (same commit boundary)

**Clause as written (status `[ ]` unchecked):** "Implement the S3.0 placement pins, Stop-only
S4f.0–S4f.2/S4f.4/S4f.5 path and existing-owner S4.0 liveness specified by `IMPLEMENTATION_CONTRACT.md` §1 before
removing reservations. Later stages extend these helpers; S5 reconstruction joins the predicate only when
introduced. Validate stop retry/transport retargeting together with boot removal; do not ship a
reservation-removal-only intermediate commit."

**Boundary observation (audit finding, no checkbox written):** the "same commit boundary" the clause guards has
already shipped — `S2b.1` (per-slot boot reservation removed) is checked **DONE 2026-09-23** and its own note
records that the prerequisites shipped with it. The clause is therefore landed-in-substance but has no
completion record of its own, and two of the clauses it names (`S4f.1`, `S4f.2`) are formally **`[ ]`** open on
*later* members that the tasks themselves delegate elsewhere. It cannot be checked as written without first
recording which prerequisite clauses the landed Stop-only path satisfies; that recording is not this packet's
mandate (audit-only).

| clause / term | symbol | test | carrier |
|---|---|---|---|
| **S3.0** placement and size pins | `Track::placementPins()` with `static_assert(sizeof(PendingInteractionLatch) == 32)`, `offsetof(slot) == 2`, `offsetof(interval) == 8`, `is_trivially_copyable`, `sizeof(Track) == 384`, `sizeof(CaptureStopJobs) == 29032` (`include/Track.h`); device-ABI pins `sizeof(LoopPlaybackRuntime) == 7632` (host `9768`), `sizeof(Loop) == 992`, `sizeof(TrackPlaybackRuntime) == 32` (`src/Track/TrackPlaybackRuntime.cpp`) | compile-time: a wrong pin fails the device build in every TU including `Track.h`; post-link guard `scripts/check_ram1_budget.py` against `scripts/ram1_budget.json` | [`ARCHITECTURE-REVIEW.md`](../../ARCHITECTURE-REVIEW.md) § P1 — S3.0 placement and size pins; the re-base history is carried in the code comments beside each pin |
| **S4f.0** slot-parameterised stop admission | `Track::beginDeferredOverdubStopForSlot(destination, tick, slot, interval)`; `Track::beginDeferredOverdubStop` as the live-edge wrapper passing `activeLoopIndex`; `stale_interval` report on a dead interval | `test_s4f4_a_request_whose_interval_is_gone_is_superseded_not_bound` (`test_continuous_capture`) | § P1 — S4f.4 transition 3: a stale request is superseded, not parked |
| **S4f.1** the latch (Stop variant) | `Track::PendingInteractionLatch` (32 B POD, carried inside the externally allocated `CaptureStopJobs`, not on `Track`); `Track::latchAcceptedStop`, `Track::consumeAcceptedStopObligation`, `Track::acceptedStopSlot`, `Track::acceptedStopWaitCount` (`include/Track.h`, `src/Track/TrackOverdubLifecycle.cpp`) | `test_b02_accepted_stop_waits_for_canonical_credit_then_executes_once`; `test_s4f10c/d/e` (same-tick queueing, transport supersession, undo replacement) | § P1 — accepted-stop latch, drive and park implemented; [`S22-LATCH-EVIDENCE-PACKET.md`](../../../response-driven-hitl-acceptance/S22-LATCH-EVIDENCE-PACKET.md) for the sequenced-identity half |
| **S4f.2** drive placement and reachability | `Track::hasRealtimeDeferredIdleWork()`, `Track::serviceAcceptedInteractionTurn()`, `Track::driveAcceptedInteraction()`; the dispatch branch in `Track::processDeferredIdleMaintenance`; `processDeferredCanonicalPrefix` yields while an interaction is merely accepted | `test_s4f2_deferred_eligibility_includes_an_accepted_interaction_on_an_idle_track` (in `TRACK_STOPPED` only the latch term can satisfy the predicate) | § P1 — B06's eligibility decision extracted; § P1 — the latch drive's declared bound (`ExecutionBounds.md` row 31) |
| **S4f.4** transport epoch and retargeting | transport-epoch callers named by S4f.4; `driveAcceptedInteraction` recomputes the boundary in the tick domain at execution | `test_s4f4_the_boundary_is_recomputed_in_the_tick_domain_at_execution` (recorded limit: the `ClockManager` entries that move the domain are harness stubs, so production entry is device territory); `test_record_replaced_loop_supersedes_accepted_stop` / `test_changed_capture_mode_supersedes_accepted_stop` (`test_shared_capture_finalization`) | § P1 — B03 transport retarget, and why the obligation is consumed there; § P1 — S4f.4 transition 2 |
| **S4f.5** one in-flight predicate | `Track::hasStopInFlight()` (`hasPendingDeferredOverdubStop() \|\| hasAcceptedUnexecutedInteraction()`) and `Track::hasLiveCaptureInterval()`; adopted by the persistence predicates and `resolveActiveCaptureTrackSlot` | `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes` | § P1 — S4f.5's stop-related in-flight consumers (also lists the `getActiveLoopIndex()`-keyed release sites tracked in S4.1) |
| **S4.0** per-slot liveness (existing owners) | `Track::noLiveOwnerForSlot(slot)` (`src/Track/TrackPlaybackWindowBuild.cpp`), consulted by `src/StorageManager/LoopSlotRestoreQueue.cpp` before a restore request is removed; counts the S4f.1 latch as a live owner | `test_runtime_working_set_ownership`: `noLiveOwnerForSlot` refuses while an owner remains and frees on retirement; `test_storage_manager_load` holds a slot through `noLiveOwnerForSlot` | § P1 — S4.0 per-slot liveness and S2b.4 reservation release |
| stop retry / transport retargeting **together with boot removal** | `Track::prewarmPlaybackForSlot(s, false)` reserves no canonical output; the park (`parkAcceptedStopForCanonicalOutput`) acquires what it needs in a deferred turn | `test_b02_...` (accepted -> waits -> executes once when the credit frees); `test_s2b6_park_terminal_never_cancels_an_accepted_stop` | § P1 — S2b.1/S2b.2 boot/load canonical admission |

**Carrier check against the packet I1 names.**
[`evidence/2026-10-10-edge-effect-boot/EXECUTION.md`](../2026-10-10-edge-effect-boot/EXECUTION.md) carries the
*boot entry point* half of this clause: `main.cpp::setup` -> existing `TrackManager::setup` -> boot-only
edge-effect initialization -> later `ClockManager::setup`, with the linked ordering verified and the RAM1 line
unchanged. It is a boot-path carrier, not a prerequisite-completion carrier: it records no S2b.0/S3.0/S4f.0–S4f.5
acceptance, and its own hardware gate is still open.

---

## S4f.8 — Make the waits observable as scheduling, not refusal

**Clause as written:** "`accepted_stop_finalizer_wait` counts waits and names the holding owner; the pre-fix
counterfactual is `successor_stop_denied_capacity` + `reentryDecision("deny", ...)`."

| clause / term | symbol | test | carrier |
|---|---|---|---|
| the wait is reported as **scheduling** | `DebugSessionCapture::overdubStopStage(..., "accepted_stop_finalizer_wait", resolveTrackIndexForPersistence(*this))` in `Track::parkAcceptedStopForCanonicalOutput` (`src/Track/TrackOverdubLifecycle.cpp`); wire form `#CAP,<us>,ODUB,stop,<stage>,...,<outcome>,track=<n>` (`src/Utils/DebugSessionCapture.cpp`) | `test_s4_5_finalization_busy_owner_vocabulary_is_tier_a` (`test_capture_line_tier`) classifies the outcome literal as Tier-A | § P1 — accepted-stop latch, drive and park implemented; design/S4F closure text for the counter rename |
| **counts waits** | `PendingInteractionLatch::waitCount` incremented once per park turn, read back by `Track::acceptedStopWaitCount()`; the wire row is emitted **once per episode** (`waitReported` guard) | **gap** — no fixture asserts `acceptedStopWaitCount()` (see § Gaps G2) | same |
| **names the holding owner** | the row's trailing `,track=<n>` (house sentinel `0xFF` = not supplied, appended so a positional reader that stops earlier still parses); the *resource* actually holding the refusal is named by the `busyReason` literal at the deny site (`stop_job_pending`, `canonical_output_busy`, ...) | **gap** — no fixture asserts the trailing `track=` field on these rows, and the latch/`busyReason` literals are not asserted as a pair (see § Gaps G3) | same |
| the pre-fix counterfactual, retained for the oracle | `Track::stopOverdubbing()` emits `ODUB,stop,admission,...,successor_stop_denied_capacity` and `DebugSessionCapture::reentryDecision("deny", tick, interval, slot, depth)`, then latches instead of dropping | read by the host oracle `scripts/hitl/retention_probe.py` (L2/L5) and covered by `scripts/test_retention_probe.py` | § P1 — accepted-stop latch, drive and park implemented; `FALSIFICATION.md` F3 |
| the wait is not a refusal | `accepted_stop_park_terminal` after `waitCount == 8` is a report-only fault/escalation; it never clears, drains or cancels | `test_s2b6_park_terminal_never_cancels_an_accepted_stop` (drives 40 turns past the threshold; the request survives, no second job) | § P1 — S2b.6: a stale annotation, and a design/spec conflict the terminal must not paper over |
| the latch's own sequenced identity (so a wait inside one poll gap stays provable) | `SC_ACCEPTED_LATCH("wait", ...)` -> `DIAG,latch,wait,<track>,<slot>,<interval>,<destination>,waiting` (`Track::interactionLatchTransition`) | `test_task22_the_latch_family_is_accounted_and_read_back`, `test_task22_a_refused_latch_append_is_counted` (`test_capture_ring`) | [`S22-LATCH-EVIDENCE-PACKET.md`](../../../response-driven-hitl-acceptance/S22-LATCH-EVIDENCE-PACKET.md) |

**Carrier check against the packet I1 names.**
[`evidence/2026-10-01-s4f14-cross-track-leg/FINDING.md`](../2026-10-01-s4f14-cross-track-leg/FINDING.md) is the
device-side carrier that reads the counter for attribution ("no `accepted_stop_finalizer_wait` attributable to
track 4's job while track 4 finalizes concurrently", the only latch edges being track 4's own). It is a
cross-track *attribution* observation on a recorded run, not a completion record for this clause.

---

## S4f.9 — Prove no new capacity is implied

**Clause as written:** "the latch is one small state per live interval, not a stored stopped interval, so the
descriptor-ring finding is unchanged."

| clause / term | symbol | test | carrier |
|---|---|---|---|
| the latch is **one small state per live interval** | `Track::PendingInteractionLatch` — 32 B, trivially copyable, no pointers, no transport epoch; size- and offset-pinned in `Track::placementPins()` (`include/Track.h`) | compile-time `static_assert`s; the pin is structural, so drift fails the device build rather than a fixture | § P1 — S3.0 placement and size pins; DEC-078 item 10 |
| it is **not a stored stopped interval** (no descriptor, no ring capacity) | `Track::latchAcceptedStop` writes only the latch; the stop-descriptor ring (`Track::captureStopJobs_`, `CaptureStopJobs::kCapacity`, `Track::captureStopQueueDepth()`) is untouched by the latch; the latch lives **inside** the externally allocated `CaptureStopJobs` object, so `sizeof(Track)` is unchanged | `test_b02_...` asserts `queueSize == 0` while the stop is latched and the interval is still live; `test_s2b6_...` asserts `queueSize == 0` across 40 stalled park turns; `test_full_stop_ring_denies_admission_and_preserves_accepted_capture` covers the genuine ring-exhaustion path | § P1 — S3.0 placement and size pins (the `unique_ptr` form measured 64 B of RAM1 and took padding 872 -> 856, below the floor — that measurement is why the latch lives in the ring object) |
| the descriptor-ring finding is **unchanged** | `CaptureStopJobs` depth and tail-free requirement; `queue_full` remains exceptional exhaustion, not ordinary wait | as above, plus the ring-family counters in `test_capture_ring` | DEC-078 item 7 (the ring is still a ring), item 10 (no new capacity); § P1 — S3.0 placement and size pins |

**Pin-value drift (audit finding, no fix).** The S3.0 clause text records the 2026-09-23 originals
(`PendingInteractionLatch` 40, `CaptureStopJobs` 2,736, `LoopPlaybackRuntime` 6,344, `Loop` 984); the enforced
assertions today read 32, 29,032, 7,632 (target; host 9,768) and 992. The **pin is maintained** — each re-base is
recorded beside the assertion in `src/Track/TrackPlaybackRuntime.cpp` — but the task text was never updated, so a
reader trusting the clause gets stale numbers. Recorded as a documentation debt, not an implementation defect.

---

## Gaps named by this audit (no fix in this packet)

| # | gap | evidence it is real | who owns it |
|---|---|---|---|
| **G1** | `S2b.0` is landed-in-substance with no completion record, while two clauses it names (`S4f.1`, `S4f.2`) stay `[ ]` on later members | `S2b.1` is checked DONE 2026-09-23; `S4f.1`="PARTIAL ... Outstanding: the start/switch carriers and the supersession rules -- those are S4f.3/S4f.6"; `S4f.2`="Still outstanding: the scheduler's *use* of the eligibility predicate for a stopped track is in `src/main.cpp`, which is not native-linkable" | whoever reconciles the boot/admission stage; **not** this audit (it checks no box) |
| **G2** | the "counts waits" half of `S4f.8` has no assertion: `Track::acceptedStopWaitCount()` is read by no fixture | `grep -rn "acceptedStopWaitCount\|waitCount" test/` returns nothing; the accessor exists and is used only by the park's own threshold | a future S4f.8 slice, if the count is to be evidence rather than an internal guard |
| **G3** | the "names the holding owner" half has no wire-level assertion: no fixture checks the trailing `,track=` on `ODUB,stop` rows, and no fixture pairs the latch literal with the `busyReason` literal | the five `stop,admission` references in `test/` are all in `test_capture_line_tier` and assert **Tier-A classification** of the outcome literal only | same as G2 |
| **G4** | `S4f.9`'s compile-time pins are the enforcement, but the S3.0 clause text still carries the 2026-09-23 numbers | code pins vs task text, both quoted above | documentation closeout of the S3.0 clause |
| **G5** | `S4f.8`'s device-side attribution and `S4f.9`'s device-side "no new capacity" claim are not qualifiable natively | `test_s4f4_the_boundary_is_recomputed_in_the_tick_domain_at_execution`'s own recorded limit; `S4f.5`'s named non-native-linkable consumer | the change's device gates, which I1 does not enter |

---

## What this map does **not** say

- It does not check `S2b.0`, `S4f.8` or `S4f.9`, and it does not move any parent stage.
- It does not claim `S4f.8`/`S4f.9`'s acceptance: two of their halves have no fixture (G2/G3) and the device half
  is untouched (G5).
- It does not treat a document's statement as code. Every symbol above was read in the tree at `37a63011`; every
  test name above is registered in its suite.
- It does not promote native results to timing, device, pixel or deadline claims, and it enters no hardware gate.
