# Prioritized boot load isolation — refinement

**Kind:** refinement  
**Date:** 2026-07-15 (editorial clarity pass 2026-07-17)  
**Status:** Phase 0 signed off 2026-07-17; ready for Phase 1A  
**Supersedes / extends:** [`deferred_boot_load_speed_refinement.md`](deferred_boot_load_speed_refinement.md)

**Context:** Stashed lazy-load improved time-to-UI (~6 s vs ~16 s in [`session_20260715_135614.log`](../captures/session_20260715_135614.log)) but **mid_pass during background load** broke normal operation. Root cause: SD load still uses the capture persistence contract while runtime schedulers run concurrently.

**Do not re-enable lazy USB / background load until Phase 1 passes device verification.**

### Wrong lever (pairing / stop validate)

Slow large-loop load and first piano-roll paint are **not** fixed by putting full `validateAndCleanupMidiEvents` back on record/overdub stop. That path was removed in **`15a35b4`** (wrap-window `LoopStopFinalize` + idle-deferred full validate) and recovered ~120→~320 KiB internal free heap. Full storage validate is **not** on the SD load path today.

Load latency is SD read + visualCache / `NoteUtils::reconstructNotes`. Per-slot materialize-on-restore was already dropped in **`68ce6ad`** (heap exhaustion on 42-slot boot). Normative note: [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) § Heap tradeoff. This refinement’s window-first / prioritized slot load is the correct track.

---

## Architectural invariant

> **Invariant**
>
> A chunk loaded from SD is already authoritative. It must **never** re-enter the persistence pipeline unless it is later modified by recording or editing.

Everything in **Phase 1** exists to enforce this invariant.

---

## Loading policy

- Publish only after successful validation.
- Failed loads never modify the destination slot.
- SD-loaded chunks never enter the persistence queue.
- Staging allocations are discarded on failure.
- Reprioritization never cancels an active slot load.

---

## Design principles (resolved 2026-07-17)

1. **Only active work owns resources.** Pending queue entries are intent only. Only an active `SlotLoadSession` owns SD access, staging allocations, scheduler coordination, and runtime restrictions.
2. **Publication is the only visibility boundary.** Nothing outside the loader may observe partially loaded data.
3. **Lifecycle stays stable across phases.** Phase 1 and Phase 3 share the same `SlotLoadSession` lifecycle; only session storage duration changes (stack vs StorageManager-owned).
4. **Avoid overloaded state queries.** Persistence, display, USB readiness, and background-work checks use **independent** queries — not one shared boolean.

Independent queries (do not collapse):

| Query | Answers |
|-------|---------|
| `SlotLoadSession::isActive()` / session state | Is a slot load owning resources right now? |
| `hasPendingSlotLoads()` | Is background loading work still outstanding (queue **or** active session)? |
| `bootInteractiveReady()` | May USB / interactive UI proceed? (Phase 1: queue empty; Phase 3: tier-0 Published) |
| `needsSlotLoad(track, slot)` | Should this address be admitted to the queue? |

---

## Terminology glossary

| Term | Meaning |
|------|---------|
| **Staging** | Temporary private representation owned exclusively by `SlotLoadSession`. Invisible to runtime systems; may be discarded without affecting the destination slot. |
| **Published** | Live runtime representation visible to the rest of the firmware (event payload adopted). Manifest / metadata-only hydrate is **not** Published. |
| **Adopt** | Transfer ownership from staging into the published representation. |
| **Publish** | Atomically expose the adopted representation to runtime systems. |
| **Session** | One complete logical slot loading operation (`SlotLoadSession`). |
| **Queue** | Pending load intent only (`SlotLoadQueue` / `SlotLoadRequest`) — no resources. |

---

## Loading pipeline

High-level mental model for the rest of this document. Every later section explains one part of this flow.

```text
SlotLoadQueue
        │
        ▼
SlotLoadSession
        │
        ▼
Read into staging
        │
        ▼
Validate staging
        │
        ▼
Publish atomically
        │
        ▼
Completed
```

Abstraction: **Load loop → Publish into slot**. The source may change (current set SD, another set, MIDI import, backup, recovery); the destination is always a slot.

---

## SlotLoadSession lifecycle

```text
                +---------+
                | Queued  |
                +---------+
                      |
                      v
               +-------------+
               |  Dequeued   |
               +-------------+
                      |
                      v
               +-------------+
               |   Reading   |
               +-------------+
                      |
                      v
               +---------------+
               |  Validating   |
               +---------------+
                 /           \
                /             \
               v               v
      +---------------+   +-----------+
      |  Publishing   |   |  Failed   |
      +---------------+   +-----------+
               |
               v
        +---------------+
        |   Completed   |
        +---------------+
```

### State ownership

> Only **`SlotLoadSession`** owns and transitions its lifecycle state (after Dequeued). External systems **observe** `SlotLoadSessionState` but never modify it. While Queued, the request is owned by `SlotLoadQueue` and has no session yet.

### Core definitions

> **Staging** is temporary memory owned exclusively by `SlotLoadSession`. Objects in staging are invisible to runtime systems and may be discarded at any time without affecting the destination slot. Only the **Publishing** state may transfer staged data into the live runtime representation.

> **Publishing** is the atomic transition that converts a previously private staging snapshot into the live runtime representation.

> **Completed** means: the slot has been successfully Published; the session no longer owns temporary resources; scheduler restrictions are removed; runtime systems may treat the load as finished. (Telemetry, cleanup order, and deferred-work resume are implementation details of reaching Completed.)

### State responsibilities

#### Queued

**Owner:** `SlotLoadQueue`

- Determine load priority; allow reprioritization
- No resources allocated
- Other systems operate normally

#### Dequeued

**Owner:** transfers from queue → **`SlotLoadSession`**

- Create session; enter loading mode; increment activity
- Queue no longer owns this request

#### Reading

**Owner:** `SlotLoadSession`

- Read SD; parse data; allocate **staging** only
- No adopt / live publish into the destination slot
- May span multiple `loop()` iterations (Phase 3 cooperative slices)

#### Validating

**Owner:** `SlotLoadSession`

- Validate **staging** payload, metadata, structural consistency
- Destination slot remains unchanged
- Failure exits directly to **Failed**

#### Publishing

**Owner:** `SlotLoadSession`

Architectural meaning: atomic private → live transition (see definition above).

Implementation consequences of that transition:

- Adopt staged chunks into the live representation
- Mark chunks persisted (`markChunkPersistedFromSdLoad`)
- Swap published references; finalize metadata
- Expose visibility — **only** externally visible transition

#### Completed

**Owner:** session teardown

Architectural meaning: load finished successfully (see definition above).

Implementation consequences: leave loading mode; drop activity; emit `#CAP,LOAD,outcome,published`; allow selected-slot display rebuild.

#### Failed

**Owner:** session teardown

- Discard staging; restore normal runtime; destination slot unchanged; report diagnostics
- Emit `#CAP,LOAD,outcome,failed`
- System state as if load never started; may re-enqueue via `needsSlotLoad` when appropriate

### Design goal

`SlotLoadSession` represents one complete logical slot loading operation regardless of source. Only the reader changes; the lifecycle stays identical. Future features (time-sliced load, display budgeting, pause/resume, progress) attach to session state without new ownership concepts.

**Same lifecycle across phases; different storage duration:**

| Phase | Session storage | Behavior |
|-------|-----------------|----------|
| **Phase 1** | Stack RAII inside `loadLoopSlotFromCurrentSetSd` | One call runs Reading → Completed/Failed |
| **Phase 3** | `StorageManager`-owned optional active session | Survives ~35 ms cooperative slices across `loop()` iterations |

Do not pretend Phase 1 has a persistent session member — promote storage only in Phase 3.

**Example (Phase 1 sync path):**

```cpp
bool loadLoopSlotFromCurrentSetSd(Track& track, uint8_t slot) {
  SlotLoadSession session(storageManager, track.index(), slot);
  session.setState(SlotLoadSessionState::Reading);
  // read SD → parse into staging only (no live publish)
  readCapturePassSlotFileHeader(...);
  session.setState(SlotLoadSessionState::Validating);
  if (!validateStaging()) { session.fail(); return false; }
  session.setState(SlotLoadSessionState::Publishing);
  adoptPersistedSnapshot();
  markLoopPublishedChunksPersistedFromSdLoad(...);
  publishMetadata();  // only externally visible transition
  session.complete();
  return true;
}
```

**Phase 2 note:** Replace multiple SD reads with **batched reads inside the existing Reading state**. Session ownership is unchanged.

---

## Ownership model

```text
SlotLoadQueue          ← pending work only (depth = not-yet-started)
        │
        ▼ dequeue
SlotLoadSession        ← one active load; owns staging + session state
        │
        ▼
StorageManager         ← performs read / validate / publish orchestration
        │
        ▼
LoopEventStore         ← published chunk data + persistence marks
        │
        ▼
Published Slot         ← externally visible only after Publishing completes
```

| Component | Responsibility |
|-----------|----------------|
| **SlotLoadQueue** | Decides **what loads next**; holds **pending** `SlotLoadRequest` entries only |
| **SlotLoadSession** | Owns staging and lifecycle for exactly one slot load |
| **StorageManager** | Executes the load (source-specific readers call into shared publish path) |
| **LoopEventStore** | Owns published chunk data; SD-load mode routes staging seals → mark persisted |
| **Recovery** | Independent policy — may publish partial when structurally consistent |

### Naming (slot-oriented)

| Responsibility | Name |
|----------------|------|
| Queue | **`SlotLoadQueue`** |
| Queue entry | **`SlotLoadRequest`** |
| Active operation | **`SlotLoadSession`** |
| Scheduling helper | **`needsSlotLoad(track, slot)`** |
| Priority helper | **`computeSlotLoadPriority(...)`** in **`SlotLoadPriority.h`** |

| Current (rename in follow-up PR) | Target |
|----------------------------------|--------|
| `PendingLoopSlotRestoreQueue` | **`SlotLoadQueue`** |
| `pendingLoopSlotRestores_` | `slotLoadQueue_` |
| `DeferredLoopSlotRestore` | **`SlotLoadRequest`** |
| `processDeferredLoopSlotRestore` | `processNextSlotLoad` (optional rename) |
| `hasPendingLoopSlotRestore()` | **`hasPendingSlotLoads()`** — background work only; **not** USB / mid_pass / expensive display |
| `BootLoopSlotRestore.h` | **`SlotLoadPriority.h`** |

Source-specific executors:

- `loadLoopSlotFromCurrentSetSd(...)` — Phase 1–3
- `loadLoopSlotFromMidiFile(...)` — future
- `loadLoopSlotFromBackup(...)` — future
- `recoverLoopSlot(...)` — recovery policy (DEC-020 Phase 5)

Every implementation creates a **`SlotLoadSession`** internally; loading policy stays identical; only the source differs.

### Queue vs active load

The queue owns **pending work only**. Once a request begins executing it is **dequeued** and tracked by **`SlotLoadSession`**.

- **Queue depth** = pending `SlotLoadRequest` count only
- **Active load** = at most one `SlotLoadSession`
- Retries: re-enqueue a new `SlotLoadRequest` after **Failed**

### `needsSlotLoad(track, slot)`

Single admission gate for boot, deferred loading, reprioritization, and future import paths.

Returns **true** when the slot:

1. Is **not** currently **Published** (payload not in live representation — metadata-only still needs load)
2. Is **not** already in **`SlotLoadQueue`**
3. Is **not** currently being loaded (**no active `SlotLoadSession`** for that address)

Empty / missing SD files are not queued.

---

## Scheduler contract

> The scheduler never manipulates loading state.
>
> The loading system owns `SlotLoadSession`.
>
> The scheduler only observes `SlotLoadSessionState` and adjusts runtime behavior accordingly.

Scheduling **reacts** to loading; it does not control session transitions.

| Session state | Expensive display rebuilds | Playback | mid_pass / conflicting SD |
|---------------|----------------------------|----------|---------------------------|
| **Queued** | **Allowed** | Normal | Normal (queue owns no resources) |
| **Reading** | Deferred | Normal | Session active — mid_pass suppressed |
| **Validating** | Deferred | Normal | Session active — mid_pass suppressed |
| **Publishing** | Deferred briefly | Normal | Finalize publish |
| **Completed** | Allowed; **selected slot may rebuild immediately** | Normal | Normal |
| **Failed** | Allowed | Normal | Normal |

**mid_pass:** suppressed **only** while `SlotLoadSession` is active — **not** while queue is merely non-empty. Same semantic Phase 1 and Phase 3.

**Display:** follow session state, not queue depth. Queued background work must not freeze the piano roll. After tier-0 / focus slot Publishes, allow immediate display rebuild even if the queue still has work.

Phase 3 adds cooperative time-slicing within **Reading** without changing states or gate APIs.

---

## Publication rule and visibility

**Visibility invariant:** Nothing outside the loader may observe a slot until **Publishing** has completely finished.

No component may observe:

- Partially adopted chunks
- Partially updated metadata
- Incomplete persistence markings

Preconditions before Publishing may finish:

1. Payload successfully read into staging (**Reading**)
2. Validation of staging complete (**Validating**)
3. Chunk adoption complete (`adoptPersistedSnapshot`)
4. Persistence correctly marked (`markChunkPersistedFromSdLoad` per chunk)
5. Slot metadata consistent / swapped live

Never publish partially loaded data.

---

## Loading vs reloading vs recovery

### Loading (this plan)

**Purpose:** Read a valid loop from SD into an **empty** slot.

**Guarantee:** All-or-nothing — slot is completely **Published** or unchanged. Partial publication is **never** allowed.

### Reloading (future / explicit path)

**Purpose:** Replace an already **Published** slot with a newly loaded version.

```text
Published slot
  → load into staging (off live passes)
  → validate
  → atomic publish (swap passes)
  → release previous published chunk refs
```

If validation fails, the previously published loop remains active.

Not required for Phase 1–3 boot lazy load (empty slots at manifest scan).

### Recovery (separate policy — DEC-020 Phase 5)

**Purpose:** Best-effort recover from damaged storage (truncated slot, `.sealj` prefix).

**Guarantee:** Partial success allowed; loop may be marked **recovered**.

> **Recovery policy** may intentionally publish a **partially reconstructed** loop when the recovered data is structurally consistent. **Loading never permits this.** Recovery may. This is why the policies remain separate.

See [`continuous_runtime_persistence_phase5_recovery_handoff.md`](continuous_runtime_persistence_phase5_recovery_handoff.md).

**Share parser helpers with loading; do not merge policies.**

---

## Active load cancellation

When the user changes slot while another slot is loading:

```text
finish current slot load
  → reprioritize SlotLoadQueue
  → load newly selected slot next
```

**Do not** cancel an active SD read. Only **queued** work changes priority.

---

## Failure handling

| Failure | Result |
|---------|--------|
| **SD read** — header OK, payload fails | Discard staging; slot unchanged; log; continue queue |
| **Validation** — bad header, event count, payload, integrity | Discard staging; slot unchanged; log diagnostics |
| **Memory exhaustion** — `append` / pool full | Discard staging; no partial publish; log; continue per policy |
| **User reprioritization** | Current load completes; queue reordered; no cancel |
| **Unexpected exit** | RAII / session teardown restores SD mode + activity count |

---

## Telemetry (SESSION_CAPTURE; RAM1-safe)

Aggregate counters only (no per-chunk strings in hot path):

| Marker | Meaning |
|--------|---------|
| `#CAP,BOOT,persist,queue,N` | Persistence queue depth after slot load |
| `#CAP,LOAD,queue,peak,N` | High-water pending queue depth this session |
| `#CAP,LOAD,queue,remain,N` | Pending queued slots (not including active session) |
| `#CAP,LOAD,chunks,N` | Chunks adopted in last completed load (uint16) |
| `#CAP,LOAD,bytes,N` | Approx bytes read in last load (uint32, optional) |
| **`#CAP,LOAD,outcome,published`** | Session completed successfully |
| **`#CAP,LOAD,outcome,failed`** | Session failed; slot unchanged |

Emit outcome + aggregate counters on **Completed** / **Failed** only. No dynamic String; fixed `char` stacks like existing `#CAP,PERS,...`.

Verifier: keep `mid_pass_during_restore`; parse `outcome,failed` for `slot_load_failed_count`.

---

## Implementation phases (order unchanged)

### Phase 0 — Baseline

- Current-tree firmware (blocking restore as today) — **not** the reverted stashed lazy-load branch
- Historical captures for timing comparison: [`session_20260715_134532.log`](../captures/session_20260715_134532.log), [`session_20260715_135614.log`](../captures/session_20260715_135614.log)
- Optional: fresh capture + [`verify_boot_restore_timing.py`](../scripts/verify_boot_restore_timing.py) on current tree
- Sign off interference checklist before Phase 1

#### Phase 0 results (2026-07-17) — **SIGNED OFF**

Verifier runs (historical logs; no stash re-applied):

| Capture | Kind | Queued | Deferred lines | Restore span | Verifier `mid_pass_during_restore` | Host USB begin | Notes |
|---------|------|--------|----------------|--------------|-------------------------------------|----------------|-------|
| [`session_20260715_134532.log`](../captures/session_20260715_134532.log) | Blocking drain (closest to **current tree**) | 26 | 26 | (batched in setup/`loop` before USB; no per-line CAP span) | **0** (window ends at `usb_host,begin`) | ~**16.3 s** host | **17×** `#CAP,PERS,mid_pass` **after** USB for `t1:s3` — still waste; verifier does not count them |
| [`session_20260715_135614.log`](../captures/session_20260715_135614.log) | Stashed lazy (regression) | 25 bg | 25 | **11.09 s** | **17 FAIL** | early (`BOOT,restore,interactive` then USB before drain) | mid_pass interleaved with deferred restores; DISP 352 ms then 1379 ms |
| [`session_20260718_010126.log`](../../captures/session_20260718_010126.log) | **Phase 1+1C/2B sign-off** | 26 | 26 | **2.93 s** | **0 PASS** (0 mid_pass entire log) | after drain | Continuous `DFRAME` notes; 64-bar windowed `DISP`; see windowed plan |

**Phase 1 exit gate clarification:** zero mid_pass while a load session is active **and** zero mid_pass that re-writes SD-loaded published chunks (mark-from-SD). Do not treat “PASS mid_pass_during_restore” alone as sufficient if mid_pass still labels `ok:t*:s*:` for freshly restored slots after USB.

#### Interference checklist (current tree — signed off)

| # | Interference | Evidence / owner | Phase 1 fix |
|---|--------------|------------------|-------------|
| 1 | `sealChunk` always `admitSealedChunk` | [`LoopEventStore::sealChunk`](../../src/LoopEventStore.cpp) | `SlotLoadSession` → mark persisted, not admit |
| 2 | No `markChunkPersistedFromSdLoad` | absent in tree | PersistenceQueue + post-publish mark |
| 3 | mid_pass gate ignores slot load | [`shouldRunMidPassWriter`](../../src/PersistenceFailurePolicy.cpp) = `queueDepth && !otherSdIoActive` | Suppress only while session active |
| 4 | Focus path sync double-load | [`prioritizeLoopSlotRestoreForFocus`](../../src/StorageManager.cpp) calls `requestLoopSlotRestoreFromSd` | `needsSlotLoad` + reprioritize only |
| 5 | USB tied to empty restore queue | [`main.cpp`](../../src/main.cpp) `!hasPendingLoopSlotRestore()` | Introduce `bootInteractiveReady()` (P1: queue empty; P3: tier-0) |
| 6 | Display/idle defer on queue depth | [`DisplayManager`](../../src/DisplayManager.cpp), [`Track`](../../src/Track.cpp) `hasPendingLoopSlotRestore` | Defer on session state only |
| 7 | Full DISP after load | `#CAP,DISP` note counts (e.g. 352 then 1379) misread as ms historically; hang = sync full visual rebuild | Phase 1C/2B bounded published reconstruction |
| 8 | mid_pass labels selected track/active slot | `ok:t1:s3` during other slot work | Isolation (1) removes need; do not expand mid_pass retarget in Phase 1 |

**Phase 0 exit:** Checklist signed; proceed to Phase 1A.

### Phase 1 — SD load isolation + session + policy

**1A — Invariant enforcement**

- `PersistenceQueue::markChunkPersistedFromSdLoad`
- **`SlotLoadSession`** — stack RAII + **`SlotLoadSessionState`**; Reading = staging only; Publishing = adopt + mark + visibility
- `markLoopPublishedChunksPersistedFromSdLoad` after successful publish
- **`needsSlotLoad(track, slot)`** — not Published (payload), not queued, not actively loading

**1B — Scheduler gates (final semantics — no Phase 3 API change later)**

- `shouldRunMidPassWriter` false **only** while `SlotLoadSession` is active (queue depth irrelevant)
- Dequeue before session starts; queue depth = pending only
- Focus reprioritize uses **`needsSlotLoad`**; finish active session before reorder
- **`hasPendingSlotLoads()`** — background work outstanding — **not** used for USB or mid_pass
- **`bootInteractiveReady()`** — Phase 1: queue empty; Phase 3: tier-0 Published
- Behavioral PR only: session + gates + `needsSlotLoad`. Mechanical rename = **follow-up PR**

**1C — Derived-view firewall** + **2B — Window-first display**

Shipped together as **bounded published reconstruction** ([`boot_load_windowed_display_reconstruction_refinement.md`](boot_load_windowed_display_reconstruction_refinement.md)):

- Canonical `Loop::gatherPublishedEvents` / `gatherPublishedEventsInWindow` via `PublishedEventRange`
- `shouldAvoidFullVisualRebuild` — no `ensureVisualCacheBuilt` on display hot path when true
- Windowed reconstruction (not “provisional”); idle slice uses windowed gather
- Normative: `mergeActiveCapturePasses` is capture-time only for published display

**Phase 1 exit:** `mid_pass_during_restore == 0` (no mid_pass during **active** session windows); `#CAP,LOAD,outcome,*` in captures; HITL smoke.

#### Phase 1A progress (2026-07-17)

Shipped in tree (behavioral; rename follow-up still pending):

- `PersistenceQueue::markChunkPersistedFromSdLoad`
- `LoopEventStore::enterSdLoadStaging` / `leaveSdLoadStaging` — `sealChunk` marks Persisted under staging
- [`SlotLoadSession`](../../include/SlotLoadSession.h) stack RAII around `loadLoopSlotFromCurrentSetSd`
- `markLoopPublishedChunksPersistedFromSdLoad` after successful read
- `StorageManager::needsSlotLoad` + queue admission gate

Native: `test_persistence_queue` + `test_sd_load_adopt` **PASS**.

#### Phase 1B + 1C progress (2026-07-17)

Shipped in tree after 1A device evidence ([`session_20260717_215801.log`](../../captures/session_20260717_215801.log) still showed 17× mid_pass from rematerialize):

- `LoopEventStore::enterEphemeralSeal` / `leaveEphemeralSeal` — `sealChunk` does not admit mid_pass
- `Loop::materializeEditViewFromPasses` + `rematerializeEditView` wrap materialize in ephemeral seal
- `PersistenceQueue::resetForTests` from pool init (EXTMEM queue must not survive soft reset)
- `StorageManager::bootInteractiveReady()` — queue empty && !session active; USB gate uses it
- `otherSdIoActive` includes `SlotLoadSession::isActive()`
- `prioritizeLoopSlotRestoreForFocus` — skip sync only when deferred work already pending
- Display / Track: defer expensive rebuild on **session active** only (not queue non-empty)

Native: `test_persistence_queue` (+ ephemeral seal), `test_sd_load_adopt`, `test_persistence_failure_policy` **PASS**.

#### Phase 1 + 1C/2B device gate (2026-07-18) — **PASS**

Evidence: [`session_20260718_010126.log`](../../captures/session_20260718_010126.log)

| Gate | Result |
|------|--------|
| `verify_boot_restore_timing.py` | **PASS** — 26 restores, **2.93 s**, `mid_pass_during_restore == 0` |
| mid_pass after USB | **0** in entire capture (not only restore window) |
| Display paint | First `DISP` **216** notes; `DFRAME` note counts continuous (~10–13 ms) |
| 64-bar window | `DISP` 16-bar window path after LoopEnd to `len=49152` |
| Detail | [`boot_load_windowed_display_reconstruction_refinement.md`](boot_load_windowed_display_reconstruction_refinement.md) |

**Next:** Phase 2 device timing gate — flash + boot capture; compare restore span to `010126` (~2.93 s).

### Phase 2 — Batch SD read — **In tree** (2026-07-18)

- `readCapturePassSlotFileHeader` / skip-capture / edit-pass payload: batched `ioRead` up to `CHUNK_CAPACITY` (256) into extmem scratch (mirrors write chunk stream)
- Native: `test_capture_pass_read_uses_chunk_stream_batch_bound` + existing round-trips **PASS**
- No ownership / session-scope change
- **Device gate:** restore span shorter than [`session_20260718_010126`](../../captures/session_20260718_010126.log) baseline on comparable set; `mid_pass_during_restore == 0`

### Phase 2B — Window-first display (>16 bars) — **Done** (shipped with 1C)

- Canonical `gatherPublishedEventsInWindow` / idle `rebuildVisualCacheIdleSlice` (windowed gather)
- Device: 64-bar useful paint via windowed `DISP`; use `#CAP,DFRAME` elapsedUs (~12 ms) — do **not** treat `DISP` payload fields as milliseconds

### Phase 3 — Prioritized lazy load — **early USB / audible set REVERTED** (2026-07-18)

| Priority | Slots |
|----------|-------|
| 0 | Active slots all tracks + focus selected if split (drain order only) |
| 1 | Selected slot on other tracks |
| 2 | All other slots |

**Kept:** priority sort for restore order; OLED gated on `SlotLoadSession` active only during drain; Phase 1 isolation + windowed display + Phase 2 batch `ioRead`.

**Reverted:** audible sync publish + early USB (`audibleBootSetReady_`). Interactive path is again **full queue drain** before `bootInteractiveReady()` → `finishBootSetup()` → USB → piano roll. Title (OSTINATIX) stays until drain completes.

**Historical PASS (early USB, tier-0 only):** [`012025`](../../captures/session_20260718_012025.log) — superseded by revert; Play incomplete with tier-0-only.

**Deferred (Phase 3b):** StorageManager-owned mid-file ~35 ms Reading slices.  
**Not in scope:** load-while-playing; playback merged-events window; Phase 4 SD chunk index.

**Device gate (post-revert):** flash + boot; `verify_boot_restore_timing.py` drain-before-USB PASS; no `Boot audible` logs; no deferred restores after `usb_host,begin`; piano roll only after ready.

### Phase 4 (optional) — v7 SD chunk index

See Cursor plan § Phase 4. Only if SD read time still unacceptable after 1–2.

---

## Key files

| Area | Files |
|------|-------|
| Session / invariant | `SlotLoadSession` (new), `LoopEventStore.cpp`, `PersistenceQueue.cpp` |
| Queue / policy | `StorageManager.cpp`, `SlotLoadPriority.h` |
| Wire parse | `StorageLoopIo.cpp` |
| Gates | `PersistenceFailurePolicy.cpp`, `main.cpp` |
| Display | `DisplayManager.cpp`, `Loop.cpp` |

---

## Verification matrix

| Gate | Command / artifact |
|------|-------------------|
| Native | `pio test -e native` |
| Boot timing | `verify_boot_restore_timing.py --follow-current-session` |
| No mid_pass during load | `mid_pass_during_restore == 0` — **PASS** `010126` |
| Long-loop first paint | Windowed `DISP` + `#CAP,DFRAME` elapsedUs (~12 ms) — **PASS** `010126` (do not use DISP count fields as ms) |
| Load telemetry | `#CAP,LOAD,outcome,*` + queue/chunk counters; no RAM1 regression |
| HITL | `host_midi_hitl.py run --preset base` after Phase 3 |

---

## Summary

Phase order: 0 → 1 → **2 (next)** → 2B display done with 1C → 3. Phase 1+2B device gate **PASS** (`010126`).

Document flow for readers:

1. Context → Invariant → Policy → Principles  
2. Glossary → **Loading pipeline** → **Lifecycle**  
3. Ownership → **Scheduler contract** → Publication  
4. Recovery / failure / telemetry → Phases → Verification  

---

## Pre-implementation review (clarity)

### Ready

| Topic | Decision |
|-------|----------|
| Invariant | SD-loaded chunks never re-enter persistence unless later modified |
| Ownership | Queue (pending) → Session (active) → StorageManager → LoopEventStore → Published slot |
| Naming | `SlotLoadQueue`, `SlotLoadRequest`, `SlotLoadSession`, `needsSlotLoad`, `computeSlotLoadPriority` |
| Lifecycle states | Queued → Dequeued → Reading → Validating → Publishing \| Failed → Completed |
| Cancellation | Never cancel active session; only reorder pending queue |
| Loading vs recovery | Loading all-or-nothing; recovery may publish partial |
| Phase order | 0 → 1 → 2 → 2B → 3; Phase 1+2B display **PASS** (`010126`); Phase 3 after Phase 2 |
| Priority model (Phase 3) | 0 = selected track selected slot; 1 = other tracks’ selected slots; 2 = rest |
| Publication visibility | Nothing outside loader observes slot until Publishing finishes |

### Resolved (2026-07-17 open-review resolutions)

| # | Topic | Decision |
|---|-------|----------|
| 1 | mid_pass gate | Suppress **only** while `SlotLoadSession` active — never because queue non-empty |
| 2 | USB / interactive | Dedicated `bootInteractiveReady()` (Phase 1: queue empty; Phase 3: tier-0 Published); not `hasPendingSlotLoads()` |
| 3 | Phase 0 baseline | Current tree + historical `session_20260715_*` logs — no stashed branch |
| 4 | Session lifetime | Same lifecycle; Phase 1 stack RAII; Phase 3 StorageManager-owned optional session |
| 5 | Staging vs publish | Reading = staging only; Validating = validate staging; Publishing = adopt + mark + swap |
| 6 | Published | Payload successfully adopted into live runtime; metadata-only is not Published → `needsSlotLoad` true |
| 7 | Phase 1 scheduler | Ship final mid_pass semantics in Phase 1 (session-active only) |
| 8 | Rename scope | Behavioral PR first; mechanical rename follow-up PR |
| 9 | Phase 2 wording | Batched reads inside existing Reading state — no ownership change |
| 10 | Display defer | Follow session state; Queued allows rebuilds; selected slot may rebuild immediately after Publish |

### Proceed?

- **YES** — open items pinned; Phase 1 may start after Phase 0 checklist sign-off.
