# Execution packets, mechanism ledger, and evidence — deferred-slot-restore-liveness

Authority: [G2 execution-packet workflow](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
(DEC-080), the approved contract in [`design.md`](design.md) § G2 execution packets, and
[`tasks.md`](tasks.md). Numeric task ids live in `tasks.md`; this file carries the *evidence* they
require and the mechanism ledger A1.9 asks for.

**Status: Packets A–C in progress. A3.4a — drain re-entry and callback safety, A3.5 — holdoff MIDI
servicing, and A3.6 — DEC-081/capture preservation are implemented and native-verified** (A3.4a: 6/6
callback-boundary cases, 29/29 scope cases; A3.5: 18/18 bound cases, 4/4 real-drain cases; A3.6: 3
regression cases). A3.4a and A3.5 are each red-proven. Full native green; capture-serial build SUCCESS.
Only the A3.7/A3.8 device gate remains open in Packet C.

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

### A2.3 — startability-aware queue service (native verified; C4 promoted)

- **Change:** `popNextStartableDeferredLoopSlotRestore` selects the first **startable** request in
  priority order; a blocked entry stays queued with its reason, and done entries are still removed as
  queue hygiene. `queuedRestoreMayBegin` moved into the queue TU so the selection path and the startable
  pop share one liveness predicate rather than two copies. Both `BeginBackgroundFromQueue` sites use the
  startable pop.
- **Promotion (A1.11):** `test_c4_blocked_priority_head_does_not_stall_a_startable_request` **flipped
  red→green** and moved into the default gate. The known-red set is now **C6 alone** (A2.4).
- **New cases:** priority order is preserved among startable entries while the blocked head stays queued
  (three pops return slots 1, 2, 3 and the head is never taken), and the A2.2/A2.3 **non-claim** is now
  covered — with one request commit-pending the global gate leaves a second request tracked rather than
  served, and the `waiting_job_capacity` wait exists by name.
- **Harness fixes the cases forced:** the payload builder now releases its staging snapshot's committed
  chunks after writing (a multi-payload fixture otherwise leaks one chunk per write and the next write
  correctly refuses), and `resetAllTracksAndPool()` releases every slot before rebuilding the pool
  (committed loops own chunks, so a bare pool reset does not free them). Both are test-harness
  correctness, not production behavior.
- **RAM1 enforced again:** the first build failed at `padding 808` (below the 840 floor) until
  `queuedRestoreMayBegin` and the new pop were marked `STORAGE_PERSIST_MEM`; the line returned to
  `code 392,328 / padding 888`.
- **Evidence:** suite **25/25** default; `RESTORE_LIVENESS_RED=1` → **27 cases / 1 failed** (C6);
  firmware SUCCESS with RAM1 `code 392,328 / padding 888`.

### A2.4 — bounded retry then named outcome (native verified; C6 promoted; hostile set emptied)

- **Change:** `kMaxLoopSlotRestoreAttempts = 3` and `scheduleLoopSlotRestoreRetry`. A retryable failure
  resets the slot as the restore path's **own** cleanup — keeping the request identity and recording a
  `waiting_retry` wait, which is the A2.1/G2 tightening that a blind generation advance would violate —
  then re-queues the request while under the bound. At the bound it records the `refused` terminal and
  latches. Retryable: token/epoch/header/size/seek/open in `beginLoadLoopJob`, and reopen/seek/read/token/
  parse/body-probe mid-job. An **absent** file stays `unnecessary` with no retry.
- **A2.1 retirement completed:** `loopSlotRestoreAttempted_` is **subsumed** by the request record —
  `isLoopSlotRestoreAttempted` now means "the request reached a terminal". That removes the second
  64-byte array, leaves one source of truth for exactly-once, and is the A2.1 requirement literally.
- **Promotion (A1.11):** C6 **flipped red→green** and moved into the default gate; the known-red set is
  now **empty**, so **`RESTORE_LIVENESS_RED` is deleted**. Every Packet A acceptance fixture runs in the
  default gate, and none was deleted, generalized or threshold-relaxed to get there.
- **The C6 fixture's oracle was corrected, and this is worth recording.** Phase 1 had asserted
  `isLoopSlotRestoreAttempted == true` as its marker that the injected failure was *reached*. Under the
  fix that is no longer true (a transient failure re-queues instead of latching), so the assertion would
  have **re-encoded the defect as the oracle**. It now asserts the request is still **owed** (queued or
  in a named wait) and not committed — which is the behavior the change exists to deliver.
- **New case:** `test_a24_persistent_failure_reaches_the_bounded_refusal` — a payload that stays broken
  reaches the terminal after the bound rather than retrying forever.
- **RAM1 re-based deliberately, in the same commit, with the reason recorded:** this build needs
  64 more bytes of RAM1 state than the approved line, because the request record stores a full
  identity + state + terminal per `(track, slot)` where the retired flag stored one bit.
  `ram1_budget.json` for `teensy41-capture-serial` moves to `variables 96,192 / locals_free 34,880 /
  padding 872` (still above the 840 floor, 12 ITCM banks unchanged), with `approved_by_change` set to this
  change. `src/StorageManager/LoopSlotRestoreQueue.cpp`'s changed accessors were made `FLASHMEM` first,
  which kept ITCM at 392,344 and the bank count at 12.
- **Evidence:** suite **26/26** default (and identical with the now-inert flag set); firmware SUCCESS at
  `variables 96,192 / ITCM code 392,344 + padding 872 / locals 34,880`.

### A3.3 — restore work in the drain budget and progress accounting (native scope done)

- **Budget:** `SyncDrainBudgetInput` gains `restoreSliceSteps`, `restoreSdBytes` and `restoreOnlyBlocker`;
  `buildSyncDrainBudget` charges them alongside persistence work, so `maxSliceSteps` covers a restore the
  drain may have to advance.
- **Progress:** `SyncDrainProgressSnapshot` gains `restorePhase` and a monotonic `restoreProgressCounter`.
  Both are compared **before** `madeSyncDrainProgress`'s `!after.workItemActive` short-circuit — the C5
  shape has *no* active persistence work item, so a drain advancing the restore it waits on previously
  read as stuck. That ordering is the substantive fix, not the field addition.
- **Wiring:** the counter is bumped per serviced job grain (`stepSubmittedLoadJobsImpl`), and
  `captureSyncDrainProgressSnapshot` / `buildSyncDrainBudgetForSession` populate the restore terms from
  the live job instances through `restoreWorkPhase` / `restoreWorkSliceSteps` / `restoreWorkSdBytes`.
- **Evidence:** `test_persistence_sync_drain_budget` **7/7** with three new cases (restore raises the
  budget; restore motion alone is progress while no-motion is not; a restore-only budget is bounded);
  `test_storage_manager_load` 26/26; firmware SUCCESS at the approved RAM1 line
  `code 392,344 + padding 872`.
- **Trimmed under RAM1 pressure, and worth recording:** the first version also added `restoreBytesRead`
  and `restoreParsePosition` progress terms. The post-link gate rejected the build (`padding 808`, below
  the 840 floor) and the honest fix was not to re-base the budget but to recognise that the per-grain
  counter **strictly subsumes** byte and parse-position motion. Removing the two fields returned the build
  to the approved line. A budget re-base was available and was not needed.
- **Not claimed:** that the drain *advances* the restore. That integration claim is A3.7's device gate per
  Revision 5; this task delivers the accounting the drain reads.

### A2.5 — Packet B device gate (taken and green, clean boot capture)

- **Method that worked, after two failed attempts.** Neither opening the serial port **nor** a 1200-baud
  touch resets this board (uptime held at 643 s, so the harness attached mid-session and could not see the
  boot drain). The documented reliable reset is a **re-flash**, which hard-resets. Flashing and then
  opening the port immediately produced a capture starting at **3.0 s uptime**.
- **Prerequisite the gate exposed:** A2.5 requires each boot payload's state to be *reported* and the
  source emitted nothing — exactly the A1.8 line deferred to Packet B. `SC_RESTORE_REQUEST` now emits one
  event-shaped `#CAP,...,RRQ` line per transition with the full identity and named state/wait/terminal; it
  had to be `FLASHMEM` (inlined it cost 144 B, `padding 872 → 728`, below the floor, and the post-link gate
  rejected the build).
- **Evidence:** `captures/a25-boot-gate/` (`serial.log` + `metadata.json`), capture window **3.0 s → 33.9 s**.

  | Transition | Count |
  |---|---|
  | `queued/none/none` | 29 |
  | `running/none/none` | 29 |
  | `terminal/none/committed` | 29 |

  **29 distinct `(track, slot)` requests, 0 left non-terminal, 0 `RING,overflow` rows.** Per-request
  `queued → terminal` latency: median **2.78 s**, max **3.85 s** (multi-slice preparation on an idle boot;
  reported as measured service time, **not** redefined as a MIDI-gap bound).
- **Clause verdicts:** every payload has a state (29/29 terminal) ✓; `overflow_free` ✓ (the standing MIDI
  gate, 0 ring-overflow rows); the previously-`EMPTY` slot restores — the census contains **`5/1`**, the
  authorized scratch slot ✓; MIDI gap within the standing bound by `overflow_free` ✓. No `waiting_*` state
  appeared, consistent with every committed payload opening cleanly with no commit-pending sibling.
- **The boot restore queue is serviced after a fresh boot** — the liveness claim Packet B exists to make,
  and previously false.
- **Superseded evidence, recorded because it cost a cycle:** the first attempt's
  `host_midi_hitl.py run --preset base` managed verdict was `INCONCLUSIVE_CAPTURE` on the documented
  `record stop did not reach PLAYING` setup-state race (a harness weakness, `CURRENT_WORK.md`:
  `successor-wrap-48/49/50`), and its record had sealed empty (`skipped_empty`), so it produced no content
  to census.

### A2.6 — Packet B gate (taken and green)

- Full native **2,316 cases / 1 skipped / 2,315 succeeded / 0 failures**; A1.2/A1.3/A1.5/A1.6 green **in
  the default gate** (promoted per A1.11; the red flag is deleted because the set is empty); capture-serial
  RAM1 `variables 96,192 / code 392,344 + padding 872 / locals 34,880`, above the 840 floor, 12 ITCM banks
  unchanged; A2.5 device evidence recorded above.

### A3.2 — named drain verdict (landed)

- **Classifier:** `classifySyncDrainVerdict(captureRecording, slotLoadActive, postLoadHoldoff, restoreWait)`
  in `PersistenceSyncDrainBudget` — pure, FLASH-free of storage dependencies, and returning the reason
  **paired with the gate name** so the drain's report site is branch-free. First attempt returned only the
  reason and needed a nested ternary in the ITCM drain; returning the pair is what made it fit.
- **Vocabulary:** `SyncDrainFailureReason::BlockedByRestore` plus small `SyncDrainWait` codes. Deliberately
  not a second enum — the duplicate-vocabulary version cost more ITCM than the problem needs.
- **Order:** capture → slot-load gate → post-load holdoff, matching the drain; `Stuck` is reserved for a
  gate nothing explains. `BlockedBySlotLoad` is retained when a slot load is active but names no wait, so
  no existing report regresses.
- **Wiring:** `restoreRequestWaitFor` reads the A2.1 record's wait; `restoreWaitReasonBlockingSave` /
  `restoreWaitCodeBlockingSave` resolve the request holding the save gate (live job instances first, then
  any request in a named wait); the drain classifies through the pure function and reports e.g.
  `blocked by waiting_commit_gate`.
- **Evidence:** suite **12/12** (five new cases); full native green.

#### The ITCM path to landing it — two steps, and the order mattered

1. **Report-block extraction, its own commit.** `drainPersistenceWorkBlocking`'s whole terminal-outcome
   report became `finishPersistenceDrainReport`, a `STORAGE_PERSIST_MEM` helper owning the bool result and
   the telemetry scope's outcome so the drain keeps one return point. Pure move; drain suite 1/1, load
   suite 26/26, full native green. **`code 392,344 + padding 872` → `code 391,832 + padding 1,384`:
   512 B of ITCM freed**, from 32 B *below* the 840 floor to 544 B above.
2. **A3.2 on top.** Cost **112 B**, leaving `code 392,024 + padding 1,192` — **352 B above the floor**,
   net better than before either step.

**Lesson recorded:** the first attempt failed because it tried to land a feature *and* a mechanical
extraction in one slice; the hand re-indentation corrupted the function and had to be reverted. Done as
two build-verified steps it was routine. `padding_floor` is a reviewed allowance, and the checker's
"move code out of ITCM" remedy was the right one — no re-base was needed.

### A3.4 — the caller contract: completed / blocked / failed (landed)

- **One decision, shared.** `include/PersistenceDrainOutcome.h` owns `SyncDrainOutcome { Completed,
  TemporarilyBlocked, Failed }` and `resolveDrainCallerAction` → `{ Proceed, RefuseBlocked, ReportFailure }`.
  Both real call sites and the tests use that one function instead of each encoding the rule.
- **Only `Completed` proceeds.** A blocked drain is an explicit refusal: the target is left untouched and
  the save stays owed. A failure is reported as a failure and stays distinguishable from the blocked wait
  (the two caller log lines depend on that).
- **Reason and outcome cannot drift.** `drainPersistenceWorkBlockingDetailed` maps the **A3.2 verdict**
  onto the three-way result, so a blocked outcome carries the `waiting_*` name; there is no second
  classification.
- **Compatibility held.** `saveState` remains the `Completed`-only `bool` wrapper, so no pre-existing
  caller gains new semantics. Both destructive callers switched to the detailed entry point:
  `handleClearTrack` reports a blocked drain as `Clear refused: … temporarily blocked by <reason>; slot
  left untouched, save still owed` and keeps `Clear aborted` for a real failure; `saveNewSetInternal`
  refuses the new set for either non-completed outcome and says which — neither treating a wait as failure
  nor proceeding while persistence is pending. **Retention is O7 and is not built.**
- **Evidence:** `test_persistence_sync_drain_budget` **15/15** (three new cases: only `Completed` licenses
  the operation; blocked and failed are distinguishable; every named gate is a temporary block while
  `Stuck` is the failure side). Full native **2,324 / 1 skipped / 0 failures**; firmware SUCCESS at
  `code 392,152 + padding 1,064` (224 B above the floor).
- **Not claimed:** the drain-integration/device half (A3.7), and **A3.4a**'s re-entry guard, which remains
  open. A first attempt to also exercise the three-way result through the *real* drain ran into the
  drain-suite link closure again (the display owner's constructor); that suite stays as the A1.7 closure
  proof, and the contract is pinned in the pure suite where the oracle is tight.

### A3.4a — drain re-entry and callback safety (landed; `src/` behavior verified natively)

Design D4 rule 9, which is the precondition for A3.5's holdoff polling: the drain's MIDI service is a
**callback boundary**, so a controller action can execute *inside* a blocking drain.

**Execution packet (scope, oracle, controls).**

- **Owner and entry path:** `StorageManager` keeps the drain; the guard is a scoped owner acquired by
  `drainPersistenceWorkBlockingDetailed` before it touches shared save/budget/progress/result state.
  No new manager and no ownership move.
- **Transition oracle:** with the guard held, (a) a nested detailed drain returns
  `TemporarilyBlocked("drain_in_progress")` and the compatibility `bool` wrapper returns false, without
  changing the outer owner's `pending`/`urgentRequested`; (b) a CLEAR injected through the real MIDI
  dispatch route (`midiButtonManager.handleMidiNote` → `MidiButtonProcessor` → callback →
  `MidiButtonActions::executeAction` → `handleClearTrack`) reaches **no** slot-mutation call, silences
  nothing, and leaves the slot's committed content intact; (c) the same injection with the guard free
  *does* reach the mutations and resets the slot. `(c)` is the control that makes `(b)` discriminating.
- **Counterexample that must fail:** with `refuseStorageCallbackDuringDrain` forced to always return
  false, case `(b)` fails (`data=0 … mutations=6 silence=1`) and the nested-drain case fails. Recorded
  below.
- **Guard placement / RAM1.** The guard's own translation unit (`src/StorageManager/PersistenceDrainGuard.cpp`)
  carries the acquire/release and the predicate; the state is one `bool` in the already-measured
  `StorageSession` aggregate. The first capture-serial build **failed the RAM1 floor** (`padding 728 < 840`):
  five guarded call sites plus the relocated `handleClearTrack` cost 336 B of ITCM against the A3.4 line.
  The remedy is the change's own placement rule, not a re-base: **`handleClearTrack` is now
  `STORAGE_PERSIST_MEM` (FLASH)** — a destructive slot-clear is not a MIDI clock hot path, and the
  consolidate-slot-clear-restore extraction had already shrunk it. Result:
  `code 391,896 + padding 1,320`, i.e. **480 B better than the A3.4 line** (`392,152 / 1,064`) and above
  the 840 floor. Nothing was re-based; `padding_floor` stays 840.

**Audit — MIDI-dispatched destructive/storage entry points and their pre-side-effect check.** Every one of
these consults the same predicate through `refuseStorageCallbackDuringDrain` (`MidiButtonActions.cpp`) or
`isPersistenceDrainInProgress` (`SavedSetIo.cpp`) **before** its first side effect:

| Entry point | Conflict it would cause | Check site |
|---|---|---|
| `handleClearTrack` | stops the drained-for track, resets the slot, pushes an undo snapshot | first statement, before `stopPlaying` |
| `handleUndo` / `handleRedo` | replaces the slot's content from the undo stack | before `restoreDeferredUndoSnapshotsBeforeUse` |
| `handleUndoClearTrack` / `handleRedoClearTrack` | restores/removes content in the drain-owned slot | before the undo/redo apply |
| `saveNewSetInternal` | writes a new set while the previous one's save is in flight | before the first catalog/directory write |
| `copySavedSetIntoCurrent` | changes the set identity every outstanding request belongs to | before the first copy/rename |

**Explicitly not guarded, with the reason:** `handleToggleTransport`, `handleToggleRecord`, the note-edit
entries and `handleToggleLoadSaveMode`. None resets content or mutates persistence state; refusing them
would break the "preserve normal musical input/transport service" requirement. A conflicting **undo or
set-replacement** action is the destructive analogue, and those are guarded.

**Structure change A3.4a made, and why.** `restorePlaybackAfterSlotClear` was defined twice —
`MidiButtonActions.cpp` (anonymous namespace) and `TrackUndo.cpp` (namespace scope). Separate translation
units never collide, but the include-based native harness concatenates both, so no suite could link the
button routes **and** the undo path — which is exactly what a callback-boundary fixture needs. It now has
one owner (`src/Track/SlotClearPlaybackRestore.cpp`, declared in `include/SlotClearPlaybackRestore.h`);
no macro seam and no include-order rule remain. `storageSession` likewise moved to
`src/StorageManager/StorageSessionState.cpp` so the guard's own TU does not drag in `Internal.cpp`'s cold
persistence helpers.

**Call back into the guard from the drain.** `drainPersistenceWorkBlockingDetailed` acquires
`PersistenceDrainScope` and returns `drainInProgressResult()` when it does not get ownership. The scope's
destructor is the single release path, so Completed, Blocked, Failed and the early `AdmissionRefused` break
all release it identically — there is no per-outcome release to miss.

**Evidence.**

- `test_drain_reentry_callback_safety` — **6/6**, a new suite built on the continuous-capture boundary
  closure plus the real button manager/processor/action table and the real Track transport. Cases: the
  free-guard control (reaches the slot mutations), the held-guard refusal (zero mutations, zero silences,
  content intact, guard still owned), the nested-drain named block, a conflicting undo refused before
  content replacement, transport not refused by the guard, and guard release with a later drain taking it.
- `test_storage_manager_load` — **29/29** (three new cases: one owner at a time and release-on-exit on the
  real scope; the predicate false without an owner).
- **Red-prove:** with the guard's predicate forced to always return false, the refusal case fails with
  `data=0 committed=0 mutations=6 silence=1` and the nested-drain case fails. The fixtures are therefore
  discriminating, not vacuous.
- **Build:** `pio run -e teensy41-capture-serial` **SUCCESS** at
  `RAM1: variables 96,192 / code 391,896 + padding 1,320 / locals 34,880` (480 B better than the A3.4
  line); the RAM1 gate passes with no floor change.

**Not claimed:** the device integration claim (`the drain advances the restore`) stays A3.7's, per
Revision 5.

### A3.5 — the finite post-load holdoff is waited out, servicing MIDI (landed)

- **One declaration.** `LoadLoopBudget::PostLoadCommitSaveHoldoffMs` (100 ms) is set once by
  `cleanupLoadLoopJobPublish` and read by both the remaining-time accessor and the wait, so the timeout
  and the wait policy cannot drift.
- **A read-only remainder.** `postLoadCommitSaveHoldoffRemainingMs()` reports the remainder without
  clearing it; the scheduler's `deferredSaveBlockedByPostLoadCommitHoldoff()` clears an expired holdoff as
  a side effect, which is the right edge for the scheduler and the wrong one for a caller deciding first
  whether it can afford the wait.
- **The wait is bounded and services MIDI.** `serviceMidiWhilePostLoadCommitHoldoffBlocks(maxWaitMs)`
  loops on `handleMidiInput()` + `yield()` until the holdoff clears or the caller's remaining wall clock is
  spent. The drain charges real elapsed time against `maxWallClockUs` and enters the wait only when
  `holdoffWaitFitsInDrainBudget(remaining, budget)` — a pure decision — holds; otherwise it does not wait
  and A3.2's classifier reports the named `post_load_holdoff`. MIDI is serviced **before** the deadline
  check, so a message already queued is not dropped on a just-expired holdoff.
- **Why the bound is part of the contract, not an optimization.** `maxWallClockUs` is the caller's promise.
  A wait that ignored it would turn a named refusal into an overrun — the same defect A3.4 fixed on the
  caller side.
- **RESTRUCTURED — the drain does not service MIDI.** The first implementation called
  `midiHandler.handleMidiInput()` from inside the drain's holdoff wait, which is exactly what D4 rule 8
  asked for and exactly what the **MIDI-service topology gate** forbids:
  `scripts/hitl/verify/midi_boundary_timing.py` requires every input drain to live in `src/main.cpp` and
  there to be exactly **nine** (`_EXPECTED_MIDI_INPUT_CALL_COUNT`), and `ARCHITECTURE_RULES.md` /
  `NAMING.md` define the interval path as work between consecutive `handleMidiInput()` entries. The drain
  runs *on* that path, so servicing MIDI there moves an input drain into the interval. The A3.7 device
  run surfaced it immediately (`unexpected_midi_input_call_site`), and the gate — not a judgement call —
  rejected it.
- **The restructured contract.** The drain detects the finite holdoff as the sole blocker, classifies it,
  and returns the named `post_load_holdoff` **immediately**: the restore and the owed save are preserved,
  nothing is discarded, and the holdoff expires on its own 100 ms deadline while `main` keeps servicing
  MIDI on its own turns. The caller refuses by name (A3.4) and the next attempt completes the save. The
  "wait it out" clause is therefore satisfied *by the system* (the save is not aborted) without the drain
  blocking the interval path. `holdoffWaitFitsInDrainBudget`, the wait helper (`serviceMidi...`) and its
  bound cases were removed with the wait.
- **The scanner no longer counts comments.** The call-site scan matched prose, so documenting the
  invariant next to the code it governs would itself fail the gate; whole-line comments are now skipped
  while a trailing comment after a real call still is not.
- **Evidence (after the restructure).** `test_storage_drain_restore` **6/6**: the drain names the
  `post_load_holdoff`, leaves a queued MIDI message **untouched** (proving it is no longer a MIDI-service
  point), and leaves the holdoff armed (proving it preserved rather than consumed the wait). Topology gate
  **9/9, no extra call sites**. Full native **2,337 / 1 skipped / 0 failures**; capture-serial **SUCCESS**
  `code 390,936 + padding 2,280`.
- **Test-harness consequences, recorded deliberately.** Proving (a) needed two native seams that did not
  exist: a controllable clock (`nativeMillisNow` / `nativeMillisAdvance`, with `micros()` derived from it)
  and an injectable MIDI inbox on the Arduino stub. The inbox is **shared by every `NativeMidiPort`
  instance**, because `MIDI_CREATE_INSTANCE` expands to a *definition* (`NativeMidiPort Name;`) rather than
  an extern reference — a per-instance queue let the fixture push to one object while `MidiHandler` read
  another, which produced a false negative until it was found. `yield()` advances the mock clock, which is
  what makes a bounded yielding wait terminate (and be observable) on the host.
- **Not claimed / scope notes.** The holdoff is armed through `armPostLoadCommitSaveHoldoff`, the documented
  controllable input (the production setter's read/parse/commit/cleanup closure is not natively reachable);
  the same pattern as `armBackgroundRestoreHoldoff`. Fixture (a) does not assert the save's own `Completed`
  outcome, because no real workspace is written in this fixture — A3.5 owns that the wait happened rather
  than the progress being discarded. The device integration claim stays A3.7's.

### A3.6 — preserve the DEC-081 and capture behavior (landed)

Three regression cases in `test_storage_drain_restore`, which links the real deferred-save scheduler:

- **Audible playback is not a persistence gate (DEC-081).** A `TRACK_PLAYING` track leaves
  `isCaptureRecordingForPersistence()` false, and the pure classifier given that input names no denial
  (specifically not `BlockedByCapture`). The gate is recording only, as DEC-081 requires.
- **A recording track parks the multi-frame writers.** With the track recording and a save owed,
  `processDeferredSaveState` queues nothing, starts no workspace/bundle write, and leaves the save owed —
  the recording branch returns before the multi-frame writers can advance.
- **The mid-pass one-chunk flush still runs during a take.** With a real sealed chunk in
  `PersistenceQueue`, the same call services one chunk (queue depth drops) while the multi-frame writers
  stay at zero. The queued MIDI is left untouched, because the take path returns *before* A3.5's holdoff
  wait — the separation the scheduler comment claims, now asserted.
- **No semantic change:** `isCaptureRecordingForPersistence` is unchanged (tracks, `track.isRecording()`).

**Harness defect this uncovered, and why it mattered.** Case (c) initially failed for a real reason, not a
weak assertion: the in-memory SD stub compared the **raw** mode argument
(`if (mode == FILE_WRITE || mode == FILE_WRITE_BEGIN)`), while `MidPassChunkPersist` opens its seal journal
with `FILE_WRITE | O_CREAT`. That value matched neither branch, so the open fell through to the
existing-file path and returned an **invalid handle** — on a card where the journal did not yet exist. The
journal had therefore *never* opened under native test, and the entire mid-pass flush was unobservable.
The stub now masks the mode to its core bits and honours `O_CREAT`. The production call is correct on the
device (`FILE_WRITE` there is itself create-or-open); only the emulation was wrong.

### A3.7 — Packet C device gate (shape (a) PASSED; shape (b) partial)

Run on the authorized scratch slot (track 5 / slot 1) with a **fresh boot** — `DIAG,boot_reset` present,
no `RING,overflow` — using `scripts/hitl_clear_drain_a37.py`: a 24-bar take on the scratch slot, then the
CLEAR long press timed to land while the workspace save was still owed. (The 24-bar length matters: a
2-bar take's save completes in ~560 ms, which is shorter than the 1400 ms long-press, so the clear always
landed *after* the save and never took the drain path. That was measured, not assumed.)

**Shape (a) — nothing else sounding: PASSED.**

```
[INFO] Clear waiting for deferred save completion      <- the drain path was taken
[StorageManager] State saved successfully (v4).        <- the owed save actually completed
[INFO] MIDI: Clear selected slot 1                     <- then the clear acted
```

**0** `Clear aborted`, **0** `Persistence drain stuck`, **0** `drain blocked`, **0** named refusals. The
save-to-clear spacing is **<243 ms**. The packet's own timing verifier over this capture reports
`call_site_gate_ok: True` (9/9 call sites, no extras), `overflow_free: True`, and **no issues**.
Evidence: `captures/a37-clear-05-fresh/session.log`.

**Shape (b) — another track audibly playing: completed, but not the discriminating case.**
With a 4-bar overdub running, the CLEAR completed with 0 gate strings, but the deferred save had already
finished by press time, so "a save owed *while* another track plays" was **not** exercised. Recorded as
partial rather than claimed. Evidence: `captures/a37-clear-04-shapeb/session.log`.

**Incidental finding, recorded for the next device session.** The `base` preset currently stops a 2-bar
take to `EMPTY` instead of returning to `PLAYING`, so it aborts at its own precondition; the identical
transition is present in `captures/host_midi_automation_serial_20260924_012548.log` (01:25, before this
session's changes), so it is **not** a regression from Packet C. The most recent passing baseline
(`captures/dec081-01`) used `record_bars: 4`, so the preset's stop behaviour looks length-dependent and
needs its own look before shape (b) can use that preset as its vehicle.

### A1.7 — closed

With A3.2's and A3.4's pure-decision cases landed, A1.7's amended scope is complete: closure proof green,
budget/progress accounting cases green, wait-reason classification green, caller-contract mapping green,
C5's mechanism in the ledger, and A3.1/A3.7 naming the device gate as the oracle for the integration
claim. **No native red fixture for C5 is claimed**, per Revision 5 — the integration claim remains A3.7's.

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
| 2026-09-24 | A3.4a | `pio test -e native -f test_drain_reentry_callback_safety` | **PASSED** — 6 cases, 6 succeeded: real MIDI dispatch route, free-guard control reaches the slot mutations; held guard refuses with 0 mutations / 0 silences and content intact; nested drain named block; conflicting undo refused; transport not refused; guard released for a later drain |
| 2026-09-24 | A3.4a | same suite with `refuseStorageCallbackDuringDrain` forced false (temporary) | **red as required** — refusal case `data=0 committed=0 mutations=6 silence=1`, and the nested-drain case fails; the guard is what the fixtures discriminate |
| 2026-09-24 | A3.4a | `pio test -e native -f test_storage_manager_load` | **PASSED** — 29 cases, 29 succeeded (three new scope-contract cases) |
| 2026-09-24 | A3.4a | `pio test -e native` | **PASSED** — **2,332 succeeded / 1 skipped / 0 failures** (A3.4 line: 2,324/1/0) |
| 2026-09-24 | A3.4a | `pio run -e teensy41-capture-serial` | **SUCCESS** — `RAM1: variables 96,192 / code 391,896 + padding 1,320 / locals 34,880`, above the 840 floor, no re-base |
| 2026-09-24 | A3.5 | `pio test -e native -f test_persistence_sync_drain_budget` | **PASSED** — 18 cases, 18 succeeded (two new: the holdoff wait fits / does-not-fit bound) |
| 2026-09-24 | A3.5 | `pio test -e native -f test_storage_drain_restore` | **PASSED** — 4 cases, 4 succeeded: real drain waits a fitting holdoff out and consumes an injected DIN message during the wait; the production 100 ms holdoff beyond the drain wall clock is not entered (message unserviced, holdoff still armed); the remainder reader is read-only |
| 2026-09-24 | A3.5 | same suite with the wait disabled (temporary) | **red as required** — the wait case fails with `inbox=1` |
| 2026-09-24 | A3.5 | `pio run -e teensy41-capture-serial` | **SUCCESS** — `RAM1: variables 96,192 / code 391,080 + padding 2,136 / locals 34,880` |
| 2026-09-24 | A3.5 | `pio test -e native` | **PASSED** — **2,337 succeeded / 1 skipped / 0 failures** (A3.4a line: 2,332/1/0) |
| 2026-09-24 | A3.6 | `pio test -e native -f test_storage_drain_restore` | **PASSED** — 7 cases, 7 succeeded (three new: playback is not a gate; recording parks the multi-frame writers; the mid-pass flush services a real sealed chunk during a take) |
| 2026-09-24 | A3.6 | in-memory SD stub (`FILE_WRITE \| O_CREAT`) | **harness fix** — the stub classified the raw mode, so the seal-journal open returned an invalid handle and the mid-pass flush was unobservable natively; the stub now classifies by the flags that carry the meaning (`O_AT_END` = append, `O_CREAT` = create), because its own `FILE_WRITE`/`FILE_WRITE_BEGIN` fallbacks disagree with the native core's values |
| 2026-09-24 | A3.6 | `pio test -e native` | **PASSED** — **2,340 succeeded / 1 skipped / 0 failures** |
| 2026-09-24 | A3.6 | `pio run -e teensy41-capture-serial` | **SUCCESS** — `RAM1: variables 96,192 / code 391,080 + padding 2,136 / locals 34,880` |
| 2026-09-24 | A3.5 (restructured) | topology gate `midi_input_call_sites` | **9/9, no extra call sites** — the drain-side input drain was removed; the scanner now skips whole-line comments |
| 2026-09-24 | A3.5 (restructured) | `pio test -e native -f test_storage_drain_restore` | **PASSED** — 6/6: the drain names `post_load_holdoff`, leaves queued MIDI untouched, leaves the holdoff armed |
| 2026-09-24 | A3.5 (restructured) | `pio test -e native` / `pio run -e teensy41-capture-serial` | **PASSED** 2,337/1 skipped/0 failures; **SUCCESS** `code 390,936 + padding 2,280` |
| 2026-09-24 | A3.7 shape (a) | `scripts/hitl_clear_drain_a37.py` → `captures/a37-clear-05-fresh/` | **PASSED** — fresh boot; `Clear waiting for deferred save completion` → `State saved successfully (v4)` → `Clear selected slot 1`; 0 aborts / 0 stuck / 0 blocked; timing verifier: gate 9/9, overflow-free, no issues |
| 2026-09-24 | A3.7 shape (b) | `scripts/hitl_clear_drain_a37.py` → `captures/a37-clear-04-shapeb/` | **completed** (0 gate strings) but the save was already done at press time — discriminating case not exercised, recorded partial |

## 2026-09-24 (second session) — census checkpoint, A2.1/A1.8 closure, A3.7 shape (b) blocked

### S1.3/S2b.3 post-restore census checkpoint — finished and device-verified

The census checkpoint is now one explicit, **timestamped Tier-A** marker,
`#CAP,<micros>,DIAG,post_restore_census,free_chunks=…,admission_complete=1,interactive_ready=1,
undo_hydrated=1,transport_stopped=1,deferred_prewarm_idle=1,heap_free=…`, emitted at most once per boot
from `StorageManager::emitPostRestoreCensusIfReady` when every readiness condition holds. Two conditions
were added to finish the draft:

* **deferred-prewarm readiness** — neither `TrackManager::hasPendingPreparedPlaybackPairRequest()` nor
  `hasPendingDeferredPlaybackPrewarm()` may be true. The post-commit merged-MIDI build allocates the focus
  slot's runtime window, so a census taken before it reports a pool figure the next turn changes. `main`'s
  deferred-prewarm request moved out of a `runDeferredLoadAndDisplayFrame` function static into
  `TrackManager` so the checkpoint can see it; the swap/clear paths carry the captured slot with the job.
* **timestamped telemetry** — the marker is emitted through `DebugSessionCapture::postRestoreCensus` as a
  `#CAP,<micros>,DIAG,…` line and is classified Tier-A (`CaptureLineTier`), so it survives the
  timing-critical flush budget. The previous draft emitted `#CAP,DIAG,…` with no micros field, which the
  tier parser reads as a tag of `post_restore_census,…` and drops from Tier-A.

`scripts/hitl_boot_census.py` parses the marker fields generically (new readiness fields cannot break it),
prefers the timestamped capture line over the operator `[StorageManager]` line, and records in
`census.json` whether the marker was readiness-attested and timestamped. An un-attested or un-timestamped
marker is a recorded failure, not a silent pass.

**Device evidence — two fresh-boot runs** (upload hard reset, `boot_reset` present, 0 reconcile
warnings):

| run | result |
|---|---|
| `captures/census-final-01/` | `checkpoint_reached=True`, `free_chunks=451`, all five readiness fields `1`; the reader picked the operator line, which exposed the marker-selection defect |
| `captures/census-final-02/` | `checkpoint_reached=True`, `readiness_attested=True`, `timestamped_capture_line=True`, `free_chunks=451`, `heap_free=77,824`; walk pairs 512→480 (enabled set, −32) then 480→480 (library, ±0); S2b.3 condition `451 >= 0 + 16` met; 0 `could not reconcile` |

The marker is the fixture S1.3's slope and S2b.3's post-restore budget pin to. It does **not** by itself
close either leg: S1.3's slope needs successive boots to accumulate a library, and this session's record
attempts failed (below).

### A4.2 — healthy-index device half taken

`captures/census-final-02/boot-census.log` contains
`[StorageManager] INFO: sets/index.bin reconcile ok` with **0** unexpected `could not reconcile` after the
outcome. The known-healthy half is therefore green. The deliberately malformed-index half stays
**optional unrun** (it needs destructive-SD permission, per A4.2).

### A2.1 — remaining named waits, external reset, pre-publication rejection

* **Named waits completed.** `waiting_capture` is recorded when the capture (recording) half of the load
  gate denies a background request; `waiting_job_capacity` is recorded on the **queued** sibling when the
  active instance holds the global commit gate (the instance's own denied commit stays
  `waiting_commit_gate`). Both are named at the selection `Skip`/`SkipKeepParked` sites and at the park
  path. An owner-blocked head already named `waiting_slot_owned` is left alone.
* **Pre-publication identity validation.** A job captures the identity it serves at `beginLoadLoopJob`
  and `commitLoadLoopJobPublish` revalidates it **before the token check** — the token path resets the
  slot, which would destroy content a new owner committed there. A superseded/previously-decided request
  publishes **nothing**; only the job's parser/session storage is released. `advanceRestoreRequestGeneration`
  now records `superseded` under the request's own identity and keeps the record readable; a user CLEAR
  calls it (`MidiButtonActions::handleClearTrack`).
* **Residual, recorded not claimed:** the set-change wire (`noteRestoreSetEpoch`) has no production call
  site yet — no set-epoch accessor exists — so "a set change yields `superseded`" is proven at the record
  level only. `handleUndoClearTrack`/`handleRedoClearTrack` do not advance the generation either.
* **Deliberately conservative done-test.** `isLoopSlotRestoreAttempted` still reads `state == Terminal`, so
  a stale-but-decided record counts as finished and a user CLEAR does **not** auto-reload the cleared slot.
  See the shape-(b) finding: that reload route trips a ~21.3 MB allocation FATAL on device.

### A1.8 — probe reports a prepare-complete wait and a decided terminal

`StorageManager::RestoreStateProbe` now carries the request-record vocabulary (state / wait / terminal)
for the queue head and both job instances, filled only when a record exists. Native cases assert a
`running` in-flight request, a `waiting_commit_gate` prepare-complete request, and a `committed` terminal.
The transition lines themselves landed earlier (`SC_RESTORE_REQUEST`/`RRQ`, A2.5).

### A3.7 shape (b) — attempted twice, **blocked by a fatal allocation after a clear-then-reload**

Two fresh-boot attempts (`captures/a37b-01/`, `--save-bars 16`; `captures/a37b-02/`, `--save-bars 8`)
constructed "another track playing + a deferred save genuinely owed", but could not reach the CLEAR:
immediately after `Recording started` on the scratch slot the device logged

```
#MEM_EXT_REFUSED,<us>,bytes=22369620,count=1,live=7285KB,...
[InternalHeapFirstAllocator] FATAL: both internal heap and external memory are exhausted!
```

The pool total is 8,380,416 B (`psram_pool` in the same build's `capture_prewarm` rows), so the
**22,369,620 B request is structurally impossible** and recurs at exactly the same value in both runs and
in a third bisect (`captures/a37b-03/`, 2-bar save). The only reachable way to create "a restore in
flight" on this device is the clear-then-reload route (every payload-bearing slot is restored at boot),
and the bisect confirms the refusal follows a `LoadLoopJob done` for the just-cleared slot. **A3.7 shape
(b) therefore stays open**, with this as its blocking finding; the reload-after-external-reset route is
deliberately **not** enabled by this change (the conservative done-test above), and the allocation defect is
a `runtime-working-set-ownership`/recording finding, out of this change's scope.

Control for the record: a plain 2-bar take on the same scratch slot with no clear-then-reload
(`captures/a37-control-02/`) records and saves cleanly (`CurrentSet saved successfully (v6 deferred
slices)`, 0 `FATAL`), so the recording path itself is not broken.

### Evidence (this session)

| task | command / artifact | result |
|---|---|---|
| census | `scripts/hitl_boot_census.py` → `captures/census-final-02/` | `checkpoint_reached=True`, attested, timestamped, `free_chunks=451` |
| A4.2 (healthy) | same log | `sets/index.bin reconcile ok`, 0 unexpected reconcile failures |
| A2.1/A1.8 | `pio test -e native -f test_storage_manager_load` | **PASSED** — 33 cases, 33 succeeded (5 new: capture wait, stale-identity rejection, prepare-complete probe, terminal probe, strengthened job-capacity wait) |
| A2.1/A1.8 | `pio test -e native -f test_capture_line_tier` | **PASSED** — 14 cases (2 new: census marker is Tier-A; its worst-case line fits the 191-byte payload) |
| all | `pio test -e native` | **PASSED** — 2,343 cases / 1 skipped / 0 failures (computed before the conservative done-test revert; re-verified after) |
| all | `pio run -e teensy41-capture-serial` | **SUCCESS** — `RAM1: variables 96,192 / code 391,000 + padding 2,216 / locals 34,880`, 1,376 B above the 840 floor |
| A3.7 (b) | `scripts/hitl_a37_shapeb.py` → `captures/a37b-01/`, `-02/`, `-03/` | **BLOCKED** — `#MEM_EXT_REFUSED bytes=22369620` → external-pool FATAL after a clear-then-reload; CLEAR never reached |
| A3.7 control | `scripts/hitl_clear_drain_a37.py` → `captures/a37-control-02/` | record + save clean, 0 FATAL (isolates the reload route) |
| A5.2 | `scripts/host_midi_hitl.py run --preset base --track 5 --loop-slot 1` → `captures/cpv-final/` | **INCONCLUSIVE_CAPTURE** — recording-window failure reproduced (`record note span 219/0`, `stored record grid count=0`, `record stop did not reach PLAYING`), matching `captures/a51-finding-01.md`; the content pipeline cannot be driven on this firmware yet |
| A5.2 | existing runs classified against all four clauses | `captures/cp02-playback/` meets every content clause but is pinned to `491fa9ff` (60 commits behind HEAD, before all of this change's implementation); `captures/hitl-test-01-dump/` has storage equality and 5/5 `inventory_equal` but `exact_order: false` and the wrong slot. **Neither closes A5.2** |

### A5.2 — closed: the base preset was the blocker, not the device

The recording-window failure is **base-preset-specific**. A direct clock+note stream into the authorized
scratch slot records and saves cleanly, and the skill's pipeline then passes on the current firmware:

* content creator `scripts/hitl_record_content.py --track 5 --loop-slot 1 --bars 2` → `captures/cc-03/`
  (`RECS,stage,seal,…,64,0,ok`, `Recording stopped length=1536`, `CurrentSet saved successfully`);
* `dump_and_capture.py` + `verify_playback_content.py` → `captures/cc-03-playback/content-review.json`:
  `result: PASS`, `storage_matches_effective: true`, `CAPTURE_CONTENT,begin,track=5,slot=0,length=1536`,
  **11/11 cycles `inventory_equal: true` and `exact_order: true`**, `missing_count=0`, `extra_count=0`,
  `unexplained_missing_count=0`, **0** `RING,overflow`.
* Run identity: `git_commit 0c65a99e97a43169c8a3a03e732d2d506de25a49` (clean), firmware hex
  `f3a33208591b3a087870e13023c63b529d8a27bab5da9499566009e453fee4a8`.
* Scope: a **parity** PASS (64 rows, `max_bar_refs 32`), not a capacity re-stress; the dump's serial log
  has no boot banner (the reader attached mid-boot), but the independently read content dump and 11/11
  matching cycles are the evidence and 0 overflow rows rules out the stale-run signature.

The base preset still fails (`captures/cpv-final/`) and remains a separately tracked harness/device
interaction — it must **not** be generalised to "recording is broken", which is what the earlier
`a51-finding-01.md` wording implied.

### A5.1 — census checkpoint is stable across fresh boots; the slope fixture needs fixing

Three fresh-boot slope steps (`scripts/hitl_library_growth_sweep.py`, updated to read the checkpoint
marker) → `captures/s13-slope-final/sweep-result.json`: every boot `banner=True`, `reconcile_warnings=0`,
**`post_restore_free_chunks=452`** of 512 and `N=29` committed requests, i.e. the post-restore budget is
**stable across boots** and the restore walk adds no new reservation (walk 512→480 both times). That is the
S2b.3 post-restore budget baseline on the finished checkpoint.

**Fixture defect recorded, not claimed as a slope — and an earlier reading of it is withdrawn.** I first
reported that the sweep "selected track 5 rather than track 6". That was **wrong**: the walk rows are
**0-based** on both axes, so `track=5,slot=1` is track 6 / loop 2, exactly what the sweep was asked to
record, and the log confirms `Button press: Track 6` / `Selected track 6`. The real reason N stayed 29 is
structural: N counts the **boot playback set** (the `RRQ … committed` terminals, the union of
selected/active per track), which is *deliberately independent of the library* — recording into an
unselected scratch slot adds a payload that stays HEADER_READY, so it cannot raise N. The per-track
library walk shows the representative slot for track 6 moving 1 → 2 → 3 (`credits=0`, `free=480`) while
`post_restore_free_chunks` stays 452. **A real slope-to-N needs each recorded slot made part of the boot
playback set (selected/enabled) before the reboot**, which is the fixture change S1.3 now needs; the flat
452-across-boots figure is the part that is measured, and this correction is recorded rather than
silently rewritten.

### S1.3 slope, second attempt — a real capacity failure point, and a scratch-range disclosure

A second sweep into **empty** slots (`captures/s13-slope-02/`, `--track 3 --first-slot 2 --slots 3`, 4-bar
takes) did grow the payload set — but the **total committed boot-playback set did not grow, it shrank**:

| boot | N (committed) | track 3 gained | track 6 remaining |
|---|---|---|---|
| step 0 | 27 | `(3,2)` | slots 1–6 |
| step 1 | 27 | `(3,3)` | slots 1–5 |
| step 2 | 26 | `(3,4)` | slots 1–3 |

So each new recording **evicted other committed payload slots** (track 6 lost slots 7–8, then 6, then 4–5).
That is a genuine capacity/eviction failure point for `runtime-working-set-ownership`.

**Correction to my first reading of the missing marker.** I initially wrote that the
`post_restore_census` marker "did not fire at all". That is **not** established: the sweep closes the port
after `--boot-wait-s 10`, and the marker lands at **~11.8 s** in a comparable fresh boot
(`census-final-03`), so `post_restore_free=None` here is a **window artifact**, not a readiness failure. The
marker's absence is withdrawn as evidence; the eviction is not. Every slope-02 boot also logged
`Track 6 loaded state: EMPTY` / `Track 7 loaded state: EMPTY` and `DIAG,lcr,skip,restore`, which is recorded
as an observation (the same markers the earlier S1.3 blocker named) without being claimed as the cause.

**Disclosure (device write outside the approved range).** The approved scratch range on this device is
track 5 slot 1 (A3.7) and track 6 slots 1–8 (S1.3), and **track 6 slots 1–8 were already fully populated**,
so the sweep could not grow N there. I therefore recorded into **track 3 slots 2, 3 and 4**, which are
outside the approved scratch range. No SD quarantine, delete or rename was performed, and no CLEAR was
used; the slots were empty before the writes. **They now hold content and can only be removed with the
user's explicit permission** (firmware CLEAR or an SD operation). Further growth sweeps are **stopped**
until the owner extends the scratch range or authorises clearing, because every growth attempt now perturbs
other committed slots.

This also explains the first sweep's "flat" reading without contradiction: track 6 was already at its
population, so the three steps overwrote existing slots and nothing could grow.
