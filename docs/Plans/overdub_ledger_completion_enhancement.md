# Overdub ledger completion

**Status:** **shipped** — Stages 2–4 native **1387/1387**. Baseline [`202256`](../../captures/session_20260819_202256.log). HITL gate open post-flash.  
**Date:** 2026-08-19  
**Kind:** enhancement  
**Supersedes (execution authority):** active slices of participant architecture Phase 5 / 64-bar RC2; consolidates ledger consume + participant tracks below.  
**Parent:** [`overdub_participant_loop_content_architecture.md`](overdub_participant_loop_content_architecture.md)  
**Decisions:** [DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat), [DEC-042](../DECISION_LOG.md#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem)

**Historical (frozen — do not reopen):**
- [`overdub_consume_ledger_merge_enhancement.md`](overdub_consume_ledger_merge_enhancement.md) — selection-change rejected
- [`overdub_consume_id_resolution_completeness_bugfix.md`](overdub_consume_id_resolution_completeness_bugfix.md) — Stages 1–2 shipped

---

## Invariant (one sentence)

On long loops, overdub note-off materializes consume participant rows from prepared pitch spans or a bounded miss fallback — not a blanket 16-bar `resolveWindow` on every hold when ledger ids already resolve in the source view.

---

## Shipped baseline (do not regress)

| Layer | Owner | Status |
|-------|-------|--------|
| Note-on occupy | `Track::snapshotOverlapHoldCandidates` → `Loop::collectOverdubNoteOnParticipantIds` | shipped (DEC-041) |
| Consume id set | `completeConsumeParticipantIds` + `appendNotesForIds` | shipped (id-resolution Stages 1–2) |
| Consume geometry | `resolveConstrainedGeometry` | unchanged |
| Short-loop note-off | skip hold fill when `loopLen <= overdubSourceWindowLengthTicks()` | shipped (Phase 4) |

---

## Problem anchor — HITL [`202256`](../../captures/session_20260819_202256.log)

66-bar loop, extended overdub. **32** `DIAG,lcr,src,why=hold,from=win` lines: ~**196 ms** each, **1135** events, **`merged=0`**, **1064** notes already in source view. **0** `DIAG,consume`. Occupy **15**× `from=ledger`, all **`eq=0`**, **`b=0`** (prepared miss).

---

## Baseline gates (Stage 1)

| Gate | Baseline capture / test |
|------|-------------------------|
| Consume zero | [`201457`](../../captures/session_20260819_201457.log), [`202256`](../../captures/session_20260819_202256.log): `DIAG,consume` = 0 |
| Native | **1387/1387** at track start |
| Hold cost (pre-fix) | [`202256`](../../captures/session_20260819_202256.log): 32× `from=win`, `merged=0` |
| Ahead-note JIT | `test_empty_ids_resolve_jit_ahead_note_on_64_bar_loop`, `test_empty_ids_shorten_jit_ahead_after_sounding_snapshot` |
| Short-loop skip | `test_note_off_skips_hold_fill_when_source_view_covers_loop` |

---

## Implementation stages

### Stage 2 — RC2 prepared-ready hold resolution (note-off) — **shipped**

**Owner:** `Loop::ensureOverdubSourceNotesForHold`  
**Reuse:** `LoopContentResolution::tryCopyPreparedPitchSpansForHold`  
**Behavior:** Prepared-ready path logs `why=hold,from=span`; no `resolveWindow` on prepared hit.

### Stage 3 — Bounded miss fallback — **shipped**

**Owner:** `Loop::ensureOverdubSourceNotesForHold` miss branch  
**Behavior:** `DIAG,lcr,hold,miss` then `from=win` fallback.

### Stage 4 — Skip default long-loop hold window when unnecessary — **shipped**

**Owner:** `Loop::accumulatePendingNoteChangesForIncomingNote` (`completeConsumeParticipantIds`)  
**Behavior:** Skip hold fill when non-empty occupy ids all have source-view rows; removed duplicate long-loop pass; empty ids still materialize for ahead-note contract.

### Stage 5 — Enter/wrap (opportunistic)

**Owner:** `Loop::rebuildOverdubSourceView` — already prefers `from=span` / `from=prep` before `from=win`. No firmware change unless HITL shows regressions.

### Stage 6 — Validation and freeze

- `pio test -e native`
- 64-bar HITL: `why=hold,from=win` only on prepared miss + empty-id ahead path; consume diagnostics zero.

---

## Hard boundaries

- No consume ledger-merge selection change.
- No `length`/`endTick` on `ActiveNoteLedger::Entry`.
- No `evaluateOccupyOverlap`.
- No geometry owner change.
- Do not delete `overdubSourceView`.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — narrow row materialization inside `Loop::ensureOverdubSourceNotesForHold` |
| Transition change? | **NO** |
| Reuse | **YES** — prepared span copy + existing win fallback |
