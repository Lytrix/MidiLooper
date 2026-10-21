# 3.15e — wrap-continuation packet (design first; no ring change)

**Change:** `windowed-playback-generation` · **Task:** 3.15e · **Branch:** `feature/unified-rolling-playback-path` ·
**Base:** `8131356f`. **Nature:** design/execution packet only. No production line, no admission constant, no predicate, no
fallback-selection change is made by this document.

## Status in one paragraph

The paged lane serves a dense interval **inside** a long loop completely (2 500 = oracle, element-wise, through
`Track::playMidiEvents`). The same lane, on a **whole-loop** dense interval, emits the first 2 437 events exactly and then
loses the remaining **63**. The delivery report says `CannotStraddleTheWrap` and the selector admits it anyway; refusing it
was measured and emits **0** (merged store 0), so refusal is strictly worse. The correctness obligation is therefore *make
the existing paged delivery survive the wrap*, not admission.

## 1. Where the 63 events are lost — named, measured

Instrumented at the wrap turn (`t = 1 536` of a 2-bar loop), short dense shape (2 500 accepted events, 1 536 ticks):

| Reading at the wrap turn | Value |
|---|---|
| `consumedOrdinal` (walk position in the ring) | **2 437** |
| `producedOrdinal` (producer position) | **2 628** |
| `passContentOrdinal` (this pass's content end, set by the producer's re-arm) | **2 500** |
| `passBaseOrdinal` after `notePassBoundary` | **2 437** |
| emitted so far | **2 437** |
| next-pass head already in the ring | 2 628 − 2 500 = **128** |

**The transition:** `PagedEmissionState::notePassBoundary(publishedOrdinal)` sets `passBaseOrdinal = consumedOrdinal`. At
this wrap the walk had consumed 2 437 while the pass's content ends at 2 500, so walk index 0 of the new pass is mapped to
ring ordinal **2 437** — *behind* the pass's own content end. Consequences, in order:

1. the 63 events in ordinals `[2 437, 2 500)` are still published and still in the ring, but they now lie **before** the new
   pass base, so no walk index addresses them: never emitted;
2. the new pass's own head (`[2 500, 2 628)`) is addressed 63 events late, so the following pass is mis-indexed by the same
   amount;
3. `admissible` stays true (`passesUnstraddled == 0`), so nothing names the loss — the report says
   `CannotStraddleTheWrap` and the predicate is `bound && admissible`, which is true.

**Why the walk had not finished the pass:** the producer's page rate is one page per owner turn (128 events), and the dense
content needs ≈104 events per 64-tick edge advance. At the wrap turn the last page holding the tail had not been visible to
that turn's walk (it was produced at the same turn, after the boundary was judged). So the boundary is reached with a
**latency of one page**, and the boundary logic turns a recoverable lateness into a permanent loss.

**Ambiguity this packet must resolve, not assume:** whether the correct repair is to stop the base from jumping past the
content end (serve the tail from the new pass, in order but late by ≤ one turn), or to keep the producer one page ahead so
the tail is staged before it is walked (no lateness at all). The measurement above is consistent with both; the RED/GREEN
cases below discriminate them.

## 1a. The lateness measurement that decides A vs B (2026-10-21)

Read-only, before any change. At the wrap turn of the short dense shape (`t = 1 536`, 2-bar loop):

| Reading | Value |
|---|---|
| phases of the 63 stranded events | **1 473 … 1 535** |
| the wrap turn's own walk window (`prevTickInLoop` → loop end) | **1 472 … 1 536** |
| emitted before that turn | 2 437 |

**The stranded tail is entirely inside the wrap turn's own permitted window.** There is no post-wrap catch-up to justify:
the events are due *in the very turn* that fires the boundary, and that turn emits nothing for them. So the choice is not
"A (late tail) vs B (staged ahead)" but:

- **A′ — defer the re-base past that turn's emission.** The boundary is judged (admission) before selection as today, but
  the *re-base* must take effect only after the wrap turn has consumed its own window. The tail is then served in-window, on
  time, exactly once, with no new state, no rate change and no deeper ring — the defect is the ordering of the boundary
  against the turn that owns those events.
- **B** would only be justified if the tail could *not* be served inside the wrap turn's own window. It can, so B is not
  required by this measurement.

**Consequence for the acceptance test the owner set:** the "one owner turn late" clause is not the bar to clear — the bar is
that the tail is emitted **in the wrap turn itself**, i.e. zero lateness. A′ must prove that, plus order before any phase-0
event, continuous ledger, and no accumulation across wraps.

## 2. Contract (must hold after the change)

> A bound paged delivery for an interval that crosses the loop boundary remains **one logical delivery** across that
> boundary. Reaching the wrap may advance or re-arm page state, but must not retire, invalidate, or strand the remaining
> accepted events. Every accepted event is emitted **exactly once and in loop order**.

Corollaries:
- the delivery identity `(slot, generation, revision, interval)` is unchanged across the wrap;
- no replay from tick 0: the new pass continues the same ordinal stream, it does not restart it;
- a late page may cost **lateness**, never **loss**, and must be visible in the existing accounting (`dryTurns` /
  `passesWithDrys`) rather than silent;
- the long dense control (2 500/2 500) must be **byte-for-byte the same behaviour** before and after.

## 3. What must survive the wrap (pin these in the implementation)

| Item | Requirement |
|---|---|
| delivery identity | same `(slot, generation, revision, interval)`; no re-bind at the wrap |
| source generation | the retained source and its pin survive; no re-retain path is introduced |
| page ordinals | monotone across the wrap; **no ordinal is skipped** by a boundary re-base |
| accessor state | `consumedOrdinal`, `passBaseOrdinal`, `passContentOrdinal` satisfy `passBase ≤ consumed ≤ passContent` at every boundary |
| emission cursor | rewinds per pass (index 0 = new base), never resets the ordinal stream |
| ledger | unchanged across the wrap; no double On, no orphan Off, no closure replay |
| remaining authority | the pass's content end stays authoritative for the *emitting* pass; the next pass's head is never served inside it |

## 4. Bounded mechanism — candidates, smallest first

**A. Boundary base honours the content end.** `notePassBoundary` maps the new pass to `passContentOrdinal` (this pass's own
content end) instead of `consumedOrdinal` when the walk has not yet consumed it. The unwalked tail then remains *ahead* of
the walk and is emitted at the head of the new pass: order preserved, exactly once, lateness ≤ one turn. No new state, no
ring-depth change, one expression in `notePassBoundary` (plus its `passBase` initialisation). **Lateness is the cost**, and
it must be measured against the dense long control (which must not move).

**B. One page staged ahead across the wrap.** Keep the producer a page ahead when the pass end is within one page of the
walk (the `emitting` re-arm already exists for the *next* pass's head; this extends it to *this* pass's tail). No lateness,
but it changes the producer's per-turn budget and the ring's occupancy, so it needs the cost proof first.

**Rule for choosing (refined by §1a):** **A′** is the candidate — the tail is due inside the wrap turn's own window, so
there is no lateness to permit. **B** only if A′ cannot be made to serve the tail in that turn. **Not allowed without its own measurement:** increasing ring depth
(a larger ring would hide the 2 500 case and leave the lifecycle wrong), replaying the pass from tick 0, or refusing the
delivery (measured: 0/2 500).

## 5. Cost proof required before implementation

| Term | Requirement |
|---|---|
| RAM1 `variables` / ITCM / `locals` | unchanged, or a named, approved rebase; state the delta per candidate |
| PSRAM / LoopPool | unchanged; no new per-slot buffer, no whole-loop materialization |
| `sizeof(LoopPlaybackRuntime)` / `PagedEmissionState` | unchanged for A; any growth for B priced and pinned |
| page rate | events per turn and pages per turn stated for both shapes; the long control must not regress |
| hot-path class | the edge gains no new work: A is a boundary-time expression (off-edge), B must be shown not to add per-turn cost to the walk |

## 6. RED / GREEN cases (all six in the suite that lands the fix)

1. **dense long interval** — 2 500/2 500, element-wise identical to the oracle (the control that must not move).
2. **dense 2-bar whole-loop** — 2 437 → **2 500**, element-wise exact.
3. **explicit wrap crossing with events on both sides of tick 0** — a dense interval whose content straddles the loop
   boundary by construction, asserted in order across the boundary.
4. **no duplicate On/Off at the wrap** — exactly one emission per accepted event, `(tick, note, kind)` multiset equal to the
   oracle's.
5. **multi-wrap traversal does not replay the first page** — three consecutive traversals, third-pass content asserted, no
   first-page repetition.
6. **negative control: unbinding paging still yields 0** in the rootless shape — proves there is no hidden fallback and keeps
   the "refusal is unsafe" finding load-bearing rather than folklore.

## 7. Definition of done for the implementation slice

- **The gated RED joins the default gate.** `DENSE_DELIVERY_EDGE_ORACLE` currently keeps the measured failure off the normal
  suite; the implementation slice that fixes wrap continuation must remove the gate (or flip it on by default) **in the same
  slice**, so the contract cannot silently regress into an optional test.
- Both images at the approved pins, or an approved rebase with the reasoning recorded.
- `openspec validate --strict`, `git diff --check`, full native green with the oracle enabled.
- No change to `admittedForEmission()`, any admission constant, or fallback selection.

## 8. Non-goals (explicit)

- Not a paged redesign: no new ring, no new scheduler, no new owner.
- Not the `CannotStraddleTheWrap` semantics question: the report may stay exactly as it is; this packet fixes the *loss*.
- Not route unification for short dense loops by the merged fallback — 3.15f still owns that, and this measurement shows the
  fallback is absent in the rootless shape anyway.
- Not a device claim: the fix is native-provable; device parity remains 3.15f/6.5.

## 9. Open questions the implementation must answer

1. Does candidate **A** keep the emitted stream within the permitted traversal rule, or does the ≤ one-turn lateness itself
   count as a missed deadline? (Measured: the emitted prefix is exact, so the question is only about the tail's phase.)
2. Is `passContentOrdinal == 0` (never re-armed) a distinct boundary case that A must keep refusing to re-base from?
3. Does B change the *long* dense page rate at all — and if it does, by how many events per turn?
4. Does the wrap case need the ledger's boundary summary at all, or is the ordinal base alone sufficient (the emitted prefix
   being exact suggests the latter)?
