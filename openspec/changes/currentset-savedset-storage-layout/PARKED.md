# Parked — tasks §2–3 superseded

**Date:** 2026-06-26  
**Superseded by:** `openspec/changes/set-revision-persistence/`

## Status

| Task | Status | Notes |
|------|--------|-------|
| §1 Incremental slot-skip | **Shipped** | 1.1–1.3 done; HITL `current_set_incremental_save` PASS |
| §2 Slot metadata index in CurrentSet `workspace.bin` | **Parked** | Re-scoped to `workspace.bin` slot summary in `set-revision-persistence` 2.3 |
| §3 SavedSet packed `loops.bin` | **Parked** | Replaced by append-only `revisions/v####.bin` commits |

## Do not implement

- Packed SavedSet writer/loader (tasks 3.1–3.3)
- CurrentSet `workspace.bin` slot index solely for flat SavedSet browser

## Reference

Product architecture: `docs/Plans/set_revision_persistence_architecture_enhancement.md`
