# Refactor priority backlog

**Kind:** index  
**Date:** 2026-08-06  
**Status:** living index — update when items ship or priorities change

Consolidated **refactor and naming-debt priority** across scattered plans. This is a navigation aid, not implementation authority.

| Layer | Authority for… |
|-------|----------------|
| [CURRENT_WORK.md](../runtime/CURRENT_WORK.md) | What to implement **now** |
| [ROADMAP.md](../runtime/ROADMAP.md) | Milestone sequencing (product) |
| [NAMING.md](../00-authority/NAMING.md) | Vocabulary policy (timeless) |
| **This file** | Cross-cutting refactor backlog and relative priority |

**Rules:** No rename-only PRs ([NAMING.md](../00-authority/NAMING.md) § Migration policy). Adopt preferred names when touching a subsystem. Run `pio test -e native` after logic refactors.

---

## How to read priority

| Priority | Meaning |
|----------|---------|
| **P1** | High impact on clarity or correctness; schedule when subsystem is next touched or as dedicated slice |
| **P2** | Medium impact; bundle with related feature or hygiene work |
| **P3** | Low impact or doc-only; opportunistic |
| **Done** | Shipped — kept for history; detail in source plan |

**Risk:** Low = rename/docs; Medium = multi-file symbol move + tests; High = ownership or stop-path touch.

---

## P1 — schedule next

| Item | Category | Impact | Risk | Trigger / timing | Source |
|------|----------|--------|------|------------------|--------|
| **StorageManager** TU extraction until `saveState` leaves root | Structural | High — maintainability | Medium | With persistence CURRENT_WORK | [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) § Next hygiene slices |
| **Persistence / overlay hardening** | Product | High — active milestone | High | CURRENT_WORK queue | [CURRENT_WORK.md](../runtime/CURRENT_WORK.md), [ROADMAP.md](../runtime/ROADMAP.md) |
| **`runEditSessionGeometryPipeline` → `runEditSessionGeometryResolution`** (+ files, driver, call sites) | Naming | High — wrong architectural metaphor | Medium | Next note-edit geometry refactor | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § B, § C; [NAMING.md](../00-authority/NAMING.md) § Geometry resolution |
| **Dedicated HITL refactor** (layered **`base`** gate device PASS) | Test infra | High — regression gate | Medium | Parked slice; confirm in CURRENT_WORK | [CURRENT_WORK.md](../runtime/CURRENT_WORK.md) |

---

## P2 — bundle with related work

| Item | Category | Impact | Risk | Trigger / timing | Source |
|------|----------|--------|------|------------------|--------|
| **`PersistenceQueue` → mid-pass/chunk-oriented name** | Naming + structural | Medium | Medium | Persistence hardening | [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) |
| **`completeOutboundPipelineAtDone` — evaluate rename** | Naming | Medium | Low–Medium | Next ControlSurface motor/outbound work | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § B |
| **Scoped EditPass model** (data model, not rename-only) | Data model | High — future edit domains | High | OpenSpec when prioritized; **do not** mix with naming drift Track 1 | [naming_drift_scoped_edit_pass_handoff.md](naming_drift_scoped_edit_pass_handoff.md) Track 2 |
| **Promote Commit/Publish glossary to NAMING.md** | Docs | Medium | Low | When unified publish pipeline stabilizes | [unified_publish_pipeline_commit_terminology_refinement.md](unified_publish_pipeline_commit_terminology_refinement.md) |
| **Phase 5 recovery** (deferred save) | Persistence | Medium | High | After persistence slice; currently parked | [CURRENT_WORK.md](../runtime/CURRENT_WORK.md) |

---

## P3 — opportunistic

| Item | Category | Impact | Risk | Trigger / timing | Source |
|------|----------|--------|------|------------------|--------|
| **`*Ingress.cpp` → `*Input.cpp`** (2 ControlSurface TUs) | Naming | Low | Low | Next ControlSurface TU work | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § B |
| **`Playing` → `Playback` in docs** where meaning is runtime machinery | Docs | Low | Low | Incremental doc edits | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § B |
| **`Controller` → Manager/Handler in plans** | Docs | Low | Low | When plans touched | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § B |
| **Mass `docs/plans/` archive / purge** | Docs hygiene | Low | Low | Optional | [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) item 18 |
| **HITL scenario imports off thin shims** | Test infra | Low | Low | Optional | [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) |
| **Future `PlaybackWindow` domain type** (musical working region) | Naming | Low | Medium | After `PlaybackMergedMidiEvents` stable; separate from merge cache | [slot_playback_window_interaction_architecture.md](slot_playback_window_interaction_architecture.md) |

---

## Done (recent — do not re-queue)

| Item | Shipped | Source |
|------|---------|--------|
| **NAMING.md** canonical authority + investigation doc | 2026-08-06 (`docs/architecture-naming-authority`) | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) |
| **Memory tier renames** (`InternalHeapFirstAllocator`, `ExternalMemoryFirstAllocator`, …) | Hygiene sprint | [naming_drift_scoped_edit_pass_handoff.md](naming_drift_scoped_edit_pass_handoff.md) Track 1 |
| **`mergeActiveCapturePasses`** (was `flattenActiveCapturePasses`) | Prior timeline refactor | [64bar_regression_commit_analysis_enhancement.md](64bar_regression_commit_analysis_enhancement.md) |
| **`PlaybackMergedMidiEvents`** (was `PlaybackWindow` merge cache) | Hygiene Phase −1 | [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) |
| **DisplayManager TU extraction** Phases 0–6 | Complete | [displaymanager_translation_unit_extraction_refinement.md](displaymanager_translation_unit_extraction_refinement.md) |
| **Firmware ownership lifetime review** P0–P1 | 2026-08-06 | [firmware_ownership_lifetime_review.md](firmware_ownership_lifetime_review.md) |

---

## Category index (detail lives in source plans)

| Category | Primary backlog |
|----------|-----------------|
| **Naming** | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § B; [naming_drift_scoped_edit_pass_handoff.md](naming_drift_scoped_edit_pass_handoff.md) Track 1 |
| **Structural / hygiene** | [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) |
| **Persistence / storage** | [CURRENT_WORK.md](../runtime/CURRENT_WORK.md); [set_revision_persistence_handoff.md](set_revision_persistence_handoff.md) |
| **Note edit** | [MOVE_NOTE_LOGIC.md](../Guides/MOVE_NOTE_LOGIC.md); geometry resolution § in naming investigation doc |
| **Display** | [displaymanager_translation_unit_extraction_refinement.md](displaymanager_translation_unit_extraction_refinement.md) (complete) |
| **HITL** | [HITL_TEST_SCENARIOS.md](../Guides/HITL_TEST_SCENARIOS.md); CURRENT_WORK HITL refactor row |
| **Data model** | [naming_drift_scoped_edit_pass_handoff.md](naming_drift_scoped_edit_pass_handoff.md) Track 2; `openspec/changes/scoped-edit-pass-model/` |

---

## Updating this file

When an item ships:

1. Move row to **Done** with date and branch/commit reference.
2. Update source plan status if applicable.
3. Do **not** duplicate full rename tables here — link to source plans.

When priority changes, adjust P1/P2/P3 only after CURRENT_WORK or user decision — this file does not override [CURRENT_WORK.md](../runtime/CURRENT_WORK.md).
