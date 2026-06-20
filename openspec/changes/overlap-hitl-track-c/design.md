## Context

**M8 edit** shipped pass storage and undo. HITL **M8 pass verify** passes on `20260620_161431`. Three **Track C** sub-verifiers fail on the same capture — see [BUG.md](./BUG.md).

Prior archived work:

| Change | Relevance |
|--------|-----------|
| [`change-length-commit-rematerialize`](../archive/2026-06-20-change-length-commit-rematerialize/) | Track A rematerialize + Track B home — green on `203729`; regressed or verifier drift on `161431` |
| [`note-move-pitch-overlap-flaky`](../archive/2026-06-20-note-move-pitch-overlap-flaky/) | AC4–AC7 mapping; AC7 insert/reorder still open |
| [`note-edit-modification-session`](../archive/2026-06-20-note-edit-modification-session/) | Overlap engine + focus — shipped |

### Investigation summary (`161431`)

| Track | Verifier | Leading root cause | Firmware vs verifier |
|-------|----------|-------------------|----------------------|
| **1** Insert/reorder | `_verify_delay_move_insert_reorder` | **1A** log alias; **1B** pass redo 6-note recon | Mixed |
| **2** M0 home + native parity | `_verify_long_over_short_pitch_restore`, `_verify_change_length_store_rebuild` | Home snapshot = pre-move recon @ tick 112 | **Primarily verifier** (H1) |
| **3** Split-overlap mover | `_verify_split_overlap_note_round_trip` | Same H1 home snapshot | **Primarily verifier** (H1) |

---

## Goals / Non-Goals

**Goals:**

- Green `_verify_delay_move_insert_reorder` on canonical edit baseline.
- Green `_verify_long_over_short_pitch_restore` (`m0_home_ok`) and home native parity in `_verify_change_length_store_rebuild`.
- Green `_verify_split_overlap_note_round_trip` (`mover_at_home`).
- Re-run HITL on `teensy41-capture-serial` with serial capture; document pass capture id.

**Non-Goals:**

- Change overlap **EditChange** model or **NoteEditFocus** architecture.
- Fix `short_over_long_forward` / `restore` (AC5/AC6) unless same patch closes them.
- Block **`m8-edit`** archive on this work (optional §4.7 only).

---

## Decisions

### 1. Fix order (locked for `/opsx:apply`)

1. **§1 Verifier quick wins** — restore log alias (Track 1A); home snapshot after post-move signal (Track 2/3 H1). Re-run HITL on `161431` log replay + live device.
2. **§2 Firmware pass redo** — if `insert_missing_after_in_edit_redo` still fails: fix `NoteEditPassClosed` redo rematerialize note inventory (Track 1B / H3).
3. **§3 Native + HITL sign-off** — extend `test_edit_apply` for redo parity; full edit baseline green.

### 2. Home snapshot selection (proposed)

Prefer **last inventory signal at fixture home** after a `position_edit:*->m0_tick` line:

1. First `Reconstruction complete` after `Moved note events: … start->m0_tick`, **or**
2. First `#CAP,…,DNTE,…,m0_tick,…` after that POSITION EDIT, **or**
3. Fallback: current `_snapshot_position_edit_to_home` (document why fallback used).

Skip scratch recon passes where `Final note` for the mover still shows **pre-move start tick** (e.g. 112 when target is 16).

### 3. Insert restore detection (proposed)

Accept any of these in the move-back window after `position_edit:*insert*->*`:

- `Restoring deleted note`
- `Will restore note`
- `Restoring hidden overlap note` (current firmware path on `161431`)

Optional: assert via reconstruction inventory (pitch 60 @ insert tick) instead of log string only.

### 4. In-edit undo/redo semantics (**locked 2026-06-20**)

**User decision:** in-edit double/triple record SHALL use **NoteEditSessionUndoStack** only — not global **NoteEditPassClosed** undo while still in note edit mode.

Implications:

- Firmware: route in-edit record multi-press to session stack (may already partially exist via `handleUndo` — audit and close gap).
- HITL script: verifier SHALL look for `NoteEditSession undo` / `NoteEditSession redo` (or equivalent serial markers), not `Note edit pass undone` + note-count delta on global pass redo.
- Track 1B shifts from pass-redo rematerialize fix to **session undo/redo correctness** for insert/reorder scenario.

---

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Verifier-only fix masks firmware bug | Re-run live HITL after §1; keep inventory-based AC in spec |
| Home snapshot heuristic brittle | Add native replay test parsing `161431` log; pin checkpoint line numbers in BUG.md |
| Pass redo fix touches global undo | Native test before HITL; `pio test -e native` gate |

---

## Locked decisions (2026-06-20)

| Question | Decision |
|----------|----------|
| Home native parity lifecycle | **Snapshot immediately after overlap round-trip home** (before short-over-long B); keep lengthen-end AC at that checkpoint |
| In-edit undo target | **NoteEditSessionUndoStack** — not global pass undo during edit |
| PR strategy | **Verifier first** (§1), then firmware (§2) if still failing |

---

## Verification

```bash
# Log replay (no device)
.venv/bin/python scripts/host_midi_automation_edit_baseline.py \
  --verify-serial-log captures/host_midi_automation_edit_baseline_20260620_161431_serial.log \
  ... fixture args ...

# Live HITL (canonical — see HITL-Edit-Test-Flow.mdc)
pio run -e teensy41-capture-serial -t upload   # user confirms
.venv/bin/python scripts/host_midi_automation_edit_baseline.py ...
```

Pass = `serial_verification.edit.ok` true and all three sub-verifier groups `ok: true`.
