# Architecture review — restored-library-stability (Packets A and B)

This is the change's review/evidence artifact. It carries:

- **Task 1.1** — the corrected measurement definitions and attribution, with citations (documentation only).
- **Task 1.0** — the G2 execution packets for the two firmware-editing slices of Packet A: **1.2**
  (observed fields, two outputs, four counts, pure classifier) and **1.3** (the bounded on-demand
  observation with its owner, admission and invalidation contract).
- **Packet B** (appended) — **task 2.1** (not exercised: nothing needed clearing) and **task 2.2** — the
  complete-manifest stability gate over three consecutive boots, with the read-only comparator that
  performs it.

Authority: [IMPLEMENTATION_READINESS.md](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice);
template: [EXECUTION_PACKET.md](../../../docs/Templates/EXECUTION_PACKET.md). Cite the approved contract
(`proposal.md`, `design.md` D2–D6, the `lazy-slot-hydration` spec delta, `tasks.md`) — it is not duplicated
here. Packets are **specified** until their Completion record says otherwise.

Packet A is **observation-only firmware plus read-only analysis**: no library-behavior change, no
destructive SD action.

---

## Task 1.1 — corrected measurement definitions and attribution (recorded)

Each fact below is the corrected statement; the citation is the evidence path. The same facts are recorded
in this change's `proposal.md` § *Known…* and `design.md` § *Context*, and the corresponding live claims in
`runtime-working-set-ownership` S1.3 / S2b.3 / the S2b gate are corrected in
`openspec/changes/runtime-working-set-ownership/tasks.md`.

**(a) `n_committed_slots` is a window-scoped observed-completion count, not the boot playback set and not
the library size.** `scripts/hitl_library_growth_sweep.py` computes it as the distinct `(track, slot)` of
`RRQ,…,terminal,none,committed` lines seen **within the capture window** (the sweep's own
`committed_slots` set). Background admission visits every slot, so the count includes background restores;
a window that closes before admission completes changes it on its own. The earlier explanation for `N=29`
("the boot playback set, deliberately library-independent") is **withdrawn**, and subtracting a playback
set from the payload set does **not** establish loss. Evidence: `scripts/hitl_library_growth_sweep.py`
(lines computing `committed_slots`), `captures/s13-slope-02/sweep-result.json`,
`captures/s13-slope-final/sweep-result.json`.

**(b) `MidiLooper/sets/index.bin` is a saved-set folder counter, not a slot catalog.**
`SavedSetCatalog::SetIndex` is `{ uint32_t nextSequence; }`; the live library is
`/MidiLooper/current/loops/loop_TT_SS.bin` plus the `loopSlotPayloadOnSdInRam_` mirror. A reconcile failure
therefore cannot by itself explain a missing slot. Evidence: `include/SavedSetCatalog.h` (the struct),
`src/StorageManager/LoopSlotRestoreQueue.cpp` (the mirror and `probeLoopSlotPayloadOnSdFromSd`),
`src/StorageManager/CurrentSetBootLoad.cpp` (the boot scan that refreshes the mirror).

**(c) The boots that appeared to shrink report `reconcile_warnings=0`.** Evidence:
`captures/s13-slope-02/sweep-result.json` (each step's `reconcile_warnings`), where N goes 27 → 27 → 26.

**(d) `DIAG,lcr,skip,restore` is a content-resolution device-gate line, not a library marker.**
`TrackDeferredMaintenance` / `LoopContentResolution` emit it with `kind=skip`, `reason=restore`. Evidence:
`src/Track/TrackDeferredMaintenance.cpp` and `src/StorageManager/LoopContentResolution.cpp` call sites,
plus the raw lines in `captures/s13-slope-02/step02_slot4/boot.log`.

**(e) `Track 6 loaded state: EMPTY` also appears on the stable boots.** Evidence:
`captures/s13-slope-final/step*/boot.log` (`Track 6 loaded state: EMPTY` with N=29 and
`reconcile_warnings=0`), against the shrink run `captures/s13-slope-02/`.

**(f) `free_before/after` 512 → 480 is the enabled-set walk, not the library.** The walk rows are the
`DIAG,capture_prewarm,track=…,slot=…,full=…,ready=…,credits=…` lines, `credits=0` on every row because
boot leaves committed-but-inactive slots cold (S2b.1). Evidence:
`captures/s13-slope-02/step02_slot4/boot.log` (two walk pairs: the enabled set, then the library walk);
`docs/Runtime/PROJECT_STATE.md` § S2b.3.

**(g) Both sweeps' `library_rows` are all-zero — no per-slot inventory was ever captured.** 16 rows with
`phases: 0` in each `sweep-result.json`; the rows that exist are `capture_prewarm` reservation telemetry,
which `scripts/hitl_boot_census.py` documents as *not* a restoration signal. This is the gap Packet A
closes.

**Verify (task 1.1):** no durable doc still asserts the catalog reconcile, or `n_committed_slots`, as a
**demonstrated cause of loss**. Grep result after the edits: `docs/Runtime/PROJECT_STATE.md` and
`docs/Runtime/CURRENT_WORK.md` carry the historical paragraphs with an explicit
`[Attribution CORRECTED 2026-09-24 …]` marker; `runtime-working-set-ownership` S1.3 / S2b.3 / S2b gates
carry the correction inline; `openspec/specs/lazy-slot-hydration/spec.md`'s requirement that the reconcile
*outcome* be reported is an observability requirement, not a loss claim, and is left unchanged.

---

## Packet for task 1.2 — observed fields, two outputs, four counts, pure classifier

### Identity and scope

- **Task/contract IDs:** `restored-library-stability` **1.2**; spec delta `lazy-slot-hydration` §
  *Observed current state is reported separately from a nullable historical cause*; `design.md` D3, D4.
- **Status:** specified.
- **Owner and production path:** the classifier is a **pure function over a snapshot**; it owns no runtime
  state and has no production entry point of its own. In Packet A it is consumed by task 1.3's collection
  (firmware) and by the read-only capture analysis (task 1.4, a Python mirror over `RRQ` lines). It is
  deliberately **not** wired into a runtime decision.
- **Primary files:** new `include/StorageManager/LibraryObservation.h` (types + classifier declaration),
  new `src/StorageManager/LibraryObservation.cpp` (implementation); new native suite
  `test/test_library_observation/`.
- **Preconditions already present:** the restore-request record
  (`include/StorageManagerInternal.h` `restoreRequest*`), the payload mirror
  (`readLoopSlotPayloadOnSdInRamEntry`), the committed-content inventory pattern
  (`LoopEventStore::countEventsInChunkIds`, `Loop::passes`), the boot-playback-set predicate
  (`include/Utils/BootLoopSlotRestore.h` `isBootPlaybackSlot`).
- **Prerequisites included in this slice:** none beyond the new TU.
- **External blockers:** none.
- **Mutation:** none on production state. The slice adds a pure classifier and its native fixtures.
- **Postconditions:** `classifySlot()` yields a `current state` **set** and a **nullable** cause; the
  mandatory overlapping-input case yields `empty` + `unknown` in one result; `summarize()` yields the four
  D4 counts and the window framing as separate outputs.
- **Explicit non-goals:** no runtime decision consumes the classifier in this slice; no new persisted
  catalog; no change to `emitPostRestoreCensusIfReady`; no SD access in the classifier itself.
- **Commit boundary:** the new header/TU plus its native suite; independently buildable and green on the
  current tree without task 1.3.

### Oracle fixed before behavior edits

The classifier is new, so the oracle is its contract, frozen by the red-first suite:

| Case ID | Initial input (per slot) | Expected observation |
|---|---|---|
| L01 overlapping input | no payload, no content, no request, no prior inventory, no event | state = {`empty`}; cause = `unknown`; `empty` never a cause |
| L02 prior inventory, no event | prior inventory recorded populated, now no payload/content/request | state = {`empty`}; cause = `unknown` with the prior inventory as evidence; **not** `content loss` |
| L03 explicit eviction | eviction/deletion event recorded | cause = `intentional eviction` |
| L04 corrupt payload + refusal | payload present, token invalid; request terminal `refused` | both facts recorded; cause = `ambiguous` (both consistent) |
| L05 pending request | request queued / running / waiting at observation | state ∋ `pending`; **no terminal asserted**; cause not applicable |
| L06 two consistent causes | payload-validation failure **and** not-offered-with-prior-inventory | cause = `ambiguous`, both recorded |
| L07 populated | verified payload and/or committed content, no outstanding request | state = {`populated`}; cause nullable (`not applicable`) |
| L08 counts/window | window `{startUs, endUs, admissionComplete, everyInitialPlaybackSlotObserved, consistentView, turns}` | the four counts are separate fields; an incomplete window marks counts not-settled |

**Counterexample that must fail:** a classifier that returns a scalar state, or that records `empty` (or
`content loss`) as the cause for L01/L02, or that asserts a terminal for L05, fails the suite.

**Precedence, stated (design D3).** (1) An explicit eviction/deletion event is authoritative →
`intentional eviction`. (2) Observed content present → the cause is nullable (`not applicable`). (3) An
outstanding request → the cause is nullable and no terminal is asserted. (3b) A **completed** restore
whose content field could not be observed (denied SD read) → likewise nullable, never a loss. (4)
Otherwise the consistent causes are gathered — an invalid payload token/body (`content loss`), a `refused`
terminal (`restore refusal`), a payload present but not offered by the boot enumeration (`catalog
omission`) — and ≥2 yield `ambiguous` with all of them recorded, exactly 1 yields that cause. (5) None
yields `unknown`, with the prior inventory as evidence when one exists. `empty` requires the absence to be
**observed**: `payload == absent`, content observed as absent, and no outstanding request. If payload or
content is `unknown`, the state set stays **empty** (reported `unknown`), never `empty`.

### Controls and coverage

- **Seams reused:** none needed — the classifier takes a plain snapshot struct, so no clock/allocator/I/O
  seam is involved.
- **Production paths exercised:** none (pure function). Task 1.3's fixture is what exercises a production
  service point.
- **Independent oracle:** expected states/causes are written explicitly per case (a table, not derived from
  the implementation).
- **Untestable-before-link cases:** none.

### Verification recipe

- Focused: `pio test -e native -f test_library_observation` → all cases pass.
- Broad: full `pio test -e native`; `pio run -e teensy41-capture-serial` SUCCESS with the RAM1 budget line
  re-read (the pure TU is FLASH-resident; expected cost ≈ 0 RAM1 bytes).
- Device: none (no device-observable behavior in this slice).
- Evidence path: this file, § Task 1.2 Completion record; task 1.2 checkbox in `tasks.md`.

### Completion record (task 1.2)

- **Oracle/contract unchanged.** The L01–L08 table above is the contract that shipped; no assertion was
  weakened, and both the native and the Python mirror were adjusted only where the *contract itself*
  needed stating precisely (see the two refinements below).
- **Fixture implemented:** `test/test_library_observation/test_library_observation.cpp` — 13 cases:
  L01–L08 plus `unobserved fields are unknown not empty`, `denied SD admission keeps the waiting request
  named`, `invalidation clears fields and is excluded from counts`, and
  `completed restore with unobserved content stays nullable`.
- **Discriminating assertion demonstrated (mutation, reverted):** replacing the classifier's final
  `unknown` fallback with `Cause::ContentLoss` makes exactly the intended cases fail —
  `test_l01_…` (`Expected 6 Was 4`), `test_l02_…` (`Expected 6 Was 4`) and
  `test_unobserved_fields_are_unknown_not_empty` (`Expected 6 Was 4`) → **3 failed / 9 succeeded**.
  The file was restored byte-identical (`shasum -a 256` = `a0f0a257bd669e4d883bd01ac62bdfd0b1c2c286c97d280c0dc51a0aa93aeb18`)
  and the suite returned to 13/13.
- **Native verification:** `pio test -e native -f test_library_observation` → **13 cases, 13 succeeded,
  0 failed** (`native:test_library_observation [PASSED]`).
- **Python mirror verification:** `scripts/hitl/library_observation.py` mirrors the classifier and carries
  the same cases in `scripts/test_library_observation_classifier.py` → **13 tests, OK**
  (`.venv/bin/python -m unittest scripts.test_library_observation_classifier`).
- **Two contract refinements made while implementing (recorded, not silently absorbed).**
  (a) `catalog omission` requires a payload to be present but not offered; it is not inferred from a prior
  inventory alone (the pre-implementation wording was ambiguous and would have made L02 read
  `catalog omission`). (b) `empty` requires observed absence, and a completed restore whose content field
  was not observable keeps a nullable cause — both needed so the offline capture mirror, which cannot
  observe payload/content, does not report `empty` or a loss for a slot it simply could not see.
- **Build/RAM1 verification:** recorded in task 1.3's completion record (the same firmware build; the
  classifier functions are `TRACK_COLD_MEM` = `FLASHMEM`, so they cost no ITCM code).
- **Device verification:** NOT APPLICABLE — no device-observable behavior in this slice.
- **Task eligible for checkoff:** yes (1.2). Stage gates still open: 1.5 (device attribution).

---

## Packet for task 1.3 — bounded on-demand observation (owner, admission, invalidation)

### Identity and scope

- **Task/contract IDs:** `restored-library-stability` **1.3**; spec delta `lazy-slot-hydration` § *The
  on-demand library observation has a bound owner, admission and invalidation contract*; `design.md` D2
  (owner/service point, incremental collection, SD-read admission, invalidation, content version, permitted
  degradation, consistency, framing), D4.
- **Status:** specified.
- **Owner and production entry → deferred work → commit:** owner is **`StorageManager`** (the existing
  persistence/restore owner; no new owner). Trigger: an explicit on-demand request from the existing HITL
  serial command surface (`StorageManager::requestLibraryObservation()`, reached by `!LIB_OBSERVE` in
  `StorageManagerHitlSerial.cpp`). **Service point:** the **unconditional observation tail in `main`**
  (`src/main.cpp`, the idle-owner position that already calls `processHitlSerialCommands` /
  `emitPostRestoreCensusIfReady`), via `StorageManager::serviceLibraryObservation()` — **not** the
  admission-gated deferred load/restore frame. No new thread, timer, interrupt, or periodic hot-path cost:
  the tail is a single `requested`-flag check when no observation is outstanding, and the collection is
  active only between the request and the emitted report.
- **Primary files:** new `src/StorageManager/LibraryObservationSession.cpp` (the bounded state machine +
  collection), declarations in `include/StorageManager.h`, one call added to `src/main.cpp`, one HITL
  command added to `src/StorageManager/StorageManagerHitlSerial.cpp`, one emitter added to
  `include/Utils/DebugSessionCapture.h` / `src/Utils/DebugSessionCapture.cpp`.
- **Preconditions already present:** payload mirror and `refreshLoopSlotPayloadOnSdInRam`,
  `restoreRequest*` records, `bootBackgroundFillAdmissionComplete()`, `trackManager` slot indices,
  `isBootPlaybackSlot`, `LoopEventStore::countEventsInChunkIds`, and the SESSION_CAPTURE-tier emitter
  pattern.
- **Prerequisites included in this slice:** the pure classifier types from 1.2.
- **External blockers:** none. Device evidence for the *read-only attribution* is task 1.5, not a
  prerequisite for the slice to be native-verified.
- **Mutation:** adds observation state in `DMAMEM` (external to RAM1) and a bounded read-only collection.
  It **never** writes SD, and it does not touch the load/restore admission, the commit gate, or the settled
  census.
- **Postconditions:** an outstanding request yields a report that names the waiting request even though the
  settled census does not fire; the collection advances at the declared service point while load admission
  is blocked; a library larger than one invocation is collected incrementally and marked incomplete; a
  generation/epoch/content-revision change between slices invalidates or restarts and never mixes records;
  denied SD admission degrades fields to `unknown` without suppressing the report.
- **Must preserve:** `emitPostRestoreCensusIfReady`'s gates, purpose and output; the load/restore commit
  gate; the SD-session exclusion (no concurrent SD use with capture, a load job, or a persistence write);
  `padding >= 840`.
- **Explicit non-goals:** no library-behavior fix; no new SD format; no replacement for the settled census;
  no second queue or persisted catalog; no periodic collection when nothing is requested.
- **Commit boundary:** the new session TU, the accessors, the `main` tail call, the HITL trigger and the
  emitter; independently buildable and green with 1.2 already landed.

### Oracle fixed before behavior edits

Per-invocation bound (declared, and asserted by the fixtures): **at most
`kSlotsPerInvocation = 2` slots observed — and at most 2 records emitted — with at most
`kSdProbesPerInvocation = 2` SD token probes** per invocation. Total traversal is bounded by
`NUM_TRACKS × MAX_LOOPS_PER_TRACK = 64` slots, so one observation completes in at most 32 collection
invocations plus 32 emission invocations. A set-epoch change restarts the whole observation at most
`kMaxRestarts = 2` times before the records are invalidated and the report is emitted, so a churning
epoch cannot spin the service point.

| Case ID | Initial state | Ordered actions (production service hook) | Expected observation after each step |
|---|---|---|---|
| O01 permanently waiting request | one request in a named wait; settled census therefore never fires | `requestLibraryObservation()`; service until the report is emitted | the settled census does not fire; the report is produced and names the waiting request, its named wait, and the slot's payload/content state |
| O02 service point vs load admission | load admission blocked (the commit gate denies the load child) | service the observation at `serviceLibraryObservation()` | the observation **advances** (cursor/counts move) and is not suppressed; zero collection performed by the load/restore frame |
| O03 bounded, incremental collection | library larger than one invocation | one service invocation; then service to completion | one invocation observes ≤2 slots with ≤2 SD probes; the partial result is marked incomplete with its turn count; the completed result covers every slot |
| O04 request-generation change between slices | slot already observed in slice k | `advanceRestoreRequestGeneration(t,s)`; service again | that slot's record is invalidated/restarted; no record mixes pre/post fields; if it cannot be re-observed, fields read `unknown` |
| O05 set-epoch change between slices | observation in progress | `noteRestoreSetEpoch(newEpoch)`; service again | the whole observation is invalidated/restarted under the new epoch; no slot record mixes epochs |
| O06 publication between slices | slot already observed; generation **and** epoch deliberately unchanged | publish committed content for that slot (production publish or an ordinary commit); service again | the slot's **content revision identity** differs; the slot record is invalidated/restarted; no record mixes pre-publication fields with post-publication content |
| O07 denied SD admission | a load job / recording holds the SD session at a collection slice | service the observation | SD-requiring fields (`payload` token validity, content) read `unknown`; the report is still produced and still names a waiting request |
| O08 not the settled measurement | observation emitted | compare emitter output | the observation line is a distinct tag and is never emitted by, or as, `post_restore_census` |

**Transition oracle detail.** `windowStartUs` is recorded at request time; the set of slots whose request
record already reads terminal `committed` at that instant is captured as a bitmask
(`committedAtWindowStart`), so `observedRestoreCompletions` counts **only** slots that reached `committed`
during the observation's own window — the correction D4 requires. `setEpochAtStart` and each observed
slot's `(requestGeneration, contentRevisionIdentity)` are recorded at its slice and re-read in a final
validation pass before emission; content fields are re-read in that pass, never carried across slices.

**Outcomes at the boundary.** No universal enum is added: the existing
`RestoreRequestState` / `RestoreRequestWait` / `RestoreRequestTerminal` names are reported as-is; a slot
whose fields could not be observed reads `unknown`; the report is always produced, and `incomplete` is an
explicit framing flag, never a silent pass.

**Counterexamples that must fail:** a collector serviced only from the deferred load/restore frame (O02
fails); an unbounded single-turn traversal (O03 fails); a record that keeps pre-change fields (O04/O05/O06
fail); a collector that opens SD while a load job holds the session (O07 fails); a report that reuses the
settled census tag (O08 fails).

### Controls and coverage

- **Seams reused:** the existing native SD stub (`test/test_support/firmware/SD.h`), the
  load/liveness harness (`test/test_storage_manager_load/load_and_restore_harness.h`), the request-record
  accessors, and the `sequencerRunning` / track-state controls the harness already drives. No new seam is
  required; the only new test-facing surface is the request/service pair plus read-only accessors
  (`libraryObservationActive()`, `libraryObservationSnapshotForTest()`).
- **Production paths exercised:** the real request record, the real SD stub, the real payload mirror and
  the real `main` tail function (`StorageManager::serviceLibraryObservation()`), not a copied state
  machine.
- **On-path domain stubs:** only the existing native SD/Arduino boundaries; nothing that returns success
  for the behavior under test.
- **Interleavings covered:** waiting request, blocked load admission, mutation between slices
  (generation, epoch, content publication, ordinary commit), denied SD admission, library larger than one
  invocation.
- **Independent oracle:** expected counts and per-slot states are written explicitly in the fixtures; the
  content revision identity is checked by re-reading production content, not by echoing the collector.
- **Numeric limits:** `2` slots, `2` SD probes and `2` emitted records per invocation are slice
  constants, asserted directly; `padding >= 840` from `scripts/ram1_budget.json`.
- **Bounded failure output:** the fixture fails with the cursor, turn count and the last observed slot
  state; no sleep-based timing.

### Verification recipe

- Focused: `pio test -e native -f test_library_observation` (classifier + session cases).
- Broad: full `pio test -e native`; `pio run -e teensy41-capture-serial` SUCCESS with the post-link
  RAM1 line recorded (`scripts/check_ram1_budget.py`).
- Build seam note: the session TU reads production `TrackManager`/`Loop`/request-record state; the native
  suite links it against the in-memory SD stub, exactly as `test_storage_manager_load` links
  `StorageManager.cpp`.
- Device-only claim: **none** in this slice. Packet A's read-only device attribution is task 1.5 and needs
  no firmware behavior claim.
- If measurement: inputs are the per-slot observed fields, units are counts of slots and microseconds of
  the window; the consumption decision is task 1.4/1.5's attribution and, only if the instability is
  evidenced, a later separately-scoped production slice.
- Evidence path: this file, § Task 1.3 Completion record; task 1.3 checkbox in `tasks.md`.

### Completion record (task 1.3)

- **Oracle/contract unchanged.** The O01–O08 table above is the contract that shipped. The declared
  per-invocation bound was tightened from the specified 4 slots to **2 slots / 2 SD probes / 2 emitted
  records** while implementing, because with a shared per-invocation probe budget a slot observed after
  the probes were spent could never have its token verified within that observation; 2 slots with at most
  2 probes gives every slot a probe opportunity when SD admission is granted. Total work is unchanged
  (64 slots, ≤32 collection invocations).
- **Production entry points added:** `StorageManager::requestLibraryObservation()` /
  `StorageManager::serviceLibraryObservation()` (declared in `include/StorageManager.h`), serviced from the
  unconditional observation tail in `src/main.cpp` immediately after `emitPostRestoreCensusIfReady`, and
  requested by `!LIB_OBSERVE` in `src/StorageManager/StorageManagerHitlSerial.cpp`. Owner, service point,
  admission, invalidation and framing are implemented in
  `src/StorageManager/LibraryObservationSession.cpp`; the records are emitted as the distinct `LIBOBS`
  family (`DebugSessionCapture::libraryObservationBegin/Slot/End`).
- **Fixture implemented and driving the actual service hook:**
  `test/test_library_observation_session/test_library_observation_session.cpp` — 8 cases (O01–O08) linking
  the production request records, payload mirror, load admission and `CurrentSetStorage` against the
  in-memory SD card, with the observation's three capture emitters substituted by a recorder. The suite is
  registered in `scripts/native_test_link.py` as a firmware-boundary suite.
- **Discriminating assertions demonstrated (two mutations, both reverted byte-identical):**
  1. removing the **content-revision** comparison in `recordStillCurrent` fails exactly
     `test_o06_publication_between_slices_invalidates_without_a_generation_change`
     (`the content revision changed between slices, so the record is invalidated`) → 1 failed / 7 succeeded;
  2. gating `serviceImpl` on `StorageManager::hasLoadLoopJobCommitPending()` fails
     `test_o02_observation_advances_while_load_admission_is_blocked` (0 of 2 slots observed),
     `test_o01_…` and `test_o06_…` (the observation never reports) → 3 failed / 5 succeeded.
  After both reverts, `shasum -a 256 src/StorageManager/LibraryObservationSession.cpp` =
  `685918eca1b0518ff5a0301a696d1f70ad06d78397fac8eb94b9d9c985c1f66e` (pre-mutation hash) and the suite
  returned to 8/8.
- **Native verification:** `pio test -e native -f test_library_observation -f test_library_observation_session`
  → **21 cases, 21 succeeded, 0 failed**; the session suite alone is 8/8.
- **Broad native verification:** full `pio test -e native` → **2,373 cases / 1 skipped / 2,372 succeeded /
  0 failures** (baseline before this change: 2,352 / 1 skipped). Re-run after the post-device `LIBOBS`
  Tier-A/length fix: **2,374 / 1 skipped / 0 failures** (the +1 case is
  `test_library_observation_lines_fit_capture_text_budget`).
- **Build/RAM1 verification:** `pio run -e teensy41-capture-serial` → **SUCCESS**;
  `[RAM1 budget] variables 96,192 / ITCM 393,216 (code 391,160 + padding 2,056) / locals 34,880` — the
  approved 12-bank ITCM line is unchanged, `padding` is **1,216 B above the 840 floor**, and `locals`
  is unchanged at 34,880 (no bank cross). The pre-change line was `code 391,080 + padding 2,136`, so the
  whole observation costs **+80 B of ITCM** (the FLASHMEM call sites/veneers): every new function is
  `STORAGE_PERSIST_MEM`/`TRACK_COLD_MEM` (flash) and the state is `STORAGE_PERSIST_DATA` (DMAMEM,
  external to RAM1). The first build *without* the flash placement crossed into a 13th bank
  (`ITCM 425,984`, locals 2,112) — the documented silent boot hang — and the placement was applied
  rather than the budget re-based.
- **Device verification:** NOT APPLICABLE in this slice; the read-only device attribution is task 1.5.
- **Task eligible for checkoff:** yes (1.3). Stage gates still open: 1.5.

---

## Task 1.4 — read-only capture re-analysis (recorded)

**What was run (read-only; no device, no upload, no SD write):**

```
.venv/bin/python scripts/hitl_library_observation_report.py \
    --capture-dir captures/s13-slope-02 --capture-dir captures/s13-slope-final \
    --out captures/s13-library-observation.json
```

The classifier is the shared mirror in `scripts/hitl/library_observation.py`
(`parse_capture_lines` → `classify_slot` → `summarize`), which `scripts/hitl_library_growth_sweep.py` now
also uses from `boot_census()`, so every recorded step carries `library_observation` and
`n_committed_slots_window_scoped`. The offline suite is
`scripts/test_library_observation_classifier.py` → **13 tests, OK**.

**Result per boot window** (`boot.log`; the record windows are excluded by name):

| Capture | Step | Window `[start,end]` (µs) | Lines | Requests | `N` (window-scoped) | payload inventory | initial playback | committed at checkpoint |
|---|---|---|---|---|---|---|---|---|
| `s13-slope-02` | step00_slot2 | 2,875,004 → 12,899,799 | 420 | 30 | **27** | unknown | unknown | unknown |
| `s13-slope-02` | step01_slot3 | 2,895,004 → 12,926,994 | 423 | 31 | **27** | unknown | unknown | unknown |
| `s13-slope-02` | step02_slot4 | 2,825,005 → 12,846,738 | 422 | 32 | **26** | unknown | unknown | unknown |
| `s13-slope-final` | step00_slot1 | 3,005,004 → 14,633,410 | 530 | 29 | **29** | unknown | unknown | unknown |
| `s13-slope-final` | step01_slot2 | 3,005,004 → 15,011,675 | 537 | 29 | **29** | unknown | unknown | unknown |
| `s13-slope-final` | step02_slot3 | 3,005,004 → 15,354,291 | 442 | 29 | **29** | unknown | unknown | unknown |

**Findings.**

- **No slot in either sweep records a loss cause.** Every slot with a request record classifies
  `not_applicable` (a completed or outstanding request — nothing missing to attribute) and every other
  slot classifies `unknown` with its missing fields named. `content loss`, `restore refusal`,
  `catalog omission` and `intentional eviction` are **not** evidenced anywhere in the recorded state.
- **`N` is window-scoped, and the two sweeps' windows differ.** `s13-slope-02`'s windows close at
  ~12.85–12.93 s with 420–423 captured lines; `s13-slope-final`'s run to ~14.6–15.4 s with 442–537 lines.
  The run that appeared to shrink is the run whose window closed **~1.8–2.5 s earlier** — consistent with
  the incomplete-window explanation D4 predicted, and it is now stated per boot instead of inferred.
- **The other three quantities are not recoverable from these captures**, and the report says so:
  `payload_inventory`, `initial_playback_required` and `committed_at_checkpoint` are `None` with
  `payload_token`, `initial_playback_selection` and `committed_content` named as the missing fields. The
  captures recorded no payload token, no committed-content dump and no active/selected index; only the
  `RRQ` request family and the `capture_prewarm` reservation walk exist. This is exactly the observability
  gap the on-demand device observation (task 1.3) closes.
- **No single-snapshot historical claim is made.** Every `unknown` cause carries the prior-inventory /
  missing-field evidence rule; nothing is promoted to `content loss` or `intentional eviction`.

**Verify (task 1.4):**

| Requirement | Result |
|---|---|
| each slot gets an observed state | ✓ 64 slots per boot window: `not_applicable` records carry `populated`/`pending`; the rest carry the explicit empty state set (unknown) |
| each attribution carries its evidence or is `unknown` with the missing field named | ✓ every non-`not_applicable` slot names its missing fields; the only positive cause in the data would be a `refused` terminal, which is covered by a dedicated case |
| the re-analysis performs **no** device write | ✓ reads `captures/**/boot.log` only; no serial, no upload, no SD operation |
| the reported `N` is explicitly window-scoped | ✓ `n_committed_slots_window_scoped` plus `n_committed_slots_definition`, and the window `[start,end]` per boot |

**Task eligible for checkoff:** yes (1.4).

---

## Task 1.5 — read-only device attribution (device verified)

**What was run.** Read-only, on `/dev/cu.usbmodem154944801`, through the established boot-census entry
point extended with the observation:

```
pio run -e teensy41-capture-serial -t upload            # the only reliable reset; owner-authorized
.venv/bin/python scripts/hitl_boot_census.py --out-dir captures/s13-libobs-full-02 \
    --library-observe --settle-s 25 --observe-window-s 30
.venv/bin/python scripts/hitl_boot_census.py --out-dir captures/s13-libobs-incomplete-02 \
    --library-observe --settle-s 25 --observe-window-s 30 --observe-early-close-s 0.15
```

The only thing sent to the device is `!LIB_OBSERVE`, plus the upload that performs the reset. No CLEAR, no
rename, no quarantine, no delete, and no transport start. The observation respects the SD-session admission
and writes nothing.

**Defect found and fixed by the first device run (recorded, not papered over).** The first format emitted
full enum **names**, which overran the capture text cap (`DebugSessionCapture::kMaxCaptureTextBytes = 192`)
and was **silently truncated**: `captures/s13-libobs-full-01/`'s records lost everything after `passes`, so
the frame's trailing fields (`admission_complete`, `every_initial_observed`, `consistent_view`, `complete`)
read as zero and a completed observation reported `complete=False`. The wire form was changed to compact
enum **codes** plus one/two-character keys
(`LibraryObservation::causeCode`/`payloadCode`/`offeredCode`/`requestCode`/`terminalCode`), the host parser
was updated with the reverse mapping, a native/Python test locks it
(`DeviceObservationTests` in `scripts/test_library_observation_classifier.py`), and the run was repeated.

**Result — the per-slot inventory that had never been captured** (`captures/s13-libobs-full-02/`):

| Quantity (D4) | Value |
|---|---|
| **payload inventory** (verified payload) | **36** |
| **initial playback-required set** | **8** |
| **observed restore completions in this window** | **0** |
| **currently committed at the checkpoint** | **8** |

Window: `[13,017,777, 13,312,720]` µs, `turns=64`, `restarts=0`, `slots_observed=64`,
`admission_complete=1`, `every_initial_observed=1`, `consistent_view=0` (the observation spans turns and
says so), `complete=1`.

Per slot: **36 `populated` / 28 `empty`**, and **no loss cause anywhere** —
`not_applicable × 36`, `unknown × 28`, with **zero** `content loss`, `restore refusal`, `catalog omission`
or `intentional eviction`. The 28 `empty` slots are payload-`absent`, request-`none`, content-`none`, with
cause `unknown` (no history — exactly the case the spec forbids promoting to a loss). The initial-playback
set is one slot per track — `(0,0) (1,2) (2,1) (3,0) (4,0) (5,0) (6,0) (7,0)` — and **all eight are
committed**. Payloads are present on tracks 0–5 (36 slots: `(0,0)`, `(1,0..2)`, `(2,0..7)`, `(3,0..7)`,
`(4,0..7)`, `(5,0..7)`) and **absent on tracks 6 and 7**, which is the recorded
`Track 6/7 loaded state: EMPTY` observation seen on stable boots — now measured per slot instead of read
off a track-level line. `restore_completions = 0` because every required slot was already committed when
the window opened (the `committedAtWindowStart` mask doing its job).
**[Corrected 2026-09-24, task 2.2 run: "all eight are committed" is not supported by the per-slot
records — 4 of the 8 required slots hold in-RAM committed content; see § Packet B → *Disposition*.]**

**Incomplete-window case** (`captures/s13-libobs-incomplete-02/`): the capture window closed 0.15 s after
the request, before the observation's records had drained from the ring. The report says
`INCOMPLETE window: these counts are NOT the settled library state`, carries **zero** slot records and
**all four counts as `None`** — never as zeros, and never as the library state.

**Verify (task 1.5):**

| Requirement | Result |
|---|---|
| the four quantities are reported per boot | ✓ 36 / 8 / 0 / 8, each a separate field |
| the window completeness is stated | ✓ `admission_complete=1`, `every_initial_observed=1`, `consistent_view=0`, `complete=1`, with the window's `[start,end]` and `turns` |
| an incomplete window is reported as incomplete and its counts are **not** the settled state | ✓ `complete=False`, counts `None`, and the console/report say so explicitly |
| 0 `FATAL` | ✓ `grep -c FATAL` = 0 in every observation capture |
| no SD mutation | ✓ no CLEAR/rename/quarantine/delete was issued; the only `QUARANTINE` text in the logs is the boot banner's instruction line |

**What this means for the change's own question.** The instability was an **observation**, and this packet's
read-only attribution finds **no loss to attribute**: the library is 36 payload slots with 8 committed
initial-playback slots, every required slot is committed, and the "shrinking" count is a **window-scoped
restore-completion count** (task 1.4: 27/27/26 with a ~1.8–2.5 s shorter window vs 29/29/29). Whether a
*repeated* boot reproduces the same manifest is Packet B's complete-manifest comparison, not this packet's
claim.

**Task eligible for checkoff:** yes (1.5).

---

## Flagged, out of this change's scope — stale `teensy41` RAM1 baseline (+64 B, pre-existing)

While checking that Packet A did not regress the non-capture env, `pio run -e teensy41` **fails** the
post-link RAM1 budget check:

```
[RAM1 budget] teensy41: variables 79,232 / ITCM 393,216 (code 382,216 + padding 11,000) / locals 51,840
  - RAM1 variables changed: 79,232 B vs approved 79,168 B (+64 B).
```

**This is not Packet A's growth, and it was verified rather than assumed:** with the whole change stashed
(`git stash push -u`), the baseline tree builds to the **identical** line
(`variables 79,232 / code 382,216 + padding 11,000 / locals 51,840`). Packet A adds **0 B** of DTCM/RAM1
variables and **0 B** of ITCM to that env (every new function is flash-resident; the observation state is
`DMAMEM`). The `teensy41` entry in `scripts/ram1_budget.json` was last re-based 2026-09-23 and is **stale**
relative to `runtime-working-set-ownership`'s S2b.7/S2b.8, which landed 2026-09-24 and only re-recorded
`teensy41-capture-serial` (`code 391,080 + padding 2,136`).

Recorded here so it is not lost, and **deliberately not re-based**: the baseline is edited by a human with
the reason recorded in the *owning* change's review artifact, and this change neither caused nor consumes
that 64 B. The required env (`teensy41-capture-serial`) is green and within budget.

---

## Post-device follow-up — `LIBOBS` is Tier-A, and the truncation is locked by test

The device run's defect (records over the 192-byte capture text cap, silently truncated) is fixed on two
fronts, not only by shortening the format:

1. **`CaptureLineTier::isTierALine` now protects the `LIBOBS` family.** The observation is an explicitly
   requested one-shot inventory (66 records, ≤2 per invocation) that is read back precisely in the stuck
   states a dense run produces, so a Tier-A flood must not evict it.
2. **`test_capture_line_tier::test_library_observation_lines_fit_capture_text_budget`** regenerates the
   three record shapes at worst case (10-digit micros, every numeric field at its type maximum), asserts
   each is ≤ 191 bytes, and asserts the Tier-A classification (including that a lookalike prefix
   `LIBOBSX,` is not Tier-A). `test_capture_line_tier` is **15/15**.

**Applicability of the earlier evidence.** The format/budget change is additive and does not alter the
oracle, the bounds or the invalidation contract, so task 1.3's evidence still matches: the two native
suites are re-run green (`test_library_observation` 13/13, `test_library_observation_session` 8/8, together
21/21) and the full native suite is re-run at **2,374 / 1 skipped / 0 failures**. The device inventory in
§ Task 1.5 was captured with the compact format and the Tier-A classification is a protection, not a
content change, so those figures stand.

**Durable docs updated with the behavior (closeout).** `docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md`
§ *The on-demand library observation* (contract table, the four quantities, the window-scope warning, the
reading recipe, and "this is not the settled census"), with a `Read this when` entry and an
ownership-table row; a `docs/Guides/BOOT_LOAD.md` key-files row; and
`.agents/skills/hitl-content-verification` § 9 (plus its discovery description) for the read-only
attribution path. `DEC-083` records the decision and its rejected alternatives.

---

## Packet B — task 2.1 (not exercised) and task 2.2 complete-manifest stability (device verified)

Both Packet B tasks below are **read-only**: the only thing sent to the device is `!LIB_OBSERVE`, and the
upload that performs the reliable reset. This section adds no firmware edit and issues no destructive
command, so no G2 execution packet is required for it (task 1.0: only firmware-editing slices need one).

### Task 2.1 — TBD-1 authorization gate: **NOT REQUIRED for the reused fixture**

Stability was proven on the **existing** library, so no slot was emptied and no per-slot destructive
authorization was requested or needed. Task 2.1 is therefore **complete as not applicable** for this
fixture: the gate it defines is opened only by a fixture that needs a slot cleared, and "don't clear
anything merely to repeat evidence already available" is the rule this run followed. This is an
authorization disposition, not a claim that clearing is never needed — a later fixture that must empty a
slot re-opens the gate. No `CLEAR`, rename, quarantine or delete command appears
in any of the three new logs (`grep` for the destructive verbs over `boot-census.log` → 0 lines; the only
`QUARANTINE` text is the boot banner's instruction line).

### Task 2.2 — reuse first: what the existing Packet A captures actually settle

`scripts/hitl_library_manifest_stability.py` was run over **all five** existing `LIBOBS` captures before any
new device action (`captures/s13-libobs-stability-existing.json`):

| capture | eligible? | why |
|---|---|---|
| `s13-libobs-full-02/` | **yes** | fresh boot (`boot_reset` + banner), `complete=1`, 64 slots |
| `s13-libobs-incomplete-03/` | **yes** | fresh boot, `complete=1`, 64 slots |
| `s13-libobs-full-01/` | no | the pre-fix long-form wire format was truncated (`complete=0`); the frame and its trailing fields are not the settled record |
| `s13-libobs-incomplete-01/` | no | **not a boot**: no `boot_reset` and no banner — it is a mid-session attach at ~72 s uptime on the **same boot** as `full-02` (its `checkpoint_reached=False` because that boot's once-per-boot census had already fired) |
| `s13-libobs-incomplete-02/` | no | the window was closed 0.15 s after the request (`complete=0`, 0 slot records, counts `None`) |

The existing evidence therefore settles **two** eligible boots, not three, and the third complete-looking
observation (`incomplete-01`) is not a second boot at all. Neither the requirement nor the intent ("compare
in full on each of three consecutive boots") is met by re-labelling a same-boot observation as a boot, so
the missing consecutive boots were captured. This is the **only** reason for the device action; the tooling
itself is read-only and re-used `full-02` as the recorded expected manifest.

### The gate — three consecutive fresh boots compared in full

**Recipe** (`.agents/skills/hitl-content-verification` § 9):

```
for i in 01 02 03; do
  .venv/bin/python scripts/hitl_boot_census.py --out-dir captures/s13-libobs-stab-$i \
      --library-observe --settle-s 25 --observe-window-s 30
done
.venv/bin/python scripts/hitl_library_manifest_stability.py \
    --expected-dir captures/s13-libobs-full-02 \
    --boot captures/s13-libobs-stab-01 --boot captures/s13-libobs-stab-02 \
    --boot captures/s13-libobs-stab-03 --out captures/s13-libobs-stability-3boot.json
```

`hitl_boot_census.py` resets by re-uploading before each capture, so the three captures are three
**consecutive** boots of one run. Run identity:
`captures/s13-libobs-stability-3boot.identity.json` — `git_commit 74cadc7b`, env
`teensy41-capture-serial`, `firmware_hex_sha256 ede704b614b7e9cd…`, port
`/dev/cu.usbmodem154944801`.

The **expected per-slot manifest** is recorded from the existing Packet A boot `full-02` (reuse), and
carries, per slot: payload presence/token, offered, the committed content identity and counts
(`has_committed_content`, pass count, highest pass id, loop length, committed event count), the initial
playback-required flag, and the observed request record. The comparison is performed in full over all 64
slots, and the settled counts are compared too.

| boot | window `[start,end]` µs | turns | restarts | admission / every-initial / complete | payload / initial / committed | manifest sha256 |
|---|---|---|---|---|---|---|
| `s13-libobs-full-02` (expected) | `[13,017,777, 13,312,720]` | 64 | 0 | 1 / 1 / 1 | 36 / 8 / 8 | `4e451a07d0a94c18…` |
| `s13-libobs-stab-01` | `[14,135,590, 14,431,905]` | 64 | 0 | 1 / 1 / 1 | 36 / 8 / 8 | `4e451a07d0a94c18…` |
| `s13-libobs-stab-02` | `[13,179,791, 13,476,012]` | 64 | 0 | 1 / 1 / 1 | 36 / 8 / 8 | `4e451a07d0a94c18…` |
| `s13-libobs-stab-03` | `[13,066,713, 13,362,639]` | 64 | 0 | 1 / 1 / 1 | 36 / 8 / 8 | `4e451a07d0a94c18…` |

**Result: `verdict: pass`** (`captures/s13-libobs-stability-3boot.json`) — every one of the three observed
boots matches the recorded expected manifest **in full**: 0 manifest differences, 0 lifecycle differences,
0 settled-count differences. The only window-scoped field D4 excludes
(`observed_restore_completions`) is `0` in all of them, and every per-slot request record
(`request`/`wait`/`terminal`/`generation`/`epoch`) reproduced as well, which is stronger than the manifest
core requires. **There are no differences in the observed fields.** That is the whole claim: the observed
manifest reproduces. It is **not** a claim that the payload files are byte-identical, and **not** a proof
that no byte was written anywhere — an identical field record (or even an identical file hash) would
establish unchanged observed content, not the absence of a write. The absence of a *destructive* action is
established separately, by the command log: only `!LIB_OBSERVE` was sent, and no `CLEAR`, rename,
quarantine or delete command was issued.

The five complete-manifest observations (the two existing plus the three new) share the **same** observed
manifest sha256 `4e451a07d0a94c18fc177967521344c31ba4e9c8774a42d19aa1114f46e963f8`, so the "same boot"
reading of `incomplete-01` is also internally consistent (identical *observed* manifest within one boot)
without being counted as a boot.

### Disposition — the 36 payload slots vs the 8 committed slots (and the two different 8-sets)

The three counts `36 / 8 / 8` are **three different populations**, and the gap between them is not a loss
and not a write. Per-slot evidence: `captures/s13-libobs-full-02/census.json` (identical per-slot records in
the three `stab-0N` captures).

| D4 quantity | value | what it observes (firmware owner) |
|---|---|---|
| **payload inventory** | **36** | a payload file is present in the RAM mirror **and** the SD file exists with a valid token (`LibraryObservationSession.cpp::observeSlot` → `verifySaveFileTokenAtPath`) |
| **initial playback-required set** | **8** | `isBootPlaybackSlot(...)` — the per-track union of the selected and active slot (`requiredForInitialPlayback`); one slot per track today, not "the loaded set" |
| **currently committed at the checkpoint** | **8** | an **in-RAM** `Loop` has committed passes — `loop.passes.hasRecordPass() \|\| !loop.passes.overdubPasses.empty()`; counted in `LibraryObservation.cpp` |
| **observed restore completions** | **0** | window-scoped (`committedTerminalInWindow`); every loaded slot was already committed when the window opened |

**The two 8-sets are not the same set**, and neither is a subset of the other:

- committed at the checkpoint (8): `(0,0) (1,0) (1,1) (1,2) (2,0) (3,0) (5,0) (5,1)`
- initial playback-required (8): `(0,0) (1,2) (2,1) (3,0) (4,0) (5,0) (6,0) (7,0)`
- overlap: `(0,0) (1,2) (3,0) (5,0)` — **4 slots**.

Consequences that must be read correctly:

- **28 = 36 − 8**: payload-present slots with **no in-RAM committed content** — `(2,1) … (2,7)`,
  `(3,1) … (3,7)`, `(4,0) … (4,7)`, `(5,2) … (5,7)`; two of them, `(2,1)` and `(4,0)`, are in the
  initial-playback-required set. They are on SD and not loaded; they are **not** loss.
- **28 = 64 − 36** is a **different** 28: the payload-absent (`empty`) slots, cause `unknown` (no history),
  correctly not promoted to a loss. The two 28s are a numeric coincidence and must not be conflated.
- `(6,0)` and `(7,0)` are in the initial-playback-required set and are **payload-absent** — the recorded
  `Track 6/7 loaded state: EMPTY`, now measured per slot.
- **Correction to Packet A task 1.5.** Its sentence "the initial-playback set … and **all eight are
  committed**" is not supported by the compact-format records and is corrected here: **4** of the 8 required
  slots hold in-RAM committed content at the checkpoint (`(0,0) (1,2) (3,0) (5,0)`); `(2,1)` and `(4,0)`
  are payload-present but unloaded, and `(6,0)`/`(7,0)` are payload-absent. The `committed_at_checkpoint`
  count is also 8, but over the partly different set listed above. `[Corrected 2026-09-24, task 2.2 run.]`

**Disposition for the runtime slope fixture (`runtime-working-set-ownership` S1.3).** The slope measures
**residency against the committed/loaded working set**, so the population it varies is the
**in-RAM committed** one (8 today), not the 36-slot payload inventory. The fixture must therefore:

1. **name the population it varies** — committed/loaded slots (and therefore select/enable each scratch slot
   so boot actually loads it), not merely create an SD payload; and
2. **explain the other 28** — payload-present but unloaded slots are outside the runtime working set, so a
   change in the payload inventory (36) must never be read as a change in the loaded set (8), and neither
   may be read against the payload-absent 28.

This is the disposition task 4.1 transfers; it is not a capacity or eviction finding.

**Verify (task 2.2):**

| Requirement | Result |
|---|---|
| any unexplained difference between observed and expected is a failure, including in a slot that is not part of the fixture | ✓ the comparator fails on any required-field, lifecycle-field or settled-count difference, and `authorized_changes` is empty (no fixture change was made), so every slot must match; locked by `test_content_difference_in_a_non_fixture_slot_fails`, `test_a_difference_in_a_non_fixture_slot_fails_the_three_boot_gate` |
| the pass requires every slot to match, not merely the fixture slot to persist | ✓ 64/64 slots matched in each of the three boots; 0 differences |
| three consecutive boots | ✓ `stab-01/02/03`, each a fresh boot (`boot_reset` + banner, `complete=1`), run back-to-back with the documented upload reset before each; only `!LIB_OBSERVE` was sent |
| no slot outside the approved range is written (asserted from the offline manifest, not from intent) | ✓ **no differences in the observed fields** across all 64 slots, and **no destructive command issued** (0 `CLEAR`/rename/quarantine/delete lines; only `!LIB_OBSERVE`; 0 `FATAL`, 0 `#MEM_EXT_REFUSED`). Recorded precisely: identical observed records — or even identical file hashes — would establish unchanged *observed* content, **not** unchanged bytes and **not** the absence of a write; the no-destructive-action statement rests on the command log, and the manifest comparison rests on the observed fields |

**Tooling added** (read-only; no firmware): `scripts/hitl/library_manifest.py` (the pure per-slot manifest
comparator, required vs lifecycle vs window-scoped fields, fingerprints),
`scripts/hitl_library_manifest_stability.py` (the CLI gate), and
`scripts/test_library_manifest_stability.py` (**14/14**).

### Not claimed here, and transferred out of this change

- **Task 2.3** (the fixture can hold the S2b.7 condition — a save owed + a restore holding the SD session +
  another track playing) is **not** done here and is **TRANSFERRED 2026-09-24 to
  `runtime-working-set-ownership` S2b.7 — drain-driven restore advancement**, recorded there **unchecked**.
- **Packet C** (the S2b.7 drain device confirmation) is **TRANSFERRED** the same way to that change's S2b.7,
  independently reportable there (its 3.2 clause travels with it). Neither gates this stability result.
- This gate proves the **current** library's *observed* manifest is reproducible across boots
  (36 payload slots, 8 loaded/committed, 8 initial-playback-required — three different populations, see
  *Disposition*). It is not a claim about unchanged payload bytes, not a claim that a *constructed* fixture
  with an emptied slot is stable, and not a capacity or eviction finding.
- **Task 4.2** (the stale drain clause in the main `lazy-slot-hydration` spec) is **TRANSFERRED** to its
  runtime owner and recorded in that change's Closeout; this change does not rewrite another change's
  requirement.
- **Next runtime work (recorded, not started):** `runtime-working-set-ownership` **S1.3 — slope-fixture
  definition** must explain **why 28 verified payloads remain unloaded** and **why only four
  initial-playback slots overlap the committed set**. This stability gate shows the observation is
  **repeatable**; it does **not** establish correct hydration or a usable increasing-population fixture.
