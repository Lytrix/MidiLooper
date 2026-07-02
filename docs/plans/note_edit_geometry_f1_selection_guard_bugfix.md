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
| Block F1 select apply during `isGeometryEditKind` | **Shipped** |
| Wire `selectFaderFeedbackIgnoreUntilMs_` on inbound F1 | **Shipped** |
| `syncGeometrySelectionToUi` (no full `syncNoteEditSessionStateToUi`) | **Shipped** |
| Native tests (54 in `test_note_edit_fader_feedback`) | **PASS** |
| Host serial verifier (`test_note_edit_geometry_fader1_serial_verify.py`) | **PASS** (6 tests) |
| Manual HITL — geometry move + F1 motor follow | **PASS** (user 2026-07-02) |

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
| 1 | Early-return `handleSelectFaderInput` when `isGeometryEditKind`; log `geometry_edit_active` | `NoteEditManager.cpp` |
| 2 | `shouldIgnoreFaderInput` FADER_SELECT: honor `selectFaderFeedbackIgnoreUntilMs_` (1500 ms) | `NoteEditManager.cpp` |
| 3 | `EditManager::syncGeometrySelectionToUi` — bracket + `requestNoteInfoRefresh` only | `EditManager.cpp`, `EditManager.h` |
| 4 | `applySelectionFromGeometryEdit` → `syncGeometrySelectionToUi` | `EditManager.cpp` |

**Behavior:** Geometry F1 motor is **outbound-only** during Move/Length/Pitch. User F1 note select resumes in **Select** kind only. See [`DROID_MOTORFADER_PITCHBEND.md`](../Guides/DROID_MOTORFADER_PITCHBEND.md).

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

§7.13.1 **removed** a coarse geometry **driver-time** block on F1 select (blocked deliberate F1 navigation during F2 drag). §7.24 re-introduces a **kind-scoped** guard: block select apply only while `NoteEditKind` is a geometry edit kind (Move/Length/Pitch/Add/Delete), not while user is in Select kind.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | No |
| State transition change? | Yes — intentional: geometry motor echo must not transition `kind` to Select |
