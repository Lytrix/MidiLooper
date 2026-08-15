# Preflight — 6D production architecture gate

**Mode:** Full — formal trigger fired.  
**Date:** 2026-08-15  
**Change:** `loop-content-resolution`  
**Status:** 6.0 reading + 6D.4 architecture **approved 2026-08-15**. Firmware **not** authorized.

Plan: [`docs/Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md`](../../../docs/Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md) § Production architecture gate.

---

## Problem

After a PLAYING overdub commit, the next overdub button cannot consume prepared LCR. `commitPendingCapturePass` does `++playbackRevision`, so `preparedWindowReady` is false. The device gate runs only while STOPPED and is one-shot after `deviceGateComplete`. 6C then falls back to the 3b `visualCache.notes` copy. Native 6D.1–6D.3 showed a split historical `tickEvents` + delta `TickEventEntryVec` can stay current without rewriting H. Production would restamp the prepared window after publishing that delta so the next `establishOverdubSourceView` can consume LCR.

## Domain

Loop content resolution / overdub source view (DEC-037).

## Similar historical decisions

### Search locations

- [x] `docs/DECISION_LOG.md` — DEC-037, DEC-036 Layer D 3b
- [x] `docs/Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md`
- [x] `openspec/changes/loop-content-resolution/`

### Relevant findings

- DEC-037: LCR is a parallel prototype; materialize stays until gates pass. 6.0: start/stop must not construct/sort/checkpoint/resolve LCR **to open the source view**. Bounded index update belongs at the commit site (with 6B), not on the button.
- DEC-036 3b: overdub entry copies `visualCache.notes` ([`045556`](../../../captures/session_20260814_045556.log) 2214 µs). Keep that fallback.
- 6D.1 FAIL: one-vector sort/merge tracks H.
- 6D.2 / 6D.3 PASS: two-source find; repeated commits track accumulated Δ, not H.
- A and B rejected (STOPPED re-arm; PLAYING full-history slice).

### Existing owner

`LoopContentResolution` / `DeviceGateSession` owns the prepared `TickIndex` and the stamp. `Loop::commitPendingCapturePass` publishes the `OverdubPass` and bumps `playbackRevision`. `Track::finalizeCommitSideEffects` already runs 6B dirty-bar marks on overdub commit.

### Existing extension point

`Track::finalizeCommitSideEffects` (Committed + overdubStop) — same site as `Loop::markAffectedDisplayCacheRanges`. Consume stays `tryResolvePreparedWindow`.

### Reuse possible

**YES** — extend `DeviceGateSession` + `tryResolvePreparedWindow`. Do not add a Manager. Do not put a delta member on `TickIndex` (5.17 `tickEvents` stays the frozen historical vector).

### If no — why not

n/a

### Architecture review required

**YES** — state-transition change for `preparedWindowReady`.

---

## Loaded docs

- [x] `docs/Runtime/PROJECT_STATE.md`
- [x] `docs/Runtime/CURRENT_WORK.md`
- [x] `docs/DECISION_LOG.md` (DEC-037)
- [x] `docs/Authority/ARCHITECTURE_RULES.md`
- [x] `docs/ARCHITECTURE_REASSESSMENT.md`
- [x] `openspec/changes/loop-content-resolution/tasks.md`
- [x] `docs/Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md`
- [x] `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` (commit/stop owners; no change to those owners)

## Extension point (implementation)

`LoopContentResolution` method called from `Track::finalizeCommitSideEffects` after a committed `OverdubPass`, only when a prepared index is already kept. `tryResolvePreparedWindow` uses the existing two-source `resolveWindow` when the session delta vector is non-empty.

## Ownership change

| Field | Answer |
|-------|--------|
| Current owner | `LoopContentResolution` (`DeviceGateSession`) for prepared index + stamp. `Loop` for pass publish + `playbackRevision`. |
| Target owner | Same |
| Transfer needed | NO |
| If YES — compat removal trigger | n/a |

## Files affected (if approved later)

- `include/LoopContentResolution.h`
- `src/LoopContentResolution.cpp`
- `src/Track/TrackCaptureStopCommit.cpp` (one call at existing 6B site)
- `test/test_loop_content_resolution/test_loop_content_resolution.cpp`
- OpenSpec / DEC-037 / 6D plan

## New abstractions

| Name | Kind | Justification |
|------|------|---------------|
| none | | Session holds a `TickEventEntryVec`. Not a new type. Member name is an open naming pin (see gate). |

## Persistence impact

None. In-RAM prepared session only. No SD field.

## Undo impact

None in the first production slice. Undo already does `++playbackRevision`; stamp mismatch keeps 3b. Do not incrementally maintain undo/disable.

## Migration required

Firmware-only after approval. Host tests first. No SD format bump.

## Architecture review required (summary)

**YES** — see plan § Production architecture gate.

## Authority conflict check (mandatory)

| Source | Conflicts with architecture or intent? |
|--------|--------------------------------------|
| OpenSpec / tasks | **PIN** — 6.0 literal text says start/stop must not sort LCR. DEC-037 already places bounded update at the commit site (`finalizeCommitSideEffects` is on overdub stop). Needs an explicit 6.0 reading before firmware. |
| Proposed new class/helper | NO |

**If any YES:** **STOP** — no implementation until reassessment approved and docs updated.

This preflight **stops before firmware**. The 6.0 reading and 6D.4 architecture are approved. Implementation waits for an explicit 6D.4 implement request and a pinned name for the session `TickEventEntryVec`.

## Context summary

- Owner: `LoopContentResolution` prepared session; commit site already used by 6B.
- 6D.1 FAIL; 6D.2/6D.3 PASS native; production still frozen.
- `preparedWindowReady` is stamp equality + one-shot `deviceGateFinished`.
- LCR gate runs only when STOPPED (`processDeferredIdleMaintenance`).
- `tryResolvePreparedWindow` still queries one `tickEvents` vector.
- 6.0 vs commit-site sort is the approval pin.
- A/B remain rejected. 3b copy stays fallback.
- Tests: native `test_loop_content_resolution` first; device HITL only after an approved firmware slice.
