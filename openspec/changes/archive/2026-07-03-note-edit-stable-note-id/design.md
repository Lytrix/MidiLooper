## Architectural intent

Today the note editor identifies notes by **geometry** ([`NoteRef`](../../../include/EditPass.h): channel, pitch, startTick, endTick). That couples editor state, UI state, and storage layout. Normal editing (move, pitch, length, overlap restore, wrap handling) **changes geometry**, forcing every consumer to continually rediscover “the edited note.”

This change introduces a stable **`NoteId`** representing the **lifetime of a logical note**.

| Layer | Rule |
|-------|------|
| **Geometry** | Mutable |
| **Identity** | Immutable (`NoteId` on note-on) |
| **Storage** | Remains MIDI-event based (`LoopEventStore` + passes) |
| **Display** | Remains fully reconstructed (`NoteUtils::reconstructNotes` → `DisplayNote`) |

`NoteId` exists solely to provide stable identity for selection, edit replay, undo, motor fader synchronization, and future multi-select. It is **not** intended to change the storage architecture.

---

## Architectural invariants

These are the contract for future refactors. Violating any of these is a design bug.

1. **Loop** owns `NoteId` allocation.
2. **Loop** is the **only** allocator (`allocateNoteId()`).
3. **`EditorSelection`** stores only **`NoteId`s** (plus `trackId`, `loopId`, `bracketTick`) — never notes, pointers, or list indices as identity.
4. **Geometry never defines identity** — `NoteRef` removed for note targeting.
5. **Display indices are derived** — never stored in selection or undo as identity.
6. **`DisplayNote` is reconstructed** — not a second source of truth.
7. **`NoteId` never changes** on an existing logical note (move/pitch/length keep the same id).
8. **`NoteId` is never reused** within a loop — retired ids stay retired.
9. **Every edit resolves `NoteId` at mutation time** — `findNoteOnById` / `deleteNoteById` at apply, not cached global indices in undo rows.
10. **Caches never own identity** — see [Cache ownership](#cache-ownership).
11. **MIDI event storage is the single source of truth** — ids live on note-on events in the store.

---

## Main architecture

Identity originates in **`Loop`**, flows through reconstruction and selection, and resolves back to **`LoopEventStore`** at mutation time.

```text
Loop
   │
allocateNoteId()
   │
   ▼
MidiEvent.noteId
   │
reconstructNotes()
   │
   ▼
DisplayNote.noteId
   │
   ▼
EditorSelection.primaryNote
   │
   ▼
EditPass.targetNoteId
   │
   ▼
findNoteOnById()
   │
   ▼
LoopEventStore
```

| Step | Rule |
|------|------|
| `allocateNoteId()` | **Only** on logical note **birth** (see allocation regression list) |
| `MidiEvent.noteId` | Stored on **note-on only** — canonical identity |
| `reconstructNotes()` | **Derives** `DisplayNote.noteId` — never allocates |
| `EditorSelection` | **References** ids — never owns notes or allocates |
| `EditPass.targetNoteId` | **References** id for replay — Create rows carry id on `addedEvents` note-on |
| `findNoteOnById()` | **Resolves** id → live note-on in store at apply/undo time |
| `LoopEventStore` | **Single source of truth** — all mutations land here |

**EditApply** and session undo sit on the `findNoteOnById()` → `LoopEventStore` path; they do not introduce new ids except when replaying a **Create** row (id already on the stored `addedEvents` note-on).

### Decision rationale: `MidiEvent.noteId` on note-on

**Rejected:** separate `Note` table, sidecar id map, or parallel object graph.

**Chosen:** `uint32_t noteId` on the **note-on** [`MidiEvent`](../../../include/MidiEvent.h) inside the existing event stream.

**Rationale:** One source of truth; no sync between parallel graph and event store; matches note-on anchor + LIFO note-off pairing; playback hot paths unchanged.

---

## NoteId vocabulary

| Term | Meaning |
|------|---------|
| **Allocate** | Take next serial from `Loop::nextNoteId_` — `allocateNoteId()` |
| **Assign** | Write that number onto note-on `MidiEvent.noteId` when the note is created |
| **Assign guard rail** | On session open, assign missing ids for `noteId == 0` note-ons |

**Not used:** “mint” — use **allocate** + **assign**.

---

## NoteId allocation regression list

Primary native + HITL regression contract for Phase B.

### Creates NEW `NoteId`

| Operation | When | Code path | Regression check |
|-----------|------|-----------|------------------|
| **Recording** | Each note-on appended during `RECORDING` | `Track::recordMidiEvents` → `allocateNoteId()` on NOTE_ON | After record stop: every sealed note-on has `noteId != 0`; unique monotonic |
| **Overdub** | Each note-on during `OVERDUBBING` | Same capture path | After overdub stop/fold: captured note-ons have ids; no duplicates |
| **Manual Add Note** | User creates note at bracket | `createNoteAtTick` → `EditActionType::Create` | New note-on gets one id; note-off has no id; id stable on later edits |

**Safety net:** `assignMissingNoteIds()` on session open — allocates only when `noteId == 0`; log WARNING per assign.

### Does NOT create NEW `NoteId`

| Operation | Expected | Regression check |
|-----------|----------|------------------|
| **Move / Pitch / Length / Velocity / Quantize** | Same `noteId`; geometry changes | id unchanged before/after |
| **Overlap Restore** | Reuses original id from `OverlapNote` / focus | No `allocateNoteId()` call |
| **Undo / Redo** | Restores same ids from snapshot | `primaryNote` and event ids match |
| **Delete** | Removes events; id may be in undo snapshot; no new id |
| **`reconstructNotes()`** | Read-only derive | Never allocates |
| **Wrap display split** | Two `DisplayNote` rows, one `noteId` | Same id on tail + head |
| **Fader select / motor sync** | Updates `EditorSelection` only | References existing ids |

### Suggested native test suite

1. Record fixture → unique monotonic ids on all note-ons
2. Move/pitch/length → `noteId` unchanged on targeted note-on
3. Manual Add → new id > max existing; subsequent move keeps id
4. Overlap restore → restored note-on id equals pre-hide id
5. Session undo round-trip → `EditorSelection.primaryNote` and event ids restored
6. `assignMissingNoteIds` → assigns only zeros; does not touch non-zero ids

---

## Logical note lifecycle

```text
Created (record / overdub / manual Add)
    → allocateNoteId() + assign on note-on
    → Stored (note-on + note-off)
    → Selected (EditorSelection.primaryNote)
    → Edited (move / pitch / length — same NoteId)
    → Wrap split (display only — two DisplayNotes, one NoteId)
    → Deselected
    → Deleted (events removed after pair-resolution)
    → Removed from live storage

Undo paths (identity preserved):
  Global Undo  → Restored (same NoteId)
  Note Edit Undo  → Restored (same NoteId)
  Final deletion (no undo)  → NoteId retired forever
```

**Identity can outlive storage temporarily.** After Delete, events may be gone from the live store but the logical note may still be restored via global undo or note edit undo. The allocator **never reuses** retired ids.

---

## Context

- Phase A (displayIdx / fader selection refactor) **shipped** 2026-07-02 — Phase B NoteId work starts after D0a resolution + explicit user scope (see Phase A exit below).
- Live selection uses `NoteEditSelection` + `NoteRef` + `selectedNoteIdx` / `displayIdx`.
- `EditPass.target` is `NoteRef` (~10 bytes on SD v5 wire).
- `Loop::nextPassId_` pattern exists for edit pass ids.
- Overdub stop while in note edit calls `foldLiveCaptureIntoNoteEditSession` — does **not** call `sealCapture`; ids must be assigned at append or in fold path.

Primary files: `Loop.h/cpp`, `MidiEvent.h`, `EditPass.h`, `EditApply.cpp`, `EditManager.cpp`, `NoteEditFocus.*`, `NoteUtils.*`, `NoteEditSessionUndo.cpp`, `StorageLoopIo.cpp`, `Track.cpp`.

---

## Goals / Non-Goals

**Goals:**

- Stable note identity decoupled from geometry and list index
- Single allocator owner (`Loop::allocateNoteId()`)
- `EditorSelection` with `primaryNote` + `selectedNotes[]` (multi-select ready)
- Full replace of `NoteRef` for note targeting
- SD v6 with `nextNoteId` and `targetNoteId` on edit rows
- Fader feedback gates on `primaryNote` delta (after Phase A)

**Non-Goals:**

- Parallel note object graph
- `ControlChangeId` (Phase D)
- Record/overdub display merge in NOTE_EDIT (Phase C)
- v5 → v6 SD migration (dev wipe)
- Jam / persistence overlay scope

---

## Decisions

### D0 — Entity id type aliases (Phase 0 — shipped)

**Decision:** **`NoteId`** and invalid sentinel in [`include/MidiEvent.h`](../../../include/MidiEvent.h). **`TrackId`** in [`include/NoteEditSessionState.h`](../../../include/NoteEditSessionState.h). Phase 0 temporary `EntityIds.h` hub removed after Phase B co-location (D0a).

| Type | Invalid sentinel | Rationale |
|------|------------------|-----------|
| **`NoteId`** | **`0`** | Unassigned note-on; valid ids start at 1 after first **allocateNoteId()** |
| **`TrackId`** | **`UINT32_MAX`** | Matches **`LoopId`** — **0** is a valid track index in pool addressing |

Included from public API headers (**`NoteEditSessionState.h`**, **`EditPass.h`**, **`EditManager.h`**, **`NoteEditFocus.h`**, **`Loop.h`**, **`MidiEvent.h`**) with no behavior change until Phase B.

### D0a — `EntityIds.h` scope and filename (resolved)

**Status:** Resolved — **(C) document-only** plus planned post-Phase B co-location (dissolve thin header).

**Why scoped today:**

- Minimal `<cstdint>`-only include for public API surfaces — no pull-in of **LoopPasses**, undo, or storage headers.
- Phase 0 was a zero-behavior typedef landing ahead of Phase B schema work.
- **`PassId`**, **`LoopId`**, **`EditPassId`** already live next to **LoopPasses** / **EditPass** structs, SD wire fields, and **`Loop::nextPassId_`** allocation.

**Why the filename is misleading:** `EntityIds` reads like a global entity-id registry, but the file currently holds **note-edit selection identity** only (`NoteId`, `TrackId`). Other stable ids remain domain-local:

| Id | Current home |
|---|---|
| `PassId`, `EditPassId` | [`include/EditPass.h`](../../../include/EditPass.h) |
| `LoopId` | [`include/LoopPasses.h`](../../../include/LoopPasses.h) |
| `UndoEntryId` | [`include/GlobalUndoStack.h`](../../../include/GlobalUndoStack.h) |
| `setId`, `revisionId`, … | Storage / catalog headers |

**Options (pick one before Phase B):**

| Option | Action |
|--------|--------|
| **(A) Rename** | Scoped header name (e.g. `NoteEditIdentityIds.h`, `EditorSelectionIds.h`); update includes/tests — no behavior change |
| **(B) Expand** | Move all timeline `*Id` typedefs into the central header — typedef move only; structs/allocation stay in domain headers |
| **(C) Document** | Keep `EntityIds.h`; add header comment + D0 scope note that it means cross-cutting edit/selection identity, not every `*Id` |

**Out of scope for any option:** Jam/M10 ids, storage catalog ids, stack-internal ids unless a separate OpenSpec says otherwise.

**Naming reference:** [`docs/Plans/note_edit_stable_note_id_enhancement.md`](../../docs/Plans/note_edit_stable_note_id_enhancement.md) — `Id` vs `Ref` vs index; in-scope vs out-of-scope ids.

**Chosen (2026-07-02):** **(C) + post-Phase B co-location** — temporary hub during Phase B; **complete:** `NoteId` in [`include/MidiEvent.h`](../../../include/MidiEvent.h), `TrackId` in [`include/NoteEditSessionState.h`](../../../include/NoteEditSessionState.h); `EntityIds.h` deleted.

### D1 — Full replace `NoteRef` → `NoteId`

**Decision:** Remove `NoteRef` as note target identity. `NoteId` only for live selection, focus, overlap maps, and committed `EditPass` rows. Keep `ControlChangeRef` until Phase D.

### D2 — `EditorSelection` replaces `NoteEditSelection`

```cpp
using TrackId = uint32_t;
using NoteId = uint32_t;
constexpr NoteId kInvalidNoteId = 0;

struct EditorSelection {
  TrackId trackId = kInvalidTrackId;
  LoopId loopId = kInvalidLoopId;
  std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> selectedNotes;
  NoteId primaryNote = kInvalidNoteId;
  uint32_t bracketTick = 0;
};
```

**Resolved (Phase 0):** `kInvalidTrackId = UINT32_MAX` — matches **`LoopId`** / **`kInvalidLoopId`**. Defined in [`include/NoteEditSessionState.h`](../../../include/NoteEditSessionState.h).

Phase 1 behavior: always `selectedNotes = { one }`, `primaryNote = that id`. Chord same 16th slot: **first selected** (pitch-low → pitch-high).

### D3 — Assign on append (primary), batch at stop/fold (safety net)

| Boundary | When | File |
|----------|------|------|
| Capture birth | Record/overdub note-on append | `Track::recordMidiEvents` |
| Capture birth (defensive) | Stragglers at stop/fold | `Loop::sealCapture`, `foldLiveCaptureIntoNoteEditSession` |
| Edit birth | User Create note | `createNoteAtTick` |
| Session open | Guard rail only | `assignMissingNoteIds()` |

### D4 — Assign guard rail (not assert-only)

On `openNoteEditSession`, scan note-ons with `noteId == 0`, allocate from `loop.nextNoteId_`, assign, **log WARNING** per assign. Optional debug assert if any still zero after assign.

### D5 — SD v6 clean break

- Add `uint32_t noteId` to `MidiEvent` (note-on meaningful; 0 = invalid)
- Replace `editPass.target` (`NoteRef`) with `editPass.targetNoteId` (`uint32_t`)
- Add `PersistedLoopSnapshot.nextNoteId` after `nextPassId`
- Reject v5 slot files; dev wipe and re-record

### D6 — `SelectNavSlot.noteId` (Phase B)

Fader-1 nav slots carry `NoteId` per slot; `EditorSelection.primaryNote` is output after slot resolve — not a substitute for the slot list.

---

## Cache ownership

```
Caches never own identity.
Caches are always rebuildable.
Destroying every cache must never lose information.
```

| Cache | Rebuild from |
|-------|--------------|
| `baselineMap` / overlap maps | session store + `NoteId` keys |
| `Track` note cache | `reconstructNotes` |
| Filtered selectable inventory | ephemeral per nav |
| `SelectNavigation` slots | window-filtered notes + bracket |

Invalidate caches on store mutation; never treat cache index as `NoteId`.

---

## Phase A exit / acceptance (prerequisite for Phase B)

**Status: satisfied** (2026-07-02, commit `d3d5798`). Evidence: [`note_edit_stable_note_id_phase_a_handoff.md`](../../docs/Plans/note_edit_stable_note_id_phase_a_handoff.md).

Phase B NoteId work starts only when all pass **and** D0a (`EntityIds.h` scope) is resolved with explicit user scope.

### Native

- [x] `pio test -e native` green
- [x] `test_note_edit_fader_feedback` — selection gate tests pass

### HITL — slow fader-1 sweep

Enter NOTE_EDIT → slow continuous fader-1 sweep left → right across full loop display.

- [x] Full-loop nav inventory — 59 nav slots (2+2 + second overdub via SEVT)
- [x] `select_ignored_rate` = 0.0
- [x] Sibling sync — `sibling_select_count=2` on multi-note steps
- Capture: `captures/phase_a_slow_fader_sweep_20260702_011229_serial.log`

### Windowed sorted inventory

- [x] Selectable list = `filterSelectableDisplayNotes` then window filter; rebuild on window scroll

### Phase A identity gate (pre-NoteId)

- [x] Gate on **`NoteRef` equality** — `shouldApplySelectionOnNoteRefChange`; no persisted `displayIdx`

---

## Ownership split

| Module | Owns |
|--------|------|
| `Loop` | `nextNoteId_`, `allocateNoteId()` |
| `MidiEvent` | `noteId` on note-on (durable) |
| `EditManager` | `EditorSelection` |
| `NoteEditFocus` | `movingNoteId`, overlap maps by `NoteId` |
| `NoteUtils` / `NoteEditFocus` helpers | Stateless resolve: `findNoteOnById`, `deleteNoteById`, `resolveDisplayNoteById` |

---

## EditPass row shape (Phase B)

```cpp
struct EditPass {
  NoteId targetNoteId = kInvalidNoteId;  // replaces NoteRef target
  uint32_t startTick = 0;
  uint32_t endTick = 0;
  uint8_t pitch = 0;
  uint8_t velocity = 0;
  MidiEventVec addedEvents; // Create: note-on/off with assigned noteId on on
};
```

| `actionType` | `targetNoteId` |
|--------------|----------------|
| **Create** | `kInvalidNoteId` — identity in `addedEvents` note-on |
| **Delete** | id to remove |
| **Update** | id to mutate |

---

## Delivery slices (Phase B)

1. Types + allocators — `NoteId`, `Loop::nextNoteId_`, `MidiEvent.noteId`, assign on note-on append
2. EditPass + EditApply — `targetNoteId`; session undo diff by id; remove `NoteRef` note paths
3. Resolve + delete — `DisplayNote.noteId`, `findNoteOnById`, `deleteNoteById`
4. `EditorSelection` + focus — overlap/`baselineMap` by `NoteId`; session undo stores ids
5. Fader feedback — gate on `primaryNote` (after Phase A)
6. SD v6 — `nextNoteId` + wire; dev wipe; update all tests

Phase 0 (before A/B): `TrackId` / `NoteId` aliases on public API surfaces only.

---

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Missed id on capture path | Append-time assign + seal/fold safety net + session-open guard rail with WARNING logs |
| `sizeof(MidiEvent)` bump breaks v5 | v6 clean break; reject old files; dev wipe |
| Phase B before Phase A stable | **Resolved** — Phase A satisfied 2026-07-02; D0a + user scope gate Phase B |
| Assign guard rail masks bugs | WARNING log per assign; optional debug assert after assign |
| Overdub-in-edit without seal | Assign at append; fold path batch for stragglers |

---

## Migration Plan

1. ~~Complete Phase A (displayIdx / fader selection) on current branch~~ **Done** (`d3d5798`)
2. Phase 0: type aliases (no behavior)
3. Phase B slices 1–6 per tasks.md
4. Dev wipe SD; re-record loops
5. `pio test -e native`; HITL fader sweep
6. Archive change; merge specs

Rollback: revert NoteId branch; dev wipe again if v6 files written.

---

## Open Questions

- **Phase C timing** — separate OpenSpec after Phase B stable

---

## Follow-up slices (out of this change)

| Phase | Work |
|-------|------|
| **C** | Record/overdub in edit + capture display merge |
| **D** | `ControlChangeId` |

See backup plan [`docs/Plans/note_edit_stable_note_id_enhancement.md`](../../../docs/Plans/note_edit_stable_note_id_enhancement.md) for full touchpoint map and Phase C product rules.
