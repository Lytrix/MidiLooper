# Unified publish pipeline — terminology refinement (Commit over Publish)

**Kind:** architecture refinement  
**Date:** 2026-07-18  
**Parent:** [`unified_publish_pipeline_deferred_lazy_loading_architecture.md`](unified_publish_pipeline_deferred_lazy_loading_architecture.md)

Companions:

- [`unified_publish_pipeline_bootstrap_vs_deferred_executor_refinement.md`](unified_publish_pipeline_bootstrap_vs_deferred_executor_refinement.md)
- [`unified_publish_pipeline_final_review_refinement.md`](unified_publish_pipeline_final_review_refinement.md)
- [`unified_publish_pipeline_commit_as_verb_refinement.md`](unified_publish_pipeline_commit_as_verb_refinement.md)
- [`unified_publish_pipeline_review_resolutions_refinement.md`](unified_publish_pipeline_review_resolutions_refinement.md)

---

## Motivation

The project is not fundamentally about **publishing** (visibility). The stronger concept is **committing** immutable state: staging becomes the **canonical** version of the loop.

This matches existing vocabulary: `commitCapturePass()`, sealed work, immutable passes, committed runtime state.

---

## Why Commit fits

```text
Capture → Validate → Seal → Commit → Playback / Editor / Display
```

The important property is canonical ownership and atomic transition — not merely that other systems can *see* the data.

---

## Commit (the operation)

```text
Transient / Staging → Build → Validate → Commit → Committed state → Derived state
```

Staging is private until **Commit**. After Commit, state is immutable runtime truth. Consumers use only committed state.

Do **not** name this a “Commit Boundary” — see [`unified_publish_pipeline_commit_as_verb_refinement.md`](unified_publish_pipeline_commit_as_verb_refinement.md). Prefer: *All runtime-visible loop changes occur through Commit.*

---

## Terminology map (architecture / OpenSpec)

| Previous (docs) | Use instead |
|-----------------|-------------|
| Published Truth | **Committed state** / committed passes |
| Published State | **Committed state** |
| Publish Boundary / Commit Boundary | *(avoid noun)* — use verb **Commit** |
| Publish | **Commit** |
| Published immutable state | **Committed immutable state** |
| External `PUBLISHED` hydration | **`COMMITTED`** |

Filenames under `unified_publish_pipeline_*` are historical; content uses Commit.

---

## Rename pass (code + guides + OpenSpec)

**In scope for this project:** a dedicated **rename pass** so runtime vocabulary matches Commit — not permanent dual naming.

### Naming rule (mandatory)

Follow project **action + scope** (and object acted on) — [`.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc`](../../.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc) and global naming-and-terminology:

| Part | Role | Example |
|------|------|---------|
| **Action** | What it does | `commit`, `gather`, `mark`, `transfer`, `detach`, `reconcile`, `has` |
| **Scope / object** | What it applies to | `CapturePass`, `CommittedEvents`, `CommittedChunkIds`, `LoopLength` |

**Do not** invent names by only swapping the substring `Published` → `Committed` when the result has no clear action (e.g. a bare type rename is OK only when it remains a **noun for a domain object**: `CommittedEventRange`).  

Vocabulary already forbids **published** for new identifiers (use **committed**).

Phase 0 freezes each row as: **Action | Scope/object | Full identifier**.

### Goals

- One mental model: **commit** = staging → canonical immutable loop truth.  
- Align with `commitCapturePass` (action `commit` + scope `CapturePass`).  
- Update guides and active OpenSpec for the same concept.  
- Keep distinct senses of “commit” that are *not* this architectural Commit (e.g. `RevisionCommit`, mid-pass APIs).

### Do rename (committed-truth family)

**Status: FROZEN (2026-07-18)** — identifiers below are the Phase 1 rename targets. Do not invent bare `…ToCommitted` or new **Flat** names.

| Current | Action | Scope / object | Identifier (frozen) |
|---------|--------|----------------|---------------------|
| `hasPublishedEvents` | has | **committed passes** (on Loop) | `hasCommittedPasses` |
| `hasPublishedEventsInSlot` | has | **committed passes** in slot | `hasCommittedPassesInSlot` |
| `lastPublishedPassId` | — (field) | last **committed pass** id | `lastCommittedPassId` |
| `publishedChunkIds` | — (field) | chunk ids of committed passes | `committedChunkIds` |
| `PublishedChunkIdList` | — (type) | list of those chunk ids | `CommittedChunkIdList` |
| `PublishedChunkIdAllocator` | — (type) | allocator for that list | `CommittedChunkIdAllocator` |
| `transferCaptureStoreToPublished` | transfer | capture store → committed chunk ids | `transferCaptureStoreToCommittedChunkIds` |
| `transferCaptureChunkIdsToPublished` | transfer | capture chunk ids → committed chunk ids | `transferCaptureChunkIdsToCommittedChunkIds` |
| `detachChunksToPublished` | detach | chunks → committed chunk ids | `detachChunksToCommittedChunkIds` |
| `tryCopyPublishedChunkIds` | tryCopy | committed chunk ids | `tryCopyCommittedChunkIds` |
| `deepClonePublishedChunkIds` | deepClone | committed chunk ids | `deepCloneCommittedChunkIds` |
| `tryAssignPublishedChunkIds` | tryAssign | committed chunk ids | `tryAssignCommittedChunkIds` |
| `hasHeadroomForPublishedChunkIdList` | hasHeadroomFor | committed chunk id list | `hasHeadroomForCommittedChunkIdList` |
| `gatherPublishedEvents` | gather | **committed MIDI events** (from passes) | `gatherCommittedEvents` |
| `gatherPublishedFlatWithCapture` | gather | committed MIDI events + capture | `gatherCommittedEventsWithCapture` |
| `gatherPublishedEventsInWindow` | gather | committed MIDI events in window | `gatherCommittedEventsInWindow` |
| `gatherPublishedEventsInWindowWithCapture` | gather | committed MIDI events in window + capture | `gatherCommittedEventsInWindowWithCapture` |
| `PublishedEventRange` | — (type) | tick/range over committed MIDI events | `CommittedEventRange` |
| `markLoopPublishedChunksPersistedFromSdLoad` | mark … Persisted | loop committed-pass chunks from SD load | `markLoopCommittedChunksPersistedFromSdLoad` |
| `SlotLoadSessionState::Publishing` | — (session state) | session performing Commit | `Committing` |
| `tryClearPublishedMidiScratch` | tryClear | committed MIDI scratch | `tryClearCommittedMidiScratch` |
| `reconcileLoopLengthWithPublishedContent` | reconcile | loop length with **committed passes** content | `reconcileLoopLengthWithCommittedPasses` |
| `gatherPublishedFlatForDerivedView` | gather | committed MIDI events for derived view | `gatherCommittedEventsForDerivedView` |
| `probePublishedChunkIdBytes` | probe | committed chunk id bytes | `probeCommittedChunkIdBytes` |
| `markPublishedChunkIdsPersistedFromSdLoad` | mark … Persisted | committed chunk ids from SD load | `markCommittedChunkIdsPersistedFromSdLoad` |
| `findLastPublishedEventTick` | find | last committed MIDI event tick | `findLastCommittedEventTick` |
| `collectActivePublishedChunkLists` | collect | active committed chunk id lists | `collectActiveCommittedChunkLists` |
| `selectedSlotHasPublishedEvents` | — (param) | selected slot has committed passes | `selectedSlotHasCommittedPasses` |
| `PublishedEventRange.h` / `.cpp` | — (files) | committed event range module | `CommittedEventRange.h` / `.cpp` |
| `test_published_event_range/` | — (dir) | native test | `test_committed_event_range/` |

**Pinned:** **Pass** is the domain object (same family as **recordPass** / **overdubPass** / **editPass** / `commitCapturePass`). Use **Pass** / **committed loop state** in identifiers when the API is about pass presence, pass id, or pass-owned content. Prefer **committed passes** over “committed events” for pass-level APIs. Use **Events** only when the API gathers or ranges over **MIDI events** materialised from those passes. Avoid bare `…ToCommitted` — name the object (`…ToCommittedChunkIds`). Do not introduce new **Flat** identifiers.

Do **not** invent a new domain noun beyond **Pass** / committed MIDI events.

### Do **not** rename (different meanings)

| Keep | Why |
|------|-----|
| `commitCapturePass` / `CommitReason` | Already action+scope for capture-stop commit |
| Revision / set **commit** (`RevisionCommit`, revision jobs) | Persistence revision pipeline — different object |
| `admitSealedChunk` / mid_pass / seal journal | Seal ≠ Commit (different operation) |
| USB MIDI / external protocol “publish” | External |
| Historical plan filenames `unified_publish_pipeline_*` | Optional docs-only later |
| `openspec/changes/archive/` | Leave unless actively confusing |

### Execution order

1. **Phase 0** — Freeze rename table as **Action | Scope/object | Identifier** + exclusions; reject rows that fail action+scope review.  
2. **Rename pass** — identifiers + call sites + tests + guides/OpenSpec; `pio test -e native`.  
3. Feature work uses **only** new names (no new Publish/Published identifiers).

If too large for one PR: split by layer (LoopEventStore → Loop/Track → Display → Storage/SlotLoad → tests/docs) still **before** adding more publish symbols.

### Exit criteria

- [ ] Every new/changed public identifier has clear **action + scope/object**.  
- [ ] No new public APIs using Publish/Published for committed-truth.  
- [ ] Grep of `hasPublished` / `publishedChunk` / `gatherPublished` / `PublishedEvent` / `::Publishing` in `src/` `include/` `test/` empty or only documented exclusions.  
- [ ] `pio test -e native` PASS.  
- [ ] `LOOP_MIDI_STORAGE_AND_VALIDATION.md` + active OpenSpec use Commit for this boundary.  
- [ ] Optional: DECISION_LOG + Naming vocabulary already says avoid **published** — confirm code matches.

---

## Updated producer flow

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

---

## Updated design rules

1. Committed state is never modified in place.  
2. All runtime-visible loop changes occur through **Commit**.  
3. Playback only observes committed immutable state.  
4. Scheduling is separated from loading and build logic.  
5. Derived state may be discarded and rebuilt at any time.

---

## Immutable pass alignment

```text
Construct new pass → Validate → Commit → Runtime observes new committed version
```

Previous committed version remains valid until Commit completes (edition-style; no in-place edit of committed state).

---

## Recommendation

Architecture, OpenSpec, and the **Phase 1 rename pass** use **Commit** as the architectural verb. After the rename, prefer no remaining public Publish/Published identifiers for committed state. Prefer action+scope names aligned with `commitCapturePass`; do not invent Boundary / Gate / Pipeline nouns.
