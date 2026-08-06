# Note edit overlap projection — follow-up investigation

This investigation assumes the **driver validity invariant** introduced by the driver-drift fix is already enforced. See [`note_edit_focus_driver_drift_bugfix.md`](note_edit_focus_driver_drift_bugfix.md) for the completed RC1–RC3 work and `(NoteId, LinearSpan)` architecture.

Driver-span drift and geometry pipeline apply failures (`GEOM_APPLY,pipeline,…,0,3,0`) are **resolved** (PR #15).

The remaining NOTE_EDIT issues originate in **display projection**, **select relatch / macro commit**, or **reconstruction** — not in driver cache identity.

**Primary capture:** `captures/session_20260806_212810.log`  
**Validation capture (RC1 fixed):** `captures/session_20260806_212448.log`

No new top-level ownership. Work stays within existing D19a owners:

- `EditManager`
- `SelectFaderInput`
- `NoteEditFocusDisplayProjection`
- `NoteEditSessionCommit`
- `applyOwnedEditPassRows` (reconstruction trace)

---

## Symptoms (`212810`)

| Symptom | When | Log anchor | RC |
|--------|------|------------|-----|
| Length shorten/delete flicker while moving inner note | ~219–221s | Repeated `type=1 noteId=3` Length on outer note; `type=0 noteId=3` Delete at 221.282 | RC6 |
| Selection index jumps during geometry | ~219–234s | `Note selection changed: 4 → 3 → 2 → 1` during position fader | RC6 |
| Ghost segment at tick 0 after select | ~235.1s | `DNTE,65,0,0,47,0` immediately after select at tick 369 | RC7 |
| Bad macro commit on select relatch | ~235.1s | `non-canonical store (check=16)`; `NoteRange start=0 end=1145` on `noteId=3` | RC7 |
| Note empty after select / exit | ~235.1s | `Select fader: selected empty step at tick 369`; `M65@369 missing in recon` | RC7 + RC8 |
| Long note move at 1185 works | ~234.2s | `GEOM_APPLY,pipeline,…,1,3,0`; `type=3 noteId=3` MoveNote | driver drift **fixed** |

**What the captures show:** invalid macro commit, ghost projection, and reconstruction miss. These observations are **consistent with** store corruption obscuring debugging, but they do not yet prove store corruption as root cause.

---

## Root causes

Numbering continues from the driver-drift investigation (RC6–RC8) for traceability.

### RC6 — Overlap shorten cascade + display list reorder

**Not driver drift.** Geometry commits succeed (`GEOM_APPLY,pipeline,…,1,3,0` throughout `212810`).

When the user selects the **short** pitch-65 note at 1044 (`noteId=7`, length 101) and moves it left into the **outer** long note (`noteId=3`, 609–959+):

1. Each position step emits `type=1 noteId=3` **Length** rows shortening the outer note (`609 end=947`, then `899`, `851`, …).
2. Eventually `type=0 noteId=3` **Delete** at 221.282 when the outer note is fully consumed.
3. `Note selection changed: 4 → 3 → 2 → 1` fires during geometry — filtered display list reindexes while `focus.last` tracks the mover.
4. `DNTE` alternates segment lengths (350, 101, 89) on the same pitch lane — reads as “weird length show/remove” even when store actions are correct.

**Hypothesis:** overlap geometry is working; **display projection** does not consistently replace shortened/hidden outer-note rows with live store spans. Projection ownership rules are violated or incomplete for overlap-shortened participants.

**Owner:** `projectNoteEditDisplayNotes` (`NoteEditFocusDisplayProjection.cpp`).

### RC7 — Select-after-move macro commit with invalid `NoteRange`

**Sequence (wall clock, `212810`):**

| Time | Event |
|------|-------|
| 234.303 | Long note move succeeds: `noteId=3 start=1233 end=1583`; driver valid |
| 234.842 | Clean macro commit: `NoteRange 1233–1583` on `noteId=3` |
| 235.126 | Select fader: note at tick **369** (`note_idx=2`) |
| 235.128 | `DNTE,65,0,0,47,0` — ghost at tick 0 |
| 235.144 | `NOTE_EDIT macro commit: non-canonical store (check=16)` |
| 235.144 | `NoteRange start=0 end=1145` on `noteId=3` — invalid mover span |
| 235.156 | `selected empty step at tick 369` |

**Hypothesis:** select relatch after geometry drives macro commit from **stale `mover_focus`** while `primaryNote` / filtered list / `focus.last` disagree. `isLiveEditDriverValid` may pass ID+span checks against store while the pre-commit `mover_focus` row is built from a projection that no longer matches the selectable note at the new bracket.

**Gap from driver-drift Phase 2:** `applySelectNav` uses post-commit `NoteId`, but macro commit on subsequent select relatch is not guarded and pre-commit `mover_focus` span is not validated.

**Owners:** `SelectFaderInput` select apply; `NoteEditSessionCommit` pre-commit row builder (`mover_focus` source).

### RC8 — Session store reconstruction miss after commit

Repeated at exit and mid-session select:

```
commitEditAction session_store: M65@369 missing in recon flatEvents=13
commitEditAction session_store: M65@1050 missing in recon flatEvents=13
```

Pre-commit rows can look plausible (`NoteRange 1233–1583`) while materialized replay does not contain the note at the expected start tick. User sees note **gone** after NOTE_EDIT exit or after select relatch.

**Distinct from driver drift:** driver validity holds; geometry applies; commit runs — but post-commit projection / materialize loses the note.

**Owner:** `applyOwnedEditPassRows`, `commitEditAction` recon trace, `populateBaselineMapForEditClosure`.

---

## Implementation phases

Recommended order:

```
Phase 1 (macro commit guard)
        ↓
Phase 2 (projection ownership)
        ↓
Phase 3 (reconstruction audit)
```

Phase 1 removes one source of invalid pre-commit state before projection and reconstruction debugging. Phase 2 should be easier to reason about once invalid macro commits are blocked. Phase 3 runs only after commit and projection behavior are trustworthy.

| Follow-up phase | Maps to driver-drift doc | Focus |
|-----------------|--------------------------|-------|
| Phase 1 | Phase 5 | Macro commit guard (RC7) |
| Phase 2 | Phase 3 | Projection ownership (RC6) |
| Phase 3 | Phase 4 | Reconstruction parity (RC8) |

### Phase 1 — Macro commit guard (RC7) ✅ shipped

**Owner:** `SelectFaderInput` select apply; `NoteEditSessionCommit.cpp`; `NoteEditFocusPreCommit.cpp`.

1. After select apply rebuild, call `isLiveEditDriverValid` — if false, rebuild from `selectedNote` and do not macro-commit until valid.
2. In pre-commit row builder: reject `mover_focus` **NoteRange** when `startTick == 0` or `startTick >= loopLength` (unless explicit wrap move documented in focus).
3. Defer macro commit on select apply while geometry editing-activity hold is active (`editingActivity` / 750 ms post-`GEOM_APPLY`).
4. SESSION_CAPTURE telemetry (optional): `SELECT_APPLY driver_valid=… primary=… moving=… focus_last=… selected_ticks=… pre_commit_start=…`.

**Native test:** after geometry move with valid driver, select at different bracket — assert no pre-commit row with `start=0`; assert `isLiveEditDriverValid` true before commit.

**Rationale:** prevents invalid macro commits from obscuring the remaining investigation; does not by itself prove or fix store corruption.

### Phase 2 — Projection ownership invariant (RC6)

**Owner:** `projectNoteEditDisplayNotes` (`NoteEditFocusDisplayProjection.cpp`).

After overlay, the projected display list must satisfy:

| Rule | Meaning |
|------|---------|
| Each visible `NoteId` appears **exactly once** | No duplicate bars for the same object identity |
| Each participant appears **exactly once** | `movingNoteId` + `changedOverlapNoteIds` overlay does not double-insert |
| No committed row **and** a projected participant row for the same logical note | Participant update must replace/bind the committed row, not add alongside it |
| Shortened overlap note shows **live store span** | After `type=1` Length on outer note, `DNTE` length must match store end − start, not pre-shorten length |

Implementation notes:

- If committed row has `kInvalidNoteId` but matches participant baseline, bind in place; do not `push_back` duplicate.
- When outer note is shortened by inner-note move, update or hide the committed row — do not leave full-length bar (350 ticks) while store has shortened end.
- Native test from `note_edit_scoped_display_handoff.md` (display count must not grow).
- **New native test:** pitch-65 lane with outer `noteId=3` (609–959) + inner `noteId=7` (1044–1145); move inner left 3 steps; assert one bar per `NoteId`, outer bar length decreases monotonically in projection.

**HITL gate:** moving inner note through outer — no full-length ghost of outer note; selection index stable or explicitly follows mover only.

### Phase 3 — Reconstruction parity (RC8)

**Entry condition:** Phase 1 guards in place; Phase 2 projection ownership holds on native fixture.

**Owner:** `populateBaselineMapForEditClosure`, `applyOwnedEditPassRows`, `commitEditAction` recon trace.

- Log or assert `storeNoteOns` vs visible baseline entries after hide/shorten actions.
- Pin `212810` slice fixture: after overlap shorten + move + commit, `reconstructNotes(session_store)` contains expected pitch-65 notes at committed linear spans — no `missing in recon`.
- Only if orphans found: fix pairing in `applyEditSessionActions` / `stampNoteIdsOntoPairedNoteOffs` path.

---

## Verification

| Gate | Command / action | Phase |
|------|------------------|-------|
| Native regression | `pio test -e native` — macro commit guard + overlap projection + recon fixtures | 1–3 |
| Build | `pio run -e teensy41-capture-serial` | all |
| HITL — RC6 | Move inner note through outer; no full-length outer ghost | 2 |
| HITL — RC7 | After geometry move, select different note; no `DNTE,65,0,0,…`; no `NoteRange start=0` | 1 |
| HITL — RC8 | Exit NOTE_EDIT; note count unchanged; no `missing in recon` for edited lane | 3 |
| Capture (`212810` class) | 0× `non-canonical store` on select relatch; no ghost `DNTE` at tick 0 | 1–2 |

Overlap shorten may still emit `type=1` Length rows in the store — that is expected geometry. Display must match store span after each step.

---

## Open items

1. **Pin `212810` fixture** — pitch-65 notes: outer `noteId=3` (609–959+), inner `noteId=7` (1044–1145), select-at-369 after move-to-1233 sequence.
2. **Trace `mover_focus` pre-commit** — which field supplies `start=0` at 235.144 (`focus.last`, `movingNoteRange`, or projection bracket).
3. **Editing-activity hold** — confirm owner and whether select apply respects it today (`SelectFaderInput` / `EditManager`).

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Owner module | `NoteEditFocusDisplayProjection`, `NoteEditSessionCommit`, `SelectFaderInput`, `applyOwnedEditPassRows` |
| Primary invariant | Projection ownership (Phase 2); valid pre-commit spans before macro commit (Phase 1) |
| Ownership change? | **NO** — extend existing owners; no new coordination layer |
| State transition change? | **Phase 1 only** — defer macro commit during editing-activity hold |
| Behavior-preserving? | **YES** for valid paths; Phase 1 blocks invalid commits only |

---

## Related docs

- [`note_edit_focus_driver_drift_bugfix.md`](note_edit_focus_driver_drift_bugfix.md) — completed driver validity investigation (RC1–RC3)
- `docs/plans/note_edit_scoped_display_handoff.md` — projection duplicate / participant scope
- `docs/plans/note_edit_pitch_lane_highlight_bugfix.md` — highlight index vs `primaryNote`
