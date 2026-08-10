# Refactor priority backlog

**Kind:** index  
**Date:** 2026-08-08  
**Status:** living index — update when items ship or priorities change

Consolidated **refactor and naming-debt priority** across scattered plans. This is a navigation aid, not implementation authority.

| Layer | Authority for… |
|-------|----------------|
| [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) | What to implement **now** |
| [ROADMAP.md](../Runtime/ROADMAP.md) | Milestone sequencing (product) |
| [NAMING.md](../Authority/NAMING.md) | Vocabulary policy (timeless) |
| **This file** | Cross-cutting refactor backlog and relative priority |

**Rules:** No rename-only PRs ([NAMING.md](../Authority/NAMING.md) § Migration policy). Adopt preferred names when touching a subsystem. Run `pio test -e native` after logic refactors.

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
| **TrackManager TU extraction** (process boundaries) | Structural | High — largest remaining monolith | Medium–High (transport/capture phases) | After #18 Phase 4 design — `refactor/trackmanager` first | [codebase_consistency_phase4_extraction_boundaries_refinement.md](codebase_consistency_phase4_extraction_boundaries_refinement.md) §1 |
| **NoteMovementUtils TU split** (pair resolve vs geometry apply) | Structural | Medium — edit hot path clarity | Medium | After TrackManager or parallel low phases | Same doc §2 |
| **DisplayNoteResolve second split** | Structural | Medium — display read path | Medium | After DisplayManager Phase 2 shipped | Same doc §3 |
| **Persistence / overlay hardening** | Product | High — active milestone | High | CURRENT_WORK queue | [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md), [ROADMAP.md](../Runtime/ROADMAP.md) |
| **Dedicated HITL refactor** (layered **`base`** gate device PASS) | Test infra | High — regression gate | Medium | Parked slice; confirm in CURRENT_WORK | [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) |

---

## P2 — bundle with related work

| Item | Category | Impact | Risk | Trigger / timing | Source |
|------|----------|--------|------|------------------|--------|
| **`Note geometry` promotion** (`EditedGeometry`, `ResolveConstrainedGeometry`, ControlSurface geometry driver, …) | Naming | Medium — boundary ambiguity | Low–Medium | Touch-and-rename per [NAMING.md](../Authority/NAMING.md) § Note geometry | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § C; EditManager Phase 7c / ControlSurface |
| **`completeOutboundPipelineAtDone` — evaluate rename** | Naming | Medium | Low–Medium | Next ControlSurface motor/outbound work | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § B |
| **Scoped EditPass model** (data model, not rename-only) | Data model | High — future edit domains | High | OpenSpec when prioritized; **do not** mix with naming drift Track 1 | [naming_drift_scoped_edit_pass_handoff.md](naming_drift_scoped_edit_pass_handoff.md) Track 2 |
| **Promote Commit/Publish glossary to NAMING.md** | Docs | Medium | Low | When unified publish pipeline stabilizes | [unified_publish_pipeline_commit_terminology_refinement.md](unified_publish_pipeline_commit_terminology_refinement.md) |
| **Phase 5 recovery** (deferred save) | Persistence | Medium | High | After persistence slice; currently parked | [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) |
| **`PersistenceQueue` → mid-pass/chunk-oriented name** | Naming | Medium | Low–Medium | With persistence hardening | [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) |

---

## P3 — opportunistic

| Item | Category | Impact | Risk | Trigger / timing | Source |
|------|----------|--------|------|------------------|--------|
| **`Controller` → Manager/Handler in plans** | Docs | Low | Low | When plans touched | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § B |
| **Mass `docs/Plans/` archive / purge** | Docs hygiene | Low | Low | Optional | [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) item 18 |
| **HITL scenario imports off thin shims** | Test infra | Low | Low | Optional | [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) |
| **Future `PlaybackWindow` domain type** (musical working region) | Naming | Low | Medium | After `PlaybackMergedMidiEvents` stable; separate from merge cache | [slot_playback_window_interaction_architecture.md](slot_playback_window_interaction_architecture.md) |

---

## Done (recent — do not re-queue)

| Item | Shipped | Source |
|------|---------|--------|
| **Codebase consistency Phase LR** NoteMovementUtils shims | 2026-08-10 — PR [#26](https://github.com/Lytrix/MidiLooper/pull/26) | [phase4 plan](codebase_consistency_phase4_extraction_boundaries_refinement.md) |
| **`*Ingress.cpp` → `*Input.cpp`** (ControlSurface TUs) | 2026-08-10 — code `c5f1da1`; header comments + doc closeout PR #27 | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § B |
| **`Playing` → `Playback` in active Plans** (runtime machinery + Phase 2.4 symbol sync) | 2026-08-10 — doc pass | same § B |
| **StorageManager** Phases 0–8 + remaining root trim (HITL, preamble/epilogue, status-query inlines); root ~422 LOC | 2026-08-08 — [PR #17](https://github.com/Lytrix/MidiLooper/pull/17) merged → `dev`; Task [#16](https://github.com/Lytrix/MidiLooper/issues/16) closed | [storagemanager_translation_unit_extraction_refinement.md](storagemanager_translation_unit_extraction_refinement.md) |
| **`NoteEditFocus.cpp` TU extraction** (Phases 0–10 + LR) | Done — root TU removed | [noteditfocus_translation_unit_extraction_refinement.md](noteditfocus_translation_unit_extraction_refinement.md) |
| **`Loop.cpp` TU extraction** (Phases 0–11) | Merged to `dev` | [loop_translation_unit_extraction_refinement.md](loop_translation_unit_extraction_refinement.md) |
| **EditManager** TU extraction (Phases 0–9) | PR #12 → `dev` | [editmanager_translation_unit_extraction_refinement.md](editmanager_translation_unit_extraction_refinement.md) |
| **Track** TU extraction (Phases 0–11) | PR #13 → `dev` | [track_translation_unit_extraction_refinement.md](track_translation_unit_extraction_refinement.md) |
| **DisplayManager** TU extraction (Phases 0–6) | Complete | [displaymanager_translation_unit_extraction_refinement.md](displaymanager_translation_unit_extraction_refinement.md) |
| **`runEditSessionGeometryPipeline` → `NoteGeometryResolver`** | EditManager Phase 7 / PR #12 | [editmanager_translation_unit_extraction_refinement.md](editmanager_translation_unit_extraction_refinement.md); [NAMING.md](../Authority/NAMING.md) § Geometry resolution |
| **NAMING.md** canonical authority + investigation doc | 2026-08-06 | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) |
| **Memory tier renames** (`InternalHeapFirstAllocator`, `ExternalMemoryFirstAllocator`, …) | Hygiene sprint | [naming_drift_scoped_edit_pass_handoff.md](naming_drift_scoped_edit_pass_handoff.md) Track 1 |
| **`mergeActiveCapturePasses`** (was `flattenActiveCapturePasses`) | Prior timeline refactor | [64bar_regression_commit_analysis_enhancement.md](64bar_regression_commit_analysis_enhancement.md) |
| **`PlaybackMergedMidiEvents`** (was `PlaybackWindow` merge cache) | Hygiene Phase −1 | [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) |
| **Mechanical TU split workflow** (template + Cursor rule + smoke checklist) | 2026-08-06 | [Mechanical-TU-Split-Workflow.mdc](../../.cursor/rules/Mechanical-TU-Split-Workflow.mdc) |
| **Legacy API retirement** (move first, Phase LR) | Policy doc + rule | [legacy_api_retirement_tu_extraction_refinement.md](legacy_api_retirement_tu_extraction_refinement.md) |
| **Firmware ownership lifetime review** P0–P1 | 2026-08-06 | [firmware_ownership_lifetime_review.md](firmware_ownership_lifetime_review.md) |
| **Branch/PR default for tracked Issues** (Work board NOW/NEXT/PARKED) | 2026-08-08 on `dev` | [GITHUB_WORK_TRACKING.md](../Authority/GITHUB_WORK_TRACKING.md) §10 |

---

## TU extraction summary

| Module | Plan | Status |
|--------|------|--------|
| DisplayManager | [displaymanager…](displaymanager_translation_unit_extraction_refinement.md) | **Done** |
| StorageManager | [storagemanager…](storagemanager_translation_unit_extraction_refinement.md) | **Done** (PR #17; root ~422 LOC) |
| EditManager | [editmanager…](editmanager_translation_unit_extraction_refinement.md) | **Done** (PR #12) |
| Track | [track…](track_translation_unit_extraction_refinement.md) | **Done** (PR #13) |
| Loop | [loop…](loop_translation_unit_extraction_refinement.md) | **Done** |
| NoteEditFocus | [noteditfocus…](noteditfocus_translation_unit_extraction_refinement.md) | **Done** |

No further primary monolith TU extractions are queued. Remaining structural hygiene is opportunistic (P2/P3) or product-gated (persistence / HITL).

---

## Category index (detail lives in source plans)

| Category | Primary backlog |
|----------|-----------------|
| **Naming** | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § B; [naming_drift_scoped_edit_pass_handoff.md](naming_drift_scoped_edit_pass_handoff.md) Track 1 |
| **Structural / hygiene** | [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) |
| **Persistence / storage** | [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md); [set_revision_persistence_handoff.md](set_revision_persistence_handoff.md) |
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

When priority changes, adjust P1/P2/P3 only after CURRENT_WORK or user decision — this file does not override [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md).
