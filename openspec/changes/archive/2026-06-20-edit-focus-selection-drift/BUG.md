# BUG — Edit focus / selection drift (HITL, post Phase 2a)

**Change:** `edit-focus-selection-drift`  
**Status:** **Recheck complete** — consolidated into [note-edit-hitl-focus-restore](../note-edit-hitl-focus-restore/BUG.md) (**B1**)  
**Parent:** [note-edit-modification-session](../note-edit-modification-session/)  
**Primary capture:** `20260619_235109`, `20260620_000429`, `20260620_001421`, `20260620_001758` (+ `_serial.log` siblings)

---

## User-visible symptom

During the full edit baseline, **delete B** removes the wrong note and **overlap round-trip** does not leave **M67@16** at home. Fader moves after a long edit session may act on **focus.last** for a note other than the one fader-1 last selected.

---

## Serial proof (235109)

| Time (s) | Expected | Actual |
|----------|----------|--------|
| 64.051 | Select B @ tick 208 (pitch 64) | `Select fader: selected note 0 at tick 208` ✓ |
| 81.601 | D delay move (pitch 64 @ 880 → 400) | `Coarse fader using focus.last: pitch=64, start=880` ✓ for D |
| 86.401 | Delete B (pitch 64 @ ~208) | `NOTELEN double: delete selected note` → **`Deleting note pitch=67, start=400, end=496`** ✗ |

**Edit baseline report:** `edit.ok=false`

**Issues (JSON):**

- `insert_missing_after_in_edit_redo`
- `m0_not_home_after_round_trip`
- `after_overlap_round_trip_home:native_m0_count:0!=1`
- `after_overlap_round_trip_home:native_m0_pitch_67_count:0!=1`
- `split_overlap_note_mover_not_home`
- `split_victim_mover_not_home`

**Phase 1 AC verifier** (`verify_overlap_hidden_ac.py`):

| AC | Result |
|----|--------|
| AC1 | PASS |
| AC2 | PASS |
| AC3 | **FAIL** — no delete pitch=64 near tick 192 |
| AC4 | PASS |
| AC5 | **FAIL** — delete B not found |

**Contrast:** Pre-2a capture `20260619_233328` — AC1–AC5 all pass (Phase 1 sign-off).

---

## Suspected area (hypothesis — not confirmed)

| Layer | Notes |
|-------|--------|
| **focus.last vs selectedIdx** | `liveEditNoteForFader` prefers **focus.last** when `focus.active`; delete path may still use **selectedNoteIdx** / bracket without matching **focus** after undo/redo or long move chains |
| **Selection after in-edit undo/redo** | `insert_missing_after_in_edit_redo` in same run — may leave **focus** on moved M0 (67@400) while user-facing selection implied B |
| **Phase 2a bridge** | `bridgeMovingNoteWriterFromFocus` / `resetMovingNoteWriter` on select — correct for fader identity; delete may not read the same source |

**Do not fix here.** Track **B1** in [note-edit-hitl-focus-restore/BUG.md](../note-edit-hitl-focus-restore/BUG.md).

---

## Reproduction

```bash
pio run -e teensy41-capture-serial -t upload

.venv/bin/python scripts/host_midi_automation_edit_baseline.py \
  --midi-out "Teensy MIDI" --midi-in "Teensy MIDI" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --midi-channel 5 --record-bars 2 --start-transport \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 500
```

Artifacts: `captures/host_midi_automation_edit_baseline_20260619_235109.json`, `..._235109_serial.log`

---

## Recheck gate (after 2b–2d) — **DONE**

Recheck on `20260620_001421` and `20260620_001758`: **AC3/AC5 still fail**; `edit.ok=false`.  
→ [note-edit-hitl-focus-restore](../note-edit-hitl-focus-restore/) (**B1** delete/select + **B2** hidden restore on pitch).

---

## Related

- [note-edit-focus-reads](../note-edit-focus-reads/) — Phase 2a implementation
- [note-move-pitch-overlap-flaky/BUG.md](../note-move-pitch-overlap-flaky/BUG.md) — overlap round-trip (partially closed on `203729`)
- [change-length-commit-rematerialize/BUG.md](../change-length-commit-rematerialize/BUG.md) — Track A/B closed; Track C insert/reorder still open
