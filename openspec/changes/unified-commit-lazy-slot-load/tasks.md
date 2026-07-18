# Tasks — unified-commit-lazy-slot-load

Architecture (frozen): [`docs/plans/unified_publish_pipeline_deferred_lazy_loading_architecture.md`](../../../docs/plans/unified_publish_pipeline_deferred_lazy_loading_architecture.md)  
Review resolutions: [`docs/plans/unified_publish_pipeline_review_resolutions_refinement.md`](../../../docs/plans/unified_publish_pipeline_review_resolutions_refinement.md)  
Rename table: [`docs/plans/unified_publish_pipeline_commit_terminology_refinement.md`](../../../docs/plans/unified_publish_pipeline_commit_terminology_refinement.md)

**Branch:** create from `af1227c` (e.g. `feature/deferred-lazy-load`) before firmware.

## 1. Phase 0 — Freeze and OpenSpec

- [x] 1.1 Freeze architecture docs (parent Status: Frozen)
- [x] 1.2 Open OpenSpec change `unified-commit-lazy-slot-load` (proposal, design, specs, tasks)
- [x] 1.3 Point `CURRENT_WORK.md` / `PROJECT_STATE.md` at this change
- [x] 1.4 Append DEC entry to `docs/DECISION_LOG.md` (Commit-centered lazy slot load)
- [ ] 1.5 Freeze rename table rows as Action | Scope/object | Identifier (reject bare `ToCommitted` / new Flat)
- [ ] 1.6 Complete DECISION_REVIEW + PREFLIGHT before first firmware edit

## 2. Phase 1 — Publish → Commit rename

- [ ] 2.1 Rename committed-truth APIs per terminology table (`hasCommittedPasses`, chunk-id helpers, gather*Events*, `SlotLoadSessionState::Committing`, …)
- [ ] 2.2 Update guides / comments that describe published pass presence as committed passes
- [ ] 2.3 Update native tests and fixtures for renamed symbols
- [ ] 2.4 Grep gate: no remaining public Publish/Published identifiers in the committed-truth family
- [ ] 2.5 **Gate:** `pio test -e native` PASS

## 3. Phase 2 — Audible-only boot (existing sync load OK)

- [ ] 3.1 Stop auto-enqueue of non-audible SD slots at boot
- [ ] 3.2 Sync-commit audible set only (`isAudibleBootSlot`); MVP may keep `loadLoopSlotFromCurrentSetSd`
- [ ] 3.3 Redefine `bootInteractiveReady` for audible COMMITTED (explicit flag; never remap via `getActiveLoopIndex()`)
- [ ] 3.4 Allow piano roll / interactive UI at COMMITTED without waiting for DERIVED_READY
- [ ] 3.5 Update `docs/Guides/BOOT_LOAD.md`
- [ ] 3.6 **Gate:** device capture — ready ≪ full-set baseline `020628`; all actives audible on first Play

## 4. Phase 3 — Cooperative SlotLoadSession (incremental)

- [ ] 4.1 Extend `SlotLoadSession` cooperative advance (internals); `Publishing` → `Committing` already renamed
- [ ] 4.2 Optionally factor `loadLoopSlotFromCurrentSetSd` body into session steps
- [ ] 4.3 **Gate:** `pio test -e native`; boot still uses sync audible Commit

## 5. Phase 4 — Deferred on-demand restore

- [ ] 5.1 Budget `processDeferredLoopSlotRestore` for explicitly requested slots only (priorities: audible, requested)
- [ ] 5.2 Wire select/focus unload → request restore (`prioritizeLoopSlotRestoreForFocus` / equivalent)
- [ ] 5.3 MVP: while PLAYING, queue request and defer SD until transport idle
- [ ] 5.4 **Gate:** select unloaded slot while stopped → COMMITTED + display; no silent full-set hydrate
- [ ] 5.5 Device capture evidence

## 6. Phase 5+ — Later (parked until MVP green)

- [ ] 6.1 Derived rebuild off Commit critical path (DERIVED_READY optional polish)
- [ ] 6.2 Load while PLAYING interactive slices (formal gate)
- [ ] 6.3 Undo / import / paste documented on Commit contract (as those producers land)

## 7. Closeout

- [ ] 7.1 Update `CURRENT_WORK.md`, `PROJECT_STATE.md`, `DELIVERABLE_TRACKING.md`
- [ ] 7.2 Archive change when gates pass (`/opsx:archive`)
