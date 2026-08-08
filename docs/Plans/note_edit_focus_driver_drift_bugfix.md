# Note edit focus driver drift — completed investigation (FROZEN)

**Status: FROZEN** — RC1 driver drift investigation closed. Do not extend this document for new bugs; misattribution to driver drift is a process failure.

Architectural reference for the driver validity model. Primary capture: `captures/session_20260806_204643.log`.

Related archived work: D19a (`edit-session-action-geometry`), `edit-focus-selection-drift`, `note_edit_scoped_display_handoff.md`.

**Shipped:** PR #15 (`bugfix/note-edit-focus-driver-drift` → `dev`).

**Downstream work** (display projection, select relatch, reconstruction) is tracked separately — see Related docs.

---

## Status

Driver drift is **resolved** in PR #15.

The original geometry pipeline failure (`GEOM_APPLY,pipeline,…,0,3,0`) no longer reproduces in post-fix captures (`212448`, `212810`, `214302`).

Remaining NOTE_EDIT issues visible after that fix (overlap display flicker, ghost projection, reconstruction miss) are **distinct investigations** with a clean debugging boundary: geometry through `EditSessionAction` is trusted; display/commit/recon after that line is not.

---

## Symptoms (original capture — `204643`)

| Symptom | When | Log anchor |
|--------|------|------------|
| Move fader does not move highlight | Select long pitch-65 note at storage 1185 (~130s, ~146s) | `focus.last: start=1185` never updates; `GEOM_APPLY,pipeline,...,0,3,0` |
| Other pitch-65 notes black out during move | Same session | `EditSessionAction: type=1/2 noteId=7 start=804…` (Shorten/Hide on inner note) |
| Note gone after NOTE_EDIT exit | ~154s | `NoteEditPassClosed editPass=0 edits=7` |
| “Same note still on screen” after first move | ~118s first successful move | See § Same-note-on-screen below |

## Verification (post-fix)

| Capture | Verdict | Key evidence |
|---------|---------|--------------|
| `session_20260806_204643.log` | **Broken baseline** | 504× `GEOM_APPLY,pipeline,…,0,3,0`; `geometry pipeline did not apply move` at 1185 |
| `session_20260806_212448.log` | **Fixed** | 0 pipeline failures; `Moving note start=1185`; `type=3 noteId=3` MoveNote |
| `session_20260806_212810.log` | **RC1 fixed** | 0 pipeline failures; long note moves at 234s (display issues → projection doc) |
| `session_20260806_214302.log` | **RC1 fixed** | 0 pipeline failures; long-note moves clean; overlap cascade only after inner-note move |

---

## Final validation (`214302`)

Final validation capture after driver-drift fix + macro-commit guard (Phase 1 of commit/projection follow-up). Confirms RC1 is closed; symptoms that remain are **not** driver drift.

| Check | Result |
|-------|--------|
| `GEOM_APPLY,pipeline,…,0,3,0` | **0** (geometry applies on every move step) |
| Long note move (`noteId=3`, 609–959) | `overlapNotes=0`; only `type=3` MoveNote; `DISP` stays 5 painted notes |
| Driver validity at select | `DNTE,65,609,849,350,2` — storage start matches store span, not bracket-only composite |
| `NoteRange start=0` / ghost `DNTE,65,0,0,…` | **None** in this capture |
| Breakpoint | Overlap cascade starts only when moving **inner** note `noteId=7` at 1044 into outer `noteId=3` (~21.7s) |

**Store at NOTE_EDIT entry (not a driver bug):** loop has **5 notes** on pitch 65 (`DISP,…,5,5,5,5`). Nested overdub notes (`noteId=6` 89 ticks at 1050, `noteId=7` 101 ticks at 1044) sit under the long outer note on the piano roll — a rendering stack, not missing stop-path cleanup.

**What `214302` proves:** geometry and driver cache are trustworthy through `EditSessionAction`. `DISP` oscillating 4↔5 while `sourceEventCount` stays 5, `DNTE` alternating 350/101/89 lengths, and notes appearing/disappearing on first inner-note move are **downstream of geometry** — projection ownership (RC6), not RC1.

**Misattribution guard:** if a new capture shows `GEOM_APPLY,pipeline,…,0,3,0` or `focus.last` stuck while `movingNoteId` and store span disagree, reopen driver drift. If pipeline apply succeeds and `EditSessionAction` rows look correct, do **not** revisit this document.

---

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

---

## Driver validity invariant

A live edit driver is **valid** iff all of the following hold:

| # | Condition |
|---|-----------|
| 1 | `movingNoteId == primaryNote` |
| 2 | `movingNoteId != kInvalidNoteId` |
| 3 | `movingNoteId` resolves to a note in the session store (`findLinearNoteSpanForNoteId` succeeds) |
| 4 | The resolved store linear span equals `focus.last` (pitch, start tick, end tick) |

**Native helper:** `isLiveEditDriverValid(sel, focus, sessionStore, channel, loopLength)` — returns true only when the full invariant holds. All geometry and display paths consult this before trusting cached focus.

When invalid: rebuild via `rebuildNoteEditFocusForDisplayNote` from the current selectable `DisplayNote` at `selectedNoteIdx`. Never synthesize a hybrid `DisplayNote` from mismatched `noteId` + `focus.last` ticks.

---

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
- `GeometryPipeline: storeNoteOns=5 baselineMap=4` — store/baseline counts diverge on first apply (hidden overlap note still counted in store).

**Likely user perception:** while moving the short note, the **long note at 1185 stays on screen** (correct — different `NoteId`) and the **inner note at 1050 is hidden** (overlap blackout). On a dense pitch lane this reads as “the same note is still there” even though the capture shows one moved note, not a duplicate at the old tick.

**Conclusion:** early-log “duplicate” is primarily **multiple same-pitch notes + overlap hide preview**, not a broken note-on/off pair at 1044. The 1185 stuck-move bug is the stronger D19a driver-drift failure later in the same session.

---

## Root cause (ranked)

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

### RC2 — Select path used two sources for identity (secondary)

`SelectFaderInput` (select apply, pre-fix):

1. `selectNoteId` from **pre-commit** filtered list (`noteIdFromFilteredDisplayNote(notes, noteIdx)`)
2. `rebuildNoteEditFocusForDisplayNote(selectedNote)` from **post-commit** list
3. `applySelectNav(..., selectNoteId)` — session selection used pre-commit id

The capture is **consistent with** this contributing to RC1, but RC1 alone explains the observed failure once IDs align with mismatched spans.

**Fix:** single post-rebuild `DisplayNote` drives **both** `movingNoteId` and `primaryNote` (Phase 2).

### RC3 — `ensureNoteEditFocusForLiveEdit` skipped rebuild when IDs matched

```cpp
if (focusMatchesSelection) return; // ID match only — insufficient for driver validity
```

After RC1, IDs could match while spans differ → no rebuild before every move/pitch fader tick.

### RC4 — Display projection participant overlay (secondary, not primary for 1185)

`projectNoteEditDisplayNotes` can leave a committed row and add a participant row when `noteId` binding fails (`note_edit_scoped_display_handoff.md`). The capture is **consistent with** wrong `NoteId` on a visible row feeding RC2. Not the primary explanation for the 1185/7 span split once IDs align. Overlap projection issues are tracked in the follow-up doc.

### RC5 — Broken MIDI event vector (investigated, not primary for driver drift)

**Evidence against (this capture):**

- No `note-on missing noteId`, `orphan`, or `LinearNote` repair lines.
- First move at 118.608 applies clean `MoveNote` + `HideNote`; DNTE updates to new tick.
- `findLinearNoteSpanForNoteId` resolves note 7 consistently at 804–896 in later actions.

**Evidence for further audit:**

- `storeNoteOns` vs `baselineMap` size mismatches (e.g. `storeNoteOns=5 baselineMap=4` at 118.608). Consistent with **hidden overlap notes still in `baselineMap`** — not necessarily unpaired on/off.

**Verdict:** not the driver-drift root cause. Store/reconstruction audit is in the follow-up doc if needed after projection and commit guards land.

---

## What we did not do

| Avoid | Why |
|-------|-----|
| Coordinate-matching fallback only in `findBaselineNoteIdForDisplay` | Symptom patch; does not restore driver validity or fix select pre/post-commit split |
| New top-level module / noun | D19a already names owners |
| Full `validateAndCleanupMidiEvents` on geometry tick | Forbidden on hot path per `Loop-MIDI-Storage-And-Validation` |

---

## Implementation (shipped)

| Phase | Owner | Change |
|-------|-------|--------|
| 0 — Native regression | `test_note_edit_focus` | `test_is_live_edit_driver_valid_rejects_id_match_span_mismatch` |
| 1 — Driver validity | `EditManager`, `NoteEditFocusState.cpp` | `isLiveEditDriverValid`; enforce in `liveEditDisplayNoteAtSelect`, `ensureNoteEditFocusForLiveEdit` |
| 2 — Select single source | `SelectFaderInput.cpp` | `applySelectNav(..., selectedNote.noteId)` after post-commit rebuild |

No new architectural ownership was introduced. Work stays within D19a owners: `EditManager`, `SelectFaderInput`, `NoteEditFocus`.

---

## Verification gates (driver drift)

| Gate | Result |
|------|--------|
| `pio test -e native` | `isLiveEditDriverValid` regression passes |
| HITL / capture | 0× `GEOM_APPLY,pipeline,…,0,3,0` when moving selected outer note at 1185 |
| HITL / capture | `EditSessionAction` includes `MoveNote` on correct `NoteId`; driver valid after select rebuild |

---

## Architecture checkpoint (final)

| Question | Answer |
|----------|--------|
| Owner module | `EditManager::isLiveEditDriverValid`, `liveEditDisplayNoteAtSelect`, `ensureNoteEditFocusForLiveEdit`; `SelectFaderInput` select apply |
| Primary invariant | Cached edit driver = `(NoteId, LinearSpan)`; invalid cache must rebuild before geometry |
| Ownership change? | **NO** — enforce existing D19a contract on `EditManager` + `NoteEditFocus` |
| State transition change? | **NO** — select rebuild and geometry entry preconditions only |
| Behavior-preserving? | **YES** — when driver is already valid, no rebuild churn |

---

## Related docs

- [`note_edit_projection_ownership_bugfix.md`](note_edit_projection_ownership_bugfix.md) — **active** RC6 projection ownership (Phase 2)
- [`note_edit_overlap_projection_followup.md`](note_edit_overlap_projection_followup.md) — RC7 macro commit + RC8 reconstruction (index)
- `docs/Plans/note_edit_scoped_display_handoff.md` — projection duplicate / participant scope
- `docs/Plans/note_edit_pitch_lane_highlight_bugfix.md` — highlight index vs `primaryNote`
- `openspec/changes/archive/2026-08-05-edit-session-action-geometry/ARCHITECTURE-REVIEW.md` — D19a gate
