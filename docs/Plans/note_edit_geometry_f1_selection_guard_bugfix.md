# Bugfix — Geometry F1 selection guard during motor sync

**Date:** 2026-07-02  
**Branch:** `load-save-sets-loops`  
**OpenSpec:** [`note-edit-fader-feedback-regression`](../../openspec/changes/note-edit-fader-feedback-regression/) §7.24  
**Evidence:** `captures/session_20260702_183747.log`  
**Build env:** `teensy41-capture-serial`

---

## Status

| Item | Status |
|------|--------|
| Wire `selectFaderFeedbackIgnoreUntilMs_` on inbound F1 | **Shipped** |
| `syncGeometrySelectionToUi` (no full `syncNoteEditSessionStateToUi`) | **Shipped** |
| Kind-scoped F1 block (`geometry_edit_active`) | **Reverted** 2026-07-02 — blocked deliberate user F1 select after F2/F3/F4 moves (`session_20260702_221633`) |
| Echo-window guard only (D39) | **Shipped** — no `select_apply` within 1500 ms of `geometry_motor_sync` |
| Native tests (54 in `test_note_edit_fader_feedback`) | **PASS** |
| Host serial verifier (`test_note_edit_geometry_fader1_serial_verify.py`) | **PASS** (6 tests) |
| Manual HITL — geometry move + F1 motor follow | **PASS** (user 2026-07-02) |
| Post-§7.24.8 smoke (`3348857`) | **PASS** — `captures/session_20260702_222845.log`; echo guard OK; 143 user F1 applies; 0 `geometry_edit_active` |

---

## Problem

After split geometry→F1 motor sync (`geometry_motor_sync sent=1`), F1 motor echo arrived ~11 ms later with pitch off by > `SELECT_MOVEMENT_THRESHOLD` (capture: pb 3003 vs echo 3201). Firmware treated echo as user select navigation:

- `select_apply apply=1` → `applySelectNav` → `kind=Select`
- `Exited EditStartNoteState` — moving note edit torn down
- Wrong note selected; F2/F3 could snap

Root causes:

1. `selectFaderFeedbackIgnoreUntilMs_` armed at `sendFader1MotorTimedBurst` but not checked on inbound F1.
2. `handleSelectFaderInput` ran during Move/Length/Pitch geometry kinds.
3. `applySelectionFromGeometryEdit` called full `syncNoteEditSessionStateToUi`, recomputing `selectedNoteIdx`.

---

## Fix

| # | Change | File |
|---|--------|------|
| 1 | `shouldIgnoreFaderInput` FADER_SELECT: honor `selectFaderFeedbackIgnoreUntilMs_` (1500 ms) | `NoteEditManager.cpp` |
| 2 | `EditManager::syncGeometrySelectionToUi` — bracket + `requestNoteInfoRefresh` only | `EditManager.cpp`, `EditManager.h` |
| 3 | `applySelectionFromGeometryEdit` → `syncGeometrySelectionToUi` | `EditManager.cpp` |
| ~~4~~ | ~~Early-return `handleSelectFaderInput` when `isGeometryEditKind`~~ | **Removed** `3348857` — user F1 select must work during geometry edit |

**Behavior:** Geometry F1 **motor** sync is outbound-only; motor echo is blocked by the ignore window. User F1 note select during geometry edit commits geometry and applies navigation. See [`DROID_MOTORFADER_PITCHBEND.md`](../Guides/DROID_MOTORFADER_PITCHBEND.md).

---

## Verification

```bash
pio test -e native -f test_note_edit_fader_feedback
.venv/bin/python scripts/test_note_edit_geometry_fader1_serial_verify.py
pio run -e teensy41-capture-serial -t upload   # user confirms
```

**Manual:** Enter NOTE_EDIT → Move → drive F2/F3 → stop. F1 follows after ~300 ms idle; moving note stays selected; kind stays Move; no F2/F3 snap-back.

**Capture gates** (within 1500 ms of each `geometry_motor_sync sent=1`):

- No `#DBG select_apply ... apply=1`
- No `Exited EditStartNoteState`

```bash
.venv/bin/python scripts/test_note_edit_geometry_fader1_serial_verify.py
# or apply verify_geometry_fader1_no_select_apply_after_flush() to capture log
```

---

## Relation to §7.13 dwell-gap fix

§7.13.1 **removed** a coarse geometry **driver-time** block on F1 select. §7.24 initially re-introduced a **kind-scoped** inbound block; **§7.24.8** (2026-07-02, `3348857`) removed it — echo protection is `selectFaderFeedbackIgnoreUntilMs_` only.

---

## §7.24.8 refinement (2026-07-02)

**Evidence:** `captures/session_20260702_221633` — blanket `geometry_edit_active` block prevented user F1 select after F2/F3/F4 geometry moves.

**Change:** Remove kind-scoped early return from `handleSelectFaderInput`. Keep D39 (ignore window) + D40 (`syncGeometrySelectionToUi`).

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | No |
| State transition change? | User F1 during geometry edit intentionally commits geometry and transitions to Select kind via `applySelectNav` |
