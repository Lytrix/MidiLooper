# Design — unified note edit overlap (move + pitch)

**Change:** `note-move-pitch-overlap-flaky`  
**Status:** Superseded for implementation by [note-edit-modification-session](../note-edit-modification-session/design.md) (2026-06-19).  
**Parent bug spec:** [BUG.md](./BUG.md)  
**Aligns with:** m8-edit **NoteEditSession** (RAM edit layer only; **Takes** untouched)

---

## Context

Root-cause analysis confirmed:

- **H1:** Position overlap (`NoteMovementUtils`) and pitch overlap (`NoteEditManager` adjacent merge) diverge.
- **H2:** `movingNote.origEnd` stays at record gate (120) after length edit (696); inner-note checks fail → A merged on pitch.

User decisions:

1. **Yes** — expand shared move/overlap logic to include **pitch changes** (DRY), not only tick moves.
2. **Session snapshot** must update on length edit (and other edit geometry changes); **do not modify record/overdub Takes**.
3. **Yes** — tighten HITL AC2 so merged mega-spans cannot pass as “inner A preserved”.

---

## Goals

- One overlap engine for **position**, **pitch**, and **length** edits while a note edit session is active.
- **Inner notes** under the mover’s session span survive pitch change and reappear when uncovered.
- **Lane merge** on pitch (D-delay case) still works when the neighbor is **not** an inner note under the session span.
- Short-over-long (cross-pitch) stays in the shared engine.
- All edits apply to **edit RAM / session store** only until `saveEdit()` — **never** rewrite committed Takes.

## Non-Goals

- Changing Take capture, commit, or epoch/take storage layout.
- Full M8 `NoteEditSession` + `EditChange[]` migration in this patch (prepare API shape only).
- Refactoring `EditStartNoteState` encoder path in the same PR (follow-up once fader path is stable).

---

## Decision 1 — Single overlap owner (DRY)

### Problem

Today:

| Operation | Owner | Overlap rules |
|-----------|--------|----------------|
| Fader 2 position | `NoteMovementUtils::moveNoteWithOverlapHandling` | contained delete, shorten, restore, cross-pitch short-over-long |
| Fader 4 pitch | `NoteEditManager::handleNoteValueFader` | adjacent merge loop, separate contained delete, restores old-pitch `deletedNotes` only |

### Decision

**Extend `NoteMovementUtils`** (name TBD: keep namespace, add entry points) to own **all** overlap side effects during an active edit session:

```text
NoteMovementUtils
├── applyPositionChange(Track&, EditManager&, targetStartTick, delta)   // existing moveNoteWithOverlapHandling
├── applyPitchChange(Track&, EditManager&, uint8_t newPitch)             // NEW — replaces NoteEditManager merge block
└── (length edit overlap: none today; length only extends mover — see snapshot rules)
```

`NoteEditManager` becomes a **thin adapter**: read fader CC → call `NoteMovementUtils::applyPitchChange` → update selection/bracket/fader feedback.

`EditPitchNoteState` / encoder path should call the same API in a follow-up (DRY completion).

### Shared pipeline (both position and pitch)

```text
1. Read session + mover geometry from EditManager::movingNote
2. Restore pitch-conflict neighbor notes from deletedNotes (pitch step only)
3. Classify overlaps vs other notes (same pitch + cross-pitch)
4. Apply shorten / contained delete via applyShortenOrDelete
5. Apply mover mutation (move events OR change pitch on span)
6. Restore neighbor notes no longer overlapping (restoreNotes)
7. Update movingNote.last* + session snapshot fields (rules below)
8. invalidateCaches / markEditFlatDirty
```

### Pitch-specific rules (inside shared engine)

| Case | Behavior |
|------|----------|
| Neighbor same **new** pitch, **adjacent**, **inside session span** | **Preserve** — do not merge, do not delete (inner note) |
| Neighbor same new pitch, adjacent, **outside** session span | **Lane merge** (D-delay) — extend mover span, delete neighbor pair |
| Neighbor overlapping, contained in mover span, same pitch | Contained delete + `deletedNotes` (same as move) |
| Cross-pitch, mover shorter, fully inside longer note | Short-over-long shorten + truncated-zone restore (existing) |

**Session span** = `sessionStart`..`sessionEnd` (see Decision 2), not stale `origEnd` from first 2×16th gate.

---

## Decision 2 — Session snapshot (Takes untouched)

**Locked in parent:** [note-edit-modification-session/design.md](../note-edit-modification-session/design.md) — **A1** split:

| Field | Role |
|-------|------|
| **commitBaseline** | Frozen at fader-1 select; used for pending commit + **EditChange** **NoteRef** |
| **sessionSpan** | Overlap / inner tests; `end` updates on length edit and position move |
| **last** | Live mover geometry |
| **overlapNotes** | hidden / shortened / visible (replaces `deletedNotes` vectors) |

**Length edit:** updates **sessionSpan.end** only until commit — not **commitBaseline.end**.

**Baseline:** full note map from **NoteEditSession.store** at select (not first overlap).

**Take boundary:** unchanged — edit RAM only until **saveEdit**.

### Event → snapshot table (normative)

| Event | active | sessionStart | sessionEnd | lastStart | lastEnd | deletedNotes |
|-------|--------|--------------|------------|-----------|---------|--------------|
| Fader-1 select note (new) | true, clear | = note start | = note end | = note | = note | clear |
| Position move | true | unchanged | = lastEnd after move | = target | = target+len | engine |
| Length edit | true | unchanged | **= new lastEnd** | unchanged | = new end | unchanged |
| Pitch change | true | unchanged | per lane-merge rule | = mover start after merge | = mover end | engine |
| Fader-1 reselect other | false | — | — | — | — | clear (or commit policy TBD) |
| Exit edit / commit | false | — | — | — | — | clear |

---

## Decision 3 — HITL AC2 (verifier)

Inner note **preserved** after pitch only if:

- Separate `Final note: pitch=A, start=a_tick, end=a_tick+gate±tol` appears in the post-pitch reconstruction window, **and**
- No `Merged N adjacent same-pitch notes into span` line in that window, **and**
- End − start ≤ `RECORD_GATE_TICKS + tick_tolerance` (not mega-span).

Implement in `scripts/host_midi_automation_edit_baseline.py` before firmware patch lands (regression guard).

---

## Architecture checkpoint (resolved)

| Question | Answer | Resolution |
|----------|--------|------------|
| Ownership change? | Yes | Pitch overlap moves to `NoteMovementUtils` |
| State transition change? | Yes | Documented session snapshot table; length edit updates `sessionEnd` |
| Take storage touched? | **No** | Edit RAM only |

Patch phase is **unlocked** for scoped implementation below.

---

## Patch phase tasks (ordered)

1. **Snapshot fields** — Add `sessionStart`/`sessionEnd` (or repurpose `orig*` with documented semantics); length edit updates `sessionEnd`.
2. **`applyPitchChange`** — Move overlap logic out of `NoteEditManager`; delete adjacent-merge duplicate.
3. **Inner vs lane merge** — Use session span in shared classifier.
4. **Tests** — Native unit tests for span + inner/lane classification (host-only, synthetic notes).
5. **HITL AC2** — Verifier tightening (can land first).
6. **Capture** — One `host_midi_automation_edit_baseline` run; attach serial log to BUG patch history.
7. **Docs** — Update `MOVE_NOTE_LOGIC.md` session table + pointer to this design (minimal delta).

---

## Validation

| Check | Command / criterion |
|-------|---------------------|
| Native | `pio test -e native` |
| Overlap round-trip | HITL edit baseline; AC1–AC8 in BUG.md |
| D-delay regression | insert/reorder checks in same script |
| Takes unchanged | No edits to `commitTake`, capture buffers, or Take I/O in this PR |

---

## Open questions (none blocking patch start)

- Rename `origStart`/`origEnd` → `sessionStart`/`sessionEnd` in one PR vs alias in comments first.
- `commitMovingNote` on fader switch: stub today — confirm still no-op for overlap session.

---

## Clarification — multi-step pitch + position without commit (2026-06-18)

**User scenario:** Moving note at **higher pitch** passes over a **shorter neighbor note** → pitch change **truncates the neighbor’s head** (first segment before mover start). Later, **same-pitch** overlap **cuts the tail** correctly. **Move back** (no fader-1 reselect, no exit edit) → **neighbor gone** — suggests chained pitch + position steps are not one coherent session.

**What the code does today (shipped):**

| Step | Path | Neighbor effect | Tracked in `movingNote.deletedNotes`? | Restored on move-back? |
|------|------|-----------------|--------------------------------------|-------------------------|
| Pitch overlap | `NoteEditManager` | If neighbor **starts before** mover: **shorten head** to `moverStart - 1`. If neighbor **starts after** mover start: **delete** (hidden) | Yes, via `applyShortenOrDelete` | **No** — restore loop runs only in `moveNoteWithOverlapHandling`, not after pitch |
| Same-pitch position overlap | `NoteMovementUtils` | Contained delete or shorten | Yes | Yes, if overlap tests pass |
| Pitch adjacent merge | `NoteEditManager` | Neighbor **deleted**, mover span extended | Sometimes (merge deletes neighbor) | Unreliable across later moves |

**Why “second half OK, move back gone” fits this:**

1. **Two different engines** — pitch step records one `DeletedNote` (often head shorten); a later position step may **delete the tail** without a restorable entry, or merge neighbor into mover.
2. **Restore only on position move** — pitch changes do not run the “move away → restore neighbors” pass; moving back is a position move but **mover pitch/span changed** since the pitch step, so overlap/restore tests fail (same failure mode as A@409 in root-cause capture).
3. **Not undo** — `movingNote.deletedNotes` is **overlap scratch state**, not `TrackUndo::pushUndoSnapshot`. Disappearing on move-back is **lost overlap restore**, not missing global undo.

**Design intent (unified engine):** One `deletedNotes` ledger and one restore pass after **every** geometry change (position **and** pitch), using **session span** + truncated-zone rules for both. Optional: split neighbor (head shortened + tail hidden) must keep **one restorable record** or **two linked entries** until mover clears the zone.

**Not the same as “commit”:** Commit = fader-1 reselect or exit edit (clears / finalizes `movingNote`). User means **no commit** — session should still restore neighbors when the mover leaves; that is **`movingNote` session continuity**, not `NoteEditSessionCommitted`.

**Vocabulary:** **moving note** + **overlap note**; store states **hidden** | **shortened** | **visible** — see `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc`.
