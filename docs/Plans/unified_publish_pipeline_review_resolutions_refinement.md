# Unified publish pipeline — review resolutions

**Status:** Frozen (2026-07-18) — OpenSpec `unified-commit-lazy-slot-load`  
**Date:** 2026-07-18  
**Parent:** [`unified_publish_pipeline_deferred_lazy_loading_architecture.md`](unified_publish_pipeline_deferred_lazy_loading_architecture.md)

Companions:

- [`unified_publish_pipeline_bootstrap_vs_deferred_executor_refinement.md`](unified_publish_pipeline_bootstrap_vs_deferred_executor_refinement.md)
- [`unified_publish_pipeline_final_review_refinement.md`](unified_publish_pipeline_final_review_refinement.md)
- [`unified_publish_pipeline_commit_terminology_refinement.md`](unified_publish_pipeline_commit_terminology_refinement.md)
- [`unified_publish_pipeline_commit_as_verb_refinement.md`](unified_publish_pipeline_commit_as_verb_refinement.md)

---

## Purpose

Architectural decisions taken during review before creating the OpenSpec.

Eliminate ambiguity while intentionally leaving implementation details open where they do not affect the architectural contract.

---

## High priority

### 1. Commit semantics vs `commitCapturePass()`

Standardize **Commit semantics**, **not** a specific helper.

Every producer (record, overdub, edit, load, import, paste, undo restore) performs:

```text
Build immutable state → Commit → Committed state
```

The architecture **does not require** all producers to call `commitCapturePass()`. Implementations may share a helper, converge later, or keep producer-specific commit functions — provided commit invariants hold.

### 2. COMMITTED vs DERIVED_READY

| State | Responsibility |
|-------|----------------|
| **COMMITTED** | Playback may use the slot; editor may access committed passes; display may begin rendering. Piano roll may build from committed passes if derived structures are missing. Derived is **not** required before first interaction. |
| **DERIVED_READY** | Optional reconstructed structures (merge caches, lookup tables, piano-roll caches, visual geometry). Improves performance; **not** required for correctness. |

### 3. Hydration state ownership

Lifecycle is an **architectural concept**, not a mandated stored structure:

```text
UNLOADED → HEADER_READY → COMMITTED → DERIVED_READY
```

Implementation may use explicit state, derived state, flags, or computed properties. Architecture does **not** mandate a dedicated enum or field.

### 4. Slot loading order

Architecture does **not** require the cooperative loading implementation before the new loading model.

MVP:

- Synchronous audible boot may continue using the **existing** load implementation  
- Runtime deferred loading can be introduced afterwards  

Architectural concepts are independent of implementation order (minimizes migration risk).

---

## Medium priority

### Terminology

Prefer **committed passes** / **committed loop state** over “committed events” for pass-level APIs (immutable pass model). Keep **Events** only for MIDI gather/range helpers.

### Transfer naming

Avoid bare `…ToCommitted` without naming what becomes committed. Prefer explicit scope, e.g. `…ToCommittedChunkIds` (action + scope).

### “Flat” terminology

Do not introduce new identifiers containing **Flat**. Legacy names may remain temporarily. New APIs/docs: committed passes, merged events, derived playback structures.

### Phase numbering

Refinement docs must **not** invent independent phase numbers. Reference **parent phases / parent tasks** only.

### Historical filenames

Filenames may keep `publish` / `executor` until architecture stabilizes. Rename is doc cleanup, not functional work.

---

## Runtime behaviour

### Background hydration priorities

Only two priorities are architecturally required:

1. **Audible slots**  
2. **Explicitly requested slot**

Speculative adjacent / prefetch is deferred until a demonstrated need. No premature Priority-3 fill policy.

### Loading while transport is active

**MVP:** not required. Select unloaded while PLAYING may queue and defer SD until transport idle. Interactive mid-play load = later parent phase.

### `hasCommittedPasses`

Applies to all committed loop content: record, overdub, edit, and future committed pass types — broader than capture-only.

### OpenSpec timing

Architecture documents are **Frozen** (2026-07-18). OpenSpec change: `unified-commit-lazy-slot-load`. The OpenSpec describes the agreed model; it does not evolve it.

---

## Low priority

### Rename table

Terminology table is stable. Remaining open items are isolated (legacy Flat, transfer naming), not broader vocabulary.

### Success metrics

Initially **qualitative**: audible startup faster than full-set load; playback before full hydration; derived rebuild off critical startup path. Quantitative targets only after profiling representative projects.

---

## Final architectural position

- **Commit** replaces Publish as the architectural operation.  
- Loading is one producer of committed state, not a special subsystem.  
- Every runtime-visible loop change occurs through **Commit**.  
- COMMITTED is sufficient for playback and initial editor/display.  
- DERIVED_READY is optional optimization, not correctness.  
- Hydration lifecycle is an architectural model, not necessarily a stored enum.  
- Synchronous audible boot with the existing load path remains acceptable for MVP.  
- Runtime deferred loading is incremental after the model is established.  
- Architecture specifies **behavioral contracts**; helpers, state storage, and scheduling internals stay flexible.  
