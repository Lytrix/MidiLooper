# Clarifications — before clean implementation tasks

**Change:** `overlap-hidden-note-select`  
**Status:** Locked (2026-06-19) — user accepted all defaults **C1–C13**  
**Related:** [architecture-review.md](./architecture-review.md) §10, [tasks.md](./tasks.md)

---

## How to use this file

| Column | Meaning |
|--------|---------|
| **ID** | Reference in [tasks.md](./tasks.md) |
| **Resolution** | Locked decision for implementation |

---

## C1 — Two-baseline policy at fader-1 select

**Default:** **A** — baselineMap = `applyEdits(takes, edits)` replay; **focus.last** = live **session.store** at select.

**Resolution:** **Accepted default A** (2026-06-19)

---

## C2 — Selection identity: index vs **NoteRef**

**Default:** **B** — store **NoteRef** at fader-1 select (**focus.moving** after `rebuildNoteEditFocusAtSelect`); keep `selectedNoteIdx` as derived index into **`filterSelectableDisplayNotes`** for bracket UI.

**Resolution:** **Accepted default B** (2026-06-19)

---

## C3 — **Shortened** overlap notes in select navigation

**Default:** **Yes** — include **Shortened** overlap notes in fader-1 select slots.

**Resolution:** **Accepted default Yes** (2026-06-19)

---

## C4 — **Hidden** overlap notes on piano roll

**Default:** **Never drawn** — match **session.store** and serial.

**Resolution:** **Accepted default never drawn** (2026-06-19)

---

## C5 — **Inner** overlap notes (`innerUnderMovingNote`)

**Default:** **A** — exclude from **`filterSelectableDisplayNotes`** when `innerUnderMovingNote`.

**Resolution:** **Accepted default A** (2026-06-19)

---

## C6 — Delete pre-commit when selection ≠ prior moving note

**Default:** **A** — capture delete **NoteRef** → if ≠ **focus.moving**: `commitPendingOverlapNoteEdits` → `rebuildNoteEditFocusAtSelect` on target → **DeleteNote**.

**Resolution:** **Accepted default A** (2026-06-19)

---

## C7 — Encoder edit path on hardware

**Default:** **A** — port **EditStartNoteState** to `applyNoteEditChange(Move)` (thin adapter).

**Resolution:** **Accepted default A** (2026-06-19). Task 1.4 still records whether encoder is reachable on device (investigation only).

---

## C8 — **`MovingNoteIdentity` retirement scope**

**Default:** Delete entire **`movingNote`** struct after all reads migrated to **focus**.

**Resolution:** **Accepted default full delete** (2026-06-19)

---

## C9 — NOTE_EDIT vs session-active **`getCachedNotes`** migration

**Default:** Migrate when `noteEditSession.active` **or** `MAIN_MODE_NOTE_EDIT`.

**Resolution:** **Accepted default** (2026-06-19)

---

## C10 — **`commitPendingOverlapNoteEdits`** vs boundary module

**Default:** Keep through Phase 1; consolidate into single boundary module in Phase 3.

**Resolution:** **Accepted default** (2026-06-19)

---

## C11 — **`finalReconstructAndSelect`** index update

**Default:** Match moved note by **NoteRef** from **focus.last**, then derive filtered index.

**Resolution:** **Accepted default** (2026-06-19)

---

## C12 — Native suites **`test_delete_restore`** / **`test_shorten_delete_restore`**

**Default:** Delete suites in same PR as **EditStartNoteState** overlap removal (Phase 2d).

**Resolution:** **Accepted default** (2026-06-19)

---

## C13 — **`edit-record-display-length-mode`** ordering

**Default:** After Phase 1 select/filter stable.

**Resolution:** **Accepted default after Phase 1** (2026-06-19)

---

## C14 — Focus rebuild API name

**Default:** **`rebuildNoteEditFocusForDisplayNote`** (new function; do not pass filtered index into `rebuildNoteEditFocusFromStore`).

**Resolution:** **Accepted default** (2026-06-19)

---

## C15 — `SelectNavSlot.noteIdx` contract

**Default:** When **NoteEditSession.active**, `noteIdx` indexes **`filterSelectableDisplayNotes`** output (document in `SelectNavigation.h`).

**Resolution:** **Accepted default** (2026-06-19)

---

## Investigation outcomes (`212149` capture)

Recorded from read-only pass on `captures/host_midi_automation_edit_baseline_20260619_212149_serial.log` (2026-06-19).

### 1.1 Delete segment (tasks 1.1) — **C6 confirmed**

@ **111.702s** (`NOTELEN double: delete selected note`):

1. `Edit committed ChangeLength start=8 baselineEnd=680 newEnd=391` on **note=67 @ tick 8** (long mover / pending pitch shorten)
2. `commitAllPendingNoteEditActions` runs **before** focus aligns to user selection
3. `Deleting note pitch=67, start=8, end=391` — **not** B (`pitch=64 @ 200`)
4. `Note selection changed: 2 -> -1` — delete used **selectedNoteIdx=2** from unfiltered list, not captured **NoteRef** for B

**Fix target:** PRE-EXEC §2 / C6 sequence; capture **NoteRef** before any commit.

### 1.2 Third-note select / bracket (tasks 1.2) — **index ≠ tick**

@ **89.332s**:

- Log: `Select fader: selected note 3 at tick 200`
- Same moment `#CAP DNTE,60,584,584,96,3` — **selectedIdx 3 = pitch 60 @ start 584**, not pitch 64 @ 200

Bracket tick (200) disagrees with **selectedNoteIdx** into unfiltered `getCachedNotes()` order. Index 3 is **inner overlap** note (pitch 60 @ 584 inside long gate), not the note at tick 200.

**Hidden** overlap @ **81.303** (`Stored hidden overlap note: pitch=60, start=584`) was restored by **83.713** before this select — hop is **wrong index mapping**, not a ghost **Hidden** pair still in store. Filter must still exclude **innerUnderMovingNote** (C5) when inner P0 @ 392 stays visible (`Skipping overlap on inner overlap note`).

**Fix target:** filtered select list + **rebuildNoteEditFocusForDisplayNote** (C14); do not trust slot index into unfiltered cache.

### 1.3 Store vs cache vs display (tasks 1.3)

@ **81.303s** immediately after **Hidden** hide (`pitch=60 @ 584–680`):

| Source | Note count | Notes |
|--------|------------|-------|
| Session reconstruct (log) | **6** | Hidden pair absent — correct |
| `#CAP DISP` frameNotes | **6** | Matches session (`frameNotes` = `resolveDisplayNotes`) |
| `#CAP DISP` visualNotes | **7** | `loop.visualCache.notes` — **stale** (+1) |

**Conclusion:** Session store and **frame** display path agree after invalidation. Stale **`loop.visualCache`** (7 vs 6) is a display risk if any path reads visual cache during NOTE_EDIT. Phase 1 filter on **`resolveDisplayNotes`** / **`liveDisplayNotes`** is still required for **innerUnderMovingNote** and index contract; optional follow-up: do not use **visualCache** in NOTE_EDIT branch.

---

## Resolution log

| ID | Date | Decision |
|----|------|----------|
| C1 | 2026-06-19 | Accepted default **A** — committed baselineMap, live focus.last |
| C2 | 2026-06-19 | Accepted default **B** — NoteRef on select, derived filtered index |
| C3 | 2026-06-19 | Accepted default **Yes** — Shortened in select nav |
| C4 | 2026-06-19 | Accepted default **never drawn** — Hidden off piano roll |
| C5 | 2026-06-19 | Accepted default **A** — exclude innerUnderMovingNote from filter |
| C6 | 2026-06-19 | Accepted default **A** — scoped delete pre-commit |
| C7 | 2026-06-19 | Accepted default **A** — port encoder to applyNoteEditChange |
| C8 | 2026-06-19 | Accepted default **full delete** MovingNoteIdentity |
| C9 | 2026-06-19 | Accepted default — migrate when session active or NOTE_EDIT |
| C10 | 2026-06-19 | Accepted default — keep overlap-only commit until Phase 3 |
| C11 | 2026-06-19 | Accepted default — NoteRef match then filtered index |
| C12 | 2026-06-19 | Accepted default — delete legacy native suites in Phase 2d |
| C13 | 2026-06-19 | Accepted default — length-mode FSM after Phase 1 |
| C14 | 2026-06-19 | Accepted default — `rebuildNoteEditFocusForDisplayNote` |
| C15 | 2026-06-19 | Accepted default — filtered `SelectNavSlot.noteIdx` |
| C16 | 2026-06-19 | Accepted — Phase 1 sign-off = **BUG.md AC1–AC5** only; parent `serial_verification.edit.ok` sub-verifiers (insert/reorder, overlap round-trip, `m0_home`) **non-gating** unless AC1–AC5 fail |
| C17 | 2026-06-19 | Accepted — fader-1 select at **new tick** (no prior **focus.last** at step): **first filtered note at step**, not **focus.last** carry-over |
| C18 | 2026-06-19 | Accepted — **Demoted APIs** registry + grep gate in [PRE-EXECUTION.md](./PRE-EXECUTION.md) §11; positive-index **rebuildNoteEditFocusAtSelect** forbidden in firmware |

---

## C16 — Phase 1 sign-off scope

**Default:** Phase 1 complete when **AC1–AC5** pass on a fresh edit-baseline capture with `--verify-serial-log`. Do **not** block Phase 1 on parent edit baseline sub-verifiers already tracked elsewhere (insert/reorder, split-overlap round-trip, native m0 count).

**Resolution:** **Accepted** (2026-06-19)

---

## C17 — Select at new tick

**Default:** When fader-1 lands on a tick with no prior **focus.last** at that step, **resolveNoteIdxAtSlot** picks the **first** filtered **DisplayNote** at that step — not **movingNote** identity and not stale **focus.last** from a different tick.

**Resolution:** **Accepted** (2026-06-19)

---

## C18 — Demoted consumer paths (conflict retirement)

These paths caused **212149**-class bugs (wrong index, stale focus, delete on mover). Phase 1 **must not** reintroduce them in NOTE_EDIT fader/display/delete code.

| Demoted | Replacement | Firmware rule (Phase 1) |
|---------|-------------|-------------------------|
| `rebuildNoteEditFocusAtSelect(track, idx≥0)` | `rebuildNoteEditFocusForDisplayNote` | **Forbidden** — grep must show only `rebuildNoteEditFocusAtSelect(track, -1)` |
| Filtered/unfiltered index into `rebuildNoteEditFocusFromStore` from UI | `rebuildNoteEditFocusForDisplayNote` | UI must not call with fader-1 index |
| `getCachedNotes()[selectedNoteIdx]` in NOTE_EDIT fader paths | `selectableDisplayNotesForEditUi` | **NoteEditManager** fader/select/delete/pitchbend — no bare cache when session active |
| Unfiltered list in `buildSelectNavigationSlots` | `selectableDisplayNotesForEditUi` | Done |
| Unfiltered display in NOTE_EDIT | `filterSelectableDisplayNotes` → `liveDisplayNotes` | Done when session active |
| Delete: `commitAllPending` → `rebuildNoteEditFocusAtSelect(idx)` → overlap commit | C6 order in `deleteSelectedNote` | Done |
| `resolveNoteIdxAtSlot` prefers **movingNote** at same tick | **focus.last** at same tick; **C17** at new tick | Select path updated; **movingNote** fallback removed in Phase 2a |

**Allowed exceptions (not violations):**

- `selectableDisplayNotesForEditUi` / `DisplayManager::resolveDisplayNotes` when `!isNoteEditActive()` — falls back to `getCachedNotes()`.
- `rebuildNoteEditFocusFromStore` in native tests and inside `rebuildNoteEditFocusAtSelect(-1)` clear path.
- **Phase 2 deferrals** (encoder FSM, `EditManager::selectNextNote`, `MidiFaderProcessor`) — listed in PRE-EXEC §11; do not extend demoted patterns there.

**Resolution:** **Accepted** (2026-06-19)
