# Handoff — StorageSession (DEC-012) — CLOSED

**Date:** 2026-06-29  
**Branch:** `load-save-sets-loops`  
**OpenSpec archive:** [`openspec/changes/archive/2026-06-29-storage-session-state-refactor/`](../../openspec/changes/archive/2026-06-29-storage-session-state-refactor/)  
**Normative specs:** `openspec/specs/{revision-load,storage-session-jobs,storage-session-layout}/`  
**Decision:** [DEC-012](../DECISION_LOG.md#dec-012-storagesession-persistence-state-model)

---

## Status: shipped

DEC-012 Tier 0–3 complete. OpenSpec change archived 2026-06-29.

| Tier | Deliverable |
|------|-------------|
| **0–2** | `StorageActivitySnapshot`, `resolvePersistencePhase()`, `StorageSession` partial → full |
| **3** | FSM TUs; `Overlay.cpp`; job struct migration; API + policy renames; HITL serial RAM fix |
| **HITL** | Overlay MIDI presets (watchable dwell); dirty yes/no/cancel; `revision_commit_save`; `load_save_overlay_load` |

**Verification:** `pio test -e native` (292); `pio run -e teensy41-capture-serial`; device HITL 2026-06-29.

---

## Follow-on (not this change)

- `transport.bin` / `global.bin` workspace split — new OpenSpec when scoped
- `SetBrowserOverlayPolicy` module rename
- LoopPick behavior

---

## Session notes

- HITL `!OVERLAY_ENTER` crash: FLASHMEM `processHitlSerialCommands` + DMAMEM line buffer → fixed (buffer in RAM).
- HITL scenarios refactored to MIDI overlay gestures + `>>> dwell … <<<` logging (`scripts/hitl/scenarios/load_save_overlay_helpers.py`).
