# Architecture review — overdub pass overlap resolution

**Change:** `overdub-pass-overlap-resolution`  
**Date:** 2026-08-11  
**Status:** Active — load before each phase  

**Related:** [proposal.md](proposal.md), [design.md](design.md), [tasks.md](tasks.md), plan [wrap_duplicate_display_freeze_c7075cd6](../../../.cursor/plans/wrap_duplicate_display_freeze_c7075cd6.plan.md)

---

## Primary invariant (north star)

> Each overdub session has a stable, materialize-aware `overdubSourceView` established at start. Newly inserted notes are resolved against that view (including across wraps). The source is never destructively modified. The resulting `overdubPass` records the complete delta (additions plus source shorten/remove). The session remains one overdub pass/undo under the current undo model.

---

## Finding → phase map

| Severity | Finding | Phase | Owner |
|----------|---------|-------|-------|
| Critical | No stable session source view today | 1 | `overdubSourceView` on Loop; Track lifecycle |
| Critical | Capture-only `isDuplicateCaptureEvent` is not full overlap model | 2 | Constrain/build → overdubPass **complete delta** |
| Critical | Append-order reverse-tick early-out invalid after wrap (`183525`) | 2–3 | Lookup into `overdubSourceView` (not Phase 1) |
| High | CAP/WARN deny storm → RING overflow | Separate / 3 | Throttle (observability only) |
| High | Vague “all previous passes” / “freeze” language | 0 | `overdubSourceView` naming (D1) |
| Medium | `OverdubPass` chunk-IDs-only vs delta encode | 2 | Open Q4 + PREFLIGHT |
| Medium | Q16 capture min-length vs edit constrained geometry | 2 | Shared globals + docs |
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

### Phase 1 — `overdubSourceView` + native tests only

**Scope:** Establish `overdubSourceView` at overdub start (`Loop` provides; `Track` lifecycle triggers); materialize-aware; stable across wraps; native tests for geometry exposure, candidate lookup into the view, source immutability. **Do not** wire into append accept/reject. **Do not** fix reverse-tick early-out, full resolve/encode, or deny throttle in this phase (throttle may be a separate interim commit).

**Refinement pins (2026-08-11):**

| Pin | Decision |
|-----|----------|
| Naming | `overdubSourceView` — not freeze / FrozenPass |
| When | Overdub start |
| Ownership | Loop canonical view; Track session lifecycle |
| Representation | Semantic contract only; events/spans/chunk-window OK |
| Deny path | Not wired in Phase 1 |

#### Architecture gate

| Question | Required |
|----------|----------|
| Ownership change? | **NO** — extend `Loop`/`Track` |
| State transition change? | **NO** |
| Formal trigger? | **NO** |
| Behavior-preserving? | **YES** — view + tests; no musical policy change |
| Reuse | YES — `materialize*` / editPass-aware gather |
| Phase scope | Establish/clear view + native tests only |

#### Implementation review checklist

- [x] `overdubSourceView` established at overdub start; cleared at session end
- [x] Materialize-aware (editPasses covered)
- [x] Native: stable across wraps; immutability; candidate lookup into view
- [x] Append accept/reject unchanged this phase
- [x] No `freeze*` / Frozen* identifiers; no lastSeenTick authority
- [x] `pio test -e native`

---

### Phase 2 — G2 approved (unified resolution, transitional dual storage)

**Scope:** Shared geometry → pending logical Add/Shorten/Hide; at stop encode via existing capture chunks + edit rows; one logical undo.  
**Pin:** DEC-032 **G2**. Dual seal is **encoding only** (DEC-031 mechanics). U1/U2 out of scope.  
**Review:** [`UNIFIED-PASS-ARCHITECTURE-REVIEW.md`](UNIFIED-PASS-ARCHITECTURE-REVIEW.md). Design §19.

#### Architecture gate

| Question | Required |
|----------|----------|
| Ownership change? | **NO** — Loop session buffer + existing `saveNoteEditPass` / `TrackUndo` |
| State transition change? | **NO** — evaluate during session, commit at stop; wrap ≠ pass |
| Formal trigger? | **YES — PREFLIGHT done** ([PREFLIGHT.md](PREFLIGHT.md)): undo apply/reclaim/GUS for companion ids; no new undo kind; no OverdubPass schema |
| Behavior-preserving? | **NO** vs today’s capture-only duplicate — intentional NOTE_EDIT geometry alignment |
| Reuse | YES — `resolveConstrainedGeometry` → EditPass-shaped pending ops; seal via `saveNoteEditPass` |
| Phase scope | Slice 1: pending buffer + native bridge. Slice 2: seal + undo + restore gate |

#### Implementation review checklist

- [x] Source immutability tests (`test_pending_note_change`, `test_overdub_source_view`)
- [x] Same-phase wrap-2 re-eval / append accept (`test_overdub_source_view_skips_capture_duplicate_deny`)
- [x] Parity via shared `resolveConstrainedGeometry` path (pending matrix)
- [x] Min-length shared globals documented (`LOOP_MIDI_STORAGE_AND_VALIDATION.md` + pending call site)
- [x] `pio test -e native` (1016/1016 after 3.7)
- [x] Persistence Non-Goals still hold (no resolve in save/load)

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
