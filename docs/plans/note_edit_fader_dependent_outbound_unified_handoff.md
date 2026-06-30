# Handoff — NOTE_EDIT dependent outbound (F2/F3/F4 on F1 select)

**Date:** 2026-06-30  
**Branch:** `load-save-sets-loops`  
**Base commit (implement here):** `3adb27b` — *WIP: F3/F4 follow F1 select; F2 coarse and note select still broken.*  
**OpenSpec change:** [`openspec/changes/note-edit-fader-feedback-regression/`](../../openspec/changes/note-edit-fader-feedback-regression/)  
**Prior handoff:** [`docs/plans/note_edit_fader_feedback_phase8_handoff.md`](note_edit_fader_feedback_phase8_handoff.md)  
**Investigation plan:** [`.cursor/plans/fader_dependent_outbound_fix_8d376618.plan.md`](../../.cursor/plans/fader_dependent_outbound_fix_8d376618.plan.md)  
**Build env:** `teensy41-capture-serial`  
**Capture port:** `/dev/cu.usbmodem154944801`

---

## Status summary

| Area | Status |
|------|--------|
| Phase 3 outbound coordinator (`processFaderOutbound`) | **Shipped** (`ab1e3b0`) |
| F2 diagnostics (`#DBG outbound_ctx`, analyzer) | **Shipped** (`2ecf25d`) |
| F3/F4 motor triggers (ch15 LED bypass) | **Partial** (`3adb27b`) — motors follow F1; pace-skip side effect may hurt F2 |
| F2 motor on F1 select (~80% user-visible) | **Open** — firmware sends 100%; motor latch / quiet refresh |
| F2/F3 absolute position (RC11) | **Open** — `loopStartTick` not applied in outbound send helpers |
| D34 send honesty + D20 single trigger | **Not started** |
| Phase 8–12 firmware | **Not started** (docs partially updated) |

**Do not implement on `2ecf25d`.** That commit lacks ch15 motor bypass; F3/F4 fix would have to be re-added. `3adb27b` is the superset (+16 lines in `NoteEditManager.cpp` / `MidiHandler.cpp`).

---

## Commits (fader track)

| Commit | Summary |
|--------|---------|
| `ab1e3b0` | Phases 1–3 outbound coordinator + capture diagnosis |
| `2ecf25d` | `#DBG outbound_ctx` + `scripts/analyze_fader2_select_feedback.py` |
| **`3adb27b`** | **`setDroidMotorOutboundPriority`** — F3/F4 direct; F2 coarse still broken per commit message |

---

## Capture evidence (read before coding)

| Log | Commit / build | User observation | Key metric |
|-----|----------------|------------------|------------|
| [`captures/session_20260630_191718.log`](../../captures/session_20260630_191718.log) | `2ecf25d` | F1+F2 responsive | HITL **PASS**; RC11 `pb != expected_pb_rel` (`loop_start=424`) |
| [`captures/session_20260630_211328.log`](../../captures/session_20260630_211328.log) | `3adb27b` | F3+F4 follow F1; F2 bad | ch15 triggers **320/320** `OUT NoteOn ch=15` |
| [`captures/session_20260630_222821.log`](../../captures/session_20260630_222821.log) | `2ecf25d` (F1–F2 run) | F2 ~**80%** motor update | **243/243** slot changes → new `pb` within 3 ms; **259/259** `SEND_F2` ↔ `MO,224,14` |

### Settled (no spike needed)

1. **Firmware sends F2 on every slot change** — `session_20260630_222821` proves send path; ~20% miss is not “forgot to send.”
2. **F3/F4 need ch15 LED bypass** — `2ecf25d`: 0% direct ch15 motor OUT; `3adb27b`: 100% direct.
3. **QUIET_REFRESH duplicates** — 13/13 quiet bursts in `222821` repeated same `pb` + tick; cannot recover a missed motor latch.
4. **Motor echo rare** — `MO,224,14` ×259 vs `MI,H,224,14` ×26 in `222821`.

### Still open (tune after patch)

- **Pace-skip in `3adb27b`** — `paceDroidUsbHostBeforeSend()` returns early when `droidMotorOutboundPriority_`; narrow to LED bypass only.
- **D10 motor settle** — only if post-patch capture still shows miss.
- **RC11** — verify on loop with `loopStartTick ≠ 0` after Phase 8.

---

## Implementation order (next session)

Execute on **`3adb27b`** (current HEAD). **Architecture checkpoint:** transport pacing + tick input + pipeline gating only — no ownership or session transition changes.

| Step | What | Files |
|------|------|-------|
| **1** | Keep LED bypass; **remove** pace-skip from `paceDroidUsbHostBeforeSend` | [`src/MidiHandler.cpp`](../../src/MidiHandler.cpp) |
| **1b** | Skip quiet refresh when `pb`+tick unchanged; log `QUIET_REFRESH` only when outbound scheduled | [`src/NoteEditManager.cpp`](../../src/NoteEditManager.cpp) |
| **2** | D34: send helpers return `bool`; skip triggers on no-op | `NoteEditManager.cpp` |
| **3** | D20: single motor trigger owner (remove inline triggers from send helpers) | `NoteEditManager.cpp` |
| **4** | RC11: `noteRelativeTick` in `sendCoarseFaderPosition` + native test | `NoteEditManager.cpp`, [`test/test_note_edit_fader_feedback/`](../../test/test_note_edit_fader_feedback/) |
| **5** | RC11 fine: same for position-mode `sendFineFaderPosition` | `NoteEditManager.cpp` |
| **6** | Phase 9 (if needed): arm F1 ignore during F2 outbound burst | `NoteEditManager.cpp` |

---

## Code pointers (`3adb27b`)

**Motor priority (keep LED part, fix pace):**

```cpp
// MidiHandler.cpp — paceDroidUsbHostBeforeSend: REMOVE early return when droidMotorOutboundPriority_
// Keep: queueAsLed = isLedChannel && !droidMotorOutboundPriority_
// Keep: processDroidUsbHostOutbound early return when priority active
```

**Outbound pipeline steps:** `BEGIN → ARM → SEND_F2 → TRIGGER_F2 → SEND_F3 → TRIGGER_F3 → SEND_F4 → TRIGGER_F4 → DONE`

**Known bugs in current code:**

- `sendCoarseFaderPosition` calls `sendCoarseFaderMotorTrigger()` at line ~1012 **and** pipeline runs `TriggerCoarse` (double F2 trigger).
- `processFaderOutbound` logs `SEND_F2` even when `No note selected for coarse position` (empty slot → trigger-only burst).
- `processFaderSelectQuiet` logs `QUIET_REFRESH` even when `requestFaderOutbound` is skipped.

**RC11 fix sketch (Phase 8):**

```cpp
const uint32_t loopStartTick = track.getLoopStartTick() % loopLength;
const uint32_t storageTick = lengthEditingMode ? liveNote.endTick : liveNote.startTick;
const uint32_t relTick =
    SelectNavigation::noteRelativeTick(storageTick, loopStartTick, loopLength);
anchorTick = relTick;
```

---

## Verification

```bash
# Host (required before push)
pio test -e native

# Firmware build (ask user before upload)
pio run -e teensy41-capture-serial

# Capture (parallel terminal)
.venv/bin/python scripts/capture_session.py --port /dev/cu.usbmodem154944801

# Analyze
python3 scripts/analyze_fader2_select_feedback.py captures/<session>.log --after <NOTE_EDIT_entry_s>
rg '#DBG outbound_ctx|OUT NoteOn.*ch=15|MO,224,14|QUIET_REFRESH|No note selected' captures/<session>.log
```

**Pass criteria (note selected):**

1. `MO,224,14` before every `SEND_F2`; `pb == expected_pb_rel` when `loop_start ≠ 0`
2. `OUT NoteOn ch=15 note=1/2` for F3/F4 motor triggers
3. Empty slot: no trigger-only `SEND_F2/3/4` (D34)
4. HITL [`scripts/hitl/verify/note_edit_fader_select_refresh.py`](../../scripts/hitl/verify/note_edit_fader_select_refresh.py) — clusters ≤3 s F2 gap

Use **same loop** with non-zero `loopStartTick` (e.g. 424/768) for RC11; do not compare absolute F2 position across captures with different `loop_start`.

---

## Out of scope (this handoff)

- Revert to `2ecf25d` or remove Phase 3 coordinator
- Full `MidiFaderProcessor` / `cc21df9` restore ([`docs/plans/note_edit_fader_dependent_outbound_regression_bugfix.md`](note_edit_fader_dependent_outbound_regression_bugfix.md) parked items)
- Phase 12 dead-code cleanup **before** post-patch capture passes

---

## Session notes

- Working tree at handoff: **clean**, HEAD **`3adb27b`**, branch **3 commits ahead** of `origin/load-save-sets-loops`.
- User confirmed: implement on `3adb27b`, not `2ecf25d`.
- After implementation: update [`openspec/changes/note-edit-fader-feedback-regression/tasks.md`](../../openspec/changes/note-edit-fader-feedback-regression/tasks.md), [`BUG.md`](../../openspec/changes/note-edit-fader-feedback-regression/BUG.md) (pace-overreach RC), [`docs/runtime/CURRENT_WORK.md`](../../docs/runtime/CURRENT_WORK.md) if in scope.
