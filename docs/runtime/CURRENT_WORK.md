# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-07-08 (DEC-020 Phase 3 cooperative scheduler)

---

## Now implementing

**OpenSpec: [`continuous-runtime-persistence`](../../openspec/changes/continuous-runtime-persistence/)** (DEC-020) — Phase 3 **shipped** (native); short HITL gate pending capture-serial build.

| Phase | Status |
|-------|--------|
| **0** Diagnostics | **Complete** |
| **1** Chunk lifecycle | **Complete** |
| **2** Persistence queue | **Complete** — `PersistenceQueue`, `test_persistence_queue` |
| **3** Cooperative scheduler | **Shipped** — transport gate removed; HITL gate pending |
| **4** Mid-pass persistence | **Next** |
| 5–6 | Pending |

**M5 boot load** (DEC-022) — cold boot passes. **Build blocker:** `teensy41-capture-serial` RAM1 overflow (~30 KB) blocks HITL validation.

| Doc | Role |
|-----|------|
| Agent map | [RUNTIME_STORAGE_AND_PERSISTENCE.md](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) |
| OpenSpec | [continuous-runtime-persistence](../../openspec/changes/continuous-runtime-persistence/) |

**Verify:** `pio test -e native` · Phase 3 HITL: 16-bar record+overdub with slice during capture

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
- Transport-gate removal before Phase 2 queue — DEC-020 phase order
