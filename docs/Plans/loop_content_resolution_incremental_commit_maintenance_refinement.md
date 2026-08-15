# LoopContentResolution — incremental post-commit index (6D / 6D.1 / 6D.2 / 6D.3)

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
| Experiment **6D.2** | Frozen historical `tickEvents` + separate delta `TickEventEntryVec`. Two-source `findRawWindow` without compacting. Native only |
| Experiment **6D.3** | Repeated overdub commits on the 6D.2 split: each pass appends into the same delta vector. History stays frozen. Native only |

**6D.1** measured FAIL for one-vector mutation. **6D.2** PASS: two ordered sources are enough for `findRawWindow`. **6D.3** asks whether that split still holds after N successive overdubs. It is **not** Stage 6C and **not** “LCR is now always live.”

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

6B already does bounded work at commit (`Loop::markAffectedDisplayCacheRanges` from `Track::finalizeCommitSideEffects`). That commit site is a **later firmware candidate**, not 6D.1 / 6D.2. Production architecture stays untouched until repeated-overdub scaling and a production architecture gate.

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
- Production commit, idle gate, overdub start/stop, and `preparedWindowReady` until a production architecture gate
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

---

## 6D.2 — split history + delta (native experiment only)

**Decision boundary:** 6D.2 is the final native test of whether a split historical/delta representation can preserve overdub readiness without mutating the historical index. It is **not** authorization to make LCR incrementally live.

```text
6D.1 question:
Can one sorted vector remain incrementally ordered after Δ?
Answer: NO.

6D.2 question:
Does the query actually require one globally sorted vector?
Answer: NO for `findRawWindow`. Two ordered sources match the compacted oracle; visit counts track the requested window.
```

```text
6D.2 PASS
   ↓
6D.3 repeated overdub commits  ← PASS
   ↓
only then consider production architecture gate

6D.2 FAIL
   ↓
stop incremental overdub LCR
   ↓
keep 3b
```

Option 1 (stop incremental LCR, keep 3b) is the **failure-gate fallback**, not the next step. Option 3 (reopen the 6D.1 < 50 ms samples as a pass) stays dead. A 2.6 ms full-history operation that grows with the loop is not overdub readiness.

### Representation (preserve frozen 5.17)

```text
tickEvents        ← frozen 5.17 historical vector; immutable on commit
TickEventEntryVec ← newly appended pass/delta; not a TickIndex member
```

Do not add a new domain noun. Do not compact the two vectors into one (that is 6D.1). No tree. No `byTick`. No representation B. No firmware.

Commit:

```text
append/copy Δ → sort Δ → publish delta
```

Target commit cost: `O(Δ log Δ)`, not `O(H log H)` and not `O(H + Δ)` from merging.

Query: `findRawWindow` consumes two ordered sources without materializing them back into one.

### Acceptance

```text
fixed Δ
increasing H

commit maintenance → should track Δ
query window        → should NOT track H materially
oracle              → exact/semantic match
```

Windows:

1. window with **no** delta events — if a tiny window walks thousands of historical entries, FAIL
2. window containing delta events
3. window overlapping old + new events

Record:

```text
H       Δ       commit_us    query_us
8192    8       ...          ...
16384   8       ...          ...
32768   8       ...          ...
65536   8       ...          ...
```

and `candidate_history`, `candidate_delta`, `resolution_ops`.

If `commit ≈ Δ` and `query ≈ H`, **stop immediately and take option 1**.

### Native results (2026-08-15)

Instrument: `test_stage6d2_split_query_matches_merged_oracle`, `test_stage6d2_split_history_delta_scales`. Production untouched. `TickIndex` has no delta member. Host native, min of 5 runs, delta **8**.

**Correctness:** three successive overdubs keep `tickEvents` at the prepared size. Two-source `resolveWindow(history, delta)` matches compacted `tickEvents`, cold `commitCapturePass`, and (after the third) the materialize oracle.

**Scaling** (`no_delta` window of 16 ticks; `delta` and `overlap` visit counts identical at every H):

| H | Δ | commit_us | query_us (no_delta) | candidate_history | candidate_delta | resolution_ops |
|---|---|-----------|---------------------|-------------------|-----------------|----------------|
| 8192 | 8 | 0 | 4 | 8 | 0 | 8 |
| 16384 | 8 | 0 | 4 | 8 | 0 | 8 |
| 32768 | 8 | 0 | 4 | 8 | 0 | 8 |
| 65536 | 8 | 0 | 4 | 8 | 0 | 8 |

`delta` window: `candidate_history=0` `candidate_delta=8` `query_us=4`. `overlap` window: `4 + 8` `query_us=6`. `commit_us=0` is below 1 µs timer resolution for an 8-entry sort.

**6D.2 PASS.** Commit tracks Δ. Query visit counts track the requested window, not H. A tiny history-only window does not walk thousands of historical entries.

This is **not** authorization to make LCR incrementally live. Next native step is **6D.3** repeated-overdub commit scaling (delta accumulating). Only then a production architecture gate. Firmware stays frozen. Option 1 is unused. Option 3 stays dead.

---

## 6D.3 — repeated overdub commits (native experiment only)

**Decision boundary:** 6D.3 asks whether the 6D.2 split remains valid when delta accumulates across successive overdubs. It is **not** authorization to make LCR incrementally live and **not** a production architecture gate.

```text
6D.2 question:
Does the query require one globally sorted vector?
Answer: NO for one Δ.

6D.3 question:
After N overdubs, does commit still avoid H, and does query still avoid H and the accumulated delta?
```

Same representation as 6D.2:

```text
tickEvents        ← frozen; size stays H after every commit
TickEventEntryVec ← append this pass, sort this vector only
```

Do not compact into one vector. Do not add a third list. Do not add a `TickIndex` member. No firmware.

### Acceptance

```text
fixed δ, two H sizes, increasing N

history size        → stays H
commit of pass N    → tracks accumulated Δ, not H
no_delta query      → candidate_history=8, candidate_delta=0 at every N and H
all_delta query     → candidate_history=0, candidate_delta=Nδ
oracle              → two-source find matches compacted merge after each commit
```

If commit of pass N tracks H, or a tiny history-only window walks H or the accumulated delta, **FAIL and take option 1**.

### Native results (2026-08-15)

Instrument: `test_stage6d3_repeated_overdub_matches_oracle`, `test_stage6d3_repeated_overdub_scales`. Production untouched. Host native, min of 5 runs, δ **8**.

**Correctness:** eight successive synthetic overdubs keep `tickEvents` at H. After each commit, two-source `findRawWindow` matches compacted merge of history+delta.

**Scaling:**

| H | N | Δ_acc | commit_us | query_no_delta_us | candidate_history | candidate_delta | query_all_delta_us | all_delta candidates |
|---|---|-------|-----------|-------------------|-------------------|-----------------|--------------------|----------------------|
| 8192 | 1 | 8 | 0 | 4 | 8 | 0 | 4 | 0 + 8 |
| 8192 | 4 | 32 | 0 | 4 | 8 | 0 | 15 | 0 + 32 |
| 8192 | 16 | 128 | 1 | 4 | 8 | 0 | 60 | 0 + 128 |
| 32768 | 1 | 8 | 0 | 4 | 8 | 0 | 4 | 0 + 8 |
| 32768 | 4 | 32 | 0 | 4 | 8 | 0 | 15 | 0 + 32 |
| 32768 | 16 | 128 | 1 | 4 | 8 | 0 | 60 | 0 + 128 |

`no_delta` stays 8/0 and `query_us=4` at both H and every N. `all_delta` visit counts equal Nδ, not H. `query_all_delta_us` tracks the requested window (8 → 32 → 128 events), identically at both history sizes. `commit_us` at N=16 is 1 µs at both H.

**6D.3 PASS.** Repeated commits do not mutate history. Commit tracks accumulated Δ, not H. A tiny history-only window does not walk H or the accumulated delta.

This is **not** authorization to make LCR incrementally live. A production architecture gate may now be considered. Firmware stays frozen until that gate. Option 1 is unused. Option 3 stays dead.

---

## Production architecture gate (6.0 + 6D.4 architecture approved 2026-08-15 — no firmware)

**Status:** 6.0 reading and 6D.4 architecture **approved**. Firmware **not** authorized.  
**Preflight:** [`openspec/changes/loop-content-resolution/PREFLIGHT.md`](../../openspec/changes/loop-content-resolution/PREFLIGHT.md)

### Formal triggers

| Trigger | Fired? | Evidence |
|---------|--------|----------|
| New owner | NO | Extend `DeviceGateSession` |
| Ownership transfer | NO | `Loop` still publishes the pass; LCR still owns the prepared index |
| State transition | **YES** | `preparedWindowReady` would become true after a PLAYING overdub commit. Today it is true only after STOPPED `deviceGateComplete` with a matching stamp |
| OpenSpec vs architecture | **PIN** | 6.0 literal text vs DEC-037 commit-site placement |
| Schema / undo / timing model | NO | No SD. Undo stays stamp-mismatch → 3b. Commit-site work is the existing 6B site |

Hard stop until approval.

### Current architecture (proven)

```text
STOPPED idle
  → deviceGateBegin … deviceGateComplete(playbackRevision)
  → prepared TickIndex.tickEvents  (5.17 flat)
  → preparedWindowReady(rev) == true

PLAYING overdub stop
  → Loop::commitPendingCapturePass
       ++playbackRevision
       clearOverdubSourceView
  → preparedWindowReady(new rev) == false   // stamp mismatch
  → device gate does not re-run: STOPPED-only, and deviceGateFinished is one-shot
  → next startOverdubbing
       establishOverdubSourceView
         tryResolvePreparedWindow → miss
         3b visualCache.notes copy
```

Proof:

- `commitPendingCapturePass` increments `playbackRevision` after publishing the `OverdubPass`.
- `preparedWindowReady` requires `deviceGateFinished && preparedIndexKept && preparedPlaybackRevision == playbackRevision`.
- `processDeferredIdleMaintenance` calls `processDeferredContentResolutionDeviceGate` only when not playing, recording, overdubbing, or stopped-recording.
- `maybeQueueContentResolutionDeviceGate` returns if `deviceGateFinished()`.
- `tryResolvePreparedWindow` calls one-vector `resolveWindow(index)` (`index.tickEvents` only).
- 6B already runs in `Track::finalizeCommitSideEffects` on overdub `Committed`.

Native evidence: 6D.1 one-vector mutation **FAIL**. 6D.2/6D.3 split **PASS**.

### 6.0 reading (approval pin)

OpenSpec 6.0: overdub start/stop must not construct, sort, checkpoint, or resolve LCR.

DEC-037: bounded index update belongs at the commit site (with 6B), not on the button that **opens** the source view.

`finalizeCommitSideEffects` runs on overdub **stop**. A delta sort there is a sort on the stop path.

| Reading | Consequence |
|---------|-------------|
| **6.0 = do not build LCR to open the view** | Commit-site publish is allowed. Next `establishOverdubSourceView` only consumes. Matches DEC-037 and 6D. |
| **6.0 = no LCR sort anywhere on start or stop** | Commit-site publish is forbidden. Then only A, B, or keep 3b. A and B are already rejected. |

The gate recommends the first reading and an explicit 6.0 clarification before any firmware.

### Proposed evolution (single preferred path)

If approved, first production slice (**6D.4**):

```text
finalizeCommitSideEffects (overdub Committed)
  │
  ├── 6B mark affected display bars          (already shipped)
  └── if prepared index kept
        beginCapturePass + append events into capturePasses
        append rows into session TickEventEntryVec
        sort that vector only
        restamp preparedPlaybackRevision
        do not write tickEvents

tryResolvePreparedWindow
  └── two-source find when the session vector is non-empty
      else existing one-vector find
```

Constraints for that slice:

- Overdub only. Record still needs a cold prepare.
- Do not update `byNoteId`, checkpoints, `spanBoundaries`, `openOnByPitch`.
- Undo / disable: no incremental work. Stamp mismatch keeps 3b.
- Do not compact history+delta on the overdub path. Optional later: STOPPED idle may run a new full gate and replace both with one 5.17 `tickEvents`.
- Keep the 3b copy. Miss → 3b.
- No new Manager. No `TickIndex` delta member. No new domain type.
- 6.0 unchanged for `startOverdubbing` / `establishOverdubSourceView`: consume only.

Member name for the session `TickEventEntryVec` is an open pin. Candidates (not chosen): keep calling it the delta vector in prose; do not add a new domain noun. Ask before adding the field.

### Rejected here

| Path | Why |
|------|-----|
| A — re-arm STOPPED cold-build | 30–60 s. Does not meet PLAYING overdub-over-overdub |
| B — slice full-history LCR during PLAYING | Another O(H) cache on the perform path |
| Compact into one `tickEvents` at commit | 6D.1 FAIL |
| Make all of LCR incrementally live | Checkpoints / `byNoteId` / undo are later slices |
| Sort/resolve on `startOverdubbing` | Violates 6.0 even under the recommended reading |

### Recommendation

**Approve the 6.0 clarification + 6D.4 slice above.** Do not implement firmware until that approval.

Confidence: high on the native contract (6D.2/6D.3). High that restamp is a state-transition change. High that the commit site is the only placement that is ready at the next button.

### Approval (2026-08-15)

**Accepted:** 6.0 means do not construct/sort/resolve LCR on the overdub button / source-view opening path. Commit-site publish at `finalizeCommitSideEffects` is coherent with that reading.

**Accepted:** 6D.4 architecture as specified below.

**Not accepted:** firmware implementation. The next artifact is the 6D.4 implementation plan, not code.

What passed native is narrower than “LCR is incrementally live”:

> The capture-event index required by the subsequent overdub query can be maintained incrementally using frozen history plus accumulated delta.

### Tightened stamp contract

Today:

```text
preparedWindowReady(revision)
  == deviceGateFinished
  && preparedIndexKept
  && preparedPlaybackRevision == playbackRevision
```

After 6D.4, a PLAYING overdub commit may establish that condition **without `deviceGateComplete()` running again**.

> A prepared index may become valid through an incremental commit-site update, not exclusively through the STOPPED device-gate completion path.

`deviceGateFinished` must not be treated as synonymous with “the prepared index is currently valid.” Do not “fix” a stamp miss by re-running the STOPPED gate during PLAYING.

---

## 6D.4 — incremental overdub publish

**Status:** Native + commit-site call landed 2026-08-15. Not “LCR is incrementally live.”  
**Session field:** `DeviceGateSession::delta` — existing 6D.2/6D.3 word; `TickEventEntryVec`; not a `TickIndex` member and not a new type.  
**Does not authorize:** making all of LCR incrementally live; A; B; undo/disable incrementalization; compacting `tickEvents` at commit

### Architecture gate (6D.4)

| Question | Answer |
|----------|--------|
| **Owner module** | `LoopContentResolution` / `DeviceGateSession` owns prepared `tickEvents`, the session delta `TickEventEntryVec`, and the stamp. `Track::finalizeCommitSideEffects` calls publish after an overdub `Committed`. Consume stays `tryResolvePreparedWindow` |
| **Primary invariant** | After a committed `OverdubPass`, if a prepared index already exists, the overdub-query index is updated by appending/sorting delta only and restamping. Next `establishOverdubSourceView` consumes. Miss → 3b |
| **Ownership change?** | NO |
| **State transition change?** | YES — approved: prepared validity may come from commit-site restamp, not only `deviceGateComplete` |
| **Behavior-preserving?** | NO for the prepared-window stamp after overdub commit. YES for overdub FSM, 6.0 consume-on-button, and 3b fallback |
| **Reuse** | YES — existing 6B commit site + two-source `findRawWindowFromTickEvents` |
| **Phase scope** | `publishPreparedOverdubPass` + two-source `tryResolvePreparedWindow` + one call from `finalizeCommitSideEffects`. Native `test_stage6d4_publish_restamps_without_device_gate_complete`. No `startOverdubbing` edits |

### Approved 6.0 reading

```text
overdub stop / commit
    │
    ├─ publish OverdubPass
    ├─ existing 6B display invalidation
    │
    └─ if prepared LCR exists:
         append new pass to delta
         sort delta only
         restamp prepared revision

next overdub button
    │
    └─ consume prepared state
         └─ two-source resolve
              └─ fallback to 3b if unavailable
```

`startOverdubbing` / `establishOverdubSourceView` still must not construct, sort, checkpoint, or resolve in order to open the view.

### Scope (exact)

- Overdub commits only
- Prepared index must already exist
- Append/sort **delta only**
- No mutation of historical `tickEvents`
- Restamp `preparedPlaybackRevision` to the post-commit `playbackRevision`
- Two-source `findRawWindow` when the session delta vector is non-empty
- 3b fallback remains
- No `byNoteId` / checkpoints / undo / disable incrementalization
- No changes to `startOverdubbing`
- No new manager or domain type
- Session field is `DeviceGateSession::delta` (`TickEventEntryVec`)

### Files (when implementation is requested)

- `include/LoopContentResolution.h` / `src/LoopContentResolution.cpp` — publish + two-source `tryResolvePreparedWindow` + stamp contract
- `src/Track/TrackCaptureStopCommit.cpp` — one call at the existing 6B overdub `Committed` site
- `test/test_loop_content_resolution/test_loop_content_resolution.cpp` — publish + restamp + consume; miss when unprepared; history size frozen across N commits
- OpenSpec 6.0 clarification + 6D.4 task; DEC-037 amendment already started above

### Native tests (when implementation is requested)

1. Prepared index + one overdub publish → `preparedWindowReady(newRevision)` true without `deviceGateComplete`.
2. `tryResolvePreparedWindow` matches compacted-merge oracle; `tickEvents` size unchanged.
3. N successive publishes: same 6D.3 visit-count contract (`no_delta` stays window-sized).
4. No prepared index → publish is a no-op; `tryResolvePreparedWindow` false.
5. Undo-shaped `++playbackRevision` without publish → miss → caller keeps 3b.

Device HITL only after native PASS and an explicit upload request.

### Out of scope

- Record-pass incremental prepare
- Folding delta into `tickEvents` on STOPPED idle (later, optional)
- midi_gap, 6.3, 6.4
- Deleting `materializeToEventVector` or the 3b copy
- 6E overdub `resolveState` consume / wrap-commit / session undo — sibling [`loop_content_resolution_overdub_state_evaluation_refinement.md`](loop_content_resolution_overdub_state_evaluation_refinement.md)

### Native + firmware result (2026-08-15)

`publishPreparedOverdubPass` appends into `DeviceGateSession::delta`, sorts that vector only, restamps `preparedPlaybackRevision`. `tryResolvePreparedWindow` uses two-source find when `delta` is non-empty. `Track::finalizeCommitSideEffects` calls publish after a committed `OverdubPass`.

`test_stage6d4_publish_restamps_without_device_gate_complete`: three publishes after a partial prepare; `preparedWindowReady` matches the new revision without `deviceGateComplete`; `eventsInHistory` stays the frozen `tickEvents` size; window matches the materialize oracle; unprepared publish is a no-op; stamp+1 without publish misses.

`pio test -e native` and `teensy41-capture-serial` build succeeded. Not all of LCR is incrementally live.

### Device [`205928`](../../captures/session_20260815_205928.log) — 6D.4 restamp PASS

STOPPED idle completed on the 4-bar loop: `DIAG,lcr,mat=0,...,hist=91,walk=0` then `DIAG,lcr,6a,...,match=0`. No later `DIAG,lcr,phase` / `deviceGateComplete`.

PLAYING overdub stop at 115.796 s: `VCACHE,stale_range` `dcnt=4`, `ODUB,stop,seal,...,published`, `ST,Track,OVERDUBBING,PLAYING`. Next overdub at 117.269 s (still PLAYING, no STOPPED gate):

`DIAG,lcr,6c,win=23723,proj=13466,tot=37189,ev=363,notes=194` then `ODUB,stage,begin_capture,37747`.

`6c` is emitted only from `tryResolvePreparedWindow` success. Overdub commit increments `playbackRevision`. Without publish restamp, `preparedWindowReady` is false and `6c` cannot appear. No idle gate ran between `mat=` and this `6c`. Therefore the PLAYING commit restamped the prepared index.

| Check | Result |
|-------|--------|
| Prepared index existed | `mat=` `hist=91` before PLAYING |
| No second `deviceGateComplete` | no `lcr,phase` after `mat=` |
| Next overdub consumed LCR | `DIAG,lcr,6c` present |
| 6D.4 restamp | **PASS** |

`begin_capture` **37747 µs** is 6C consume cost (`win` + `reconstructDisplayNotes`), not the 6D.2 `findRawWindow` 4 µs. It is slower than 3b **2214 µs**. That is not a 6D.4 fail. Do not start midi_gap / 6.3 from this capture.

First post-`mat=` overdub (87.628 s, `events=188`) has no `ODUB,stage` / `6c` `#CAP` lines; `RING,overflow` at that stop. Not scored as a miss.

Track 0 overdubs before `mat=` (`begin_capture` 7111 / 8185 / 10228 µs) had no prepared index. Expected 3b. 6B `stale_range` still fired on those stops (`dcnt` 8 / 7).

### Device [`210508`](../../captures/session_20260815_210508.log) — same boot; restamp holds; third consume 312 ms

Continuation of [`205928`](../../captures/session_20260815_205928.log) (no `BOOT`; micros continue; no `lcr,phase` / `mat=`). Two STOPPED undos at 340.053 / 341.339 (`kind=1`, `Overdub undone`) before the first overdub.

| Overdub | Evidence | Result |
|---------|----------|--------|
| 352.581 after undo | no `6c`; `begin_capture,120` | stamp miss → 3b. Expected (undo `++playbackRevision` without publish) |
| 363.558 after PLAYING publish | `6c,win=14482,proj=16019,tot=30501,ev=427,notes=227` → `begin_capture,31128` | **6D.4 restamp PASS** |
| 374.884 after next PLAYING publish | `6c,win=298215,proj=13546,tot=311761,ev=289,notes=144` → `begin_capture,312636` | restamp still hits; **6C consume 312 ms** |

`win=` is the full two-source `resolveWindow` timer in `tryResolvePreparedWindow` (find + sort + unique + filter + sort), not the 6D.2 `findRawWindow` 4 µs. `proj=` is `reconstructDisplayNotes`. No second `deviceGateComplete`. 6B `stale_range` `dcnt=4` on both scored stops. Do not start midi_gap / 6.3 as a separate investigation; the 312 ms `6c` is the consume cost.
