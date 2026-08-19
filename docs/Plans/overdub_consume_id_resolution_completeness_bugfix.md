# Overdub consume id-resolution completeness

**Status:** **FROZEN** — Stages 1–2 shipped; native **1387/1387**; HITL Stage 1 [`195016`](../../captures/session_20260819_195016.log), Stage 2 [`201457`](../../captures/session_20260819_201457.log).  
**Date:** 2026-08-19  
**Kind:** bugfix  
**Parent:** [`overdub_consume_ledger_merge_enhancement.md`](overdub_consume_ledger_merge_enhancement.md) (FROZEN — selection change rejected)  
**Evidence:** HITL [`191133`](../../captures/session_20260819_191133.log) (`norow=1`, non-empty ids + `scan=1`); [`193024`](../../captures/session_20260819_193024.log) (`scanadd` 18, all `in_ids=0`)

---

## Invariant (one sentence)

At overdub note-off, every geometric consume participant is represented in the effective overlap id set **and** resolves to a row in `overdubSourceViewNotes_` via `appendNotesForIds`. The window-scan path (`collectConsumeWindow`) is **removed** — all holds use the same geometric + id lookup path.

---

## Root causes (device-backed)

| Class | Symptom | Cause |
|-------|---------|-------|
| `scanadd` / `scanOnly` | Participant selected only by window scan (`in_ids=0`) | `overlapNoteIds` captured ledger/open-at-S identities; geometric overlap can include committed notes that were not open at hold start and not started during the hold |
| `norow` | `ids>=1`, `idsel=0`, `norow=1` | Ledger id in `overlapNoteIds` with no matching `overdubSourceViewNotes_` row at lookup time (row never merged or pruned by identity filter) |

Empty `overlapNoteIds` is not “no participants” — geometric overlap from the source view still produces Hide/Shorten via the same path (122848 hide contract).

---

## Stage 1 (shipped)

Non-empty `overlapNoteIds` → complete `effectiveOverlapNoteIds`, id lookup only; window scan skipped when non-empty.

---

## Stage 2 (shipped)

**Goal:** Apply geometric id completion to **empty** incoming id sets and remove `collectConsumeWindow` entirely.

**Owner:** `Loop::accumulatePendingNoteChangesForIncomingNote` in [`LoopPendingNoteChange.cpp`](../../src/Loop/LoopPendingNoteChange.cpp).

1. Always run `completeConsumeParticipantIds`: geometric union from `overdubSourceViewNotes_` per hold segment (wrap-aware).
2. `ensureOverdubSourceNotesForHold` per segment **only when** `loopLen > overdubSourceWindowLengthTicks()` (Phase 4 — short loops must not JIT-fill at note-off).
3. `appendNotesForIds` on `effectiveOverlapNoteIds` for all holds; `emptySets` only when the effective set stays empty.
4. **Deleted** `collectConsumeWindow`, `unionSelectedNote`, and `DisplayWindowUtils` include on this path.

**Validation:** Native **1387/1387**; `test_overdub_consumes_existing_source_view_overlap` and `test_note_off_skips_hold_fill_when_source_view_covers_loop` now expect `lookedUp=1`, `emptySets=0` for empty incoming ids with geometric overlap.

**HITL [`201457`](../../captures/session_20260819_201457.log):** track 7, 24-bar loop, extended manual overdub + undo. Zero `DIAG,consume` / `scanadd` / `norow` (vs [`193024`](../../captures/session_20260819_193024.log): 27 / 18 / 9). Occupy `DIAG,lcr,part` **86/86** `eq=1` (vs `193024`: 46/62). `why=hold,from=win` (180) is hold-prep JIT only — not consume window scan.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — extend `Loop::accumulatePendingNoteChangesForIncomingNote`; identity writers unchanged |
| State transition change? | **NO** |
| Reuse | **YES** — `ensureOverdubSourceNotesForHold`, `collectOverdubSourceHoldParticipantIds` geometry (`existingNoteOverlapsIncomingHold`), `OverlapCandidateLookup::appendNotesForIds` |
| New owner? | **NO** |

---

## Implementation (Stage 1 + 2)

**Owner:** `Loop::accumulatePendingNoteChangesForIncomingNote` in [`LoopPendingNoteChange.cpp`](../../src/Loop/LoopPendingNoteChange.cpp).

1. Build `effectiveOverlapNoteIds` from incoming `overlapNoteIds`.
2. `completeConsumeParticipantIds`: geometric union + long-loop hold fill per segment.
3. `appendNotesForIds` on `effectiveOverlapNoteIds` — sole candidate selection path.

**Does not change:** `resolveConstrainedGeometry`, occupy ledger path, `Track::snapshotOverlapHoldCandidates` contract for note-on.

---

## Validation

| Gate | Criterion | Result |
|------|-----------|--------|
| Native | `test_consume_attribution_*` + `test_consume_id_resolution_norow_repaired_by_hold_fill`; `pio test -e native` | **PASS** 1387/1387 |
| HITL Stage 1 | Zero `DIAG,consume` on id-complete non-empty holds | **PASS** [`195016`](../../captures/session_20260819_195016.log) |
| HITL Stage 2 | Zero `DIAG,consume` / `scanadd` / `norow`; occupy `eq=1` under extended manual overdub | **PASS** [`201457`](../../captures/session_20260819_201457.log) |
| Decommission | `collectConsumeWindow` removed; all holds use geometric id completion + `appendNotesForIds` | **shipped** Stage 2 |

---

## Pre-implementation review

### Proceed?

**YES** — extend existing `Loop` consume owner; empty fallback preserved.
