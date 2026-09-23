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
  - `RESTORE_LIVENESS_RED=1 pio test -e native -f test_storage_manager_load` → **13 cases / 4 failed /
    8 succeeded**, the four failures being exactly the listed red fixtures (C1, C2, C4, C6).
- **Promotion log:** none yet — A1.2/A1.3 are promoted in the A2.2/A2.3 slices, A1.5 in A2.3.

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

### A1.5 — C4 head-of-line blockage (desired behavior, red)

- **Shape:** the priority head is a request whose slot a live owner holds **and** which has no
  committed content; a lower-priority request for a free, uncommitted slot sits behind it. Both enter
  through the forced boot entry, since this fixture is about queue ordering.
- **Oracle:** the startable request is served or owned, the blocked head is still queued afterwards, and
  its owner is still live. `pending` distinguishes "head blocked" from "queue empty".
- **Red evidence:** `C4: the startable request behind a blocked head was never served or owned after 32
  turns (served=0, unowned=1, pending=2, committed2=0, attempted2=0, active=0, parked=0,
  headStillQueued=1, headOwned=1)`.
- **Mechanism correction to `design.md` C4.** The design says `popNextDeferredLoopSlotRestore` "takes the
  priority head and has no track/slot filter, so a head that cannot start blocks every entry behind it".
  The **first half is incomplete**: the pop drains *every* done entry from the front
  (`isLoopSlotRestoreAttempted || getLoop(slot).hasCommittedPasses()`), so a **committed** head is
  *skipped*, not blocking, and the lower-priority entry is then served normally. The genuine stall needs a
  head that fails the done-test **and** fails `noLiveOwnerForSlot` — i.e. a slot with a live owner and no
  committed content, which is the capture-interval window. Two earlier drafts built on a committed head
  were green on the untouched tree and were discarded rather than weakened (the second only after the
  `setUp` defect below was fixed). The mechanism is real, but the design's stated trigger is narrower than
  written; the fixture now encodes the exact pair.
- **Suite defect this surfaced (fixed):** `setUp` cleared the restore queue and the attempted flags but
  **not** the `LoadLoopJob` instances, which are function-local statics in `LoadLoopJob.cpp`. An earlier
  fixture's parked job therefore survived into the next fixture and owned *its* request, making an
  outcome assertion read a state the fixture never produced (measured: a leftover parked job made C4 read
  "owned" while its own request was still queued). `StorageManagerInternal::clearLoadLoopJob()` now runs
  in `setUp`.

### A1.6 — C6 attempt latch (desired behavior, red)

- **Shape:** a payload-backed restore whose first read attempt fails transiently, then the payload is
  healed and further turns are driven with no external advancement of the request.
- **Injection:** the slot file is truncated by 3 bytes. The save token is at the **end** of the slot
  file, so the failure lands in the **read** stage (short read) rather than a parse stage — a genuine
  transient failure of the class C6 latches.
- **Two-phase oracle:** phase 1 asserts the production failure path was actually reached
  (`attempted=1`, not committed); phase 2 asserts the healed payload is retried to a commit. Without
  phase 1 a green phase 2 could simply mean the injected failure never happened.
- **Red evidence:** `C6: a healed payload was never retried after a transient failure (committed=0,
  attempted=1, pending=0, active=0, parked=0)` — the latch holds the slot for the power cycle.
- **Harness additions:** `snapshotSlotPayloadBytes`, `replaceSlotPayloadBytes`, `truncateSlotPayload`.

### A1.7 — C5 the blocking drain cannot advance the restore (closure measured; fixture not yet landed)

A1.7 requires the **real** `drainPersistenceWorkBlocking`, and this path has zero native coverage. Two
rounds have been spent measuring what retaining that call actually costs, rather than modelling the drain
with a test-only stand-in (which the G2 packet rules forbid as acceptance evidence).

**Landed and kept** (`test/test_storage_drain_restore`, in `scripts/native_test_link.py`'s
`_FIRMWARE_BOUNDARY_SUITES`):

- Boundary stubs, device-faithful and additive: `PSTR`, `NativeSerial::printf_P`,
  `NativeSerial::print(value, base)` with `DEC/HEX/OCT/BIN`, `File::flush`, `O_CREAT`/`O_RDWR`/`O_AT_END`.
- **Directory handles in the in-memory card.** The saved-set scan reads the sets archive with
  `open(dir)` + `openNextFile()` + `isDirectory()` + `name()`, which the flat path map did not model.
  Added as the directory form of `SDClass::open` plus the three `File` methods, with the card global
  forward-declared before `File` (a first attempt that stored an `SDClass*` on the handle created a
  declaration cycle). The load suite — critical path for every native suite — was run immediately after
  and stayed **8/8 green**.
- **Overlay boundary:** `LooperStateManager::setLoadSaveModeForTest`, because `exitLoadSaveMode`'s
  definition sits in `src/LooperState.cpp`, which also defines the harness-owned `looperState` global.
  `RtcTime` (whose TU includes `core_pins.h`) is supplied by `drain_harness_stubs.h`.
- The suite links a 15-TU import set and runs one behaviour-free case proving that set compiles and links.

**Corrected finding — the tail is not three boundary items.** Retaining the actual drain call revealed a
second, larger class that the earlier enumeration missed:

| Remaining need | Owner | Note |
|---|---|---|
| `DisplayManager::invalidateForSlotChange`, `EditManager::emitEditEvent`/`flushDeferredNoteEditDisplayRefresh`/`persistActiveNoteEditSession`/`reopenNoteEditSession`, `LoopEditManager::commitLoopEditOnDepart`/`reopenLoopEditSession` | display + edit managers | cold UI routes, unreachable from a drain fixture but referenced through the workspace-save path |
| `TrackManager::getMasterLoopLength`/`setMasterLoopLength`/`forceMidiLedUpdate`/`hasQueuedRecordingTrack`/`reclaimUnreferencedDisabledPasses`, `Track::silenceSlotMidiOutput` | `TrackManager*`/`Track` owners | `Track::silenceSlotMidiOutput` lives in `TrackPlaybackHotPath.cpp`, which also **redefines** the harness's `silenceTrackMidiOutput` boundary |
| `SavedSetCatalog::parseSavedSetFolderName`/`readSavedSetMetadataTrailer`, `PersistenceSchema::crc32*`, `RevisionPackedBlob::*`, `SetRevisionCatalog::*` | catalog + revision pack | the revision/catalog import set |
| `StorageManagerInternal::applyLoadedTrackStateAfterLoopSlots` | `CurrentSetBootLoad.cpp` (already included) | still unresolved after inclusion, so it has a second owner |

**A latent production defect, found and fixed (2026-09-23).**
`TrackManagerCaptureQueue.cpp`'s call to `requestLoopSlotPersistAndSaveState` was **ambiguous**: the
capture-queue TU defines a `(const Track&, uint8_t, uint8_t)` overload in an anonymous namespace while
`include/TrackInternal.h` declares a global `(Track&, uint8_t, uint32_t)` one (defined in
`TrackStopTelemetryColdHelpers.cpp`), and the literal argument converts to both. The firmware never sees
it because it compiles each TU separately; an include-based harness that joins them does, which is how it
was found. Fixed with an explicit `::` qualification at the call site (one line plus a comment); the
firmware build and the native suites were both re-run after the fix. This unblocked the capture-queue TU
natively, which in turn supplies `hasQueuedRecordingTrack` and the capture-side `TrackManager` members.

**Assessment, after attempting the expansion.** A fifth round took the "absorb it" path and measured
where it actually ends: retaining the real drain call pulls the **display stack** as well
(`DisplayManager::DisplayManager()` and its `_display` member drag the OLED/Adafruit-GFX font chain into
the native build), on top of the persistence stack and the cold display/edit/track-manager owners. The
native drain path is therefore close to the whole firmware, and the expansion cannot be justified as a
test-only closure.

**Revised recommendation — split, do not absorb.** Packet C's requirements divide cleanly:

- **Pure decisions are already native-testable** and should carry the tight oracles: drain budget and
  progress accounting (`PersistenceSyncDrainBudget`, `buildSyncDrainBudget`, `madeSyncDrainProgress` —
  already a native suite), the A3.2 wait-reason classification, and the A3.4 three-way caller contract
  mapping. These are the parts where a device run is a slow and weak oracle.
- **The integration claim — "the drain advances the restore it waits on" (A3.1)** — is what needs the
  real thread, and that belongs to the device gate (A3.7), with A2.5/A3.7's capture evidence as its
  oracle. The contract already has that gate; this only says the native fixture cannot be the primary
  evidence for it.

This keeps A1.7's deliverable honest: **the drain's C5 defect stays read-from-code with its mechanism
cited, plus a native closure proof for everything that does link**, and the red-fixture claim moves to the
packets whose logic is separable. Amending `tasks.md` A1.7/A3.1 to that scope is the next step and is
recorded here rather than taken silently. No fixture, red evidence or diagnostic count is claimed for
A1.7.

### A4.1 — boot catalog reconcile outcome (native verified)

- **Change:** `SetIndexReconcileOutcome { Ok, Repaired, Failed }` and
  `reconcileSetIndexOnSdWithOutcome` in `SavedSetIo.cpp`; `reconcileSetIndexOnSd` remains the
  `Ok || Repaired` compatibility wrapper, so no caller gains new semantics. The boot site
  (`BootRecovery.cpp`) now logs the named outcome with `printf_P`/`PSTR` (RAM1-boundary literals) and
  keeps the existing warning for `Failed` only.
- **New suite:** `test/test_storage_reconcile_index`, registered in `native_test_link.py`. It links
  `SavedSetIo` + `SavedSetCatalog` + `PersistenceSchema` + `CurrentSetStorage` + `FileIo` against the
  in-memory card, with a test wall clock in place of `RtcTime`.
- **Evidence:** **4 cases / 4 succeeded** —
  consistent index ⇒ `Ok`; missing index ⇒ `Repaired`, index written back, second reconcile `Ok`;
  malformed index ⇒ `Repaired`, reread ⇒ `Ok` (the repair is durable and not repeated); the bool
  wrapper matches the outcome. RAM1 `code 392,328 / padding 888` unchanged; firmware build SUCCESS.
- **Why it matters:** the device's `could not reconcile MidiLooper/sets/index.bin` line could not be
  separated from a normal boot, which is the ambiguity the proposal lists under observability.

### A1.8 — observability (partial: probe + reconcile landed; transition lines with Packet B)

- **Landed — queue/state probe.** `StorageManager::RestoreStateProbe` + `probeRestoreState` report:
  queued count; the priority head's `(track, slot)` and **startability**; each job instance's owned
  request with phase and read progress; and whether the deferred save is held by restore work
  (`saveBlockedBySlotLoad`, `saveBlockedByPostLoadHoldoff`). `queueHeadStartable` is the field that
  replaces a bisect: it is the difference between "the head is blocked on an owner" and "the queue is
  empty".
- **Landed — reconcile outcome mapping.** See A4.1.
- **Native cases (default gate, observation-only):**
  `test_restore_state_probe_reports_queue_and_job_ownership` covers four states in one case (nothing
  queued / queued-and-startable / in-flight with payload size / live-owned blocked head), and
  `test_restore_state_probe_reports_the_save_blocked_by_restore` asserts the probe agrees with the
  drain's own gate predicate rather than duplicating it.
- **Evidence:** suite **10/10** green by default; `RESTORE_LIVENESS_RED=1` → **15 cases / 4 failed**
  (C1, C2, C4, C6 exactly); full native **2,297 / 1 skipped / 2,296 succeeded / 0 failures**;
  capture-serial SUCCESS with RAM1 `code 392,328 / padding 888`.
- **Open, deferred to Packet B:** the capture-tier **state-transition lines** and the `terminal` /
  `prepare-complete-waiting` probe states. Emitting them now would require inventing the request-state
  vocabulary that A2.1 defines, which is exactly the "do not fabricate a state" line the design draws.
  No per-turn state line is emitted (capture-ring overflow precedent respected); the probe is on-demand.

### A2.1 — request identity and state separation (in progress: record + birth/committed wiring landed)

- **New owner, no new module boundary:** `src/StorageManager/RestoreRequestState.cpp`, declared in
  `StorageManagerInternal.h`, beside the existing restore arrays. `STORAGE_PERSIST_DATA` for the records
  (external allocation) and `FLASHMEM` for every function.
- **Identity:** `(track, slot, requestGeneration, setEpoch)`. The generation advances **only** through
  `advanceRestoreRequestGeneration`, which the call sites for an *external* change use; the restore path's
  own failure cleanup keeps the identity and names a retry wait. `noteRestoreSetEpoch` supersedes
  outstanding requests before the epoch moves.
- **State separation:** `RestoreRequestState { Queued, Running, Waiting, Terminal }` with
  `RestoreRequestWait { SlotOwned, Capture, CommitGate, JobCapacity, Retry }` and
  `RestoreRequestTerminal { Committed, Unnecessary, Refused, Superseded }`, each with a name function for
  the observability work.
- **Wiring landed:** born `queued` at both queue-entry sites; `running` in `beginLoadLoopJob`; the
  `committed` terminal in `commitLoadLoopJobPublish`.
- **Native evidence:** suite **18/18**; full native **2,305 / 1 skipped / 2,304 succeeded / 0 failures**.
  The cases include the production-driven
  `test_restore_request_state_is_driven_by_the_production_path` and the six identity/terminal contracts.
- **Two defects the cases caught (both fixed):**
  1. **The identity check was self-satisfying.** `restoreRequestRecordIsCurrent` compared the record's
     stored generation against the per-slot counter — but the advance *writes the new counter into the
     record*, so a stale record always compared equal and the "an earlier terminal must not describe the
     new request" contract was unobservable. Fixed with an explicit `identityValid` flag that only
     `beginRestoreRequest` sets, so an advance leaves the record describing **no** request.
  2. **The set-change supersede wiped its own outcome.** `noteRestoreSetEpoch` recorded `superseded` and
     then cleared the records, erasing the terminal it had just written. The epoch now moves first and the
     records are rebuilt as superseded terminals in one pass.
- **RAM1 was enforced, not advisory.** The first version cost 64 B of ITCM (`padding 888 → 824`, below the
  approved floor of 840) and the post-link budget gate failed the build. Making every helper `FLASHMEM`
  restored `code 392,328 / padding 888` with 48 B of headroom.
- **Named wait wired (fixture-first):** `test_restore_request_slot_owned_denial_names_the_wait` was
  written red first (*Expected 2 Was 0* — the request stayed `queued`), then the three selection denials
  were wired to `markRestoreRequestWaiting(..., SlotOwned)`: the empty-idle priority-head check, the focus
  branch's early return, and the parked/focus branch. A request blocked by a live owner now reports
  `waiting_slot_owned`, which is the observable form of C4 and the reason the A1.8 probe's
  `queueHeadStartable` matters.
- **Still open:** the remaining wait transitions (`waiting_capture`, `waiting_commit_gate`,
  `waiting_job_capacity`) and the `refused`/`unnecessary` terminals land with A2.2–A2.4 (they are the gates
  those tasks introduce); pre-publication identity validation and `loopSlotRestoreAttempted_` retirement
  remain in A2.1/A2.4.

### A2.2 — background-restore preparation admission (native verified; C1/C2 promoted)

- **Change:** `canPrepareBackgroundLoadLoopNow()` admits a bounded background **preparation** slice under
  active transport; `backgroundJobMayBeServicedNow()` keeps commit/cleanup on the pre-existing full
  admission, so `canRunBackgroundLoadLoopNow()` still governs applying and the commit stop-gate
  (`admitLoadJobChild` in `main.cpp`) is **untouched**. Capture keeps its existing deferral — the
  preparation predicate returns false whenever a track is recording, which is the SD-session exclusion
  (proposal TBD-2 / design O1), so no recording behavior changed.
- **Named wait:** `markRestoreRequestWaiting(..., CommitGate)` fires when the phase becomes
  commit/cleanup under a denying runtime, so a prepare-complete request reports `waiting_commit_gate` on
  the same turn. This is the wait `design.md` D2 requires and the one the global commit gate implies.
- **Promotion (A1.11):** `test_c1_transport_gated_background_admission_desired` and
  `test_c2_parked_job_is_serviced_or_named_after_transport_starts` **flipped red→green** and moved into
  the default gate in this same slice, as A1.11 requires. The known-red set is now C4 (A2.3) and C6
  (A2.4) only.
- **New case:** `test_restore_request_prepare_complete_waits_at_commit_gate` asserts that preparation
  completes, the commit is **not** applied (`hasCommittedPasses()` false), and the request is waiting or
  terminal — never still queued.
- **Evidence:** suite **22/22** default; `RESTORE_LIVENESS_RED=1` → **25 cases / 2 failed** (C4, C6
  exactly); full native green; RAM1 `code 392,328 / padding 888`; firmware SUCCESS.
- **Not claimed:** that a restore applies under active transport (runtime-maintenance 3.3), or that
  queue-head skipping resolves the global `waiting_job_capacity` blockage.

---

## Mechanism ledger (A1.9)

Recorded per mechanism: covered by a red desired-behavior fixture, or withdrawn with the observation
that falsified it.

**TBD-1 conclusion (proposal § Open decisions).** The observed stuck restore is **dominated by the
transport gate** (C1/C2), with two independently reproduced contributors: C4's head-of-line stall needs
the narrower trigger recorded above, and C6's session latch makes a transient failure permanent. The
`(track,slot)` census changing between boots is explained by C6: a latched slot stays `EMPTY` for the
power cycle and a later boot restores it. C3 was **withdrawn before code** — the boot scan already offers
every payload-bearing `(track,slot)` and a blocked request stays queued, so no state exists in which a
request is neither queued, active/parked nor terminal. Two mechanisms differ from `design.md`'s stated
form: **C4**'s trigger (a committed head is skipped, not blocking) and **C1/C2**'s relationship (they are
one gate seen from two sides — the selection path never begins under transport, and a job begun while idle
is parked and never re-serviced).

| Mechanism | `design.md` claim | Packet A disposition | Fixture |
|---|---|---|---|
| C1 | `canRunBackgroundLoadLoopNow()` false under transport; nothing begins | **covered by red fixture**; mechanism reproduced (request left **unowned** for 64 production turns: pending, no active or parked job) | `test_c1_transport_gated_background_admission_desired` |
| C2 | A parked non-focus job is never re-serviced under play | **covered by red fixture**; mechanism reproduced (parked instance owns the request for all 64 turns, actively owned 0) | `test_c2_parked_job_is_serviced_or_named_after_transport_starts` |
| C3 | one-shot enqueue never re-offers | **withdrawn before code** (G2 review finding 5): the boot scan already offers every payload-bearing `(track,slot)` and a blocked request stays queued | none — no work produced |
| C4 | priority head with a live owner stalls the queue behind it | **covered by red fixture**; mechanism reproduced, with the trigger **corrected**: the head must also fail the pop's done-test (a committed head is skipped, not blocking) | `test_c4_blocked_priority_head_does_not_stall_a_startable_request` |
| C5 | the blocking drain never services the load scheduler | **covered by code citation + closure proof, oracle split by Revision 5.** `drainPersistenceWorkBlocking` (`SyncDrainSaveState.cpp`) loops on `processDeferredSaveState(state)` + `yield()` and never invokes `processDeferredLoopSlotRestore`, whose only production caller is the main loop's deferred-load frame — so while the drain owns the thread the restore cannot advance and `deferredSaveBlockedByActiveSlotLoadSd()` cannot clear. A native red fixture needs the display stack (not a test-only closure); the integration claim moves to the A3.7 device gate, the pure decisions to A3.2/A3.3/A3.4 | A1.7 (amended, closure proof green) |
| C6 | `markLoopSlotRestoreAttempted` latches a transient failure for the power cycle | **covered by red fixture**; mechanism reproduced (failure path reached, healed payload never retried) | `test_c6_transient_failure_is_retried_not_latched` |

---

## Evidence record

| Date | Task | Command / artifact | Result |
|---|---|---|---|
| 2026-09-23 | A1.1 | `pio test -e native -f test_storage_manager_load` | **PASSED** — 8 cases, 8 succeeded, 0 failures; smoke case drives a queued request to a committed slot through production calls only |
| 2026-09-23 | A1.11 | `pio test -e native -f test_storage_manager_load` (flag unset) | **PASSED** — 8 cases, 8 succeeded, 0 failures |
| 2026-09-23 | A1.11 | `RESTORE_LIVENESS_RED=1 pio test -e native -f test_storage_manager_load` | 10 cases, **1 failed / 8 succeeded** — exactly the listed red fixture |
| 2026-09-23 | A1.2 | as above | red as required: *left unowned after 64 turns (served=0, ownedTurns=0, pending=1)* |
| 2026-09-23 | A1.3 | as above | red as required: *never serviced after 64 turns (parkedEnd=1, parkedPhase=1, ownedTurns=64, activelyOwnedTurns=0)* |
| 2026-09-23 | A1.5 | as above | red as required: *never served or owned after 32 turns (served=0, unowned=1, pending=2, headStillQueued=1)* |
| 2026-09-23 | A1.6 | as above | red as required: *healed payload never retried (committed=0, attempted=1, pending=0)* |
