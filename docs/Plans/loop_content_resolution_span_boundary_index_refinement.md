# Loop content resolution — span-boundary index (5.15 design)

**Status:** **5.15 complete** 2026-08-15 — A settled on device [`151450`](../captures/session_20260815_151450.log). No A2. No B. Successor: [`loop_content_resolution_device_phase_budget_refinement.md`](loop_content_resolution_device_phase_budget_refinement.md) (5.16).
**Change:** `openspec/changes/loop-content-resolution/` (DEC-037 Stage 9)  
**Does not start:** 5.1 / 5.2 re-run, Stage 6 production swap, batch-size 1 or 4, deleting `materializeToEventVector`

**Boundary:** trust `resolveState` / `resolveWindow` answers and `walk=0` after the index exists ([`143009`](../captures/session_20260815_143009.log) `st=16384` `rep=350` `hist=2394` `walk=0`). Investigate **how the lookup is stored and built**, not whether indexed find is the right query model.

---

## Goal

Determine the minimum representation that satisfies the current `startsByTick` **query contract** without per-note PSRAM associative-container insertion.

Not the goal: a faster `emplace()` into the same `std::multimap`.

---

## Contract (from code)

`StateCheckpoints::startsByTick` is **not** “tick → notes that start here.”

`appendSpansFromNotes` inserts **two** keys per span:

```text
startsByTick.emplace(span.startTick, spanIndex);
startsByTick.emplace(span.endTick, spanIndex);
```

`StateCheckpoints::resolveState` then does a range walk and applies **on** or **off** by comparing the key to the span:

```text
lower_bound(replayStart + 1) … lower_bound(queryTick + 1)
  key == startTick → upsertPresentNote
  key == endTick   → erasePresentNote
```

So the contract is:

```text
tick range → span-boundary events (start and exclusive-end) in tick order
```

That is a sorted sequence of `(tick, spanIndex)` with two entries per span. An associative map is more general than the query.

`resolveWindow` does **not** read `startsByTick`. It uses a **second** PSRAM `std::multimap`: `TickIndex::byTick` (`visitTickRange` / `findRawWindow`). IndexCommit’s 50–83 ms slices in [`143009`](../captures/session_20260815_143009.log) are that map, not `startsByTick`.

`walk=0` on the complete `DIAG,lcr` line is `passChunkListsWalked` from `resolveState` after the index exists. Preserve that.

`fillCheckpointRange` does **not** use `startsByTick`. It walks every `spans` row per checkpoint (`notePresentAt`). That is the `ckpt` phase (not the 5.7 `spans` stall).

---

## Ordering (from code)

`rebuildNotes` is **not** ordered by `startTick`.

`dedupeProjectedDisplayNotes` ranks by `(note, startTick, endTick)`, uniques, then `qsort` by **original projection index** (first-seen order). `appendSpansFromNotes` walks that list.

Canonical spans are also event-walk order, not global `startTick` order.

Therefore a flat index cannot be “just append during the existing span pass” and stay sorted. Do not rematerialize.

**Equal-tick order (required before A is equivalent to C):**

`std::multimap` keeps equivalent keys in **insertion order**. C inserts `startTick` then `endTick` per span, spans in `rebuildNotes` order.

A MUST append in that same sequence, then **`stable_sort` by `tick` only**. Do not sort by `(tick, spanIndex)` until a native test proves that matches C — it can reorder two boundaries that share a tick (one span ends where another starts).

If `startTick == endTick` on one span, C’s apply runs **both** `upsertPresentNote` and `erasePresentNote` on that entry (`if` / `if`, not `else if`). A must keep two entries and the same apply.

Do not add another `materializeToEventVector` / reconstruct to get tick order.

---

## Two PSRAM multimaps (do not collapse)

| Structure | Owner | Build site | Query | [`143009`](../captures/session_20260815_143009.log) |
|-----------|--------|------------|-------|-----|
| `TickIndex::byTick` | `TickIndex` | IndexCommit `indexCapturePassEventRange` — one `emplace` per **event** | `resolveWindow` | `idx` / `pair` 50–83 ms; `DFRAME` 2–3 s |
| `startsByTick` | `StateCheckpoints` | `appendSpansFromNotes` — two `emplace` per **span** | `resolveState` tail | `spans` 224–413 ms; `DFRAME` 5.8–13.1 s |

5.15 measures a replacement for **`startsByTick` first** (the 5.7 killer). The same contiguous pattern may later apply to `byTick`; that is a follow-up, not this task’s swap.

---

## RAM (what to measure — not guessed node sizes)

Each `startsByTick.emplace` allocates a `std::multimap` tree node through `ExternalMemoryFirstAllocator` (PSRAM). [`121702`](../captures/session_20260815_121702.log) already showed **one** such insert at 50–75 ms. [`115242`](../captures/session_20260815_115242.log) CrashReport was `_M_emplace_equal` in PSRAM.

5.15b must record, for the canonical fixture (device 139-bar class later, after a winner):

| Counter | Why |
|---------|-----|
| **append** µs | sequential writes of `2 × N` entries |
| **sort** µs | `stable_sort` by tick (A1: in place on the same vector) |
| **index total** | append + sort |
| **C emplace** µs | same spans, map-only rebuild |
| **resolve** µs | `resolveState` A vs C at the Stage 7 tick set |
| entry count | `2 × spanCount` |
| `walk` / `eventsReplayed` | must match C; `walk` stays 0 |

Do **not** build A2 (sort in RAM1/RAM2, commit to PSRAM) unless **sort** is a material share of A’s total. 5.15b distinguishes representation cost from sorting cost first.

A contiguous `(tick, spanIndex)` vector of `2 × N` entries removes per-insert node allocation. Exact bytes are a measurement, not a design assumption.

---

## Three representations only

| Id | Representation | Build | Query |
|----|----------------|-------|-------|
| **C** | Current `startsByTick` `std::multimap` | 2 PSRAM `emplace` / span | `lower_bound` + scan |
| **A** | Tick-ordered flat `(tick, spanIndex)[]` | sequential append + one sort (or sort-then-append) | binary search + scan |
| **B** | A plus a coarse bucket/offset table (`tick / bucketSize` → first entry) | sequential after A is sorted | bucket + scan |

Bars are an **implementation detail** of B’s bucket size if we pick `TICKS_PER_BAR`. They are not a new resolver abstraction. Do not make `resolveState` “per bar.”

Grouped “one record per coincident start tick” is a compression of A, not a fourth representation. Measure coincident-tick cardinality on the fixture before adding a group type.

**Naming:** do not add a new musical noun. If A ships, the type stays a private `StateCheckpoints` entry (working name in code: span-boundary entry). `startsByTick` is already a misnomer (it stores ends). Rename only if the representation swap lands.

---

## Success criteria (5.15)

1. Same `resolveState` answers as C on the canonical fixture (and Stage 7/8 loop-switch). Equal-tick order proven, not assumed.
2. `walk` remains 0.
3. Find does not walk pass lists.
4. **Index construction performs no per-entry dynamic allocation in the steady state** (`N` boundaries → `O(N)` storage; no `N` allocator/tree operations).
5. No individual boundary insertion may exceed the device idle slice budget; total index construction is measured as **append + sort**, not allocator/tree mutation. End-to-end latency stays 5.1’s gate.
6. RAM lower or no worse than C.

Then implement **only the winner**. Measure **A vs C only**. Investigate B only if A’s **resolve** cost requires it. `TickIndex::byTick` stays out of this change. Do not reopen the query model (`walk=0` already earned it).

---

## Dependency (unchanged)

```text
startsByTick representation
        ↓
5.7 cold-build latency
        ↓
5.1 device gate
        ↓
5.2 re-run
        ↓
Stage 9 complete
        ↓
ONLY THEN Stage 6
```

Do not wire `resolveWindow` onto playback because the query is already fast.

---

## 5.15b native result (2026-08-15)

Canonical fixture: 47 spans, 94 boundary entries, 1 equal-tick pair. Equivalence vs C and vs materialize oracle at Stage 7 ticks. `walk=0`. Equal-tick same-`noteId` join (end then start at 100) matches C: sounding is the starting span only.

Host malloc (not PSRAM):

| Counter | µs |
|---------|----|
| C emplace | 29 |
| A append | 1 |
| A sort | 4 |
| A index total | 5 |
| C resolve (5 ticks) | 5 |
| A resolve (5 ticks) | 3 |

A resolve is not worse. Sort is most of A’s host total and still 4 µs — **do not build A2**. **Do not build B**. Pick **A** for 5.15c because the device 5.7 stall is C’s per-entry PSRAM `emplace`, and A meets criteria 1–4 on native.

These host numbers do **not** prove device append+sort time. 5.15c owns the device measurement (append / sort / total) after the swap.

## 5.15c shipped (2026-08-15)

`StateCheckpoints::spanBoundaries` is the production index. `startsByTick` is removed.

- `appendSpansFromNotes` appends start then exclusive-end (C order). No per-entry map allocation.
- One idle slice after the last append runs `stable_sort` by tick (`DIAG,lcr,phase,sort`).
- `resolveState` reads the flat list.
- Complete line: `app=` / `sort=` (append µs and sort µs).
- `TickIndex::byTick` unchanged.

Device 5.7 remasure [`151450`](../captures/session_20260815_151450.log): **append+sort beats the map.**

| Counter | [`143009`](../captures/session_20260815_143009.log) C | [`151450`](../captures/session_20260815_151450.log) A |
|---------|------|------|
| complete `reb` | 104.36 s | **13.85 s** |
| `app` | (map emplace in `spans`) | **5.49 s** |
| `sort` | — | **10.2 ms** |
| `st` | 16.4 ms | 13.0 ms |
| `walk` | 0 | 0 |
| `spans` wall | ~108 s | **16.5 s** |
| `spans` `loop_rem` | 206–412 ms / slice | **none** |
| `spans` `DFRAME` gap | 8.9–13.1 s | 1.4–2.2 s |

**Do not build A2** (sort is 10 ms). **Do not build B** (`resolveState` is 13 ms). `byTick` `idx`/`pair` unchanged (50–83 ms). `prep` still 182 ms.

## 5.15 closed

A wins. A2/B unjustified. `walk=0` held. Resolver query model stays. Remaining 5.7 work is **phase budget**, not this index — [`loop_content_resolution_device_phase_budget_refinement.md`](loop_content_resolution_device_phase_budget_refinement.md).
