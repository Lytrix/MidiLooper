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
  - `RESTORE_LIVENESS_RED=1 pio test -e native -f test_storage_manager_load` → **11 cases / 2 failed /
    8 succeeded**, the two failures being exactly the listed red fixtures (C1, C2).
- **Promotion log:** none yet — A1.2/A1.3 are promoted in the A2.2/A2.3 slices.

### A1.2 — C1 transport-gated background admission (desired behavior, red)

- **Shape:** transport-active runtime (`track 1` audibly playing, focus track stopped), a
  payload-bearing empty slot queued through the production queue, driven for a bounded number of turns
  through the production selection/child-admission calls.
- **Oracle (Packet A) — request ownership.** The request must be **served** (the slot committed, or the
  slot was reset and the request retired) or **owned** by a production mechanism: an active or parked
  job instance holding *this* `(track, slot)`. A queued request that no mechanism owns is the
  unexplained stall the change removes. Named waits do not exist until Packet B, so A2.2 replaces
  "owned" with `waiting_commit_gate` / `waiting_job_capacity`.
- **Red evidence (current `src/`, `RESTORE_LIVENESS_RED=1`):**
  `C1: the queued background restore was left unowned after 64 turns (served=0, ownedTurns=0,
  pending=1, active=0, parked=0, attempted=0, committed=0)`.
- **Mechanism finding (probe gap, now closed):** `StorageManager::probeActiveLoadLoopJob` reports only
  the **active** instance, so a parked job is invisible to it — yet a parked instance keeps its
  `SlotLoadSession`, which is exactly what `deferredSaveBlockedByActiveSlotLoadSd()` reports. Packet A
  therefore adds `StorageManager::probeParkedLoadLoopJob` (observation-only, no behavior change).
- **Rejected oracle (recorded so it is not reintroduced):** "did the bytes advance?" via
  `probeLoadLoopJobProgress`. Neither instance's `bytesRead`/`payloadSize` is cleared on every teardown
  path, so a leftover count from an earlier request or fixture reads as progress — it made C1 look
  green on the untouched tree. The probe is kept for failure messages only; the oracle is structural.
- **No `src/` behavior change** in this slice (the two probe additions are observation-only).

### A1.3 — C2 parked job never re-serviced (desired behavior, red)

- **Shape:** the runtime is idle, the background job is begun and given one production turn (which
  reads its small payload), **then** the transport starts on another track. The job is therefore parked
  in `Parsing` while holding the slot-load session.
- **Oracle:** `served || activelyOwned` — the request reached a terminal, or the **active** instance
  held it for at least one turn. A parked instance legitimately owning the request is not success,
  because a parked job is never stepped; Packet B replaces this with the named-wait oracle.
- **Red evidence:** `C2: the parked background job was never serviced after 64 turns (parkedEnd=1,
  parkedPhase=1, ownedTurns=64, activelyOwnedTurns=0, pending=0, attempted=0, committed=0)` — the
  parked instance holds the request for every turn and the active instance never gets it.

### A1.5 — C4 head-of-line blockage (in progress; fixture not yet landed)

Two fixture-shaping facts measured while drafting it, recorded so the next attempt does not repeat
them:

1. **The owner must be a production term, and the interval route is expensive.**
   `Track::noLiveOwnerForSlot` counts four terms: a live `captureStopJobs_` ring entry, the runtime's
   `captureStopOwner`, a pending prepared pair, and the **S4f.1 accepted-interaction latch**.
   `Track::latchAcceptedStop` is private, but `TrackCaptureStopTestAccess` is a declared friend, so the
   suite gained `latchAcceptedStopForSlot` (committed; unused until the fixture lands). That is the
   cheap, production-state way to hold the priority-head slot.
2. **The head must be uncommitted, and the startable slot must be reset.**
   The pop's done-test (`isLoopSlotRestoreAttempted || getLoop(slot).hasCommittedPasses()`) discards a
   **committed** head as already satisfied — correct queue hygiene, not the C4 defect — so a
   committed head cannot express head-of-line blockage. Symmetrically, `observeRestoreOutcome`'s
   `served`/terminal reading is per-slot, and an earlier fixture in the same binary can leave a
   committed slot 2, which makes the C4 assertion read false-green. The startable slot must be
   production-reset and asserted empty in the fixture's own setup; a draft that omitted that step
   passed on the untouched tree and was discarded rather than weakened.

---

## Mechanism ledger (A1.9)

Recorded per mechanism: covered by a red desired-behavior fixture, or withdrawn with the observation
that falsified it. Filled in as Packet A proceeds; **do not check A1.9 while any mechanism is
unreviewed.**

| Mechanism | `design.md` claim | Packet A disposition | Fixture |
|---|---|---|---|
| C1 | `canRunBackgroundLoadLoopNow()` false under transport; nothing begins | red fixture in place; mechanism reproduced (request left **unowned** for 64 production turns: pending, no active or parked job) | `test_c1_transport_gated_background_admission_desired` |
| C2 | A parked non-focus job is never re-serviced under play | red fixture in place; mechanism reproduced (parked instance owns the request for all 64 turns, actively owned 0) | `test_c2_parked_job_is_serviced_or_named_after_transport_starts` |
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
| 2026-09-23 | A1.2 | as above | red as required: *left unowned after 64 turns (served=0, ownedTurns=0, pending=1)* |
| 2026-09-23 | A1.3 | as above | red as required: *never serviced after 64 turns (parkedEnd=1, parkedPhase=1, ownedTurns=64, activelyOwnedTurns=0)* |
