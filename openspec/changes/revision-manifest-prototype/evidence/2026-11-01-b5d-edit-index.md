# B.5d — hides as scalable source operations, and a manifest-qualified edit index

Date: 2026-11-01. Native: `test_revision_manifest_store` (33), `test_revision_store_differential` (18),
`test_revision_store_capacity`. Device: probe image HEX `365b6d113d78f066…`, boot verified
(`#CAP,1369003,DIAG,boot_reset,crash=0`, `BOOT,load,ok`), RAM1 identical to the default capture image.
Raw rows: [`2026-11-01-b5d-revstore-probe-raw.txt`](2026-11-01-b5d-revstore-probe-raw.txt).

## Scope (owner, 2026-11-01)

Two problems behind the 30.7 ms window (B.5c): the lookup was rows × later-runs-with-hides, and a run could hold at
most 256 hides. The owner's scope was more than adding an index: make hides scalable immutable source operations
keyed to stable provenance, and derive a manifest-specific, boundedly built edit index from them.

## Design

- **Hide storage.** HideNote rows use the same page-list machinery as Midi rows: one inline page, then directory
  pages from the pool. A run holds hides up to its page limit and the pool, not 256. Exhaustion is a latched, named
  refusal (`DeniedNoPage` / `DeniedRunFull`).
- **Hide target = source note identity (`noteId`)**, loop-scoped and set at capture. It never names a page, a row or
  a compaction ordinal, so compaction can move a note without retargeting any hide. Replacements (Shorten, NOTE_EDIT)
  deliberately carry their target's `noteId`: a note and its replacements are one lineage, and that is what makes
  "later wins" well defined on a single key.
- **Precedence (explicit, independent of any index):** a hide's position is (run index in the manifest, source
  origin). A Midi row is hidden iff the *latest* hide of its `noteId` sits at a strictly later position than the
  row's own. Equal position is visible: a pass's hide never hides its own replacement. The scan path and the index
  both implement exactly this rule, and the tests hold them to it.
- **EditIndex** (layer-3 acceleration of layer 2): a sorted table noteId → latest hide position, in pool pages, keyed
  by **manifest identity** (not revision), pinning its manifest. Two LRU slots, so a reader of an older manifest
  (for example, one that compaction replaced at the same revision) keeps its index while the new one is built.
- **Built off the edge.** `stepEditIndex(maxRows)` is a bounded, resumable k-way merge of the manifest's sorted hide
  regions, O(hides). It always targets the current manifest and abandons a job whose manifest stopped being current.
  `editIndexOwed()` names the obligation.
- **Composition never builds it.** `EffectivePolicy::hideLookup` has three modes:
  - `IndexOnly`, the real-time mode, returns `ComposeStatus::IndexOwed` when the index for that exact manifest is not
    published;
  - `Scan` is the slow reference, kept as the host oracle;
  - `IndexOrScan` is the off-line default.

  `ComposeStamp::usedIndex` reports which path ran.

## Tests (all required by the owner)

| Requirement | Test |
|---|---|
| Index output = scan output for `revision()` and `productionCompat()` | `assertIndexEqualsScan` in every B.5d test |
| Randomised manifests: index = scan = independent vector oracle, compaction mid-way | `test_edit_index_equals_scan_and_oracle_on_random_sessions` (6 seeds × 7 revisions) |
| Repeated hides and replacements obey explicit precedence | `test_edit_precedence_latest_hide_position_wins`: A, hide A, replacement A′, hide A again, then compaction, then undo |
| Compaction does not alter hide targets | same test, after compacting hides and replacement into one merged run |
| An old manifest's index stays valid beside a compacted same-revision manifest | `test_edit_index_is_qualified_by_manifest_identity` |
| Thousands of hides, up to pool capacity | `test_thousands_of_hides_in_one_edit_run`: 3,000 hides, then a named refusal on exhaustion |
| Index construction bounded and off the edge; real-time compose never builds | `test_realtime_compose_never_builds_the_index_and_steps_are_bounded` |
| No heap after startup | `test_zero_heap_after_construction` now includes the index build and an `IndexOnly` compose |
| Slow composition kept as a host oracle | `HideLookup::Scan` |

## Device timing (flash-resident code, PSRAM pages)

| | Rows max | avg | max |
|---|---|---|---|
| 31 runs, no hides | 1,492 | 4.41 ms | 5.08 ms |
| 31 runs, 30 × 60 hides, **scan** | 1,434 | 25.5 ms | **30.8 ms** |
| 31 runs, 30 × 60 hides, **edit index** | 1,434 | 5.06 ms | **5.56 ms** |
| Edit index build, 1,800 hides, 256-row steps | — | 0.79 ms/step | 1.12 ms/step (8 steps, 6.3 ms in total) |
| Dense burst (one window, 4,429 rows) | 4,429 | — | 7.88 ms |

All 32 hide-heavy windows resolved through the index and gave the same 44,600 rows as the scan. Pages in use for that
manifest, index included: 858.

## Reading

- The asymptotic fix holds on hardware. With the index, a hide-heavy window costs about the same as one without hides
  (5.56 vs 5.08 ms): one lookup per row, proportional to the window's rows, not rows × hide-runs.
- The 256-hide ceiling is gone. A whole-take edit is bounded by pages, like captured content.
- The real-time contract is explicit: until the index for that exact manifest identity is published, an `IndexOnly`
  reader gets `IndexOwed` and keeps its previous window. Nothing is built on the window path.

## Not covered

- Who drives `stepEditIndex` in firmware (which deferred owner, at what budget) belongs to stage C, with the other
  workers.
- Two index slots is a prototype choice. The production count follows from how many manifests real-time readers hold
  at once.
