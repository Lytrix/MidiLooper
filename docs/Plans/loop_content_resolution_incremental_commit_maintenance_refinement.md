# LoopContentResolution — incremental post-commit index (6D / 6D.1)

**Status:** Active — design/measurement investigation. No firmware.  
**Date:** 2026-08-15  
**Kind:** refinement (investigation)  
**Decision:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype)  
**Parent:** [`loop_event_sourced_resolution_architecture.md`](loop_event_sourced_resolution_architecture.md)  
**Handoff:** [`loop_content_resolution_stage9_handoff.md`](loop_content_resolution_stage9_handoff.md)  
**OpenSpec:** `openspec/changes/loop-content-resolution/`  
**Does not authorize:** firmware; production commit/idle/overdub paths; A; B; making all of LCR incrementally live; 6C consume-path edits; flattening `openOnByPitch`; representation B; deleting `materializeToEventVector`

---

## Identity

| Name | What it is |
|------|------------|
| Stage **6C** | Consume-when-ready: `tryResolvePreparedWindow` → `overdubSourceView` when the stamp already matches |
| Investigation **6D** | After a committed content mutation, keep the index required for a **subsequent overdub query** incrementally current, within a bounded maintenance budget |
| Experiment **6D.1** | One committed `OverdubPass` on an already-prepared index. No edits, undo, disabled passes, checkpoints, or other LCR indexes |

**6D.1** is the next experiment. It is **not** Stage 6C and **not** “LCR is now always live.”

Naming: 6C stays consume. Calling the experiment 6C-1 would collide with that consume path. 6D.1 is the first slice of 6D.

6D is DEC-037 capability **B** (incremental effective content) for the **overdub query**, not capability **D** (checkpoints) in this slice.

---

## Narrow invariant (6D)

> After a committed content mutation, the index required for a subsequent overdub query is updated incrementally within a bounded maintenance budget.

Not:

> All of LoopContentResolution stays incrementally ready.

Keep the owner as it is:

```text
LoopContentResolution
    ├── immutable/history-derived indexes
    ├── checkpoints
    └── resolution
```

6D.1 only asks whether the **overdub-query index** can stay current. Checkpoints, `spanBoundaries`, `byNoteId`, and `resolveState` stay cold-built until a later slice names them.

---

## Why C, and why not A or B

```text
A  re-run the giant build later          → 30–60 s STOPPED
B  continuously build the giant structure while PLAYING
                                         → 30–60 s PLAYING; button can still arrive first
C  maintain the relevant index as history changes
                                         → already ready at the next button
```

Only C attacks the scheduling problem. Evidence: [`loop_content_resolution_stage9_handoff.md`](loop_content_resolution_stage9_handoff.md) § Why LCR is not ready.

Target shape:

```text
PLAYING
   │ overdub happens
   ▼
commit new pass
   │
   ├── bounded index update
   ▼
LCR remains usable for the overdub query
   │
   ▼
next overdub
   └── consume prepared state
```

Not:

```text
commit → invalidate → 30–60 s STOPPED rebuild → prepared
```

---

## 6.0 stays

Overdub **start/stop must not** construct, sort, checkpoint, or **resolve** LCR in order to open the source view.

```text
PLAYING
   ↓
button
   ↓
consume already-prepared state
   ↓
begin_capture
```

6B already does bounded work at commit (`Loop::markAffectedDisplayCacheRanges` from `Track::finalizeCommitSideEffects`). That commit site is a **later firmware candidate**, not 6D.1. Production architecture stays untouched until native 6D.1 establishes the mutation contract.

Do not make 6D a “LCR is now always live” change.

---

## `< 3 ms` is consume of prepared state

[`045556`](../../captures/session_20260814_045556.log) `begin_capture` **2214 µs** is a copy of an already-authoritative `visualCache.notes`. It does **not** prove that switching consumers to LCR is 2.2 ms.

[`180624`](../../captures/session_20260815_180624.log) 3b restore is **10050 µs** — under the 50 ms hard gate, not the 2.2 ms copy.

LCR migration target:

```text
prepare before button  +  constant/bounded consume
        ↓
begin_capture ≈ 2–3 ms
```

Not:

```text
button → build LCR → resolve → begin_capture
```

The second path violates 6.0. Do not promise that “switching everything to LCR” recovers 2214 µs.

---

## Mutation contract first (do not start by rewriting `TickIndex`)

5.17 proved the **flat `tickEvents` representation** is the right query shape. It did **not** prove that incrementally mutating that array is the right operation. Pin the contract before the algorithm:

```text
commitCapturePass / commit edit
        ↓
what new/changed history becomes visible?
        ↓
which LCR indexes are affected?
        ↓
can those changes be appended/merged without rebuilding?
```

### What a normal overdub stop publishes

From `Loop::commitPendingCapturePass` and `Track::finalizeCommitSideEffects`:

1. New `OverdubPass` (id, `mergeSequence`, `committedChunkIds`) on `LoopPasses`.
2. `++playbackRevision` — this is why `preparedWindowReady` becomes false today.
3. Optional companion `EditPass` rows from `Loop::sealPendingNoteChangesToEditPasses` (Shorten/Hide). Those rows target `PendingNoteChange.noteId` taken from **existing** `overdubSourceViewNotes_` (`accumulatePendingNoteChangesFromSourceNotes` skips the incoming/`causingId` note). They are **not** the newly captured pass’s notes.
4. 6B dirty-bar mark. No LCR index update.

Undo/disable/NOTE_EDIT close are different mutations. **Not 6D.1.**

### What the next overdub query reads

`Loop::establishOverdubSourceView` → `LoopContentResolution::tryResolvePreparedWindow`:

- Requires `preparedWindowReady(playbackRevision)` (stamp match + kept `TickIndex`).
- Calls `resolveWindow(prepared index, **live** `passes.editPasses`, window, …)`.
- `resolveWindow` uses `TickIndex::findRawWindow` (`tickEvents` → `capturePasses` events) then `appendNoteEvents` via `byNoteId` for active edit rows, then `applyNoteEditPassSequence`.

Edit rows are applied at **query time** from `LoopPasses`. They are not a frozen prepared edit index. Checkpoints are not on this path.

For 6D.1, “index required for a subsequent overdub query” is:

```text
capturePasses[new]  +  tickEvents including the new pass  +  stamp = new playbackRevision
```

`byNoteId` is needed only for edit-row `targetNoteId`s. Companion overlap edits target **prior** notes, which the prepared `byNoteId` already holds. 6D.1 does **not** update `byNoteId`.

---

## 6D.1 — incremental post-commit index (overdub pass only)

Prove only:

1. Start with a fully prepared `TickIndex` (same as a completed device gate: `commitLoopPasses` of existing history, stamp matches).
2. Commit one `OverdubPass` onto `LoopPasses` (native stand-in for `commitPendingCapturePass`).
3. Update the affected derived index **incrementally**. Do **not** rebuild the complete index (`commitLoopPasses` of all history is the fail oracle, not the treatment).
4. In the **native fixture**, restamp the prepared revision so a `tryResolvePreparedWindow`-shaped check succeeds. Do not restamp production `preparedWindowReady` / `sDeviceGateSession`.
5. `resolveWindow` on the updated index + live `editPasses` matches the materialize+reconstruct oracle for the overdub source window (old content + new pass).
6. Measure `index_append_us`, `index_order_us`, and `total_maintenance_us` against **both** `history_events` and `commit_delta_events` (see Native instrument).
7. Repeat for several successive overdub commits on the same prepared index, and at **at least two history sizes** with the same `commit_delta_events`.
8. Next `tryResolvePreparedWindow` / `resolveWindow` succeeds **without** a STOPPED cold rebuild.

Simplest candidate for step 3 (measure, do not assume):

```text
existing tickEvents
+
new pass events
→ append into capturePasses
→ ordered merge into tickEvents
```

`TickIndex::indexCapturePassEventRange` already appends. `TickIndex::commitCapturePass` then **full-sorts** `tickEvents` and also pairs/`byNoteId`-uniques — that extra work is **out of 6D.1**. Compare:

| Treatment | What it does |
|-----------|----------------|
| Fail oracle | Cold `commitLoopPasses` of all passes |
| Existing one-pass API | `commitCapturePass` (full `tickEvents` sort + pair + `byNoteId` unique) — **too much** for 6D.1 if it updates `byNoteId` |
| 6D.1 treatment | `beginCapturePass` + chunk append + `indexCapturePassEventRange` + order `tickEvents` only (merge or sort of that vector) + restamp |

Device full-index `isort`: [`173842`](../../captures/session_20260815_173842.log) **28116 µs**; [`194643`](../../captures/session_20260815_194643.log) **32280 µs** at `hist=2614`. Those samples are under 50 ms **and still O(all tickEvents)**. Under-50-ms does **not** pass 6D.1. Full sort of `tickEvents` **fails** if `index_order_us` grows with `history_events` at fixed `commit_delta_events`. Do not reopen 5.17 to a tree unless this measurement names the stall.

`TickIndex::mergeSortedMidiEventRange` merges **MIDI events**, not `tickEvents`. Do not reuse it by accident.

### 6D.1 does not solve

- Edits as an incremental index problem (live `editPasses` at query time stay as they are)
- Undo / disable / enable
- Checkpoints / `spanBoundaries` / `soundingAt` / `channelByNoteId`
- `byNoteId` last-wins unique
- `openOnByPitch` (pairing-time LIFO; not a query index)
- Display 6A / playback 6.3
- PLAYING admission for a 30–60 s build (that is B, rejected)

---

## Native instrument (6D.1)

Extend `test_loop_content_resolution` only. Do not add a second owner. **Do not edit production** (`Loop::commitPendingCapturePass`, `Track::finalizeCommitSideEffects`, `preparedWindowReady`, device gate, `establishOverdubSourceView`) until this native experiment establishes the mutation contract.

| Counter | Meaning |
|---------|---------|
| `history_events` | `tickEvents` size before the commit |
| `commit_delta_events` | events in the new pass |
| `index_append_us` | `beginCapturePass` + chunks + `indexCapturePassEventRange` |
| `index_order_us` | merge or sort of `tickEvents` |
| `total_maintenance_us` | sum |
| `order_per_history` | `index_order_us / history_events` |
| `order_per_delta` | `index_order_us / commit_delta_events` |
| oracle match | `resolveWindow` vs materialize+reconstruct on the overdub source window |

Report every sample against **both** `history_events` and `commit_delta_events`. Use at least two history sizes with the same `commit_delta_events` so scaling is visible, not inferred from a single `hist=2614` point.

### Pass / fail (scaling)

| Result | When |
|--------|------|
| **PASS** | `index_order_us` (and `total_maintenance_us`) track `commit_delta_events`: they stay flat or grow with the delta when `history_events` changes and the delta does not |
| **FAIL** | `index_order_us` grows with `history_events` at fixed `commit_delta_events` |

A full `stable_sort` of `tickEvents` is a **legitimate FAIL** under that second row even when every sample is **< 50 ms**. The 50 ms gate is MIDI/idle latency, not the 6D.1 scaling contract. Device `isort` 28–32 ms at `hist=2614` is evidence that full sort can be “cheap enough” today and still scale with history.

If append+merge also tracks `history_events`, 6D.1 fails the same way — DEC-037 failure gate (do not add another O(history) owner).

Repeat N overdubs. After each: stamp matches, window matches oracle, no `commitLoopPasses` of history.

Production architecture stays as it is until this native result exists. No firmware from 6D.1 until then.

---

## Later 6D (not 6D.1)

These stay documented so they are not forgotten. They are **not** this experiment.

### `byNoteId`

Last-wins unique. Incremental append+keep-last drops earlier assignments; undo of a later pass leaves `appendNoteEvents` empty. Pin in a later slice.

### Checkpoints

```text
affected tick interval
  → checkpoints before floor(minTick / interval) remain valid
  → from that boundary onward become dirty
```

`fillCheckpointRange` today scans all spans × all checkpoints. Incremental checkpoints that still rematerialize all history (`mat=` 30–60 s) fail. Capability D, not 6D.1.

### Disabled passes

`collectTickRangeRefs` already skips non-Active at visit time. Rows can stay in `tickEvents`. Still not 6D.1.

---

## Out of scope

- A and B
- 6C consume-path edits (`establishOverdubSourceView`)
- Production commit, idle gate, overdub start/stop, and `preparedWindowReady` until native 6D.1 closes the mutation contract
- Flatten `openOnByPitch`; representation B; rewrite `recon`
- `ensure*` rebuild helpers on `startOverdubbing` / `stopOverdubbing` / `handleMidiInput`
- midi_gap ([`192334`](../../captures/session_20260815_192334.log)); 6.3; 6.4
- Deleting `materializeToEventVector` or the 3b copy

---

## Native results (2026-08-15)

Instrument: `test_stage6d1_overdub_pass_merge_matches_oracle`, `test_stage6d1_tick_events_order_scales_with_history`. Production untouched.

**Correctness:** append + ordered merge of one `OverdubPass` into an already-sorted `tickEvents` matches cold `commitCapturePass` of the same prefix and, after three successive overdubs, the materialize `resolveWindow` oracle. Canonical history ~88–92 events, delta 2.

**Scaling** (host native, min of 5 runs, delta **128** events):

| Treatment | `history_events` | `index_order_us` | vs 4× history |
|-----------|------------------|------------------|---------------|
| full `stable_sort` | 8192 | 602 | |
| full `stable_sort` | 32768 | 2624 | **4.36×** |
| ordered merge | 8192 | 46 | |
| ordered merge | 32768 | 174 | **3.78×** |

Both grow with `history_events` at fixed `commit_delta_events`. Both samples are **< 50 ms**. Full sort is a **6D.1 FAIL**. Ordered merge of the frozen 5.17 flat array is also a **6D.1 FAIL**: `std::merge` rewrites the whole `tickEvents` vector (`O(history + delta)`).

**Mutation contract:** not established for flat `tickEvents`. An `O(commit_delta_events)` update would require a representation that does not rewrite historical entries. Do not reopen 5.17 / add a tree in this slice. Production stays frozen.

Do not check off 6D as “all of LCR incremental.” Firmware needs an architecture gate after a treatment that passes the scaling contract.
