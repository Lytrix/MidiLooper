# Tasks — edit focus selection drift (parked)

**Change:** `edit-focus-selection-drift`  
**Evidence:** [BUG.md](./BUG.md)

**Gate:** Recheck done — failures tracked in [note-edit-hitl-focus-restore](../note-edit-hitl-focus-restore/).

---

## Recheck (sign-off) — done

- [x] `pio test -e native`
- [x] Full HITL edit baseline → `001421`, `001758`
- [x] `verify_overlap_hidden_ac.py` → AC3/AC5 fail both runs
- [x] Consolidated into [note-edit-hitl-focus-restore/BUG.md](../note-edit-hitl-focus-restore/BUG.md)

---

## Patch phase (locked)

Unlocked only when recheck fails and triage assigns root cause to **focus vs selection** (not overlap engine or insert/reorder Track C alone).
