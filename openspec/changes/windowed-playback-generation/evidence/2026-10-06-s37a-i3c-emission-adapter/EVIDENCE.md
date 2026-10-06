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
