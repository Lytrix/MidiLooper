# 2026-10-27 — Heap admission: low tail resolved by the read-only free-list walk (DEC-169)

Owner-approved slice ("take `!HEAP_STATS`, then Fix 1"). The `!HEAP_STATS` reading at
the failure state settled the diagnosis, and the fix replaces the raw `_sbrk` tail as the
verdict for user-gesture admissions with a read-only free-list walk.

## Device reading that settled it (`!HEAP_STATS`, 2026-10-27, failure state)

```
#HEAP_STATS,v=1,units=bytes,internal_complete=1,arena=318688,used_chunk_bytes=223944,
free_chunk_bytes=94744,largest_chunk=81320,top_chunk=81320,free_blocks=13,heap_tail=36864,...
```

- `heap_tail = 36864` (36 KB) — this is what `getInternalHeapFreeBytes()` (and the gate) reads.
- `largest_chunk = 81320` (79.4 KB) — the largest single free chunk (the top chunk).
- `free_chunk_bytes = 94744` (92.5 KB) across `free_blocks = 13` — free memory in reusable holes below `__brkval`.
- `used_chunk_bytes = 223944` (219 KB) of the 311 KB arena is in-use working sets.

**Verdict: not a leak — the metric is wrong.** The tail reads 36 KB but the allocator can serve a 79.4 KB block; the
overdub start's 48 KB `floor + margin` is available. The memory the owner suspected as "filled and not freed" is mostly
*freed into holes* (`free_chunk_bytes = 92.5 KB`), while `__brkval` stays pinned at the peak because `_sbrk` never
shrinks. The 113 ms `elapsed_us` is dominated by the 8 MB PSRAM scan; the internal walk alone is microseconds at 13 free
blocks (bounded ≤ 4096 chunks).

## Fix (as landed)

- **`MemoryMonitor::largestInternalFreeChunkBytes()`** — walks `__malloc_av_` via the pinned
  `StoppedHeapDiagnostics::scanInternal` (read-only, `FLASHMEM`, no allocation), `arena` computed from `__brkval -
  _heap_start` (never depends on a prior `mallinfo()`). Returns the largest free chunk, or 0 when the walk is incomplete.
- **`admitOperationMemory`** — the tail stays a cheap pre-filter; when it says "low" for a **user-gesture** admission
  (`OverdubStart`/`RecordStart`/`SlotSwitch`), the verdict is `largestInternalFreeChunkBytes() >= floor + margin`.
  `SnapshotReserve` and the point-of-use reserves keep their existing paths (the withdrawn malloc-probe was withdrawn
  precisely because it perturbed the arena those tail-based gates depend on; the walk allocates nothing).

## Verification

- RED fixture `test_tail_low_but_free_list_servable_admits_the_start` (rewrote the defect-pinning fixture
  `test_tail_predicate_falsely_denies_a_start_the_allocator_could_serve`): tail 36 KB + largest 79 KB → admitted;
  largest 40 KB → refused; tail ≥ floor+margin → admitted without the walk; `SnapshotReserve` keeps the tail rule.
- Mutation red-proof: neutralize the free-list consultation → `[FAILED]` at the "must be admitted" assert; restored.
- `test_memory_pressure_policy` 12/12, full native **3,525 / 7 skipped / 3,518 succeeded / 0 failed**,
  `teensy41-capture-serial` **SUCCESS** at RAM1 `variables 96,192` / ITCM `code 390,472 + padding 2,744` (both unchanged;
  the walk is FLASH, +352 B) — no re-base. No commit, no flash.

## Honest limits / follow-ups

- The "used 219 KB" of the 311 KB arena is genuine live working-set memory (view sets, capture stagings, stop
  descriptors, prepared companion history, publication reserves), not content (the census shows `ib=eb=ub=0`; content
  lives in PSRAM). Whether any of it is *retained* beyond its lifetime (the owner's "fills and doesn't get freed"
  instinct) is a separate question — answerable with the per-`_sbrk` call-site accounting (§11's proposed instrument),
  which names the peak-setter. This slice does not add it; the admission is now correct regardless.
- `SnapshotReserve` still uses the tail (36 KB < 40 KB → denied). The device log showed 74 such denials, and the
  deferred-stop finalization needs `SnapshotReserve`; in this run the stop still finalized (the session-exit receipt
  path used a different reserve), but a tail-based `SnapshotReserve` is the next candidate for the same free-list
  treatment if a future capture shows it blocking finalization.
