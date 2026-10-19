# I4 — why the saved Set is absent from the Set browser (read-only diagnosis)

Owner-requested diagnosis of the blocker PLAYING-LOAD-RUN02 recorded as *"Actual SD root cause
UNKNOWN"* — the browser showed **zero Set rows** (`cachedSetCount == 0`) while the test-created
checkpoint `S0001_v0002` existed on the card. No firmware, test, host-tooling, threshold, marker or
stimulus change; no build, upload, reset, Save, load, SD-destructive or Git action. Read-only code
trace plus the already-captured device logs.

## Answer in one paragraph

`/MidiLooper/sets/index.bin` is written by **two owners in two incompatible layouts**. The Set
revision catalog writes a **16-byte** `SetRevisionCatalog::SetCatalogIndex` there; the legacy
SavedSet sequence index writes an **8-byte** record (`nextSequence` + the `"SAVE"` token) to the same
path. `BootRecovery::loadState` reconciles that path at **every boot that has a current workspace**
through `reconcileSetIndexOnSdWithOutcome`, which treats a file it cannot read in the *legacy* layout
as unreadable metadata and **rewrites it in the legacy 8-byte layout**. A catalog index can never pass
the legacy read (the token slot lands on `reserved`+`nextSetId`), so every boot destroys the catalog
index. `listSetRevisionBrowserEntries` then fails its 16-byte `readSetCatalogIndex`, returns **0
rows**, and the browser legitimately shows no Sets. Because the Set list is also read **at most once
per boot** — at the first overlay-enter frame whose catalog gate allows — and is **never invalidated**
(`loadSaveListCacheValid_` has no invalidation site; the sticky state
starts at the first *successful* refresh, which need not be the boot frame), a Set saved during the
same boot cannot appear either — so the Set is unreachable from the UI in every boot.

## The listing path (production owners)

| Step | Owner | Behaviour on the card's 8-byte index |
|---|---|---|
| Root rows | `SetBrowserOverlayPolicy::rootWorkspaceListRowCount` | `2 + setCount` — with `setCount 0` the list clamps at row 1 (`Current`), exactly the run02 receipt `OVLY,sel,0,1` |
| List read | `DisplayManager::refreshLoadSaveListCache` → `StorageManager::listSetRevisionBrowserEntries` | `loadSaveListCount_ = …; loadSaveListCacheValid_ = true;` — **empty is cached as valid** |
| Catalog gate | `StorageManager::isOverlayCatalogReadAllowed` | `OverlayCatalogReadPolicy` blocks only while a writer is *in progress* or holds SD; `*Pending` is allowed |
| Index read | `SetRevisionCatalog::readSetCatalogIndex` (`SD.open(kSetIndexPath, FILE_READ)`) | reads `sizeof(SetCatalogIndexFileLayout)` = **16** bytes; fails on an 8-byte file |
| Row filter | per candidate id `< nextSetId` | requires `sets/S####/set.bin` present, readable, `setId` match, `latestRevisionId != 0` — all unreachable when the index read failed |

`readRaw` (`src/StorageManager/FileIo.cpp`) prints exactly the observed failure when
`file.size() - file.position() < size`: **`Needed: 16, available: 8`**. At the overlay-entry frame the
only 16-byte read on that path is `readSetCatalogIndex` (the 16-byte layout carries a `static_assert`
against `kSetCatalogIndexByteSize`; the legacy trailer read is 60 bytes, and the Save/Current row's
detail resolve is in-memory via `readCurrentSetBrowserMetadata`). The two HITL arm paths
(`requestCommitRevisionForHitl`, `beginRevisionCommitSnapshot`) read the same 16 bytes.

## Why the file is 8 bytes (the collision)

* Catalog writer — `SetRevisionCatalog::writeSetCatalogIndex` (16 bytes, `static_assert`ed layout:
  `schemaVersion`, `reserved`, `nextSetId`, `setCount`, `catalogChecksum`), reached from
  `stepRevisionCommitCatalogUpdate` at commit `CatalogUpdate` (the COMPLETE publication of a Save).
* Legacy writer — `SavedSetCatalog::writeSetIndex` writes **4** bytes (`nextSequence`) and
  `writeSetIndexToSd` appends the **4**-byte `CurrentSetStorage::kSaveFileToken` (`0x45564153`,
  `"SAVE"`) = **8 bytes**, then renames onto `CurrentSetStorage::kSetIndexPath` — the *same* string as
  `SetRevisionCatalog::kSetIndexPath` (`/MidiLooper/sets/index.bin`, declared in both headers).
* The clobber — `reconcileSetIndexOnSdWithOutcome` (`src/StorageManager/SavedSetIo.cpp`) reads it in
  the legacy layout and repairs when `!indexReadable || reconciled != nextSequence || !SD.exists(path)`.
  `readSetIndexFromSd` also reports "readable" for an **absent** file, so a missing index is created
  as the 8-byte form too. Call site: `BootRecovery::loadState`, on the `hasCurrentWorkspace` path,
  before `load,ok`.
* Byte arithmetic (`kSaveFileToken` vs the catalog bytes at offset 4): with `reserved = 0` and
  `nextSetId = 1/2/3`, the legacy token reads `0x00000001/2/3`, never `0x45564153` — **a catalog index
  is always "unreadable" to the reconcile**, so the repair write always fires.
* The legacy *save-new-set* API (`StorageManager::saveNewSet` → `saveNewSetInternal`, the other
  `writeSetIndexToSd` caller) has **no call site** in `src/`, so the boot reconcile is the only
  production writer of the 8-byte form.

## Device evidence (already captured, no new run)

| Capture | Frame | Reading |
|---|---|---|
| `captures/i4-checkpoint-t8s4-02/session_20261012_160619.log` | after `#CAP,…,LDSV,1`; again after `LDSV,0` + `PERS,rev_overlay_save,…,queued_exit` | `readRaw … Needed: 16, available: 8` twice — browser entry **and** commit arm read the index successfully… they fail |
| `captures/i4-checkpoint-t8s4-02/session_20261012_160619.log` | after `#CAP,…,LDSV,1`; again after `LDSV,0` + `PERS,rev_overlay_save,…,queued_exit` | `readRaw … Needed: 16, available: 8` twice — both the browser-entry read and the commit-arm read fail on the 8-byte file |
| `captures/i4-overlay-load-t8s4-02/session_20261012_211542.log` | `[INFO] Load/save mode entered` → `readRaw … Needed: 16, available: 8`; then 18 fenced queries, no row 2 | the boot after that COMPLETE clobbered the index; driver exit 2 `missing_fresh_overlay_snapshot{open:1,mode:0,row:2,phase:0}` (`i4-overlay-load.json`) |
| `captures/i4-overlay-load-t8s4-01/session_20261012_202900.log` | no `readRaw` failure in 47,384 lines | **not** a discriminator: no message also results from an absent index or from a refresh skipped by the catalog gate; it is not evidence of a readable index |

Nothing in the tree records a run in which the browser listed a Set row, and no `OVLY` row with a
first-Set selection (`row >= 2`) exists in the i4/overlay captures.

## Named, not claimed

* `sets/index.bin` is **not corrupt**: the 8-byte file is a valid legacy index. Nothing here deletes
  revisions; `sets/S0001/` and its revision payload are not touched by the clobber (no directory dump
  was taken, so their present state is unverified).
* **Risk, unverified:** with an unreadable index, `beginRevisionCommitSnapshot` keeps a default
  `SetCatalogIndex` (`nextSetId = 1`), so a commit from a workspace with
  `workspaceDerivedFromSetId == 0` calls `allocateNextSetId` → **id 1** → reuses `S0001`, writes
  `revisions/r0001.bin` and rewrites `set.bin` with `latestRevisionId = 1` (`peekNextRevisionId` on a
  fresh meta is 1). Not observed in the i4 runs (their commits derived from set 1); needs its own
  fixture before any claim.
* **Secondary consequence, code-read only:** `queueWallClockSdCatalogSync` reads the same 16 bytes and
  silently returns on `!indexOk`, so Set-timestamp wall-clock sync never runs while the index is the
  legacy form.
* **Contributing defect B (no re-read):** `loadSaveListCacheValid_` is *declared*, set `true` in
  `refreshLoadSaveListCache`, and read once in `DisplayManager::runPaintResolvePhase`; it has **no
  invalidation site**. The refresh is attempted only on the overlay-enter transition
  (`loadSaveActive && !loadSaveModeWasActive_`), and `refreshLoadSaveListCache` returns without setting
  the flag when the gate is closed, with no retry on later frames. So (a) a Set saved while the
  overlay is open cannot appear until reboot, and (b) a refresh skipped once leaves the browser empty
  for the whole overlay session. Both are latent today; neither is needed to explain the observed
  empty list, since defect A alone empties it. *Wording correction folded in from the review:* the
  cache becomes sticky **after its first successful refresh**, not at boot — a gate-blocked attempt
  leaves the flag `false` and is simply never retried, so "read failed" and "read empty" currently
  collapse into the same empty list.
* **Why it escaped:** `test/test_storage_reconcile_index/test_storage_reconcile_index.cpp` pins the
  legacy outcomes only (`ok` / `repaired` for missing / `repaired` for malformed / wrapper) — it never
  places a 16-byte catalog index in the path, so "repair a file the *other* owner wrote" is untested;
  the catalog round-trip tests (`test_set_revision_persistence`) never run the boot reconcile between
  write and read.

## Fix candidates and required checks (no implementation authorized here)

One focused adversarial read-only review of this packet (2026-10-12) endorsed the cause and the path
separation and required four refinements; they are folded in below.

1. **Ownership (preferred):** give the revision catalog its own index path (e.g. `sets/catalog.bin`),
   leaving `sets/index.bin` to the legacy SavedSet index. A **read** of the old path only recovers a
   file that still holds the catalog layout (the window between a commit and the next boot) — it is
   *not* recovery for a card the reconcile has already overwritten, so it is a one-time migration aid,
   not the fix.
2. **Recovery is mandatory, and it is a rebuild — not a re-read:** reconstruct the catalog from the
   validated existing Set folders. Bounded, and owned by StorageManager boot recovery, whose existing
   repair already walks exactly this tree (`discardIncompleteRevisionTempFilesOnSd` enumerates
   `sets/S####/` and each `revisions/`; the HITL catalog path caps its own scan at `kMaxSetFolders`).
   Derive rows from `sets/S####/set.bin` read through `readSetMetaRecord` (schema + CRC validated,
   `setId` must agree with the folder name, `latestRevisionId != 0` — the same filter the browser rows
   use), then write the index through the existing temp+rename so an interrupted rebuild leaves the
   previous file intact and the rebuild is idempotent. No per-frame and no display-path folder scan.
3. **Never allocate over an existing Set:** today an unreadable index leaves `allocateNextSetId` at
   **id 1** (the named risk above). Both the rebuild and the commit's unreadable-index path must derive
   `nextSetId = max(existing setId) + 1` from the folders, or refuse the commit, instead of defaulting.
4. **Defence in depth (subordinate to 1–3, not a substitute):** the listing/cache must distinguish
   "index unreadable" from "catalog read successfully and is empty", and the legacy reconcile must
   refuse to rewrite a file whose layout is not the legacy one. A folder-scan fallback in the browser
   alone would mask the ownership defect and is explicitly **not** the recommendation.
5. **Cache freshness (defect B, separate decision):** invalidate the Set list cache on catalog
   publication **and** retry a gate-blocked refresh, so a Save is visible without a reboot and a
   skipped refresh cannot empty the browser for the session.
6. **Interruption surface:** both owners also share the temp path (`CurrentSetStorage::kSetIndexTempPath`
   and `SetRevisionCatalog::kSetIndexTempPath` are the same string, `sets/index.bin.tmp`), so whichever
   path split is chosen must split the temp file too — an interrupted write must not leave a temp file
   the other owner would rename over its own index.
7. **Do not** treat the current behaviour as an SD/user-data problem: quarantine, delete or re-save the
   card must not be used to work around it, and a blind `!REV_NUKE_SETS`/re-save would destroy the
   artifact this diagnosis depends on.

**Required tests for that slice** (production-path, per the review): commit → boot reconciliation →
browser listing in one fixture; an already-clobbered card (legacy 8-byte index present, `sets/S0001/`
populated) that must end with the Set listed; an interrupted rebuild (temp left behind) that must not
lose the previous index; and a commit from a workspace with `workspaceDerivedFromSetId == 0` while
`sets/S0001/` exists, which must **not** reuse `S0001` or overwrite an existing revision.

Acceptance criterion for a later authorized slice: after a boot with a current workspace present, the
browser lists the Set whose `sets/S####/set.bin` exists with `latestRevisionId != 0` — proven by a
fresh-boot device read of the real index (not by a same-boot Save, which masks A), plus the fixtures
above.

## Closeout

Read-only diagnosis; `docs/Runtime/PROJECT_STATE.md` and `docs/Runtime/CURRENT_WORK.md` carry the
status entry. One adversarial read-only review of this packet (2026-10-12) endorsed the cause and the
path separation, corrected the cache wording in the task's original phrasing and required the rebuild
form of recovery; its findings are folded in above. No firmware/OpenSpec checkbox, budget, marker,
stimulus, device, SD, upload or Git delivery. Both prior checkpoints and the failed run02 evidence are
preserved.
