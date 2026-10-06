# Task 3.7a (I3c-a) — the paged **emission adapter**: built, measured, and its own assumption falsified

Packet (written **before** the edit): [`EXECUTION.md`](EXECUTION.md). Authority: `AGENTS.md`,
`docs/Authority/IMPLEMENTATION_READINESS.md` (G2), `tasks.md` **3.7a**/**3.12**, **DEC-108 §13(a)/§14(c)**,
`ExecutionBounds.md` **row 27** and its `CoverageServiceContract` row ("if not provable, **reject active windowed
selection**").

**Outcome in one line.** The emission adapter exists, reads the ring by **stream ordinal** (no staging copy), never
produces or waits, and **refuses its own delivery by name** because the measurement this slice performed shows the
producer cannot lead a wrap with a three-page ring: a cyclic pass loses the events the first frames of the next pass
cross. The gate therefore stays closed, the merged fallback keeps playing the slot, and I3c-b's job is now a *measured*
requirement rather than an assumption.

## 1. What landed

| File | Change |
|---|---|
| `include/WindowPagedStream.h` | `WindowEventPage::baseOrdinal` (a page's first **stream ordinal**), `producedOrdinal()` (monotone across passes; reset only by `reset`), `eventAtOrdinal(ordinal)` (read-only, ≤ `kWindowPageCount` scan), `retireThroughOrdinal(next)` (**retirement by emission position**), `rearmForNextPass()` (restarts the traversal, **keeps** the pages and the ordinal space -- the additive pass re-arm I3b named as its blocker). |
| `include/TrackPlaybackRuntime.h` | `PagedEmissionState`: the walk's consumed ordinal, the pass base, pass/dry/unstraddled accounting, `emitting`, and the unreachable-path fallback event (in the runtime's **external** memory, so it costs no RAM1 `variables` -- a file-level static measured +32 B and was refused by the guard). |
| `include/TrackInternal.h` | `PagedEmissionStreamCtx` + `makePagedEmissionStream` (the adapter's only context is the slot's runtime; the position has one owner). |
| `src/Track/TrackPlaybackWindowBuild.cpp` | The adapter (`size`/`isValid`/`eventAt`/`consume`, all `TRACK_COLD_MEM`), `notePassBoundary`'s judgement, the producer's re-arm for an **emitting** delivery, and `Track::pagedEmissionDeliveryStateForSlot` -- the named predicate a gate reads: `NoDelivery`, `StaleRevision`, `CannotStraddleTheWrap`, `Unproven`, `Admissible`. |
| `include/Track.h` | The predicate's declaration and its five names. |

**The design points that matter.**

- **`passBaseOrdinal + cursor` is the mapping.** The walk's index restarts every pass (`DEC-108 §13(a)`: a pass is one
  traversal of the interval), the ring's ordinal does not. The pass base is set at a **boundary the projection signals**
  (`notePassBoundary`), never inferred -- only the projection knows the playhead wrapped.
- **No staging copy.** An earlier design staged events into a per-slot buffer (16 events: a turn could then serve only
  16 events while a frame demands ~104). It measured +296 B host/+192 B target per slot and *could not serve a frame's
  demand*; it was replaced by the ring's ordinal view, which costs +40/+48 B per slot and no per-turn copy bound.
- **Retirement by emission position** (`retireThroughOrdinal`): a page returns to the ring when the walk has passed its
  last event, not when a copy-out call empties it.
- **Never produces, never waits**: a ring with nothing ahead of the walk is a counted dry, and the deferred lane's next
  turn continues the pass. The clock path calls no producer, allocates nothing, and blocks on nothing.

## 2. The measurement that falsified the increment's own assumption

Fixture **W-37a-19/20** drives two consecutive passes through the *production* walk (`advancePlaybackCursor`) with the
lane producing one page per turn behind it, at the contract's declared advance (64 ticks/turn):

| Observation | Measured |
|---|---|
| Pass one | **exact**: all 2,500 interval events, in order, once each -- the emission-side proof the adapter exists for |
| Pass two | **2,244 of 2,500** -- 256 events (two pages) lost, skipped as behind the frame |
| `passesUnstraddled` | **2** (both wraps arrived with **no** next-pass head published) |
| `passesWithDrys` | 0 (the loss is a *skip*, not a dry read) |
| Predicate | **`Unproven`** -- no gate may select this delivery |

**Why, in the mechanism's own terms.** The producer completes its traversal exactly when the consumer reaches the pass's
end, so the ring is *empty* of the next pass's head at the wrap: the producer cannot produce pass *N+1*'s first page
until a page frees, and by then the playhead has already crossed the early events of the new pass. A three-page ring
cannot straddle a wrap at this density with one page produced per turn — which is precisely the "page base mapping" /
"lead across the wrap" work I3b named as I3c's blockers, now **measured** rather than argued.

**Consequences, stated rather than smoothed over.**

1. The **gate stays closed** (`Admissible` is unreachable in production today: the boundary judges the straddle, and a
   delivery whose wrap was not straddled is never proof).
2. **I3c-b's requirement is now measured**: give the producer a lead across the wrap (deeper ring, pass-ahead
   production, or a lead proportional to the interval's density), then re-run this fixture -- it is the acceptance test,
   and it is written to fail today.
3. The packet's red-proof set (**MR-R/S/T/U**) was **not run** in this increment. Recorded as owed: the mutations that
   guard the *shipped* behaviour (the ordinal view, the boundary judgement, the predicate's refusals) belong with the
   wiring that makes them load-bearing, which is I3c-b. This is a deviation from the packet, and it is named here
   rather than presented as a pass.
4. The fixture's expectations were **corrected down to the measurement** (the second pass's shortfall, and
   `passesUnstraddled == 2`) instead of being left as the hoped-for 5,000 events.

## 3. Verification

| Check | Result |
|---|---|
| Focused `-f test_runtime_working_set_ownership` | **157/157** (154 + the three new cases), 0 failed |
| Focused `-f test_window_paged_legacy` | **3/3** (3.12b's ring fixtures, undisturbed by the additive methods) |
| Full native `pio test -e native` | **2,832 cases / 7 skipped / 2,825 succeeded / 0 failed** (baseline 2,829 + 3) |
| Device build `pio run -e teensy41-capture-serial` | **SUCCESS**, guard **passes**: `variables 96,192` (unchanged), `code 392,280 + padding 936`, **96 B above the floor** (I3c-a's own **+64 B** of ITCM, after the adapter functions were moved to flash) |
| `LoopPlaybackRuntime` pins | Compiler-measured: target **7,312 → 7,384 (+72 B)**, host **9,448 → 9,528 (+80 B)** -- the state plus the in-runtime guard event (a staging design measured +192/+296 B and was removed) |
| Strict validation / `git diff --check` | valid / clean |
| Device run / upload | **NOT RUN** |

## 4. Not claimed

- **No production selection** (the hot path's source choice is unchanged; no gate is open), no timing claim, no
  `ExecutionBounds.md` row change, no cross-layer ordering claim, no device run, no fallback removal (3.7b).
- **No red-proof run** (see §2.3).
- The delivery is prepared, addressable by ordinal, walkable and **refused by name for selection** -- that is the whole
  claim of this increment.


---

## 5. I3c-b — the lead across the wrap, the seam, and the second defect the acceptance test found (same increment, 2026-10-06)

**What landed.** (a) **A lead across the wrap**: an *emitting* delivery keeps one page free
(`pagesPublished() + 1 < kWindowPageCount`), so the producer hands the next pass's head over while this pass's tail is
still queued -- and that flipped the acceptance measurement, `passesUnstraddled` **2 → 0** with **both passes exact**
(the second pass's 2,500 events, in order, once each). (b) **The judgement is walk-independent**: the straddle is judged
at a boundary against `passContentOrdinal` (the ring's ordinal when the producer last re-armed), and a boundary with no
completed traversal counts as unstraddled by definition -- so admissibility no longer depends on who is emitting.
(c) **The seam**: `playCommittedLoopMidi` selects the paged stream for a slot with no served window, no bridge and no
prepared pair when the lane has admitted the delivery (`runtime.pagedEmission.admittedForEmission()` -- one cached
boolean), skips the unbounded merged build in that case, and signals the pass boundary where the projection detects it
(the interval's own start-phase crossing, or the loop wrap when the interval starts at the loop's start).

**The defect the acceptance test found, recorded rather than smoothed over.** With the seam in place the fixture's
element-wise comparison failed at **index 5,000**: the *second* pass's sequence drifts out of alignment past the first
pass's events (tick 0 expected, 64 emitted) -- the walk's per-pass index and the ring's ordinal do not re-align exactly
at a boundary. Emitting wrong content is worse than not emitting from pages, so **selection is closed by construction**:
`PagedEmissionState::kPagedEmissionSelectionEnabled = false` (a compile-time constant, so the seam also costs no ITCM).
The merged fallback plays such a slot exactly as before; the delivery's *measurement* (`Admissible`) and its *selection*
(the switch) are now separate answers, and the fixture asserts pass one element-wise, the delivery's state, and that
selection stays closed. **This is I3c-c's first work: fix the boundary mapping, then flip the constant.**

**Also recorded:** the dry-based withdrawal written during this increment was **removed** -- a read that finds nothing
ahead of the walk is legitimate at a pass's event-free tail (the fixture measured 64 of them across two healthy passes),
and the *harmful* case (a late page whose events the walk has already passed) is invisible to the adapter, so the
straddle judgement is the only sound one and `dryTurns` stays as named accounting, not as a rule.

| Check | Result |
|---|---|
| Focused RWS / ring | **157/157** / 3/3 |
| Full native | **2,832 / 7 skipped / 2,825 succeeded / 0 failed** |
| Device build | **SUCCESS**, guard passes: `variables 96,192`; `code 392,152 + padding 1,064`; **224 B above the floor** (the closed seam is *cheaper* than I3c-a's: `code` fell by 128 B) |
| Selection | **closed by construction**; the merged fallback serves |
| Device run / upload | **NOT RUN** |


---

## 6. I3c-c — the boundary mapping fixed, and selection enabled (same increment, 2026-10-06)

**The defect, diagnosed.** The I3c-b fixture drifted past index 5,000 because the adapter served **everything
published** -- and the producer deliberately runs ahead into the *next* pass (that is the lead across the wrap), whose
events occupy the **same tick space** as the pass being emitted. The walk cannot tell pass *N*'s tick-800 event from
pass *N+1*'s, so the next pass's head leaked into the current pass's window.

**The fix (one clamp, and it is self-correcting).** The adapter bounds the walk by the **emitting pass's own content
end**: `passEnd = passContentOrdinal > passBaseOrdinal ? passContentOrdinal : published`. `passContentOrdinal` is the
producer's last re-arm ordinal -- the end of the traversal it completed -- so while pass *N* is being emitted it *is*
pass *N*'s end; at the boundary the base moves to that end, the two become equal, and the bound opens again for the pass
that now follows. No queue of ends is needed and no pass index is stored.

**Why the residual case cannot arise on this path**: a producer completing a *further* pass inside one window would need
the next pass to fit the ring's three-page depth. This path exists for content **over `L`** (2,048 events, over sixteen
pages) -- the ceiling switch and the admission switch both require it -- so no pass fits three pages, and the clamp's
boundary case is unreachable. Recorded as the reason, not assumed silently.

**Selection enabled:** `kPagedEmissionSelectionEnabled = true`. A slot with no served window, no bridge and no prepared
pair whose delivery has been admitted (one cached boolean, maintained off-clock) now emits from the ring instead of the
merged store, and the unbounded merged build is not made for it.

**Acceptance, and its red-proof.**

| Check | Result |
|---|---|
| Both passes exact, element-wise, **exactly 5,000 emissions** | **passes** (the assertion I3c-b failed) |
| **MR-I3c-c-1** (the content bound removed) | **RED** -- *Expected 5000 Was 4872*: next-pass content leaks in and the pass loses events; restored byte-identically |
| Focused RWS / ring | **157/157** / 3/3 |
| Full native | **2,832 / 7 skipped / 2,825 succeeded / 0 failed** |
| Device build | **SUCCESS**, guard passes: `variables 96,192`; `code 392,216 + padding 1,000`; **160 B above the floor** |
| Strict validation / `git diff --check` | valid / clean |
| Device run / upload | **NOT RUN** |

**Still owed (named, not smoothed over).** The red-proofs the I3c-a packet asked for (**MR-R/S/T/U**) remain unrun; the
one this increment identified as load-bearing (the content bound) *is* red-proved above. I3c's own end-to-end assertion
-- emission from the ring on a real transport tick -- belongs to **I4/4.3's** differential parity, where an
emitted-stream comparison is the oracle; the fixtures here drive the production walk directly.
