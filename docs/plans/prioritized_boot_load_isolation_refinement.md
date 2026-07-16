# Prioritized boot load isolation — refinement

**Kind:** refinement  
**Date:** 2026-07-15  
**Status:** Planned (stashed lazy-load reverted; Phase 0 not started)  
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

## Ownership model

```text
SlotLoadQueue          ← pending work only (depth = not-yet-started)
        │
        ▼ dequeue
SlotLoadSession        ← one active load; owns temporary loading state
        │
        ▼
StorageManager         ← performs read / validate / publish orchestration
        │
        ▼
LoopEventStore         ← published chunk data + persistence marks
        │
        ▼
Published Slot         ← externally visible only after publication completes
```

| Component | Responsibility |
|-----------|----------------|
| **SlotLoadQueue** | Decides **what loads next**; holds **pending** `SlotLoadRequest` entries only |
| **SlotLoadSession** | Owns **temporary loading state** for exactly one slot load (RAII lifetime) |
| **StorageManager** | Executes the load (source-specific readers call into shared publish path) |
| **LoopEventStore** | Owns published chunk data; SD-load mode routes `sealChunk` → mark persisted |
| **Recovery** | Independent policy — may publish partial when structurally consistent |

The **source** changes (current set SD, another set, MIDI import, backup, recovery). The **destination** (slot) does not. Abstraction:

```text
Load loop  →  Publish into slot
```

not “load current set” as the top-level concept.

---

## Ownership and naming

### Slot-oriented terminology

Express the pipeline in terms of **slots**, not boot or sets. The same pipeline will eventually support boot, another set, single-loop import, MIDI import, backup restore, and future formats.

| Responsibility | Name |
|----------------|------|
| Queue | **`SlotLoadQueue`** |
| Queue entry | **`SlotLoadRequest`** |
| Active operation | **`SlotLoadSession`** |
| Scheduling helper | **`needsSlotLoad(track, slot)`** |
| Priority helper | **`computeSlotLoadPriority(...)`** in **`SlotLoadPriority.h`** |

| Current (rename in implementation) | Target |
|-----------------------------------|--------|
| `PendingLoopSlotRestoreQueue` | **`SlotLoadQueue`** |
| `pendingLoopSlotRestores_` | `slotLoadQueue_` |
| `DeferredLoopSlotRestore` | **`SlotLoadRequest`** |
| `processDeferredLoopSlotRestore` | `processNextSlotLoad` (optional rename) |
| `hasPendingLoopSlotRestore()` | **`hasPendingSlotLoads()`** — queue depth **or** active session |
| `BootLoopSlotRestore.h` | **`SlotLoadPriority.h`** |

Source-specific executors (unchanged verbs where possible):

- `loadLoopSlotFromCurrentSetSd(...)` — Phase 1–3
- `loadLoopSlotFromMidiFile(...)` — future
- `loadLoopSlotFromBackup(...)` — future
- `recoverLoopSlot(...)` — recovery policy (DEC-020 Phase 5)

Every implementation creates a **`SlotLoadSession`** internally; loading policy stays identical; only the source differs.

### Queue vs active load

The queue owns **pending work only**. Once a request begins executing it is **dequeued** and tracked by **`SlotLoadSession`**, not the queue.

- **Queue depth** = pending `SlotLoadRequest` count only
- **Active load** = at most one `SlotLoadSession` (or refcount if nested sources share session — prefer one session per slot)
- Retries: re-enqueue a new `SlotLoadRequest` after **Failed** (diagnostics emitted in Failed)

### Central helper: `needsSlotLoad(track, slot)`

Single admission gate for boot, deferred loading, reprioritization, and future import paths.

Returns **true** when the slot:

1. Is **not** currently **Published**
2. Is **not** already in **`SlotLoadQueue`**
3. Is **not** currently being loaded (**no active `SlotLoadSession`** for that address)

Replaces scattered checks (`hasPublishedEvents`, duplicate queue entries, sync + deferred double-load).

### `SlotLoadSession` (Phase 1 — replaces `SdLoadGuard` + `LoadActivityGuard`)

One session = one complete logical slot load. It owns temporary loading context, coordinates with the scheduler, defines publication boundaries, and restores normal runtime behavior on **Completed** or **Failed**.

The loading architecture is evolving from a synchronous boot operation into a **cooperative background process** (lazy load, tiered priority, set switch, import, recovery). A session may span many `loop()` iterations and therefore interacts with display, playback, `mid_pass`, storage I/O, and future task budgeting.

The lifecycle below is primarily an **architectural coordination tool** — Phase 1 may complete a session in one call path, but the session should expose explicit state so Phase 3 time-sliced loading, display budgeting, pause/resume, and progress reporting can attach without changing the model.

**RAII responsibilities (Dequeued → session end):**

- Enable SD loading mode on `LoopEventStore` (`sealChunk` → `markChunkPersistedFromSdLoad`)
- Increment `activeLoadCount` (mid_pass gate, `otherSdIoActive`, display defer)
- Track **`SlotLoadSessionState`** for scheduler coordination
- On destruction (**Completed** or **Failed**): disable SD loading mode; decrement `activeLoadCount`

**Example (sync path; same session object supports cooperative slices in Phase 3):**

```cpp
bool loadLoopSlotFromCurrentSetSd(Track& track, uint8_t slot) {
  SlotLoadSession session(storageManager, track.index(), slot);
  session.setState(SlotLoadSessionState::Reading);
  readCapturePassSlotFileHeader(...);
  session.setState(SlotLoadSessionState::Validating);
  if (!validate()) { session.fail(); return false; }
  session.setState(SlotLoadSessionState::Publishing);
  adoptPersistedSnapshot();
  markLoopPublishedChunksPersistedFromSdLoad(...);
  publish();  // only externally visible transition
  session.complete();
  return true;
}
```

No explicit cleanup required — RAII on early return, validation failure, or OOM.

**Phase 2 note:** Extend the existing **`SlotLoadSession`** to cover the **entire** slot load (one session per slot, not one per pass). Phase 2 adds batch read inside **Reading** state.

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

### State responsibilities

#### Queued

**Owner:** `SlotLoadQueue`

- Determine load priority; allow reprioritization
- No resources allocated
- Other systems operate normally

#### Dequeued

**Owner:** transfers from queue → **`SlotLoadSession`**

- Create session; enter loading mode; increment `activeLoadCount`
- Queue no longer owns this request

#### Reading

**Owner:** `SlotLoadSession`

- Read slot data; adopt persisted chunks; staging work
- May span multiple `loop()` iterations (Phase 3 cooperative slices)
- Scheduling may defer expensive display rebuilds; suppress conflicting storage work; playback continues normally

#### Validating

**Owner:** `SlotLoadSession`

- Validate payload, metadata, structural consistency
- Destination slot remains unchanged
- Failure exits directly to **Failed**

#### Publishing

**Owner:** `SlotLoadSession`

- Atomically publish slot; finalize metadata; expose new loop
- **Only externally visible transition** — no partial publication observable outside loader
- Scheduling may delay display updates until publish completes

#### Completed

**Owner:** session teardown

- Leave loading mode; decrement `activeLoadCount`; resume deferred work; remove session
- Emit `#CAP,LOAD,outcome,published`
- Slot fully operational

#### Failed

**Owner:** session teardown

- Discard staging; restore normal runtime; destination slot unchanged; report diagnostics
- Emit `#CAP,LOAD,outcome,failed`
- System state as if load never started; may re-enqueue via `needsSlotLoad` when appropriate

### Scheduler interaction (coordination points)

Exact decisions remain implementation-specific; lifecycle provides stable sync points:

| Session state | Display | Playback | Storage |
|---------------|---------|----------|---------|
| **Queued** | Normal | Normal | Normal |
| **Reading** | Defer expensive rebuilds | Normal | Slot load active |
| **Validating** | Defer rebuilds | Normal | Slot load active |
| **Publishing** | Delay updates until complete | Normal | Finalize publish |
| **Completed** | Resume | Normal | Normal |
| **Failed** | Resume | Normal | Normal |

Phase 1 implements gates at **Reading/Validating/Publishing** (mid_pass suppress, `otherSdIoActive`, display defer). Phase 3 adds cooperative time-slicing within **Reading** without changing states.

### Design goal

`SlotLoadSession` represents one complete logical slot loading operation regardless of source (boot, another set, single loop, MIDI import, backup, recovery). Only the reader changes; the lifecycle stays identical. Future features (time-sliced load, display budgeting, pause/resume, progress) attach to session state without new ownership concepts.

---

## Loading vs reloading vs recovery

### Loading (this plan)

**Purpose:** Read a valid loop from SD into an **empty** slot.

**Guarantee:** All-or-nothing — slot is completely **Published** or unchanged.

Partial publication is **never** allowed.

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

Not required for Phase 1–3 boot lazy load (empty slots at manifest scan); document for set import / revision reload alignment.

### Recovery (separate policy — DEC-020 Phase 5)

**Purpose:** Best-effort recover from damaged storage (truncated slot, `.sealj` prefix).

**Guarantee:** Partial success allowed; loop may be marked **recovered**.

> **Recovery policy** may intentionally publish a **partially reconstructed** loop when the recovered data is structurally consistent. **Loading never permits this.** Recovery may. This is why the policies remain separate.

See [`continuous_runtime_persistence_phase5_recovery_handoff.md`](continuous_runtime_persistence_phase5_recovery_handoff.md).

**Share parser helpers with loading; do not merge policies.** Loading stays all-or-nothing; recovery stays best-effort. Each path uses its own `SlotLoadSession` + policy-specific validate/publish rules.

---

## Active load cancellation

When the user changes slot while another slot is loading:

```text
finish current slot load
  → reprioritize SlotLoadQueue
  → load newly selected slot next
```

**Do not** cancel an active SD read (avoids partial slot state, abandoned staging, SD seek thrashing). Only **queued** work changes priority.

---

## Failure handling

| Failure | Result |
|---------|--------|
| **SD read** — header OK, payload fails | Discard staging; slot unchanged; log; continue queue |
| **Validation** — bad header, event count, payload, integrity | Discard staging; slot unchanged; log diagnostics |
| **Memory exhaustion** — `append` / pool full | Discard staging; no partial publish; log; continue per policy |
| **User reprioritization** | Current load completes; queue reordered; no cancel |
| **Unexpected exit** | RAII: **`SlotLoadSession`** restores SD mode + activity count |

---

## Publication rule and visibility

**Visibility invariant:** Nothing outside the loader may observe a slot until **publication has completely finished**.

No component may observe:

- Partially adopted chunks
- Partially updated metadata
- Incomplete persistence markings

Publish only after:

1. Payload successfully read
2. Validation complete
3. Chunk adoption complete (`adoptPersistedSnapshot`)
4. Persistence queue correctly marked (`markChunkPersistedFromSdLoad` per chunk)
5. Slot metadata consistent

**Publication** is the final externally visible step. Never publish partially loaded data.

---

## Telemetry (SESSION_CAPTURE; RAM1-safe)

Extend Phase 0/1 markers; **aggregate counters only** (no per-chunk strings in hot path):

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

- [`verify_boot_restore_timing.py`](../scripts/verify_boot_restore_timing.py) on stashed firmware
- Baselines: [`session_20260715_134532.log`](../captures/session_20260715_134532.log), [`session_20260715_135614.log`](../captures/session_20260715_135614.log)
- Sign off interference checklist before Phase 1

### Phase 1 — SD load isolation + session + policy

**1A — Invariant enforcement**

- `PersistenceQueue::markChunkPersistedFromSdLoad`
- **`SlotLoadSession`** — RAII + **`SlotLoadSessionState`** enum; gates at Reading/Validating/Publishing
- `markLoopPublishedChunksPersistedFromSdLoad` after successful publish
- **`needsSlotLoad(track, slot)`** — not Published, not queued, not actively loading; all admission paths use it

**1B — Scheduler gates**

- `shouldRunMidPassWriter(...)` false while **`SlotLoadSession`** active or queue non-empty (per policy)
- **`SlotLoadQueue`** rename; dequeue before session starts; queue depth = pending only
- Focus reprioritize uses **`needsSlotLoad`**; finish active session before reorder
- **`hasPendingSlotLoads()`** — pending queue **or** active session (for display defer / USB timing)

**1C — Derived-view firewall**

- Defer full materialize / 1379 ms DISP while **`hasPendingSlotLoads()`**
- Audit `gatherPublishedFlatForDerivedView` paths; respect publication visibility invariant

**Phase 1 exit:** `mid_pass_during_restore == 0`; `#CAP,LOAD,outcome,*` in captures; HITL smoke.

### Phase 2 — Batch SD read

- Batch `ioRead` in `readCapturePassSlotFileHeader` (extmem scratch)
- Extend existing **`SlotLoadSession`** to cover entire slot load (one session per slot, not per pass)

### Phase 2B — Window-first display (>16 bars)

- `appendChunkRefEventsInWindow` / `mergeActiveCapturePassesInWindow`
- Playhead-priority `rebuildVisualCacheIdleSlice`
- First paint ≤ 200 ms `#CAP,DISP` on 64-bar loop

### Phase 3 — Prioritized lazy load

**Priority** ([`SlotLoadPriority.h`](include/Utils/BootLoopSlotRestore.h) rename):

| Priority | Slots |
|----------|-------|
| 0 | Selected track, **selected slot only** |
| 1 | Selected slot on other tracks |
| 2 | All non-selected slots |

- Sync tier-0 only in `loadState` under **`SlotLoadSession`** + publish rules
- Background: ~35 ms slices; one session per dequeued request
- Interactive USB after tier-0 + isolation proven; no loading screen
- Reprioritize on focus; **no cancel** of active session

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
| No mid_pass during load | `mid_pass_during_restore == 0` |
| Long-loop first paint | 64-bar `#CAP,DISP` ≤ 200 ms (Phase 2B) |
| Load telemetry | `#CAP,LOAD,outcome,*` + queue/chunk counters; no RAM1 regression |
| HITL | `host_midi_hitl.py run --preset base` after Phase 3 |

---

## Summary

These refinements **do not change phase order** (0 → 1 → 2 → 2B → 3). They clarify:

- Core **invariant**, **loading policy**, and **publication visibility**
- **Ownership model:** `SlotLoadQueue` → `SlotLoadSession` → StorageManager → LoopEventStore → Published slot
- **Queue vs active load:** pending in queue; dequeued into session; lifecycle Queued → Dequeued → Reading → Validating → Publishing/Failed → Completed
- **`SlotLoadSession`** + explicit lifecycle (cooperative-ready; sync in Phase 1)
- **`needsSlotLoad`** — not Published, not queued, not actively loading
- **Slot-oriented terminology** and future source extensibility
- **State machine**, **failure handling**, **recovery vs loading**
- **No cancel** of active session on reprioritize
- **RAM1-safe** load telemetry including **`#CAP,LOAD,outcome,published|failed`**
