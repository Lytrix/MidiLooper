# BUG — ChangeLength commit vs rematerialize (HITL)

**Change:** `change-length-commit-rematerialize`  
**Parent:** `note-edit-modification-session`  
**Primary capture:** `20260619_195830` (+ serial log sibling)

---

## User-visible symptom

Lengthen M0 → move over P0 → pitch → move back: **live edit** uses lengthened span during the session, but after **ChangeLength commit** reconstruction still shows **M0 end at record gate** (~136 ticks not ~712). Later checkpoints lose M0 in native parity; **M0 not at home** at round-trip end.

---

## Serial proof (195830)

| Observation | Signal |
|-------------|--------|
| Length commit logged | `Edit committed ChangeLength start=40 baselineEnd=136 newEnd=712` (serial **6022**) |
| Store not updated | First post-commit M0 `Final note: pitch=60, start=40, end=136` (serial **6026**; verifier snapshot **6044**) |
| Live lengthen before commit | M0 merged `start=40, end=712` during length mode (serial **5889**, **5916**) |
| Native parity chain | `after_change_length_commit:native_m0_not_lengthened`, `after_overlap_pitch_change:native_m0_count:0!=1`, … |
| Overlap partial pass | `contained_delete`, `pitch_restore_log`, `inner_a_after_pitch`, `inner_p0_ok` |
| Round-trip home fail | `m0_home_ok` false, `split_overlap_note_mover_not_home` |
| Note length restore | `note_lengths.ok` **true** (overlap note shorten restore — separate from Track A) |

---

## Serial trace — capture `195830` (§0)

**File:** `captures/host_midi_automation_edit_baseline_20260619_195830_serial.log`

| Step | Serial line | Event |
|------|-------------|--------|
| Length mode on | 5783 | `Length editing mode ENABLED` |
| Live lengthen (session store) | 5889, 5916 | `Final note: pitch=60, start=40, end=712` |
| Length mode off | 6020 | `Length editing mode DISABLED` |
| ChangeLength commit | 6022 | `Edit committed ChangeLength start=40 baselineEnd=136 newEnd=712` |
| Replay recon #1 (verifier gate) | 6023–6045 | M0 @40–**136** (record gate); separate M0 @616–712 unchanged |
| Replay recon #2–3 | 6046–6091 | Same as #1 — `applyEdits` replay did not lengthen M0@40 |
| Session-scratch recon #4 | 6092–6114 | M0 @40–712 reappears; duplicate note-off @712 (lines 6101+6103) |
| Display recon #5+ | 6115+ | Loop length 1385; M0 @40–136 again |

**Commit path (code, confirmed §0.2):**

```text
NoteEditManager::toggleLengthEditingMode(false)
  → commitAllPendingNoteEditActions
      → resolveOverlapNotesForPreCommit
      → buildPreCommitEditChanges → ChangeLength { target=(5,60,40,136), newEnd=712 }
      → commitEditAction
          → loop.saveEdit
          → loop.rematerializeEditView(session store)   // applyEdits
          → applyEditsToFlat → session store loadFromFlat
          → track.invalidateCaches()
```

**Reconstruction emitter (§0.5):** `NoteUtils::reconstructNotes` logs `Reconstructing notes` / `Final note:` (`src/Utils/NoteUtils.cpp`). Verifier uses first `Reconstruction complete` after commit line (**6045**, JSON `post_commit_snapshot_line`).

**Native parity (§0.3):**

| | Device `195830` | Native `test_lengthen_commit_rematerialize_hitl_fixture` |
|--|-----------------|--------------------------------------------------------|
| Channel | 5 | 5 |
| **NoteRef** | `{5, 60, 40, 136}` | `{5, 60, 8, 104}` (2-bar fixture ticks differ) |
| `newEndTick` | 712 | 680 |
| Reload path | Full `commitEditAction` (double reload) | `rematerializeEditView` only |

Native passes with simpler fixture (single M0 lengthen, no second pitch-60 @616). Device take has **two** pitch-60 notes (M0@40, P0@616) — overlap replay may diverge.

---

## Failure tracks

| Track | Verifier / issue keys | Hypothesis priority |
|-------|----------------------|---------------------|
| **A** Rematerialize | `after_change_length_commit:*`, `after_overlap_pitch_change:*`, `after_reselect_b:*` | **P0** — fix first |
| **B** M0 home | `m0_not_home_after_round_trip`, `split_overlap_note_mover_not_home` | **P1** — re-HITL after A |
| **C** Insert/reorder | `insert_reorder.*`, `missing_move_over_inserted_note` | **Deferred** — parent 7.4 |
| **D** P0 peak 1535 | `shortened_still_short_after_restore:p60@592` (144458) | **Investigate** — parent 0.1; not failing 195830 |

---

## Ranked hypotheses (Track A)

| ID | Hypothesis | §0 status | Next step |
|----|------------|-----------|-----------|
| **H1** | `EditApply::applyChangeLength` replay wrong when take has **two** pitch-60 notes (M0@40 + P0@616) | **Partial fix** — native replay passed pre-fix; hardened `findNoteOffForRef` + note-off insert fallback |
| **H2** | `commitEditAction` double reload drops lengthen | **Fixed** — drop live flat before single `applyEditsToFlat` reload; removed redundant pre-load `rematerializeEditView` |
| **H3** | **NoteRef** stale — replay no-op | **Rejected** — commit log shows `baselineEnd=136 newEnd=712` |
| **H4** | Session cache / scratch overwrites replay | **Mitigated** — `discardFlatCache()` before replay; pre-commit uses `focus.last` not stale `movingNote` |
| **H5** | Length commit skips pre-commit path | **Rejected** — lines 6020–6022: mode off → commit in same timestamp |
| **H6** | Native fixture too simple | **Closed** — `test_change_length_rematerialize_hitl_195830_ticks` + commit reload path |

Native **106/106**; HITL re-run required to confirm device path (§2).

## Child spec AC mapping (203729)

| Source | Criterion | 203729 |
|--------|-----------|--------|
| lengthen-overlap-neighbor-restore | rematerialize parity, `m0_home_ok`, `inner_p0_ok` | **Pass** |
| lengthen-overlap-neighbor-restore | `inner_a_contained_at_home` | **Open** |
| note-move-pitch-overlap-flaky | AC4 M0 home | **Pass** |
| note-move-pitch-overlap-flaky | AC5/AC6 short B | **Open** (verifier flags) |
| note-move-pitch-overlap-flaky | AC7 insert/reorder | **Fail** (Track C) |

---

## Patch history

| Date | Action | Result |
|------|--------|--------|
| 2026-06-19 | OpenSpec change opened | Bug plan in OpenSpec (this folder) |
| 2026-06-19 | §0 investigation on `195830` | H1 leading; H3/H5 rejected; H6 native gap confirmed |
| 2026-06-19 | §1 fix (EditApply + EditManager) | `commitEditAction` single replay; pre-commit `focus.last`; `findNoteOffForRef`; native 106/106 |
| 2026-06-19 | Track A root cause (203418) | Rematerialize OK; verifier grabbed `applyChangeLength` pre-apply recon — fixed `verboseLog=false` in `EditApply.cpp` |
| 2026-06-19 | HITL `203729` Track A | `change_length_store` post-commit parity green |
| 2026-06-19 | Track B (203729) | Firmware overlap home OK (`m0_home_ok`, `mover_at_home`); verifier used wrong pitch/home snapshot timing — fixed in baseline script |
| 2026-06-19 | §4 parent/child closure | Parent 7.1 partial; lengthen-overlap rematerialize+M0 gates; note-move-pitch-overlap-flaky AC4 mapped |
