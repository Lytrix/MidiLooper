# Handoff — NoteRef selection Phase A (stable NoteId prerequisite)

**Date:** 2026-07-02  
**Branch:** `load-save-sets-loops`  
**Commit:** `d3d5798` — *Ship Phase A NoteRef-driven fader selection and HITL sweep pipeline*  
**OpenSpec:** [`openspec/changes/note-edit-stable-note-id/`](../../openspec/changes/note-edit-stable-note-id/) (Phase A **shipped**); [`note-edit-fader-feedback-regression`](../../openspec/changes/note-edit-fader-feedback-regression/) (selection refactor **shipped**)  
**Plan:** [`note_edit_fader_feedback_selection_driven_refactor.md`](note_edit_fader_feedback_selection_driven_refactor.md)  
**Build env:** `teensy41-capture-serial`  
**Capture port:** `/dev/cu.usbmodem154944801`

---

## Status summary

| Milestone | Status |
|-----------|--------|
| Phase 0 type aliases (`EntityIds.h`) | **Shipped** |
| Phase A.1 — NoteRef apply + motor sync gates | **Shipped** |
| Phase A.2 — Drop persisted `displayIdx`; derive OLED index | **Shipped** |
| Phase A.3 — Windowed selectable inventory | **Shipped** |
| Phase A.4 — Encoder via `applySelectNav`; dead list-index nav removed | **Shipped** |
| Phase A.5 — Native `test_note_edit_fader_feedback` | **Shipped** (`pio test -e native` pass at commit) |
| Phase A.6 — HITL slow fader-1 sweep (2+2 + second overdub) | **Shipped** — see captures below |
| Base HITL LOOP_EDIT precondition before record | **Shipped** — `hitl/edit_mode_precondition.py` |
| Phase B `NoteId` | **Not started** — resolve D0a (`EntityIds.h` scope) before schema work |

---

## What shipped (firmware)

| Area | Change |
|------|--------|
| Selection identity | `shouldApplySelectionOnNoteRefChange`, `noteEditSelectionTargetChanged`, `syncMotorsForDisplaySelection` gated on **NoteRef** — not list index alone |
| Session state | `NoteEditSelection` ref-only (`hasNote`, `NoteRef`, `bracketTick`); no persisted `displayIdx` |
| Display | `NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection`; windowed `selectableDisplayNotesForEditUi` |
| Encoder | `EditSelectNoteState` → `stepSelectNavSlot` → `applySelectNav` (removed `notesAtBracketTick` chord cycle) |
| Outbound | `NoteEditFaderOutboundPlan` ref-driven gates; `applySelectNav` signature without `displayIdx` |

**Primary files:** `src/NoteEditManager.cpp`, `src/EditManager.cpp`, `src/EditStates/EditSelectNoteState.cpp`, `include/NoteEditSessionState.h`, `include/Utils/NoteEditDisplaySnapshot.h`, `include/Utils/NoteEditFaderOutboundPlan.h`

---

## HITL evidence (2026-07-02)

### Base preset (record + 2 overdub passes)

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset base \
  --midi-out Teensy --midi-in Teensy \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 --out-dir captures
```

| Check | Result |
|-------|--------|
| LOOP_EDIT precondition | `[hitl] LOOP_EDIT confirmed after edit long press` |
| `PLAYING→OVERDUBBING` | 2 transitions |
| Undo/redo after overdub | undo=1 redo=1 |
| SEVT overdub span | count_ok, span_ok |
| DISP at transport stop | 61 frame notes |
| Report | `captures/host_midi_automation_baseline_20260702_011228.json` — **PASS** |

### Chained base → NOTE_EDIT → sweep

```bash
.venv/bin/python scripts/run_phase_a_slow_fader_sweep.py \
  --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --midi-channel 5 --slot-dwell-ms 1500
```

| Check | Result |
|-------|--------|
| Nav inventory | 59 note pairs / 59 nav slots (record + both overdubs via SEVT) |
| Multi-note steps | 17 sixteenth-steps with 2+ notes |
| `select_ignored_rate` | **0.0** |
| `apply_zero_fraction` | **0.0** |
| `motor_sync_sent` | 60 |
| Sibling sync | `sibling_select_count=2`, `same_bracket_apply_count=1` |
| Serial | `captures/phase_a_slow_fader_sweep_20260702_011229_serial.log` |

**Skip seed** (loop already on device):

```bash
.venv/bin/python scripts/run_phase_a_slow_fader_sweep.py --skip-base-seed \
  --serial-port /dev/cu.usbmodem154944801 --track 5 --midi-channel 5 --slot-dwell-ms 1500
```

---

## HITL scripts (new)

| Script | Role |
|--------|------|
| [`scripts/run_phase_a_slow_fader_sweep.py`](../../scripts/run_phase_a_slow_fader_sweep.py) | Base preset → NOTE_EDIT enter → slow fader-1 nav sweep + analyzers |
| [`scripts/hitl/baseline_loop_inventory.py`](../../scripts/hitl/baseline_loop_inventory.py) | Nav layout from base report config + REVT + SEVT (grid fallback if SEVT missing) |
| [`scripts/hitl/edit_mode_precondition.py`](../../scripts/hitl/edit_mode_precondition.py) | Exit NOTE_EDIT / confirm LOOP_EDIT before base record |
| [`scripts/hitl/verify/fader_select_sibling_sync.py`](../../scripts/hitl/verify/fader_select_sibling_sync.py) | Same-bracket / sibling select checks |
| [`scripts/hitl/verify/fader_motor_echo_correlation.py`](../../scripts/hitl/verify/fader_motor_echo_correlation.py) | MO→MI correlator (DROID ch13 ack optional) |

**Base preset fix:** [`scripts/hitl/scenarios/base.py`](../../scripts/hitl/scenarios/base.py) merges `canonical_baseline_legacy_args()` so `overdub-bars=2` is always set.

---

## Verification gates (routine)

```bash
pio test -e native
pio run -e teensy41-capture-serial   # ask before upload
.venv/bin/python scripts/run_phase_a_slow_fader_sweep.py --skip-base-seed ...  # after fresh base
```

---

## Open decisions before Phase B

1. **D0a — `EntityIds.h` scope** ([`openspec/changes/note-edit-stable-note-id/tasks.md`](../../openspec/changes/note-edit-stable-note-id/tasks.md) §2): rename (A), expand (B), or document-only (C). **User decision required** before `MidiEvent.noteId` / SD v6.
2. **Phase B gate:** Phase A exit criteria in [`design.md`](../../openspec/changes/note-edit-stable-note-id/design.md) — treat Phase A HITL sweep PASS as satisfied unless user wants more chord-order cases.

---

## Next chat — suggested scope (pick one)

| Track | OpenSpec | First tasks |
|-------|----------|-------------|
| **A — NoteId Phase B** | `note-edit-stable-note-id` §3 | Resolve D0a → `MidiEvent.noteId` + `Loop::allocateNoteId()` |
| **B — Fader RC11** | `note-edit-fader-feedback-regression` §Phase 8 | F2 loop-relative tick capture (`pb == expected_pb_rel`) |
| **C — Persistence** | `set-revision-persistence` | [`set_revision_persistence_handoff.md`](set_revision_persistence_handoff.md) — branch primary track per CURRENT_WORK |

**Do not** start Phase B `NoteId` without explicit user go-ahead and D0a resolution.

---

## Architecture checkpoint (Phase A)

| Question | Answer |
|----------|--------|
| Ownership change? | No — extended `EditManager` / `NoteEditManager` / `SelectNavigation` |
| State transition change? | Yes — selection apply/motor sync gated on NoteRef; user approved via OpenSpec Phase A |
| Recording in NOTE_EDIT? | **Fixed** — base HITL forces LOOP_EDIT before record |

---

## Related docs

- [`note_edit_fader_feedback_selection_driven_refactor.md`](note_edit_fader_feedback_selection_driven_refactor.md) — live selection-driven refresh design
- [`note_edit_stable_note_id_enhancement.md`](note_edit_stable_note_id_enhancement.md) — full Phase B–D roadmap
- [`note_edit_fader_feedback_phase8_handoff.md`](note_edit_fader_feedback_phase8_handoff.md) — RC11 F2 coordinate (parallel track)
