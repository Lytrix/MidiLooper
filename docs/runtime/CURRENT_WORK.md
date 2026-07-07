# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-07-07 (continuous-runtime-persistence Phase 0 diagnostics shipped)

---

## Now implementing

**OpenSpec: [`continuous-runtime-persistence`](../../openspec/changes/continuous-runtime-persistence/)** (DEC-020) — Phase 0 complete. **Next: Phase 1** chunk ownership and lifecycle.

| Phase | Status |
|-------|--------|
| **0** Diagnostics | **Complete** — HITL `20260707_202345` validated `PERS,diag` starvation metrics |
| 1 Chunk ownership and lifecycle | Pending |
| 2 Persistence queue | Pending |
| 3 Cooperative scheduler | Pending (after Phase 0 + 2) |
| 4 Mid-pass persistence | Pending |
| 5 Recovery | Pending |
| 6 Full 64+64 HITL | Pending |

| Doc | Role |
|-----|------|
| OpenSpec | [continuous-runtime-persistence](../../openspec/changes/continuous-runtime-persistence/) |
| Agent guide | [RUNTIME_STORAGE_AND_PERSISTENCE.md](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) |
| Evidence | `captures/host_midi_automation_baseline_20260707_192649.json` |

**Verify Phase 0:** `pio test -e native` · 64+64 HITL track 2/slot 1 — diagnostics only, no behavior change.

**Parked:** persistence starvation / `SC_REC_FLUSH` defer / transport-gate patches on `runtime-derived-representation-heap` → superseded by this change.

### Prior track (wind-down)

**OpenSpec: [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)** — M1–M3 shipped; M4 partial; M5 steps 1–2 shipped. No further persistence/stop-path patches here.

---

## Paused — blocked by UIP

**Derived note overlap logic** — EditSessionAction geometry pipeline:

- **OpenSpec:** [`edit-session-action-geometry`](../../openspec/changes/edit-session-action-geometry/) — **blocked** until UIP Phases 1–5 + HITL
- Handoff: [derived_note_overlap_logic_handoff.md](../plans/derived_note_overlap_logic_handoff.md)

## Explicitly NOT implementing

- D13 arrangement jam capture — future roadmap only
- Bisect / save-bypass HITL gates — parked (DEC-017)
- New `*Manager` classes for persistence — extend `StorageManager` (DEC-008)
- Note-edit continuous persistence — out of scope v1 (DEC-020)
- Transport-gate removal before Phase 0 diagnostics + Phase 2 queue — DEC-020 phase order
