# Design — note edit modification session

**Change:** `note-edit-modification-session`  
**Status:** Design locked (2026-06-19)  
**Aligns with:** [m8-edit/design.md](../m8-edit/design.md) (**NoteEditSession**, **EditChange**)  
**Vocabulary:** [m8-pass-vocabulary](../m8-pass-vocabulary/design.md) — **edit pass**, **focus**, **moving note range**; **moving note**, **overlap note**

**Child bug evidence:**

- [note-move-pitch-overlap-flaky/BUG.md](../note-move-pitch-overlap-flaky/BUG.md)
- [lengthen-overlap-neighbor-restore/BUG.md](../lengthen-overlap-neighbor-restore/BUG.md)

---

## Goals

1. **One owner** for note geometry overlap during note edit.
2. **One source of truth** for live MIDI: `NoteEditSession.store` via `editAwareMidiEvents()`.
3. **Baseline at note select** — full note inventory from materialized store (includes overlap notes from Take).
4. **A1** — separate **moving note range** from **commitBaseline**.
5. **B1** — rematerialize via `applyEdits`; pre-commit resolve makes store commit-ready.
6. **Incremental updates** — mutate only the moving note and overlap notes touched by overlap/restore.

## Non-Goals

- Rewriting Record/Overdub **Take** chunks on live edit.
- CC / velocity overlap tracking (focus moving note only).
- Replacing `EditStartNoteState` encoder path in the first PR slice.

---

## Architecture — one owner

```text
NoteEditSession (EditManager)          ← note edit mode
├── store                              ← SOT: live MIDI (takes + edits + scratch)
├── editPassIndex                      ← M8 edit pass (legacy: spanIndex)
├── focus                              ← single moving note (replaces movingNote scratch)
│   ├── moving                         ← NoteRef + live start/end/pitch
│   ├── commitBaseline                 ← frozen at fader-1 select (A1)
│   ├── movingNoteRange                ← start/end for inner/overlap tests (A1)
│   └── overlapNotes                   ← impacted OverlapNote entries only
├── undoStack                          ← in-pass undo (before saveEdit)
└── passEditIds / pendingChanges       ← M8 (legacy: spanEditIds)

NoteMovementUtils                      ← sole overlap engine
└── applyNoteEditChange(kind)          ← move | length | pitch

Fader / EditNoteState                  ← thin adapter → applyNoteEditChange
```

**Take boundary:** all live mutations go through `noteEditSession.store`. Takes are read-only
during edit; committed state is `edits[]` replayed by `applyEdits`.

---

## Decision A1 — commit baseline vs moving note range

Two fields; **do not** overload `origEnd`.

| Field | Purpose | Updated when |
|-------|---------|--------------|
| **commitBaseline** `{start,end,pitch}` | Compare vs live for `commitAllPendingNoteEditActions`; **NoteRef** target for **EditChange** | **Fader-1 select**; after successful commit (advance baseline to live) |
| **movingNoteRange** `{start,end}` | Inner, contained delete, lane-merge classification | Length edit (`end = lastEnd`); move (`end = lastEnd`); pitch lane merge only when external overlap note merged |
| **last** `{start,end,pitch}` | Current mover geometry | Every move / length / pitch apply |

**Length edit:** MUST update `movingNoteRange.end = lastEnd`. MUST NOT update `commitBaseline.end`
until commit — so `commitAllPendingNoteEditActions` still detects pending **ChangeLength**.

**Pitch lane merge (external):** may extend `movingNoteRange.end`. **Inner preserve:** must not extend moving note range.

---

## Decision — baseline at fader-1 select

**When:** after `commitAllPendingNoteEditActions` on fader-1 note select (existing hook in
`NoteEditManager`).

**Capture:** read-only scan of `noteEditSession.store` → `baselineMap: NoteRef → {pitch,start,end}`.

- Includes overlap notes already present from Record Take + prior **Edit**s (materialized view).
- **Not** a Take flatten; **not** lazy at first overlap.

**On select:**

```text
focus.commitBaseline     = baselineMap[selected NoteRef]
focus.movingNoteRange    = { start: commitBaseline.start, end: commitBaseline.end }
focus.last            = commitBaseline
focus.overlapNotes.clear()
focus.moving          = selected NoteRef + live geometry
```

**baselineMap** stays read-only for the focus lifetime (until next select or commit advances
moving baseline). **OverlapNote** entries **copy** from baselineMap when first impacted.

---

## Decision — one source of truth

| Layer | Role | Writers |
|-------|------|---------|
| **NoteEditSession.store** | Live MIDI during edit | `applyNoteEditChange` only (incremental event patch) |
| **baselineMap** | Restore geometry for overlap notes | Filled once at select; read-only |
| **overlapNotes** | hidden / shortened / visible per **OverlapNote** | Overlap engine only |

**Removed / merged (fader path):** overlap hide/shorten scratch → **`focus.overlapNotes`**
(`OverlapNote` replaces `DeletedNote`).

**Still present (encoder path — task 8.1):** `movingNote.deletedNotes` in `EditStartNoteState`;
`sessionHiddenOverlapNotes` / `sessionShortenedOverlapNotes` cleared but unused.

Hidden overlap note = absent from **store**; full geometry in **baselineMap** + **OverlapNote** state.

---

## Decision B1 — rematerialize + pre-commit resolve

### Persisted truth

After **saveEdit**, canonical state is **takes + edits[]**. `loop.rematerializeEditView(store)`
rebuilds from `applyEdits` (already in `commitEditAction`).

### Pre-commit resolve (before `commitAllPendingNoteEditActions`)

When fader-1 reselect, state exit, or explicit commit boundary:

```text
1. For each **OverlapNote** in `overlapNotes` with state != visible:
     restore or materialize that **NoteRef** in store from baselineMap + current shortened end
     (incremental — only overlapNotes entries)
2. Build EditChangeList:
     - moving note: MoveNote | ChangeLength | ChangePitch if live != commitBaseline
     - overlap notes: DeleteNote | ChangeLength only if final store state != baselineMap entry
3. saveEdit(changes)  — ordered list; multi-change per gesture allowed (m8-edit)
4. rematerializeEditView(noteEditSession.store)
5. Advance focus.commitBaseline to live moving geometry; clear overlapNotes
```

**Impacted-notes-only rule:**

| Phase | Touch |
|-------|--------|
| Live fader edit | Moving note events + overlap note **NoteRef**s entering/leaving overlap |
| Pre-commit resolve | **Only** `overlapNotes` entries + moving note |
| `applyEditChange` replay | **Only** **NoteRef** targets in each **EditChange** |
| Rematerialize full store | Rebuild loop MIDI events from Takes + Edits (unchanged M8); edits list stays sparse |

Do **not** re-run overlap simulation on rematerialize (rejects B2). Do **not** flatten and rewrite
all notes on each fader CC.

### EditChange emission

Each change carries **NoteRef** at **commitBaseline** coordinates (existing pattern in
`buildMoveCommitChanges`). Overlap note deletes/shortens become explicit **EditChange** entries so
replay matches live session without **overlapNotes** scratch.

---

## Decision — unified move / length / pitch pipeline

```text
applyNoteEditChange(kind):
  1. restoreOverlapNotesNoLongerOverlapping()   // overlapNotes + store; impacted refs only
  2. classify overlaps vs movingNoteRange
  3. apply shorten / contained delete → overlapNotes + store patch
  4. mutate moving note in store
  5. update focus.last; update movingNoteRange per A1 rules
  6. invalidateCaches / sync session store if needed
```

**Pitch** and **move** share steps 1–3. **Length** skips overlap classify unless extending
into neighbors (future: same path if length overlap added).

**Restore symmetry:** step 1 runs after **every** kind (fixes pitch-only restore gap).

---

## CC / velocity (session boundary)

**NoteEditSession** remains the RAM container for note-edit UI. **overlapNotes** applies only to
note on/off overlap (move / length / pitch — not CC/velocity).

| Edit kind | Session | overlapNotes |
|-----------|---------|----------------|
| Move / length / pitch | NoteEditSession | Yes |
| Velocity | NoteEditSession (future **EditChange**) | No — moving note only |
| CC | ControlChangeEditSession (future) | No |

No overlap tracking for CC/velocity.

---

## Validation

| Layer | Criterion |
|-------|-----------|
| Native | A1 split: length updates movingNoteRange not commitBaseline until commit |
| Native | Pre-commit: only overlapNotes + mover in store diff |
| Native | `applyEdits` parity after lengthen → move → pitch → move back |
| Native | Shared-release boundary (A@496 case) |
| HITL | `host_midi_automation_edit_baseline` overlap round-trip + rematerialize checks |
| Takes | No writes to Take capture/commit in this change |

---

## Sequencing (see tasks.md)

1. **focus** struct + baseline at select + A1 field split  
2. **overlapNotes** + retire duplicate vectors  
3. **applyNoteEditChange** (move + pitch)  
4. Pre-commit resolve + **EditChange** builder (impacted only)  
5. **EditApply** fixes for replay parity  
6. HITL + docs  

Child folders `note-move-pitch-overlap-flaky` and `lengthen-overlap-neighbor-restore` retain
scenario BUG.md; tasks defer to this change for implementation order.

---

## Data structures (normative)

### `baselineMap` (on **focus**, rebuilt each fader-1 select)

```cpp
// Key: NoteRef at materialized coordinates from store scan
struct NoteBaseline {
  uint8_t pitch;
  uint32_t startTick;
  uint32_t endTick;
};
using BaselineMap = /* ordered map or hash */ NoteRef → NoteBaseline;
```

- Built from read-only scan of `noteEditSession.store` after `commitAllPendingNoteEditActions`.
- **Full loop** inventory (all notes in store), not overlap-only.
- Immutable until next fader-1 select on any note.

### `OverlapNote` (one impacted overlap note)

```cpp
enum class OverlapNoteStoreState : uint8_t { Visible, Hidden, Shortened };

struct OverlapNote {
  NoteRef ref;                    // baseline coordinates (commit target identity)
  NoteBaseline baseline;          // copy from baselineMap at first impact
  OverlapNoteStoreState state;
  uint32_t shortenedEndTick = 0;  // valid when state == Shortened (live note-off tick)
  bool innerUnderMovingNote = false; // set when visible after pitch but still inside moving note range
};
```

### `NoteEditFocus`

```cpp
struct NoteEditFocus {
  bool active = false;
  NoteRef moving;                 // selected note identity
  NoteBaseline commitBaseline;    // A1 — frozen at select until commit
  MovingNoteRange movingNoteRange;
  NoteBaseline last;              // live moving note geometry
  BaselineMap baselineMap;
  /* OverlapNote collection */ overlapNotes;  // NoteRef → OverlapNote, impacted only
};
```

---

## Overlap note shortening (when moving note overlaps)

The **moving note is never shortened by overlap** — only user move / length / pitch edits
change the mover. Overlap logic applies to **other** notes (**overlap notes**).

Classification uses moving note range `[last.start, last.end]` (display-unwrapped) vs each candidate
same-pitch overlap note (cross-pitch uses separate short-over-long rules in engine).

| Overlap shape | Action on overlap note | `OverlapNote.state` | Store |
|---------------|------------------------|---------------------|-------|
| Fully **contained** in moving note range | Remove note pair | **Hidden** | absent |
| Starts **before** mover, tail crosses mover start | Note-off → `moverStart - 1` (wrap-aware) | **Shortened** | truncated pair |
| Shortened gate **&lt; 49 ticks** | Remove pair (too short to keep) | **Hidden** | absent |
| Starts **after** mover start (unusual partial) | Remove pair | **Hidden** | absent |
| **Inner** under `movingNoteRange` | Skip — preserve | not added / unchanged | unchanged |
| Already **Shortened** overlap note tracked | Update `shortenedEndTick` only | **Shortened** | update off tick |
| **Cross-pitch** short-over-long | Tail shorten or hide per existing rules | **Shortened** or **Hidden** | per engine |

**Lengthen** on the moving note uses the same classifier (`changeLengthWithOverlapHandling` →
`findOverlaps`): extending end into another same-pitch note can **shorten** or **hide** that
overlap note; `movingNoteRange.end` updates (A1).

**Restore:** when mover moves away, `restoreOverlapNotesNoLongerOverlapping()`:

- **Hidden** → reinsert pair from `baseline` (or `shortenedEndTick` if was shortened-then-hidden)
- **Shortened** → set note-off back to `baseline.endTick` (or remove from `overlapNotes` if fully restored)
- Remove entry from `overlapNotes` when state returns to **Visible** and store matches baseline

**Pre-commit / EditChange for shortened overlap note:**

```text
ChangeLength { target: NoteRef@baseline, newEndTick: shortenedEndTick }
```

**Pre-commit for hidden:**

```text
DeleteNote { target: NoteRef@baseline }
```

---

## `overlapNotes` state transitions

| From | Event | To | Notes |
|------|-------|-----|-------|
| — | First overlap impact | **Hidden** or **Shortened** | Copy `baseline` from `baselineMap` |
| **Hidden** | Pitch restore (lane clear) + still inner | **Visible** + `innerUnderMovingNote=true` | Reinsert in store; stay in `overlapNotes` |
| **Visible** (inner) | Move covers again | **Hidden** | Re-hide from baseline |
| **Visible** | Move uncovers | remove entry | Store matches baseline |
| **Shortened** | Move uncovers | **Visible** or remove | Restore full gate from baseline |
| **Shortened** | Further overlap | **Shortened** | Update `shortenedEndTick` |
| any | Pre-commit resolve | **Visible** in store | Then emit EditChanges; clear entry |

Pitch restore MUST NOT drop `innerUnderMovingNote` until mover leaves moving note range or commit clears focus.

---

## Pre-commit `EditChange` emission

**One `saveEdit()` per fader-1 reselect** (merged list). Order within the list:

1. **DeleteNote** — overlap notes that end **Hidden** vs baseline  
2. **ChangeLength** — overlap notes **Shortened** (`newEndTick = shortenedEndTick`)  
3. **MoveNote** — moving note if `last.start != commitBaseline.start` (or end if move carries end)  
4. **ChangeLength** — moving note if `last.end != commitBaseline.end`  
5. **ChangePitch** — moving note if `last.pitch != commitBaseline.pitch`  

Skip no-op deltas. Each **NoteRef** target uses **commitBaseline** coordinates (existing M8 pattern).

---

## Commit boundaries

| Event | Pre-commit resolve + `saveEdit`? | Notes |
|-------|----------------------------------|-------|
| Fader-1 select **different** note | **Yes** | Today: `commitAllPendingNoteEditActions` |
| Exit note edit mode | **Yes** | Before `closeNoteEditPass` |
| Overdub start while editing | **Yes** | Flush focus first, then `closeNoteEditPass` |
| Move / length / pitch fader (same selection) | No | Live scratch + `overlapNotes` only |
| In-session MIDI undo | No | `NoteEditSessionUndoStack` restores store snapshot |

---

## Test matrix (native + HITL)

### Native — keep

| Suite | Role |
|-------|------|
| **`test_edit_apply`** | **Keep** — B1 `applyEdits` replay; extend for overlap **DeleteNote** + **ChangeLength** ordering |
| **`test_note_edit_focus`** (new) | Focus A1, overlapNotes state machine, shared-release @496, lengthen→move→pitch→back live path |
| **`test_loop_stop_finalize`**, **`test_noteutils_reconstruct`**, **`test_loop_event_store`** | **Keep** — unrelated hot paths |

### Native — remove after `test_note_edit_focus` lands

| Suite | Reason |
|-------|--------|
| **`test_delete_restore`** | Duplicated overlap math + `main()` harness; not firmware |
| **`test_shorten_delete_restore`** | Same — replace with `NoteMovementUtils` / focus tests |

Until removal: keep green in CI; mark deprecated in suite README.

### HITL — keep (rename keys in follow-up task)

| Verifier | Role |
|----------|------|
| `_verify_long_over_short_pitch_restore` | Primary overlap round-trip AC (`m0_home_ok`, inner A/P0) |
| `_verify_change_length_store_rebuild` | Rematerialize / P0 loop-end stretch |
| `_verify_note_length_integrity` | Shortened overlap note restore (rename `*_victim_*` → `*_overlap_note_*`) |
| `_verify_beat_move_routing` | Position vs length edit routing |
| `_verify_m0_survives_warmup` | M0 before length edit |
| `_verify_edit_move_display` | Move display + same-pitch overlap log |

### HITL — review / optional split

| Verifier | Action |
|----------|--------|
| `_verify_split_overlap_note_round_trip` | **Review dedup** with `_verify_long_over_short_pitch_restore` after PR 4; merge if redundant |
| `_verify_delay_move_insert_reorder` | **Keep separate** — D-delay insert scenario; out of scope unless same root cause proven |
| P0 `original_end=1535` in `_verify_note_length_integrity` | **Investigate** (task 0.1) before gating PR 4 on it |

### HITL — do not remove pre-implementation

No verifiers dropped until `test_note_edit_focus` + overlap engine PR passes equivalent AC.
Weak gates (`1535`) may be **demoted to warning** after investigation, not deleted silently.

