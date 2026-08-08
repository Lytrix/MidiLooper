# Note edit overlap restore span — RC9 (shipped) + RC9g (active)

**Status:** RC9g leave-restore + projection window-order shipped. RC9d/e/f WIP **reverted** (`session_20260807_000657` regression). Commit-authority patch landed separately.

**Primary captures:**

- `captures/session_20260806_222937.log` (~30.5s) — move-away full `RestoreNote` 609–959
- `captures/session_20260806_225121.log` (~15s, ~38s) — `HideNote` full baseline on head overlap
- `captures/session_20260807_000657.log` — spurious cross-pitch `RestoreNote` during RC9d/e/f WIP (reverted)

**Debugging boundary:** same as [`note_edit_projection_ownership_bugfix.md`](note_edit_projection_ownership_bugfix.md) — trust `EditSessionAction` rows; fix action **selection** in geometry pipeline, not projection paint.

---

## Implementation status

| Track | Status | Notes |
|-------|--------|-------|
| RC9b — scratch restore on move-away | **Shipped** | `31379a9` |
| RC9c — head overlap trim | **Reverted** | user request — `OverlapNoteOn` → hide per OpenSpec; no overlap `MoveNote` head trim |
| RC9 commit authority | **Shipped** | preserve empty-step overlap, reconcile on rebuild, conditional forget |
| RC9d — restore gating | **Reverted** | bundled with RC9e/f; caused `000657` cross-pitch restore spam |
| RC9e — wrong-side shorten + leave restore | **Reverted** | same WIP |
| RC9f — vacated-lane baseline recalc | **Reverted** | same WIP |
| RC9g — same-pitch leave-restore | **Shipped** | `determineConstrainedGeometryTargetNoteIds` same-pitch gate; projection full-loop base |
| RC9h — leave-restore storage baseline | **Shipped** | `session_20260807_010415` — storage `baselineMap` authority on leave; not projected/shortened live |
| RC9i — pitch-change lane gate | **Shipped** | `session_20260807_011115` — leave-restore uses edited causing pitch, not stale `focus.last` |
| RC9j — storage overlap analyze | **Shipped** | `session_20260807_011618` — classify/constrain on `baselineMap` ticks, not projected k-shift |

---

## RC9g — Leave-restore (open, findings-first)

**Invariant (one commit):** When causing leaves overlap (`interactions=0`, mover span does not intersect live overlap), emit **baseline-equivalent** restore for **same-pitch lane only** — not cross-pitch sticky `changedOverlapNoteIds`, not per-tick `reconcileChangedOverlapNoteIdsFromLiveStore` in `NoteGeometryResolver::resolve`.

**Evidence (`000657`):** Lane-94 move with `interactions=0`, `changed=5+` → `RestoreNote` on pitch 84/88 (`type=0 noteId=7 pitch=84` at ~139.8s). Root cause: RC9d/e `determineConstrainedGeometryTargetNoteIds` motion loop + per-tick reconcile.

**Native pin (proposed):** Mover on pitch 94, `interactions=0`, sticky changed overlap on pitch 84 → **no** restore target for pitch 84.

**Owners:** `determineConstrainedGeometryTargetNoteIds`, `constrainedGeometryFromRestoreCandidate` — extend RC9 scratch path incrementally; OpenSpec baseline vs scratch decision documented before code.

**Do not merge with:** commit-authority patch (`reconcile` on rebuild only, empty-step preserve).

---

## RC9b — Move-away scratch restore (shipped)

### Symptom

After shorten → hide → move away, `RestoreNote` used full `transactionBaseline` (609–959) instead of last shortened span (609–659).

### Fix

| Change | Owner |
|--------|--------|
| `constrainedGeometryFromRestoreCandidate` — live tail-shorten extends to baseline; live head-trim / scratch keeps last span | `ResolveConstrainedGeometry.cpp` |
| `recordOverlapGeometryScratch` on `ShortenNote` / `HideNote` / overlap `MoveNote` | `ApplyEditSessionActions.cpp` |
| Restore-only resolve uses scratch baseline when live absent | `resolveAllConstrainedGeometry` |

---

## RC9c — Head overlap trim (reverted)

**OverlapNoteOn** and **CompleteCover** → **`visible = false`** (`HideNote`). **OverlapNoteOff** → tail shorten only (`ShortenNote` / min-length hide). No overlap-target **`MoveNote`** head trim.

---

## Tests

| Test | Suite |
|------|--------|
| `test_resolve_overlap_note_on_hides_when_target_extends_past_causing_end` | `test_resolve_constrained_geometry` |
| `test_resolve_overlap_note_off_inside_target_tail_shortens_or_min_length_hides` | `test_resolve_constrained_geometry` |
| `test_resolve_restore_candidate_uses_overlap_scratch_not_full_baseline` | `test_resolve_constrained_geometry` |
| `test_builder_emits_hide_for_overlap_note_on_constrained_geometry` | `test_edit_session_action_builder` |
| `test_empty_step_select_preserves_changed_overlap_note_ids` | `test_note_edit_focus` |
| `test_forget_changed_overlap_only_on_full_baseline_restore` | `test_apply_edit_session_actions` |

**HITL:** `225121` / `222937` shorten→hide→move-away; `DISP` must not jump with full-length restore; RTL move past long note must not `HideNote` full baseline on first contact. `000657` replay: no cross-pitch `RestoreNote` when `interactions=0`.

---

## Related

- [`note_edit_overlap_projection_followup.md`](note_edit_overlap_projection_followup.md)
- [`note_edit_selection_index_stability_bugfix.md`](note_edit_selection_index_stability_bugfix.md) — RC10 (shipped)
