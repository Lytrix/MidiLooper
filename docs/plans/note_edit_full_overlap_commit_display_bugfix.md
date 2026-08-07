# Note edit — full overlap commit still paints hidden short note

**Status:** OPEN — documented from HITL `session_20260807_203805.log`; **no firmware fix in this artifact**.

**Parent:** [note_edit_resolver_authority_contracts_refinement.md](note_edit_resolver_authority_contracts_refinement.md) — Stage 6.5 display/inventory follow-up.

**Related shipped work:** Stage 6.5 (1) shorten paint vs selectable inventory (`2aeb175`, `c4136fd`); HITL **`203805`** confirms L→R/R→L **shorten paint** is good.

---

## Symptom (user report)

When a **short note** is **fully overlapped** by the mover and the edit is **committed** (select-bracket / macro commit path):

| Layer | Expected | Observed (`203805`) |
|-------|----------|---------------------|
| **Selectable inventory** (DNTE / fader-1) | Hidden overlap not selectable | **PASS** — count drops; note absent from inventory |
| **Grid paint** (OLED piano roll) | Fully covered note **removed** from display | **FAIL** — note **still drawn** at committed span |

Inventory masking and paint masking are **split** for shortened overlap (Stage 6.5 (1)); this bug is the **full hide / complete-cover** case after commit.

---

## Log anchors (`session_20260807_203805.log`)

### Short note fully covered (pitch 88, noteId=9, span 2592–2639, length 47)

| Time | Evidence |
|------|----------|
| ~`21.661s` | Mover (noteId=17) steps onto short note 9 at 2592 (`overlapNotes=0` — same-pitch stack). |
| **`34.404s`** | `EditSessionAction: type=2 noteId=9 start=2592 end=2639` — **HideNote** (complete cover). Mover `17` → 2544–2992. `interactions=1 constrained=1`. |
| `34.405s` | `Note selection changed: 9 -> 7` — inventory index drops (short note no longer selectable). |
| `34.406s` | `#CAP,34409689,DISP,2,STOPPED,5376,12,13,12,12,1` — **frameNotes=12**, **visualCache=13** (one note still in committed cache not removed from frame). |
| `34.406s` | `DNTE,88,2544,2544,448,7` — sidebar lists mover only (inventory OK). |

### Commit on overlapping short note (pitch 84, noteId=9 @ 2592 — prior overlap session)

| Time | Evidence |
|------|----------|
| `30.039s` | Focus rebuild: `changedOverlapNoteIds count=1`, `changedOverlapNoteId=9`. |
| `30.059s` | `NOTE_EDIT pre-commit row: targetNoteId=9 action=Update property=NoteRange start=2592 end=2639`. |
| `30.086s` | `commitEditAction loop_materialized: M84 start=2592 end=2639`. |
| `30.106s` | Focus rebuild: **`changedOverlapNoteIds count=0`** — overlap membership cleared after commit. |

---

## Debugging boundary

```
Geometry / apply (HideNote on complete cover)  ← inventory correct in log
  → NoteEditCurrentState (presence Hidden, not in session store)
  → commit / focus rebuild (changedOverlapNoteIds cleared)
  → projectNoteEditDisplayNotes + visualCache committed base  ← **current investigation**
```

Do **not** re-open shorten-vs-hidden semantics or overlap closure (Stage 6.5 (2)) unless a new log shows `ShortenNote` on this path.

---

## Hypothesis (code-backed)

`projectNoteEditDisplayNotes` builds from **`loop.visualCache.notes`** (committed base) and overlays **participants** from `changedOverlapNoteIds` + session store.

1. **HideNote** sets `NoteEditCurrentState` presence **Hidden** and removes the note from session store projection — inventory filter (`rowIncludedInSelectableInventory` / Hidden) works.
2. **Participant overlay** only applies to noteIds in the participant set (`hasChangedOverlapNote` / `changedOverlapNoteIds` when current state non-empty).
3. After **commit**, focus rebuild logs **`changedOverlapNoteIds count=0`** while the loop **visual cache** still holds the committed span for the hidden note (`visualCache=13` vs `frameNotes=12` in DISP).
4. Committed-base copy in `projectNoteEditDisplayNotes` **still includes** notes that are Hidden in `noteEditCurrentState` but **no longer listed as overlap participants** — so the grid paints the stale committed row.

Secondary path: during edit (before commit), if participant discovery misses the hidden id, the same committed-base leak would paint the full span even while inventory is masked.

**Invariant to restore:** Any `NoteEditCurrentState` row with `presence == Hidden` or `Deleted` must **not** appear in `EditManager::projectedNoteEditDisplayNotes` output, regardless of `changedOverlapNoteIds` or visualCache contents, until leave-restore or full restore explicitly paints committed span (mover left overlap zone).

---

## Proposed fix direction (not implemented)

| Option | Owner | Notes |
|--------|--------|------|
| **A** | `projectNoteEditDisplayNotes` | When `noteEditCurrentState` non-empty, strip or mask any committed-base row whose noteId is Hidden/Deleted in current state (defense in depth beyond participant list). |
| **B** | `EditManager::ensureNoteEditDisplayProjectionCachesBuilt` | Merge current-state hidden set into paint fingerprint; never paint from stale visualCache alone for hidden ids. |
| **C** | Commit / `syncCommittedSpan` / visual cache | On HideNote commit row or macro commit, invalidate or patch `loop.visualCache` for hidden noteIds (heavier; couples display to pass bake). |

Prefer **A** first — smallest diff, matches Stage 6.5 inventory/paint split, no stop-path ownership change.

---

## Tests to add (before fix)

Native fixture from `203805` slice:

1. Upsert overlap short note + mover; apply **HideNote** full cover; assert `projectNoteEditDisplayNotes` (paint path) **excludes** hidden noteId while `filterProjectingSelectableDisplayNotes` also excludes it.
2. Same fixture after **synthetic commit rebuild** (`changedOverlapNoteIds` cleared, current state row still Hidden) — paint list must **still** exclude hidden noteId.

---

## Verification gate (after fix)

- `pio test -e native` — new fixture in `test_note_edit_current_state` or `test_note_edit_focus`.
- HITL: repeat `203805` short-note full-cover + commit; DISP `frameNotes` should not include hidden id; grid visually empty at overlap tick.

---

## Pre-implementation review

### Ready

- Log proves HideNote + inventory drop + DISP/cache mismatch.
- Owner for paint: `projectNoteEditDisplayNotes` / `EditManager::projectedNoteEditDisplayNotes`.

### Open before coding

1. Confirm leave-restore paint (Hidden row, mover **past** committed span) still paints full committed baseline — must not regress RC10h / `test_selectable_inventory_excludes_paint_only_hidden_row`.
2. Decide whether commit should also clear hidden rows from `visualCache` (option C) or paint-only suppression (option A) is sufficient.

### Proceed?

- **NO** for this session — documentation only per user request.
