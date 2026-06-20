# Proposal — ChangeLength commit rematerialize (HITL bug closure)

**Change:** `change-length-commit-rematerialize`  
**Kind:** bug  
**Status:** Proposed (2026-06-19)  
**Parent:** [note-edit-modification-session](../note-edit-modification-session/) (overlap engine **shipped**; native **105/105**)

## Why

After [note-edit-modification-session](../note-edit-modification-session/) landed the unified overlap owner, native replay tests pass but **HITL edit baseline** still fails on a coherent chain: **ChangeLength commits** log correct parameters while **reconstruction snapshots** show M0 still at record gate length, then **native parity verifiers** fail through pitch and reselect, and **M0 is not at home** after the overlap round-trip.

Serial evidence: [`captures/host_midi_automation_edit_baseline_20260619_195830.json`](../../../captures/host_midi_automation_edit_baseline_20260619_195830.json) (see [BUG.md](./BUG.md)).

This change is the **focused bug plan** for Track **A** (rematerialize) and **B** (M0 home downstream), without reopening overlap-engine architecture.

## What Changes

- **Ranked hypotheses** and fix order for ChangeLength → `saveEdit` → `applyEdits` → **NoteEditSession.store** on device.
- **Normative rematerialize AC** at HITL checkpoints (post length commit, post pitch, post reselect B).
- **Sign-off matrix** mapping child bug specs → verifiers → this change's tasks.
- **Explicit deferrals** for insert/reorder (Track C) and P0 `1535` peak (Track D) — remain parent tasks **0.1**, **7.4**.

## Capabilities

### New Capabilities

- `change-length-commit-rematerialize`: **ChangeLength** commit and **Takes + Edits** replay MUST match live **NoteEditSession.store** on Teensy; M0 home after overlap round-trip when Track A is fixed.

### Modified Capabilities

- *(none in `openspec/specs/` yet)* — parent delta [note-edit-modification-session/spec.md](../note-edit-modification-session/specs/note-edit-modification-session/spec.md) B1/rematerialize is **implementation-complete**; this change closes the **device gap**.

## Impact

- **Firmware (likely):** `EditManager.cpp` (`commitEditAction`, `commitAllPendingNoteEditActions`), `EditApply.cpp` (**ChangeLength** replay), `Loop.cpp` (`rematerializeEditView`, `applyEditsToFlat`), `NoteEditManager.cpp` (length-mode commit path).
- **Tests:** extend `test_edit_apply` if host reproduces device gap; HITL `_verify_change_length_store_rebuild`, `_verify_long_over_short_pitch_restore`, `_verify_split_overlap_note_round_trip`.
- **Evidence:** [lengthen-overlap-neighbor-restore/BUG.md](../lengthen-overlap-neighbor-restore/BUG.md), [note-move-pitch-overlap-flaky/BUG.md](../note-move-pitch-overlap-flaky/BUG.md) AC4–AC6 partial.

## Non-Goals

- D-delay **insert/reorder** (`_verify_delay_move_insert_reorder`) — parent **7.4** unless same root cause proven.
- Short B over long M0 forward/restore (AC5/AC6) — separate overlap segment; not gated on Track A sign-off.
- `EditStartNoteState` encoder DRY — parent **8.1**.
- Display / length-mode — [edit-record-display-length-mode](../edit-record-display-length-mode/).

## Open Decisions (TBD)

- **TBD-1:** Whether Track B (M0 home) closes automatically once Track A passes full HITL — default **yes**, re-evaluate after first green `change_length_store` run.
