# Design — pass vocabulary

**Change:** `m8-pass-vocabulary`  
**Status:** Locked (2026-06-19)

---

## Unified pass model (audio/MIDI time)

A **pass** is one bounded performance or edit stretch that ends in a **commit** and (where
applicable) a global undo entry.

```text
Loop slot timeline (conceptual)

  record pass     Capture → commitTake() → Record Take     → TakeCommitted undo
  overdub pass    Capture → commitTake() → Overdub Take    → TakeCommitted undo
  edit pass       saveEdit()×N → closeNoteEditPass()       → NoteEditSessionCommitted undo
```

**edit pass** is nested inside **NoteEditSession** (note edit mode), not a separate session:

```text
NoteEditSession (mode open)
├── edit pass 0   (editPassIndex = 0)
│     └── focus cycles: select note → modify → saveEdit …
├── [overdub while editing] closeNoteEditPass()
├── edit pass 1   (editPassIndex = 1)
└── exit note edit → closeNoteEditPass()
```

---

## edit pass vs focus vs moving note range

| Term | Domain | Role |
|------|--------|------|
| **NoteEditSession** | Mode | RAM store + undo while in note edit |
| **edit pass** | Time / undo batch | **Edit**s grouped between overdub or exit boundaries |
| **focus** | Single note | Moving note select → modify → pre-commit resolve |
| **moving note range** | Tick range on **NoteEditFocus** | Moving note start..end for overlap note classification |

Do **not** use **pass** for moving note range or for focus lifecycle.

---

## Global undo (unchanged mechanics)

**NoteEditSessionCommitted** stores **EditId** list for one **edit pass**. Undo calls
`disableEdits(editIds)` — not a scan by index name.

The index field (today `spanIndex`, target `editPassIndex`) tags each **Edit** for SD and logging.

---

## Legacy → target identifiers

| Legacy (shipped) | Target | Notes |
|------------------|--------|-------|
| `spanIndex` | `editPassIndex` | `NoteEditSession`, `Edit` struct |
| `Edit.spanIndex` | `Edit.editPassIndex` | SD v4 field — rename with format note or v5 |
| `closeNoteEditSpan()` | `closeNoteEditPass()` | Boundary flush |
| `noteEditSpanIndex` | `noteEditPassIndex` | `UndoEntry` |
| `spanEditIds` | `passEditIds` | ids collected for current edit pass |
| prose **span** | **edit pass** | docs, rules, OpenSpec |
| `overlapFootprint` / **overlap footprint** | **movingNoteRange** / **moving note range** | **NoteEditFocus** only |
| overlap **ledger** / `deletedNotes` | **overlapNotes** | **NoteEditFocus**; not **ActiveNoteLedger** (playback) |

Keep **NoteEditSessionCommitted** — already session-scoped, not span-named.

---

## Words to avoid

| Avoid | Use instead |
|-------|-------------|
| **span** (new prose/code) | **edit pass** / **editPassIndex** |
| **layer** for edit batches | **edit pass** (layer = Take / overdub performance) |
| **sessionSpan** / **session span** | **moving note range** on focus |
| **footprint** | **moving note range** on **NoteEditFocus** |
| **ledger** (overlap hide/restore) | **overlapNotes** |
| **flat** in new identifiers | **loop MIDI events**, **session store events**, **Takes + Edits replay** |
| **chapter** | **edit pass** |

Compaction “tick/bar span” in m8-edit → qualify as **compaction window** in prose.

---

## Relationship to record / overdub pass

Prose may say “record pass” / “overdub pass” when describing **Take** commit — parallel to
“edit pass” for **Edit** commit. Code keeps **TakeType::Record** / **Overdub** and
**commitTake()**; **pass** is the time-domain umbrella in docs and UX copy.
