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

## 1b. A′ attempt 1 — recovered the tail, perturbed the control (reverted, 2026-10-21)

Implemented and measured, then reverted (no production line left in the tree). Mechanism: `notePassBoundary` no longer
re-bases; it sets `passRebasePending`, and `servicePagedPassBoundaryForSlot` applies `passBaseOrdinal = consumedOrdinal` at
the start of the **next** owner turn. The flag fits existing padding (host `sizeof(LoopPlaybackRuntime)` stays 10 104; a
variant with a second `uint32_t` needed +8 B and was dropped).

| Case | Before | Attempt 1 | Verdict |
|---|---|---|---|
| dense 512-bar control | 2 500 | **2 498** | **regressed — not acceptable** |
| dense 2-bar whole-loop | 2 437 | **4 933** | tail recovered; the drive then covers a second traversal, so a one-traversal count is no longer the right oracle |
| report vs selector | passes | passes | unchanged |

A first form that also captured the base at the boundary (`pendingPassBaseOrdinal`) measured **4 937**: the stale base
re-addressed the already-emitted tail, so the base must come from the live `consumedOrdinal`, not a boundary snapshot.

**What attempt 1 established:** deferring the re-base *does* release the `1473…1535` tail, so §1a's direction is right.
**What it also established:** an unconditional deferral moves the long control by 2 events, so the base may only advance
once the walk has actually reached the pass's content end, and the oracle must state traversals explicitly.

**Refinement for attempt 2 (not implemented):**

1. Apply the deferred base only when `consumedOrdinal >= passContentOrdinal`, otherwise keep the old base so the walk
   drains the tail.
2. Express the harness oracle per traversal (walk exactly `n × loopLength`, expected `n × oracle`).
3. Re-run the long control first: it must be **2 500, unchanged**, before the short case is judged.

## 1c. A′ attempt 2 — conditional deferral (measured, reverted, 2026-10-21)

Mechanism exactly as specified: `notePassBoundary` keeps the **immediate** re-base when
`consumedOrdinal >= passContentOrdinal`, and only parks the base (`passRebasePending`) when the walk is still short of the
pass's content end; the parked base is applied from **live** `consumedOrdinal` once it reaches `passContentOrdinal`. One
`bool`, no size change.

| Case | Before | Attempt 2 | Verdict |
|---|---|---|---|
| dense 512-bar control | 2 500 | **2 500** | **unchanged — the conditional form fixes attempt 1's regression** |
| dense 2-bar whole-loop, two traversals | 2 437 (1st only) | **4 937 / 5 000** | pass 1 now complete (2 500); pass 2 still loses its final 63 |

**What this isolates:** the deferral is correct and sufficient for the **first** wrap — pass 1 goes 2 437 → 2 500 — and the
control is untouched. The residual 63 is the **final traversal's** tail: at that boundary the walk's index resets while
`passRebasePending` has not yet been applied (it needs `consumedOrdinal` to reach `passContentOrdinal`, which for the last
traversal happens only after the tail would have been served). So the defect is **per-wrap**, not one-off: the same
conditional must hold for every boundary, including the last one in the drive.

**Next diagnostic (do this before another mechanism):** record at the **second** boundary `passRebasePending`,
`consumedOrdinal`, `passContentOrdinal`, `passBaseOrdinal`, and the walk's index immediately after the reset, and whether the
tail is again *inside* that turn's window (it was for pass 1: 1 473…1 535 in [1 472, 1 536)). If it is, the fix is to apply
the parked base **after** the wrap turn's emission rather than at the next boundary service — i.e. the pending flag must be
consumed by the turn that parks it, not by the next one.

## 1d. Second-boundary probe — the instrument was wrong, not the hypothesis (2026-10-21)

Attempt 2 was re-applied and a probe sampled the **second** boundary from *after* the turn (`playMidiEvents` returned). The
capture is internally inconsistent and cannot be used:

| Reading (after the second boundary turn) | Value |
|---|---|
| saw a second boundary | yes |
| `passContentOrdinal` | **2 500** (stale — pass 2's end should be 5 000) |
| `passBaseOrdinal` | **0** (the pass-1 park was never applied) |
| `passRebasePending` | **1** |
| emitted by then | **4 937** |

`pending = 1` while `emitted = 4 937` is impossible under the code as written (a 4 937 walk satisfies
`consumed >= passContent`), and `base = 0` contradicts a pass 2 that emitted 2 437 events. So the sampling point is wrong:
by the time the probe reads, a later turn has already overwritten the fields it wanted. **Attempt 3 is therefore not
justified yet** — the measurement it depends on was not obtained.

**Corrected instrument (next):** capture the boundary state **inside `notePassBoundary`**, before that turn's emission — a
one-line probe that records `(consumedOrdinal, passContentOrdinal, passBaseOrdinal, publishedOrdinal, dryTurns)` for each
boundary ordinal and asserts the second tuple. Only that can answer, coherently:

1. is `passContentOrdinal` advanced per pass (2 500 → 5 000), or does it stay at the first pass's end;
2. does the pass-1 park get applied at all, and if not, which guard skipped it;
3. is the second tail again inside that turn's own window.

Only if (3) is yes *and* the park is provably skipped does the relocation ("apply after the same wrap turn's emission") have
a measured basis.

## 1e. Boundary probe inside `notePassBoundary` — the authority is NOT correct (2026-10-21)

Captured at entry of `notePassBoundary`, before any boundary mutation, over a three-traversal drive of the short dense shape
(2-bar loop, 2 500 accepted):

| Boundary | `consumedOrdinal` | `passContentOrdinal` | `passBaseOrdinal` | `publishedOrdinal` |
|---|---|---|---|---|
| 0 | 0 | **0** | 0 | (first re-arm pending) |
| 1 | **2 437** | **2 500** | **0** | **2 628** |
| 2 | **2 437** | **2 500** | (unchanged) | **2 628** |
| 3 | **2 437** | **2 500** | (unchanged) | ... |

`probeCount = 4` boundaries over three traversals.

**Answer to the question this probe existed for: no, `passContentOrdinal` does not advance per traversal.** It is set once
(2 500) by the producer's first re-arm and never moves again, and after the first boundary the walk's `consumedOrdinal` is
**stuck at 2 437** — every later boundary records the *same* tuple. So the paged delivery's per-pass content authority is
stale from the second pass onward, and the whole `consumed >= passContent` conditional (attempt 2) is being evaluated against
a value that no longer describes the emitting pass.

**Consequence:** attempt 3 (relocating the re-base to the end of the same wrap turn) is **not** the next move. The relocation
would re-order a decision whose input is wrong. The ordering question can only be diagnosed once the content authority
advances per pass.

**Ordered next steps (measurement first, each with its own probe):**

1. Establish *why* `passContentOrdinal` is not advanced: trace `notePassContent(producedOrdinal)` call sites and the
   producer's re-arm condition (`emitting`), and record the producer's `producedOrdinal` at each pass end. Expected shape:
   2 500, 5 000, 7 500 — the re-arm must publish a new content end per traversal.
2. Establish *why* the walk stops at 2 437 once the base stops moving: whether the 63 tail events are filtered by phase, by
   `servable <= consumed`, or by the index reset. The coherent instrument is the adapter's own `size` return plus the walk's
   `(from, to)` for that turn.
3. Only then decide the boundary ordering (same-turn-end application vs next-turn application) and re-run the long control
   first, requiring 2 500 unchanged.

## 1f. Producer re-arm × adapter walk — the first false transition (2026-10-21)

Static trace of `stepPagedPlaybackForSlot` against the boundary probe's tuples.

**The re-arm's only writer.** `notePassContent` has exactly one call site
(`TrackPlaybackWindowBuild.cpp`, the producer's `sourceComplete()` branch), and it is reached only when:

1. the ring has a free page — `emitting && pagesPublished() + 1 >= kWindowPageCount` returns `ProducerWaiting` *before*
   producing anything (the "one page free" rule that exists to hand the next pass's head over);
2. `pagedWindow.produce(...)` returns anything but `RingFull`/`StaleRevision`;
3. `pagedWindow.sourceComplete()` — i.e. **the whole interval has been produced**.

Then, in that order: `rearmForNextPass()`, `notePassContent(producedOrdinal())`.

**Reconciled with the measured tuples** (boundary 1: `consumed 2 437`, `content 2 500`, `published 2 628`):

- pass 1 produced 2 500 events; `sourceComplete()` fired once; `notePassContent(2 500)`; the re-arm then produced the next
  pass's head, taking `publishedOrdinal` to 2 628 (2 500 + 128 = one page). ✔ matches the tuple.
- at the boundary the adapter's own bound is `servable = min(published 2 628, content 2 500) = 2 500`, so the size it offers is
  `2 500 − 2 437 = **63**` — **the adapter is asked for the tail and does offer it.**
- the walk consumed **0** of those 63 (`consumedOrdinal` stays 2 437 at boundaries 2 and 3), so the pages stay published; the
  "one page free" rule then refuses production (`pagesPublished() + 1 >= kWindowPageCount`), pass 2 can never reach
  `sourceComplete()`, and `notePassContent` is **never called again** — which is exactly why `passContentOrdinal` stays 2 500
  for every later boundary.

**First false transition, named:** not the re-arm's value (it is correct: 2 500) and not the adapter's offer (63 is
correct). It is the **walk dropping the 63 offered events after the index reset** — the tail is offered with phases 1 473…1 535
while the walk's window after the reset no longer contains them. The stranded tail then blocks the producer through the
"one page free" rule, and the stale content end is a *downstream symptom*, not the cause.

So the ordering question is now justified — but its target is precise: the **walk's tail consumption across the index reset**,
not `notePassContent` and not the base arithmetic.

## 1g. The reset ordering — RED target named (2026-10-21)

The wrap turn's own code answers it. On `wrappedBackward` the hot path builds **one** explicit tail frame for the *bridge*
stream — `PlaybackTickFrame tail{&playbackContext, loopLengthTicks − 1, loop.lastTickInLoop, false}`, with the comment
"Consume the old cycle's remaining due events before rewinding either physical cursor" — and then rewinds
(`loop.nextEventIndex = 0`, `projectionCycleStartTick` advance) and runs the turn's normal frame
`{tickInLoop = 0, prevTickInLoop = 1 472, atLoopStart}`.

**There is no equivalent tail frame for the paged stream.** So the walk's window for the wrap turn is the *post-reset*
window `[0, 0]`, not `[1 472, 1 536)`: the 63 events the adapter offers (ordinals 2 437…2 499, phases 1 473…1 535) are
outside it and are never consumed. That is exactly the measured signature — adapter offers 63, `consumedOrdinal` does not
move, the pages stay published, the producer is blocked, `passContentOrdinal` never advances.

**RED target (exact, and no other):** *the wrap rewind must advance the paged stream through the old cycle's remaining due
events before the index reset takes effect, the way the bridge already does.* Nothing else in this packet needs to move — not
`passBaseOrdinal`, not the producer's re-arm, not page depth, not admission. The asymmetry is the defect: the bridge gets a
tail frame, the paged delivery does not.

**What the fix must not do:** emit the tail *after* the new cycle starts (that is the relocation attempt 3, which would put
phases 1 473…1 535 into a phase-0 window), or leave the tail to the next turn (the post-reset window excludes it by
construction). The tail must be consumed inside the wrap turn, before the rewind — the same place and the same mechanism the
bridge uses.

**RED/GREEN for the implementation slice:** the gated oracle's whole-loop case is already the RED (2 437 of 2 500); the
control must stay at 2 500, the tail must emit in the wrap turn at phases 1 473…1 535 before any phase-0 event, the ledger
must stay continuous, and multi-wrap must not replay. Then the gate promotion.

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
