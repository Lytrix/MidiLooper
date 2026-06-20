# Proposal — Overlap HITL Track C (insert / M0 home / split-overlap)

**Change:** `overlap-hitl-track-c`  
**Kind:** bug  
**Status:** Proposed (2026-06-20)  
**Parent:** [`m8-edit`](../m8-edit/) §4.7 — M8 pass verify **OK**; full baseline **fail** on Track C only

## Why

[`m8-edit`](../m8-edit/tasks.md) storage, pass undo, and M8 pass verify are shipped and green on capture `20260620_161431`. The same run fails three **HITL sub-verifiers** that exercise overlap round-trip, insert/move-over-insert, and split-overlap mover home — historically **Track C**, deferred from archived [`change-length-commit-rematerialize`](../archive/2026-06-20-change-length-commit-rematerialize/) and [`note-move-pitch-overlap-flaky`](../archive/2026-06-20-note-move-pitch-overlap-flaky/).

Serial investigation on `161431` shows **two verifier gaps** (restore log alias, home snapshot timing) and **one firmware gap** (incomplete note-edit pass redo rematerialize). See [BUG.md](./BUG.md).

This change is the **focused bug plan** to close `_verify_delay_move_insert_reorder`, `_verify_long_over_short_pitch_restore` / `_verify_change_length_store_rebuild` (home checkpoints), and `_verify_split_overlap_note_round_trip` on device — without reopening M8 pass storage or overlap-engine architecture.

## What Changes

- Ranked hypotheses and fix order per [BUG.md](./BUG.md) §Ranked hypotheses.
- Normative HITL AC for each of the three verifier groups.
- Native replay tests where host can reproduce pass-redo gap (Track 1B).
- Verifier checkpoint fixes where firmware behavior is already correct (Track 1A, Track 2/3 H1).

## Capabilities

### New Capabilities

- **`overlap-hitl-track-c`**: Insert restore on move-back, M0 at home after overlap round-trip (pitch 67 @ fixture step 0), split-overlap mover home — all passing on canonical edit baseline capture.

### Modified Capabilities

- *(none in `openspec/specs/` until archive)* — evidence links to archived child specs AC4–AC7.

## Impact

- **Firmware (likely):** `TrackUndo.cpp` / pass redo rematerialize, `EditApply.cpp`, `NoteMovementUtils.cpp` (if restore path gaps found beyond verifier).
- **HITL script:** `scripts/host_midi_automation_edit_baseline.py` — home snapshot selection, restore log aliases.
- **Tests:** extend `test_edit_apply` for pass-redo note count parity; optional verifier unit tests on `161431` log replay.
- **Evidence:** [BUG.md](./BUG.md), capture `20260620_161431`.

## Non-Goals

- M8 pass verify (`NoteEditPassClosed`, session undo markers) — already green.
- Short-over-long B forward/restore flags (`short_over_long_forward` / `restore`) — separate AC5/AC6 family.
- Display + length-mode UX — parked change.
- Full `m8-edit` archive — remains gated on §4.1–§4.4; this change unblocks optional §4.7 only.

## Locked decisions (2026-06-20)

| Topic | Decision |
|-------|----------|
| Overlap-home checkpoint | Snapshot at overlap round-trip home; keep lengthen-end native AC |
| In-edit undo | **NoteEditSessionUndoStack** (not global pass undo in edit) |
| PR order | Verifier §1 first, firmware §2 second |
