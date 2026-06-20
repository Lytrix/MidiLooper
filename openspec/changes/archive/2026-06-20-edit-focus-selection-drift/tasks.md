# Tasks — edit focus selection drift (parked)

**Change:** `edit-focus-selection-drift`  
**Status:** **Closed** — recheck done; fixes shipped in **note-edit-hitl-focus-restore** (2026-06-20)  
**Archived:** `openspec/changes/archive/2026-06-20-edit-focus-selection-drift/`  
**Evidence:** [BUG.md](./BUG.md)

**Gate:** Recheck done — failures tracked in archived [note-edit-hitl-focus-restore](../archive/2026-06-20-note-edit-hitl-focus-restore/).

---

## Recheck (sign-off) — done

- [x] `pio test -e native`
- [x] Full HITL edit baseline → `001421`, `001758`
- [x] `verify_overlap_hidden_ac.py` → AC3/AC5 fail both runs (pre-fix)
- [x] Consolidated into archived [note-edit-hitl-focus-restore/BUG.md](../archive/2026-06-20-note-edit-hitl-focus-restore/BUG.md)
- [x] B1/B2 fixes signed off — captures `013630` / `013835` / `014601`

---

## Patch phase (locked — not reopened)

Unlocked only when recheck fails and triage assigns root cause to **focus vs selection** (not overlap engine or insert/reorder Track C alone). No reopen after **note-edit-hitl-focus-restore** sign-off.
