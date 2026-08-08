# Handoff — Set / Revision persistence (next chat)

**Date:** 2026-06-27  
**OpenSpec:** `openspec/changes/set-revision-persistence/`  
**Architecture plan:** `docs/Plans/set_revision_persistence_architecture_enhancement.md`  
**Apply command:** `/opsx:apply` on `tasks.md` — **next: task 3.9 (parked) or section 4 overlay**

---

## One-line goal

**Current** (mutable, epoch-based) + **Sets** (immutable revisions). Save appends packed `v####.bin` (REVPK02 chunk stream) via deferred FSM; Current unchanged after Save. Extend existing pass/chunk storage — **no** StorageManager / LoopPasses rewrite.

---

## What is done (tasks 1.x–2.x, 3.1–3.6)

### Section 1 — metadata + catalog + REVPK02

| Task | Deliverable |
|------|-------------|
| 1.1 | SD file layouts: `WorkspaceMetaRecord`, `SetCatalogIndex`, `SetMetaRecord`, `EpochFileHeader`; pass header `CapturePassSlotFileHeader` |
| 1.2 | `SetRevisionCatalog` — `index.bin`, `set.bin`; revision id on COMPLETE only |
| 1.3 | Monotonic Set id; failed revision reuses id |
| 1.4 | `RevisionPackedBlob` — **REVPK02**: 128 B header, typed chunk stream (`Transport` / `LoopSlot` / `SlotIndex`), 12 B CRC footer |
| 1.5 | Native: `test_set_revision_persistence` (28 tests) |

**New modules:** `PersistenceLayout`, `PersistenceSchema`, `CurrentWorkspaceStorage`, `SetRevisionCatalog`, `RevisionPackedBlob`

**REVPK02 layout (LMDB-inspired mental model — not an LMDB port):**

```text
[Header]     chunkCount, payloadSize, sourceEpoch, …
[Chunks]     Transport → LoopSlot(s) → SlotIndex (directory written last)
[Footer]     SVOK + payloadCrc32 + fileSize  (see DEFERRED_RUNTIME_PERSISTENCE.md § Persistence tokens)
```

`LoopSlot` bodies use `StorageLoopIo` v5 **slot file layout** (`recordPass`, `overdubPasses[]`, `editPasses[]`) streamed to SD on commit (task 3.6).

OpenSpec **pros/cons** (format vs MIDI timing): `proposal.md` + `design.md` § REVPK02 vs REVPK01.

### Section 2 — current workspace on SD

| Task | Deliverable |
|------|-------------|
| 2.1 | Epoch headers on current files; `workspace.bin` at deferred-save completion |
| 2.2 | Roots `/MidiLooper/current/` + `/MidiLooper/sets/`; deferred FSM retargeted |
| 2.3 | `SlotSummary[8]` in `workspace.bin` for overlay preview |
| 2.4 | Native: epoch valid/invalid; dirty = `currentEpoch != lastCommittedEpoch` |

### Section 3.1 — revision commit (shipped)

| Item | Detail |
|------|--------|
| FSM | `REQUEST → SNAPSHOT → WRITE → VALIDATE → CATALOG_UPDATE → COMPLETE` on deferred infrastructure |
| API | `StorageManager::requestCommitRevision()`, `hasRevisionCommitWork()`, `isRevisionCommitActive()` |
| Budget | `Config::maxPersistenceMicrosActive` (300µs) while PLAYING/RECORDING/OVERDUBBING |
| WRITE | REVPK02 chunk stream — LoopSlots via `StorageLoopIo` pass shapes; Transport opaque copy interim |
| VALIDATE | Header CRC + payload CRC + **SVOK**; patch `revisionId` + `chunkCount` on header |
| COMPLETE | Updates `lastCommittedWorkspaceEpoch` (= live `currentWorkspaceEpoch`), `workspaceDerivedFromSetId/RevisionId`, `workspace.bin` |
| Block | Revision blocked while deferred `current/` save active (`rev_blocked` serial) |

**Firmware helpers:** `appendRevisionCommitPayloadCrc`, `computeRevisionCommitPayloadCrcFromFile` (footer CRC from on-disk payload — fixes incremental CRC mismatch on Teensy).

**Stale provenance:** if `workspaceDerivedFromSetId != 0` but `set.bin` missing, commit allocates new Set id.

### HITL — revision commit save (with cleanup)

| File | Role |
|------|------|
| `scripts/hitl/scenarios/revision_commit_save.py` | Transport stop → deferred idle → `!REV_COMMIT` → `!REV_CLEANUP` |
| `scripts/hitl/verify/revision_commit_save.py` | Serial: `rev_request`, `rev_hitl_arm`, `rev_dispatch`, `rev_complete`, `rev_cleanup ok` |
| `scripts/hitl/deferred_save_idle.py` | Wait for `PERS,result,ok` before commit |
| `scripts/hitl/registry.py` | Preset `revision_commit_save` |

**SESSION_CAPTURE hooks** (`main.cpp`, `StorageManager.cpp`): `!REV_COMMIT`, `!REV_CLEANUP`; telemetry `rev_*` via `#CAP,PERS,…`.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset revision_commit_save \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5
```

**Verified 2026-06-27:** PASS (`S0001_v0001`, cleanup restores catalog/workspace).

### Section 3.2 — revision load (shipped 2026-06-27)

| Item | Detail |
|------|--------|
| FSM | `VALIDATE → WRITE (new epoch) → ReloadRam → COMPLETE` on deferred infrastructure |
| API | `requestLoadRevision()`, `hasRevisionLoadWork()`, `isRevisionLoadActive()` |
| VALIDATE | Stream footer CRC + chunk walk from SD (no 4 KB full-file buffer) |
| WRITE | New Current epoch; copy Transport + LoopSlots; empty slots get deferred empty loop wire |
| Transport missing | Default `runtime.bundle.bin` from SlotIndex (`occupied`, `loopLengthTicks`); LoopSlots still restore |
| Commit fix | Transport chunk header written when `readPos == 0`; bundle body size without strict epoch match |
| SlotIndex | Occupied slots only; `loopLengthTicks`, `noteCount`, `bars` at commit |
| Display | `consumeRevisionLoadDisplayRefreshPending()` → invalidate live + visual caches |
| Provenance | `derivedFromSetId/RevisionId` updated in ReloadRam; `workspace.bin` written |

**SESSION_CAPTURE hooks:** `!REV_LOAD <setId> <revisionId>`; `rev_load_request`, `rev_load_dispatch`, `rev_load_complete` (detail may include `,default_transport`).

### HITL — revision load

| File | Role |
|------|------|
| `scripts/hitl/scenarios/revision_load.py` | Transport stop → `!REV_COMMIT` → `!REV_LOAD` → `!REV_CLEANUP` |
| `scripts/hitl/scenarios/revision_load_post_record.py` | Skip transport prelude (after base record save) |
| `scripts/hitl/verify/revision_load.py` | Serial: commit + load `rev_*` lines, `rev_cleanup ok` |
| `scripts/hitl/registry.py` | Presets `revision_load`, `revision_load_record` (= `base` + post-record) |

```bash
# Empty-workspace smoke (no loop data):
.venv/bin/python scripts/host_midi_hitl.py run --preset revision_load \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 --track-number 5

# Record baseline → commit → load (loop data):
.venv/bin/python scripts/host_midi_hitl.py run --preset revision_load_record \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 --track-number 5
```

**Verified 2026-06-27:** `revision_load_record` PASS — base record on track 5, `rev_load_complete` `S0001_v0019`.

Use `--skip-hitl-cleanup` when debugging failed load. Optional dev `!REV_NUKE_SETS` via `--nuke-sets-before-run` (not default).

### Naming (locked on SD)

| File | Path |
|------|------|
| Workspace record | `/MidiLooper/current/workspace.bin` |
| Runtime bundle (interim) | `/MidiLooper/current/temp/runtime.bundle.bin` |
| Slot loops | `/MidiLooper/current/slots/loop_TT_SS.bin` |
| Catalog | `/MidiLooper/sets/index.bin` |
| Per-Set metadata | `/MidiLooper/sets/S####/set.bin` |
| Revision snapshot | `/MidiLooper/sets/S####/revisions/v####.bin` |

### Verification (green as of 2026-06-27)

```bash
pio test -e native                              # 251 tests
pio test -e native -f test_set_revision_persistence
openspec validate set-revision-persistence
pio run -e teensy41-capture-serial             # ask before upload
# HITL: revision_commit_save preset (see above)
```

---

## What is NOT done (start here)

### Section 3.3 — SNAPSHOT epoch freeze (shipped 2026-06-27)

| Item | Detail |
|------|--------|
| Source epoch | `resolveCompletedWorkspaceEpochForRevisionSnapshot` — idle: `currentWorkspaceEpoch`; deferred save in progress: `deferredSaveWorkspaceEpoch - 1` |
| Post-snapshot | `currentWorkspaceEpoch = sourceEpoch + 1` at SNAPSHOT; rollback on commit failure |
| Runtime bundle | Same epoch cap as loop slots via `slotSourceFileReadableForRevisionCommit` |
| Native | `test_revision_snapshot_source_epoch_*`, `test_revision_snapshot_bumps_live_epoch` |

### Section 3.4 — lastCommittedEpoch sync (shipped 2026-06-27)

| Item | Detail |
|------|--------|
| COMPLETE | `lastCommittedEpoch = currentWorkspaceEpoch` (clears dirty after 3.3 snapshot bump) |
| Deferred save | Writes `workspace.bin` with existing `lastCommittedEpoch` — no sync on epoch save |
| Load | ReloadRam sets `currentEpoch == lastCommittedEpoch` to new loaded epoch |
| Native | `test_last_committed_sync_after_revision_commit_complete_clears_dirty`, deferred-save dirty scenario |

### Section 3.5 — PLAYING budget + no materialize (shipped 2026-06-27)

| Item | Detail |
|------|--------|
| Budget | `PersistenceBudget::resolveMaxPersistenceMicros` — PLAYING/RECORDING/OVERDUBBING/capture → 300µs; idle → unbounded |
| Slice loop | `persistenceSliceBudgetExhausted` shared by deferred save and revision commit in `processDeferredSaveState` |
| No materialize | `RevisionCommitPolicy::kWritePathUsesLoopPassesMaterialize = false`; compile-time `static_assert` in StorageManager |
| Native | `test_revision_commit_playing_*`, slice budget tests, `test_revision_commit_write_path_does_not_materialize` |

### Section 3.6 — StorageLoopIo stream commit (shipped 2026-06-27)

| Item | Detail |
|------|--------|
| LoopSlot WRITE | `stepDeferredLoopPersist` + `measureLoopSlotFileBytes` from live RAM passes (chunk refs) |
| Layout | SlotIndex `bodyLength` from `measureLoopSlotFileBytes`, not opaque SD epoch file size |
| Transport | Still opaque copy of `runtime.bundle.bin` (split transport.bin deferred) |
| CRC | `LoopPersistPayloadCrc::RevisionCommit` on streamed loop body bytes |
| Native | `test_measure_persisted_loop_snapshot_wire_bytes_matches_buffer`, policy stream flag |

### Section 3 — hardening (priority order)

- [x] **3.6** Remove SavedSet shims; stream commit via `StorageLoopIo` / pass shapes (replace opaque copy)
- [x] **3.7** DIRTY_PROMPT Yes/No/Cancel — pipeline + minimal overlay display
- [x] **3.8** Boot: Current epoch → derived rev → latest → recovery → empty
- [ ] **3.9** **Parked** — 8h failsafe when epochs diverge > 8h (see `recovery-boot` spec; defer until field testing confirms revision-commit vs legacy SavedSet failsafe — `processSavedSetFailsafe` still runs today)

**Open engineering items (not separate tasks):**

- Incremental payload CRC during WRITE (optional; footer uses file read as source of truth)

### Sections 4–7

Overlay modes, loop picker, button remap, HITL `set_revision_overlay` — spec'd, not coded.

**Interim gap:** deferred FSM still writes single **runtime bundle** (`temp/runtime.bundle.bin`). OpenSpec target split `transport.bin` + `global.bin` is future work — do not block 3.2 on split.

---

## Architecture constraints (do not violate)

```text
current = mutable runtime workspace   →  MidiLooper/current/
sets    = immutable revision history  →  MidiLooper/sets/
```

- Extend `requestDeferredSaveState` / `processDeferredSaveState` — no parallel persistence worker
- Revision **WRITE** streams pass/chunk refs — **no** `LoopPasses::materialize` on save path
- Save SHALL NOT move, clear, or reload Current
- Runtime priority: MIDI → playback → clock → display → persistence (`maxPersistenceMicros`)
- **MIDI timing** is guaranteed by slice budget + chunk-bounded I/O, not by REVPK01 vs REVPK02 format

**Do NOT refactor:** StorageManager core FSM shape, `StorageLoopIo`, `LoopPasses`, chunk pool, stop-path materialize.

---

## Key files

| Area | Path |
|------|------|
| Path constants | `include/PersistenceLayout.h`, `CurrentSetStorage.h`, `CurrentWorkspaceStorage.h`, `SetRevisionCatalog.h` |
| REVPK02 wire + parser | `include/RevisionPackedBlob.h`, `src/RevisionPackedBlob.cpp` |
| Commit FSM + epoch | `src/StorageManager.cpp` (`stepRevisionCommit*`, `processDeferredSaveState` ~3300+) |
| HITL serial hooks | `src/main.cpp` (`processHitlSerialCommands`) |
| Workspace / catalog | `src/CurrentWorkspaceStorage.cpp`, `SetRevisionCatalog.cpp` |
| Native tests | `test/test_set_revision_persistence/` |
| HITL | `scripts/hitl/scenarios/revision_commit_save.py`, `revision_load.py`, `revision_load_post_record.py` |
| Overlay stub | `src/DisplayManager.cpp` (`drawLoadSaveView`) |
| OpenSpec | `openspec/changes/set-revision-persistence/` |

---

## Boot / recovery (shipped 3.8)

Order on boot:

1. Boot hygiene — discard incomplete `v####.bin.tmp` under `sets/S####/revisions/`
2. **Current workspace** — highest valid epoch (`loadCurrentWorkspaceAtBoot`)
3. **Derived revision** — `workspace.bin` provenance → synchronous deferred load
4. **Latest validated revision** on that Set (when derived fails or differs)
5. **Recovery checkpoints** — `MidiLooper/recovery/checkpoints/`
6. **Empty** Current

`BootRecoveryPolicy` — native-tested plan/fallback/epoch-scan helpers. Firmware: `validateEpochFileOnSd`, `discardIncompleteRevisionTempFilesOnSd`, `runBootRevisionLoadSynchronously`.

SavedSet fallback removed from boot chain (brownfield v5 monolith migration unchanged).

---

## Superseded (do not extend)

- `openspec/changes/workspace-session-persistence/` — flat SavedSet model
- `openspec/changes/currentset-savedset-storage-layout/` §2–3 — parked
- **REVPK01** — retired; no on-disk migration (dev stage)

---

### Section 3.7 — DIRTY_PROMPT (shipped 2026-06-27)

| Item | Detail |
|------|--------|
| Gate | `requestLoadRevision` → `rev_load_dirty_prompt` when `currentEpoch != lastCommittedEpoch` |
| **Yes** | `confirmRevisionLoadDirtyPromptSaveThenLoad` → commit then staged load (`rev_load_dirty_yes`) |
| **No** | `confirmRevisionLoadDirtyPromptDiscard` → discard uncommitted, load (`rev_load_dirty_no`) |
| **Cancel** | `cancelRevisionLoadDirtyPrompt` → clear staged target (`rev_load_dirty_cancel`) |
| Overlay | `getSetBrowserOverlayMode()` — `DirtyPrompt` / `MinimalLoading` / `Root` |
| Display | `drawLoadSaveDirtyPromptView`, `drawLoadSaveMinimalLoadingView` |
| HITL serial | `!REV_LOAD_DIRTY_YES`, `!REV_LOAD_DIRTY_NO`, `!REV_LOAD_DIRTY_CANCEL` |
| Native | `RevisionLoadPolicy` + 4 dirty-pipeline tests |

### Section 3.8 — Boot recovery chain (shipped 2026-06-27)

| Item | Detail |
|------|--------|
| Step 1 | `loadCurrentWorkspaceAtBoot` — scan down from `workspace.bin` epoch; ignore partial successor slot epochs |
| Step 2–3 | `attemptBootRecoveryChain` — derived revision then latest on Set via `runBootRevisionLoadSynchronously` |
| Step 4 | `tryLoadLatestRecoveryPoint` (unchanged path layout) |
| Hygiene | `discardIncompleteRevisionTempFilesOnSd` at `loadState` |
| Policy | `BootRecoveryPolicy` + 5 native tests |
| Removed | SavedSet newest-folder fallback from boot chain |

## Suggested next-chat prompt

```text
Read docs/Plans/storage_session_state_refactor_handoff.md (DEC-012).
Implement Tier 0: StorageActivitySnapshot, contract tests, remove RevisionLoadPolicy::isMinimalLoadingOverlayActive wrapper.
pio test -e native.

Or /opsx:apply set-revision-persistence — remaining tasks.md overlay items.
Read docs/Plans/set_revision_persistence_handoff.md first.
```
