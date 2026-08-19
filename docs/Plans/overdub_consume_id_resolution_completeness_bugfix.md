# Overdub consume id-resolution completeness

**Status:** **shipped** — Stage 1 native + HITL [`195016`](../../captures/session_20260819_195016.log). Native **1387/1387**.  
**Date:** 2026-08-19  
**Kind:** bugfix  
**Parent:** [`overdub_consume_ledger_merge_enhancement.md`](overdub_consume_ledger_merge_enhancement.md) (FROZEN — selection change rejected)  
**Evidence:** HITL [`191133`](../../captures/session_20260819_191133.log) (`norow=1`, non-empty ids + `scan=1`); [`193024`](../../captures/session_20260819_193024.log) (`scanadd` 18, all `in_ids=0`)

---

## Invariant (one sentence)

At overdub note-off, every geometric consume participant is represented in the effective overlap id set **and** resolves to a row in `overdubSourceViewNotes_` via `appendNotesForIds`, so non-empty holds do not depend on the window-scan path in `collectConsumeWindow`.

---

## Root causes (device-backed)

| Class | Symptom | Cause |
|-------|---------|-------|
| `scanadd` / `scanOnly` | Participant selected only by window scan (`in_ids=0`) | `overlapNoteIds` captured ledger/open-at-S identities; geometric overlap can include committed notes that were not open at hold start and not started during the hold |
| `norow` | `ids>=1`, `idsel=0`, `norow=1` | Ledger id in `overlapNoteIds` with no matching `overdubSourceViewNotes_` row at lookup time (row never merged or pruned by identity filter) |

Empty `overlapNoteIds` fallback is unchanged — 201/~347 note-offs in [`191133`](../../captures/session_20260819_191133.log) had empty sets.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — extend `Loop::accumulatePendingNoteChangesForIncomingNote`; identity writers unchanged |
| State transition change? | **NO** |
| Reuse | **YES** — `ensureOverdubSourceNotesForHold`, `collectOverdubSourceHoldParticipantIds` geometry (`existingNoteOverlapsIncomingHold`), `OverlapCandidateLookup::appendNotesForIds` |
| New owner? | **NO** |

---

## Implementation

**Owner:** `Loop::accumulatePendingNoteChangesForIncomingNote` in [`LoopPendingNoteChange.cpp`](../../src/Loop/LoopPendingNoteChange.cpp).

1. Build `effectiveOverlapNoteIds` from incoming `overlapNoteIds`.
2. When non-empty: `ensureOverdubSourceNotesForHold` per hold segment (long-loop JIT + short-loop row repair).
3. Union geometric participants from `overdubSourceViewNotes_` that `existingNoteOverlapsIncomingHold` with each hold segment.
4. `appendNotesForIds` on `effectiveOverlapNoteIds` only — **skip `collectConsumeWindow` when non-empty**.
5. When empty: keep existing `collectConsumeWindow` (empty-ids hide contract).

**Does not change:** `resolveConstrainedGeometry`, occupy ledger path, empty-set fallback, `Track::snapshotOverlapHoldCandidates` contract for note-on.

---

## Validation

| Gate | Criterion | Result |
|------|-----------|--------|
| Native | `test_consume_attribution_*` + `test_consume_id_resolution_norow_repaired_by_hold_fill`; `pio test -e native` | **PASS** 1387/1387 |
| HITL | `DIAG,consume,select` on non-empty holds: `scan=0`, `norow=0`; no `scanadd` on attributed holds | **PASS** [`195016`](../../captures/session_20260819_195016.log) — zero `DIAG,consume` lines (attribution emits only when non-zero) |
| Decommission | Window scan removed for non-empty effective id set | **shipped** in same commit |

---

## Pre-implementation review

### Proceed?

**YES** — extend existing `Loop` consume owner; empty fallback preserved.
