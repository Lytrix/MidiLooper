# LoopContentResolution — incremental post-commit maintenance (6D)

**Status:** Active — design/measurement investigation. No firmware.  
**Date:** 2026-08-15  
**Kind:** refinement (investigation)  
**Decision:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype)  
**Parent:** [`loop_event_sourced_resolution_architecture.md`](loop_event_sourced_resolution_architecture.md)  
**Handoff:** [`loop_content_resolution_stage9_handoff.md`](loop_content_resolution_stage9_handoff.md)  
**OpenSpec:** `openspec/changes/loop-content-resolution/`  
**Does not authorize:** firmware; A (re-arm STOPPED cold-build); B (slice the 30–60 s cold build during PLAYING); 6C consume-path changes; flattening `openOnByPitch`; representation B; deleting `materializeToEventVector`

---

## Identity

**6D** is a Stage 6 investigation name. It is **not** DEC-037 capability letter C (candidate find). It is **not** Stage 6C (consume prepared LCR into `overdubSourceView` when already ready).

| Name | What it is |
|------|------------|
| DEC-037 capability **B** | Incrementally maintain effective content |
| DEC-037 capability **D** | Reach a useful state without replaying history from zero |
| Stage **6C** | Consume-when-ready proof: `tryResolvePreparedWindow` → `overdubSourceView` |
| Stage **6D** | Can a committed overdub update already-built LCR indexes and invalidate/rebuild only affected resolution state, without cold reconstruction of historical content? |

6D is capability **B + D on commit**, plus a scheduling question: maintenance slices while PLAYING. 6C can remain a small consume-when-ready proof. It does not make LCR always-ready across commits.

---

## Why this investigation exists

Prepared LCR is not late at the overdub button. The scheduler and stamp rules produce this state on purpose:

```text
PLAYING
  │
  ├─ visualCache maintenance runs
  │
  └─ LCR maintenance does NOT run
          │
          ▼
OVERDUB STOP
  │
  └─ playbackRevision changes
          │
          ▼
preparedWindowReady = false
```

Evidence and the three firmware rules: [`loop_content_resolution_stage9_handoff.md`](loop_content_resolution_stage9_handoff.md) § Why LCR is not ready.

The requirement 6D has to answer:

> LCR must have an incremental maintenance path that keeps the already-built representation usable across commits, rather than relying on a new cold build after every playback revision.

---

## Rejected (do not implement)

### A — re-arm STOPPED cold-build on stamp mismatch

```text
revision mismatch → wait until STOPPED → 30–60 s preparation
```

Does not meet PLAYING overdub-over-overdub. Repeats the existing one-shot cold build. Architecturally rejected.

### B — slice the existing full-history LCR build during PLAYING

Uninterrupted cold complete: [`173842`](../../captures/session_20260815_173842.log) **31.8 s** STOPPED; [`185931`](../../captures/session_20260815_185931.log) **39.6 s**. Slicing that work onto PLAYING spends that CPU/memory bandwidth on the perform path and still rebuilds all history after every commit. That is a continuously maintained full-history cache. Rejected.

Do not start A or B.

---

## Gate before any incremental algorithm

Do not design the merge/repair algorithm first. Answer:

> **What exactly has to change in LCR when an OverdubPass is committed?**

Then measure whether that work can scale as `O(new events + affected checkpoints)` rather than `O(all historical events)`.

---

## What commit already knows

`Loop::commitPendingCapturePass` (`src/Loop/LoopCapture.cpp`) publishes:

- new `OverdubPass` (or `RecordPass`) id, `mergeSequence`, `sealedAtTick`, `committedChunkIds`
- `++playbackRevision`

It does **not** call `TickIndex::commitCapturePass` or touch `StateCheckpoints`.

`Loop::markAffectedDisplayCacheRanges` (6B, `src/Loop/LoopVisualCache.cpp`) already walks those chunks via `LoopEventStore::appendChunkRefEvent` and marks display-bar neighborhoods dirty. The commit already has an affected tick/bar interval for display. 6D must not invent a second pass-list walk to rediscover that interval.

6.0 still holds: `startOverdubbing` / `stopOverdubbing` must not synchronously construct, sort, checkpoint, or resolve LCR. Any 6D work is **post-commit sliced maintenance**, not work on the button.

---

## What 6A / 6C actually read

`LoopContentResolution::resolveWindow` (`src/LoopContentResolution.cpp`) used by 6A idle display and 6C `tryResolvePreparedWindow`:

1. `TickIndex::findRawWindow` → `findRawWindowFromTickEvents` → `visitTickEventRange` on `tickEvents`
2. `TickIndex::appendNoteEvents` via `byNoteId` for active `EditPass` rows
3. sort / unique / `applyNoteEditPassSequence`

`StateCheckpoints` (`soundingAt`, `spanBoundaries`, `channelByNoteId`) are **not** on this path. They serve `resolveState` (bounded historical replay).

So “LCR ready for the next overdub source window” and “LCR ready for `resolveState`” are two different prepared states.

```text
LoopPasses
   │
   └── committed OverdubPass
           │
           ▼
     LCR maintenance
      ┌────┴─────┐
      ▼          ▼
 Layer 1      Layer 2
 tickEvents   spans + spanBoundaries
 byNoteId     soundingAt checkpoints
      │          │
      ▼          ▼
 resolveWindow resolveState
 (6A / 6C)     (loop-switch / state query)
```

Layer 1 is the minimum for “next overdub can use LCR.” Layer 2 is required for the DEC-037 `resolveState` invariant. Do not collapse them.

---

## Per-structure: what a new OverdubPass changes

Pinned from `include/LoopContentResolution.h` and `TickIndex` / `StateCheckpoints` in `src/LoopContentResolution.cpp`. Frozen representations stay frozen (5.15 / 5.17 / 5.7c / 5.18). This table is “what must stay true after commit,” not “which algorithm.”

### `capturePasses` / `passById`

`TickIndex::beginCapturePass` already appends one `CapturePassEntry` and records `passById`. A new committed pass is an append. Undo/redo is `setCapturePassState` (Active ↔ Disabled), not a rewrite of chunks. Content stays immutable (DEC-035).

### `tickEvents`

Flat `{tick, passId, eventIndex}[]`. C-order append then `stable_sort` by tick (5.17 A). `indexCapturePassEventRange` already appends only the new pass’s events. `commitCapturePass` then calls `sortTickEventEntriesByTick` on the **entire** vector.

`findRawWindowFromTickEvents` binary-searches that ordered list. Disabled passes stay in the list; `collectTickRangeRefs` skips `CapturePassState` ≠ Active at visit time. Undo does not require deleting `tickEvents` rows.

**Question for measurement:** append unsorted delta then bounded merge into ordered `tickEvents`, versus today’s full `stable_sort`. Device full-index `isort` on the 139-bar class: [`173842`](../../captures/session_20260815_173842.log) **28116 µs**; [`194643`](../../captures/session_20260815_194643.log) **32280 µs** at `hist=2614`. That is under 50 ms **at this history size** and still `O(all events)`, not `O(new events)`.

`TickIndex::mergeSortedMidiEventRange` already exists for **MIDI event** merge during materialize slices. It does not merge `tickEvents`. Do not assume it is the tick-index merge.

5.17 froze `tickEvents` as one sorted flat array. An `O(new)` update that never rewrites the existing array is a representation change. 6D may measure full sort / two-way merge first; it must not reopen 5.17 to add a tree unless measurement names that as the stall.

### `byNoteId`

Last-wins `NoteId → {passId, on, off}` (5.18). `pairCapturePassNotes` / `pairCapturePassEventRange` append NOTE_ON rows for **this pass only**, then `sortAndUniqueByNoteId` keep-last on the **entire** vector.

A newly committed pass can introduce or override entries. That matches last-wins **if** the new pass is the latest assignment.

**Undo hole (must measure / pin before coding):** unique keep-last drops earlier assignments for the same `NoteId`. `appendNoteEvents` then refuses a Disabled pass and returns nothing. Cold rebuild from Active passes would restore the previous assignment. Incremental append+keep-last does not, unless `byNoteId` retains prior assignments or unique is rebuilt from remaining Active passes.

Device `nsort` [`173842`](../../captures/session_20260815_173842.log) **10003 µs** for the full unique. Same comment as `tickEvents`: currently cheap at this size, still `O(all noteIds)`.

### `openOnByPitch`

**Do not incrementally maintain.** It is a pairing-time LIFO stack keyed by pitch, not a persistent query index (5.18). `pairCapturePassNotes` allocates it locally for one pass and discards it. A new OverdubPass pairs **that pass’s** events with a fresh stack. Keep that distinction.

### `spanBoundaries` / `spans`

Start and exclusive-end entries per resolved span (5.15). Built from `reconstructDisplayNotes` of the **full resolved event list**, then C-order append + `stable_sort`.

A new overdub is not “append two boundaries per new note.” Overlap/shorten/hide (DEC-031/032) can change existing spans in the affected tick interval. Incremental span work is **re-resolve the affected interval**, not concatenate new notes onto an immutable span list.

### `channelByNoteId`

First NOTE_ON channel per `NoteId` (5.7c keep-first). A new pass that first introduces a `NoteId` appends; a new pass that repeats a `NoteId` must not replace the first channel. Keep-first is the opposite of `byNoteId` keep-last. Do not mix the unique rules.

### `soundingAt` checkpoints

Sparse jump points (`kDeviceCheckpointBarStride` = 8 bars on device). `resolveState` copies `soundingAt[floor(tick / interval)]` then replays `spanBoundaries` to `tick`.

`StateCheckpoints::fillCheckpointRange` currently, for each checkpoint index, scans **all** `spans`. That is `O(checkpoints × spans)`, not `O(affected)`.

Invalidation model 6D must design (not implement yet):

```text
new pass
   │
   ▼
affected tick interval   (from the commit’s events / 6B bars)
   │
   ├── Layer 1 indexes updated
   ├── checkpoints before floor(minTick / interval) remain valid
   └── checkpoints from that boundary onward become dirty
```

Do **not** rebuild all later checkpoints in one STOPPED pass. Do **not** leave a checkpoint whose resolved state changed. Repair is sliced while PLAYING (if scheduling is approved). Bound: dirty checkpoint count × per-checkpoint span apply, not a full `rebuild()`.

`prepareRebuildResolvedEvents` / materialize of **all** active passes is the 30–60 s `mat=` work. Incremental checkpoints that still rematerialize all history fail the scaling test even if `soundingAt` slots are sparse.

---

## Existing API that almost does Layer 1

`TickIndex::commitCapturePass` already:

1. `beginCapturePass`
2. append each chunk
3. `indexCapturePassEventRange` (append `tickEvents`)
4. `sortTickEventEntriesByTick` (**whole** vector)
5. `pairCapturePassNotes` (this pass; ephemeral `openOnByPitch`)
6. `sortAndUniqueByNoteId` (**whole** vector)

The device idle gate does **not** call this on the kept prepared index after `deviceGateComplete`. It cold-builds via `commitLoopPasses` of every pass, then `keepPreparedIndex` + `sDeviceGateFinished`. `maybeQueueContentResolutionDeviceGate` returns immediately once finished. `Loop::commitPendingCapturePass` never calls `commitCapturePass` on that kept index.

6D is not “invent TickIndex commit.” It is: whether that one-pass update (or a merge variant of it) plus Layer 2 invalidation can run as bounded PLAYING slices and leave `preparedWindowReady` true for the new `playbackRevision`.

---

## Scaling criterion

Valuable:

```text
O(new events + affected checkpoints)
```

Not valuable (A/B / D1 again):

```text
O(all historical events)
```

Measure both legs independently:

```text
history events
vs
new overdub events
```

Device cold-build complete-path costs that 6D must **not** repeat per commit ([`173842`](../../captures/session_20260815_173842.log) / [`194643`](../../captures/session_20260815_194643.log)):

| Step | What it scales with today |
|------|---------------------------|
| `idx` / `iapp` | all events (append) |
| `isort` | all `tickEvents` |
| `pair` / `nsort` | this-pass events + all `byNoteId` |
| `mat=` | all history events (merge) |
| `reb` / spans / `soundingAt` | all resolved notes × checkpoints |

Layer 1 without Layer 2 still rematerializes on any `resolveState` consumer. Layer 1 alone is enough to score “next overdub source uses LCR” if 6C consume stays on `resolveWindow`.

---

## Scheduling (design, not code)

Today (`Track::processDeferredIdleMaintenance`):

| State | visualCache slices | LCR gate |
|-------|--------------------|----------|
| STOPPED | yes | yes (one-shot until `deviceGateFinished`) |
| PLAYING | yes | no |
| OVERDUB | no (capture path) | no |

Visual maintenance already runs while PLAYING. LCR does not. That split is intentional (5.1: gate does not run while PLAYING).

Eventual shape if 6D measurement passes:

```text
PLAYING
   │
   ├── MIDI continues
   ├── visual maintenance continues
   └── LCR maintenance gets bounded slices
```

```text
                 LoopPasses
                     │
                     ▼
             committed delta
                     │
                     ▼
          ┌─────────────────────┐
          │ LCR maintenance     │
          │                     │
          │ index delta         │
          │ affected ranges     │
          │ checkpoint repair   │
          └────────────┬────────┘
                       ▼
                  LCR READY
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
     display        playback       overdub
       idle          window         source
```

**Architecture checkpoint (implementation later, not now):**

1. **Ownership?** NO move — `LoopContentResolution` stays the derivation owner; `LoopPasses` stays content. Wiring `commitPendingCapturePass` → LCR maintenance is a new call path on the same owner.
2. **State transitions?** Record/overdub FSM: NO. **Idle-maintenance admission:** YES if LCR slices run during PLAYING. That is a scheduling/admission change, not a silent 6C follow-on. Do not implement PLAYING LCR slices without an explicit design approval (DEC-037 amendment).

6.0 remains: no construct/sort/checkpoint/resolve **on the overdub start/stop call**. Post-commit cooperative slices are a different path. They still must not stall MIDI (`midi_gap` / `clockrate`).

---

## Measurement plan (native first)

No firmware until the native instrument answers the scaling question.

Instrument (extend `test_loop_content_resolution` counters; do not add a second owner):

| Counter | Meaning |
|---------|---------|
| `commit_delta_events` | events in the new pass |
| `history_events` | events already in `tickEvents` before the commit |
| `index_delta_build_us` | append new pass events + pair this pass |
| `index_merge_or_sort_us` | make `tickEvents` ordered |
| `bynoteid_unique_us` | keep-last unique after the new pass |
| `affected_tick_min` / `affected_tick_max` | interval from the new pass |
| `affected_checkpoint_count` | checkpoints from `floor(min/interval)` onward |
| `checkpoint_repair_us` | rebuild only dirty `soundingAt` slots |
| `total_maintenance_us` | sum |
| `worst_slice_us` | if sliced at `kDeviceGateEventsPerSlice` / 50 ms budget |

Compare three native treatments of **Layer 1 only** on the canonical fixture, then on a 139-bar-class fixture if present:

1. Cold `commitLoopPasses` of all passes (today’s device gate).
2. `TickIndex::commitCapturePass` of the new pass only on an already-built index (today’s one-pass API: full sort).
3. Append + two-way merge of already-sorted `tickEvents` with the new pass’s tick-ordered entries (no representation change).

**Pass for Layer 1 investigation:** (2) or (3) tracks `commit_delta_events`, not `history_events`, **or** stays under the 50 ms slice budget with a documented `O(history)` residual that remains under budget at the 139-bar class. If (2) and (3) both track history, Layer 1 fails the scaling test and 6D stops before algorithm design — same failure gate as DEC-037 (do not add another O(history) derived owner).

**Layer 2** is a separate native experiment: given an already-built `StateCheckpoints`, apply one new pass and count dirty checkpoints vs total. If repair still calls `prepareRebuildResolvedEvents` on all history, Layer 2 fails until resolved-event maintenance exists. Do not hide that behind Layer 1 success.

Undo/redo: native fixture that disables the last overdub after incremental `byNoteId` update and compares `appendNoteEvents` / `resolveWindow` to a cold Active-only rebuild. Pin the last-wins hole before any firmware.

Device measurement (only after native Layer 1 pass **and** scheduling approval): same counters on USB serial during PLAYING after overdub stop. Do not start that in this investigation.

---

## Open before coding

1. Is Layer 1 (`tickEvents` + `byNoteId`) sufficient to declare “next overdub can use LCR,” with Layer 2 deferred until a `resolveState` consumer is wired?
2. On undo of a pass that overrode a `NoteId`, must `byNoteId` restore the previous Active assignment, or is visit-time Active skip on `tickEvents` enough because `resolveWindow` does not need `appendNoteEvents` for capture notes?
3. PLAYING LCR slices: same 50 ms idle budget as visual cache, or a tighter cap? (Admission change — needs explicit approval.)
4. Does 6B’s dirty-bar neighborhood equal the LCR affected tick interval, including wrap-crossing notes?

None of these are implementation. Pin them from native fixtures, then a DEC-037 amendment before firmware.

---

## Out of scope

- A and B
- 6C consume-path edits (`establishOverdubSourceView`)
- Flatten `openOnByPitch`; representation B; rewrite `recon`
- `ensure*` rebuild helpers on `startOverdubbing` / `stopOverdubbing` / `handleMidiInput`
- midi_gap investigation ([`192334`](../../captures/session_20260815_192334.log)); 6.3; 6.4
- Deleting `materializeToEventVector` or the 3b `visualCache.notes` copy
- Persisted D3 checkpoints; overlay picker

---

## Docs to update when 6D closes (later)

Native measurement results go in this file. Firmware, if ever approved, is a later stage with an architecture gate in `ARCHITECTURE-REVIEW.md`. Do not check off OpenSpec 6D implementation tasks from this investigation.
