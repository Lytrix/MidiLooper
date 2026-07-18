# Unified publish pipeline — Bootstrap vs deferred scheduling

**Kind:** architecture refinement  
**Date:** 2026-07-18  
**Parent:** [`unified_publish_pipeline_deferred_lazy_loading_architecture.md`](unified_publish_pipeline_deferred_lazy_loading_architecture.md)  
**Also see:** [`unified_publish_pipeline_final_review_refinement.md`](unified_publish_pipeline_final_review_refinement.md), [`unified_publish_pipeline_commit_terminology_refinement.md`](unified_publish_pipeline_commit_terminology_refinement.md), [`unified_publish_pipeline_commit_as_verb_refinement.md`](unified_publish_pipeline_commit_as_verb_refinement.md), [`unified_publish_pipeline_review_resolutions_refinement.md`](unified_publish_pipeline_review_resolutions_refinement.md)

**Phase map:** use **parent** phase numbers only (this doc does not define an independent schedule). Relevant parent phases: **2** audible boot, **3** cooperative session, **4** deferred runtime.

---

## Motivation

> Should the initial audible boot load also use the deferred load scheduler?

No — boot has no playback/MIDI/display deadlines. Sync audible restore is enough. Runtime needs budgets.

**Decision:** Two **scheduling styles** (when): boot sync vs runtime deferred process. Architecture center remains **Commit** (not Load, not Publish-as-visibility).

Target long-term: one **`SlotLoadSession`** implementation (what). Per [review resolutions](unified_publish_pipeline_review_resolutions_refinement.md) §4, MVP audible boot may still use the **existing** sync `loadLoopSlotFromCurrentSetSd` before cooperative session work lands.

---

## Design principle

When `SlotLoadSession` owns load progress (state, cursors, staging, validate, **commit**, complete): **no scheduling policy inside the session.**

```text
Boot sync restore ──► load path / SlotLoadSession  ◄── processDeferredLoopSlotRestore (budgeted)
                              │
                              ▼
                           Commit
```

Do not land `*Executor` type names until Phase 0 naming review against PersistenceQueue, `processDeferred*`, and `commitCapturePass`.

---

## Why not deferred during boot?

| Boot | Runtime |
|------|---------|
| No playback / MIDI / record / display deadlines | All compete for SD and CPU |
| Make playable ASAP | Never monopolize `loop()` |
| Sync audible restore → commit | Budgeted session advance → commit |

---

## Shared implementation (target)

Read / validate / attach / commit / complete logic converges on `SlotLoadSession`.  
Boot and runtime only decide **how often** to advance it. Convergence is incremental — not an MVP gate for audible-only boot.

---

## Delivery (parent phases)

| Parent phase | Scope |
|--------------|--------|
| **2** | Boot sync audible set → **commit** → interactive (existing sync load OK) |
| **3** | `SlotLoadSession` cooperative advance; factor load body as ready |
| **4** | Budgeted `processDeferredLoopSlotRestore` for explicitly requested slots |

---

## Success criteria

- Startup = sync audible **commit** only (not full-set drain).  
- Runtime loads = deferred process for requested slots (when Phase 4 lands).  
- Scheduling ≠ loading logic.  
- Vocabulary: **Commit** as verb; identifiers aligned with `commitCapturePass` / `processDeferred*`.  
