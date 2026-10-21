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

## 1h. Tail-drain probe — one fact established, the instrument needs rebuilding (2026-10-21)

The instrumented drain (probe flag only, no behaviour change when off) ran the block on each wrap and recorded inside the
attempt. What survived is decisive on one point and unusable on the rest:

| Fact | Value | Status |
|---|---|---|
| the block **is entered** (`wrappedBackward`, no bridge/view/pair, `admittedForEmission()` true) | 1 / 1 / 1 | **established — the "not entered" explanation is dead** |
| drain attempts over a three-traversal drive | 3 | established |
| tail frame actually passed | `prevTickInLoop = 1 472`, `tickInLoop = 0` | established |
| adapter `size` at the attempt / consumed delta | 0 / 130 | **unreliable** |

**Why the last two are unreliable:** the probe used ten separate test cases, each rebuilding a 2 500-event fixture, and the
run ended in **SIGBUS** after the assertions — so the recorded fields belong to whichever case completed last, and the
`size` mirror in the probe reproduced `min(published, passContent) − base` while the adapter's own rule is
`(passContent > passBase ? passContent : published) − passBase`. A probe that mirrors the adapter incorrectly cannot judge it.

**Rebuilt instrument (next, one fixture, one function):**

1. one fixture, all readings captured in the first wrap only, asserted as separate `TEST_ASSERT`s in a single case (no
   repeated heavy fixtures — that is what exhausted the pool and crashed);
2. read the adapter's own `size` rather than mirroring it: call the stream's size function directly at the attempt and record
   its return;
3. record `(consumedBefore, consumedAfter)` around that single call, plus `(published, passContent, base)` as the adapter sees
   them, **before** any boundary service of that turn can mutate `passContent`.

Only that record can answer whether `[2 437, 2 500)` was available at the exact moment of the drain, which is the
discriminator the fix depends on.

## 1i. The single measurement — size 63, consumed 0 (2026-10-21)

One fixture, one case, first wrap only, no drain (production behaviour unchanged), tuple written from inside the run:

```
captures=1  size=63  consumed=2437  base=2437  published=2628  content=2500  prev=1472  tick=0  emitted=2437  passes=3
```

**Fork resolved: `size = 63`, consumed delta = 0.** At the wrap turn the adapter is handed the old-cycle frame
(`prev = 1 472`, `tick = 0`), the delivery is admitted, the base has already re-based to 2 437 (the current code's immediate
re-base), and the adapter offers exactly the 63 tail events (ordinals 2 437…2 499, phases 1 473…1 535). The walk consumes
none of them: `emitted` stays 2 437 for the whole drive.

So the loss is **not** eligibility, not the frame, not the offer, and not the base arithmetic — it is inside
`advancePlaybackCursor` and its phase filtering for this stream on a wrap frame. That is the function boundary the earlier
probes could not reach, and it is now the only one left.

**Constraints for the next slice (unchanged):** do not change `passBaseOrdinal`, the producer re-arm, page depth or admission.
The RED target is the walk's handling of an offered tail under a wrap frame (`from = 1 472`, `to = 0`, size 63, phases
1 473…1 535) — whatever in `advancePlaybackCursor`'s wrap/phase path drops them.

**Next probe (one run, same technique):** inside the wrap turn, record for the paged stream which indices the walk actually
visits and the phase each `eventAt` returns (e.g. first index visited, its phase, the phase that failed the window test), so
the exact predicate that rejects 1 473…1 535 is named rather than inferred.

## 1j. The rejecting predicate, named — and the drain that now works (2026-10-21)

The walk's filter is `IntervalProjection::didPlaybackEventCross(frame.atLoopStart, frame.prevTickInLoop, evTick,
frame.tickInLoop)`, with an early `break` when an event's phase is greater than `frame.tickInLoop`. On the wrap turn the
normal frame is `{tickInLoop = 0, prevTickInLoop = 1 472}`, so an offered tail event at phase 1 473…1 535 cannot pass the
window and the walk breaks before consuming it — **that is the predicate that loses the 63 events**, and it is exactly what
the bridge avoids with its own tail frame.

**Drain implemented with the old-cycle tail frame *and its own cursor* (`uint16_t` starting at 0):** the paged adapter indexes
from `passBaseOrdinal`, so passing the merged cursor (attempt 1) never walked the tail. Measured: **2 437 → 2 563** on the
whole-loop shape — the 63 tail events are now emitted — with the 512-bar control still **2 500**. So the drain is correct.

**Remaining defect (measured, not inferred):** 2 563 = 2 500 + 63, i.e. the tail is emitted **twice**: the adapter's `size` is
`servable − passBaseOrdinal` and ignores `consumedOrdinal`, so after the drain the main frame's fresh cursor re-walks the same
ordinals. Attempting to close that by moving `passBaseOrdinal` to the live consumed value **after** the drain measured
**2 758** — worse, because the next traversal then starts from a base the producer has not reached. So the duplicate needs a
different lever than the base.

**Candidate (not implemented):** after the drain, seed the **main walk's cursor** past the drained ordinals instead of touching
the base — the main frame and the drain share the adapter's index space, so the cursor is the authority that must reflect the
drain. That change is local to the wrap turn, leaves `passBaseOrdinal`, the producer, page depth and admission untouched, and
must show 2 500 on the short shape and 2 500 unchanged on the 512-bar control.

## 1k. The fix works — and costs 192 B of ITCM (reverted, not shipped) 2026-10-21

Implemented in the wrap turn: **drain the paged tail with the old-cycle tail frame and its own cursor, then seed the main
walk's cursor past the drained ordinals** (`loop.nextEventIndex = drained` after the reset), gated to whole-loop intervals
(`pagedInterval.lengthTicks >= loop.loopLengthTicks`), because an interval inside a long loop is already covered by its own
interval-start frame.

| Case | Result |
|---|---|
| dense 512-bar control | **2 500 — unchanged** |
| dense 2-bar whole-loop, 2 traversals | **5 000 → 2 500 per traversal, element-wise exact** |
| existing `test_3_7a_i5_a_rootless_demoted_slot_is_served_from_the_ring_pass_exact` | passes (2 500) |
| existing `test_3_12_the_paged_path_serves_a_restored_over_l_interval_at_each_due_tick` | passes |
| gated suite with the oracle **in the default gate** | **233/233** |

So the mechanism is proven: drain + cursor seed, no base change, no producer change, no admission change. Without the
whole-loop gate, the two existing paged fixtures moved (2 244 and 364 skipped) — recorded so the gate is not mistaken for
decoration.

**Blocker: ITCM.** `teensy41-capture-serial` measured `code 392 408 + padding 808` (was `392 216 + 1 000`) and the
qualification image `392 568 + 648` (was `392 376 + 840`) — **+192 B of ITCM, below the 840 floor.** The hot path is the
ITCM TU, so the drain must become flash-resident before it can ship.

**Next increment (mechanical):** move the drain into a `TRACK_COLD_MEM` helper (the window-build TU is itself ITCM-routed, so
the helper must be explicitly cold) taking `(slotIndex, currentTick, prevTick)` and rebuilding its own
`ProjectionContext`/frame/policy inside; the call site then costs a call plus the cursor seed. Re-run the 512-bar control
first, then the short shape, then both builds against the floor.

## 1l. Cold helper attempt — +192 B becomes +16 B, still 16 short (reverted) 2026-10-21

The drain was moved into `Track::drainPagedTailBeforeWrap(slotIndex, currentTick, prevTick)` with `TRACK_COLD_MEM` in the
window-build TU (explicitly cold, since that TU is ITCM-routed): it rebuilds its own `ProjectionContext`, tail frame and
policy, passes `nullptr` for the jam filter (the tail drain needs none), and returns the ordinals it consumed; the hot path
then seeds `loop.nextEventIndex` after the reset.

| Image | Baseline | Inline drain | Cold helper |
|---|---|---|---|
| `teensy41-capture-serial` | `392 216 + 1 000` | `392 408 + 808` (FAIL) | **`392 232 + 984` (PASS)** |
| `-window-qualification` | `392 376 + 840` | `392 568 + 648` (FAIL) | **`392 392 + 824` (FAIL, 16 B short)** |

So the helper pays for itself: **+192 B → +16 B**. The default image passes with 144 B of headroom; the qualification image
is **16 B** below the 840 floor, so it cannot ship yet.

**Remaining work is a 16-byte ITCM diet in the wrap turn**, not a design question — candidates: fold the two cursor-seed
stores into one, drop the local counter by returning the drained count straight into `loop.nextEventIndex` at the single
`isActive` reset site, or give the seed a single cold setter. Then: 512-bar control 2 500, short shape 2 500/traversal,
both builds, the six cases, gate.

## 1m. The 16 B are invariant across call-site shapes — map-file diet next (2026-10-21)

Four call-site shapes were built and each measured **exactly** `code 392 392 + padding 824` on
`teensy41-capture-serial-window-qualification` (baseline `392 376 + 840`):

1. helper returns the drained count, hot path keeps a local and two seed stores;
2. helper returns the count, one assignment after the reset block;
3. helper is `void` and writes `loop.nextEventIndex` itself;
4. one cold call owns both the reset and the drain (the two branch stores removed).

**Invariant +16 B.** That rules out call-site shape as the cause: the extra bytes are not the marshalling of arguments or the
assignment, and the cold helper's own body is correctly flash-resident (the +192 B inline form is gone). The likely sources are
a small ITCM-resident symbol the declaration pulls in (e.g. an instantiation/thunk in the ITCM-routed TU) or an alignment step
in the wrap block.

**Next step (cheap and decisive):** diff the two link maps (`arm-none-eabi-nm --print-size --size-sort` on both ELFs) for the
qualification image, with the cold helper applied. The symbol that grew by 16 B names the cause directly; diet that symbol, not
the call site. The default image already passes at `392 232 + 984` with 144 B of headroom, so no floor rebase is needed —
only this 16 B.

## 1n. The 16 B are a long-call veneer — the cold helper is not the lever (2026-10-21)

Link-map diff of the qualification ELF, baseline vs cold helper:

| Symbol / section | Baseline | With helper |
|---|---|---|
| `.text.itcm` | 391 520 | **391 536 (+16)** |
| `Track::playCommittedLoopMidi` | `0x8b6` | `0x8ac` (−8) |
| `Track::drainPagedTailBeforeWrap` | — | `0x124` at **`0x6007f6fc`** (flash) ✔ |
| `___ZN5Track24drainPagedTailBeforeWrapEhmm_veneer` | — | **`0x8` at `0x5f888`** (ITCM) |

**The helper itself is correctly flash-resident and cheaper than the inline form by 168 B; the 16 B are the linker's
long-call veneer** needed for a `BL` from ITCM (address 0) to flash (0x6007f6fc), plus alignment. That is why every call-site
shape measured the same +16: the veneer is per-callee, not per-call-shape, and no amount of argument or store folding can
remove it.

**Therefore the diet is "no new call from ITCM", not "a smaller call site".** Two forms achieve that and both leave the hot
path byte-identical to baseline:

1. **Fold the drain into an existing flash entry.** `servicePagedPassBoundaryForTurn` is already called from the hot path on
   every turn of a bound delivery; it computes the same wrap facts. The drain can live there, with the walk index seeded
   through a single `uint16_t` field that fits existing padding (recorded cost: one store in a flash function).
2. **Make the adapter skip already-consumed ordinals**, so no seed is needed at all: `size` returns `servable − max(passBase,
   consumed)` and `eventAt` indexes from the same `max`. Both functions are already flash-resident, so this costs **zero ITCM**
   — but it changes adapter semantics for every paged case, so the 512-bar control must be re-measured first.

Form 1 is preferred (localised, no semantic change to the adapter); form 2 is the zero-cost option if form 1's field cannot fit.

## 1o. Form 1 is ITCM-free but did not fire (2026-10-21)

The drain was moved **inside `servicePagedPassBoundaryForTurn`** (the flash function the hot path already calls on every turn of
a bound delivery), with a file-local `TRACK_COLD_MEM` helper and the base moved to the live consumed ordinal afterwards.

| Check | Result |
|---|---|
| qualification `.text.itcm` | **`392 376 + 840` — byte-identical to baseline (0 B)** |
| dense 512-bar control | 2 500 |
| dense 2-bar whole-loop | **2 437 — unchanged, the drain never emitted** |

So the ITCM question is solved for this form (the veneer is gone because there is no new call from the ITCM edge), but the drain
does not run or does not emit at the wrap turn. Candidate causes, in order: `runtime.pagedEmission.admittedForEmission()` is
false at that instant (the wrap's own `notePassBoundary` has not run yet in that turn), the wrap predicate computed inside the
service (`didDisplayPlayheadWrapBackward` with `tickInLoop` recomputed there) differs from the hot path's, or the base had
already moved so `size` was 0.

**Next increment:** instrument the service's own decision (wrapped flag, admitted flag, `size`, base, consumed) at the wrap
turn — one file-tuple again — then either move the drain after the `notePassBoundary` call inside the same flash function
(preferred: the admission is judged there) or make the predicate exact.

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
