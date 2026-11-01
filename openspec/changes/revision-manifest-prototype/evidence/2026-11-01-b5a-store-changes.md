# B.5a — history-closed compaction, heap-merge compose, paged directories, shared pool

Date: 2026-11-01. Native: `test_revision_manifest_store`, `test_revision_store_differential`,
`test_revision_store_capacity`. Device: probe image HEX `8f70c93a7b59672d…`, boot verified
(`#CAP,1111002,DIAG,boot_reset,crash=0`, `BOOT,load,ok`), RAM1 identical to the default capture image.
Raw rows: [`2026-11-01-b5a-revstore-probe-raw.txt`](2026-11-01-b5a-revstore-probe-raw.txt).

## Changes

1. **History-closed compaction (default).** `beginCompaction(first, count, scope = HistoryClosedOnly)` refuses
   (`NotHistoryClosed`) a range that some history entry holds only in part. On publish every entry that holds the
   range is rewritten, so the originals are freed at once. `largestClosedRangeFrom(first)` finds a candidate.
   `CompactionScope::AllowCopies` remains for tests and off-line use.
2. **Heap-merge composition with cached keys.** Segments are merged through a binary heap keyed by the cached DEC-244
   key of each cursor's current row (`(tick << 8) | (class << 6) | run`, then `seq`), so comparisons never read pages.
   Only runs that carry hides are consulted for hiding. The merge state is store-owned scratch (main loop only), not
   stack: about 6 KB at 32 runs × 8 segments. The same merge drives compaction steps.
3. **Paged run directories.** The first 4 data-page ids are inline; further ids live in directory pages taken from the
   pool (512 ids per 1 KiB page). The run descriptor is now **76 B** (was > 2 KiB with a fixed 1,024-entry directory).
4. **Shared page pool.** `PagePool` is its own object; `StoreCore` (one slot: runs, manifests, history) takes a pool
   reference, so all slots draw from one pool. `Store` is the single-slot convenience that owns its pool.

## Capacity (native, same session as B.5: dense 64-bar take, 30 overdubs with hides, undo branches, pinned reader)

| Policy | History | Peak pages | Peak / content |
|---|---|---|---|
| none | 16 | 729 | 1.12 |
| whole prefix (copies) | 16 / 99 | 1,825 / 2,197 | 2.81 / 3.39 |
| overdubs only (copies) | 16 / 99 | 1,307 / 1,346 | 2.01 / 2.07 |
| **history-closed, ≤ 128 pages in flight** | **16** | **796** | **1.23** |
| **history-closed, ≤ 128 pages in flight** | **99** | **729** | **1.12** (nothing closes in 30 overdubs) |

Final streams are hash-identical across all seven runs. Compared with B.5, "none" rose from 703 to 729 pages: those are
the directory pages, one per run longer than 4 pages (about 4 %). A larger inline count would remove most of them for
about 2 B per extra id per descriptor.

The in-flight cap is a policy in the test (and the probe), not yet in the store. A range is truncated to at most
128 pages of input; if that would re-merge a large merged run, the range starts after it (LSM-style levels).
Without the cap, history 16 peaked at 873 pages.

## Device (flash-resident code, PSRAM pages)

| Operation | B.4b | B.5a |
|---|---|---|
| Commit, 16,640-row Record | 15 µs | 10 µs |
| Compose 2-bar window, 10 runs (≤ 885 rows) | 3.32 ms | 2.18 ms |
| **Compose 2-bar window, 31 runs (≤ 1,588 rows)** | — | **5.45 ms** (8.75 ms before key caching) |
| Compose 2-bar window, compacted | 1.43 ms | 1.46 ms |
| Compaction step, 2,048 rows | 5.38 ms | **4.16 ms** |
| Pages in use at 31 runs (38,480 + 8,320 rows) | — | 784 |
| `slow_seals` | 0 | 0 |

## Reading

- **Undo depth 99 means nothing is compacted in a normal session**, because every overdub stays named by history.
  So the realistic steady state is "many runs, no compaction". It is now affordable on both axes: memory is 1.12× the
  content, and the worst 2-bar window at 31 runs is 5.45 ms, inside the 8 ms child budget, from flash.
- **Compaction is now purely an optimisation for aged-out history.** It never holds a second copy after publishing,
  and its in-flight copy is bounded by policy.
- **Budget (B.5 § 4) holds**, now with descriptors that make 64 slots cheap: about 1,024 runs × 76 B ≈ 76 KiB plus
  manifests, instead of the 2 KiB-per-run tables measured in B.5.

## Still open

- B.5b: the session high-water on device (the reserve beside which the store must fit during migration).
- Move the in-flight cap and the level rule from the test policy into the store as its compaction planner.
- A window over 31+ runs at denser content than measured, or with many hides, has not been timed. The worst case
  scales with rows in the window × log(cursors) plus hidden checks per row.
