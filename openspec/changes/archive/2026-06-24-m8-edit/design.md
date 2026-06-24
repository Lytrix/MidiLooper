## Context

M7 shipped capture storage; **m8-rename** and **timeline-pass-model** (2026-06-20) replaced
**Take**/`takes[]` + **Edit**/`edits[]` with **`LoopPasses`** (**recordPass**, **overdubPasses**,
**editPasses[]**). M8 completes **note-edit** behavior: **NoteEditSession** RAM editing,
**saveNoteEditPass** rows, **NoteEditPassClosed** undo, and SD persistence on exit.

### Current state (2026-06-20)

| Shipped | Open |
|---------|------|
| **NoteEditSession**, **saveNoteEditPass**, **closeNoteEditPass** | Per-op session undo native matrix (§4.1) |
| **LoopPasses::materialize**, SD v4 **passes** I/O | Overdub-during-edit 3-step undo native test (§4.4) |
| Sync **processEditAutosave** on **exitEditMode** | SD exit-flush-while-playing host test (§4.3) |
| HITL M8 pass verify OK | Full HITL overlap/insert (separate bug track) |
| No collapse-on-exit | **`editFlat_`** / **NoteEditCommit** cleanup (**§3.3 done**) |
| Native 110/110 | **`/opsx:archive`** → **`timeline-passes`** (§5.3) |

## Vocabulary (locked)

### Loop storage drivers

| Word | Role | Was |
|------|------|-----|
| **Loop** | Slot container | — |
| **Capture** | Live record/overdub buffer | `CaptureLayer` |
| **recordPass** / **overdubPass** | Committed capture (Record \| Overdub) | **Take** / `Epoch` |
| **editPass** | One **saveNoteEditPass** row in **`editPasses[]`** | **Edit** / `edits[]` |

### Edit session family (RAM, per UI domain)

| Session | Scope | Code anchor |
|---------|--------|-------------|
| **NoteEditSession** | Note select / move / length / pitch | `EditManager` (note edit) |
| **LoopEditSession** | Loop length / start (future rename) | `LoopEditManager` |
| **ControlChangeEditSession** | CC edit UI (future) | — |
| **JamSession** / **PerformanceSession** | Playback/jam UI (future, **TBD** — confirm before use) | — |

**NoteEditSession** is the M8 deliverable. Other session types share naming only; not in M8 scope.

### UI state machine (rename)

| Was | Now | Notes |
|-----|-----|-------|
| `class EditState` | **`class EditNoteState`** | Base for `EditSelectNoteState`, etc. |
| — | **`EditState`** on struct **`Edit`** | Active \| Disabled — parallel **`TakeState`** |

### Compound types (no abbreviations)

| Type | Role |
|------|------|
| **`EditChange`** | One atomic change inside an **Edit** (delete, move, …) |
| **`EditChangeType`** | Enum of change types (extensible: velocity, control change, paste, …) |
| **`editPassId`** | Stable id per **editPass** row |
| **`NoteRef`** | Stable note target inside a change |
| **`TakeType`**, **`TakeState`** | Record \| Overdub; Pending \| Active \| Disabled |

### Verbs (consistent)

| Verb | Meaning |
|------|---------|
| **`commitRecordPass()`** / **`commitOverdubPass()`** | Seal **Capture** → append capture pass in **`passes[]`** |
| **`saveNoteEditPass()`** | Append one **editPass** (with **EditChange** list) to **`editPasses[]`** |
| **`closeNoteEditPass()`** | Close **noteEditPass** batch before overdub or on note-edit exit |

### Global undo kinds

| Kind | Reverts |
|------|---------|
| **`RecordPassAdded`** / **`OverdubPassAdded`** | Disable one capture pass by id |
| **`NoteEditPassClosed`** | Disable all **editPassId** values from one closed **noteEditPass** |

Bulk undo after note-edit exit is **per edit pass** (all **Edits** saved during that pass), not per
individual **Edit** on the global stack. In-session undo before **saveEdit** uses
**NoteEditSessionUndoStack** only.

**Not used:** segment, gesture, published (use **committed**), `EditOp`, `working`,
`SessionCapture` (→ **DebugSessionCapture**), `NoteEditCommit`.

```
Loop
├── capture              Capture { store, phase }
└── passes               LoopPasses
    ├── recordPass       optional chunk-backed record layer
    ├── overdubPasses[]  ordered overdub layers
    └── editPasses[]     editPass { editPassId, noteEditPassIndex, changes[] }

NoteEditSession (EditManager, while in note edit)
├── store                LoopEventStore — live edit preview (passes + active editPasses)
├── undoStack            NoteEditSessionUndoStack
└── editPassIndex        increments at noteEditPass boundary (overdub while editing)
```

**Lifecycle:**

```
Capture  ──commitRecordPass/commitOverdubPass──►  passes (capture pass)
NoteEditSession  ──saveNoteEditPass──►  editPass in editPasses[]
NoteEditSession  ──closeNoteEditPass──►  NoteEditPassClosed (global undo)
```

## Goals / Non-Goals

**Goals:**

- **`saveNoteEditPass()`** → **editPass** with **EditChange** list; **editPassId** for persistence/undo
- **NoteEditSession** with **`store`**; **NoteEditSessionUndoStack**
- **`LoopPasses::materialize`** for playback/display
- **noteEditPass** boundaries at overdub start and note-edit exit; **`closeNoteEditPass()`**
- **`RecordPassAdded`** / **`OverdubPassAdded`** / **`NoteEditPassClosed`** global undo
- SD **v4** **passes** persistence; autosave + **synchronous exit flush** (shipped)
- Narrow **`editFlat_`** to derived cache only (**§3.3 done**)

**Prerequisite:** **m8-rename** complete — **Take**, **Capture**, **TakeCommitted**, UI base rename.

**Non-Goals:**

- LoopEditSession / ControlChangeEditSession (naming only in M8)
- **JamSession** vs **PerformanceSession** vs other — confirm before any playback/jam UI session type ships
- Jam recording; SD v5 unless required

## Decisions

### 1. Take vocabulary (m8-rename — prerequisite)

Implemented in **`m8-rename`**, not this change:

- `Epoch` → **Take**; `EpochKind` → **`TakeType`**; `epochs[]` → **`takes[]`**
- **`commitTake()`** / **`TakeCommitted`** undo
- **`CaptureLayer`** → **`Capture`**
- Remove **`EpochKind::Edit`**
- UI FSM base rename; **`EditState`** freed for struct **Edit**

### 2. editPass = one saveNoteEditPass (locked)

```cpp
struct EditPass {
  EditPassId id;
  EditPassKind kind;           // NoteEdit | ControlChange (future)
  uint8_t noteEditPassIndex;   // which noteEditPass batch (overdub boundary)
  EditPassState state;         // Active | Disabled
  EditChangeList changes;      // ordered; multi-change saves (move+overlap deletes)
};
```

- **`editPassId`**: assigned on **saveNoteEditPass**; stored on SD; referenced by undo
- **`EditChange`**: one atomic change; **`EditChangeType`** enum (prefer **Type** over **Kind**)
- **`NoteRef`** in each change — never display index
- No-op **saveNoteEditPass** → no **editPass**, no dirty

### 3. NoteEditSession (locked)

```cpp
struct NoteEditSession {
  LoopEventStore store;
  NoteEditSessionUndoStack undoStack;
  uint8_t editPassIndex;
};
```

- Open on note edit enter; clear on note edit exit
- **`store`**: rematerialized from **passes** at open and after overdub stop
- **saveNoteEditPass** appends **editPass** to **`editPasses[]`** and updates session store
- Do not use “working” or “gesture” in code or docs

### 4. Edit pass boundaries — not “segment” (locked)

> **Vocabulary (shipped):** **edit pass** replaces **span** in prose and identifiers. See
> [m8-pass-vocabulary/design.md](../m8-pass-vocabulary/design.md). Code uses
> `editPassIndex` / `closeNoteEditPass()`.

There is **no user-selected segment** on the timeline. An **edit pass** = contiguous period of
**NoteEditSession** between **boundary events**:

| Boundary | Action |
|----------|--------|
| Overdub start (while in note edit) | **`closeNoteEditPass()`** → **NoteEditPassClosed** → capture |
| Overdub stop (still in note edit) | **OverdubPassAdded** → rematerialize **NoteEditSession.store** → `editPassIndex++` |
| Note edit exit | **`closeNoteEditPass()`** → sync SD flush if dirty |

Global undo after exit (example with overdub): **NoteEditPassClosed** (noteEditPass 1) →
**OverdubPassAdded** → **NoteEditPassClosed** (noteEditPass 0).

### 5. Two-layer undo (locked)

| Layer | When | Holds |
|-------|------|-------|
| **NoteEditSessionUndoStack** | In note edit, before **saveNoteEditPass** | RAM store snapshots |
| **GlobalUndoStack** | After **saveNoteEditPass** / **closeNoteEditPass** | **RecordPassAdded**, **OverdubPassAdded**, **NoteEditPassClosed** |

MIDI undo in note edit → **NoteEditSessionUndoStack** only.

### 6. SD autosave (locked)

| Trigger | Behavior |
|---------|----------|
| **saveNoteEditPass** (change) | `markEditStateDirty()` |
| Periodic | `autosaveIntervalMs` (default 5 min); defer while recording/overdubbing |
| Note edit exit + dirty | **Synchronous** `processEditAutosave` in **exitEditMode** (shipped; works while playing) |

**Compaction:** prefer affected tick/bar **compaction window** + edit-pass close/autosave — not raw change count.

### 7. Retire editFlat bridge (done)

Collapse-on-exit is removed. **`editFlat_`** is now a derived cache behind **`midiEvents()`**
only; canonical ownership remains in **passes** (or **NoteEditSession.store** while editing).

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| EditNoteState rename churn | Done in **m8-rename** before Edit storage |
| NoteRef drift | Ref at **saveNoteEditPass**; apply changes in order |
| Global stack depth | One **NoteEditPassClosed** per **noteEditPass** batch, not per **editPass** row |
| SessionCapture confusion | **DebugSessionCapture** rename |

## Migration Plan

1. **`m8-rename`** — vocabulary only (`/opsx:apply m8-rename`, native green, merge)
2. **EditPass**, **EditChange**, **NoteRef**, **NoteEditSession**, **saveNoteEditPass**, materialize — **done**
3. **noteEditPass** boundaries + **NoteEditPassClosed** — **done**
4. Native tests §4.1–§4.4 + optional full HITL — **open**
5. Archive → **`timeline-passes`**

## Open Questions

- **JamSession** vs **PerformanceSession** (or other) for playback/jam UI — confirm before use
- SD v4 **EditChange** encoding layout
