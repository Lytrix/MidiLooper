# Note edit post-geometry follow-up — index (RC7 / RC8)

**Driver drift (RC1):** closed — [`note_edit_focus_driver_drift_bugfix.md`](note_edit_focus_driver_drift_bugfix.md) (FROZEN).

**Projection ownership (RC6):** active — [`note_edit_projection_ownership_bugfix.md`](note_edit_projection_ownership_bugfix.md).

This document indexes **remaining** post-geometry work that is **not** projection paint (RC6). Use the debugging boundary from the projection doc: trust `EditSessionAction` rows unless pipeline apply fails.

---

## Implementation status

| Track | Phase | Status | Doc |
|-------|-------|--------|-----|
| Macro commit guard | Phase 1 (RC7) | **Shipped** | § Phase 1 below |
| Projection ownership | Phase 2 (RC6) | Open | [`note_edit_projection_ownership_bugfix.md`](note_edit_projection_ownership_bugfix.md) |
| Reconstruction parity | Phase 3 (RC8) | Open | § Phase 3 below |

Recommended order: **RC6 (projection) → RC8 (recon)**. Phase 1 (RC7) shipped to stop invalid macro commits from obscuring projection debugging.

---

## RC7 — Select-after-move macro commit (Phase 1 ✅ shipped)

**Primary capture:** `captures/session_20260806_212810.log` (~235s ghost `DNTE,65,0,0,47,0`, `NoteRange start=0`).

**Owners:** `SelectFaderInput` select apply; `NoteEditSessionCommit.cpp`; `NoteEditFocusPreCommit.cpp`.

**Shipped guards:**

1. `isLiveEditDriverValid` before macro commit; rebuild when invalid
2. Pre-commit rejects `mover_focus` **NoteRange** when `startTick == 0` or `startTick >= loopLength`
3. Defer macro commit during geometry editing-activity hold (`isNoteEditMacroCommitDeferred`, 750 ms)
4. Native tests: `test_pre_commit_rejects_mover_note_range_zero_start_after_nonzero_baseline`, `test_pre_commit_emits_valid_mover_note_range`

**HITL gate (open):** after geometry move, select different note — no `DNTE,65,0,0,…`; no `NoteRange start=0`. `214302` did not hit this path (Phase 1 may be effective or scenario differed).

---

## RC8 — Session store reconstruction miss (Phase 3 — open)

**Primary capture:** `captures/session_20260806_212810.log` — `M65@369 missing in recon`, `M65@1050 missing in recon`.

**Entry condition:** RC6 projection ownership holds on native fixture; RC7 guards in place.

**Owners:** `populateBaselineMapForEditClosure`, `applyOwnedEditPassRows`, `commitEditAction` recon trace.

**Tasks:**

- Log or assert `storeNoteOns` vs visible baseline entries after hide/shorten actions
- Pin `212810` slice fixture: after overlap shorten + move + commit, `reconstructNotes(session_store)` contains expected pitch-65 notes at committed linear spans
- Fix pairing only if orphans found in `applyEditSessionActions` / `stampNoteIdsOntoPairedNoteOffs`

---

## Open items

1. **HITL RC7** — confirm Phase 1 blocks `212810` ghost path on device
2. **RC6 first** — complete projection ownership before deep recon audit (cleaner boundary)
3. **Pin `212810` recon fixture** — after RC6 lands

---

## Related docs

- [`note_edit_focus_driver_drift_bugfix.md`](note_edit_focus_driver_drift_bugfix.md) — FROZEN RC1
- [`note_edit_projection_ownership_bugfix.md`](note_edit_projection_ownership_bugfix.md) — active RC6
- `docs/plans/note_edit_scoped_display_handoff.md` — participant bind rules
