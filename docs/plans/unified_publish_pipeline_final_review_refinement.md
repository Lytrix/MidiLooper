# Unified publish pipeline — final review refinements

**Kind:** architecture refinement  
**Date:** 2026-07-18  
**Parent:** [`unified_publish_pipeline_deferred_lazy_loading_architecture.md`](unified_publish_pipeline_deferred_lazy_loading_architecture.md)

Companions:

- [`unified_publish_pipeline_bootstrap_vs_deferred_executor_refinement.md`](unified_publish_pipeline_bootstrap_vs_deferred_executor_refinement.md)
- [`unified_publish_pipeline_commit_terminology_refinement.md`](unified_publish_pipeline_commit_terminology_refinement.md) — **Commit** over Publish  
- [`unified_publish_pipeline_commit_as_verb_refinement.md`](unified_publish_pipeline_commit_as_verb_refinement.md) — Commit as verb; no Boundary noun  
- [`unified_publish_pipeline_review_resolutions_refinement.md`](unified_publish_pipeline_review_resolutions_refinement.md) — review resolutions before OpenSpec  

---

## 1. Commit is the architectural center (verb)

(Supersedes earlier “Publish is the center” wording.)

This project is a **commit** architecture. Loading is one producer that **Commits** immutable state.

```text
Record / Import / Undo / Load / Paste
              │
              ▼
   Build immutable committed state
              │
              ▼
            Commit
              │
              ▼
   Playback / Editor / Display
```

Docs and OpenSpec SHOULD lead with the verb **Commit** and **committed state**. Avoid “Commit Boundary.” Slot load sessions and schedulers are how Load (and later Import) *perform* Commit. Same **semantics** across producers — not necessarily one shared helper.

---

## 2. Design rules (invariants)

| # | Rule |
|---|------|
| 1 | Committed state is never modified in place. |
| 2 | All runtime-visible loop changes occur through **Commit**. |
| 3 | Playback only observes committed immutable state. |
| 4 | Scheduling is separated from loading and build logic. |
| 5 | Derived state may be discarded and rebuilt at any time. |

---

## 3. Derived is intentionally broad

**Derived** = anything that can be reconstructed from **committed** truth.

Examples (non-exhaustive):

- playback merge windows  
- editor state / session stores built from passes  
- visual caches / piano-roll geometry  
- note lookup tables  
- display geometry  
- future helper structures  

---

## 4. External vs internal load stages

**External** (UI / transport / ready policy):

```text
UNLOADED → HEADER_READY → COMMITTED → DERIVED_READY
```

**Internal** to `SlotLoadSession` only: file/chunk cursors, staging, validation.

**Phase 0 pins:** see parent + [review resolutions](unified_publish_pipeline_review_resolutions_refinement.md) — COMMITTED sufficient for UI; hydration storage flexible; existing sync boot load OK for MVP; priorities = audible + requested only.

---

## 5. Naming — PersistenceQueue / processDeferred*

Working labels BootstrapExecutor / DeferredExecutor were strategy sketches — **not** frozen.

Mirror:

| Existing | Role |
|----------|------|
| `PersistenceQueue` | `admitSealedChunk`, `beginWriteQueuedChunk`, `markChunkPersisted` |
| `processDeferredSaveState` / `stepPersistenceWorkItem` | Budgeted save orchestration |
| `processDeferredLoopSlotRestore` | Runtime slot-load process entry |
| `commitCapturePass` | Record-side **commit** vocabulary to align with |

Boot: `restore…` / `process…` / `finish…` on `StorageManager`.  
Runtime: extend `processDeferredLoopSlotRestore`.  
Prefer **commit** over new publish nouns in architecture and new identifiers.  
**Phase 1** of the parent plan is the mechanical **rename pass** (see [commit terminology](unified_publish_pipeline_commit_terminology_refinement.md#rename-pass-code--guides--openspec)).

---

## Success criteria (refinement)

- Parent doc leads with **Commit** (verb), not Load or Publish.  
- Design rules use Commit / committed state (no Boundary noun).  
- External lifecycle uses **COMMITTED**.  
- Rename pass planned before feature work that adds more publish symbols.  
- No `*Executor` types without PersistenceQueue / `processDeferred*` / commit vocabulary alignment.
