# Note edit post-geometry follow-up — index (RC7 / RC8)

**Driver drift (RC1):** closed — [`note_edit_focus_driver_drift_bugfix.md`](note_edit_focus_driver_drift_bugfix.md) (FROZEN).

**Projection ownership (RC6):** active — [`note_edit_projection_ownership_bugfix.md`](note_edit_projection_ownership_bugfix.md).

This document indexes **remaining** post-geometry work that is **not** projection paint (RC6). Use the debugging boundary from the projection doc: trust `EditSessionAction` rows unless pipeline apply fails.

---

## Implementation status

| Track | Phase | Status | Doc |
|-------|-------|--------|-----|
| Macro commit guard Phase 1 (RC7) | **Shipped** | § RC7 Phase 1 below |
| Select bracket mismatch guard (RC7b) | **Shipped** (HITL gate open) | [`note_edit_select_commit_bracket_bugfix.md`](note_edit_select_commit_bracket_bugfix.md) |
| Projection ownership (RC6) | **Shipped** | [`note_edit_projection_ownership_bugfix.md`](note_edit_projection_ownership_bugfix.md) |
| Overlap restore span (RC9) | Open | [`note_edit_overlap_restore_span_bugfix.md`](note_edit_overlap_restore_span_bugfix.md) |
| Selection index stability (RC10) | **Shipped** | [`note_edit_selection_index_stability_bugfix.md`](note_edit_selection_index_stability_bugfix.md) |
| Reconstruction parity (RC8) | Open | § RC8 below |

Recommended order: **RC9 → RC8**. RC6, RC7, RC7b, RC10 shipped.

---

## RC7 — Macro commit guards

### Phase 1 ✅ shipped

**Primary capture:** `captures/session_20260806_212810.log` (~235s ghost `DNTE,65,0,0,47,0`, `NoteRange start=0`).

**Owners:** `SelectFaderInput` select apply; `NoteEditSessionCommit.cpp`; `NoteEditFocusPreCommit.cpp`.

**Shipped guards:**

1. `isLiveEditDriverValid` before macro commit; rebuild when invalid
2. Pre-commit rejects `mover_focus` **NoteRange** when `startTick == 0` or `startTick >= loopLength`
3. Defer macro commit during geometry editing-activity hold (`isNoteEditMacroCommitDeferred`, 750 ms)
4. Native tests: `test_pre_commit_rejects_mover_note_range_zero_start_after_nonzero_baseline`, `test_pre_commit_emits_valid_mover_note_range`

**HITL gate (open):** after geometry move, select different note — no `DNTE,65,0,0,…`; no `NoteRange start=0`.

### RC7b ✅ shipped — select bracket mismatch

**Primary capture:** `captures/session_20260806_220331.log` (~55.7s).

Re-select same `NoteId` at bracket ≠ `focus.last` display bracket → skip macro commit (`isMacroCommitAlignedWithSelectTarget`). See [`note_edit_select_commit_bracket_bugfix.md`](note_edit_select_commit_bracket_bugfix.md).

**HITL validation (`222937`):** failure mode absent (`M65@609 missing in recon` not seen; no `mover_focus start=993` after select at 609). Exact gate **not exercised** — after move 609→993, select-at-609 was blocked by `geometry_driver_ignored` (~46.8s), not `bracket mismatch`. Dedicated retest: move to 993, wait >750 ms geometry hold, re-select same `NoteId` at 609 → expect `NOTE_EDIT macro commit skipped: select bracket mismatch`.

---

## RC9 — Overlap restore span (open)

**Primary capture:** `captures/session_20260806_222937.log` — shorten→hide→move-away restores full baseline 609–959.

See [`note_edit_overlap_restore_span_bugfix.md`](note_edit_overlap_restore_span_bugfix.md).

---

## RC10 — Selection index stability (shipped)

**Primary capture:** `captures/session_20260806_223833.log` — index churn during overlap geometry; re-select shorten.

See [`note_edit_selection_index_stability_bugfix.md`](note_edit_selection_index_stability_bugfix.md).

---

## RC8 — Session store reconstruction miss (open)

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
