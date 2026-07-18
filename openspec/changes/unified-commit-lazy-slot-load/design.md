## Context

Today (`af1227c`): boot queues every SD payload slot and full-drains under the OSTINATIX title before `bootInteractiveReady()` → USB + piano roll. Evidence: ~10.5 s for 26 slots (`session_20260718_020628.log`).

Runtime already has:

- `Loop::commitCapturePass` (record/overdub Commit)
- `hasPublishedEvents()` / published chunk ids (committed-pass truth under Publish names)
- `SlotLoadSession`, `needsSlotLoad`, `processDeferredLoopSlotRestore`, `isAudibleBootSlot`
- Deferred save FSM as the budgeted-process pattern

Architecture (frozen plans under `docs/plans/unified_publish_pipeline_*`) centers **Commit**, not Publish-as-visibility and not Load-as-a-special-subsystem.

Primary files: `Loop.cpp` / `Loop.h`, `StorageManager.cpp`, `SlotLoadSession.h`, `main.cpp`, `BootLoopSlotRestore.h`, `DisplayManager.cpp` (gates on published/committed presence).

Hot-path constraints: no full validate on record/overdub stop; deferred save via `requestDeferredSaveState`; do not invent a new Manager.

## Goals / Non-Goals

**Goals:**

- Normative Commit semantics for all producers (semantic parity, not one helper)
- Rename Publish→Commit for committed-pass APIs (action + scope)
- Audible-first boot interactive at COMMITTED without full-set restore
- On-demand deferred load for explicitly requested slots
- Clarify COMMITTED vs DERIVED_READY and hydration as architectural lifecycle

**Non-Goals:**

- v7 SD format; load-while-PLAYING interactive slices; speculative adjacent prefetch
- New `*Executor` / `*Manager`; renaming `RevisionCommit` / seal / mid_pass
- Forcing cooperative `SlotLoadSession` before audible-only boot MVP
- Evolving the architecture inside this change — implement the frozen model

## Decisions

### D1 — Commit is the operation; helpers are flexible

**Choice:** Specs require Commit semantics (staging invisible until Commit; committed state immutable; derived rebuildable). Producers MAY call different functions.

**Why not** require all paths through `commitCapturePass`: load/import restore different shapes; forcing one helper would create ownership transfer without need.

### D2 — COMMITTED sufficient; DERIVED_READY optional

**Choice:** Interactive UI and playback MAY proceed at COMMITTED. Derived caches MAY lag.

**Why not** wait for DERIVED_READY: that re-creates full-set latency at the derived layer.

### D3 — Hydration lifecycle not a mandated field

**Choice:** `UNLOADED → HEADER_READY → COMMITTED → DERIVED_READY` is architectural. Implementation may use flags, computed properties, or an enum.

### D4 — Boot scheduling vs load implementation

**Choice:** Boot uses **sync** audible restore (not deferred scheduler). MVP MAY keep `loadLoopSlotFromCurrentSetSd`. Cooperative session advance is a later delivery step, not a model prerequisite.

**Why not** route boot through deferred process: no playback deadlines at boot; sync is simpler and faster to interactive.

### D5 — Runtime priorities

**Choice:** Only (1) audible and (2) explicitly requested. No Priority-3 / adjacent prefetch in this change.

### D6 — Rename before feature code that would add Publish symbols

**Choice:** Phase 1 rename pass lands first (or alongside glossary freeze). New feature work uses Commit names.

### D7 — “Committed” vocabulary vs timeline-passes ban

**Choice:** `hasCommittedPasses` means “loop has canonical runtime-visible passes content” (former `hasPublishedEvents`). Specs continue to forbid **committed** as a *pass state label* / undo-kind suffix / container name on `LoopPasses`. Architectural **Commit** is the operation that places passes into `passes[]` (or attaches committed chunk ids), not a `PassState::Committed` enum.

### D8 — Extend existing owners

**Choice:** Extend `StorageManager` + `SlotLoadSession` + existing pending restore queue. No new top-level Manager. Mirror `processDeferredSaveState` for runtime load budgets when deferred path lands.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Rename churn breaks call sites / tests | Dedicated rename phase; `pio test -e native`; grep gate for Publish identifiers in committed-truth family |
| Audible-only boot leaves silent tracks if audible set wrong | Reuse `isAudibleBootSlot`; device gate: all actives audible on first Play |
| Select unloaded slot while PLAYING queues forever | MVP: defer until idle; document UX; Phase 7 later for mid-play |
| Dual load paths (monolith + session) linger | Allowed for MVP; converge in later tasks; do not block audible boot |
| Conflict with timeline-passes “no committed” wording | Explicit MODIFIED requirement clarifying operation vs pass-state label |

## Migration Plan

1. Freeze glossary + OpenSpec (this change) — **done at propose**  
2. New branch from `af1227c` (e.g. `feature/deferred-lazy-load`)  
3. Rename pass → native PASS  
4. Audible-only boot ready gate → device CAP vs `020628`  
5. Deferred on-demand restore → select unloaded while stopped  
6. Optional: factor load into cooperative `SlotLoadSession`  
7. Later: load-while-PLAYING (separate phase gate)

Rollback: revert branch; boot full-drain behavior remains on `af1227c` baseline.

## Open Questions

1. Exact public identifiers for boot sync restore / session advance after rename (Phase 0 freeze table).  
2. Whether a dedicated load-side queue type is needed beyond pending restore + `processDeferredLoopSlotRestore`.  
3. OpenSpec archive timing vs multi-PR delivery (implementer).  
