# Design — Note edit session consumer contract

**Change:** `overlap-hidden-note-select`  
**Status:** Proposed (2026-06-19)  
**Evidence:** [BUG.md](./BUG.md), [architecture-review.md](./architecture-review.md)  
**Parent:** [note-edit-modification-session/design.md](../note-edit-modification-session/design.md)

**Naming:** reuse **`filterSelectableDisplayNotes`** (D1), **`DisplayNote`**, **`NoteRef`**, **select navigation** — no new module nouns ([Naming-Vocabulary-Teensy-Looper.mdc](../../.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc)).

**Clarifications:** [CLARIFICATIONS.md](./CLARIFICATIONS.md) locked 2026-06-19 (C1–C13 defaults accepted).

---

## Context

Parent change shipped **one overlap write owner** on the **fader path** (`applyNoteEditChange` +
**focus.overlapNotes**). **Read-side consumers** and **encoder path** were never migrated. See
architecture-review §2.

This design defines the **target session architecture** and **Phase 1** (D1–D5). Phase 2–3 in
[tasks.md](./tasks.md).

---

## Target architecture

```text
PERSISTED          Takes + Edits[]
                        │
                        │ applyEdits (commit boundary only)
                        ▼
LIVE WRITE         NoteEditSession.store ◄── applyNoteEditChange
                        │
                        ├── focus (commitBaseline, movingNoteRange, overlapNotes, baselineMap)
                        │
                        ▼
READ (NOTE_EDIT)   reconstructNotes → filterSelectableDisplayNotes (D1)
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
   Display         Select nav       Delete / bracket
   (NOTE_EDIT)     (fader-1)        (NoteRef target)

FORBIDDEN in PREVIEW: commitEditAction / full rematerialize on fader tick
```

**Serial / MIDI playback** reads **session.store** directly (unchanged). **UI** uses
**`filterSelectableDisplayNotes`**.

---

## Goals / Non-Goals

**Goals:**

- One **filterSelectableDisplayNotes** path for all NOTE_EDIT note list consumers
- **Delete** boundary: capture **NoteRef** before commit; no wrong-mover pre-commit
- Document **two-baseline** policy (committed **baselineMap** vs live **focus.last**)
- Phase 2 pointer to retire **movingNote.deletedNotes** (parent 8.1)

**Non-Goals:**

- Overlap classification changes in `NoteMovementUtils`
- New type names (`*View`, `*Inventory`, `audibleNotes`, etc.)

---

## Decision E0 — Architecture milestone (not a filter-only bug)

**Choice:** Session **consumer contract** correction. D1 filter is the implementation anchor, not a
throwaway patch.

---

## Decision E1 — Two baselines (normative)

| Field | Source at fader-1 select |
|-------|--------------------------|
| **baselineMap** | `applyEdits(takes, edits)` materialization |
| **focus.last** | Live **DisplayNote** at selected index from **session.store** |
| **commitBaseline** | **baselineMap** entry for selected **NoteRef** |

---

## Decision E2 — `filterSelectableDisplayNotes` (D1)

**Choice:** Add on **NoteEditFocus**:

```text
filterSelectableDisplayNotes(sessionEvents, focus, loopLength)
  → start: NoteUtils::reconstructNotes(sessionEvents, loopLength, false)
  → drop: DisplayNote matching overlapNotes[ref].state == Hidden
  → keep: Shortened overlap notes at live gate (TBD-1 default)
```

Optional helpers (action + scope, only if needed):

- `noteRefAtFilteredDisplayNoteIndex(filtered, i)`
- `filteredDisplayNoteIndexForNoteRef(filtered, ref)`

**Rationale:** Reuses **DisplayNote** and **overlapNotes**; no new domain noun.

---

## Decision E3 — Consumer migration

During active **NoteEditSession** + NOTE_EDIT:

| Consumer | Must use |
|----------|----------|
| `DisplayManager::resolveDisplayNotes` | `filterSelectableDisplayNotes` |
| `buildSelectNavigationSlots` | `filterSelectableDisplayNotes` |
| `deleteSelectedNote` | **NoteRef** from filtered list |
| Bracket / `selectedNoteIdx` | Index into filtered **DisplayNote** list or stored **NoteRef** |

---

## Decision E4 — Delete commit boundary

1. Resolve delete **NoteRef** from filtered list at `selectedNoteIdx` — fail closed if invalid
2. If `focus.moving` ≠ delete target: overlap-only persist; rebuild focus on target; no unrelated **ChangeLength**
3. **DeleteNote** on target from live store
4. Clear selection / invalidate caches

---

## Decision E5 — PREVIEW vs boundary

| Phase | Allowed |
|-------|---------|
| PREVIEW (fader tick) | `applyNoteEditChange`, in-pass undo |
| BOUNDARY | pre-commit resolve → `saveEdit` → rematerialize |

---

## Decisions D1–D5 (Phase 1)

Same as prior design — D1 = E2; D2 select nav; D3 = E4 delete; D4 display; D5 bracket hop.

---

## Sequencing

See [tasks.md](./tasks.md).
