# Architecture review — runtime derived representations

**Change:** `runtime-derived-representation-heap` — **M6 follow-up + M7**
**Updated:** 2026-08-11
**Status:** Active — M6 display follow-up RC1; M7 historical gates retained below
**Plans:** [`long_overdub_display_freeze_bugfix.md`](../../../docs/Plans/long_overdub_display_freeze_bugfix.md), [`long_overdub_capture_preview_tail_parity_bugfix.md`](../../../docs/Plans/long_overdub_capture_preview_tail_parity_bugfix.md), [`published_pass_capture_builder_split_refinement.md`](../../../docs/Plans/published_pass_capture_builder_split_refinement.md)

**Related:** [design.md](design.md) § M7, [tasks.md](tasks.md) § M7, [specs/published-capture-pass-split/spec.md](specs/published-capture-pass-split/spec.md)

**Orthogonal:** Capture **commit** owner ([DEC-023](../unified-capture-commit-owner/ARCHITECTURE-REVIEW.md)); persistence scheduling ([DEC-020](../continuous-runtime-persistence/ARCHITECTURE-REVIEW.md)). M7 does **not** change seal/publish FSM transitions — only **representation** at existing boundaries.

**Supersedes:** Phase 2A blanket/split-tier typedef swaps (reverted — see [`memory_pressure_reclaim_conclusions_refinement.md`](../../../docs/Plans/memory_pressure_reclaim_conclusions_refinement.md)).

---

## M6 Phase 2 follow-up — long capture display starvation (2026-08-11)

**Plan:** [`docs/Plans/long_overdub_display_freeze_bugfix.md`](../../../docs/Plans/long_overdub_display_freeze_bugfix.md)
**Evidence:** [`session_20260811_004608.log`](../../../captures/session_20260811_004608.log) — 259-second `DFRAME` gap across RECORDING and post-stop PLAYING.

The prior M6 Phase 2 revision gate still copied the complete `capturePreview.notes` vector when continuous MIDI changed the revision each display frame, and the tail path copied/scanned the complete capture store whenever a preview note remained open. This is an incomplete DEC-016 migration, not a new display or capture architecture.

### Architecture gate

| Question | Answer |
|----------|--------|
| Owner module | `Loop` (`CapturePreview`) + `DisplayManager` (composition / draw) |
| Primary invariant | Capture arrival changes only the capture suffix; committed display data remains reusable; per-frame display work is independent of capture-session length |
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Behavior-preserving? | **YES** — same note, wrap-tail, and wrap-head display result |
| Reuse | **YES** — extend `CapturePreview`, committed/capture region partition, and PLAYING stale-while-revalidate |
| Phase scope | Plan Stages 1 / 1a / 1b only; no capture-stop, playback-wrap, or persistence FSM changes |

### Stage 0a decisions

1. Delta-sync exact changed capture rows into the existing partitioned `liveDisplayNotes`; do not introduce a dual-vector paint API.
2. Maintain channel-aware open/wrap metadata on `CapturePreview` during append; remove full capture-store copy/scan from the per-frame overdub tail path.
3. Preserve the last valid live-capture frame across record stop and replace it through the bounded committed-window resolver; no synchronous representation rebuild on the stop path.

### Implementation review

| Check | Pass |
|-------|------|
| Duplicate `captureDisplayRevision` bumps removed from `TrackCaptureInput` | [x] |
| Valid committed prefix survives capture-only revisions | [x] |
| Nominal capture revision copies only new/changed preview rows | [x] |
| Tail/head parity fixtures cover channel and wrap pairing | [x] |
| No per-frame `copySortedCaptureEvents` for overdub tails | [x] |
| Post-record PLAYING paints within 1 second | [ ] |
| `pio test -e native` | [x] |
| `teensy41-capture-serial` build | [x] |
| 118-bar manual record/overdub gate | [ ] |

**Approval:** APPROVE — Stage 1 firmware may proceed within this gate.

### RC1 architecture gate — capture preview open-note identity

| Question | Answer |
|----------|--------|
| Owner module | `Loop` through `CapturePreview` and `applyCaptureEventToPreview` |
| Primary invariant | Only sidecar rows with `open == true` may be closed by normal NoteOff |
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Behavior-preserving? | **YES** — pitch-LIFO normal pairing and channel-aware wrap pairing remain |
| Reuse | **YES** — existing `CapturePreviewNoteState`, `openNoteIndices`, and cold rebuild |
| Phase scope | RC1 only; no DisplayManager handoff, USB Host input, stop/commit, or persistence edits |

#### RC1 implementation review

| Check | Pass |
|-------|------|
| Closed zero-duration row cannot be selected as open | [ ] |
| Sidecar/vector alignment guarded | [ ] |
| Cold rebuild restores open/wrap metadata | [ ] |
| Focused parity fixtures | [ ] |
| `pio test -e native` | [ ] |

---

## Governing invariants (M7)

1. **Capture is mutable and latency-sensitive** — internal heap; zero domain transitions on append.  
2. **Published passes are immutable and storage-oriented** — external memory pool metadata.  
3. **Type system enforces the split** — `CaptureChunkIdList` ≠ `PublishedChunkIdList`.  
4. **One domain transition at seal** — publish moves within published domain only.  
5. **No published → capture repatriation** — except empty new capture session.  
6. **Primary success = ownership** — RAM gain is secondary; exit criterion ~5 KiB under 215312.

---

## Evidence anchors (code)

| Concern | Primary location |
|---------|------------------|
| Capture append (hot) | `Loop::appendCaptureEvent`, `LoopEventStore::append` |
| Seal / publish | `Loop::sealCapture`, `commitPendingCapturePass`, `commitCapturePass` |
| Pending staging | `PendingCapturePass`, `hasPendingCapturePass`, `discardPendingCapturePass` |
| Published passes | `LoopPasses::recordPass`, `overdubPasses`, `LoopPasses::materialize` |
| Transfer (sole domain cross) | `transferCaptureChunkIdsToPublished` (Phase 1) |
| SD I/O | `StorageLoopIo`, `readPersistedLoopSnapshot` |
| Clone / undo | `deepClonePasses`, `sharePassesSnapshot`, `setCapturePassState` |

---

## Per-phase gates

### Phase 0 — OpenSpec + plan (doc-only) — **COMPLETE 2026-07-15**

| Architecture gate | |
|-------------------|---|
| **Owner module** | `Loop` (seal/publish), `LoopEventStore` (transfer helper), `LoopPasses` (storage) |
| **Primary invariant** | CaptureBuilder internal; published chunk ids external; one transition at seal |
| **Ownership change?** | **NO** — representation split at existing boundaries |
| **State transition change?** | **NO** — same seal → pending → publish → clear builder |
| **Behavior-preserving?** | **YES** |
| **Reuse decision** | **YES** — extend `sealCapture` / `commitPendingCapturePass` |

| Implementation review | |
|-----------------------|---|
| Spec delta | [x] `specs/published-capture-pass-split/spec.md` |
| Plan doc | [x] `published_pass_capture_builder_split_refinement.md` |
| Firmware edits | [ ] none (Phase 0) |

**Approval:** APPROVE — proceed to Phase 1 firmware

---

### Phase 1 — Type split + seal transfer — **NEXT**

**Scope:** `CaptureChunkIdList`, `PublishedChunkIdList`, transfer helper, pending at seal, publish move; **not** `CommittedOverdubPassVec` yet.

#### Architecture gate

| Question | Answer |
|----------|--------|
| Owner module | `Loop`, `LoopEventStore` |
| Primary invariant | One alloc + one linear copy on seal; zero transitions on append |
| Ownership change? | **NO** |
| Transition change? | **NO** |
| Behavior-preserving? | **YES** |
| Reuse | **YES** — extend `sealCapture`, `commitPendingCapturePass` |
| Phase scope | `LoopEventStore.h/cpp`, `LoopPasses.h`, `Loop.cpp` + native tests |

#### Implementation review (before checkoff)

| Check | Pass |
|-------|------|
| `pio test -e native` (`test_loop_event_store`, `test_loop_take_survival`) | [ ] |
| Append path never references `PublishedChunkIdList` | [ ] |
| Transfer helper: ≤1 alloc, ≤1 copy, no temp vectors | [ ] |
| No `abort()` on seal extmem failure | [ ] |

---

### Phase 2 — `CommittedOverdubPassVec` + call sites

#### Architecture gate

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| Transition change? | **NO** |
| Behavior-preserving? | **YES** |
| Phase scope | `LoopPasses.h`, `LoopPasses.cpp`, persistence/reclaim call sites |

#### Implementation review

| Check | Pass |
|-------|------|
| `pio test -e native` full suite | [ ] |
| `teensy41-capture-serial` build | [ ] |

---

### Phase 3 — SD load / snapshot / undo alignment

#### Architecture gate

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| Forbidden pattern | Published → capture repatriation on load/clone | 

#### Implementation review

| Check | Pass |
|-------|------|
| `test_storage_loop_io`, `test_sd_load_adopt`, undo/snapshot tests | [ ] |

---

### Phase 4 — HITL + exit measurement

#### Architecture gate

| Question | Answer |
|----------|--------|
| Primary pass | Ownership invariants + 0 append failures + stop completes |
| Secondary pass | Heap at overdub publish vs 215312 baseline |
| Exit criterion | < ~5 KiB recovery + no pressure improvement → scope down further migration |

#### Implementation review

| Check | Pass |
|-------|------|
| HITL baseline preset | [ ] |
| 215312-profile manual capture | [ ] |
| `LoopPersist,done,ok` + persistence restore | [ ] |
| NOTE_EDIT smoke (no sluggishness) | [ ] |
| Exit measurement documented in session notes | [ ] |

---

## Relationship to reverted Phase 2A

| Phase 2A (reverted) | M7 (this review) |
|-------------------|------------------|
| Single `ChunkIdList` → extmem | Dual typedefs; capture stays internal |
| Hot path touched | Hot path isolated |
| `abort()` on dual exhaustion | Graceful seal failure |
| Primary goal: RAM | Primary goal: **ownership** |
