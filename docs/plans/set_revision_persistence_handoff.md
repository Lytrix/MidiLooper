# Handoff — Set / Revision persistence (next chat)

**Date:** 2026-06-27  
**OpenSpec:** `openspec/changes/set-revision-persistence/`  
**Architecture plan:** `docs/plans/set_revision_persistence_architecture_enhancement.md`  
**Apply command:** `/opsx:apply` on `tasks.md` — **next: section 3.x**

---

## One-line goal

**Current** (mutable, epoch-based) + **Sets** (immutable revisions). Save appends packed `v####.bin` snapshots via deferred FSM; Current unchanged after Save. Extend existing pass/chunk storage — **no** StorageManager / LoopPasses rewrite.

---

## What is done (tasks 1.1–2.4)

### Section 1 — metadata + catalog + REVPK01

| Task | Deliverable |
|------|-------------|
| 1.1 | Wire structs: `WorkspaceMetaRecord`, `SetCatalogIndex`, `SetMetaRecord`, epoch file header |
| 1.2 | `SetRevisionCatalog` — `index.bin`, `set.bin`; revision id on COMPLETE only |
| 1.3 | Monotonic Set id; failed revision reuses id |
| 1.4 | `RevisionPackedBlob` — REVPK01 128 B header, loop index, 12 B footer |
| 1.5 | Native: `test_set_revision_persistence` (17 tests) |

**New modules:** `PersistenceLayout`, `PersistenceSchema`, `CurrentWorkspaceStorage`, `SetRevisionCatalog`, `RevisionPackedBlob`

### Section 2 — current workspace on SD

| Task | Deliverable |
|------|-------------|
| 2.1 | Epoch headers on current files; `workspace.bin` at deferred-save completion |
| 2.2 | Roots `/MidiLooper/current/` + `/MidiLooper/sets/`; deferred FSM retargeted |
| 2.3 | `SlotSummary[8]` in `workspace.bin` for overlay preview |
| 2.4 | Native: epoch valid/invalid; dirty = `currentEpoch != lastCommittedEpoch` |

**Extended (not replaced):** `StorageManager.cpp` deferred FSM — epoch increment at job start, header prefix on bundle + slot files, `writeWorkspaceMetaAfterDeferredSave()`.

### Naming (locked on SD)

| File | Path |
|------|------|
| Workspace record | `/MidiLooper/current/workspace.bin` |
| Runtime bundle (interim) | `/MidiLooper/current/temp/runtime.bundle.bin` |
| Slot loops | `/MidiLooper/current/slots/loop_TT_SS.bin` |
| Catalog | `/MidiLooper/sets/index.bin` |
| Per-Set metadata | `/MidiLooper/sets/S####/set.bin` |
| Revision snapshot | `/MidiLooper/sets/S####/revisions/v####.bin` |
| Human settings (future) | `/MidiLooper/system/settings.json` |

Runtime records use `.bin` (fixed layout / memcpy). Internal C++ names may still say `Meta` (e.g. `kWorkspaceMetaPath` → **`workspace.bin`** on disk).

Legacy flat SavedSet folders (brownfield): `set.bin` via `CurrentSetStorage::kSetBinFileName` under `sets/archive/` until task **3.6**.

### Verification (green as of 2026-06-27)

```bash
pio test -e native                              # 229 tests
pio test -e native -f test_set_revision_persistence
openspec validate set-revision-persistence
pio run -e teensy41-capture-serial             # ask before upload
```

---

## What is NOT done (start here)

### Section 3 — revision commit and load (priority)

- [ ] **3.1** Commit FSM stages on deferred infrastructure; `maxPersistenceMicros` slices
- [ ] **3.2** Deferred load → new Current epoch; provenance after 100%
- [ ] **3.3** SNAPSHOT freezes completed epoch only
- [ ] **3.4** `lastCommittedEpoch` sync at COMPLETE only
- [ ] **3.5** Native: commit during PLAYING uses budget; no materialize on WRITE
- [ ] **3.6** Remove SavedSet shims; stream via `StorageLoopIo` / pass shapes
- [ ] **3.7** DIRTY_PROMPT Yes/No/Cancel
- [ ] **3.8** Boot: Current epoch → derived rev → latest → recovery → empty
- [ ] **3.9** 8h failsafe when epochs diverge > 8h

### Sections 4–7

Overlay modes, loop picker, button remap, HITL `set_revision_overlay` — spec'd, not coded.

**Interim gap:** deferred FSM still writes single **runtime bundle** (`temp/runtime.bundle.bin`). OpenSpec target split `transport.bin` + `global.bin` is future work within 2.x/3.x — do not block 3.1 on split.

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

**Do NOT refactor:** StorageManager core FSM shape, `StorageLoopIo`, `LoopPasses`, chunk pool, stop-path materialize.

---

## Key files

| Area | Path |
|------|------|
| Path constants | `include/PersistenceLayout.h`, `CurrentSetStorage.h`, `CurrentWorkspaceStorage.h`, `SetRevisionCatalog.h` |
| Deferred FSM + epoch | `src/StorageManager.cpp` (~300 epoch vars; ~1035–2020 FSM; `writeWorkspaceMetaAfterDeferredSave`) |
| Workspace / catalog / REVPK01 | `src/CurrentWorkspaceStorage.cpp`, `SetRevisionCatalog.cpp`, `RevisionPackedBlob.cpp` |
| Native tests | `test/test_set_revision_persistence/`, `test/test_current_set_storage/` |
| Overlay stub | `src/DisplayManager.cpp` (`drawLoadSaveView`) |
| OpenSpec tasks | `openspec/changes/set-revision-persistence/tasks.md` |

---

## Boot / recovery (spec — partial firmware)

Order: (1) highest valid `MidiLooper/current/` epoch → (2) exact derived `v####.bin` → (3) latest validated on Set → (4) `recovery/checkpoints/` → (5) empty. Partial epochs ignored.

---

## Superseded (do not extend)

- `openspec/changes/workspace-session-persistence/` — flat SavedSet model (docs updated to `workspace.bin` / `set.bin` naming where touched)
- `openspec/changes/currentset-savedset-storage-layout/` §2–3 — parked

---

## Suggested next-chat prompt

```text
/opsx:apply set-revision-persistence — start task 3.1 (revision commit FSM on deferred save).
Read docs/plans/set_revision_persistence_handoff.md first.
Hook SNAPSHOT → WRITE → VALIDATE → CATALOG_UPDATE; no materialize on save path.
Run pio test -e native after 3.5. Ask before Teensy upload.
```
