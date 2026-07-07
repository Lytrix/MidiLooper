# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-07-08 (DEC-020 Phase 4 mid-pass persistence)

---

## Now implementing

**OpenSpec: [`continuous-runtime-persistence`](../../openspec/changes/continuous-runtime-persistence/)** (DEC-020) — Phase 4 **shipped** (native); HITL gate pending.

| Phase | Status |
|-------|--------|
| **0** Diagnostics | **Complete** |
| **1** Chunk lifecycle | **Complete** |
| **2** Persistence queue | **Complete** |
| **3** Cooperative scheduler | **Complete** — overdub-stop HITL passed (`f0ee520`) |
| **4** Mid-pass persistence | **Shipped** (native) — seal journal writer + failure policy; HITL gate pending |
| **5** Recovery | **Next** |
| **6** Full 64+64 HITL | Pending |

**M5 boot load** (DEC-022) — cold boot passes.

| Doc | Role |
|-----|------|
| Agent map | [RUNTIME_STORAGE_AND_PERSISTENCE.md](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) |
| OpenSpec | [continuous-runtime-persistence](../../openspec/changes/continuous-runtime-persistence/) |

**Verify:** `pio test -e native` (498/498) · Phase 4 HITL: 64-bar record with `#CAP,PERS,mid_pass` slices during capture; `freeChunkCount` above reserve

### Prior track (wind-down)

**OpenSpec: [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)** — M5 adopt-on-load shipped; lazy load + 64+64 HITL gate remain.

---

## Paused — blocked by UIP

**Derived note overlap logic** — EditSessionAction geometry pipeline:

- **OpenSpec:** [`edit-session-action-geometry`](../../openspec/changes/edit-session-action-geometry/) — **blocked** until UIP Phases 1–5 + HITL
- Handoff: [derived_note_overlap_logic_handoff.md](../plans/derived_note_overlap_logic_handoff.md)

## Explicitly NOT implementing

- D13 arrangement jam capture — future roadmap only
- Bisect / save-bypass HITL gates — parked (DEC-017)
- Workspace quarantine as substitute for load fix — valid data must load (DEC-019)
- Note-edit continuous persistence — out of scope v1 (DEC-020)
- Phase 5 journal prefix recovery — not started until Phase 4 HITL passes
