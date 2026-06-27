## 1. Milestone M1 — CurrentSet persistence + boot recovery

- [x] 1.1 Add `RtcTime` module (SNVS init, `getUnixTime`, `formatLastActive`, fallback when no battery).
- [x] 1.2 Define v6 `workspace.bin` schema including dirty anchor fields (`loadedFromSequence`, `lastAnchoredSequence`, `lastMaterialChangeUnix`, `hasMaterialChangesSinceAnchor`).
- [x] 1.3 Implement `writeCurrentSetMeta` / `readCurrentSetMeta` with temp → verify → rename under `Sets/_current/`.
- [x] 1.4 Implement `writeCurrentSetLoopSlot` / `readCurrentSetLoopSlot` per `Sets/_current/loop_TT_SS.bin` (2-digit zero-padded) with per-file `STORAGE_COMPLETE_MAGIC`.
- [x] 1.5 Refactor `DeferredSaveStage` FSM: retarget from monolith to CurrentSet meta + per-slot files (one slot step per slice).
- [x] 1.6 Boot path: `loadCurrentSet` with per-slot integrity validation; sanitize recording states on load.
- [x] 1.7 v5 monolith migration: load legacy `/midilooper_state.raw`, write `Sets/_current/`, quarantine legacy file.
- [x] 1.8 Boot recovery chain stub: on CurrentSet fail, attempt latest RecoveryPoint under `Sets/_current/checkpoints/` then newest SavedSet (full RecoveryPoint creation in M4).
- [x] 1.9 Native: `test_current_set_storage` — meta round-trip, `loop_00_00.bin` round-trip, completion marker fail-hard.
- [x] 1.10 Native: `test_v5_migration` — fixture monolith → `Sets/_current/` tree with 2-digit loop names.
- [x] 1.11 HITL: canonical record/overdub baseline unchanged after M1 (`PERS,result,...,ok`, transition set).
- [x] 1.12 Update `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md` for v6 CurrentSet targets.

## 2. Milestone M2 — SavedSet snapshots

- [x] 2.1 Implement **SetIndex** at `Sets/index.bin`; reconcile sequence from date-form and UID-form folders.
- [x] 2.2 Implement hybrid folder naming: `YYMMDD_NNN` when RTC ≥ 2026-01-01, else 5-digit UID; sequence from index only.
- [x] 2.3 Implement `saveNewSet`: allocate sequence → create folder → copy CurrentSet; increment index after success; exclude `checkpoints/`.
- [x] 2.4 SavedSet `set.bin`: `sequence`, `folderNamingMode`, label, `createdAtUnix`, summary stats.
- [x] 2.5 Default list label: full date (e.g. `25 June 2026`) when no user label + valid `createdAtUnix`; else UID.
- [x] 2.6 Document SAVE NEW gesture in `MidiButtonActions` (TBD).
- [x] 2.7 Native: `test_saved_set_catalog` — hybrid naming, RTC threshold, reconcile, checkpoints excluded.
- [x] 2.8 Implement eight-hour failsafe: track CurrentSet last-modified + last SavedSet time; auto **saveNewSet** in idle maintenance when gating allows.
- [x] 2.9 Native: failsafe creates next sequence; skips when CurrentSet unchanged; defers during capture.
- [x] 2.11 Implement **loadSetIntoCurrent**: auto **saveNewSet** when dirty; copy SavedSet → `_current`; update provenance meta.
- [x] 2.12 Wire dirty anchor updates on record/overdub/edit/clear/import (same hooks as deferred save).
- [x] 2.13 Display: CURRENT always highlighted; `From:` provenance; brief toast on auto-save before load.
- [x] 2.14 Native: load dirty CurrentSet → auto SavedSet created → `_current` replaced; clean load skips auto-save.

## 3. Milestone M3 — Slot loop import

- [ ] 3.1 Long-press slot gesture → IMPORT LOOP mode for target `LoopLocation`.
- [ ] 3.2 Source picker: CurrentSet, last 3 SavedSets, Browse All (filter `_current` and `checkpoints/`).
- [ ] 3.3 Loop grid picker within selected source (slot-centric UX; track internal).
- [ ] 3.4 Import execution: read source `loop_TT_SS.bin` → apply to target loop → `invalidateCaches()` → `requestDeferredSaveState`.
- [ ] 3.5 Native: import copies geometry + passes; sibling slots unchanged.
- [ ] 3.6 HITL: record on track 5 → saveNewSet → import loop into empty slot on track 5 → verify note count and span.

## 4. Milestone M4 — Recovery points + polish

- [ ] 4.1 Implement `RecoveryPointManager`: create `Sets/_current/checkpoints/_YYMMDD_HHMM/` from CurrentSet snapshot.
- [ ] 4.2 Wire triggers: before clear slot, clear track, import overwrite, **loadSetIntoCurrent**.
- [ ] 4.3 Prune policy under `checkpoints/`: retain newest 3 + latest pre-destructive.
- [ ] 4.4 Complete boot recovery: RecoveryPoint → restore `Sets/_current/`; SavedSet fallback.
- [ ] 4.5 Implement `saveCopySet`.
- [ ] 4.6 Set browser detail view (full UX: label, date, bars, tracks, filled, per-track bars).
- [ ] 4.7 CURRENT status line + `From:` provenance + auto-save toast in `DisplayManager`.
- [ ] 4.8 Manual: corrupt `Sets/_current/loop_00_00.bin` → boot recovers from RecoveryPoint.
- [ ] 4.9 Optional low-frequency periodic RecoveryPoint (24 h dirty CurrentSet) — implement if time permits.

## 5. Milestone M5 — Favorites (future)

- [ ] 5.1 Loop favorite tag in SavedSet meta (non-goal until M1–M4 archived).
- [ ] 5.2 Favorites section in IMPORT LOOP source picker.

## 6. Verification and closeout

- [x] 6.1 Run `pio test -e native` (all new suites green).
- [ ] 6.2 HITL: baseline after M1; import scenario after M3.
- [x] 6.3 Run `openspec validate workspace-session-persistence`.
- [ ] 6.4 Update `docs/DELIVERABLE_TRACKING.md` row 80 (load/save sets) on archive.
- [ ] 6.5 `/opsx:archive` when M1–M4 gates pass (M5 may remain open as follow-up change).

**Apply order:** M1 → M2 → **see `set-revision-persistence`** for M3+ (this file's M3–M4 superseded).

> **Superseded 2026-06-26:** M3 slot import, M4 RecoveryPoint polish, auto-save-before-load →
> `openspec/changes/set-revision-persistence/`. See `SUPERSEDED.md`.

**Non-goals reminder:** updateLastSet, jam/Scenes SD fields, lazy slot load, open EditSession RAM restore, SD noun **Session** / `currentSession`.

**SD layout (canonical):**

```text
/Sets/index.bin
/Sets/_current/...
/Sets/00001/                    # UID sequence 001 when RTC invalid
/Sets/260625_001/               # date form; sequences start at _001 (_000 = CurrentSet)
```
