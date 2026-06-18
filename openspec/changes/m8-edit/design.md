## Context

M7 shipped **Take**-equivalent storage under the name `Epoch`. Note edit still collapses Takes
via `syncEditFlatToEpochs`. M8 aligns vocabulary and splits **Capture / Take** (performance) from
**Edit** (modifiers committed during edit sessions).

## Vocabulary (locked)

### Loop storage drivers

| Word | Role | Was |
|------|------|-----|
| **Loop** | Slot container | — |
| **Capture** | Live record/overdub buffer | `CaptureLayer` |
| **Take** | Committed capture (Record \| Overdub) | `Epoch` |
| **Edit** | One committed note edit (`saveEdit`) | (new) |

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
| **`EditId`** | Stable id per committed **Edit** (Onshape changeId analog) |
| **`NoteRef`** | Stable note target inside a change |
| **`TakeType`**, **`TakeState`** | Record \| Overdub; Pending \| Active \| Disabled |

### Verbs (consistent)

| Verb | Meaning |
|------|---------|
| **`commitTake()`** | Seal **Capture** → append **Take** (was publish epoch) |
| **`saveEdit()`** | Append one **Edit** (with **EditChange** list) to `edits[]` |
| **`closeNoteEditSpan()`** | Boundary flush before overdub or on note-edit exit |

### Global undo kinds

| Kind | Reverts |
|------|---------|
| **`TakeCommitted`** | One overdub/record **Take** (was `EpochPublished`) |
| **`NoteEditSessionCommitted`** | All **Edit** ids in the closed **NoteEditSession** span |

Bulk undo after note-edit exit is **per span** (all **Edits** saved during that span), not per
individual **Edit** on the global stack. In-session undo before **saveEdit** uses
**NoteEditSessionUndoStack** only.

**Not used:** segment, gesture, published (use **committed**), `EditOp`, `working`,
`SessionCapture` (→ **DebugSessionCapture**), `NoteEditCommit`.

```
Loop
├── capture           Capture { store, phase }
├── takes[]           Take { id, kind, state, chunks }
└── edits[]           Edit { id, spanIndex, state, changes[] }

NoteEditSession (EditManager, while in note edit)
├── store             LoopEventStore — materialized view (takes + active edits)
├── undoStack         NoteEditSessionUndoStack
└── spanIndex         increments at span boundary (overdub while editing)
```

**Lifecycle:**

```
Capture  ──commitTake──►  Take
NoteEditSession  ──saveEdit──►  Edit { EditChange[] }
NoteEditSession  ──closeNoteEditSpan──►  NoteEditSessionCommitted (global undo)
```

## Goals / Non-Goals

**Goals:**

- **`saveEdit()`** → **Edit** with **EditChange** list; **`EditId`** for persistence/undo
- **NoteEditSession** with **`store`** (not “working”); **NoteEditSessionUndoStack**
- **`applyEdits(takes, edits)`** for playback/display
- **Span boundaries** at overdub start and note-edit exit; **`closeNoteEditSpan()`**
- **`TakeCommitted`** / **`NoteEditSessionCommitted`** global undo
- SD **v4** extended with `edits[]`; autosave rules (§5)
- Rename **`SessionCapture`** → **`DebugSessionCapture`** if not done in **m8-rename**

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

### 2. Edit = one saveEdit (locked)

```cpp
struct Edit {
  EditId id;
  uint8_t spanIndex;       // which NoteEditSession span (overdub boundary)
  EditState state;         // Active | Disabled — parallel TakeState
  EditChangeList changes;  // ordered; multi-change saves (move+overlap deletes)
};
```

- **`EditId`**: assigned on **saveEdit**; stored on SD; referenced by undo
- **`EditChange`**: one atomic change; **`EditChangeType`** enum (prefer **Type** over **Kind**)
- **`NoteRef`** in each change — never display index
- No-op **saveEdit** → no **Edit**, no dirty

### 3. NoteEditSession (locked)

```cpp
struct NoteEditSession {
  LoopEventStore store;
  NoteEditSessionUndoStack undoStack;
  uint8_t spanIndex;
};
```

- Open on note edit enter; clear on note edit exit
- **`store`**: rematerialized from takes + active edits at open and after overdub stop
- **saveEdit** writes **Edit** to `edits[]` and updates materialized state
- Do not use “working” or “gesture” in code or docs

### 4. Span boundaries — not “segment” (locked)

There is **no user-selected segment** on the timeline. **Span** = contiguous period of
**NoteEditSession** between **boundary events**:

| Boundary | Action |
|----------|--------|
| Overdub start (while in note edit) | **`closeNoteEditSpan()`** → **NoteEditSessionCommitted** → capture |
| Overdub stop (still in note edit) | **TakeCommitted** → rematerialize **NoteEditSession.store** → `spanIndex++` |
| Note edit exit | **`closeNoteEditSpan()`** → urgent SD if dirty |

Global undo after exit (example with overdub): **NoteEditSessionCommitted** (span 1) →
**TakeCommitted** → **NoteEditSessionCommitted** (span 0).

### 5. Two-layer undo (locked)

| Layer | When | Holds |
|-------|------|-------|
| **NoteEditSessionUndoStack** | In note edit, before **saveEdit** | RAM store snapshots |
| **GlobalUndoStack** | After **saveEdit** / span close | **TakeCommitted**, **NoteEditSessionCommitted** |

MIDI undo in note edit → **NoteEditSessionUndoStack** only.

### 6. SD autosave (locked)

| Trigger | Behavior |
|---------|----------|
| **saveEdit** (change) | `markEditStateDirty()` |
| Periodic | `autosaveIntervalMs` (default 5 min); defer while capture active |
| Note edit exit + dirty | Urgent flush in post-MIDI main-loop slice (overdub OK) |

**Compaction:** prefer affected tick/bar span + span close/autosave — not raw change count.

### 7. Retire editFlat bridge

Remove collapse flush; **NoteEditSession.store** replaces **editFlat_** paths.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| EditNoteState rename churn | Done in **m8-rename** before Edit storage |
| NoteRef drift | Ref at **saveEdit**; apply changes in order |
| Global stack depth | One **NoteEditSessionCommitted** per span, not per **Edit** |
| SessionCapture confusion | **DebugSessionCapture** rename |

## Migration Plan

1. **`m8-rename`** — vocabulary only (`/opsx:apply m8-rename`, native green, merge)
2. **Edit**, **EditChange**, **NoteRef**, **NoteEditSession**, **saveEdit**, apply engine
3. Span boundaries + **NoteEditSessionCommitted**
4. Native tests + docs
5. Archive → **timeline-takes**

## Open Questions

- **JamSession** vs **PerformanceSession** (or other) for playback/jam UI — confirm before use
- SD v4 **EditChange** encoding layout
