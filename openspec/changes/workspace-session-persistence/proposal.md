## Why

The firmware persists all runtime state in a single monolithic SD file (`/midilooper_state.raw` v5)
that is continuously overwritten by the deferred save path. There is no explicit live set container,
no user SavedSet snapshots, no crash-recovery layer beyond quarantine-on-bad-load, and no
slot-level loop import from saved material. [`docs/DELIVERABLE_TRACKING.md`](../../../docs/DELIVERABLE_TRACKING.md)
lists "Display + Button workflow to load/save sessions" as **Needs spec**.

A 3V backup battery on the Teensy enables SNVS RTC timestamps and makes a **persistent instrument
with explicit set commits** the right model now: **CurrentSet** is always the live loop inventory;
**SavedSet** snapshots are intentional anchors; **RecoveryPoint** checkpoints under
`Sets/_current/checkpoints/` are hidden safety nets. **NoteEditSession** and future **JamSession**
remain RAM-only modes on top of the loaded set — SD persistence uses **Set**, not bare "Session".

## What Changes

- **CurrentSet persistence:** Replace monolithic v5 autosave target with `Sets/_current/` on SD —
  `workspace.bin` (global transport + slot tables + undo) and per-slot `loop_TT_SS.bin` files
  (zero-padded 2-digit track/slot, e.g. `loop_00_07.bin`) using existing `StorageLoopIo` loop
  blobs. CurrentSet loads on every boot and is never user-deleted.
- **SavedSet snapshots:** User-initiated SAVE NEW copies CurrentSet into immutable SavedSet folders
  using hybrid naming: `Sets/YYMMDD_NNN/` when RTC is valid on or after 2026-01-01 (e.g.
  `260625_001`), else `Sets/NNNNN/` UID form (e.g. `00001`). Sequences start at **001**; **000**
  reserved for **CurrentSet**. Sequence from `Sets/index.bin`. **Eight-hour failsafe** auto
  SavedSet when CurrentSet modified without snapshot. **checkpoints/** excluded from copy.
- **RecoveryPoint layer:** Hidden `Sets/_current/checkpoints/_YYMMDD_HHMM/` folders created on
  destructive actions and other triggers; boot fallback chain CurrentSet → RecoveryPoint → newest
  SavedSet.
- **loadSetIntoCurrent:** Load SavedSet into `Sets/_current/` + RAM. When CurrentSet has material
  changes since last anchor, auto **saveNewSet** runs first (no 8-hour wait, no blocking confirm).
  Optional brief UI toast. **RecoveryPoint** before overwrite is secondary. UI always highlights
  **CURRENT**; `From:` shows `loadedFromSequence`.
- **Slot loop import:** Long-press slot → IMPORT LOOP → pick source (CurrentSet, recent SavedSets,
  browse all) → load loop into target **LoopLocation** only.
- **Set browser display:** CURRENT status line, SavedSet list, detail view (bars, tracks, filled
  slots, per-track fill bars). Catalog filters reserved `_current` and `checkpoints/`.
- **SetIndex registry:** `Sets/index.bin` with `nextSequence` — authoritative sequence; folder name
  format switches on RTC validity (≥ 2026-01-01).
- **RTC integration:** `lastActiveUnix`, SavedSet `createdAtUnix`, and date-form folder names when
  RTC valid; UID folders when RTC unset or before 2026-01-01. Default list label: full date (e.g.
  `25 June 2026`) when no user label and RTC valid at save.
- **v5 → v6 migration:** One-time load of legacy monolith, write `Sets/_current/`, quarantine old file.
- **BREAKING:** Container format bumps from v5 monolith to v6 `/Sets/` layout (v5 remains loadable
  once for migration).

**Non-goals (v1 / this change):**

- Jam entity, Scenes, or jam-field persistence in SavedSet meta (M10 — unchanged).
- D13 arrangement MIDI capture (parked).
- **UPDATE LAST** SavedSet overwrite (deferred to v2).
- Loop **favorites** star tag (M5 follow-up).
- Lazy-load of inactive slots (all 8×8 loaded at boot — same as today).
- Open **EditSession** RAM restore on power loss (committed **editPasses** only — same as today).
- Using `currentSession` or bare **Session** for SD persistence nouns.

## Capabilities

### New Capabilities

- `current-set-persistence`: Always-active CurrentSet at `Sets/_current/`; per-slot atomic writes;
  continuous deferred writer retargeted from monolith; boot load + integrity validation.
- `saved-set`: Hybrid folder naming from `_001` / `00001`; **SetIndex**; manual **saveNewSet** and
  eight-hour failsafe **saveNewSet**; default date labels; excludes checkpoints.
- `recovery-point`: Hidden checkpoints under `Sets/_current/checkpoints/`; trigger rules;
  auto-prune; boot fallback below CurrentSet.
- `slot-loop-import`: Long-press IMPORT LOOP; source navigation; copy loop blob into target slot.
- `set-browser-display`: CURRENT status, SavedSet list, detail view; button/gesture hooks.

### Modified Capabilities

- `storage-loop-io`: Per-file loop blob container; v6 layout; `loop_TT_SS.bin` naming; migration
  from inline monolith pool.
- `long-record-memory-headroom`: Deferred save FSM stages target CurrentSet slot files instead of
  monolith byte offset.
- `multi-loop-slots`: Slot as loop-import target; **LoopLocation** resolution rules.
- `loop-temporal-persistence`: Per-slot load from CurrentSet vs SavedSet folder paths.

## Impact

| Area | Primary files |
|------|---------------|
| SD orchestration | `StorageManager.cpp`, CurrentSet helpers (same module initially) |
| Loop wire | `StorageLoopIo.cpp` (reuse `PersistedLoopSnapshot` per file) |
| Boot | `Looper.cpp`, `main.cpp` |
| RTC | New `RtcTime.cpp` / `RtcTime.h` (SNVS) |
| Set catalog | New `SetCatalog.cpp` (or `StorageManager` submodule) |
| Recovery | `RecoveryPointManager` under `Sets/_current/checkpoints/` |
| Display / UX | `DisplayManager.cpp`, `MidiButtonActions.cpp` |
| Tests | `test_current_set_storage`, `test_saved_set_catalog`, `test_v5_migration`; extend `test_storage_loop_io` |

**Apply order:** After shipped M8, pool-budget, and long-record-memory-headroom. May proceed
before or parallel with `jam-recorder` — set storage does not require jam capture. Coordinate
display work with active `long-loop-piano-roll-window` (display-only; separate PRs).

**Brownfield references:** [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md),
[`docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`](../../../docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md),
[`docs/Plans/phase-3-multi-loop.md`](../../../docs/Plans/phase-3-multi-loop.md).

## Open decisions (TBD)

| Decision | Default in this change |
|----------|------------------------|
| SAVE / LOAD button mapping on DROID | TBD — document in M2 before UX ships |
| Global undo in SavedSet snapshot | Yes — parity with today's monolith |
| Loop file naming | `loop_TT_SS.bin` — 2-digit zero-padded track and slot (supports future 16×16) |
| SavedSet sequence | Starts at **001**; **000** reserved for **CurrentSet** (`_current`) |
| Eight-hour failsafe | Auto **saveNewSet** when CurrentSet modified ≥ 8 h without new SavedSet |
| SetIndex registry | `Sets/index.bin` — `nextSequence`; ordering always from sequence, not RTC |
| Default SavedSet list label | Full date (e.g. `25 June 2026`) when no user label + valid `createdAtUnix`; else UID |
| RecoveryPoint retain count | Newest 3 + one pre-destructive (TBD in design) |
| `_current` folder spelling | `_current` (lowercase) — single canonical spelling on SD |
