# Unified publish pipeline — Commit as verb (no new architectural noun)

**Kind:** architecture refinement  
**Date:** 2026-07-18  
**Parent:** [`unified_publish_pipeline_deferred_lazy_loading_architecture.md`](unified_publish_pipeline_deferred_lazy_loading_architecture.md)

Companions:

- [`unified_publish_pipeline_bootstrap_vs_deferred_executor_refinement.md`](unified_publish_pipeline_bootstrap_vs_deferred_executor_refinement.md)
- [`unified_publish_pipeline_final_review_refinement.md`](unified_publish_pipeline_final_review_refinement.md)
- [`unified_publish_pipeline_commit_terminology_refinement.md`](unified_publish_pipeline_commit_terminology_refinement.md)

---

## Motivation

“**Commit Boundary**” (and similar nouns) are not used elsewhere in the firmware vocabulary. The project describes transitions with **actions**: capture, seal, commit, request, process, finish, attach, detach.

Keep **Commit** as the central architectural **verb**. Do not invent a separate noun for where it happens.

---

## Prefer the operation

Not:

```text
Commit Boundary
```

But:

```text
Commit
```

Architecture cares about **when runtime-visible state changes**, not naming an abstract boundary object.

---

## Model

```text
Producer → Build immutable state → Commit → Committed state → Derived state
```

Producers (record, overdub, edit, load, import, paste, undo restore) differ; the invariant does not:

> **All runtime-visible loop changes occur through Commit.**

---

## Same semantics, not one helper

Aligns with existing `commitCapturePass()`, committed passes, staged/sealed work.

Future producers may use different commit functions. The architecture does **not** require every producer to call the same helper — only the same **commit semantics** (staging invisible until Commit; then immutable committed state).

---

## Design vocabulary (use these)

| Term | Meaning |
|------|---------|
| **Commit** | Atomic operation that makes newly constructed immutable state runtime-visible |
| **Committed state** | Immutable runtime state observed by playback, editor, display |
| **Derived state** | Anything reconstructed from committed state |

## Avoid inventing

- Commit Boundary  
- Commit Pipeline  
- Commit Gate  
- Commit Stage  

(unless a future design proves a separate concept is required)

Session enum `Committing` remains a **state name** parallel to `Reading` / `Validating` — not a new top-level architectural noun.

---

## Recommendation

Docs and OpenSpec: action-oriented flow

```text
Capture → Seal → Commit → Playback / Editor / Display
```

---

## Applied

Folded into parent + companions. Superseded pin details (forced computed hydration / mandatory session before audible boot) replaced by [review resolutions](unified_publish_pipeline_review_resolutions_refinement.md).