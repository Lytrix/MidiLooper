# Execution packets, mechanism ledger, and evidence — deferred-slot-restore-liveness

Authority: [G2 execution-packet workflow](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
(DEC-080), the approved contract in [`design.md`](design.md) § G2 execution packets, and
[`tasks.md`](tasks.md). Numeric task ids live in `tasks.md`; this file carries the *evidence* they
require and the mechanism ledger A1.9 asks for.

**Status: Packet A in progress. A1.1 — native restore harness implemented and native-verified. No
production (`src/`) behavior change has been made by this change.** Everything not listed under
*Evidence record* below is specified only.

Evidence vocabulary (DEC-080): **specified** → **fixture implemented** → **native verified** →
**device verified**. None implies the next. A listed command is not evidence until its output is
recorded here.

---

## Common packet controls

- **Owner:** `StorageManager` (restore queue, `LoadLoopJob`, deferred save, blocking drain). No
  ownership move in this change.
- **Baseline revision:** `feature/runtime-working-set-ownership`, HEAD `5315965e`, clean `src/` tree.
  Planning artifacts (`design.md`, `proposal.md`, `specs/`, `tasks.md`, runtime docs) carry the
  uncommitted Revisions 2–4 corrections at the time of writing.
- **Harness:** `test/test_storage_manager_load` — the only suite that links the load path
  (`LoadLoopJob.cpp`, `LoopSlotRestoreQueue.cpp`, `SlotLoadAdmission.cpp`, `SlotLoadSession.cpp`,
  `LoadLoopSelectionPolicy.cpp`, `LoadLoopBudget.cpp`, `FileIo.cpp`, `CurrentSetStorage.cpp`,
  `StorageManager.cpp`) against the in-memory SD card (`test/test_support/firmware/SD.h`).
- **Controllable inputs:** transport (`sequencerRunning`), per-track `isRecording()` /
  `isAudiblePlaybackActive()` (`Track::forceSetState`), SD payload set (production body writer),
  queue state (production enqueue + the forced-enqueue helper).
- **Placement rule for every packet:** new code `STORAGE_PERSIST_MEM`/`FLASHMEM`; new state outside
  ITCM; post-link RAM1 line recorded per slice that links new code
  (`scripts/check_ram1_budget.py`, floor `code 392,328 / padding 888` as recorded in `tasks.md`).

**Measured harness fact (A1.1).** `trackManager`'s default track state in this suite is
`TRACK_PLAYING` — `isAudiblePlaybackActive()` is true before a fixture does anything. Every Packet A
fixture must therefore set its runtime eligibility explicitly (`RestoreHarness::setRuntimeTransportIdle`
/ `setTrackAudiblyPlaying` / `setTrackRecording`); inheriting the default silently gates every
background restore, which is exactly the defect under test.

---

## Packet A — desired-behavior acceptance fixtures + observability (no behavior change)

### A1.1 — native restore harness increment

- **Owner/API (unchanged):** `StorageManager::selectSubmittedLoadJobs`,
  `StorageManager::stepSubmittedLoadJobs`, `StorageManager::requestLoopSlotRestoreFromSd`,
  `StorageManagerInternal::appendBootLoopSlotRestore`.
- **Primary files:** `test/test_storage_manager_load/load_and_restore_harness.h` (new),
  `test/test_storage_manager_load/test_storage_manager_load.cpp`. **No `src/` change.**
- **Transition oracle (smoke):** empty slot + a valid SD payload + one production-queued request →
  bounded production turns → the request is consumed exactly once, the job commits, the slot holds the
  payload's own content (`loopLengthTicks` 768 from the fixture, committed passes present), the queue
  is empty, no job instance remains active.
- **Fixture mechanics:** payloads are written with the **production** body writer
  (`writePersistedLoopSnapshot`) through the production write boundary (`storageIoFromFileWrite`),
  prefixed by the production epoch header and suffixed with the production save token, then renamed
  into place with the production path helper. Drafting bytes by hand was rejected: a fixture that
  passes against a format the device never writes proves nothing.
- **Verification recipe:**
  `pio test -e native -f test_storage_manager_load` → **8 cases, 8 succeeded, 0 failures**, suite
  PASSED (2026-09-23).
- **Evidence:** smoke case
  `test_restore_harness_smoke_commits_a_queued_request_through_production_path` green; case count 8;
  linked production TUs listed under *Common packet controls*.

### A1.11 — red-acceptance coexistence rule (delivery prerequisite for A1.2–A1.7)

- **Rule:** the desired-behavior fixtures are red on the current `src/`. They are registered
  unconditionally and gated at `main()` by a **test-read environment variable** `RESTORE_LIVENESS_RED`
  (the `WORKING_SET_RED` precedent — not a build define). Unset ⇒ the known-red fixtures do not run;
  set ⇒ they run and are expected red. Each fixture is promoted into the default gate **in the same
  commit that turns it green**; the flag is deleted when the set is empty.
- **Verification:**
  - `pio test -e native -f test_storage_manager_load` → **8 cases / 8 succeeded / 0 failures** (flag
    unset; the red fixture is absent, not run-and-failing).
  - `RESTORE_LIVENESS_RED=1 pio test -e native -f test_storage_manager_load` → **10 cases / 1 failed /
    8 succeeded**, the one failure being exactly the listed red fixture.
- **Promotion log:** none yet — A1.2 is promoted in the A2.2 slice.

### A1.2 — C1 transport-gated background admission (desired behavior, red)

- **Shape:** transport-active runtime (`track 1` audibly playing, focus track stopped), a
  payload-bearing empty slot queued through the production queue, driven for a bounded number of turns
  through the production selection/child-admission calls.
- **Oracle (Packet A):** either the request is served, or its **preparation measurably advances**
  (a phase change, or positive `bytesRead` on either job instance). "Neither" is the unexplained stall
  this change exists to remove. Packet A cannot use the named-wait oracle because the named waits do
  not exist yet; A2.2 replaces "progressing" with `waiting_commit_gate` / `waiting_job_capacity`.
- **Red evidence (current `src/`, `RESTORE_LIVENESS_RED=1`):**
  `C1: the queued background restore was neither served nor progressing after 64 turns (pending=1,
  phase=255, jobActive=0)`.
- **Mechanism finding (why the oracle is right, and a gap in the existing probe):** the job is selected
  and then **parked** by `stepSubmittedLoadJobsImpl` (`!jobIsFocus && !backgroundAllowed` ⇒
  `parkActiveLoadLoopJob()`), while the selection path re-promotes it on the next turn
  (`resolveParkedIdleAction` ⇒ `PromoteParkedLow`) without any read occurring: the request cycles
  between the two job instances and consumes turns without progress.
  `StorageManager::probeActiveLoadLoopJob` reports only the **active** instance, so a **parked** job is
  invisible to it — the probe reports `phase=255` (none) while a parked instance still holds the
  `SlotLoadSession` that keeps `deferredSaveBlockedByActiveSlotLoadSd()` true. The A1.1 progress probe
  therefore sums **both** instances. A1.8's probe shape should name the parked instance explicitly.
- **No `src/` behavior change** in this slice.

---

## Mechanism ledger (A1.9)

Recorded per mechanism: covered by a red desired-behavior fixture, or withdrawn with the observation
that falsified it. Filled in as Packet A proceeds; **do not check A1.9 while any mechanism is
unreviewed.**

| Mechanism | `design.md` claim | Packet A disposition | Fixture |
|---|---|---|---|
| C1 | `canRunBackgroundLoadLoopNow()` false under transport; nothing begins | red fixture in place; mechanism reproduced (queued request neither served nor progressing over 64 production turns) | `test_c1_transport_gated_background_admission_desired` |
| C2 | A parked non-focus job is never re-serviced under play | open — the park/re-promote cycle was observed while diagnosing C1 | A1.3 (planned) |
| C3 | one-shot enqueue never re-offers | **withdrawn before code** (G2 review finding 5): the boot scan already offers every payload-bearing `(track,slot)` and a blocked request stays queued | none — no work produced |
| C4 | priority head with a live owner stalls the queue behind it | open | A1.5 (planned) |
| C5 | the blocking drain never services the load scheduler | open | A1.7 (planned) |
| C6 | `markLoopSlotRestoreAttempted` latches a transient failure for the power cycle | open | A1.6 (planned) |

---

## Evidence record

| Date | Task | Command / artifact | Result |
|---|---|---|---|
| 2026-09-23 | A1.1 | `pio test -e native -f test_storage_manager_load` | **PASSED** — 8 cases, 8 succeeded, 0 failures; smoke case drives a queued request to a committed slot through production calls only |
| 2026-09-23 | A1.11 | `pio test -e native -f test_storage_manager_load` (flag unset) | **PASSED** — 8 cases, 8 succeeded, 0 failures |
| 2026-09-23 | A1.11 | `RESTORE_LIVENESS_RED=1 pio test -e native -f test_storage_manager_load` | 10 cases, **1 failed / 8 succeeded** — exactly the listed red fixture |
| 2026-09-23 | A1.2 | as above | red as required: *neither served nor progressing after 64 turns (pending=1, phase=255, jobActive=0)* |
