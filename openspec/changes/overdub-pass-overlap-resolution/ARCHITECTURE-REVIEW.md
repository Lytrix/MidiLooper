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

**Scope:** Freeze/name pre-session source view; wrap-safe candidate lookup **tested but not wired into deny**; throttle duplicate/deny WARN/CAP; optionally fix capture-store reverse-tick early-out only. Do **not** invent Shorten/Hide or change source-pass musical policy.

**Architecture check (2026-08-11) — must not miss:**

| Pin | Finding |
|-----|---------|
| No freeze today | `beginOverdubSession` does not snapshot geometry |
| Canonical source | `materializeToEventVector` (includes `editPasses`); bare `CommittedEventRange` insufficient when edits active |
| Lookup ≠ note index | `CommittedEventRange` = event windows; spans reconstructed separately |
| Phase 1 deny wiring | Source lookup **not** in accept/reject until Phase 2 (design D6) |
| Early-out fix | Narrow: capture-store exact-tick duplicates after wrap only |

#### Architecture gate

| Question | Required |
|----------|----------|
| Ownership change? | **NO** — extend `Loop`/`Track` freeze + lookup helpers |
| State transition change? | **NO** |
| Formal trigger? | **NO** |
| Behavior-preserving? | **YES** for source-overlap policy; throttle OK; early-out fix is narrow capture-store only |
| Reuse | YES — `materialize*` / `gatherCommittedEvents*` (editPass-aware); not chunk-only CER |
| Phase scope | Overdub start freeze, lookup helper + tests, `TrackCaptureInput` / CAP throttle; optional early-out |

#### Implementation review checklist

- [ ] Freeze API named and called at overdub start
- [ ] Native: source lookup finds candidates with high-then-low capture order; editPass-aware path covered
- [ ] Source lookup **not** changing append accept/reject yet (unless early-out-only)
- [ ] Throttle: no multi-minute CAP gap on wrap stress
- [ ] `pio test -e native`
- [ ] No lastSeenTick semantic authority introduced

---

### Phase 2 — Wire canonical overlap into overdubPass ops

**Scope:** On insert, run constrained-geometry decisions; accumulate Shorten/Hide/Add on pending overdub pass; source immutable; session still one commit/undo.

**Must pin before coding (architecture check):** encode target — `OverdubPass` is chunk-IDs-only today; choose A/B/C in design Open Q4; use free `resolveConstrainedGeometry` (not session-gated `NoteGeometryResolver::resolve`); coexistence with `shouldRestoreCommittedOverlapOnOverdubStop`; DEC-020 mid-pass stays raw capture bytes.

#### Architecture gate

| Question | Required |
|----------|----------|
| Ownership change? | **NO** if decisions reuse constrain/build helpers and encode on existing `Loop` capture/commit path; **YES → STOP** if new Manager or dual writers of committed passes |
| State transition change? | **NO** — still evaluate during session, commit at stop; **YES → STOP** if wrap becomes pass boundary |
| Formal trigger? | **YES → PREFLIGHT** if encode adds persistent pass model / undo kind / schema; else re-evaluate |
| Behavior-preserving? | **NO** vs today’s capture-only duplicate — intentional semantic alignment with NOTE_EDIT |
| Reuse | YES — `resolveConstrainedGeometry` / `buildEditSessionActions` (not fake NOTE_EDIT session) |
| Phase scope | Loop capture + geometry resolve bridge + encode; native matrix |

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
