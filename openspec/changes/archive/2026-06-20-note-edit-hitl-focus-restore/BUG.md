# BUG — Note edit HITL focus / hidden restore (post Phase 2d)

**Change:** `note-edit-hitl-focus-restore`  
**Status:** **Signed off — ready to archive**  
**Parent:** [note-edit-modification-session](../note-edit-modification-session/)  
**Supersedes:** [edit-focus-selection-drift](../archive/2026-06-20-edit-focus-selection-drift/) recheck (2b–2d complete)  
**Firmware:** `teensy41-capture-serial` after B1 + B2 patches  
**Sign-off captures:** `20260620_013630`, `20260620_013835`, `20260620_014601` (+ `_serial.log` / `.json` siblings)  
**Pre-fix captures (evidence):** `20260620_001421`, `20260620_001758`

---

## Sign-off summary (2026-06-20)

| Capture | AC1 | AC2 | AC3 | AC4 | AC5 | Overlap JSON |
|---------|-----|-----|-----|-----|-----|--------------|
| `013630` | PASS | PASS | PASS | PASS | PASS | `p0_restore=True`, `inner_p0_ok=True` |
| `013835` | PASS | PASS | PASS | PASS | PASS | same |
| `014601` | PASS | PASS | PASS | PASS | PASS | same |

**Gate:** `verify_overlap_hidden_ac.py` AC1–AC5 pass on three consecutive full edit baselines after B1 + B2 firmware upload. Delete B stable: `pitch=64, start=200` (±16th tolerance).

**Parent `edit.ok=false`:** **Non-gating** per [overlap-hidden-note-select C16](../archive/2026-06-19-overlap-hidden-note-select/CLARIFICATIONS.md) — remaining issues deferred to [change-length-commit-rematerialize](../change-length-commit-rematerialize/BUG.md) (Track C insert/reorder, M0 home native counts, split-overlap mover home, short-over-long forward/restore flags).

**Incomplete run (discard):** `014322` — interrupted; delete wrong target (`pitch=64 start=296`).

---

## User-visible symptoms (fixed)

### B1 — Delete wrong note after long edit session

**Was:** Delete B (pitch 64 @ fixture step 4) removed inner **A@67** or moved **D@67** while fader-1 had selected B.  
**Fix:** D1+D2 — `lastFader1SelectRef` + delete via **NoteRef**; select echo tolerance after fader-4 pitch.

### B2 — Hidden overlap note not restored after lengthen + move + pitch up

**Was:** P0@60 hidden on length lengthen; pitch 60→67 left P0 missing from session store / display while mover selected (`001758`).  
**Fix:** D4+D7 — retain **Hidden** in `overlapNotes` across commit; pitch lane restore drops overlap filter (`keepOverlapTrackingForPitchRestore=false`).

---

## HITL summary (pre-fix — post 2d upload)

| Capture | `edit.ok` | AC1 | AC2 | AC3 | AC4 | AC5 |
|---------|-----------|-----|-----|-----|-----|-----|
| `001421` | false | FAIL* | PASS | FAIL | PASS | FAIL* |
| `001758` | false | PASS | PASS | FAIL | PASS | FAIL* |

\* AC1/AC5 failures on pre-fix runs included **verifier false positives** (see § Verifier fixes). AC3/AC5 firmware failures were real (B1).

---

## Serial proof — B1 delete / selection drift (`001421`)

| Time (s) | Expected | Actual |
|----------|----------|--------|
| 139.691 | Select B @ tick 216 (pitch 64) | `Select fader: selected note 0 at tick 216` ✓ |
| 162.041 | Delete B (pitch 64 @ ~216) | `NOTELEN double: delete selected note` → **`Deleting note pitch=67, start=408, end=504`** ✗ |

Post-fix (`014601` @ delete-B): `Deleting note pitch=64, start=200, end=296`; `session_store: M64@200 missing in recon flatEvents=12`.

---

## Serial proof — B2 hidden P0 not restored on pitch (`001758`)

| Step | Serial |
|------|--------|
| Length lengthen M0 end 120→696 | `Will delete overlapping note … pitch=60, start=600` → `Stored hidden overlap note: pitch=60, start=600, end=697` |
| Move over inner notes (step 0→10) | `overlapNotes=0` on move lines |
| Pitch M0 60→67 | **no** `Will restore note after pitch change` for P0@600 |

Post-fix (`014601`): `Will restore note after pitch change: pitch=60, start=584` → `Restoring hidden overlap note: pitch=60, start=584, end=680` → `inner_p0_ok=true` in JSON.

---

## Verifier fixes (`scripts/verify_overlap_hidden_ac.py`)

Verifier-only; no firmware. Required because fixture ticks drift (e.g. B@200 vs hardcoded 208) and B2 restore log line changed.

| AC | Problem | Fix |
|----|---------|-----|
| **AC5** | Hardcoded `M64@208`, `M67@16-688`, `D@880` | Derive `b_start` from delete log; tick tolerance on session_store drop and survivor **Final note** checks |
| **AC1** | Only `Restoring deleted note:` counted as restore; exact tick match | Also `Restoring hidden overlap note:`; tick tolerance; flag select only **before** matching restore (post-restore select is valid) |

---

## Edit baseline issue lists — deferred (Track C / parent sub-verifiers)

Union from sign-off runs `013630`, `013835`, `014601` — **not gating** this change:

- `insert_not_restored_after_move_back`, `insert_missing_after_in_edit_redo`
- `m0_not_home_after_round_trip`, `after_overlap_round_trip_home:native_m0_*`
- `split_overlap_note_mover_not_home`, `split_victim_mover_not_home`
- `short_over_long_forward` / `short_over_long_restore` (verifier flags)

→ [change-length-commit-rematerialize/BUG.md](../change-length-commit-rematerialize/BUG.md)

---

## Root-cause findings (2026-06-20, serial-backed)

### Architecture checkpoint

| Question | Answer | Evidence |
|----------|--------|----------|
| Changing **focus.overlapNotes** ownership? | **Yes** (B2) | `commitAllPendingNoteEditActions` cleared scratch after length commit |
| Changing delete/select contract? | **Yes** (B1) | Delete used stale `selectedNoteIdx`; fader-1 reselect dropped after pitch edit |

B1 and B2 shipped as separate firmware diffs (see patch history).

---

### B1 — Delete wrong note (fixed: D1+D2)

**Serial chain (pre-fix `001421` @ 162.041 s):**

1. Script sends fader-1 select B (step 4).
2. Select **ignored**: `Pitchbend ch=16 ignored (ch16 echo after note-value edit)`.
3. `selectedNoteIdx` still pointed at **D-delay** note (~408, pitch 67).
4. Delete ran on wrong note: `Deleting note pitch=67, start=408`.

**Shipped:** echo tolerance + `lastFader1SelectRef` / **NoteRef** delete path. B1-3 (narrow delete commit) not needed.

---

### B2 — Hidden P0 not restored on pitch (fixed: D4+D7)

**Pre-fix contrast (`001758` vs `001421`):** empty `overlapNotes` after length commit blocked pitch restore.

**Shipped:** retain **Hidden** scratch across commit (`preCommitEmitted`); pitch restore with `keepOverlapTrackingForPitchRestore=false`. B2-4 (length/move hide alignment) not needed for sign-off gate.

---

## Patch history

| Date | Attempt | Result |
|------|---------|--------|
| 2026-06-20 | Root-cause serial review (`001421`, `001758`) | B1/B2 hypotheses ranked |
| 2026-06-20 | B1 patch (D1+D2) | AC3 stable |
| 2026-06-20 | B2 patch (D4+D7) | `p0_restore=True`, `inner_p0_ok=True` |
| 2026-06-20 | Verifier AC5 fix | Dynamic ticks + tolerance |
| 2026-06-20 | Verifier AC1 fix | Both restore log lines + pre-restore-only select gate |
| 2026-06-20 | HITL `013630`, `013835`, `014601` | **AC1–AC5 pass ×3** — sign-off |

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

.venv/bin/python scripts/verify_overlap_hidden_ac.py \
  captures/host_midi_automation_edit_baseline_<stamp>_serial.log
```

---

## Acceptance criteria (patch gate)

| ID | Criterion | Status |
|----|-----------|--------|
| AC-B1 | Delete B removes pitch 64 @ B’s start tick (verifier AC3/AC5 pass) | **Pass** ×3 |
| AC-B2 | After lengthen+move+pitch up, hidden P0 restored (`Will restore…` + `inner_p0_ok`) | **Pass** |
| AC-HITL | AC1–AC5 on full baseline; parent `edit.ok` deferrals documented | **Pass** (C16) |
| AC-NATIVE | `pio test -e native` 110/110 | **Pass** (pre-upload matrix) |

---

## Related

- [edit-focus-selection-drift/BUG.md](../archive/2026-06-20-edit-focus-selection-drift/BUG.md) — pre-2d delete drift (superseded by this sign-off)
- [note-edit-focus-reads](../note-edit-focus-reads/) — Phase 2d shipped
- [change-length-commit-rematerialize/BUG.md](../change-length-commit-rematerialize/BUG.md) — Track C insert/reorder overlap
- [archive/2026-06-19-overlap-hidden-note-select/BUG.md](../archive/2026-06-19-overlap-hidden-note-select/BUG.md) — Phase 1 AC1–AC5 contract + C16 non-gating
