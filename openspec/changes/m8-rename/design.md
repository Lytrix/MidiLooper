## Context

Vocabulary is locked in `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc` and global
**action + scope** naming rules. This change is **rename-only** so M8 edit storage lands on
**Take** / **Capture** / **TakeCommitted** without mixed semantic diffs.

## Goals

- Replace **Epoch** family with **Take** family in product code, headers, SD v4 names, undo kinds
- Replace **CaptureLayer** with **Capture**
- Rename **`SessionCapture`** → **`DebugSessionCapture`**
- Free **`EditState`** name for persisted **Edit** enum (Active | Disabled) in **m8-edit**
- Grep gate: no new **`Epoch`** / **`CaptureLayer`** / **`EpochPublished`** in firmware or native tests

## Non-Goals

- **Edit** / **EditChange** / **NoteEditSession** implementation
- Removing **`editFlat_`** bridge
- OpenSpec archive **`timeline-epochs` → `timeline-takes`** (ships with **m8-edit**)
- JamSession / PerformanceSession naming

## Decisions

### 1. Mechanical rename, behavior frozen

Same call graph and storage layout; only identifiers, log strings, SD JSON keys, and test names
change. Any bugfix discovered during rename goes in a separate commit or follow-up.

### 2. Take vocabulary mapping

| Was | Now |
|-----|-----|
| `Epoch` | `Take` |
| `EpochId` | `TakeId` |
| `EpochKind` | `TakeType` |
| `EpochState` | `TakeState` |
| `epochs[]` | `takes[]` |
| `publishEpoch` / seal wording | **`commitTake()`** |
| `EpochPublished` | **`TakeCommitted`** |
| `CaptureLayer` | **`Capture`** |
| `include/Epoch.h` | `include/Take.h` (or equivalent) |

### 3. EditState UI collision

Today:

- `class EditState` — abstract note-edit FSM base
- `class EditNoteState : public EditState` — one concrete state
- Other states (`EditSelectNoteState`, …) extend **`EditState`** directly

**Target:** abstract base renamed so **`EditState`** is available for struct **Edit** lifecycle
enum in **m8-edit**. Resolve the existing **`EditNoteState`** subclass name collision in the
rename PR (e.g. re-home or rename the concrete class) — document the chosen mapping in the PR.

### 4. Test renames

Rename suites/files that encode **epoch** in the name when they test take/capture behavior
(`test_loop_epoch_survival` → `test_loop_take_survival`, etc.). Keep test assertions behavior-identical.

## Exit criteria

1. `rg` gate: no `Epoch`, `CaptureLayer`, `EpochPublished` in `src/`, `include/`, `test/` (except
   comments noting migration or archived docs)
2. `pio test -e native` — all green
3. Firmware builds: `pio run -e teensy41`
4. **`openspec validate m8-rename`** passes
5. User may run optional HITL baseline smoke (no new pass criteria beyond pre-rename behavior)

## Risks

| Risk | Mitigation |
|------|------------|
| EditState / EditNoteState name clash | Resolve in rename PR before **m8-edit** adds struct **Edit** |
| SD v4 on-disk compatibility | Rename keys only if loader accepts both or bumps minor doc; no layout change |
| Large diff | Single-purpose PR; no edit-model commits mixed in |

## Handoff to m8-edit

After rename merges, **`m8-edit`** implements **Edit**, **NoteEditSession**, span boundaries,
bridge removal, and archives **`timeline-takes`**.
