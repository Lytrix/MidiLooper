# Architecture review — overdub pass overlap resolution

**Change:** `overdub-pass-overlap-resolution`  
**Date:** 2026-08-11  
**Status:** Active — load before each phase  

**Related:** [proposal.md](proposal.md), [design.md](design.md), [tasks.md](tasks.md), plan [wrap_duplicate_display_freeze_c7075cd6](../../../.cursor/plans/wrap_duplicate_display_freeze_c7075cd6.plan.md)

---

## Primary invariant (north star)

> A newly inserted overdub note is resolved incrementally against the immutable note geometry of one source pass. Overlap consequences belong to the new overdub pass. One overdub session may perform many such evaluations across multiple loop wraps while remaining one undoable overdub operation.

---

## Finding → phase map

| Severity | Finding | Phase | Owner |
|----------|---------|-------|-------|
| Critical | Append-order reverse-tick early-out invalid after wrap (`183525`) | 1 | Source-pass candidate lookup |
| Critical | Capture-only `isDuplicateCaptureEvent` is not full overlap model | 2 | `NoteGeometryResolver` decisions → overdubPass ops |
| High | CAP/WARN deny storm → RING overflow / display freeze | 1 | Throttle (observability) |
| High | Vague “all previous passes” search domain | 0–1 | Source-pass identity (D1) |
| Medium | Q16 capture min-length vs edit constrained geometry | 0 / 2 | Shared globals + docs |
| Low | Per-wrap undo desire | Out of scope | Future separate change |

---

## Evidence anchors

| Concern | Location |
|---------|----------|
| Duplicate deny / append | `Loop::appendCaptureEventWithResult`, `isDuplicateCaptureEvent` |
| Capture input | `Track::recordMidiEvents` (`TrackCaptureInput.cpp`) |
| Overlap authority | `NoteGeometryResolver`, `resolveConstrainedGeometry`, `applyEditSessionActions` |
| Committed window lookup | `CommittedEventRange` |
| Pass commit / undo | `Loop::commitCapturePass`, overdub undo kinds |
| Capture `183525` | `captures/session_20260811_183525.log` |

---

## Per-phase gates

### Phase 0 — OpenSpec docs (this propose)

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | Specs/design only — no firmware |
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Formal trigger? | **NO** for docs; Phase 2 re-check if apply owner moves |
| Behavior-preserving? | N/A (docs) |
| Reuse | YES — extend `NoteGeometryResolver` + `CommittedEventRange` (named in design) |
| Phase scope | `openspec/changes/overdub-pass-overlap-resolution/**`, runtime doc pointers |

| Implementation review | |
|-------------------------|--|
| proposal / design / specs / tasks | [x] when propose complete |
| ARCHITECTURE-REVIEW | [x] |
| CURRENT_WORK pointer | [x] |

---

### Phase 1 — Source-pass lookup + deny throttle (behavior-preserving toward display)

**Scope:** Freeze/name pre-session source view; wrap-safe chunk/window candidate lookup; throttle duplicate/deny WARN/CAP. Do **not** yet change accept/reject musical policy beyond fixing invalid early-out that skips candidates.

#### Architecture gate

| Question | Required |
|----------|----------|
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Formal trigger? | **NO** |
| Behavior-preserving? | **YES** for overlap policy; observability throttle OK |
| Reuse | YES — `CommittedEventRange` / materialize |
| Phase scope | `LoopInternalColdHelpers`, `TrackCaptureInput`, DebugSessionCapture throttle; tests |

#### Implementation review checklist

- [ ] Native: high-then-low append order finds source candidates
- [ ] Throttle: no multi-minute CAP gap on wrap stress
- [ ] `pio test -e native`
- [ ] No lastSeenTick semantic authority introduced

---

### Phase 2 — Wire canonical overlap into overdubPass ops

**Scope:** On insert, run constrained-geometry decisions; accumulate Shorten/Hide/Add on pending overdub pass; source immutable; session still one commit/undo.

#### Architecture gate

| Question | Required |
|----------|----------|
| Ownership change? | **NO** if decisions reuse `NoteGeometryResolver` and encode on existing pending overdub/capture commit path; **YES → STOP** if new Manager or dual writers of committed passes |
| State transition change? | **NO** — still evaluate during session, commit at stop; **YES → STOP** if wrap becomes pass boundary |
| Formal trigger? | Re-evaluate; PREFLIGHT if encode ownership unclear |
| Behavior-preserving? | **NO** vs today’s capture-only duplicate — intentional semantic alignment with NOTE_EDIT |
| Reuse | YES — `resolveConstrainedGeometry` / action semantics |
| Phase scope | Loop capture + geometry resolve bridge; native matrix |

#### Implementation review checklist

- [ ] Source immutability tests
- [ ] Same-phase wrap-2 re-eval tests
- [ ] Parity tests vs NOTE_EDIT decisions
- [ ] Min-length shared globals documented
- [ ] `pio test -e native`
- [ ] Persistence Non-Goals still hold (no resolve in save/load)

---

### Phase 3 — Retire capture-store dedup as authority + device verify

**Scope:** Remove or demote `isDuplicateCaptureEvent` as semantic authority; device wrap+bar41; close 183525 plan.

#### Architecture gate

| Question | Required |
|----------|----------|
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Behavior-preserving? | Relative to Phase 2 contract |
| Phase scope | Cleanup + HITL/manual capture |

#### Implementation review checklist

- [ ] Device: continuous DFRAME past wrap+bar41
- [ ] OLED updating (user)
- [ ] Stage5a3 note: 183525 denies were `duplicate`, not `pool_alloc`
- [ ] CURRENT_WORK / PROJECT_STATE updated

---

## Protected paths

Any edit under capture stop/commit, `commitCapturePass`, or persistence gating still requires OpenSpec-Phase-Gate + [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md). This change’s Phase 2 touches capture append / pending overdub encoding — treat as protected.

## Forbidden without approved phase

- New top-level overlap Manager
- Per-wrap undo
- `lastSeenTick` as semantic authority
- Overlap resolve inside `StorageManager` / deferred save
- Checking off a phase without Implementation review above
