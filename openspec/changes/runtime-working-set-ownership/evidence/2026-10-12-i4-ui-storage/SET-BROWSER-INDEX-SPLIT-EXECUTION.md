# I4 — Set browser index ownership: execution packet

Authority: [IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice).
Cause and evidence: [SET-BROWSER-EMPTY-CATALOG-DIAGNOSIS.md](SET-BROWSER-EMPTY-CATALOG-DIAGNOSIS.md).
Owner authorization: "yes continue" to the stated slice *path split + boot-time rebuild + cache
freshness* (2026-10-12). Two slices, A then B; A is this section's subject, B is §Slice B.

## Identity and scope (Slice A — one owner per persisted index path)

- Packet/task IDs: `runtime-working-set-ownership` I4 — UI/storage, Set browser index ownership.
  Approved contract: the diagnosis packet above (§Fix candidates items 1–3, 6) and the review's
  "recovery is mandatory / never silently default to ID 1 / keep recovery bounded" refinements.
- Status at writing: **specified**; fixtures implement A1/A2 first (RED), then the production change.
- Owner and production path: `StorageManager` boot recovery
  (`StorageManagerInternal::ensureSetCatalogIndexOnSd`) → `SetRevisionCatalog` index file →
  `StorageManager::listSetRevisionBrowserEntries` (browser rows) and the revision-commit
  allocation (`beginRevisionCommitSnapshot` → `stepRevisionCommitCatalogUpdate`).
- Primary files: `include/SetRevisionCatalog.h`, `src/SetRevisionCatalog.cpp`,
  `include/StorageManagerInternal.h`, `src/StorageManager/BootRecovery.cpp` (new ensure),
  `src/StorageManager/RevisionCommit.cpp` (commit guard), `src/StorageManager/SetBrowserRead.cpp`
  (path/constant only), `include/StorageManager.h` + `src/DisplayManager/LoadSaveOverlay.cpp`
  (only the constant-driven path).
- Prerequisites already present: in-memory native card (`test/test_support/firmware/SD.h`) with
  directory enumeration, temp-write budgets and rename budgets; `writeSetCatalogIndexFile` /
  `writeSetIndexToSd`; `readSetMetaRecord` (schema + CRC); `readSetCatalogIndex`.
- Included in this slice: the catalog path + temp path split, strict Set-folder basename parse,
  adoption of an intact pre-split index, bounded rebuild from Set folders, boot call site ordered
  **before** the legacy reconcile, commit allocation guard, A1–A7 fixtures.
- Explicit non-goals: no change to the 16-byte catalog or 92-byte meta record layout, schema version,
  CRC conventions, commit stage order, revision file format, legacy SavedSet behaviour on
  `sets/index.bin` (the reconcile is deliberately untouched — see D4), no HITL marker/stimulus change,
  no display-path folder scan, no deletion of any user file.
- Commit boundary: buildable and reviewable on its own; the browser lists Sets from the boot-ensured
  catalog index without Slice B.

### Frozen decisions

- **D1 — paths.** `SetRevisionCatalog::kSetIndexPath` → `/MidiLooper/sets/catalog.bin`,
  `kSetIndexTempPath` → `/MidiLooper/sets/catalog.bin.tmp`. The legacy SavedSet index keeps
  `CurrentSetStorage::kSetIndexPath` / `kSetIndexTempPath` (`sets/index.bin[.tmp]`) unchanged, so the
  two owners no longer share a final *or* temporary path.
- **D2 — strict folder parse.** New pure `SetRevisionCatalog::parseSetFolderBasename` (optional path
  prefix stripped at the last `/`; then `S` + 1..5 digits, all-digit tail, value `1..0xFFFF`).
  `parseSetIdFromFolderName` is left unchanged for its existing callers. Rebuild adoption must never
  guess an id from a name like `S0001_backup`.
- **D3 — ensure outcome.** `SetCatalogEnsureOutcome { Ok, Adopted, Rebuilt, Failed }` +
  `ensureSetCatalogIndexOnSd()` + `setCatalogEnsureOutcomeName()`, `STORAGE_PERSIST_MEM`
  (= `FLASHMEM`) in `BootRecovery.cpp`. `Ok` performs no write. `Adopted` writes the *already valid*
  16-byte index from `CurrentSetStorage::kSetIndexPath` (pre-split card, catalog file still intact).
  `Rebuilt` derives `nextSetId = max(folder id) + 1` over every strictly-parsed folder (accepted or
  not) and `setCount` over accepted rows only. `Failed` leaves the previous catalog file untouched
  (temp + rename).
- **D4 — reconcile untouched, ordering instead.** The legacy reconcile still repairs
  `sets/index.bin`; that is now harmless because the catalog's own file is elsewhere and the boot
  ensure runs first, so an intact catalog index at the old path is copied before any rewrite. A
  failed ensure is still recoverable next boot from the folders, so no layout check is added to the
  reconcile (smaller diff, no change to legacy semantics).
- **D5 — commit refuses rather than allocating blind.** `beginRevisionCommitSnapshot` reads the index;
  on failure it calls the ensure and re-reads. If the index still cannot be established, the commit is
  refused (`false`) — the stage machine's existing failure path prints the failed stage and resets, and
  a writable card was required to write the revision anyway. `allocateNextSetId` is therefore never
  reached with a fabricated `nextSetId = 1` while `sets/S####/` exist.

## Oracle fixed before behavior edits (Slice A)

Card model: in-memory card, `sets/S0001/set.bin` written through `SetRevisionCatalog::writeSetMetaRecord`
(`setId = 1`, `latestRevisionId = 2`, `revisionCount = 2`), `sets/S0001/revisions/r0001.bin` and
`r0002.bin` present with distinct bodies, `sets/index.bin` written through the legacy
`StorageManagerInternal::writeSetIndexToSd` (8 bytes).

| Case | Initial state | Ordered actions | Expected observation after each step | Counterexample that must fail |
|---|---|---|---|---|
| **A1** index survives boot | valid catalog index (`nextSetId = 2`, `setCount = 1`) written through `writeSetCatalogIndexFile` + the A-card | ensure → legacy `reconcileSetIndexOnSdWithOutcome` → read at `SetRevisionCatalog::kSetIndexPath` | index readable, `nextSetId == 2`, `setCount == 1`; reconcile outcome `Repaired` or `Ok` but never the catalog's file | the 16-byte index becomes an 8-byte legacy file (pre-fix behaviour → 0 rows) |
| **A2** already-clobbered card | no catalog index; legacy `index.bin` present; A-card folders | ensure → `listSetRevisionBrowserEntries` | ensure `Rebuilt`; listing returns **1** row `S0001`, `latestRevisionId == 2` | listing returns 0 rows (the observed device state) |
| **A3** pre-split adoption | valid catalog index at the **old** path, no `catalog.bin` | ensure → reconcile → listing | ensure `Adopted`; catalog file valid; listing 1 row; old path may be rewritten by the reconcile | the intact index is lost because the reconcile rewrote it first |
| **A4** no allocation over an existing Set | A-card, workspace derived set id 0, commit requested | commit job to `CatalogUpdate` | ensure `Rebuilt`; committed setId == **2**; `sets/S0001/set.bin` and both revision bodies byte-unchanged; `sets/S0002/set.bin` created with `latestRevisionId == 1` | setId == 1 and `sets/S0001/…` overwritten (pre-fix behaviour) |
| **A5** interrupted / failed rebuild | (a) stale `catalog.bin.tmp` + `index.bin.tmp`, no `catalog.bin`; (b) valid `catalog.bin` with `nextSetId = 2` and a failing write (`nativeSdFailWriteOpen` / `nativeSdRenameBudget = 0`) | ensure both | (a) `Rebuilt`, catalog valid, listing 1 row; (b) `Failed`, the **previous** catalog file still readable with `nextSetId == 2`, no half file | (b) truncated/absent index or a lost previous index |
| **A6** bounded, validated scan | A-card + a folder whose `set.bin` is garbage + a folder `S0001_backup/` | ensure | `Rebuilt`; only `S0001` counted; `nextSetId == max(folder id) + 1`; `S0001_backup` ignored | a garbage or suffixed folder producing a row carried forward |
| **A7** idempotent `Ok` | valid `catalog.bin` | ensure twice | `Ok` both times; **no write** (`nativeSdWriteSizes` empty), `nextSetId` unchanged | a rewrite per boot |

Preserved invariants: exactly one catalog index file per card; `nextSetId >` every strictly-parsed Set
folder id; `setCount ==` accepted rows; no revision payload opened by the rebuild; no user file
deleted; the legacy index keeps its own layout and path.

## Controls and coverage

- Existing seams: the in-memory card (writes, budgets, `nativeSdWriteSizes`), `writeSetIndexToSd`,
  `writeSetCatalogIndexFile`, `reconcileSetIndexOnSdWithOutcome`, `listSetRevisionBrowserEntries`.
  No new test-only seam is expected; if the commit case needs one, it is an existing commit fixture
  (the real-SD admission fixture reused by the earlier I4 admission work).
- Production paths exercised: the boot ensure + the legacy reconcile in their real order, the real
  listing, and the real commit stage machine for A4 (not a copied state machine).
- Failure cases: SD write failure, rename failure, stale temp files, garbage meta, suffixed folder.
- Untestable natively: physical-card behaviour and the OLED row rendering → device leg below.
- Numeric limits: the fast-path `kMaxCatalogRebuildFolders` bound is this slice's own bound (recorded
  in the completion record after the first build), not an inherited budget.

## Verification recipe (Slice A)

- Focused: `pio test -e native -f test_set_catalog_index_ownership` (new suite, A1–A7), plus the
  neighbours `test_storage_reconcile_index`, `test_set_revision_persistence`,
  `test_storage_drain_restore`, `test_storage_manager_load`.
- RED proof before the production change: A1 and A2 written against the existing API only, failing
  with the pre-fix behaviour recorded (0 rows / clobbered index).
- Broad: `pio test -e native` (expect the standing count with the new cases added, 0 failed).
- Build/RAM1: `pio run -e teensy41-capture-serial` — RAM1 padding must stay ≥ 840; new code is
  `FLASHMEM`, new literals are minimized.
- Markers: none added (plain `Serial` boot line, no `#CAP`/Tier-A prefix), so
  `scripts/check_marker_contract.py --audit` is not expected to change; run it only if a marker
  literal is touched.
- Diff: `git diff --check`.
- Device (separate approval): flash the built hex, then a fresh-boot read-only proof that the browser
  reaches the first Set row (`row == 2`) — the registered `i4_overlay_load` stimulus already waits for
  exactly that snapshot; if a lighter read-only path exists it must be preferred. The card's current
  bytes must not be deleted or re-saved to make the proof pass.

## Slice B — cache freshness (defect B), pinned after A

Scope: invalidate the Set list cache when the catalog is published, retry a gate-blocked refresh
within a bound, and distinguish "index unreadable" from "catalog read empty".
Oracle (fixed before edits): (B1) catalog publication during an open overlay makes the new/updated Set
row appear without a reboot; (B2) a refresh skipped because the catalog gate was closed is retried and
succeeds once the gate opens, with a bounded number of attempts; (B3) an unreadable index leaves the
cache **invalid** (so a later frame may retry) and is reported distinctly from a successfully read
empty catalog; (B4) no per-frame SD read (attempts are rate-limited); (B5) a same-boot Save followed by
overlay exit/enter lists the Set. Verification: focused display-path suite using the existing
`DisplayCaptureTestAccess` seam plus the existing overlay fixtures; then the same broad/native/build
gates.

## Fixture-first evidence (Slice A)

The oracle above was written **before** the production change and observed failing for the right reasons:

| Ordered step | Command | Result |
|---|---|---|
| 1. A1 + A2 as fixtures against today's behaviour | `pio test -e native -f test_set_catalog_index_ownership` | **2 of 3 cases failed** — A1 `the 16-byte catalog index must survive the boot catalog order`; A2 `Expected FALSE Was TRUE` on `SD.exists(SetRevisionCatalog::kSetIndexPath)` (both owners still shared the path) |
| 2. Two boundary-fidelity gaps exposed by the fixture | `test/test_support/firmware/SD.h` | the in-memory card did not enumerate intermediate directories, returned full paths from `name()`, and `exists()` answered false for a directory — so no native fixture could reach a production folder scan; all three now match the device |
| 3. A1–A7 green | `pio test -e native -f test_set_catalog_index_ownership` | **8 cases, 8 succeeded** (A3 adoption, A5a stale temps, A5b failed adoption, A6a/A6b strict parse and bounded scan, A7 idempotent `Ok`) |
| 4. Browser + commit cases green | `pio test -e native -f test_storage_drain_restore` | **22 cases, 22 succeeded** — includes the listing case, the no-reuse case, the publication handshake, the `readOk` case, and the updated header-patch case |
| 5. Mutations (each restored byte-identically, verified with `cmp`) | M1 pre-fix commit path; M2 adoption branch removed; M3 boot order swapped; M5 `nextSetId` from accepted rows only | M1 → A4 `Expected 2 Was 1` (the pre-fix reuse) + the host tripwire; M2 → A3 `Expected 1 Was 2` (`Adopted` vs `Rebuilt`); M3 → the host tripwire fails while the focused suite stays green (the documented oracle gap the tripwire exists to cover); M5 → A6b `Expected 6 Was 3` |
| 6. Build / RAM1 | `pio run -e teensy41-capture-serial` | `SUCCESS`; `[RAM1 budget] variables 96,192 / ITCM 393,216 (code 392,216 + padding 1,000) / locals 34,880`, floor `padding >= 840` (12 banks, unchanged region) |
| 7. Boot-order tripwire | `.venv/bin/python -m unittest discover -s scripts -p test_boot_catalog_order.py` | **3 tests, OK** — ensure before the legacy reconcile; ensure before `allocateNextSetId`; the two owners share neither path nor temp path |

**Named, not smoothed:** the first Slice A test registration put three `RUN_TEST` lines at the *front* of the drain
**Named, not smoothed — two findings from the broad run:**

1. **Three suites could not link after Slice A.** `test/test_support/capture_storage_harness.inc` includes
   `src/StorageManager/RevisionCommit.cpp`, and that TU now calls `ensureSetCatalogIndexOnSd()` (Slice A) and
   `markSetCatalogChangedForDisplay()` (Slice B). The harness compiled neither TU, so
   `test_drain_reentry_callback_safety`, `test_shared_capture_finalization` and
   `test_live_capture_display_boundary` failed at **link** time — the first full-suite run of this slice
   reported "2993 test cases: 7 skipped, 2983 succeeded", and that arithmetic (2993 − 7 ≠ 2983) was the only
   sign: the three suites contributed no case slot at all, so "0 failed" was not "everything ran". Fixed by
   adding both TUs to the harness and dropping the now-duplicate `Overlay.cpp` include from the display-boundary
   suite; each of the three then passes alone (10/10, 85/85, 10/10) and the final complete run is recorded below.
2. **Test registration order matters in the drain suite.** The first Slice A registration put three `RUN_TEST`
   lines at the *front* of `main`, so a real commit ran before the S2b.7 fixtures and broke two of them. The
   fixtures were right and the registration was wrong; re-appending them at the end restored 22/22.

RAM1 headroom is thin (1,000 vs the 840 floor; code 392,216): if a later change needs the allowance, the named
next step is to move the display-side cache-freshness block into a `DISP_COLD_MEM` helper (this slice already
moved the new policy and boot-rebuild functions to flash).

**Final broad run:** `pio test -e native` on the final tree → **3097 test cases: 7 skipped, 3090 succeeded**, every one of
the 114 suites `PASSED` (0 failed, 0 errored, arithmetic consistent: 3090 + 7 = 3097). The three link-broken suites are
confirmed green *inside* that complete run, not only standalone. The earlier 2993-case number of this slice is **not**
used as its evidence.

## Slice A — completion record

- **Oracle/contract unchanged, or authorized change and supporting evidence:** the contract is the diagnosis
  packet's §Fix candidates items 1–3 and 6 plus the review's refinements. The path split, the boot ensure and the
  commit guard are the slice's authorized change, recorded as **DEC-120 — one owner per persisted Set index path**
  in `docs/DECISION_LOG.md`.
- **Fixture implemented and discriminating assertion demonstrated:** yes — A1–A7 plus the two drain-suite cases;
  the mutations show each assertion is discriminating (M1/M2/M5 kill exactly one case, M3 kills the tripwire).
- **Native verification:** focused `test_set_catalog_index_ownership` 9/9 (includes B1–B4); `test_storage_drain_restore`
  22/22; host tripwire 3/3; full `pio test -e native` **3097 cases / 7 skipped / 3090 succeeded, all 114 suites PASSED** (see §Fixture-first evidence for the two findings this run exposed).
- **Build/RAM1 verification:** SUCCESS at the line above; **uploaded** (`-t upload`, 15.3 s) with the hex hash
  unchanged (`ecd4f671…`, captured metadata hash) and the USB device re-enumerating.
- **Device verification: PASSED for the acceptance criterion** — see §Device leg below. The registration rows show
  the browser reaching the first Set row (`row == 2`) and opening that Set's history, which run02 clamped at row 1.
  **Owner panel witness now confirmed:** S0001/v0002 history visible and responsive; the identity-gated Load/dirtyYes continuation was not run.
- **Required reviews and durable docs:** `DEC-120` appended; this packet and the diagnosis packet updated; the
  `PROJECT_STATE`/`CURRENT_WORK` status entries carry the outcome. An independent adversarial review of this
  *implementation* is still owed if the owner wants one before the device leg.
- **Task IDs eligible for checkoff; stage gates still open:** overlay5.4 — Set/history visibility after boot is closed;
  `load-save-overlay-display-regression`6.2–6.4 — actual dirty/playing Load remain unchanged/open.

## Slice B — cache freshness, completion record

- **Scope as frozen above** (invalidate on catalog publication, bounded retry of a gate-skipped or failed refresh,
  failure ≠ empty). Owner split: `SetBrowserOverlayPolicy` decides, `DisplayManager` acts, `StorageManager`
  publishes the change signal and reports the read outcome.
- **Fixture:** the bounded retry decision is a pure policy (`ListRefreshState`, `resolveListRefreshAction`,
  `applyListRefreshResult`) with cases in the ownership suite: overlay closed / cache valid → `Idle`; closed gate →
  `WaitForGate` **with no attempt recorded**; first attempt → `Refresh`; inside the interval → `Idle`; the fourth
  failed attempt → `GiveUp`; a completed read resets the budget; a catalog change (caller reset) → immediate
  `Refresh`. `readOk` semantics are pinned in the drain suite (`an unreadable index is not an empty catalog`), and
  the publication handshake end-to-end by `a completed commit publishes one catalog change for the display`
  (exactly-once consume).
- **Not natively covered (named, not claimed):** the display wiring itself — that `runPaintResolvePhase` consumes
  the signal and calls the policy — is compile-checked and device-visible only; today's display suite stubs
  `refreshLoadSaveListCache`, so a wiring fixture would need that seam reworked.

## Device leg — 2026-10-12 (acceptance criterion met)

Invocation (the registered stimulus, authoritative entry point; uploaded image pinned by hash):

```
.venv/bin/python scripts/host_midi_hitl.py run --scenarios i4_overlay_load \
    --track 8 --loop-slot 4 --midi-channel 8 --panel-overlay-closed --boot-reset \
    --out-dir captures/i4-overlay-load-t8s4-03
```

| Fact | Reading |
|---|---|
| Image | `firmware_hex_sha256 = ecd4f6713c8977aa0523b1fed10bf858439a16438c8f3112dfe9c20b394e8ec2` (from captured metadata; earlier ecdd spelling corrected), env `teensy41-capture-serial`, git `26125ceb+dirty` |
| Boot order on the device | `BOOT,load,start` → `[StorageManager] INFO: sets catalog index ensure ok` → `BOOT,load,ok` (the ensure runs inside the load, before the legacy reconcile) |
| **The browser row run02 could not reach** | `#OVERLAY_STATUS,us=20619883,open=1,mode=0,row=2,set=0,rev=0,phase=0` — root list at **row 2** (first Set row) after the two scroll steps |
| It is the right Set | `#OVERLAY_STATUS,us=22926948,open=1,mode=2,row=0,set=1,rev=0,phase=0` — history of **Set 1** opened from that row |
| Identity gate | `human-load-request.json` written with nonce/label `S0001_v0002`; **no reply file** — the run ended `RuntimeError: owner_checkpoint_identity_timeout`, i.e. it stopped at the human gate rather than guessing an identity |
| Revision commit/load before that gate | **zero** rows matching `PERS,rev_dispatch` / `PERS,rev_commit` / `rev_load_dirty_yes` / `rev_load_dispatch` / `rev_load_complete`; this does not establish zero ordinary boot/transport SD writes |
| Shutdown | `shutdown-result.json`: `status complete`, `drains 2` ("2 stop+drain cycle(s) reached verified persistence idle"), `ok true` — no latch |

**Boot-boundary proof:** this run's managed reset boot found the catalog valid (`ensure ok`) before the
legacy reconcile and `BOOT,load,ok`, then listed Set1/history. The first post-upload boot's ensure branch
was not captured; neither adoption nor rebuilding on that earlier boot is independently attested here.
The measured hardware claim is readable catalog/browser state after the captured boot.

**Panel witness and remaining gates:**

- **Owner panel witness supplied at closeout:** “Yes — S0001/v0002 history visible and responsive;
  no Load completed.” Recorded retrospectively in run03/oled-observation.json; not an expired-nonce approval.
- **The identity-gated continuation** (Load → dirty Yes → preserving revision → load `S0001_v0002`): deliberately
  not run — it needs the owner in the loop within the ≤90 s nonce window and it writes a preserving revision to the
  card. The run left the request unanswered, so no revision Save/Load was confirmed; ordinary SD writes are not excluded.
- **`load-save-overlay-display-regression` 6.2–6.4** (minimal-loading screen, focus/restore) are unchanged by this
  slice and still need their own legs.

Capture: `captures/i4-overlay-load-t8s4-03/` (session log, `i4-overlay-load.json`, identity request, shutdown result).

## Closeout

The acceptance criterion of this slice — *after a boot with a current workspace present, the browser lists the Set
whose `sets/S####/set.bin` exists with `latestRevisionId != 0`* — is **met on the device**
(`captures/i4-overlay-load-t8s4-03`). Uploaded to the test board; **no Git delivery**; no SD-destructive action.
Set/history visibility after boot is now closed with the owner's retrospective panel witness.
The identity-gated Load continuation and overlay6.2–6.4 remain open; run04 is incomplete and supplies no teardown proof.
Both prior checkpoints/run02 evidence preserved. [Narrow validation closeout](SET-BROWSER-VISIBILITY-CLOSEOUT.md).
