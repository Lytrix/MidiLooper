# Note edit focus driver drift — bugfix plan

Handoff for `captures/session_20260806_204643.log`. Related archived work: D19a (`edit-session-action-geometry`), `edit-focus-selection-drift`, `note_edit_scoped_display_handoff.md`.

## Symptoms (this capture)

| Symptom | When | Log anchor |
|--------|------|------------|
| Move fader does not move highlight | Select long pitch-65 note at storage 1185 (~130s, ~146s) | `focus.last: start=1185` never updates; `GEOM_APPLY,pipeline,...,0,3,0` |
| Other pitch-65 notes black out during move | Same session | `EditSessionAction: type=1/2 noteId=7 start=804…` (Shorten/Hide on inner note) |
| Note gone after NOTE_EDIT exit | ~154s | `NoteEditPassClosed editPass=0 edits=7` |
| “Same note still on screen” after first move | ~118s first successful move | See § Same-note-on-screen below |

## Core invariant

**LinearSpan** (plan term; represented in code by `NoteBaseline` and the cached `focus.last`) is the note's geometry in linear storage tick coordinates—pitch, velocity, `startTick`, and `endTick` (exclusive off tick)—for one `NoteId`, as resolved by `findLinearNoteSpanForNoteId()` from the session store. It is neither display-phase geometry nor bracket ticks.

The live edit driver is a **cached view of one linear note**.

The cache is reusable only while this chain describes the **same logical note**:

```
movingNoteId
      ↓
store lookup (findLinearNoteSpanForNoteId)
      ↓
resolved linear span
      ↓
focus.last
```

If any link in this chain breaks, the driver is **stale** and must be rebuilt before geometry processing.

D19a names `EditorSelection.primaryNote` as the edit driver identity. In practice, the cached driver is identified by **`(NoteId, LinearSpan)`**, not `NoteId` alone. D19a accidentally reduced driver identity to `NoteId` in the enforcement layer (`editorSelectionMatchesDriverNote`, `ensureNoteEditFocusForLiveEdit`, `liveEditDisplayNoteAtSelect`). Any mismatch between resolved store span and `focus.last` means the cache cannot safely be reused — even when `primaryNote == movingNoteId`.

Every implementation phase below enforces this invariant rather than patching isolated symptoms.

## Driver validity invariant

A live edit driver is **valid** iff all of the following hold:

| # | Condition |
|---|-----------|
| 1 | `movingNoteId == primaryNote` |
| 2 | `movingNoteId != kInvalidNoteId` |
| 3 | `movingNoteId` resolves to a note in the session store (`findLinearNoteSpanForNoteId` succeeds) |
| 4 | The resolved store linear span equals `focus.last` (pitch, start tick, end tick) |

**Native helper (proposed):** `isLiveEditDriverValid(sel, focus, sessionStore, channel, loopLength)` — returns true only when the full invariant holds. All geometry and display paths consult this before trusting cached focus.

When invalid: rebuild via `rebuildNoteEditFocusForDisplayNote` from the current selectable `DisplayNote` at `selectedNoteIdx`. Never synthesize a hybrid `DisplayNote` from mismatched `noteId` + `focus.last` ticks.

## Same-note-on-screen (log beginning)

**Not the same failure mode as the stuck 1185 highlight**, but related display/driver confusion on the same pitch lane.

### Boot / select-only phase (lines 1–113s wall clock)

- No `POSITION EDIT` or geometry pipeline in this phase — only select-fader navigation.
- `#CAP` DNTE shows **multiple real pitch-65 notes** in the loop simultaneously, e.g. storage 1185 (len 351), 1050/1044 (len ~95). These are separate materialized notes, not a ghost from a failed move.
- Selecting note index 2 at tick 570 then index 3 at 564 changes which bar is highlighted; the long note at 1185 remains visible because it was never selected or moved.

### First move (~118.608s)

- Selected note index 3 at tick 564 (`focus.last start=1044`).
- **Move succeeds:** `MoveNote noteId=7` 1044→996; `HideNote noteId=6` 1050–1145 (overlap).
- Post-move DNTE: single entry `65,996,516,95,2` — **no second bar at 1044** in capture.
- `GeometryPipeline: storeNoteOns=5 baselineMap=4` — store/baseline counts diverge on first apply (hidden overlap note still counted in store; see § Store health).

**Likely user perception:** while moving the short note, the **long note at 1185 stays on screen** (correct — different `NoteId`) and the **inner note at 1050 is hidden** (overlap blackout). On a dense pitch lane this reads as “the same note is still there” even though the capture shows one moved note, not a duplicate at the old tick.

**Conclusion:** early-log “duplicate” is primarily **multiple same-pitch notes + overlap hide preview**, not a broken note-on/off pair at 1044. The 1185 stuck-move bug is the stronger D19a driver-drift failure later in the same session.

## Root cause (ranked — not an identity-guard fallback)

### RC1 — D19a enforcement reduced driver identity to `NoteId` only (primary)

**Authority:** `openspec/changes/archive/2026-08-05-edit-session-action-geometry/design.md` § D19a.

The architectural failure: driver identity is **`(NoteId, LinearSpan)`**, but enforcement checks **`NoteId` only**.

`editorSelectionMatchesDriverNote` only checks `primaryNote == movingNoteId`. It does **not** verify that the store linear span for that `NoteId` matches `focus.last`.

In this capture, after re-selecting the long note:

- `focus.last` = 1185–1535 (from `DisplayNote` / select bracket)
- `focus.movingNoteId` = **7** (short inner note from prior edit on pitch 65)
- `primaryNote` = **7** (from pre-commit `noteIdFromFilteredDisplayNote` on a list that still projected participant 7)

`liveEditDisplayNoteAtSelect` then synthesizes an **inconsistent `DisplayNote`** (id and ticks from different logical notes):

```cpp
// noteId=7, ticks=1185–1535 — invalid composite
return {editSession.focus.movingNoteId, last.pitch, last.velocity, last.startTick, last.endTick};
```

Geometry pipeline runs on **noteId=7** with overlap actions at 804–896; move latch stays at 1185.

This is a **driver validity invariant violation**, not a missing coordinate fallback in `findBaselineNoteIdForDisplay` alone.

### RC2 — Select path may use two sources for identity (secondary)

`SelectFaderInput` (select apply):

1. `selectNoteId` from **pre-commit** filtered list (`noteIdFromFilteredDisplayNote(notes, noteIdx)`)
2. `rebuildNoteEditFocusForDisplayNote(selectedNote)` from **post-commit** list
3. `applySelectNav(..., selectNoteId)` — session selection uses pre-commit id

One plausible path is that projection overlays `movingNoteId` onto the wrong display row before commit, so pre-commit and post-commit `NoteId` diverge. The capture is **consistent with** this contributing to RC1, but RC1 alone explains the observed failure once IDs align with mismatched spans.

**Fix direction:** single post-rebuild `DisplayNote` drives **both** `movingNoteId` and `primaryNote`.

### RC3 — `ensureNoteEditFocusForLiveEdit` skips rebuild when IDs match

```cpp
if (focusMatchesSelection) return; // ID match only — insufficient for driver validity
```

After RC1, IDs can match while spans differ → no rebuild before every move/pitch fader tick.

### RC4 — Display projection participant overlay (secondary)

`projectNoteEditDisplayNotes` can leave a committed row and add a participant row when `noteId` binding fails (`note_edit_scoped_display_handoff.md`). The capture is **consistent with** wrong `NoteId` on a visible row feeding RC2. Not the primary explanation for the 1185/7 span split once IDs align.

### RC5 — Broken MIDI event vector (investigate, not primary)

**Evidence against (this capture):**

- No `note-on missing noteId`, `orphan`, or `LinearNote` repair lines.
- First move at 118.608 applies clean `MoveNote` + `HideNote`; DNTE updates to new tick.
- `findLinearNoteSpanForNoteId` resolves note 7 consistently at 804–896 in later actions.

**Evidence for further audit:**

- `storeNoteOns` vs `baselineMap` size mismatches (e.g. `storeNoteOns=5 baselineMap=4` at 118.608; `storeNoteOns=4 baselineMap=5` at 132s+). Consistent with **hidden overlap notes still in `baselineMap`** or transient hide/remove in session store — not necessarily unpaired on/off.
- Worth a native assert: every session store note-on has `noteId != kInvalidNoteId` and a resolvable off.

**Verdict:** treat store pair health as **Phase 4 only** (see entry conditions below). Do not block Phases 1–3 on store audit.

## What we will not do first

| Avoid | Why |
|-------|-----|
| Coordinate-matching fallback only in `findBaselineNoteIdForDisplay` | Symptom patch; does not restore driver validity or fix select pre/post-commit split |
| New top-level module / noun | D19a already names owners |
| Full `validateAndCleanupMidiEvents` on geometry tick | Forbidden on hot path per `Loop-MIDI-Storage-And-Validation` |

## Fix strategy (architectural)

Each phase enforces the **driver validity invariant** or a projection ownership invariant derived from it.

### Phase 0 — Pin regression (native)

Build a minimal fixture from this capture:

- Loop length 1536; pitch-65 notes: inner ~804–899 (`noteId=7`), middle ~1044–1139, outer ~1185–1535 (`noteId` TBD from materialize).
- Sequence: edit inner note 7 → select outer at 1185 → move left.

**Primary assertion:** `isLiveEditDriverValid(...)` is true after select rebuild and remains true through geometry apply. This single check subsumes per-field asserts on `movingNoteId`, `focus.last`, and geometry target alignment.

**Secondary assertions:**

- geometry emits `MoveNote` on outer `NoteId`
- projection ownership invariant (see Phase 3) holds on projected display list

Optional capture telemetry (SESSION_CAPTURE): one line on select apply: `primaryNote`, `movingNoteId`, `focus.last` start/end, `isLiveEditDriverValid`, `DisplayNote.noteId` + ticks at selected index.

### Phase 1 — Enforce driver validity invariant

**Owner:** `EditManager` — `liveEditDisplayNoteAtSelect`, `ensureNoteEditFocusForLiveEdit`.

1. Add `isLiveEditDriverValid(sel, focus, sessionStore, channel, loopLength)` per § Driver validity invariant.
2. `liveEditDisplayNoteAtSelect`: return cached `focus.last` view **only** when `isLiveEditDriverValid` is true; else return selectable list note at `selectedNoteIdx` (never synthesize inconsistent id + ticks).
3. `ensureNoteEditFocusForLiveEdit`: rebuild when `!isLiveEditDriverValid(...)` (replace ID-only `editorSelectionMatchesDriverNote` check).

### Phase 2 — Select apply single source of truth

**Owner:** `SelectFaderInput` select apply path.

Reorder:

1. `commitAllPendingNoteEditActions`
2. `notesAfterCommit = selectableDisplayNotesForEditUi`
3. Resolve `selectedNote` by slot/tick/index on **post-commit** list only
4. `rebuildNoteEditFocusForDisplayNote(track, selectedNote)`
5. `applySelectNav(track, bracketTick, selectedNote.noteId)` — **same** `NoteId` as rebuild
6. Assert `isLiveEditDriverValid(...)` before returning from select apply

Remove use of pre-commit `selectNoteId` for `applySelectNav` (or assert it equals post-rebuild id).

### Phase 3 — Projection ownership invariant

**Owner:** `projectNoteEditDisplayNotes` (`NoteEditFocusDisplayProjection.cpp`).

After overlay, the projected display list must satisfy:

| Rule | Meaning |
|------|---------|
| Each visible `NoteId` appears **exactly once** | No duplicate bars for the same object identity |
| Each participant appears **exactly once** | `movingNoteId` + `changedOverlapNoteIds` overlay does not double-insert |
| No committed row **and** a projected participant row for the same logical note | Participant update must replace/bind the committed row, not add alongside it |

Implementation notes:

- If committed row has `kInvalidNoteId` but matches participant baseline, bind in place (existing path); do not `push_back` duplicate.
- Native test from `note_edit_scoped_display_handoff.md` (display count must not grow).

### Phase 4 — Store/baseline audit (secondary investigation)

**Entry condition:** Only investigate the session store if **driver validity invariants hold** (Phases 1–2) and **projection ownership invariant holds** (Phase 3), but the bug still reproduces on hardware or in an extended fixture.

**Owner:** `populateBaselineMapForEditClosure`, `NoteGeometryResolver` debug.

- Log or assert `storeNoteOns` vs visible baseline entries after hide actions.
- Confirm hidden overlap notes are removed from participant projection and do not leave duplicate committed rows.
- Only if orphans found: fix pairing in `applyEditSessionActions` / `stampNoteIdsOntoPairedNoteOffs` path.

## Verification

| Gate | Command / action |
|------|------------------|
| Native regression | `pio test -e native` — `isLiveEditDriverValid` fixture + `test_note_edit_focus`, `test_apply_edit_session_actions` |
| Build | `pio run -e teensy41-capture-serial` |
| HITL | Reproduce pitch-65 lane: move inner note, select outer, move outer; exit NOTE_EDIT — note count unchanged |
| Capture check | No `GEOM_APPLY,pipeline,...,0,3,0` when moving selected outer note; `EditSessionAction` includes `MoveNote` on correct id; driver valid after each select |

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Owner module | `EditManager::isLiveEditDriverValid`, `liveEditDisplayNoteAtSelect`, `ensureNoteEditFocusForLiveEdit`; `SelectFaderInput` select apply |
| Primary invariant | Cached edit driver = `(NoteId, LinearSpan)`; invalid cache must rebuild before geometry |
| Ownership change? | **NO** — enforce existing D19a contract on `EditManager` + `NoteEditFocus` |
| State transition change? | **NO** — select rebuild and geometry entry preconditions only |
| Behavior-preserving? | **YES** — when driver is already valid, no rebuild churn |

## Open items before implementation

1. Pin outer-note `NoteId` at 1185 in fixture (materialize from loop fixture or capture slice).
2. Confirm whether `findBaselineNoteIdForDisplay` first branch (`dn.noteId in baselineMap`) should require span match — only if Phases 1–2 leave a gap after `isLiveEditDriverValid` enforcement.

## Related docs

- `docs/plans/note_edit_scoped_display_handoff.md` — projection duplicate / participant scope
- `docs/plans/note_edit_pitch_lane_highlight_bugfix.md` — highlight index vs `primaryNote`
- `openspec/changes/archive/2026-08-05-edit-session-action-geometry/ARCHITECTURE-REVIEW.md` — D19a gate

## Architecture review (incorporated)

Review feedback integrated above: explicit `(NoteId, LinearSpan)` driver identity, driver validity invariant section, softened RC2/RC4 wording, `isLiveEditDriverValid` naming, Phase 0 invariant assertion, broadened projection ownership rules, Phase 4 entry conditions gated on invariant compliance.
