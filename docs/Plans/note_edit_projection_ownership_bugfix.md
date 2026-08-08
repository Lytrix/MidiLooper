# Note edit projection ownership — active investigation (RC6)

**Prerequisite:** driver drift is **closed** — see [`note_edit_focus_driver_drift_bugfix.md`](archive/bugfix/note_edit_focus_driver_drift_bugfix.md) (FROZEN). Do not revisit RC1 unless new evidence shows `GEOM_APPLY,pipeline,…,0,3,0` or driver span mismatch before geometry.

**Primary captures:**

- `captures/session_20260806_214302.log` — final validation that geometry is trusted; projection breaks on inner-note overlap cascade
- `captures/session_20260806_212810.log` — overlap shorten flicker, selection reindex during move

**Owner:** `projectNoteEditDisplayNotes` (`NoteEditFocusDisplayProjection.cpp`). No new top-level module.

---

## Debugging boundary

This investigation deliberately splits the NOTE_EDIT pipeline at `EditSessionAction`:

```
Select / focus rebuild / driver validity     ← CLOSED (driver drift doc)
        ↓
GeometryPipeline → EditSessionAction rows    ← TRUSTED unless new evidence
        ↓
projectNoteEditDisplayNotes                  ← RC6 (this doc)
        ↓
Select relatch / macro commit / recon        ← RC7 / RC8 (separate follow-up index)
```

| Side of boundary | Treatment |
|------------------|-----------|
| **Before** `EditSessionAction` | Geometry, overlap shorten/hide/delete, move rows are **correct by default**. Audit only if pipeline apply fails or store counts contradict actions. |
| **At and after** `EditSessionAction` | Display projection, paint count, `DNTE` length, selection index stability — **under investigation**. |

**Why:** prevents solved driver-drift work from absorbing projection bugs, and stops projection debugging from questioning geometry that already logged clean `GEOM_APPLY,pipeline,…,1,3,0` rows.

---

## Implementation status

| Phase | Status |
|-------|--------|
| Phase 2 — Projection ownership (RC6) | **Shipped** |

---

## Symptoms

| Symptom | When | Log anchor | Capture |
|--------|------|------------|---------|
| Overlapping notes appear/disappear on first inner-note move | ~21.7s | `DISP` 5→4 while `sourceEventCount` stays 5; `type=0 noteId=6` + `type=1 noteId=3` | `214302` |
| Bar length flicker (350 / 101 / 89) on same pitch lane | ~21.7–33s | `DNTE` alternates lengths; outer shortened in store but display may show full span | `214302`, `212810` |
| “Two 89-tick pitch-65 notes” while piano roll showed one | ~33s | `DNTE,65,1050,…,89,4` vs `DNTE,65,1044,…,101,4` — two real `NoteId`s (6 and 7), not driver duplicate | `214302` |
| Lane-wide bar (min note-on → max note-off) | First overlap cascade | Committed-base row at full outer length while participant overlay shows shortened span (or reverse next frame) | `214302` |
| Selection index jumps during geometry | ~219–234s | `Note selection changed: 4 → 3 → 2 → 1` while mover `focus.last` tracks `noteId=7` | `212810` |
| Behavior nominal after separating all three notes | End of session | Overlap participants shrink; one row per `NoteId` | `214302` |

**What captures show:** `EditSessionAction` rows are plausible and pipeline apply succeeds. `DISP` paint count and `DNTE` lengths disagree with live store spans after overlap shorten — consistent with projection ownership violation, not geometry failure.

---

## RC6 — Projection ownership invariant

When `focus.active` and overlap geometry has emitted shorten/hide/delete actions, `projectNoteEditDisplayNotes` must satisfy:

| Rule | Meaning |
|------|---------|
| One bar per `NoteId` | Each visible object identity appears **exactly once** in the projected list |
| Participant replaces committed row | `movingNoteId` + `changedOverlapNoteIds` overlay **updates or removes** the committed-base row — never adds a second bar for the same `NoteId` |
| Shortened note shows live store span | After `type=1` Length on outer note, projected length = store `endTick − startTick`, not pre-shorten committed length |
| Hidden participant omitted | `type=0` Delete / hide overlap: participant absent from paint (`hiddenParticipants`); committed row for that `NoteId` removed |
| No lane-wide ghost | Do not synthesize a bar from lane min note-on to lane max note-off across distinct `NoteId`s |

**Hypothesis:** overlap geometry is working; `projectNoteEditDisplayNotes` leaves committed-base rows at pre-edit spans while participant overlay adds or partially updates rows — reads as appear/disappear/lengthen on a dense pitch lane.

**Not in scope for RC6:** macro commit `NoteRange start=0` (RC7), `missing in recon` after commit (RC8) — see [`note_edit_overlap_projection_followup.md`](note_edit_overlap_projection_followup.md).

---

## Evidence (`214302`)

### Geometry trusted

Long note move (`noteId=3`, storage 609–959):

- Every step: `overlapNotes=0`, only `type=3` MoveNote
- `GEOM_APPLY,pipeline,…,1,3,0` on every step
- `DISP` stays **5** painted notes throughout (~16.5–17.4s)

### Projection breaks on inner-note move

Short note move (`noteId=7` at 1044) into outer `noteId=3`:

| Time | Store actions | Paint signal |
|------|---------------|--------------|
| 21.741 | `type=1 noteId=6` shorten; `type=3 noteId=7` move | `DNTE,65,1092,…,101,4` |
| 22.024 | `type=0 noteId=6` delete; `type=1 noteId=3` shorten 609→947; `type=3 noteId=7` move | `DISP` **4** painted, **5** source events |
| 22.252+ | Repeated `type=1 noteId=3` shorten steps | `DISP` oscillates 4↔5 |

**Store at entry:** five pitch-65 notes including nested overdub `noteId=6` (89 ticks, 1050–1139) and `noteId=7` (101 ticks, 1044–1145) under outer `noteId=3` (350 ticks, 609–959). One visible bar on the piano roll before move is **stacked rendering**, not proof that overlaps were resolved at record/overdub stop.

---

## Implementation — Phase 2

**Owner:** `projectNoteEditDisplayNotes` (`NoteEditFocusDisplayProjection.cpp`).

### Tasks

1. **Committed row replacement** — when a participant `NoteId` exists in `result` (committed base), skip the stale committed copy; participant overlay is the sole source. ✅
2. **Shortened outer note** — participant overlay reads live store span via `resolveParticipantDisplaySpan`. ✅
3. **Hidden participant** — existing `hiddenParticipants` path; committed copies skipped for participants. ✅
4. **`kInvalidNoteId` lane bar** — skip committed rows without `NoteId` when any participant on the same pitch overlaps the row span (visual-cache lane ghost). ✅
5. **Native fixture** — `test_project_pitch65_outer_shorten_inner_move_214302`, `test_project_pitch65_hidden_nested_not_in_display_214302`, `test_project_pitch65_visual_cache_lane_bar_not_left_alongside_participants_214302`. ✅
6. **Handoff regression** — `test_project_post_commit_no_phantom_note_153954` and related projection tests unchanged. ✅

### HITL gate

Move inner note (`noteId=7`) left through outer (`noteId=3`):

- 0× full-length ghost of outer note after shorten actions
- `DISP` paint count stable or monotonic (no 4↔5 oscillation when store note count unchanged)
- `DNTE` length matches store span for selected note after each move step

---

## Verification

| Gate | Command / action |
|------|------------------|
| Native regression | `pio test -e native` — overlap projection fixture + handoff tests |
| Build | `pio run -e teensy41-capture-serial` |
| HITL | Inner-through-outer move on pitch-65 lane; capture `214302`-class scenario |
| Capture check | `EditSessionAction` rows trusted; audit `DISP` / `DNTE` only after geometry lines |

Overlap shorten emitting `type=1` Length rows in the store is **expected geometry**. Display must match store span after each step.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Owner module | `projectNoteEditDisplayNotes` (`NoteEditFocusDisplayProjection.cpp`) |
| Primary invariant | One projected bar per `NoteId`; live store span after overlap actions |
| Ownership change? | **NO** — extend existing projection owner |
| State transition change? | **NO** |
| Behavior-preserving? | **YES** for non-overlap edit paths; overlap paths fix incorrect paint only |
| Geometry trusted? | **YES** unless `GEOM_APPLY,pipeline,…,0,3,0` reappears |

---

## Related docs

- [`note_edit_focus_driver_drift_bugfix.md`](archive/bugfix/note_edit_focus_driver_drift_bugfix.md) — FROZEN RC1 investigation + `214302` final validation
- [`note_edit_overlap_projection_followup.md`](note_edit_overlap_projection_followup.md) — RC7 macro commit + RC8 reconstruction index
- `docs/Plans/note_edit_scoped_display_handoff.md` — participant bind / display-count invariant
