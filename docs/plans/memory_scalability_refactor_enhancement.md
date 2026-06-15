# Memory scalability refactor — enhancement plan

Canonical plan content lives in the Cursor plan file and mirrors this document.

See the active plan for full detail: observability investigation (Phase 1 removed), Phases 0–7, execution order, and gates.

**Summary:** 128-bar × 7-track target; fix hot-path snapshot/save stalls via Phase 0 + append-only PSRAM event chunks (Phase 4, 256 events/chunk, bar index, no per-bar buffers) + chunk-ref undo (Phase 3). Do **not** add MemoryMonitor checkpoints on record/overdub hot path — PSRAM pool walks cost hundreds of ms. Use existing `HotPathTelemetry` + idle-only `MemoryMonitor::logStatus()` instead.
