# Design — timeline pass model

**Change:** `timeline-pass-model`  
**Status:** Proposed (vocabulary locked 2026-06-20)

## Recommendation — four pass kinds, two storage families (locked)

**Four pass kinds** in product vocabulary and logs:

| Pass kind | Storage family | In **`passes[]`** |
|-----------|----------------|-------------------|
| **recordPass** | capture (chunks) | `optional<RecordPass>` — 0 or 1 |
| **overdubPass** | capture (chunks) | `overdubPasses[]` — many, `mergeSequence` |
| **noteEditPass** | edit (op-list) | **`editPass`** row with **`EditPassKind::NoteEdit`** |
| **controlChangeEditPass** | edit (op-list) | **`editPass`** row with **`EditPassKind::ControlChange`** (future) |

**Do not** merge record+overdub into one **`capturePass`** struct — **recordPass** has distinct
cardinality (0-or-1 foundation) and product rules vs **overdubPass**.

**Do** use one **`editPass`** struct + **`EditPassKind`** for note vs CC — same op-list container,
same materialize/disable/undo shape, different **`EditChange`** subsets and batch indices.

**Do not** use four separate top-level arrays (`noteEditPasses[]` + `controlChangeEditPasses[]`)
unless CC payload diverges later — premature duplication.

```text
Loop
├── capture                    Capture — append while capturing
├── pendingCapturePass         pending capture pass (Record | Overdub phase)
└── passes                     LoopPasses
    ├── recordPass             optional
    ├── overdubPasses[]
    └── editPasses[]           EditPass { kind, batchIndex, changes[] }
         materializePasses()
```

---

## Pass vocabulary (locked)

**Do not use committed** for pass lifecycle, container names, or pass-related undo kinds.

| Role | Term |
|------|------|
| Pre-`passes[]` | **pendingCapturePass** |
| Capture transition | **commitRecordPass** / **commitOverdubPass** |
| Edit transition | **saveNoteEditPass** / **closeNoteEditPass**; **saveControlChangeEditPass** / **closeControlChangeEditPass** (future) |
| Edit row | **editPass** + **`EditPassKind`** |
| Note batch index | **noteEditPassIndex** on **editPass** when `kind == NoteEdit` |
| CC batch index | **controlChangeEditPassIndex** on **editPass** when `kind == ControlChange` (future) |
| Global undo | **RecordPassAdded**, **OverdubPassAdded**, **NoteEditPassClosed**, **ControlChangeEditPassClosed** (future) |

**Pending** is the only qualified lifecycle state. In prose, name the kind: “**noteEditPass** row
in **`editPasses[]`**” — not “committed note edit pass”.

**noteEditPass** (batch boundary) vs **editPass** (stored row): **closeNoteEditPass** closes a
batch; undo **NoteEditPassClosed** disables all **editPassId** values with matching
**noteEditPassIndex** and **`EditPassKind::NoteEdit`**.

---

## Context

Today: `takes[]` + `edits[]` + **Capture** + `pendingTake_`. [`applyEditsToFlat`](../../../src/EditApply.cpp)
merges takes, then applies edits.

---

## Investigation: one owner for takes[] + edits[]?

| Option | Verdict |
|--------|---------|
| **O1 — `passes` wrapper, two-phase materialize** | **Ship** |
| **O2 — chronological replay** | Deferred |
| **O3 — unified payload (chunks vs op-lists)** | **Rejected** |
| **Four separate edit arrays** | **Rejected** — use **`editPass` + kind** |
| **Single `capturePass` + kind** | **Rejected** — keep **recordPass** + **overdubPasses** |

---

## Storage shape

```cpp
enum class EditPassKind : uint8_t { NoteEdit, ControlChange };

enum class CapturePassPhase : uint8_t { Record, Overdub };  // on pendingCapturePass / Capture

struct EditPass {
  editPassId id;
  EditPassKind kind;
  uint8_t noteEditPassIndex;  // when kind == NoteEdit; controlChangeEditPassIndex when ControlChange (same field, kind scopes meaning)
  EditPassState state;  // Active | Disabled
  EditChangeList changes;
};
```

```cpp
struct PendingCapturePass {
  PassId id;
  CapturePassPhase phase;
  ChunkIdList chunkRefs;
  uint32_t sealedAtTick;
};
```

**Anticipate CC (apply slice, no UI):** `EditPassKind::ControlChange` in enum, SD kind byte,
materialize switch arm (no-op or stub), **ControlChangeEditPassClosed** undo kind reserved —
implementation in follow-up change.

---

## Decisions

### D1 — Remove Take

| Was | Now |
|-----|-----|
| `Take` / `takes[]` | **recordPass** + **overdubPasses** |
| `Edit` / `edits[]` | **editPass** in **editPasses[]** |
| `saveEdit()` | **saveNoteEditPass()** (note kind); **saveControlChangeEditPass()** (future) |
| Undo | **RecordPassAdded**, **OverdubPassAdded**, **NoteEditPassClosed**, **ControlChangeEditPassClosed** (future) |

### D2 — No “committed” on passes

See prior lock — **`passes[]`** only.

### D3 — Two-phase materialize

1. Merge Active **recordPass** + **overdubPasses**.
2. Apply Active **editPasses** in order; dispatch **EditChange** by **`EditPassKind`**.

### D4 — SD + grep

Dual-read legacy; wire **EditPassKind** byte on **editPass** tail.

---

## Open Questions

1. **Empty-loop edit-first UX** — storage allows no **recordPass**; **Track** arm/edit entry guards — follow-up after rename PR.
2. **O2 chronological replay** — separate change.
3. ~~**PassId** unified vs split~~ → **unified** `nextPassId_`.
4. ~~**Second recordPass**~~ → storage caps at one; empty-bar capture routes to **overdub**; defer record arm when recordPass or empty-loop rules apply (task 0.6).
