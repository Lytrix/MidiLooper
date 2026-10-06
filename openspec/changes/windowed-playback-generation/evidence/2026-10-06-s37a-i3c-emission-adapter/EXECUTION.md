# Task 3.7a — increment I3c-a: the paged **emission adapter** (hot-path source, proven off-clock)

Execution packet for **I3c-a** of slice 3.7a, written **before** the production edit it authorizes. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), the ladder
([`../2026-10-06-s37a-i3b-paged-legacy/EXECUTION.md`](../2026-10-06-s37a-i3b-paged-legacy/EXECUTION.md)),
`tasks.md` **3.7a**/**3.12**, **DEC-108 §13(a)/§14(c)**, `ExecutionBounds.md` **row 27** (the lane owner: caps declared,
bounds unmeasured) and its `CoverageServiceContract` row ("if not provable, **reject active windowed selection** … use
full-loop prewarm or STOPPED-only work").

## The finding this slice is built on (measured in the code, not assumed)

The hot path emits through `advancePlaybackCursor` over a `PlaybackEventStream` (`size` read **once per turn**, then
`eventAt`/`eventPhase` per index, `consume` per advanced index — `src/Utils/PlaybackCursorAdvance.cpp`). Three facts fix
the paged adapter's shape:

1. **The walk's index is per-pass.** `DEC-108 §13(a)`: "**Pass = one traversal of the affected interval in the playback
   projection**"; the wrap rewinds `loop.nextEventIndex`, so the walk's index space restarts every pass while a paged
   consumer's position is monotone.
2. **A dry ring is not a skip, but a late page is.** The walk consumes an event whose phase is behind the frame
   (`cursor++`, no emission). So a pass whose first page arrives *after* the wrap loses the events in between: the
   producer must **lead the wrap**, which a 3-page ring can only do by producing the next pass's head while the current
   pass's tail is still queued.
3. **`reset()` restarts the sequence space**, so a consumer cursor that continues across a re-arm would look for a
   sequence the ring no longer holds. The minimal honest fix is an **additive** ring method that re-arms a pass while
   **keeping** the sequence numbering and the revision: `rearmForNextPass()`.

So: the ring needs one additive pass re-arm, the runtime needs the adapter's position/state, and the adapter maps
**walk index → ring ordinal** by learning the pass base at each rewind (`passBase = ringOrdinal` at the wrap, which is
exactly the next pass's first ordinal because the producer re-traverses the same interval).

## Identity and scope

- **What I3c-a adds (this increment):**
  1. `WindowPagedStream::rearmForNextPass()` — clears the pages and the traversal cursor, keeps `sourceRevision_` and
     `nextProduceSequence_`, so one ring serves pass after pass with a **continuous** sequence space.
  2. The runtime's **paged emission state** (`PagedEmissionState`: the staged window, its walk-index base, the ring
     ordinal, the pass base, the dry/refill accounting) — per slot, in the externally allocated runtime.
  3. `PagedEmissionStreamCtx` + `makePagedEmissionStream(ctx)` in the cold TU: `size` = staged availability (one bounded
     refill per turn through the I3b consumer entry, **never a produce**), `eventAt` = a staged read, `eventPhase` = the
     shared `IntervalProjection::playbackEventPhase`, `consume` = advance the ring ordinal, and a rewind rebase when the
     walk's index moves behind the stage.
  4. `Track::pagedEmissionSustainablyServesSlot(slot)` — the **sustain predicate** the gate will read: the content's
     maximum events per declared advance window (`kPlaybackViewPermittedAdvancePerTurn`) must fit one page, measured by
     the producer's own traversal (so it costs the traversal nothing extra), plus the named refusals
     (`CannotStraddleTheWrap` for an interval with no re-arm room, `NoPages`/`StaleRevision`).
- **Primary files:** `include/WindowPagedStream.h`, `include/TrackPlaybackRuntime.h`, `include/TrackInternal.h`,
  `src/Track/TrackPlaybackRuntime.cpp`, `src/Track/TrackPlaybackWindowBuild.cpp`,
  `test/test_runtime_working_set_ownership/…`.
- **Preserved:** the merged fallback, the served window, the fixed-view/bridge/prepared-pair sources, every existing
  refusal name, the hot path's *selection* (unchanged in this increment — see below), and all of I3b's semantics.
- **Deliberately NOT in I3c-a (named, next increment I3c-b):** the **production selection** (routing a slot's emission
  through the paged stream) and the gate that opens it. Two reasons, both recorded rather than smoothed: (a) row 27's
  contract row requires the *proof* — "if not provable, reject active windowed selection" — and the sustain predicate
  this slice adds is the input to that proof, not the proof itself; (b) the ITCM budget is **160 B above the floor**
  after I3b, and the selection's own cost must be measured before it is spent.
- **Non-goals:** no `ExecutionBounds.md` row change, no device run, no timing claim, no fallback removal, no change to
  the walk, no new lane or scheduler.

## Oracle fixed before behavior edits

| Case | Initial state | Ordered actions | Expected observation | Counterexample that must fail |
|---|---|---|---|---|
| **W-37a-19** the adapter emits a pass's events through the *production* walk, in order, exactly once | a prepared paged ring for one pass (I3b fixtures' shape) + the adapter over it | drive `advancePlaybackCursor` with controlled frames across the pass | the emitted sequence equals the interval's effective content in order, no duplicate/missing; `size()` is read once per turn and the refill is ≤ one stage per turn; `consume` advances the ordinal once per index the walk advanced | an adapter that re-reads `size` per event (unbounded work), skips an index, or copies a whole page per event |
| **W-37a-20** a pass boundary is **straddled**, not missed | production continues while the consumer drains (the I3b lane turn), the ring re-arms for the next pass before it is drained empty | drive the walk across the wrap (two consecutive passes) | every event of both passes is emitted (no event of the second pass is skipped as "behind the frame"), the sequence space is continuous across `rearmForNextPass()`, and the pass base moves exactly once per wrap | a `reset()`-based re-arm (the consumer's cursor then addresses the wrong sequence) or an adapter that keeps its walk-index mapping across a wrap |
| **W-37a-21** the adapter never produces | a ring whose next page is not yet produced | drive the walk while the consumer is dry | the walk emits what is staged, the ring is **not** produced, no allocation happens, and the dry is counted; the deferred lane's next turn is what continues it | producing inside the clock path, or blocking/waiting for a page |
| **W-37a-22** the sustain predicate names both sides | intervals below and above `kPlaybackViewPermittedAdvancePerTurn`-window density | ask the predicate | below the bound: admissible (the producer keeps up at the declared rate); above it: **refused by name**, and an interval with no re-arm room is `CannotStraddleTheWrap` | a predicate that accepts any content (the wrap would then be missed) or that refuses everything |

**Invariants asserted, not assumed:** the merged fallback and the fixed-view/bridge/pair sources are unchanged; no
allocation on the walk's path; `loop.nextEventIndex` remains a `uint16_t` per-pass index; the runtime's new state is
per-slot external memory with the size pins re-based in the same commit; no RAM1 `variables` growth.

## Red-proofs (I3c-a)

| ID | Mutation | Must fail |
|---|---|---|
| **MR-3.7a-R** | the rewind rebase removed (the adapter keeps its mapping across a wrap) | W-37a-20 |
| **MR-3.7a-S** | the refill moved out of `size` into a per-event path (unbounded per-turn work) | W-37a-19 (the per-turn accounting fixture) |
| **MR-3.7a-T** | the sustain predicate accepted unconditionally | W-37a-22 |
| **MR-3.7a-U** | `rearmForNextPass` replaced by the `reset`-based re-arm | W-37a-20 |

Each mutation is reverted and the files restored byte-identically (sha256 recorded); a mutation that does not fail its
fixture is recorded as a non-red-proof rather than reworded.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership` (the new fixtures plus the lane's existing ones),
  and `test_window_paged_legacy` (3.12b's ring fixtures, unchanged — the additive method must not disturb them).
- Broad: `pio test -e native` — the baseline is **2,829 cases / 7 skipped / 2,822 succeeded / 0 failed**.
- Build/RAM1: `pio run -e teensy41-capture-serial`; the approved line is `variables 96,192` /
  `code 392,216 + padding 1,000` (160 B above the floor). `LoopPlaybackRuntime`'s two size pins are re-based in this
  commit if the new state moves them (compiler-measured on both ABIs).
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`.
- Device: **NOT RUN**.
- Evidence path: `evidence/2026-10-06-s37a-i3c-emission-adapter/`.

## Completion record

**Filled after the run** (2026-10-06). Detail: [`EVIDENCE.md`](EVIDENCE.md).

- **Scope:** as written, except that the adapter is shaped **without a staging buffer** -- the first implementation staged
  16 events per slot (measured +192 B target / +296 B host per slot, a file static's +32 B of RAM1, and it could not
  serve a frame's demand of ~104 events per turn at the declared advance); it was replaced by a read-only **ordinal view
  of the ring** (`baseOrdinal`/`producedOrdinal`/`eventAtOrdinal`/`retireThroughOrdinal`), which is also what the
  packet's "stream index -> page base" blocker asked for. Cost: +48 B target / +40 B host.
- **Oracle:** W-37a-19 and W-37a-21 green, including "the adapter never produces" (`pagedPagesProduced` unchanged across
  walk-only turns); W-37a-22's whole-loop case green. **W-37a-20 is the measured negative**: the wrap is **not**
  straddled with this ring (2 lost pages on the second pass, `passesUnstraddled == 2`), and the fixture asserts that
  measurement -- the acceptance test for I3c-b -- instead of the hoped-for 5,000 events.
- **Red-proofs: NOT RUN.** MR-R/S/T/U are owed to I3c-b, where the wiring makes them load-bearing. Named as a deviation
  in the evidence, not presented as a pass.
- **Verification:** focused RWS **157/157**, ring **3/3**, full native **2,832 / 7 skipped / 2,825 succeeded / 0 failed**,
  capture build **SUCCESS** with the guard passing (`variables 96,192` unchanged; `code 392,280 + padding 936`; 96 B above
  the floor; I3c-a's own +64 B of ITCM, after the adapter's functions were moved to flash and the guard's event out of
  RAM1). Pins re-based compiler-measured (target **7,384**, host **9,528**). **Device NOT RUN.**
- **Fixed during the run:** the RAM1 guard **failed** on the first device build (`variables 96,224`, `padding 680 < 840`)
  -- +32 B of RAM1 from a file static and +320 B of ITCM from adapter functions that were not flash-marked. Both were
  moved (the guard event into the runtime's external state, the functions into flash) and the guard passes.
