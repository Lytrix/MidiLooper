# Tasks — note edit HITL focus / restore bugs

**Change:** `note-edit-hitl-focus-restore`  
**Evidence:** [BUG.md](./BUG.md)  
**Status:** **Complete — ready to archive**

---

## Triage — done

- [x] Post-2d HITL — `001421`, `001758`
- [x] JSON `issues[]` classified — Track C out of scope
- [x] Architecture checkpoint — `design.md` locked

---

## Design — done

- [x] **design.md** — B1 locked (D1+D2); B2 locked (D4+D7)
- [x] Native test matrix: no new suites required; existing `test_note_edit_focus` / `test_edit_apply` + `pio test -e native` 110/110

---

## Patch B1 — Delete / selection — done

| # | Task | Status |
|---|------|--------|
| B1-1 | Select-after-pitch echo tolerance + lockout fix | [x] |
| B1-2 | `lastFader1SelectRef` + delete via **NoteRef** | [x] |
| B1-3 | Narrow delete commit | [x] cancelled — not needed |

**Verify:** AC3 + AC5 pass on `013630`, `013835`, `014601`.

---

## Patch B2 — Hidden restore on pitch — done

| # | Task | Status |
|---|------|--------|
| B2-1 | Length hide writes **focus.overlapNotes** | [x] |
| B2-2 | Retain **Hidden** after commit; `preCommitEmitted` | [x] |
| B2-3 | Pitch restore without deselect (`keepOverlapTrackingForPitchRestore=false`) | [x] |
| B2-4 | Length/move hide alignment | [x] cancelled — not needed for gate |

**Verify:** overlap JSON `p0_restore=True`, `inner_p0_ok=True` on sign-off captures.

---

## Verifier — done (scripts only)

- [x] **AC5:** dynamic `b_start`, tick tolerance, survivor **Final note** parsing
- [x] **AC1:** both restore log lines, pre-restore-only select gate, tick tolerance

---

## Sign-off — done

- [x] `pio test -e native` — 110/110
- [x] Full edit baseline ×3 → AC1–AC5 pass (`013630`, `013835`, `014601`)
- [x] BUG.md patch history + deferrals for parent `edit.ok=false`
- [x] Verifier fixes documented in BUG.md

**Deferred (non-gating):** parent insert/reorder, M0 home native counts, split-overlap home → [change-length-commit-rematerialize](../change-length-commit-rematerialize/BUG.md)

---

## Archive checklist

- [x] Firmware B1 + B2 on board (user-uploaded)
- [x] AC1–AC5 green ×3
- [x] BUG.md status = signed off
- [x] Run `/opsx:archive` — archived 2026-06-20
