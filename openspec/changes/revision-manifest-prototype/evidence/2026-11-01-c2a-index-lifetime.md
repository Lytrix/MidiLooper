# C1 follow-ups and C2a — collapse provenance matrix, and index lifetime by reader pins (host-verified)

Date: 2026-11-01. Native: `test_revision_manifest_store` (37), `test_revision_store_differential` (22). No device change
(board still on probe image `365b6d11…`).

## C1 follow-ups (owner review)

**Docs:** the import's single collapsed Edit run is a Stage-C shadow-import convenience, valid for parity only. It is
built from production's final apply result, so it discards edit provenance and order; it is not a candidate long-term
representation (`RevisionImport.h` header, C1 evidence).

**Production collapse provenance matrix**, measured on the production path (`test_collapse_matrix_*`):

| Identical rows from… | Production | Store `productionCompat()` |
|---|---|---|
| capture + capture (CC74=100 in Record and an overdub) | collapsed to 1 | 1 (parity) |
| capture + edit replacement (note 62 re-pitched to 60, identical to captured 60) | both kept | 2 (parity) |
| edit + edit (two notes re-pitched to the same 64, same span) | **both kept** (NoteOn 2, NoteOff 2; measured, not inferred) | 2 (parity) |

**The provenance bit survives compaction.** Each origin-table entry now carries `serial | editBit`
(`sourceIsEdit(run, origin)`). The projection decides per row: a row from an Edit source neither collapses nor is
collapsed against, and "earlier pass" means an earlier (run index, origin). `test_compaction_keeps_the_edit_provenance_…`
compacts an Edit run together with two overdubs, and then the Record into one run. The `productionCompat()` output is
identical at every step (capture duplicates collapse, the edit replacement is kept), and the merged run reports which
origins were edits.

## C2a — index lifetime and capacity (DEC-246 invariant 4)

**RED (before C2a).** `test_index_held_by_a_reader_is_never_evicted`: a reader pins manifest M1 and its index, two
more manifests get indexes, and the old LRU eviction dropped M1's index, so the reader's `IndexOnly` composition came
back `IndexOwed` (status 3).

**Model:**
- **Reader pins** are counted per manifest (`pin(id)` / `pinCurrent()` / `unpin(id)`), separately from history
  references.
- **Slots have explicit states:** `Empty`, `Building(identity)`, `Ready(identity)`. A slot is reclaimable only if
  it is `Empty`, or `Ready` with zero reader pins on its manifest (an index for a non-current manifest is preferred).
  `Building` and reader-pinned slots are never reclaimed.
- **`requestIndex(id)`** records demand and answers `NotNeeded`, `Ready`, `Building`, `Owed` or
  **`IndexCapacityDenied`**. When no slot is reclaimable, the worker step answers `CapacityDenied`, and nothing is
  evicted.
- **Honest identity:** a job builds the manifest it started on and publishes for exactly that identity. The
  publication is an O(1) state transition (`Building(41)` → `Ready(41)`). A newer request does not supersede it; only
  the explicit `cancelEditIndexBuild()` does. The next job takes the demanded manifest (if still live and unresolved),
  else the current one if owed.

| Contract | Test |
|---|---|
| A pinned index is never evicted | `test_index_held_by_a_reader_is_never_evicted` (RED → GREEN) |
| Two pinned slots + a third request → `IndexCapacityDenied`; release one → reclaimable | `test_two_pinned_indexes_and_a_third_request_is_capacity_denied` |
| Same revision, new manifest identity does not alias; compaction does not invalidate an old pinned index | `test_edit_index_is_qualified_by_manifest_identity` |
| A build started on M41 publishes as M41 after M42 became current; M42 stays `IndexOwed` until built | `test_index_build_publishes_the_identity_it_started_on` |
| Steps bounded; real-time compose never builds; no heap | B.5d tests (unchanged, still green) |

## Next

**C2b — bounded worker:**
- one registered child of the central idle/deferred owner (`RevisionEditIndexGrain`, ≤ 2 ms declared,
  0.5–1 ms fine grain) driving only `stepEditIndex()`;
- the store owned by a firmware-resident RevisionStore owner, with no build from playback, display or the tick;
- measured on device, which needs the C3 import to give it content.
