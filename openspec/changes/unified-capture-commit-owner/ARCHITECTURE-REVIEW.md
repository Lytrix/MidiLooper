# Architecture review — unified capture commit owner

**Change:** `unified-capture-commit-owner` (DEC-023)  
**Date:** 2026-07-08  
**Status:** Active — use this file before and after **each phase** ships  

**Related:** [proposal.md](proposal.md), [design.md](design.md), [tasks.md](tasks.md), [docs/plans/unified_capture_stop_driver_refinement.md](../../../docs/plans/unified_capture_stop_driver_refinement.md)

This change restores the **per-change review artifact** pattern used in archived OpenSpec work (e.g. [`loop-ownership-hardening`](../../archive/2026-06-22-loop-ownership-hardening/ARCHITECTURE-REVIEW.md), [`overlap-hidden-note-select`](../../archive/2026-06-19-overlap-hidden-note-select/architecture-review.md)). Global templates remain in [`docs/templates/`](../../../docs/templates/) and [`docs/agents/reviewer.md`](../../../docs/agents/reviewer.md).

---

## Repo review stack (still valid)

| Layer | Artifact | When |
|-------|----------|------|
| **Architecture (formal trigger)** | [PREFLIGHT.md](../../../docs/templates/PREFLIGHT.md) full + [ARCHITECTURE_REASSESSMENT.md](../../../docs/ARCHITECTURE_REASSESSMENT.md) | Ownership / transition change (Phase 3+) |
| **Architecture (extension)** | [architecture-checkpoint-bugfix](../../../.cursor/rules/architecture-checkpoint-bugfix.mdc) — ownership? transitions? | Every phase — answer in phase gate below |
| **Historical decisions** | [DECISION_REVIEW.md](../../../docs/templates/DECISION_REVIEW.md) | Before firmware; mandatory if formal trigger |
| **Implementation** | [reviewer.md](../../../docs/agents/reviewer.md) output format | End of each phase before merge |
| **Session** | [SESSION_CLOSEOUT.md](../../../docs/templates/SESSION_CLOSEOUT.md) | Design exclusions; DEC append |

**What got lost in recent OpenSpec execution:** per-change **finding → task → verification matrix** linked from `tasks.md`. Phases shipped with gates in `tasks.md` only, without a standing review doc agents load each session.

**Restored here:** one `ARCHITECTURE-REVIEW.md` per active change; each phase has architecture gate + implementation review checklist.

---

## Primary invariant (north star)

> Exactly one runtime component owns an in-progress **capture commit** from stop until stable runtime state.

**Capture stop ≠ capture commit.** Input routing (Record vs slot buttons) is **`slot-performance-interaction`**, not this change.

---

## Finding → phase map

| Severity | Finding | Phase | Capability / owner |
|----------|---------|-------|-------------------|
| Critical | No single owner for commit after stop; record sync vs overdub deferred FSM | 1–3 | `capture-commit-owner` |
| Critical | Scattered `cancelDeferredOverdubStop` — symptom patches | 4 | `capture-state-guards` |
| High | Duplicate post-commit: projection, invalidation, save, display (record vs overdub) | 2 | DEC-016 bridge (see design § DEC-016) |
| High | `captureAppendFrozen_` / stale FSM blocks record arm and live display | 4 | `capture-state-guards` |
| Medium | Duplicate `requestDeferredSaveState` on record tail | 2 | `commitCaptureForStop` pipeline |
| Medium | Telemetry / stage log ordering drift during refactor | 1–2 | Behavior-preserving invariant |
| Low | `OverdubStop*` names mislead after record uses same FSM | 5 | Rename only |

---

## Evidence anchors (code)

| Concern | Primary location |
|---------|------------------|
| Record sync stop | `Track::stopRecording`, `stopRecordingToStopped` |
| Overdub deferred stop | `Track::queueOverdubStopCommit`, `processDeferredOverdubStop` |
| Shared finalize | `Track::finalizeCommitSideEffects` |
| Loop seal | `Loop::commitCapturePass` |
| Post-stop projection | `Track::resetPlaybackState`, record inline `projectionCycleStartTick` |
| Playback window | `ensurePlaybackWindowBuilt`, `invalidatePlaybackWindow` |
| Slot stop paths | `MidiButtonActions::handleToggleRecordForSlot`, `TrackManager::finalizeCaptureAndSelectSlot` |
| Cancel scatter | `cancelDeferredOverdubStop` call sites in `Track.cpp`, `TrackManager.cpp` |
| Idle driver | `Track::processDeferredIdleMaintenance` |

---

## Per-phase gates

Complete **Architecture gate** before coding the phase. Complete **Implementation review** before checking off phase in `tasks.md`.

### Phase 0 — Documentation (complete)

| Architecture gate | Answer |
|-------------------|--------|
| Ownership change? | Documented — deferred to Phase 3 |
| State transition change? | Documented — Phase 3+ |
| Formal trigger fired? | **YES** (logged as DEC-023); implementation deferred |
| New top-level Manager? | **NO** — extend `Track` |

| Implementation review | |
|-------------------------|--|
| OpenSpec folder complete | [x] |
| DEC-023 appended | [x] |
| CURRENT_WORK / PROJECT_STATE | [x] |
| Naming glossary locked | [x] |
| **Approval** | APPROVE |

---

### Phase 1 — Extract `commitCaptureForStop` (behavior-preserving)

**Scope:** Move flush → `commitCapturePass` → `finalizeCommitSideEffects` into `Track::commitCaptureForStop`. Record stays sync; overdub FSM calls same function. **No renames. No scheduling change.**

#### Architecture gate

| Question | Required answer |
|----------|-----------------|
| Ownership change? | **NO** — extraction only; `Track` already owns stop |
| State transition change? | **NO** |
| Formal trigger? | **NO** — behavior-preserving |
| Pipeline owns button/transport/quantize? | **NO** — must stay in stop entry |
| New abstraction justified? | `commitCaptureForStop` only — orchestrator on existing owner |

#### Implementation review ([reviewer.md](../../../docs/agents/reviewer.md))

| Check | Pass |
|-------|------|
| `pio test -e native` unchanged pass count | [x] 504/504 |
| Record stop: same `CommitReason`, same telemetry order | [x] |
| Overdub FSM: same stage advance count per stop | [x] |
| No new `execute*` / `*Manager` / parallel FSM owner | [x] |
| `Loop::commitCapturePass` still sole seal authority | [x] |
| Hot path: no full validate on stop | [x] |
| OpenSpec `tasks.md` Phase 1 checked | [x] |

| HITL | |
|------|--|
| Baseline unchanged (no new failures) | [ ] pending user run |

**Approval:** APPROVE

---

### Phase 2 — Centralize post-finalize (behavior-preserving)

**Scope:** Single post-commit helper: `invalidatePlaybackWindow`, cache invalidation, display snapshot, one `requestDeferredSaveState`, shared `logCaptureCommitStage`. See refinement doc § Phase 2 acceptance.

#### Architecture gate

| Question | Required answer |
|----------|-----------------|
| Ownership change? | **NO** — consolidate side effects on `Track` |
| State transition change? | **NO** |
| DEC-016 bridge | **YES** — one projection/invalidation path per publish |
| Display writes from `Track`? | Only flags/snapshot trigger — same as today |

#### Implementation review

| Check | Pass |
|-------|------|
| Record + overdub call **same** post-commit helper after `Published` | [x] |
| Record tail duplicate save removed | [x] |
| Overdub FSM `Finalized`/`Complete` no duplicate `resetPlaybackState` + save | [x] |
| `playbackRevision` bumps once per publish | [x] |
| `finalizeCaptureAndSelectSlot` stop paths use pipeline | [x] |
| `pio test -e native` | [x] 504/504 |
| HITL matches pre-Phase-1 baseline | [ ] pending user run |

**Approval:** APPROVE

---

### Phase 3 — Record → deferred commit owner (first behavior change)

**Scope:** Record stop queues deferred FSM; single in-progress commit session on `Track`.

#### Architecture gate (mandatory)

| Question | Required answer |
|----------|-----------------|
| Ownership change? | **YES** — one commit session owner |
| State transition change? | **YES** — record stop latency / STOPPED_RECORDING tail |
| Formal trigger? | **YES** — full [PREFLIGHT](../../../docs/templates/PREFLIGHT.md) or inline equivalent in PR |
| DECISION_REVIEW completed? | **YES** — cite DEC-023, DEC-004, DEC-020 |
| Conflicts DEC-016 / DEC-020? | Document: commit owner does not block mid-pass persist |

#### Implementation review

| Check | Pass |
|-------|------|
| Record preparation (quantize, truncation) **before** pipeline | [x] |
| Only one commit path active per capture pass | [x] |
| `STOPPED_RECORDING` + idle maintenance runs FSM | [x] |
| Acceptable record-stop latency on hardware | [ ] pending HITL |
| No capture loss (native + HITL arm→record→stop) | [ ] pending HITL |
| Notes visible on display during record | [ ] pending HITL |
| `test_capture_state_guards` extended | [x] |
| `pio test -e native` | [x] 506/506 |

**Approval:** APPROVE (HITL gate pending)

---

### Phase 4 — Lifecycle abort (`cancelCaptureCommit`)

**Scope:** `resetCaptureCommitSession` / `cancelCaptureCommit`; remove scattered cancels.

#### Architecture gate

| Question | Required answer |
|----------|-----------------|
| Ownership change? | **YES** — sole abort API |
| State transition change? | **YES** — abort semantics |
| Arm / transport / clear paths | Single lifecycle hook documented |

#### Implementation review

| Check | Pass |
|-------|------|
| `startRecording` clears freeze + aborts stale commit | [ ] |
| `handleTransportStop` aborts all tracks | [ ] |
| `clear` aborts before `discardCapture` | [ ] |
| Guard in deferred processor on phase/reason mismatch | [ ] |
| Native lifecycle tests | [ ] |
| HITL: transport stop during queued commit | [ ] |

**Approval:** APPROVE / REQUEST CHANGES

---

### Phase 5 — Rename cleanup (behavior-neutral)

**Scope:** `OverdubStop*` → `CaptureCommit*` symbols only.

#### Architecture gate

| Question | Required answer |
|----------|-----------------|
| Behavior change? | **NO** |
| Rename-only PR? | **YES** — bisect-friendly |

#### Implementation review

| Check | Pass |
|-------|------|
| `pio test -e native` full suite | [ ] |
| Canonical 64+64 HITL `PERS,result,...,ok` | [ ] |
| No logic diff in rename commit (or split commits) | [ ] |
| Archive change when green | [ ] |

**Approval:** APPROVE / REQUEST CHANGES

---

## Verification matrix (summary)

| Phase | Native | HITL | Architecture doc |
|-------|--------|------|------------------|
| 0 | N/A | N/A | This file + DEC-023 |
| 1 | `pio test -e native` | Baseline unchanged | § Phase 1 gates |
| 2 | + structural checks | Baseline identical | § Phase 2 + refinement § Phase 2 acceptance |
| 3 | + FSM tests | Arm → record → stop | Full PREFLIGHT |
| 4 | + abort tests | Transport stop during commit | § Phase 4 gates |
| 5 | Full | 64+64 | Rename-only review |

---

## Agent workflow (each phase session)

1. Load [PROJECT_STATE](../../../docs/runtime/PROJECT_STATE.md), [CURRENT_WORK](../../../docs/runtime/CURRENT_WORK.md), this file, `tasks.md`.
2. Answer **Architecture gate** for the target phase — stop if Phase 3+ trigger and no approval.
3. Implement phase scope only (no drive-by).
4. Run verification matrix row.
5. Fill **Implementation review** checklist; output [reviewer.md](../../../docs/agents/reviewer.md) format in PR or session notes.
6. Check off `tasks.md`; update PROJECT_STATE if phase completes.

---

## Out of scope (do not reject for)

- `slot-performance-interaction` — capture **start** input routing
- DEC-020 Phase 5 recovery
- Full interval projection / 16-bar display window (DEC-016) unless Phase 2 bridge regresses
