# Tasks — timeline-pass-model

**Change:** `timeline-pass-model`  
**Design:** [design.md](./design.md)  
**Vocabulary:** 4 pass kinds; 2 families (capture + **editPass** with **EditPassKind**)

---

## 0. Preconditions

- [x] 0.1 Merge **m8-pass-vocabulary** closure into this apply: **editPassIndex** → **noteEditPassIndex**,
  **passEditIds** → **noteEditPassIds**, HITL pass log grep (single rename PR)
- [x] 0.2 `pio test -e native` green on base branch

### Resolved defaults (see design §Decisions)

| Topic | Decision |
|-------|----------|
| SD | **Extend v4 in place** + **EditPassKind** byte on **editPass** tail; dual-read if needed |
| PR scope | **One PR** — m8-pass-vocabulary leftovers + timeline-pass-model |
| PassId | Unified **nextPassId_** |
| Materialize | **LoopPasses::materialize()** |

### 0.6 Record arm / empty bar (minimum in apply)

- [x] 0.6.1 At most one **recordPass** in **`passes[]`**; empty-bar capture routes to **overdub**, not second record
- [ ] 0.6.2 Defer **record** arm when **recordPass** exists or empty-loop rules apply; document in **Track** + design (follow-up PR)
- [x] 0.6.3 Full edit-without-record UX **deferred** — storage MAY omit **recordPass**

## 1. LoopPasses owner (O1)

- [x] 1.1 `LoopPasses.h` — **RecordPass**, **OverdubPass**, **EditPass** + **EditPassKind**, **PendingCapturePass**, **materialize()**
- [x] 1.2 **Loop.passes** replaces `takes[]` + `edits[]`
- [x] 1.3 Port **applyEditsToFlat** logic — two-phase; **EditPassKind** dispatch (CC arm stub)
- [x] 1.4 Native equivalence fixtures

## 2. Remove Take — capture path

- [x] 2.1 Pass headers replace **Take.h**; **pendingCapturePass** lifecycle
- [x] 2.2 **commitCapturePass** in **Track.cpp** (routes record vs overdub via **effectiveCapturePassPhase**)
- [x] 2.3 **recordPass** 0-or-1 validation
- [x] 2.4 Grep gate: no **Take**, **TakeCommitted**, `*PassCommitted`, **committedPasses**

## 3. editPass + EditPassKind

- [x] 3.1 **Edit** → **EditPass**; **EditId** → **editPassId**; **EditPassKind::NoteEdit** on all current rows
- [x] 3.2 **saveEdit()** → **saveNoteEditPass()**; **disableEdits** → **disableEditPasses**
- [x] 3.3 **noteEditPassIndex** + **noteEditPassIds** on batch close
- [x] 3.4 Reserve **EditPassKind::ControlChange**, **controlChangeEditPassIndex**, **ControlChangeEditPassClosed** (enum + stub; no UI)

## 4. Global undo

- [x] 4.1 **RecordPassAdded**, **OverdubPassAdded**, **NoteEditPassClosed**
- [x] 4.2 **ControlChangeEditPassClosed** enum value (apply stub)
- [x] 4.3 **TrackUndo** + serial logs

## 5. SD I/O

- [x] 5.1 **v4 extend:** map Take wire → **recordPass** + **overdubPass** list; **editPass** tail (kind defaults **NoteEdit** in RAM)
- [x] 5.2 Dual-read legacy Take + Edit wire layout
- [x] 5.3 **test_storage_loop_io**

## 6. Tests + docs

- [x] 6.1 **test_loop_take_survival**, **test_edit_apply** (**editPass** / **saveNoteEditPass**)
- [x] 6.2 Storage guide + **Naming-Vocabulary-Teensy-Looper.mdc** — 4 kinds, 2 families
- [x] 6.3 HITL grep strings — edit baseline on hardware; pass-close/undo/redo strings verified (`20260620_150954`)
- [x] 6.4 `openspec validate` + archive (`2026-06-20`)

---

## PR order

| PR | Tasks |
|----|-------|
| A | 1.x **LoopPasses** + materialize |
| B | 2.x Take removal + capture undo |
| C | 3.x–4.x **editPass** + kind + undo |
| D | 5.x–6.x SD, docs, CC stub |
