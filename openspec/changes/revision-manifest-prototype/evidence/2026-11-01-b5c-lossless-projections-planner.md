# B.5c — lossless compaction, explicit projections (DEC-245), the planner, hard-window timing

Date: 2026-11-01. Native: `test_revision_manifest_store` (28), `test_revision_store_differential` (18, none ignored),
`test_revision_store_capacity`. Device: probe image HEX `2db2116af4a6d27a…`, boot verified
(`#CAP,1315002,DIAG,boot_reset,crash=0`, `BOOT,load,ok`), RAM1 identical to the default capture image.
Raw rows: [`2026-11-01-b5c-revstore-probe-raw.txt`](2026-11-01-b5c-revstore-probe-raw.txt).

## Owner decisions recorded (DEC-245)

Committed source is lossless (layer 1). Meaning is a projection (layer 2). Serving forms are caches (layer 3).
- **B.3a:** identical rows from different passes stay distinct.
- **B.3b:** zero-length pairs stay in source.
- **Compaction** changes representation only. It must preserve the source multiset, provenance, and every retained
  manifest's effective output.

## What changed in the store

1. **Compaction is lossless.** B.5a dropped hidden rows and, for prefix ranges, hides; that was semantic garbage
   collection, which DEC-245 forbids. A merged run now keeps every source row and every hide, each with
   `origin` (its source pass's ordinal inside the merged run) and its original `seq`. An origin page lists the source
   passes' serials (`PassRun::serial`, monotonic, never reused), flattening already-merged runs.
   - Hiding inside a merged run is origin-aware: a hide from source pass *j* hides rows of source passes before *j*,
     as it did across the original runs.
   - Order inside a merged run is (tick, class, origin, seq), so DEC-244 order is unchanged.
2. **Effective-view policy is explicit** (`EffectivePolicy`, an argument to `composeWindow`):
   - `revision()`: hides and replacements; the default.
   - `source()`: layer 1, with no hides.
   - `productionCompat()`: adds today's production projections, zero-length suppression (B.3b) and the cross-pass
     identical-event collapse (`midiEventMatchesSnapshot`, B.3a).
3. **Planner in the store.** `planCompaction(maxInputRows)` takes the first history-closed range of layered runs
   (never the base run) that fits the row cap. A range that would re-merge a large merged run starts after it.
4. `forEachSourceRow(manifest, f)` visits layer 1 with each row's source serial, for tests, persistence and
   diagnostics.

## Evidence

- **Lossless compaction** (`test_compaction_is_lossless_for_…`). The fixture has a zero-length pair, a CC row repeated
  byte-for-byte in three passes, a hide of a Record note, and a hide between two passes *inside* the compacted range.
  After compaction:
  - the source multiset, including each row's source serial and every hide, is identical;
  - the source, revision and production-compat outputs are each identical;
  - the in-range hide still hides the earlier pass's note and not the later one.
- **Projections** (`test_effective_policies_are_…`):

  | View | Identical CC74 rows | Zero-length pair | Hidden note |
  |---|---|---|---|
  | source | 3 | kept | present |
  | revision | 3 | kept | absent |
  | production compat | 1 | dropped | absent |

- **Differential against production:**
  - The former ignored divergence (zero-length) is now parity under `productionCompat()`, with 4 rows in source.
  - On all 520 seeded B.2b revisions, the store's `productionCompat()` output equals production's effective view
    group-for-group, including the 16 cross-pass collapses, with no harness-side model needed.
  - B.3 seeded overlap/edit/undo: 132/132.
- **Planner** (`test_planner_takes_only_closed_ranges_within_the_row_cap`): it merges only closed ranges, respects the
  row cap, and does not re-merge a full merged run.
- **Capacity with the store's planner:** history 16 peaks at 798 pages (1.23× content); history 99 at 729 (1.12×).
  Lossless costs 2 pages more than B.5a's dropping compaction. The final stream is hash-identical under every policy.

## Device timing (flash-resident code, PSRAM pages)

| Window (2 bars) | Rows max | avg | **max** |
|---|---|---|---|
| 1 run | 525 | 0.50 ms | 0.61 ms |
| 10 runs | 787 | 1.26 ms | 1.96 ms |
| 31 runs, revision | 1,492 | 4.54 ms | 5.42 ms |
| 31 runs, production compat | 1,492 | 4.76 ms | **5.26 ms** |
| Dense burst: 2,000 notes in bars 0–1 | 4,429 | 0.77 ms | **7.85 ms** |
| **31 runs, each hiding 60 Record notes** | 1,434 | 22.8 ms | **30.7 ms** |
| Compaction step, 2,048 rows | — | 3.59 ms | 4.21 ms |
| Record commit, 16,640 rows | — | — | 8 µs |

(The generator now steps in 1/64 ticks to allow bursts, so absolute rows per window differ slightly from B.5a.)

## Reading

- The projections are nearly free (5.26 ms vs 5.42 ms), and lossless compaction costs nothing measurable.
- **The dense burst fits, barely:** 4,429 rows in 7.85 ms is about 1.8 µs per row, from flash. A denser burst would
  need a shorter window or ITCM placement.
- **Hide-heavy manifests break the budget.** Every row asks every later run that carries hides, each with a binary
  search in PSRAM, so the cost is rows × hide-runs × log(hides). The same structure caps a run at 256 hides
  (4 hide pages), which a bulk NOTE_EDIT such as quantising a whole take would exceed.

## Next (B.5d)

A **per-manifest hide index**: noteId → (latest hiding run, origin), sorted by noteId, one lookup per row. It is a
layer-3 cache derived from the manifest: built off the edge (merge the parent's index with the new run's hides,
O(hides)) and held for the current and pinned manifests only. With it, the hide region of a run can use the run's own
page directory, so bulk edits fit. Then re-time the hide-heavy window.
