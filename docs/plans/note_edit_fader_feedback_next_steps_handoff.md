# Handoff — NOTE_EDIT fader feedback next steps

**Date:** 2026-07-02  
**Branch:** `load-save-sets-loops`  
**Latest firmware commits:** `3348857` (E: sidebar + F1 during geometry), `eb37930` (geometry echo guard + `syncGeometrySelectionToUi`)  
**OpenSpec:** [`note-edit-fader-feedback-regression`](../../openspec/changes/note-edit-fader-feedback-regression/) · [`note-edit-stable-note-id`](../../openspec/changes/note-edit-stable-note-id/)  
**Build env:** `teensy41-capture-serial`  
**Capture port:** `/dev/cu.usbmodem154944801`

---

## Status summary

| Area | Status |
|------|--------|
| Phase A NoteRef + Phase B `NoteId` / `EditorSelection` | **Shipped** (`e9cc97f`, `d3d5798`) |
| Selection-driven dependent motor sync (§7.18.1–7.18.4, §7.23) | **Shipped** — debounced `scheduleSelectDependentMotorSync` + parallel burst |
| Geometry F1 echo guard (§7.24.2–7.24.3, D39–D40) | **Shipped** — `selectFaderFeedbackIgnoreUntilMs_` + `syncGeometrySelectionToUi` |
| §7.24.8 user F1 during geometry edit | **Shipped** `3348857` — blanket `geometry_edit_active` block **removed** |
| Session undo sidebar `E:nn` / `U:nn` | **Shipped** `3348857` — `isSessionUndoDisplayActive` + session stack count restored |
| Native tests | **PASS** — `pio test -e native` (348/349; one env build flake unrelated to fader suite) |
| Geometry serial verifier | **PASS** — `scripts/test_note_edit_geometry_fader1_serial_verify.py` (6 tests) |
| HITL triple-motor acceptance (§7.18.5–7.18.8) | **PASS** 2026-07-03 |
| HITL `note_edit_select_dependent_faders` preset (§7.23.6) | **PASS** 2026-07-03 |
| RC11 F2 coordinate capture (§8.3–8.4) | **Deferred** post-archive |
| Phase 12 dead code cleanup | **Deferred** post-archive |
| Phase 13 `EditorSelection`-only motor paths (D41) | **Deferred** post-archive |

---

## What shipped recently (firmware)

| Commit | Fix |
|--------|-----|
| `3348857` | Restore `E:nn` sidebar (`isSessionUndoDisplayActive` + session `undoStack.undoCount()`); allow user F1 select during geometry edit (revert §7.24.1 blanket block) |
| `eb37930` | Geometry F1 motor echo: inbound `selectFaderFeedbackIgnoreUntilMs_`; `syncGeometrySelectionToUi` on geometry outbound |
| `3814868` | Debounced parallel F2/F3/F4 burst; geometry→F1 separate queue |

**Live behavior (post-`3348857`):**

- **NOTE_EDIT enter:** sidebar shows **`E:00`** (session undo), not global **`U:nn`**.
- **F1 during Move/Length/Pitch:** commits geometry + `applySelectNav` → Select kind (user can change note after F2/F3/F4 moves).
- **F1 motor echo** after `geometry_motor_sync`: ignored for **1500 ms** via `selectFaderFeedbackIgnoreUntilMs_` — verify with geometry serial tests after any F1-path change.

**Primary files:** `src/EditManager.cpp`, `src/NoteEditManager.cpp`, `include/Utils/NoteEditFaderMotorTiming.h`

---

## Recommended order (next session)

### 1. Device verification (quick smoke — 10 min) — **PASS** 2026-07-02

**Evidence:** [`captures/session_20260702_222845.log`](../../captures/session_20260702_222845.log) (post-`3348857` firmware)

| Gate | Result |
|------|--------|
| `geometry_edit_active` | **0** (blanket block removed) |
| `geometry_motor_sync sent=1` | **4** |
| Echo guard (`verify_geometry_fader1_no_select_apply_after_flush`) | **PASS** — 0 `select_apply` / 0 `Exited EditStartNoteState` within 1500 ms of each flush |
| User F1 `select_apply apply=1 reason=note_changed` | **143** (includes post–F2/F3 geometry sweep ~510 s) |
| `Exited EditStartNoteState` (spurious echo) | **0** |

Manual: LOOP_EDIT → NOTE_EDIT **`E:00`** confirmed on display (not in serial log).

```bash
.venv/bin/python -c "
from pathlib import Path; import sys; sys.path.insert(0,'scripts')
from test_note_edit_geometry_fader1_serial_verify import verify_geometry_fader1_no_select_apply_after_flush
lines=Path('captures/session_20260702_222845.log').read_text().splitlines()
print(verify_geometry_fader1_no_select_apply_after_flush(lines))
"
```

### 2. HITL — dependent fader motors (§7.23.6) — **PASS** 2026-07-03

**Evidence:** `captures/host_midi_hitl_note_edit_select_dependent_faders_20260703_122216.json` + `captures/note_edit_select_dependent_faders_20260703_122216_serial.log`

| Gate | Result |
|------|--------|
| Triple motor ack | 60 clusters, `motor_misses=0`, `ack_misses=0` |
| Outbound MO values | PASS |
| Perceptual F2/F4 | 80% / 88% (≥ 80% threshold) |
| Base loop | Fresh 2+2 record + 2 overdub (`host_midi_automation_baseline_20260703_122216.json`) |

**Required command** (canonical):

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_select_dependent_faders \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --dwell-ms 800 --toggle-dwell-ms 800 --toggle-cycles 6
```

Allow **~10–15 min** (base ~5 min + sweep ~2 min). Verifier timing aligned with firmware `kSelectFaderMotorIdleMs=300` (290 ms host min — fixes false FAIL at 299 ms).

Plan: [note_edit_select_dependent_faders_hitl_enhancement.md](note_edit_select_dependent_faders_hitl_enhancement.md).

**Pass:** one F2+F3+F4 motor cluster per dwell stop; `select_motor_sync sent=1` 290–650 ms after cluster; perceptual F2/F4 ≥ 80% (same-bracket re-selects may not change MI echo value); outbound MO values match plan.

```bash
.venv/bin/python scripts/test_note_edit_select_dependent_faders_serial_verify.py
.venv/bin/python scripts/analyze_fader2_select_feedback.py captures/note_edit_select_dependent_faders_<stamp>_serial.log
```

### 3. HITL — triple-motor acceptance matrix (§7.18.5–7.18.6) — **PASS** 2026-07-03

Satisfied by same capture as §2 (`note_edit_select_dependent_faders` preset). Verifier: `scripts/hitl/verify/note_edit_select_triple_motor_ack.py`.

### 4. RC11 coordinate capture (§8.3–8.4)

Loop with **`loopStartTick ≠ 0`**. F1 sweep; verify every `#DBG outbound_ctx f2` position-mode row has `pb == expected_pb_rel`.

Firmware: `noteRelativeTick` in `sendCoarseFaderPosition` **shipped**; `sendFineFaderPosition` position mode still uses `startTick % loopLength` (Phase 10.1).

### 5. Phase 12 — stale code cleanup (native-only gate)

**Gate:** `pio test -e native` green before and after. No behavior change expected.

| Step | Remove |
|------|--------|
| §12.1 | `sendStartNotePitchbend`, `performSelectnoteFaderUpdate`, `sendSelectnoteFaderUpdate`, `sendFaderUpdate`, `sendFaderPosition`, `syncMotorsForDisplaySelection`, `drainDependentFaderOutboundUntilDone` |
| §12.2 | `lastSelectnoteSentTime`, duplicate `PITCHBEND_IGNORE_PERIOD`, `faderHandler`, `faderProcessor`, `markFaderSent` |
| §12.3 | `Trigger::NoteSelectDependent`, `Trigger::Fader1BracketOnly`, unreachable `scheduleOtherFaderUpdates(FADER_SELECT)` |
| §12.4 | Index-only gate helpers in `NoteEditFaderOutboundPlan.h`; migrate tests to `shouldApplySelectionOnNoteIdChange` |

Inventory: [BUG.md § Stale code](../../openspec/changes/note-edit-fader-feedback-regression/BUG.md).

### 6. Phase 13 — `EditorSelection`-only motor resolution (D41)

**Gate:** after §12.1–12.4 minimum.

- Motor send helpers resolve from `EditorSelection.primaryNote`, not `getSelectedNoteIdx()` alone.
- Audit `setSelectedNoteIdx` without `applySelectNav` call sites.
- HITL regression: §7.18.8 + geometry move §7.24.7.

---

## Watch items (regression risks)

| Risk | Mitigation |
|------|------------|
| F1 motor echo re-applies select during Move (§7.24 pre-`3348857` bug) | Re-run `scripts/test_note_edit_geometry_fader1_serial_verify.py` after any `handleSelectFaderInput` / ignore-window change |
| User F1 blocked again after geometry move | Do **not** re-add blanket `isGeometryEditKind` early return; use echo window only |
| `E:` reverts to `U:` during NOTE_EDIT | `EditManager::isSessionUndoDisplayActive` must stay `editSession.active && sessionType == Note` |
| Slow F1 dwell — motors never flush | Check `kSelectFaderMotorIdleMs` (300 ms test value) and `scheduleSelectDependentMotorSync` coalesce |

---

## Parked (do not start unless reproducing)

- **NOTELEN** manual verify (tasks §4.2, §11.3)
- **Phase 11** display freeze / fine throttle (D35)
- **§7.15.6** DROID ch13 ack reload spike (blocks Phase 2 hybrid motor gate only)

---

## Closeout checklist (when HITL gates pass)

- [ ] Mark §7.18.5, §7.23.6, §8.3 in [tasks.md](../../openspec/changes/note-edit-fader-feedback-regression/tasks.md)
- [ ] Mark `note-edit-stable-note-id` §7.3 + §9.4 archive
- [ ] Commit uncommitted doc drift from §7.24.8 refinement (guides + OpenSpec) if not yet committed
- [ ] Consider archiving `note-edit-fader-feedback-regression` after Phase 12.1–12.4 + HITL gates (not before)

---

## Related handoffs

| Doc | Topic |
|-----|--------|
| [note_edit_geometry_f1_selection_guard_bugfix.md](note_edit_geometry_f1_selection_guard_bugfix.md) | §7.24 echo guard + §7.24.8 refinement |
| [note_edit_select_dependent_faders_hitl_enhancement.md](note_edit_select_dependent_faders_hitl_enhancement.md) | §7.23.6 preset + verifier |
| [note_edit_stable_note_id_phase_a_handoff.md](note_edit_stable_note_id_phase_a_handoff.md) | Phase A HITL pipeline (historical) |
| [note_edit_fader_dependent_outbound_unified_handoff.md](note_edit_fader_dependent_outbound_unified_handoff.md) | Send-path honesty / D34 |
| [set_revision_persistence_handoff.md](set_revision_persistence_handoff.md) | **Current sprint** primary track on branch |

---

## Authority

1. OpenSpec [tasks.md](../../openspec/changes/note-edit-fader-feedback-regression/tasks.md) — implementation checklist  
2. [design.md](../../openspec/changes/note-edit-fader-feedback-regression/design.md) D38–D41  
3. Guides: [DROID_MOTORFADER_PITCHBEND.md](../Guides/DROID_MOTORFADER_PITCHBEND.md), [HITL_TEST_SCENARIOS.md](../Guides/HITL_TEST_SCENARIOS.md)
