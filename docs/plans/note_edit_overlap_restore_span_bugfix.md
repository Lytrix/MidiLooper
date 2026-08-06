# Note edit overlap restore span — RC9 (shipped)

**Status:** Shipped — constrained restore span + head-overlap trim (RC9b + RC9c).

**Primary captures:**

- `captures/session_20260806_222937.log` (~30.5s) — move-away full `RestoreNote` 609–959
- `captures/session_20260806_225121.log` (~15s, ~38s) — `HideNote` full baseline on head overlap

**Debugging boundary:** same as [`note_edit_projection_ownership_bugfix.md`](note_edit_projection_ownership_bugfix.md) — trust `EditSessionAction` rows; fix action **selection** in geometry pipeline, not projection paint.

---

## RC9b — Move-away full restore (shipped)

### Symptom

After shorten → hide → move away, `RestoreNote` used full `transactionBaseline` (609–959) instead of last shortened span (609–659).

### Fix

| Change | Owner |
|--------|--------|
| `constrainedGeometryFromRestoreCandidate` — live tail-shorten extends to baseline; live head-trim / scratch keeps last span | `ResolveConstrainedGeometry.cpp` |
| `recordOverlapGeometryScratch` on `ShortenNote` / `HideNote` / overlap `MoveNote` | `ApplyEditSessionActions.cpp` |
| Restore-only resolve uses scratch baseline when live absent | `resolveAllConstrainedGeometry` |

---

## RC9c — Head overlap hide (shipped)

### Symptom (`225121`)

Moving into / through a longer same-pitch note from the right emitted `HideNote` with full baseline (`DISP` 5→4). Root causes:

1. **`OverlapNoteOn`** triggered complete hide even when target extended past causing end (partial head overlap).
2. **`OverlapNoteOff`** with causing inside target tail-shortened to a sub-min stub → min-length hide instead of head-trim.

### Fix

| Change | Owner |
|--------|--------|
| `OverlapNoteOn` → head-trim (`start = causingEnd + 1`) when `targetEnd > causingEnd` | `resolveConstrainedGeometry` |
| `OverlapNoteOff` + causing inside target → head-trim, not tail shorten | `resolveConstrainedGeometry` |
| `appendOverlapTargetActions` emits `MoveNote` when `constrained.startTick > baseline.startTick` | `EditSessionActionBuilder.cpp` |

### Invariant

Partial head overlap keeps the **tail** visible (`causingEnd + 1` … `baseline.endTick`). Full hide remains for `CompleteCover` and inverted/under-min spans only.

---

## Tests

| Test | Suite |
|------|--------|
| `test_resolve_overlap_note_on_head_trim_keeps_tail_visible_225121` | `test_resolve_constrained_geometry` |
| `test_resolve_overlap_note_off_inside_target_head_trims_225121` | `test_resolve_constrained_geometry` |
| `test_resolve_restore_candidate_uses_overlap_scratch_not_full_baseline` | `test_resolve_constrained_geometry` |
| `test_builder_emits_move_note_for_overlap_head_trim_225121` | `test_edit_session_action_builder` |

**HITL:** `225121` / `222937` shorten→hide→move-away; `DISP` must not jump with full-length restore; RTL move past long note must not `HideNote` full baseline on first contact.

---

## Related

- [`note_edit_overlap_projection_followup.md`](note_edit_overlap_projection_followup.md)
- [`note_edit_selection_index_stability_bugfix.md`](note_edit_selection_index_stability_bugfix.md) — RC10 (shipped)
