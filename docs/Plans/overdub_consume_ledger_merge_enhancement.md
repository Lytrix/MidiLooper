# Overdub consume ledger merge

**Status:** **FROZEN** — closed 2026-08-19. **Stage 1A observability COMPLETE**; **consume merge selection change (Stages 1–2) REJECTED** on device evidence HITL [`191133`](../../captures/session_20260819_191133.log). Attribution counters + `DIAG,consume,select` in tree; selection behavior **unchanged and staying**. Native **1386/1386**. RAM1 code **425964** / locals **4768**. `norow=1` successor **tagged for investigation** in [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) § Parked — no plan opened.  
**Date:** 2026-08-19  
**Kind:** enhancement  
**Parent:** [`overdub_present_at_tick_jit_enhancement.md`](overdub_present_at_tick_jit_enhancement.md)  
**Related decisions:** [DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat), [DEC-042](../DECISION_LOG.md#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem)  
**Evidence anchors:** [`121141`](../../captures/session_20260819_121141.log), [`161349`](../../captures/session_20260819_161349.log), [`174246`](../../captures/session_20260819_174246.log)

**Intent:** define the next implementation stage after the closed occupy/source-view RCs: merge consume candidate selection onto the already-shipped ledger participant identity path, without creating a new overlap owner.

---

## Invariant (one sentence)

At overdub note-off, consume candidate identities come from the committed playback ledger at hold start, while overlap geometry is still resolved only by `resolveConstrainedGeometry` over source-view note spans matched by `noteId`.

---

## Current behavior (confirmed)

1. `Track::snapshotOverlapHoldCandidates` catches up committed playback ledger to the hold tick and writes `pending.overlapNoteIds` via `collectOverdubNoteOnParticipantIds`.
2. `Loop::accumulatePendingNoteChangesForIncomingNote` first appends candidates by `noteId` (`OverlapCandidateLookup::appendNotesForIds`), then unions additional source-view notes by scanning consume windows (`collectConsumeWindow`), including `ensureOverdubSourceNotesForHold` on long loops.
3. `Loop::accumulatePendingNoteChangesFromSourceNotes` runs overlap geometry through `resolveConstrainedGeometry` and emits Hide/Shorten transforms.
4. `Loop::sealPendingNoteChangesToEditPasses` persists those transforms as companion EditPass rows.

This is a dual candidate authority today: ledger-id lookup plus source-view window walk.

---

## Scope

### In scope

- Candidate selection inside `Loop::accumulatePendingNoteChangesForIncomingNote`.
- Rules for when source-view window walk is allowed as fallback.
- Native coverage for ledger-id-driven consume selection (non-wrap and wrap-crossing).

### Out of scope

- New top-level overlap resolver or Manager.
- `evaluateOccupyOverlap`.
- Adding `length` or `endTick` to `ActiveNoteLedger::Entry`.
- `occupy = ledger` ownership transfer language.
- Consume merge with prepared full-loop `lcr,mat` readiness.
- Any change to `resolveConstrainedGeometry` ownership.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **YES (design approval required before firmware edits):** candidate authority for consume moves from dual authority toward ledger-routed ids, while keeping overlap-geometry ownership in `resolveConstrainedGeometry`. |
| State transition change? | **NO** for this stage — no new transport or mode transition. |
| Reuse | **YES** — extend `snapshotOverlapHoldCandidates`, `accumulatePendingNoteChangesForIncomingNote`, and `OverlapCandidateLookup::appendNotesForIds`. |
| New owner creation? | **NO** — do not introduce a new resolver type or manager. |

Because ownership answer is YES, this file is the required design checkpoint artifact before firmware implementation.

---

## Proposed implementation stages

### Stage 1A — consume candidate attribution (shipped, observability only)

Owner: `Loop::accumulatePendingNoteChangesForIncomingNote`. No selection change.

`OverlapHoldTotals` gained three counters that separate **identity** from **row availability**:

| Counter | Meaning |
|---------|---------|
| `scanOnlyCandidates` | Window/JIT scan selected the note and its `noteId` was **absent** from `overlapNoteIds` |
| `lateNoteCandidates` | `noteId` **was** in `overlapNoteIds`, but no source-view note existed at lookup time (JIT merge materialized it) |
| `idsWithoutNotes` | Id in the set with no selected note at all |

Device marker, emitted only when one of those is non-zero:

```text
#CAP,<us>,DIAG,consume,select,pitch=,ids=,idsel=,scan=,late=,norow=,s=,e=,jit=
```

`jit=1` means `loopLen > overdubSourceWindowLengthTicks()` for that hold, so the long-loop branch was eligible.

**Native proof:** `test_consume_attribution_counts_late_note_for_jit_ahead_candidate` (long loop, `ids={1}`, `late=1`, `scan=0`) and `test_consume_attribution_counts_scan_only_for_id_absent_candidate` (short loop, `ids={1}`, second note selected by scan, `scan=1`). The first fixture is the code-level proof of Stage 1A finding 2.

**Reading the next capture:** if `scan` and `late` are `0` across a session, non-empty ids are already sufficient and decision 2 can proceed as approved. Any non-zero `late` with `jit=1` confirms the long-loop dependency; any non-zero `scan` names holds where identity alone would drop a participant.

### Stage 1A device evidence — HITL [`191133`](../../captures/session_20260819_191133.log)

Eight `overlap_hold` summaries: `note_offs` 0, 0, 99, 64, 16, 21, 108, 39 with `empty_sets` 0, 0, 31, 20, 2, 21, 108, 19 — **201 of ~347 note-offs had an empty occupy set**, so the empty-ids fallback (decision 1) is heavily load-bearing. `overflows` **0** throughout.

Five holds emitted `DIAG,consume,select`:

| `pitch` | `ids` | `idsel` | `scan` | `late` | `norow` | `s`–`e` | Reading |
|--------:|------:|--------:|-------:|-------:|--------:|---------|---------|
| 12 | 1 | 1 | 1 | 0 | 0 | 384–96 | Non-empty ids; scan added a second participant. Wrap-crossing |
| 23 | 0 | 0 | 1 | 0 | 0 | 240–144 | Empty-ids fallback. Wrap-crossing |
| 12 | 1 | 1 | 1 | 0 | 0 | 192–288 | Non-empty ids; scan added a second participant |
| 72 | 0 | 0 | 1 | 0 | 0 | 240–288 | Empty-ids fallback |
| 96 | 1 | 0 | 1 | 0 | 1 | 0–48 | Occupy id resolved to **no** source-view note; scan found the only participant |

**Decision 2 is falsified.** Three of five attributed holds had a **non-empty** occupy set where the window scan was the only path to a participant — two with `idsel=1` (scan added a second note) and one with `norow=1` (the id resolved to nothing, so blocking extras would have produced zero candidates). Do not block additive scan candidates on non-empty ids.

**Long-loop JIT class did not reproduce.** `why=hold` fired **140** times and the session contained long loops (`live=18432`, `live=52224`, both above the 16-bar window), yet every emitted line is `jit=0` and `late=0`. Stage 1A finding 2 stays reachable in native (`test_consume_attribution_counts_late_note_for_jit_ahead_candidate`) but is **not** the observed device problem. The observed problem is the plain short-loop window scan.

**Successor question (identity vs geometry).** `pitch=96` `ids=1 idsel=0 norow=1` is the ownership split named in the Stage 1A guard check: identity assignment is already single-owner, but an id in the set had no note to resolve against. That — not candidate blocking — is the next thing worth fixing.

### Stage 1 — ledger-routed consume candidates (REJECTED 2026-08-19)

Not implemented. HITL [`191133`](../../captures/session_20260819_191133.log) showed a non-empty occupy set still depends on the window scan. Kept for the record:

Use `pending.overlapNoteIds` as the primary consume candidate identity set for note-off overlap resolution:

- Keep `OverlapCandidateLookup::appendNotesForIds` as the primary candidate materialization path.
- Keep `accumulatePendingNoteChangesFromSourceNotes` and `resolveConstrainedGeometry` unchanged.
- Gate source-view window scan fallback behind explicit conditions defined in Stage 1 decisions.

### Stage 2 — fallback contract hardening (REJECTED with Stage 1)

After Stage 1 behavior is stable, tighten fallback policy:

- Define exact conditions for source-view scan fallback (`empty`, `overflowed`, or explicit miss class).
- Keep fallback observable in capture diagnostics.
- Prove no regression for wrap-crossing consume behavior.

---

## Validation plan

### Native tests

Add focused fixtures in `test/test_pending_note_change` and `test/test_overdub_source_view` for:

1. Ledger id present, source-view window has extra same-pitch spans: consume resolves only ids selected from ledger path.
2. Wrap-crossing incoming note with ledger-selected candidates: both note segments still apply correct Hide/Shorten.
3. Fallback path behavior when overlap id set is empty or overflowed (once fallback contract is approved).

Run `pio test -e native`.

### HITL checks

Use existing capture diagnostics:

- `DIAG,lcr,part,from=ledger` (`n=`, `a=`, `b=`).
- consume lines (`DIAG,lcr,consume` / note-change totals) already emitted around note-off resolution.
- `seal_companion` for persisted Hide/Shorten rows.

Success for this stage is behavioral parity or improvement on consume correctness with no reopened closed RCs.

---

## Hard constraints

- Do not re-open Gate 5B withdrawn work.
- Do not patch occupy catch-up bounds (`occupyPhase <= lastTick` behavior stays as-is).
- Do not move overlap geometry out of `resolveConstrainedGeometry`.
- Do not fold capture stream into committed playback merged events.
- Do not delete `overdubSourceView`.

---

## Pre-implementation review

### Ready

- Candidate and geometry owners are identified and already separated in code.
- Existing tests cover overlap resolution and source-view behavior, and can be extended without architecture reshaping.
- Current branch state has closed parent RCs and clean validation baseline.

### Resolved (user / code)

| Topic | Decision |
|-------|----------|
| Overlap geometry owner | Keep `resolveConstrainedGeometry` as sole geometry decision owner. |
| Occupy participant source | Keep ledger-based participant identities (`collectOverdubNoteOnParticipantIds`). |
| Consume merge direction | Implement as candidate-authority merge inside existing `Loop` owner, not as a new resolver type. |
| Persistence path | Keep `sealPendingNoteChangesToEditPasses` unchanged for Stage 1. |
| Scope boundary | No `evaluateOccupyOverlap`, no `Entry.length/endTick`, no consume-on-`lcr,mat` coupling. |

### Open before coding

1. **Fallback contract:** when `overlapNoteIds` is empty, should consume run source-view scan fallback unconditionally, or only when `overflowed` is true?
2. **Fallback on non-empty ids:** if id lookup returns candidates and source-view scan would add additional same-pitch spans, should those additions be blocked in Stage 1?
3. **HITL gate definition:** should stage acceptance require a fresh long-loop (`64-bar`) run, or is native-plus-1-bar HITL sufficient for Stage 1?

### Decision options (with recommended defaults)

| Topic | Option A | Option B | Recommended default |
|-------|----------|----------|---------------------|
| Empty `overlapNoteIds` fallback | Treat empty as no participants; do not scan source-view | Keep source-view scan fallback when ids are empty | **Option B** — existing contract explicitly treats empty occupy as not equal to no participants; consume still reads source-view for this class (`122848` class and current runtime notes). |
| Non-empty ids + source-view extras | Keep additive source-view union after id lookup | Block additive extras when id lookup is non-empty (except explicit overflow fallback path) | **Option B** — this is the ownership move in this stage: non-empty ledger ids are authoritative candidate identities; geometry still resolves in `resolveConstrainedGeometry`. |
| Stage-1 acceptance gate | Native + 1-bar HITL only | Native + 1-bar HITL + fresh 64-bar HITL | **Option B** — code path includes long-loop note-off merge behavior (`ensureOverdubSourceNotesForHold` branch), so stage acceptance requires one fresh long-loop proof. |

Recommended defaults above are all code- and evidence-backed from the current shipped behavior and constraints in DEC-041 scope docs.

### Decision outcomes (approved 2026-08-19)

| Topic | Approved decision |
|-------|-------------------|
| Empty `overlapNoteIds` fallback | Keep source-view scan fallback when ids are empty. **Confirmed by [`191133`](../../captures/session_20260819_191133.log)** — 201 of ~347 note-offs had an empty set. |
| Non-empty ids + source-view extras | ~~Block additive source-view extras when id lookup is non-empty~~ — **REJECTED 2026-08-19** on device evidence; see Stage 1A device evidence. Dual authority stays. |
| Stage-1 acceptance gate | Native + 1-bar HITL only. Sufficient because the selection change is not being made. |

### Stage 1A guard check findings (2026-08-19, before first firmware edit)

Code-backed findings that constrain the approved decisions:

1. **`overlapNoteIds` has two writers.** `Track::snapshotOverlapHoldCandidates` inserts ledger identities open at `holdStart`; `Track::collectOverlapHoldPlaybackNoteOn` (called from `Track::sendMidiEvent` while `TRACK_OVERDUBBING`) inserts ids for notes starting during the hold. The insert precedes the `playbackEmitMidiOutput_` check, so muting does not drop ids.
2. **`appendNotesForIds` cannot reach notes absent from `overdubSourceViewNotes_`.** On loops longer than the source window, `collectConsumeWindow` calls `ensureOverdubSourceNotesForHold` and unions the returned `jitHoldPitchNotes`. That branch is the only path to a JIT-merged ahead note, regardless of whether its id is in the set.
3. **HITL [`123803`](../../captures/session_20260818_123803.log) is a counterexample to blanket blocking.** Track 0 (50688 ticks): `empty_sets=0` (ids non-empty on every note-off) with 36 `why=hold` fills, one `merged=1`.
4. **Second class, present on short loops too.** A note added earlier in the same overdub pass that starts after `S` is merged into the source view by `applyPendingNoteChangesToOverdubSourceView`, but its NoteOn was not emitted during this hold and it is not open at `S`, so ids can lack it on that cycle. The window scan is what selects it today.

Consequences: blocking additive candidates whenever ids are non-empty changes long-loop consume behavior, and a native + 1-bar HITL gate cannot observe that path because the JIT branch requires `loopLen > overdubSourceWindowLengthTicks()`.

**Recommended refinement:** scope ids-authoritative selection to loops where the source view covers the loop (`loopLen <= overdubSourceWindowLengthTicks()`), leaving the long-loop JIT branch unchanged. Under that scope, native + 1-bar HITL is a valid gate.

### Proceed?

- **NO for decision 2** — rejected on device evidence [`191133`](../../captures/session_20260819_191133.log). Consume keeps both candidate paths.
- **Stage 1A observability: shipped.** Decision 1 confirmed; decision 3 moot for this stage.
- **Successor tagged, not planned:** an occupy id that resolves to no source-view note (`norow=1`) is recorded in [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) § Parked. The detector is already in tree; it needs an architecture checkpoint before any firmware.
