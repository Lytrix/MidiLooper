# C3b — bounded, transactional shadow import; both grains device-qualified; the C4 oracle is the passes view

Date: 2026-11-01. Native: `test_revision_store_differential` (25), `test_revision_manifest_store`,
`test_realtime_idle_owner_admission` (16). Device: probe/shadow image, final HEX `cc17d96c5fcfab6c…`, boot verified
by the managed HITL run. Default image RAM1 is unchanged (96,192 / 391,944 / 1,272) with both new registry rows.
Raw rows: [`2026-11-01-c3b-occupy-status-raw.txt`](2026-11-01-c3b-occupy-status-raw.txt).

## Owner corrections applied

1. **Transactional candidate pipeline.** The source key is frozen, the candidate is private, it is built in bounded
   steps, revalidated, and published atomically.
2. **Its own child:** `IdleOwnerChild::RevisionShadowImportGrain` (`revision_shadow_import`, 2 ms / 0.5 ms), separate
   from `RevisionEditIndexGrain`; both are pinned by the admission test.
3. **The whole step is bounded.** There is no whole-pass copy, no whole-pass pairing helper, and no per-edit content
   scan.
4. **Restored slots** are `NoCommittedSource`; the import never reconstructs layer 1 from a snapshot.
5. **C4 oracle = the passes-derived `revision()` surface.** `productionCompat()` is renamed
   `fullRebuildSnapshotCompat()` and documented as one legacy path's regression oracle only.

## Design

- **Store: staged candidate.** `beginStagedRun` / `sealStagedRun` build runs that belong to no manifest, so readers
  keep the published one. `publishStaged()` makes the whole candidate current in one O(runs) step; `cancelStaged()`
  returns every candidate page.
- **`ImportJob`** (`RevisionImport.h`), fixed-capacity state placed with the store:
  - `begin` freezes `ImportSourceKey` (loopId, playbackRevision, committed-pass revision, and an FNV hash over every
    pass's id, state and chunk count and every edit pass's id and state). It plans the passes and edit targets and
    refuses **before writing anything** on pages (from `chunkUsedCount`), run slots or manifest slots, or with
    `NoCommittedSource` or `UnsupportedEditPass`.
  - `step(maxRows)` revalidates the key first. A moved key gives `SourceMoved` and cancels the candidate.
  - Phases:
    1. **Captures:** one sealed event at a time (`LoopEventStore::readSealedChunkEvent`). NoteOffs get their id by
       LIFO pairing in append order with a fixed open-note table, and edit-target rows are collected into fixed
       per-target slots.
    2. **EditApply:** one production `applyNoteEditPass` per edit, on that target's ≤ 4 events.
    3. **EditSort:** an in-place heapsort of the replacement rows.
    4. **EditEmit.**
    5. **Publish.**
  - `importLoop` is now "begin, then steps to completion", so every parity check runs through the job.
- **Firmware shadow:** the store plus job plus window total 479,940 B in PSRAM, allocated once at boot. Each Snapshot
  owner turn runs one unit, in both regimes:
  - an import step, when a candidate is building or the selected source differs from the published one (a refused key
    is not retried until it changes);
  - otherwise an edit-index step.

## Native evidence

- **662 / 662 import-parity checks** through the bounded job (B.2a, B.2b seeded, B.3, B.3 seeded, matrix). The
  streaming pairing matches production everywhere, with 0 unpaired NoteOffs.
- `test_import_candidate_is_invisible_until_published`: the current manifest and its identity are unchanged on every
  step until publish; steps of 16 rows.
- `test_import_source_moving_mid_build_cancels_the_candidate`: `SourceMoved`, the candidate's pages are returned, the
  published manifest is untouched, and no staged runs remain.
- `test_import_refusals_happen_before_anything_is_written`: `DeniedCapacity` with 0 pages written;
  `NoCommittedSource`.
- Found on the way: run slots, not pages, ran out when four history entries held older imports. Runs and manifests
  are now part of the up-front check, and the test store uses the device's shallow history (2).

## Device evidence

**Session:** HITL preset `overdub_occupy_paint` (STIM-OCCUPY-PAINT-01) on scratch track 8 / slot 4 / channel 8. The
second overdub hides and shortens notes of the first, then there are undo/redo gestures. The production state left
behind has 6 active Shorten edit passes and 11 hides.

| Reading | Run 1 | Run 2 (final image) |
|---|---|---|
| Shadow publishes / `SourceMoved` cancels / named refusals | 11 / 1 / 2 | 11 / 1 / 2 |
| **`RevisionShadowImportGrain` max whole call** (64 rows) | **683 µs** (55 calls) | **661 µs** (40 calls) |
| **`RevisionEditIndexGrain` max whole call** (256 rows) | **49 µs** (7 calls) | **76 µs** (8 calls) |
| Indexes published / `IndexOnly` reader used the index | 7 / yes | 8 / yes |
| **Shadow `revision()` vs production passes view** | — | **54 / 54, 0 production-only, 0 shadow-only** |
| Shadow `revision()` vs production snapshot (current) | 54 vs 62 (16 / 8) | 54 vs 62 (17 / 9) |

Both grains are well inside their declared 2 ms. The import step at 64 rows leaves room to raise the grain later from
this evidence.

## Findings

1. **The passes-derived `revision()` surface matches exactly under real overlap hides,** read the real-time way (edit
   index published for the exact manifest identity, `IndexOnly`).
2. **Production's snapshot and passes surfaces disagree** after overlap Shortens plus undo/redo: the current snapshot
   has 62 rows against 54 in the passes view (for example extra NoteOns at t=0, 257 and 1023 in the snapshot). Every
   shadow "mismatch" against the snapshot is this difference. That supports the passes view as the C4 oracle, and it
   is a production observation for the overdub-audibility investigation. Not investigated here.
3. **The HITL preset's own verdict was FAIL** (`FIRMWARE_CONTRADICTION`: `occupy_adopt_partial_missing`,
   `occupy_od2_draw_src_missing`, `occupy_undo_lt_2:0`, `occupy_redo_lt_2:0`, `occupy_rc1b_dumps_missing`,
   `stimulus_validation:must_prove:undo_redo`). Several named markers (`draw_src` and the like) belong to the
   investigation-diag env, not this capture image, and its undo/redo proof did not validate. Not investigated. The
   comparison above used the production state as it stood.

## Still open

- Hydrating persisted pass records for restored slots (today: `NoCommittedSource`, named).
- C4: a shadow rolling-window producer compared per window against the passes `revision()` surface under the owner
  chain.
